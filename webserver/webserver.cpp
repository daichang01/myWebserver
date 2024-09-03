#include "webserver.h"

WebServer::WebServer() {
    users = new http_conn[MAX_FD];

    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char * )malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;

}

/**
 * @brief 初始化Web服务器
 * 
 * 该函数用于在Web服务器对象创建后，初始化服务器的各种配置参数。这些参数不仅包括服务器的网络配置，
 * 如端口号，还涉及数据库连接信息，日志设置，以及服务器的运行模式。合理配置这些参数对服务器的稳定运行至关重要。
 * 
 * @param port 服务器监听的端口号
 * @param user 数据库用户名
 * @param passWord 数据库密码
 * @param databaseName 数据库名称
 * @param log_write 日志记录模式
 * @param opt_linger 是否启用linger选项
 * @param trigmode 事件触发模式
 * @param sql_num 最大SQL连接数
 * @param thread_num 线程池中的线程数量
 * @param close_log 是否关闭日志
 * @param actor_model 服务器的actor模型
 */
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                    int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port=  port;
    m_user=  user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

// 事件循环函数，处理所有事件，包括新客户端连接、读写事件等
void WebServer::eventLoop() {
    // 用于标识是否超时，用于定时器处理
    bool timeout = false;
    // 用于标识是否停止服务器
    bool stop_server = false;

    // 主循环，不断轮询和处理事件，直到stop_server为true
    while (!stop_server) {
        // 调用epoll_wait等待事件发生，m_epollfd为epoll句柄，events为事件数组，MAX_EVENT_NUMBER为数组大小，-1表示不超时
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // 如果epoll_wait返回值小于0且不是因为中断引起，则视为epoll出错
        if (number < 0 && errno != EINTR ) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 遍历发生的事件数组，处理每一个事件
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            // 如果事件对应的socket为监听socket，则有新的客户端连接请求
            if (sockfd == m_listenfd) {
                bool flag = dealclientdata();
                // 如果处理客户端数据失败，则跳过当前循环，继续等待其他事件
                if (false == flag) 
                    continue;
            }
            // 如果事件为挂起读、连接关闭或错误，则处理对应的定时器
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 如果事件对应的socket为管道的读端且有读事件，则处理信号
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                // 如果处理信号失败，则记录错误日志
                if (false == flag) {
                    LOG_ERROR("%s", "deal client data failure");
                }

            }
            // 如果事件为读事件，则处理读操作
            else if (events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            }
            // 如果事件为写事件，则处理写操作
            else if(events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }

        }
        // 如果有超时发生，则处理定时器，并记录信息
        if (timeout) {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}

// 初始化Web服务器的事件监听
void WebServer::eventListen() {
    // 创建监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0); // 确保套接字创建成功

    // 根据m_OPT_LINGER的值设置套接字的linger选项
    if (0 == m_OPT_LINGER) {
        struct linger tmp =  {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) {
        struct  linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 准备绑定的地址结构
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    // 设置套接字选项，允许地址复用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET,SO_REUSEADDR, &flag,sizeof(flag));

    // 将套接字绑定到地址
    int ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0); // 确保绑定成功

    // 开始监听连接
    ret = listen(m_listenfd, 5);
    assert(ret >= 0); // 确保监听成功

    // 初始化utils工具类
    utils.init(TIMESLOT);

    // 创建epoll事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1); // 确保epoll创建成功 f

    // 将监听套接字添加到epoll事件表
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // 创建管道用于epoll的边缘触发
    ret= socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1); // 确保管道创建成功
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 添加信号处理
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // 设置定时器
    alarm(TIMESLOT);

    // 为工具类Utils设置管道和epollfd
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

/**
 * @brief 日志写入函数
 * 

 */
void WebServer::log_write() {
    if (0 == m_close_log) {
        //初始化日志
        if (1 == m_log_write) {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

/**
 * 初始化数据库连接池和用户表
 * 
 * 本函数负责初始化数据库连接池和用户表，以确保Web服务器能够正常地与数据库交互
 * 首先，它创建并配置了一个数据库连接池，然后，它初始化了一个用户表，该表将使用数据库连接池进行操作
 * 
 * 参数：
 * - m_user: 数据库用户名
 * - m_passWord: 数据库用户密码
 * - m_databaseName: 要连接的数据库名称
 * - m_sql_num: 数据库连接池中的初始连接数量
 * - m_close_log: 是否关闭日志功能的标志
 */
void WebServer::sql_pool() {
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

/**
 * 设置监听和连接的触发模式
 * 
 * 根据m_TRIGMode的值设置m_LISTENTrigmode和m_CONNTrigmode的值
 * m_TRIGMode的值指定了监听和连接事件的触发模式组合
 * m_LISTENTrigmode用于监听事件，m_CONNTrigmode用于连接事件
 * 
 * 0: m_LISTENTrigmode和m_CONNTrigmode都为0，表示使用LT+LT模式
 * 1: m_LISTENTrigmode为0，m_CONNTrigmode为1，表示使用LT+ET模式
 * 2: m_LISTENTrigmode为1，m_CONNTrigmode为0，表示使用ET+LT模式
 * 3: m_LISTENTrigmode和m_CONNTrigmode都为1，表示使用ET+ET模式
 */
void WebServer::trig_mode() {
    // LT + LT 模式
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET 模式
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT 模式
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET 模式
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}
#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "../http/http_conn.h"
#include "../threadpool/threadpool.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

// WebServer类定义了一个Web服务器的基础结构和功能
class WebServer {
public:
    // 默认构造函数，初始化服务器
    WebServer();
    
    // 析构函数，释放服务器占用的资源
    ~WebServer();

    /**
     * 初始化服务器参数
     * @param port 服务器监听的端口
     * @param user 数据库登陆用户名
     * @param passwd 数据库登陆密码
     * @param databaseName 使用的数据库名
     * @param log_write 日志写入模式
     * @param opt_linger 是否启用OPT_LINGER
     * @param trigmode 事件触发模式
     * @param sql_num SQL连接池中的连接数
     * @param thread_num 线程池中的线程数
     * @param close_log 是否关闭日志写入
     * @param actor_model 演员模型模式
     */
    void init(int port, string user, string passwd, string databaseName,
            int log_write, int opt_linger, int trigmode, int sql_num,
            int thread_num, int close_log, int actor_model);

    // 线程池初始化函数
    void thread_pool();
    
    // SQL连接池初始化函数
    void sql_pool();
    
    // 日志写入函数
    void log_write();
    
    // 事件触发模式设置函数
    void trig_mode();
    
    // 事件监听函数
    void eventListen();
    
    // 事件循环处理函数
    void eventLoop();
    
    /**
     * 定时器函数，用于处理超时连接
     * @param connfd 客户端连接文件描述符
     * @param client_address 客户端地址信息
     */
    void timer(int connfd, struct sockaddr_in client_address);
    
    // 调整定时器函数
    void addjust_timer(util_timer* timer);
    
    /**
     * 处理定时器事件
     * @param timer 定时器对象
     * @param sockfd 客户端连接文件描述符
     */
    void deal_timer(util_timer *timer, int sockfd);
    
    // 处理客户端数据函数
    bool dealclientdata();
    
    /**
     * 处理信号函数
     * @param timeout 是否超时
     * @param stop_server 是否停止服务器
     * @return true 如果信号被处理，否则为false
     */
    bool dealwithsignal(bool& timeout, bool& stop_server);
    
    // 处理读事件函数
    void dealwithread(int sockfd);
    
    // 处理写事件函数
    void dealwithwrite(int sockfd);

public:
    // 服务器监听端口
    int m_port;
    // 静态资源根目录
    char* m_root;
    // 日志写入模式
    int m_log_write;
    // 是否关闭日志写入
    int m_close_log;
    // 演员模型模式
    int m_actormodel;

    // 管道文件描述符，用于信号处理
    int m_pipefd[2];
    // epoll文件描述符
    int m_epollfd;
    // 用户连接数组
    http_conn* users;

    // SQL连接池指针
    connection_pool* m_connPool;
    // 登陆数据库用户名
    string m_user;
    // 数据库登陆密码
    string m_passWord;
    // 使用数据库名
    string m_databaseName;
    // SQL连接池中的连接数
    int m_sql_num;

    // 线程池指针
    threadpool<http_conn> *m_pool;
    // 线程池中的线程数
    int m_thread_num;

    // epoll事件数组
    epoll_event events[MAX_EVENT_NUMBER];

    // 监听文件描述符
    int m_listenfd;
    // 是否启用OPT_LINGER
    int m_OPT_LINGER;
    // 事件触发模式
    int m_TRIGMode;
    // 监听事件触发模式
    int m_LISTENTrigmode;
    // 连接事件触发模式
    int m_CONNTrigmode;

    // 客户端连接数据数组
    client_data *users_timer;
    // 工具类对象
    Utils utils;

};
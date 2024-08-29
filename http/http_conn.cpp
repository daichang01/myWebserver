#include "http_conn.h"


// 定义HTTP响应的状态信息常量

// 成功状态 (200)
const char *ok_200_title = "OK";

// 客户端错误 - 请求错误 (400)
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";

// 客户端错误 - 禁止访问 (403)
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";

// 客户端错误 - 未找到资源 (404)
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";

// 服务器错误 - 内部错误 (500)
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


// 初始化HTTP连接对象
void http_conn::init() {
    // 将MySQL连接设置为nullptr，确保没有数据库连接
    mysql = nullptr;
    
    // 初始化发送缓冲区字节数
    bytes_to_send=  0;
    
    // 初始化已发送字节数
    bytes_have_send = 0;
    
    // 设置请求解析状态为请求行检测
    m_check_state = CHECK_STATE_REQUESTLINE;
    
    // 是否允许linger的标志位，初始化为false
    m_linger = false;
    
    // 请求方法默认为GET
    m_method = GET;
    
    // 请求URL和版本号初始化
    m_url = 0;
    m_version = 0;
    
    // 内容长度、主机、起始行、检查索引、读索引、写索引和CGI标志初始化
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    
    // 状态和定时器标志初始化
    m_state = 0;
    timer_flag = 0;
    
    // 优化标志初始化
    improv = 0;
    
    // 清空读缓冲区
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    
    // 清空写缓冲区
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    
    // 清空文件名缓存区
    memset(m_real_file, '\0', FILENAME_LEN);
}


//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
/**
 * @brief 从客户端套接字读取数据
 * 
 * 该函数根据m_TRIGMode的值选择使用LT(水平触发)或ET(边缘触发)模式读取数据。
 * 在LT模式下，一旦recv函数返回就结束读取操作。
 * 在ET模式下，会持续调用recv直到recv返回EAGAIN或EWOULDBLOCK错误，表示没有更多数据可读。
 * 
 * @return true 成功读取到数据或全部数据读取完毕
 * @return false 发生错误或连接关闭
 */
bool http_conn::read_once() {
    // 检查读缓冲区是否已满
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

    // LT模式读取数据
    if (0 == m_TRIGMode) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        // 如果recv返回非正数，视为错误或连接关闭
        if (bytes_read <= 0) {
            return false;
        }
        return true;
    }
    // ET模式读取数据
    else {
        while (true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            // 如果recv返回-1且错误码为EAGAIN或EWOULDBLOCK，表示没有更多数据可读
            if (bytes_read == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                return false;
            }
            // 如果recv返回0，视为连接关闭
            else if (bytes_read == 0) {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//从状态机，用于分析出一行内容
//返回值为行的读取装填，由LINE——OK， LINE——BAD， LINE_OPEN.
/**
 * 解析一行数据
 * 
 * 本函数负责解析接收缓冲区中的数据，以判断是否成功接收到一行完整的数据
 * 主要处理以下几种情况：
 * 1. 行数据完整且以'\r'\n'结束
 * 2. 行数据只有'\n'没有'\r'
 * 3. 数据未接收完，需要继续接收
 * 
 * @return 返回解析状态，可能为LINE_OK（解析成功）、LINE_BAD（解析失败）、LINE_OPEN（数据未接收完）
 */
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // 遍历已接收的数据，直到找到行结束符或处理完所有数据
    for (; m_checked_idx < m_read_idx; ++m_checked_idx  ) {
        temp = m_read_buf[m_checked_idx];
        // 如果找到'\r'，需要检查接下来是否是'\n'
        if (temp == '\r') {
            // 如果是文件结束符，返回LINE_OPEN
            if ((m_checked_idx + 1) == m_read_idx) 
                return LINE_OPEN;
            // 如果接下来是'\n'，则正确结束这一行
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 将行结束符置为字符串结束符，连续两个行结束符都需处理
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 单独的'\r'被认为是错误的
            return LINE_BAD;
        }
        // 如果找到单独的'\n'，需要检查上一个是否应该是'\r'
        else if (temp == '\n') {
            // 如果前一个是'\r'，则正确结束这一行
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == 'r') {
                // 将行结束符置为字符串结束符，并跳过'\n'
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx ++] = '\0';
                return LINE_OK;
            }
            // 单独的'\n'被认为是错误的
            return LINE_BAD;
        }
    }
    // 数据未接收完，需要继续接收
    return LINE_OPEN;
}

// http_conn类的成员函数，用于处理读取请求
// 返回值表示请求处理的状态
http_conn::HTTP_CODE http_conn::process_read(){
    // 初始化行状态为正常
    LINE_STATUS line_status = LINE_OK;
    // 初始化返回值为未接收到请求
    HTTP_CODE ret = NO_REQUEST;
    // 定义一个字符指针用于存储读取的文本行
    char* text=  0;

    //     ● 判断条件
    //   ○ 主状态机转移到CHECK_STATE_CONTENT，该条件涉及解析消息体
    //   ○ 从状态机转移到LINE_OK，该条件涉及解析请求行和请求头部
    //   ○ 两者为或关系，当条件为真则继续循环，否则退出
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        // 获取解析后的文本行
        text = get_line();
        // 更新下一次解析的起始位置
        m_start_line = m_checked_idx;
        // 输出日志信息，打印解析的文本行
        LOG_INFO("%s", text);

        // 根据当前的解析状态，分别处理不同的HTTP请求部分
        switch(m_check_state) {
            // 解析请求行
            case CHECK_STATE_REQUESTLINE: {
                // 解析请求行文本，返回解析结果
                ret = parse_request_line(text);
                // 如果解析出错，返回错误
                if (ret == BAD_REQUEST) 
                    return BAD_REQUEST;
                break;
            }
            // 解析请求头
            case CHECK_STATE_HEADER: {
                // 解析请求头文本，返回解析结果
                ret = parse_headers(text);
                // 如果解析出错，返回错误
                if (ret == BAD_REQUEST) 
                    return BAD_REQUEST;
                // 如果解析完成，成功获取请求，处理请求
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            // 解析请求内容
            case CHECK_STATE_CONTENT: {
                // 解析请求内容文本，返回解析结果
                ret = parse_content(text);
                // 如果解析出错，返回错误
                if (ret == BAD_REQUEST) 
                    return BAD_REQUEST;
                // 保持行状态为打开，继续解析内容
                line_status = LINE_OPEN;
                break;
            }
            // 默认情况下，返回内部错误
            default:
                return INTERNAL_ERROR;
        }
    }
    // 如果解析未完成，返回未请求状态
    return NO_REQUEST;
}

//解析http请求行，获得请求方法，目标url以及http版本号
/**
 * 解析请求行
 * 
 * @param text 请求行的文本
 * @return 请求的处理状态
 * 
 * 请求行的格式为：方法 URL HTTP版本
 * 该函数解析传入的请求行文本，提取并验证方法、URL和HTTP版本
 * 如果解析过程中遇到不符合HTTP规范的请求，返回BAD_REQUEST
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // 提取URL
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char * method = text;
    // 验证并提取请求方法
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else 
        return BAD_REQUEST;
    // 跳过URL中的空白字符
    m_url += strspn(m_url, " \t");
    // 提取HTTP版本
    m_version = strpbrk(m_url, " \t");
    if (!m_version) 
        return BAD_REQUEST;
    *m_version += '\0';
    m_version += strspn(m_version, " \t");
    // 验证HTTP版本
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 移除URL中的协议部分
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // 验证URL格式
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 当URL为根路径时，设置默认资源
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    // 切换到解析头部的状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//对文件描述符设置非阻塞
/**
 * 设置文件描述符为非阻塞模式
 * 
 * 此函数的目的是将给定文件描述符（fd）的模式从阻塞改为非阻塞
 * 在非阻塞模式下，I/O 操作不会因为等待数据而阻塞当前线程
 * 
 * @param fd 要设置为非阻塞模式的文件描述符
 * @return 返回更改前的文件描述符的阻塞选项
 */
int setnonblocking(int fd) {
    // 获取文件描述符的当前状态
    int old_option = fcntl(fd, F_GETFL);
    // 新选项是在原有选项上加上非阻塞标志
    int new_option = old_option | O_NONBLOCK;
    // 设置文件描述符为非阻塞模式
    fcntl(fd, F_SETFL, new_option);
    // 返回更改前的阻塞选项
    return old_option;
}

/**
 * 解析HTTP请求的头部信息
 * 
 * @param text 指向接收缓冲区中当前解析的位置
 * @return 返回HTTP请求的状态代码，表示请求的状态
 * 
 * 功能描述：
 * 本函数旨在解析HTTP请求的头部信息。根据传入的文本参数，识别不同的HTTP头部字段，
 * 并在解析完成后更新类的内部状态。特别地，函数会识别内容长度、连接类型和主机信息，
 * 并对持久连接作出处理。
 * 
 * 注意事项：
 * 函数返回NO_REQUEST表示请求消息尚未解析完成，需要继续解析更多的数据；返回GET_REQUEST
 * 表示请求消息已经完全解析成功。
 */
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 如果text为空字符串，表示头部信息解析完毕
    if (text[0] == '\0') {
        // 如果内容长度不为0，下一步进入内容解析状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 如果内容长度为0，表示请求头部已经完全解析，可以处理请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 解析连接类型头部，跳过头部名称
        text += 11;
        // 跳过头部值前的空白字符
        text += strspn(text, " \t");
        // 如果连接类型为keep-alive，设置m_linger为true，表示需要保持连接
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        // 解析内容长度头部，跳过头部名称
        text += 15;
        // 跳过头部值前的空白字符
        text += strspn(text, " \t");
        // 设置内容长度
        m_content_length = atol(text);

    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        // 解析主机头部，跳过头部名称
        text += 5;
        // 跳过头部值前的空白字符
        text += strspn(text, " \t");
        // 设置主机信息
        m_host = text;
    }
    else {
        // 遇到未知的头部信息，记录日志
        LOG_INFO("oop!unkonw header: %s", text);
    }
    // 表示请求消息尚未解析完成，需要继续解析更多的数据
    return NO_REQUEST;
}

/**
 * 解析HTTP请求的主体内容
 * 
 * @param text 指向读取到的请求主体内容的指针
 * @return 返回解析后的请求状态
 */
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    // 检查是否读取到了足够的主体内容
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        // 终止字符串，并将其赋值给m_string成员变量
        text[m_content_length] = '\0';
        m_string = text;
        // 请求解析完成，返回GET_REQUEST状态
        return GET_REQUEST;
    }
    // 请求解析未完成，返回NO_REQUEST状态
    return NO_REQUEST;
}

// 将内核事件注册为读事件，使用ET模式，并选择是否开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd; // 设置事件对应的文件描述符

    // 根据触发模式设置事件为ET模式还是LT模式
    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // ET模式加上边缘触发标志

    } else {
        event.events = EPOLLIN | EPOLLRDHUP; // LT模式不加边缘触发标志
    }
    
    // 如果one_shot为真，则加上EPOLLONESHOT标志
    if (one_shot) {
        event.events |= EPOLLONESHOT;

    }
    // 将事件添加到epoll文件描述符中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符为非阻塞模式
    setnonblocking(fd);
}


/**
 * 从epoll句柄中移除文件描述符fd，并关闭该fd。
 * 
 * @param epollfd epoll创建的文件描述符。
 * @param fd 需要移除的文件描述符。
 * 
 * 该函数首先使用epoll_ctl函数将指定的文件描述符从epoll监控表中移除，
 * 然后关闭该文件描述符。这一操作通常用于不再需要监控的文件描述符，
 * 以减少系统资源的占用并更新epoll监控列表。
 */
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
/**
 * 修改文件描述符在epoll中的事件类型
 * 
 * @param epollfd epoll文件描述符
 * @param fd 需要修改的文件描述符
 * @param ev 新的事件类型
 * @param TRIGMode 触发模式，1为边缘触发模式，否则为水平触发模式
 * 
 * 此函数通过epoll_ctl函数修改文件描述符fd在epoll中的事件类型根据TRIGMode的不同，
 * 设置不同的事件类型如果TRIGMode为1，将事件类型设置为边缘触发模式（EPOLLET），
 * 否则使用默认的水平触发模式在两种模式下，都会设置事件为一次性（EPOLLONESHOT）和关闭远程挂起（EPOLLRDHUP）
 */


void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode) {
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }
    else {
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    //修改fd上的注册事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 处理HTTP请求的主函数
// 该函数负责整体控制HTTP请求的读取和写入过程
void http_conn::process() {
    // 尝试读取HTTP请求，并返回读取状态
    HTTP_CODE read_ret = process_read();
    
    // 如果请求信息不完整或未准备好，不需要立即处理
    if (read_ret == NO_REQUEST) {
        // 调整epoll监听模式为读事件，等待更多数据到来
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    
    // 处理写入HTTP响应，并返回写入状态
    bool write_ret = process_write(read_ret);
    
    // 如果写入失败，关闭连接
    if (!write_ret) {
        close_conn();
    }
    
    // 调整epoll监听模式为写事件，等待数据写入
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
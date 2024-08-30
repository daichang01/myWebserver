// 防止头文件被重复包含
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>

// 包含网络通信相关的头文件
#include <netinet/in.h> 
// 包含字符串处理的头文件
#include <string>
// 包含文件状态统计的头文件
#include <sys/stat.h>  
// 包含map容器的头文件
#include <map>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h> 

#include "../lock/locker.h"


// 使用标准命名空间
using namespace std;

// 定义HTTP连接类
class http_conn {
public:
    // 定义常量
    static const int FILENAME_LEN = 200; // 文件名长度
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小

    // 定义枚举类型，表示HTTP请求方法
// 定义HTTP请求方法枚举
enum METHOD {
    GET = 0,   // 获取资源
    POST,      // 传输资源
    HEAD,      // 获取资源头部信息
    PUT,       // 更新资源
    DELETE,    // 删除资源
    TRACE,     // 追踪资源
    OPTIONS,   // 获取支持的HTTP方法
    CONNECT,   // 建立到代理的TCP连接
    PATH       // Google私有扩展，用于标识请求与资源路径相关
};

    // 定义枚举类型，表示解析HTTP请求的不同状态(主状态)
    // 定义解析状态枚举，用于表示在不同解析阶段的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0, // 表示正在解析请求行
        CHECK_STATE_HEADER, // 表示正在解析头部
        CHECK_STATE_CONTENT // 表示正在解析内容
    };

    // 定义枚举类型，表示HTTP响应状态码
    // 定义HTTP请求的可能状态码
    enum HTTP_CODE {
        NO_REQUEST,      // 尚未收到请求
        GET_REQUEST,     // 成功接收GET请求
        BAD_REQUEST,     // 请求语法错误，无法解析
        NO_RESOURCE,      // 重复定义，可能是错误？
        FORBIDDEN_REQUEST, // 请求资源禁止访问
        FILE_REQUEST,    // 请求的资源为文件
        INTERNAL_ERROR,  // 服务器内部错误
        CLOSED_CONNECTION // 连接已关闭
    };

    // 定义枚举类型，表示解析行的状态（子状态）
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    // 默认构造函数
    http_conn() {}
    // 默认析构函数
    ~http_conn() {}

private:
    // 初始化连接
    void init(int sockfd, const sockaddr_in &addr, char* , int , int, string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    // 处理HTTP请求
    void process();
    // 读取一次数据
    bool read_once();
    // 写数据
    bool write();

    // 获取客户端地址
    sockaddr_in* get_address() {
        return &m_address;
    }
    // 初始化MySQL结果
    void initmysql_result(connection_pool* connPool);
    // 定时器标志
    int timer_flag;
    // 改进标志
    int improv;

private:
    // 通用初始化函数
    void init();
    // 处理读操作
    HTTP_CODE process_read();
    // 处理写操作
    bool process_write(HTTP_CODE ret);
    // 解析请求行
    HTTP_CODE parse_request_line(char* text);
    // 解析头部信息
    HTTP_CODE parse_headers(char* text);
    // 解析内容
    HTTP_CODE parse_content(char* text);
    // 执行请求
    HTTP_CODE do_request();
    
    // 获取当前行指针
    char* get_line() {return m_read_buf + m_start_line;};
    // 解析行
    LINE_STATUS parse_line();
    // 取消内存映射
    void unmap();
    // 添加响应内容
    bool add_response(const char *format, ...);
    // 添加具体内容
    bool add_content(const char *content);
    // 添加状态行
    bool add_status_line(int status, const char *title);
    // 添加头部信息
    bool add_headers(int content_length);
    // 添加内容类型
    bool add_content_type();
    // 添加内容长度
    bool add_content_length(int content_length);
    // 添加连接状态
    bool add_linger();
    // 添加空行
    bool add_blank_line();

public:
    // 静态变量，表示epoll文件描述符
    static int m_epollfd;
    // 静态变量，表示当前用户数量
    static int m_user_count;
    // MySQL连接指针
    MYSQL* mysql;
    // 状态变量，表示读写状态
    int m_state; 

private:
    // 文件描述符
    int m_sockfd;
    // 客户端地址信息
    sockaddr_in m_address;
    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 读索引
    long m_read_idx;
    // 检查索引
    long m_checked_idx;
    // 行起始位置
    int m_start_line;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写索引
    int m_write_idx;
    // 解析状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;
    // 实际文件路径
    char m_real_file[FILENAME_LEN];
    // 请求资源
    char* m_url;
    // 协议版本
    char* m_version;
    // 主机头
    char* m_host;
    // 内容长度
    long m_content_length;
    // 连接状态
    bool m_linger;
    // 文件地址
    char* m_file_address;
    // 文件状态
    struct stat m_file_stat;
    // I/O向量
    struct iovec m_iv[2];
    // I/O向量计数
    int m_iv_count;
    // 是否启用POST
    int cgi;
    // 请求头信息存储
    char* m_string;
    // 待发送字节数
    int bytes_to_send;
    // 已发送字节数
    int bytes_have_send;
    // 文档根目录
    char* doc_root;

    // 用户信息映射表
    map<string,string> m_users;
    // 触发模式
    int m_TRIGMode;
    // 日志关闭状态
    int m_close_log;

    // MySQL用户名
    char sql_user[100];
    // MySQL密码
    char sql_passwd[100];
    // MySQL数据库名
    char sql_name[100];
  
};

// 防止头文件被重复包含
#endif
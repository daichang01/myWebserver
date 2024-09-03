#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <netinet/in.h>
#include <signal.h>
#include <assert.h>

//连接资源结构体成员需要用到定时器类
class util_timer;
// 用于保存客户端相关数据的结构体
struct client_data
{
    sockaddr_in address;   // 客户端的socket地址
    int sockfd;            // socket文件描述符
    util_timer* timer;     // 指向定时器的指针
};

// 定时器类
class util_timer {
public:
    util_timer(): prev(nullptr), next(nullptr) {}  // 构造函数，初始化前后指针为nullptr
public:
    time_t expire;   // 定时器的超时时间，使用时间戳表示

    void (*cb_func)(client_data *);  // 定时器超时后的回调函数
    client_data* user_data;          // 用户数据
    util_timer* prev;                // 指向前一个定时器
    util_timer* next;                // 指向下一个定时器
};
// 定时器链表类，定时器按超时时间升序排列
class sort_timer_lst {
public:
    sort_timer_lst();   // 构造函数
    ~sort_timer_lst();  // 析构函数

    void add_timer(util_timer *timer);       // 添加定时器
    void adjust_timer(util_timer* timer);    // 调整定时器的位置
    void del_timer(util_timer* timer);       // 删除定时器
    void tick();                             // 定时处理函数，处理链表上的超时定时器

private:
    void add_timer(util_timer* timer, util_timer* lst_head);  // 添加定时器的私有辅助函数

    util_timer* head;  // 链表的头节点
    util_timer* tail;  // 链表的尾节点
};

// 工具类，包含定时器和一些辅助函数
class Utils {
public:
    Utils() {}          // 构造函数
    ~Utils() {}         // 析构函数

    void init(int timeslot);  // 初始化定时器间隔
    int setnonblocking(int fd);  // 设置文件描述符为非阻塞
    void addfd(int epollfd, int fd, bool one_shot ,int TRIGMode);  // 添加文件描述符到epoll实例中
    static void sig_handler(int sig);  // 信号处理函数
    void addsig(int sig, void(handler)(int), bool restart = true);  // 设置信号处理函数
    void timer_handler();  // 定时器处理函数
    void show_error(int connfd, const char* info);  // 显示错误信息

public:
    static int *u_pipefd;  // 静态成员，指向管道文件描述符数组的指针
    sort_timer_lst m_timer_lst;  // 定时器链表
    static int u_epollfd;  // 静态成员，epoll文件描述符
    int m_TIMESLOT;  // 定时器间隔时间
};

// 定时器的回调函数
void cb_func(client_data *user_data);


#endif

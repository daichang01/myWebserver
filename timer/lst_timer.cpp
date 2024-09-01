#include "lst_timer.h"
#include "../http/http_conn.h"

// Utils类的信号处理函数
void Utils::sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    // 将信号类型存储在消息中
    int msg = sig;
    // 向管道写入信号消息，用于通知主循环,传输字符类型，而非整型
    send(u_pipefd[1], (char*)&msg, 1, 0);
    // 恢复原来的errno，保持函数可重入性
    errno = save_errno;
}
int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

// 注册信号处理函数
/**
 * @param sig 需要注册的信号
 * @param handler 信号处理函数
 * @param restart 是否在信号处理完成后重新启动被信号中断的系统调用
 * 
 * 该函数通过sigaction接口注册信号及其处理函数，相比直接使用signal函数，sigaction更加强大和灵活
 * 它可以设置在接收到信号后是否重新启动系统调用（通过SA_RESTART标志实现）
 * 同时，sa_mask用于阻塞除了要注册的信号外的所有其他信号，以确保信号处理过程中的安全性
 * 断言用于确保信号注册成功，否则程序将中断执行，从而保证了程序的健壮性
 */
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa)); // 初始化sigaction结构体
    sa.sa_handler = handler; // 设置信号处理函数
    if (restart) {
        sa.sa_flags |= SA_RESTART; // 设置SA_RESTART标志，以支持信号处理完成后自动重启被中断的系统调用
    }
    sigfillset(&sa.sa_mask); // 设置掩码，阻塞所有信号，除了要注册的信号
    assert(sigaction(sig, &sa, nullptr) != -1); // 注册信号处理函数，确保注册成功
}

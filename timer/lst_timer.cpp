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

/**
 * 将文件描述符添加到epoll监控中，并设置其触发模式和非阻塞状态。
 * 
 * @param epollfd epoll文件描述符。
 * @param fd 要添加到epoll监控的文件描述符。
 * @param one_shot 是否设置为一次性触发模式。
 * @param TRIGMode 触发模式，1为边缘触发，其他为水平触发。
 */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    // 根据触发模式设置epoll事件类型
    if (1 == TRIGMode) {
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }
    else {
        event.events = EPOLLIN | EPOLLRDHUP;
    }

    // 如果设置为一次性触发模式，添加EPOLLONESHOT事件
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    // 将文件描述符添加到epoll监控中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符为非阻塞模式
    setnonblocking(fd);
}




/**
 * 向定时器列表中添加一个新的定时器
 * 
 * 此函数负责将一个新的定时器对象添加到一个维护中的定时器列表里
 * 定时器列表通过每个定时器对象的前后指针链接在一起，以便于快速访问和管理
 * 
 * 参数:
 * timer: 指向新定时器对象的指针这个定时器对象已经在外部被创建，并且是有效的
 * 
 * 返回:
 * 无返回值
 * 
 * 注意:
 * 如果尝试添加一个空的定时器指针，函数将直接返回不执行任何操作
 * 如果定时器列表当前为空，这个新的定时器将被设置为列表的头和尾
 * 如果新定时器的过期时间早于当前所有定时器，则将其添加为列表的新头部
 * 否则，递归地在列表中找到合适的插入位置
 */
void sort_timer_lst::add_timer(util_timer* timer) {
    // 检查传入的定时器指针是否为空
    if (!timer) 
        return;
    
    // 如果列表为空，将这个定时器直接设置为头和尾
    if (!head) {
        head = tail = timer;
        return;
    }

    // 如果新定时器的过期时间早于当前头部定时器
    if (timer->expire < head->expire) {
        // 将新定时器插入到头部
        timer->next = head;
        head->prev = timer;
        head = timer;
    }
    // 否则，调用递归版本的函数来找到并插入到正确的位置
    add_timer(timer,head);
}

/**
 * 调整定时器列表中的定时器项
 * 
 * 本函数用于在定时器列表中调整某个定时器的位置。定时器列表是一个双向链表，
 * 该函数根据定时器的过期时间来调整其在列表中的位置，以保证过期时间最近的定时器位于列表头部。
 * 
 * @param timer 待调整的定时器指针
 */
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer) // 如果传入的 timer 是空指针，则直接返回
    {
        return;
    }
    util_timer *tmp = timer->next; // 获取 timer 的下一个定时器
    if (!tmp || (timer->expire < tmp->expire)) // 如果下一个定时器不存在或者当前定时器的过期时间小于下一个定时器的过期时间，直接返回，不需要调整位置
    {
        return;
    }
    if (timer == head) // 如果当前定时器是链表头节点
    {
        head = head->next; // 将链表头节点指向下一个定时器
        head->prev = NULL; // 将新的头节点的前驱指针置空
        timer->next = NULL; // 将当前定时器的后继指针置空
        add_timer(timer, head); // 重新将当前定时器插入链表中合适的位置
    }
    else // 如果当前定时器不是头节点
    {
        timer->prev->next = timer->next; // 将当前定时器前一个节点的后继指针指向当前定时器的下一个节点
        timer->next->prev = timer->prev; // 将当前定时器下一个节点的前驱指针指向当前定时器的前一个节点
        add_timer(timer, timer->next); // 重新将当前定时器插入链表中合适的位置
    }
}

/**
 * 从定时器列表中删除指定的定时器。
 * 
 * 该函数负责从双向链表中删除指定的定时器对象。根据定时器在链表中的位置（头部、尾部或中间），
 * 进行相应的删除操作，并保持链表的完整性。如果定时器不存在于链表中，则该函数不执行任何操作。
 * 
 * 参数:
 * - timer: 一个指向待删除定时器的指针。如果指针为nullptr，则函数直接返回。
 * 
 * 返回值:
 * - 无返回值。
 */
void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer) {
        return;
    }
    
    // 如果定时器既是链表的头部也是尾部，则直接删除该定时器，并将链表置为空。
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    
    // 如果定时器位于链表的头部，则将头指针移动到下一个定时器，并删除当前头部定时器。
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    
    // 如果定时器位于链表的尾部，则将尾指针移动到前一个定时器，并删除当前尾部定时器。
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }
    
    // 如果定时器位于链表的中间，则调整定时器前后指针，以绕过待删除的定时器，然后删除该定时器。
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

/**
 * 执行定时器列表的tick方法，用于处理到期的定时器。
 * 
 * 该方法遍历定时器列表，检查每个定时器是否已到期。如果定时器已到期，则执行相应的回调函数，
 * 并从列表中删除该定时器。这个过程将持续进行，直到列表中没有未到期的定时器为止。
 * 
 * 请注意，该方法假设调用时已经获取了必要的锁，以保证线程安全。
 */
void sort_timer_lst::tick() {
    // 如果列表为空，直接返回，无需处理。
    if (!head) {
        return;
    }
    
    // 获取当前时间，用于判断定时器是否已到期。
    time_t cur = time(nullptr);
    
    // 从头结点开始遍历定时器列表。
    util_timer* tmp = head;
    while (tmp) {
        // 如果当前定时器未到期，停止处理。
        if (cur < tmp->expire) {
            break;
        }
        
        // 已到期的话，执行定时器的回调函数，并传递用户数据。
        tmp->cb_func(tmp->user_data);
        
        // 从列表中删除当前定时器。
        head = tmp->next;
        if (head) {
            // 如果删除后有新的头结点，更新其前指针。
            head->prev = nullptr;
        }
        
        // 释放当前定时器占用的内存。
        delete tmp;
        
        // 继续处理下一个定时器（如果有的话）。
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer* lst_head) {
    // 初始化两个指针，prev 指向链表头部，tmp 指向链表的第一个元素。
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;

    // 遍历链表，找到合适的位置插入新的定时器对象。
    while (tmp) {
        // 如果新定时器的过期时间小于当前节点的过期时间，执行插入操作。
        if (timer->expire < tmp->expire) {
            // 将新定时器插入到 prev 和 tmp 之间。
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            // 插入完成后跳出循环。
            break;
        }
        // 如果没有找到合适的位置，继续遍历链表。
        prev = tmp;
        tmp = tmp->next;
    }

    // 如果 tmp 为 nullptr，说明已到链表尾部或链表为空，需要在尾部插入新定时器。
    if (!tmp) {
        // 将新定时器添加到链表末尾。
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr; // 新的尾节点的 next 总是 nullptr。
        tail = timer; // 更新链表的尾指针。
    }
}

class Utils;

/**
 * 回调函数，用于处理客户端连接的关闭操作
 * 
 * 该函数从epoll事件表中移除与客户端连接关联的事件，并关闭客户端套接字
 * 同时减少活动用户计数
 * 
 * @param user_data 指向客户端数据的指针，包含套接字文件描述符等信息
 */
void cb_func(client_data* user_data) {
    // 从epoll表中移除客户端连接的事件
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    
    // 确保user_data不为NULL，虽然在当前逻辑中已经进行了操作，但额外的断言可以提高代码的健壮性
    assert(user_data);
    
    // 关闭客户端套接字
    close(user_data->sockfd);
    
    // 减少活动用户计数
    http_conn::m_user_count--;
}

/**
 * @brief 定时器处理函数
 * 
 * 此函数负责处理定时器的计时和报警功能。首先，它使定时器列表前进一个时间点，
 * 确保所有注册在定时器中的任务都得到执行。然后，它设置一个报警，以便在
 * 一个指定的时间段（由m_TIMESLOT定义）后再次调用这个定时器处理函数。
 * 这个函数是定时器机制的核心，用于实现任务的周期性调度。
 */
void Utils::timer_handler() {
    // 使定时器列表中的所有任务前进一个时间点
    m_timer_lst.tick();
    // 设置一个报警，在m_TIMESLOT时间后再次调用定时器处理函数
    alarm(m_TIMESLOT);
}

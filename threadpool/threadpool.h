#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include <cstdio>
#include "../lock/locker.h"
#include "../log/log.h"

// 模板类threadpool用于创建和管理线程池
// T是任务的类型，即线程池将要处理的任务的数据类型
template <typename T>
class threadpool {
public:
    // 构造函数，初始化线程池
    // actor_model: 表示行动模型的整数，用于区分不同的处理策略
    // connPool: 数据库连接池的指针，提供数据库连接服务
    // thread_number: 线程池中线程的数量，默认为8
    // max_request: 请求队列中最多允许的，等待处理的请求数量，默认为10000
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_request = 10000);

    // 析构函数，销毁线程池
    ~threadpool();

    // 向线程池添加一个任务请求
    // request: 要添加的任务请求
    // state: 任务的状态或标记
    // 返回值: 添加任务是否成功
    bool append(T* request, int state);

    // 向线程池添加一个任务请求，用于处理具有不同优先级或属性的任务
    // request: 要添加的任务请求
    // 返回值: 添加任务是否成功
    bool append_p(T* request);

private:
    // 线程工作函数，每个线程都会执行这个函数以从队列中获取任务并处理
    static void* worker(void *arg);

    // 运行线程池中的任务
    void run();

private:
    int m_thread_number;         // 线程池中的线程数量
    int m_max_requests;          // 请求队列中最多允许的，等待处理的请求数量
    pthread_t* m_threads;        // 线程池中所有线程的句柄数组
    std::list<T* > m_workqueue;  // 任务队列，存储等待处理的任务指针
    locker m_queuelocker;        // 队列的锁，用于线程安全
    sem m_queuestat;             // 队列状态信号量，用于同步和互斥
    connection_pool *m_connPool; // 数据库连接池的指针
    int m_actor_model;           // 模型切换
};

// 模板类 threadpool 的构造函数
// 目的：初始化线程池
// 参数：
// - actor_model: 指定线程池的工作模式
// - connPool: 数据库连接池指针，本构造函数未直接使用，可能为其他成员函数所用
// - thread_number: 线程池中线程的数量
// - max_requests: 每个线程最大处理的请求数量
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number , int max_requests):m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests){
    // 验证线程数量和最大请求数量的有效性
    if (thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }
    // 分配线程池数组，用于存储线程的句柄
    m_threads = new pthread_t[m_thread_number];
    // 检查是否成功分配内存
    if (!m_threads) {
        throw std::exception();
    }
    // 遍历创建每个线程
    for (int i = 0; i < thread_number; i++) {
        // 创建工作线程，调用worker函数进行任务处理
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        // 将线程设置为分离模式，这样线程结束时资源会自动回收
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 销毁模板类threadpool的析构函数
// 该析构函数负责释放线程池中所有线程的资源
template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

// 向线程池的工作队列中添加一个请求
// @param request 待添加到工作队列的请求指针
// @param state 请求的状态标识
// @return 如果请求成功添加到工作队列，则返回true；否则返回false
template <typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock(); // 加锁以保护工作队列
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock(); // 解锁，避免阻塞其他线程
        return false; // 工作队列已满，无法添加更多请求
    }

    request->m_state = state; // 设置请求的状态
    m_workqueue.push_back(request); // 将请求添加到工作队列
    m_queuelocker.unlock(); // 解锁，释放对工作队列的保护
    m_queuestat.post(); // 唤醒等待队列非空的线程
    return true; // 请求成功添加到工作队列
}

// 模板函数，向线程池中添加任务请求
template <typename T>
bool threadpool<T>::append_p(T* request) {
    // 加锁以确保线程安全，在执行关键操作时防止多个线程同时访问
    m_queuelocker.lock();
    // 检查任务队列是否已满，如果达到最大请求数，则解锁并返回false
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    // 将任务请求添加到任务队列中
    m_workqueue.push_back(request);
    // 解锁以释放资源，避免长时间锁定导致的性能问题
    m_queuelocker.unlock();
    // 通知线程池有新的任务请求，用于唤醒等待的线程
    m_queuestat.post();
    return true; // 表示任务请求添加成功
}

// 线程池的工作函数，负责执行任务请求
template <typename T>
void* threadpool<T>::worker(void *arg) {
    // 强制类型转换，将void指针转换为threadpool指针
    threadpool *pool = (threadpool*)arg;
    // 调用run函数执行任务请求
    pool->run();
    // 返回线程池指针，通常用于调试或错误处理
    return pool;
}

// 模板函数，运行线程池的任务
template <typename T>
void threadpool<T>::run() {
    // 循环等待并处理任务
    while (true) {
        // 等待工作队列中有任务
        m_queuestat.wait();
        
        // 加锁保护工作队列
        m_queuelocker.lock();
        
        // 检查工作队列是否为空
        if (m_workqueue.empty()) {
            // 如果为空，解锁并继续下一轮循环
            m_queuelocker.unlock();
            continue;
        }
        
        // 获取工作队列的第一个任务
        T* request = m_workqueue.front();
        
        // 从工作队列中移除该任务
        m_workqueue.pop_front();
        
        // 解锁工作队列
        m_queuelocker.unlock();
        
        // 再次检查任务是否为空，为空则跳过
        if (!request) {
            continue;
        }
        
        // 根据actor模型的类型处理任务
        if (1 == m_actor_model) {
            // 待补充的处理逻辑
        }
    }
}

#endif




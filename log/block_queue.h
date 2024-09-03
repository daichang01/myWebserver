/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/
#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../lock/locker.h"
#include <stdlib.h>
#include <sys/time.h>

template<class T>
class block_queue {
public:
    /**
     * 构造函数，初始化队列
     * @param max_size 队列的最大容量，默认为1000
     */
    block_queue(int max_size = 1000) {
        // 检查最大容量是否合法
        if (max_size <= 0) {
            exit(-1);
        }
        m_max_size = max_size;
        // 分配存储空间
        m_array = new T[max_size];
        // 初始化队列大小为0
        m_size = 0;
        // 初始化队列前后指针
        m_front = -1;
        m_back = -1;
    }

// 析构函数，负责释放队列所占用的资源
~block_queue() {
    // 加锁以确保线程安全
    m_mutex.lock();
    // 检查数组是否被分配，如果被分配则释放内存
    if (m_array != nullptr) {
        delete[] m_array;
    }
    // 解锁以释放互斥锁
    m_mutex.unlock();
}

// 清空队列，重置队列状态
void clear() {
    // 加锁以确保线程安全
    m_mutex.lock();
    // 重置队列大小、前后指针，表示队列为空
    m_size = 0;
    m_front = -1;
    m_back = -1;
    // 解锁以释放互斥锁
    m_mutex.unlock();
}

// 检查队列是否已满
bool full() {
    // 加锁以确保线程安全
    m_mutex.lock();
    // 如果队列大小等于最大容量，则队列已满
    if (m_size >= m_max_size) {
        // 解锁并返回队列已满的状态
        m_mutex.unlock();
        return true;
    }
    // 解锁并返回队列未满的状态
    m_mutex.unlock();
    return false;
}

// 检查队列是否为空
bool empty() {
    // 加锁以确保线程安全
    m_mutex.lock();
    // 如果队列大小为0，则队列为空
    if(m_size == 0) {
        // 解锁并返回队列为空的状态
        m_mutex.unlock();
        return true;
    }
    // 解锁并返回队列不为空的状态
    m_mutex.unlock();
    return false;
}

// 获取队列的前端元素
bool front(T &value) {
    // 加锁以确保线程安全
    m_mutex.lock();
    // 如果队列为空，则不能获取元素
    if (0 == m_size) {
        m_mutex.unlock();
        return false;
    }
    // 将前端元素的值复制到传入的引用
    value = m_array[m_front];
    // 解锁以释放互斥锁
    m_mutex.unlock();
    return true;
}

/**
 * 获取队列的尾部元素值，并解锁
 * 
 * 该函数尝试从队列中获取尾部元素的值。在获取元素值前后，会加锁和解锁以确保线程安全。
 * 如果队列为空，则返回false，并且不会修改传入的引用参数。
 * 
 * @param value 将被设置为队列尾部元素值的引用，如果队列不为空。
 * @return 如果成功获取队列尾部元素值，则返回true；如果队列为空，则返回false。
 */
bool back(T& value) {
    m_mutex.lock(); // 加锁以保护对共享资源的访问
    if (0 == m_size) { // 检查队列是否为空
        m_mutex.unlock(); // 如果队列为空，解锁并返回false
        return false;
    }
    value = m_array[back]; // 将队列尾部元素值赋给value
    m_mutex.unlock(); // 解锁以释放对共享资源的访问
    return true; // 返回true表示成功获取队列尾部元素值
}

/**
 * 获取队列当前的元素数量，并解锁
 * 
 * 该函数通过加锁保护对共享资源的访问，以获取当前队列中的元素数量，
 * 然后解锁并返回这个数量。这用于获取队列的大小，而不改变队列的状态。
 * 
 * @return 队列当前的元素数量。
 */
int size() {
    int tmp = 0; // 用于存储队列当前的元素数量

    m_mutex.lock(); // 加锁以保护对共享资源的访问
    tmp = m_size; // 将队列当前的元素数量赋给tmp
    m_mutex.unlock(); // 解锁以释放对共享资源的访问
    return tmp; // 返回队列当前的元素数量
}

/**
 * 获取队列最大的元素数量，并解锁
 * 
 * 该函数通过加锁保护对共享资源的访问，以获取队列被创建时所允许的最大元素数量，
 * 然后解锁并返回这个最大数量。这用于确定队列的最大容量，而不改变队列的状态。
 * 
 * @return 队列最大的元素数量。
 */
int max_size() {
    int tmp = 0; // 用于存储队列最大的元素数量

    m_mutex.lock(); // 加锁以保护对共享资源的访问
    tmp = m_max_size; // 将队列最大的元素数量赋给tmp
    m_mutex.unlock(); // 解锁以释放对共享资源的访问
    return tmp; // 返回队列最大的元素数量
}

/**
 * 向队列中推送一个新元素
 * 
 * @param item 要推送的元素
 * @return bool 推送成功返回true，失败返回false
 */
bool push(const T &item) {
    // 加锁以确保线程安全
    m_mutex.lock(); 
    // 如果队列已满，则解锁并返回false
    if (m_size >= m_max_size) {
        // 唤醒所有等待的线程，以便它们可以检查队列状态
        m_cond.broadcast();
        m_mutex.unlock();
        return false;
    }

    // 计算新元素应插入的位置，并存储元素
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size ++;

    // 通知所有等待的线程队列状态已改变
    m_cond.broadcast();
    // 解锁以释放资源
    m_mutex.unlock();
    return true;
    
}

/**
 * 从队列中取出一个元素
 * 
 * @param item 参考变量，用于存储从队列中弹出的元素
 * @return bool 表示弹出操作是否成功
 * 
 * 此函数的主要功能是从队列中取出一个元素它首先加锁以保证线程安全，
 * 然后检查队列是否为空如果队列为空，则通过条件变量等待队列变为非空，
 * 这里使用while循环而不是if的原因是为了避免虚假唤醒问题一旦队列变为非空，
 * 它会从队列的前端取出一个元素，并将队列的大小减一最后，解锁并返回成功标志
 * 如果在等待过程中被中断，或者队列在长时间等待后仍然为空，则解锁并返回失败标志
 */
bool pop(T &item) {
    m_mutex.lock(); // 加锁以保证线程安全
    while (m_size <= 0) { // 检查队列是否为空，为空则等待
        if (!m_cond.wait(m_mutex.get())) { // 通过条件变量等待队列变为非空
            m_mutex.unlock(); // 解锁
            return false; // 返回失败标志
        }
    }

    m_front = (m_front + 1) % m_max_size; // 计算下一个要取出的元素位置
    item = m_array[m_front]; // 从前端取出一个元素
    m_size --; // 队列大小减一
    m_mutex.unlock(); // 解锁
    return true; // 返回成功标志
}

/**
 * 从队列中弹出一个元素
 * 
 * @param item 弹出的元素将存储在此处
 * @param ms_timeout 等待队列非空的超时时间（以毫秒为单位）
 * @return bool 弹出成功返回true，失败返回false
 */
bool pop(T &item, int ms_timeout) {
    // 初始化绝对超时时间
    struct timespec t = {0, 0};
    // 获取当前时间
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    // 加锁以确保线程安全
    m_mutex.lock();
    
    // 如果队列为空且在超时时间内未变为非空，则解锁并返回false
    if (m_size <= 0) {
        // 设置超时的绝对时间
        t.tv_sec = now.tv_sec + ms_timeout / 1000;
        t.tv_nsec = (ms_timeout % 1000) * 1000;
        // 在超时时间内等待队列变为非空
        if (!m_cond.timewait(m_mutex.get(), t)) {
            m_mutex.unlock();
            return false;
        }
    }

    // 如果队列仍然为空，则解锁并返回false
    if(m_size <= 0) {
        m_mutex.unlock();
        return false;
    }
    // 计算下一个要弹出元素的位置，并获取该元素
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size --;
    // 解锁以释放资源
    m_mutex.unlock();
    return true;

}

private:
    // 互斥锁，用于线程安全
    locker m_mutex;
    // 条件变量，用于线程间同步
    cond m_cond;

    // 队列存储数组
    T* m_array;
    // 当前队列大小
    int m_size;
    // 队列最大容量
    int m_max_size;
    // 队列首元素索引
    int m_front;
    // 队列末元素索引
    int m_back;
};






#endif
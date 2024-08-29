#ifndef LOCKER_H
#define LOCKER_H

#include <semaphore.h>
#include <exception>
#include <pthread.h>

// 信号量类，用于线程间的同步
class sem {
public:
    // 默认构造函数，初始化一个初始值为0的二进制信号量
    sem() {
        // 初始化一个仅在当前进程内共享的、初始值为 0 的二进制信号量，用于同步控制
        if (sem_init(&m_sem, 0, 0) != 0) {
            // 如果初始化失败，抛出异常
            throw std::exception();
        }
    }

    // 带初值的构造函数，初始化一个指定初值的二进制信号量
    sem(int num) {
        // 初始化一个仅在当前进程内共享的、初始值为 num 的二进制信号量
        if (sem_init(&m_sem, 0, num) != 0) {
            // 如果初始化失败，抛出异常
            throw std::exception();
        }
    }

    // 析构函数，销毁信号量
    ~sem() {
        // 销毁信号量
        sem_destroy(&m_sem);
    }

    // 信号量等待操作，如果成功返回 true
    bool wait() {
        // 尝试获取信号量，如果成功则返回 true，否则返回 false
        return sem_wait(&m_sem) == 0;
    }

    // 信号量发布操作，如果成功返回 true
    bool post() {
        // 发布信号量，如果成功则返回 true，否则返回 false
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem; // 内部信号量对象
};

// 互斥锁类，用于线程同步
class locker {
public:
    // 构造函数：初始化互斥锁
    locker() {
        // 初始化互斥锁，如果初始化失败，则抛出异常
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception();
        }
    }

    // 析构函数：销毁互斥锁
    ~locker() {
        // 销毁互斥锁
        pthread_mutex_destroy(&m_mutex);
    }

    // 尝试获取互斥锁
    bool lock() {
        // 如果成功获取互斥锁，则返回true
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // 尝试释放互斥锁
    bool unlock() {
        // 如果成功释放互斥锁，则返回true
        // 注意：这里错误地使用了pthread_mutex_destroy，应该是pthread_mutex_unlock
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // 获取互斥锁的指针
    pthread_mutex_t *get() {
        // 返回互斥锁的指针
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex; // 内部互斥锁
};


// 条件变量类，提供线程同步机制
class cond {
public:
    // 构造函数，初始化条件变量
    cond() {
        // 初始化条件变量，如果初始化失败则抛出异常
        if (pthread_cond_init(&m_cond, nullptr) != 0) {
            throw std::exception();
        }
    }

    // 析构函数，销毁条件变量
    ~cond() {
        // 销毁条件变量
        pthread_cond_destroy(&m_cond);
    }

    /**
     * @brief 等待条件变量
     * 
     * @param m_mutex 用于锁定的互斥锁，确保等待时的互斥访问
     * @return true 等待成功
     * @return false 等待失败
     */
    bool wait(pthread_mutex_t* m_mutex) {
        int ret = 0;
        // 等待条件变量，释放互斥锁并等待通知
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    /**
     * @brief 在限定时间内等待条件变量
     * 
     * @param m_mutex 用于锁定的互斥锁，确保等待时的互斥访问
     * @param t 超时时间点
     * @return true 等待成功
     * @return false 等待失败或超时
     */
    bool timewait(pthread_mutex_t* m_mutex, struct timespec t) {
        int ret = 0;
        // 在限定时间内等待条件变量，释放互斥锁并等待通知直到超时
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }

    /**
     * @brief 发送信号唤醒等待条件变量的一个线程
     * 
     * @return true 信号发送成功
     * @return false 信号发送失败
     */
    bool signal() {
        // 唤醒等待条件变量的一个线程
        return pthread_cond_signal(&m_cond) == 0;
    }

    /**
     * @brief 广播唤醒所有等待条件变量的线程
     * 
     * @return true 广播成功
     * @return false 广播失败
     */
    bool broadcast() {
        // 唤醒所有等待条件变量的线程
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond; // 条件变量
};

#endif

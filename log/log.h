#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <string>
#include "block_queue.h"
#include <cstdarg>

using namespace std;

/**
 * 日志类，用于记录程序运行时的日志信息
 */
class Log{
public:
    /**
     * 获取日志类的单例实例
     * @return 返回日志类的单例指针
     */
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    /**
     * 日志刷新线程入口函数
     * @param args 线程参数，未使用
     * @return 返回nullptr
     */
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
        return nullptr;
    }

    /**
     * 初始化日志类
     * @param file_name 日志文件名
     * @param close_log 是否关闭日志
     * @param log_buf_size 日志缓冲区大小，默认为8192
     * @param split_lines 日志文件分割行数，默认为5000000
     * @param max_queue_size 日志队列最大大小，默认为0表示不限制
     * @return 返回初始化是否成功
     */
    bool init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    /**
     * 写入日志
     * @param level 日志级别
     * @param format 日志信息的格式字符串
     * @param ... 格式字符串中的参数
     */
    void write_log(int level, const char* format, ...);

    /**
     * 刷新日志，将缓冲区内容写入文件
     */
    void flush(void);

private:
    Log();
    virtual ~Log();
    
    /**
     * 异步写入日志线程函数
     * @return 返回nullptr
     */
    void* async_write_log() {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while(m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //日志目录名
    char log_name[128]; //日志文件名
    int m_split_lines; //日志文件分割行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count; //日志计数
    int m_today; //当日志
    FILE* m_fp; //文件指针
    char* m_buf; //缓冲区
    block_queue<string> *m_log_queue; //阻塞队列，用于异步写日志
    bool m_is_async; //是否异步写日志
    locker m_mutex; //线程锁
    int m_close_log; //是否关闭日志
};

// 定义DEBUG级别日志宏，当m_close_log为0时，记录DEBUG级别日志
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
// 定义INFO级别日志宏，当m_close_log为0时，记录INFO级别日志
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
// 定义WARN级别日志宏，当m_close_log为0时，记录WARN级别日志
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
// 定义ERROR级别日志宏，当m_close_log为0时，记录ERROR级别日志
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
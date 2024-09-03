#include "log.h"
#include <stdio.h>
#include <cstring>

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if (m_fp != nullptr) {
        fclose(m_fp);
    }
}

//异步需要设置阻塞队列长度，同步不需要设置
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了 max_queue_size，则将日志设置为异步模式
    if (max_queue_size >= 1) {
        m_is_async = true; // 设置异步标志为 true
        m_log_queue = new block_queue<string>(max_queue_size); // 创建日志队列，大小为 max_queue_size
        pthread_t tid;
        // 创建一个异步线程，用于异步写日志，flush_log_thread 是回调函数
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    m_close_log = close_log; // 设置日志关闭标志
    m_log_buf_size = log_buf_size; // 设置日志缓冲区大小
    m_buf = new char[m_log_buf_size]; // 分配日志缓冲区
    memset(m_buf, '\0', m_log_buf_size); // 将缓冲区初始化为全零
    m_split_lines = split_lines; // 设置日志分割行数

    time_t t = time(nullptr); // 获取当前时间
    struct tm* sys_tm = localtime(&t); // 将时间转换为本地时间
    struct tm my_tm = *sys_tm; // 复制本地时间结构体

    const char* p = strrchr(file_name, '/'); // 查找文件名中最后一个 '/' 的位置
    char log_full_name[256] = {0}; // 日志文件的完整路径和文件名

    if (p == nullptr) {
        // 如果文件名中没有 '/'，直接使用文件名创建日志文件名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        // 如果文件名中包含 '/'，将目录和文件名分开
        strcpy(log_name, p + 1); // 提取文件名
        strncpy(dir_name, file_name, p - file_name + 1); // 提取目录名
        // 创建包含目录和日期的完整日志文件名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    m_today = my_tm.tm_mday; // 记录日志创建的当天日期

    m_fp = fopen(log_full_name, "a"); // 以追加模式打开日志文件
    if (m_fp == nullptr) {
        return false; // 如果文件打开失败，返回 false
    }
    return true; // 文件打开成功，返回 true
}

void Log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};  // 定义一个时间结构体，用于存储当前时间
    gettimeofday(&now, nullptr);  // 获取当前时间，精确到微秒
    time_t t = now.tv_sec;  // 提取当前时间的秒部分
    struct tm *sys_tm = localtime(&t);  // 将秒部分转换为本地时间的tm结构
    struct tm my_tm = *sys_tm;  // 将sys_tm赋值给my_tm
    char s[16] = {0};  // 定义一个字符数组s，用于存储日志级别字符串
    switch (level) {
        case 0:
            strcpy(s, "[debug]");  // 如果level为0，表示debug级别日志
            break;
        case 1:
            strcpy(s, "[info]:");  // 如果level为1，表示info级别日志
            break;
        case 2:
            strcpy(s, "[warn]:");  // 如果level为2，表示warn级别日志
            break;
        case 3:
            strcpy(s, "[erro]:");  // 如果level为3，表示error级别日志
            break;
        default:
            strcpy(s, "[info]:");  // 其他情况下，默认使用info级别日志
            break;
    }
    
    m_mutex.lock();  // 加锁，确保多线程环境下对共享资源的安全访问
    m_count++;  // 日志计数器加1

    // 检查是否需要新建日志文件
    if(m_today != my_tm.tm_mday || m_count & m_split_lines == 0) {
        char new_log[256] = {0};  // 定义一个字符数组，用于存储新日志文件名
        fflush(m_fp);  // 刷新文件缓冲区，将数据写入文件
        fclose(m_fp);  // 关闭当前日志文件
        char tail[16] = {0};  // 定义一个字符数组，用于存储日期信息

        // 格式化日期信息
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if (m_today != my_tm.tm_mday) {
            // 如果是新的一天，则新建一个新的日志文件
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;  // 更新当前日期
            m_count = 0;  // 重置日志计数器
        } else {
            // 如果是同一天，但日志数量达到了分割行数，则创建一个新文件
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");  // 打开新日志文件，追加模式
    }

    m_mutex.unlock();  // 解锁

    va_list valst;  // 定义一个可变参数列表
    va_start(valst, format);  // 初始化可变参数列表

    string log_str;  // 定义一个字符串用于存储最终的日志信息
    m_mutex.lock();  // 再次加锁，确保安全写入日志内容

    // 写入具体的时间和日志级别内容
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    // 将可变参数列表格式化并写入缓冲区
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';  // 添加换行符
    m_buf[n + m + 1] = '\0';  // 以空字符结束字符串
    log_str = m_buf;  // 将缓冲区内容转换为字符串

    m_mutex.unlock();  // 解锁

    if (m_is_async && !m_log_queue->full()) {
        // 如果是异步模式且日志队列未满，则将日志推入队列
        m_log_queue->push(log_str);
    } else {
        // 如果是同步模式或日志队列已满，则直接写入日志文件
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);  // 将日志字符串写入文件
        m_mutex.unlock();
    }

    va_end(valst);  // 结束可变参数列表

}

void Log::flush(void) {
    m_mutex.lock();  // 加锁
    fflush(m_fp);  // 刷新文件缓冲区，将数据写入文件
    m_mutex.unlock();  // 解锁
}

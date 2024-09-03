#include "sql_connection_pool.h"
#include <stdio.h>

using namespace std;

// 构造函数
connection_pool::connection_pool() {
    // 初始化当前连接数和空闲连接数为0
    m_CurConn = 0;
    m_FreeConn = 0;
}

// 单例模式获取connection_pool的实例
connection_pool *connection_pool::GetInstance() {
    // 使用静态局部变量确保connPool只初始化一次
    static connection_pool connPool;
    // 返回connection_pool实例的地址
    return &connPool;
}

// 初始化连接池
void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log ) {
    // 设置数据库连接参数
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;

    // 创建并初始化数据库连接
    for (int i = 0; i < MaxConn; i++) {
        MYSQL* con = nullptr;
        // 初始化MySQL连接
        con = mysql_init(con);
        if (con == nullptr) {
            // 初始化失败，记录错误并退出
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 尝试建立与数据库的连接
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, nullptr, 0);
        if (con == nullptr) {
            // 连接失败，记录错误并退出
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        // 将连接添加到连接列表中
        connList.push_back(con);
        // 增加空闲连接数
        ++m_FreeConn;
    }
    // 初始化信号量，用于控制连接的使用
    reserve = sem(m_FreeConn);

    // 设置最大连接数
    m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL* connection_pool::GetConnection() {
    MYSQL* con = nullptr;

    // 如果连接池中没有可用连接，直接返回空指针
    if (0 == connList.size()) {
        return nullptr;
    }
    // 等待有可用连接
    reserve.wait();

    // 加锁以保护共享资源
    lock.lock();

    // 获取连接池的第一个连接
    con = connList.front();
    // 从连接池中移除该连接
    connList.pop_front();
    // 更新可用连接数量
    --m_FreeConn;
    // 更新当前连接数量
    ++m_CurConn;
    // 解锁以释放共享资源
    lock.unlock();
    // 返回获取的连接
    return con;
}

//释放当前使用的连接
/**
 * 释放数据库连接
 * 
 * 该函数将一个数据库连接返回到连接池中。它首先检查连接是否为nullptr，
 * 如果不为nullptr，则将连接添加到连接列表中，并更新连接池的状态：增加空闲连接数，
 * 减少当前连接数。然后，通过发布信号量来通知等待的线程有可用的连接。
 * 
 * @param con 要释放的数据库连接指针
 * @return 返回true，表示连接已成功释放；如果con为nullptr，则返回false
 */
bool connection_pool::ReleaseConnection(MYSQL *con) {
    // 检查连接是否为nullptr
    if (nullptr == con) {
        return false;
    }

    // 加锁以保护连接列表和连接池状态
    lock.lock();

    // 将连接添加到连接列表中
    connList.push_back(con);
    // 更新空闲连接数
    ++m_FreeConn;
    // 更新当前连接数
    --m_CurConn;

    // 发布信号量以通知有可用的连接
    reserve.post();
    return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool() {
    // 加锁以确保线程安全
    lock.lock();
    // 如果连接池中存在数据库连接
    if (connList.size() > 0) {
        // 遍历连接池中的所有连接
        list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it) {
            // 获取当前迭代的数据库连接指针
            MYSQL* con = *it;
            // 关闭数据库连接
            mysql_close(con);
        }
        // 将当前连接数和空闲连接数重置为0
        m_CurConn = 0;
        m_FreeConn = 0;
        // 清空连接池列表
        connList.clear();
    }
    // 解锁以释放资源
    lock.unlock();
}

// 获取当前空闲的数据库连接数
int connection_pool::GetFreeConn() {
    // 返回当前空闲的数据库连接数
    return this->m_FreeConn;
}

// 析构函数：在对象销毁时调用，用于释放资源
connection_pool::~connection_pool() {
    // 销毁连接池的所有连接
    DestroyPool();
}

// 构造函数：初始化连接RAII对象
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool* connPool) {
    // 从连接池中获取一个数据库连接指针
    *SQL = connPool->GetConnection();
    
    // 将获取的数据库连接指针赋值给成员变量
    conRAII = *SQL;
    // 将连接池指针赋值给成员变量，用于后续释放连接
    pollRAII = connPool;
}

// 析构函数：释放数据库连接回到连接池
connectionRAII::~connectionRAII() {
    // 将成员变量中的数据库连接指针释放回连接池
    pollRAII->ReleaseConnection(conRAII);
}


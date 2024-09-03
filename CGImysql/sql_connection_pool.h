#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <stdio.h>
#include <list>
#include <string>

#include "../lock/locker.h"
#include "../log/log.h"

// 采用命名空间std以简化代码
using namespace std;

/**
 * @brief 数据库连接池类
 * 
 * 该类用于管理数据库连接，提供获取和释放数据库连接的方法。通过连接池可以高效地重用数据库连接，
 * 提高系统性能。实现了单例模式，确保整个系统中只有一个连接池实例。
 */
class connection_pool {
public:
    /**
     * @brief 获取一个空闲的数据库连接
     * 
     * 从连接池中取出一个空闲的数据库连接供使用。
     * 
     * @return MYSQL* 数据库连接指针
     */
    MYSQL *GetConnection();

    /**
     * @brief 释放使用的数据库连接
     * 
     * 将使用完的数据库连接放回连接池中，以便后续使用。
     * 
     * @param conn 要释放的数据库连接指针
     * @return true 释放成功
     * @return false 释放失败
     */
    bool ReleaseConnection(MYSQL *conn);

    /**
     * @brief 获取当前连接池中空闲连接的数量
     * 
     * @return int 空闲连接数量
     */
    int GetFreeConn();

    /**
     * @brief 销毁连接池
     * 
     * 关闭并释放连接池中所有数据库连接，同时销毁连接池对象。
     */
    void DestroyPool();

    /**
     * @brief 单例模式获取连接池实例
     * 
     * 确保系统中只有一个连接池实例，提供一个全局访问点。
     * 
     * @return connection_pool* 连接池实例指针
     */
    static connection_pool* GetInstance();

    /**
     * @brief 初始化连接池
     * 
     * 对连接池进行初始化设置，包括数据库服务器地址、用户信息、数据库名、端口、最大连接数等。
     * 
     * @param url 数据库服务器地址
     * @param User 用户名
     * @param PassWord 密码
     * @param DataBaseName 数据库名
     * @param Port 端口号
     * @param MaxConn 最大连接数
     * @param close_log 关闭日志的标志
     */
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
    /**
     * @brief 构造函数私有化，防止外部创建连接池实例
     */
    connection_pool();

    /**
     * @brief 析构函数，释放连接池中所有连接
     */
    ~connection_pool();

    // 连接池相关属性和成员变量
    int m_MaxConn;       // 最大连接数
    int m_CurConn;       // 当前已使用的连接数
    int m_FreeConn;      // 当前空闲的连接数
    locker lock;         // 锁，用于同步访问连接池
    list<MYSQL*> connList; // 连接列表
    sem reserve;         // 信号量，用于同步控制连接的获取和释放

public:
    // 以下为数据库连接相关信息，公开成员变量方便访问，实际应用中建议封装
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};

/**
 * @class connectionRAII
 * @brief RAII机制管理数据库连接的类
 * 
 * 该类用于通过Resource Acquisition Is Initialization (RAII) 机制管理数据库连接，
 * 确保在构造对象时从连接池获取一个数据库连接，而在对象析构时释放该连接回连接池。
 * 这种机制可以防止由于程序异常退出等原因导致的资源泄露。
 */
class connectionRAII {
public:
    /**
     * @brief 构造函数
     * 
     * 在构造时从连接池中获取一个数据库连接。
     * 
     * @param con 指向MYSQL对象的指针，用于存储从连接池获取的数据库连接。
     * @param connPool 指向connection_pool对象的指针，表示数据库连接池。
     */
    connectionRAII(MYSQL **con, connection_pool *connPool);

    /**
     * @brief 析构函数
     * 
     * 在对象生命周期结束时调用，确保数据库连接被正确地返回到连接池中。
     */
    ~connectionRAII();

private:
    MYSQL *conRAII; /**< 指向当前管理的数据库连接的指针 */
    connection_pool *pollRAII; /**< 指向数据库连接池的指针，用于归还连接 */
};


#endif
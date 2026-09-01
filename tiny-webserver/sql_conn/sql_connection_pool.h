#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <list>
#include <string>
#include <lock/locker.h>

class connection_pool{
public:
    static connection_pool* get_instance(); //单例模式
    void init(const std::string& url, const std::string& user, 
              const std::string& passwd, const std::string& dbname, 
              int port, int max_conn);//初始化数据库连接池
    MYSQL* get_connection(); //获取数据库连接
    bool release_connection(MYSQL* conn); //释放连接
    int get_free_conn(); //获取空闲连接数
    void destroy_pool(); //销毁所有连接
private:
    connection_pool();
    ~connection_pool();
    int m_max_conn; //最大连接数
    int m_free_conn; //当前空闲的连接数
    Semaphore m_sem; //信号量，用于控制连接的获取和释放
    std::list<MYSQL*> m_conn_list; //连接池
    Mutex m_lock; //互斥锁，用于保护连接池的访问
};


//RAII机制，保证连接的获取和释放
class connectionRAII{
public:
    connectionRAII(MYSQL** conn, connection_pool* conn_pool); //构造函数，获取一个数据库连接
    ~connectionRAII(); //析构函数，释放数据库连接
private:
    MYSQL* m_connRAII; //数据库连接
    connection_pool* m_poolRAII; //连接池
};


#endif // SQL_CONNECTION_POOL_H
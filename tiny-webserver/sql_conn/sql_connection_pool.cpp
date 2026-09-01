#include "sql_connection_pool.h"
#include <iostream>


void connection_pool::init(const std::string& url, const std::string& user, 
              const std::string& passwd, const std::string& dbname, 
              int port, int max_conn)
{
    m_max_conn = max_conn;
    // 循环创建多个MySQL连接，直到达到最大连接数m_max_conn
    for(int i = 0; i < m_max_conn; ++i){
        MYSQL* conn = mysql_init(nullptr);
        if(conn == nullptr){
            std::cerr << "MySQL init failed!" << std::endl;
            exit(1);
        }
        // 尝试建立MySQL连接，使用提供的URL、用户名、密码、数据库名和端口
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), passwd.c_str(), 
                                  dbname.c_str(), port, nullptr, 0);
        // 检查连接是否成功
        if(conn == nullptr){
            // 如果连接失败，输出错误信息并退出程序
            std::cerr << "MySQL connection failed!" << std::endl;
            exit(1);
        }
        // 将成功建立的连接添加到连接列表中
        m_conn_list.push_back(conn);
        // 增加可用连接计数
        ++m_free_conn;
        m_sem.post(); // 释放信号量，表示有一个新的可用连接
    }
    

}

MYSQL* connection_pool::get_connection()
{
    m_sem.wait(); // 等待信号量，确保有可用连接
    std::lock_guard<std::mutex> locker(m_lock.getMutex()); // 使用锁保护共享资源
    MYSQL* conn = m_conn_list.front(); // 获取连接列表中的第一个连接
    m_conn_list.pop_front(); // 从连接列表中移除该连接
    --m_free_conn; // 减少可用连接计数
    return conn; // 返回获取的连接
}

bool connection_pool::release_connection(MYSQL* conn)
{
    if(!conn){
        return false; // 如果连接为空，返回false
    }
    std::lock_guard<std::mutex> locker(m_lock.getMutex()); // 使用锁保护共享资源    
    m_conn_list.push_back(conn); // 将连接添加到连接列表中
    ++m_free_conn; // 增加可用连接计数
    m_sem.post(); // 释放信号量，增加可用连接数
    return true; // 返回true，表示释放成功
}

int connection_pool::get_free_conn()
{
    std::lock_guard<std::mutex> locker(m_lock.getMutex()); // 使用锁保护共享资源
    return m_free_conn; // 返回当前空闲连接数
}

void connection_pool::destroy_pool()
{
    std::lock_guard<std::mutex> locker(m_lock.getMutex()); // 使用锁保护共享资源
    for(auto conn : m_conn_list){
        mysql_close(conn); // 关闭每个连接
    }
    m_conn_list.clear(); // 清空连接列表
    m_free_conn = 0; // 重置空闲连接数
}

connection_pool::connection_pool()
{
    m_free_conn = 0; // 初始化空闲连接数为0
    m_max_conn = 0; // 初始化最大连接数为0
}

connection_pool::~connection_pool()
{
    destroy_pool(); // 析构函数中销毁连接池，释放所有连接
}

connectionRAII::connectionRAII(MYSQL** conn, connection_pool* conn_pool)
{
    *conn = conn_pool->get_connection(); // 获取一个数据库连接
    m_connRAII = *conn; // 保存获取的连接
    m_poolRAII = conn_pool; // 保存连接池指针
}

connectionRAII::~connectionRAII()
{
    m_poolRAII->release_connection(m_connRAII); // 释放数据库连接
}

connection_pool* connection_pool::get_instance()
{
    static connection_pool instance; // 使用静态局部变量实现单例模式，确保只有一个连接池实例
    return &instance; // 返回连接池实例的指针
}



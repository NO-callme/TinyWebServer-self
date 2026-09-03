#ifndef HTTP_HTTP_CONN_H
#define HTTP_HTTP_CONN_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include "lock/locker.h"
#include "sql_conn/sql_connection_pool.h"


class http_conn{
public:
    static const int FILENAME_LEN = 200; //文件名最大长度
    static const int READ_BUFFER_SIZE = 2048; //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区大小
    http_conn() {}
    ~http_conn() {}
    void init(int sockfd, const sockaddr_in &addr, connection_pool *conn_pool);//初始化新接受的连接
    void close_conn(bool real_close = true);//关闭连接
    void process();//主入口
    bool read_once(); //读数据
    void write(); //写数据

private:
    void init();//内部状态重置
    
private:
    int m_sockfd_; //套接字
    sockaddr_in m_address; //客户端地址
    char m_read_buf_[READ_BUFFER_SIZE]; //读缓冲区
    int m_read_idx_; //读缓冲区中已经读入的客户端数据的
    char m_write_buf_[WRITE_BUFFER_SIZE]; //写缓冲区
    int m_write_idx_; //写缓冲区中待发送的字节数
    char m_real_file_[FILENAME_LEN]; //客户请求的目标文件的完整路径
    char *m_url_; //客户请求的目标文件的文件名
    char* m_file_address;
    struct stat m_file_stat; //目标文件的状态
    connection_pool *m_conn_pool_; //数据库连接池
};


#endif // HTTP_HTTP_CONN_H
#ifndef HTTP_HTTP_CONN_H
#define HTTP_HTTP_CONN_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>   // struct iovec / writev
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "http/http_parser.h"
#include "sql_conn/sql_connection_pool.h"


// 一个 HTTP 连接：负责读数据、调解析器解析、构造响应、发送响应。
// 解析逻辑在 http_parser（主从状态机），这里负责 I/O 和响应构造（mmap + writev 零拷贝）。
class http_conn{
public:
    static const int FILENAME_LEN = 200;      // 文件完整路径最大长度
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;// 写缓冲区大小（放响应头/错误页）

    http_conn();
    ~http_conn();

    void init(int sockfd, const sockaddr_in &addr, connection_pool *conn_pool); // 初始化新连接
    void close_conn(bool real_close = true); // 关闭连接
    void process();                          // 主入口：解析 → 生成响应 → 发送
    bool read_once();                        // 读数据
    bool write();                            // 发送响应（writev）
    bool is_closed() const;                  // 连接是否已关闭（供主循环判断）

    // 静态资源根目录（由 main/config 启动时设置一次）
    static void set_doc_root(const char* root);

private:
    void init();                             // 内部状态重置

    void do_request();                       // 根据 url 决定响应（静态文件 / 错误页）
    void handle_cgi();                       // 处理登录/注册（POST，走 DAO）
    void build_cgi_response(const char* title, const char* msg); // 构造动态结果页
    void build_error(int status, const char* title, const char* body); // 构造错误响应
    void build_file_response();              // 构造 200 文件响应

    bool add_response(const char* format, ...);   // 往写缓冲区追加（vsnprintf）
    bool add_status_line(int status, const char* title);
    bool add_content_length(int len);
    bool add_linger();                            // Connection 头
    bool add_content_type(const char* path);      // 根据扩展名填 Content-Type
    bool add_blank_line();
    bool add_content(const char* content);

    void unmap();                            // 解除 mmap

private:
    int m_sockfd_;                 // 客户端 socket
    sockaddr_in m_address_;        // 客户端地址

    char m_read_buf_[READ_BUFFER_SIZE];  // 读缓冲区
    int m_read_idx_;                     // 已读入的字节数

    char m_write_buf_[WRITE_BUFFER_SIZE]; // 写缓冲区（响应头 / 错误页）
    int m_write_idx_;                     // 写缓冲区已用的字节数

    char m_real_file_[FILENAME_LEN];      // 目标文件的完整路径
    char* m_file_address_;                // mmap 映射地址
    struct stat m_file_stat_;             // 目标文件的 stat

    struct iovec m_iv_[2];                // writev 用：{响应头, 文件内容}
    int m_iv_count_;
    int m_bytes_to_send_;                 // 还需发送的字节数
    int m_bytes_have_send_;               // 已发送的字节数

    http_parser m_parser_;                // 请求解析器（状态跨包持久化）

    connection_pool* m_conn_pool_;        // 数据库连接池（DAO 阶段用）

    static const char* m_doc_root_;       // 静态资源根目录
};

#endif // HTTP_HTTP_CONN_H

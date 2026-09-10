#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <memory>
#include <cstdint>
#include <cstring>

#include "http/http_conn.h"
#include "timer/lst_timer.h"
#include "thread_pool/thread_pool.h"
#include "sql_conn/sql_connection_pool.h"
#include "config/config.h"

// 触发模式：第一个字母是 listenfd，第二个是 connfd（LT=水平触发，ET=边沿触发）
enum TRIGMode { LT_LT = 0, LT_ET, ET_LT, ET_ET };

// 常量
static const int MAX_FD = 65536;           // 最大文件描述符数（连接数组大小）
static const int MAX_EVENT_NUMBER = 10000; // epoll 一次最多返回的事件数
static const int TIMESLOT = 5;             // 定时器时间槽（秒），连接超时 = 3*TIMESLOT


// 服务器主类：epoll 事件驱动 + 线程池（半同步/半反应堆，proactor 模型）
//
// 分工：
//   主线程（event_loop）：accept / read_once / write，只做 I/O；
//   线程池（process）：解析请求 + 构造响应，只做 CPU/DB 业务。
// 两者通过 epoll 事件 + 任务队列解耦。
class Server {
public:
    Server();
    ~Server();

    // 初始化：socket/bind/listen、epoll、信号、连接池、线程池、定时器
    bool init(const Config& cfg);

    void event_loop();  // epoll 主循环（阻塞，直到收到退出信号）

private:
    void handle_accept();                    // 新连接
    void handle_read(int sockfd);            // 读事件：读数据 + 派发给线程池
    void handle_write(int sockfd);           // 写事件：真正发送响应
    void handle_signal(bool& stop_server);   // 统一事件源：信号到达
    void init_connection(int connfd, const sockaddr_in& addr); // 初始化连接 + 挂定时器
    void close_connection(int sockfd);       // 关闭连接 + 移除 epoll + 删定时器
    void adjust_timer(util_timer* timer);    // 有活动，续期定时器

    static void cb_func(client_data* user_data);  // 定时器回调（超时关闭连接）
    static void sig_handler(int sig);             // 信号处理（写入 pipe）

    int m_listenfd_;
    int m_epollfd_;
    int m_port_;

    std::unique_ptr<http_conn[]> m_users_;         // 连接数组（按 fd 索引）
    std::unique_ptr<client_data[]> m_users_timer_; // 定时器数据数组
    std::unique_ptr<epoll_event[]> m_events_;      // epoll_wait 结果
    std::unique_ptr<ThreadPool<http_conn>> m_thread_pool_;

    sort_timer_lst m_timer_lst_;   // 升序链表定时器
    connection_pool* m_conn_pool_; // 数据库连接池（单例，不拥有）

    int m_listen_trig_;  // listenfd 触发模式 0=LT 1=ET
    int m_conn_trig_;    // connfd 触发模式 0=LT 1=ET
    int m_thread_num_;
    int m_close_log_;    // 0=开日志 1=关日志

    static int s_pipefd_[2];  // 统一事件源 socketpair（信号 → 事件）
};

#endif // SERVER_SERVER_H

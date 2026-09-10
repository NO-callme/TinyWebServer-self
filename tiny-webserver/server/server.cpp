#include "server/server.h"

#include <cstdio>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <unistd.h>

#include "util/fd_util.h"
#include "log/log.h"


int Server::s_pipefd_[2] = {0, 0};

namespace {
    // 注册信号处理函数。handler 可为 SIG_IGN / SIG_DFL / 自定义函数。
    void add_sig(int sig, void (*handler)(int)) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handler;
        sigfillset(&sa.sa_mask);   // 处理信号期间屏蔽所有信号
        sigaction(sig, &sa, nullptr);
    }
}


// 信号处理函数：只做一件最轻的事——把信号值写进 pipe，
// 让事件循环像处理普通 I/O 一样处理信号（统一事件源）。
void Server::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(s_pipefd_[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 定时器回调：连接超时未活跃，从 epoll 移除并关闭。
// 注意：定时器节点本身由 sort_timer_lst::tick() 负责 delete，这里只关 fd。
void Server::cb_func(client_data* user_data)
{
    if (!user_data) return;
    epoll_ctl(user_data->epollfd, EPOLL_CTL_DEL, user_data->sockfd, nullptr);
    close(user_data->sockfd);
}


Server::Server()
    : m_listenfd_(-1), m_epollfd_(-1), m_port_(0),
      m_conn_pool_(nullptr),
      m_listen_trig_(0), m_conn_trig_(0), m_thread_num_(8), m_close_log_(0)
{
}

Server::~Server()
{
    // 优雅退出：先 join 掉所有工作线程（确保没有线程还在碰连接），再关资源
    m_thread_pool_.reset();

    if (m_listenfd_ != -1) close(m_listenfd_);
    if (s_pipefd_[0] != 0) close(s_pipefd_[0]);
    if (s_pipefd_[1] != 0) close(s_pipefd_[1]);
    if (m_epollfd_ != -1) close(m_epollfd_);

    // 关闭所有还开着的客户端连接（init 失败时 m_users_ 可能还没分配）
    if (m_users_) {
        for (int i = 0; i < MAX_FD; ++i) {
            m_users_[i].close_conn();
        }
    }

    // m_timer_lst_ 析构会释放剩余的定时器节点
    if (m_conn_pool_) {
        m_conn_pool_->destroy_pool();
    }
}


bool Server::init(const Config& cfg)
{
    m_port_ = cfg.port;
    m_thread_num_ = cfg.thread_num;
    m_close_log_ = cfg.close_log;
    // 拆分触发模式：高 1 位是 listenfd，低 1 位是 connfd
    m_listen_trig_ = cfg.trig_mode / 2;   // 0=LT 1=ET
    m_conn_trig_ = cfg.trig_mode % 2;     // 0=LT 1=ET

    // 1. 静态资源根目录 + http_conn 全局参数
    http_conn::set_doc_root(cfg.doc_root.c_str());

    // 2. 日志（按配置决定开/关、同步/异步）
    if (!cfg.close_log) {
        // 异步模式传队列大小 > 0 触发，同步模式传 0
        Log::get_instance()->init("./tinywebserver.log", 8192, 2000000,
                                  cfg.async_log ? 1024 : 0);
    }

    // 3. 数据库连接池
    m_conn_pool_ = connection_pool::get_instance();
    m_conn_pool_->init(cfg.sql_host, cfg.sql_user, cfg.sql_passwd,
                       cfg.sql_dbname, 3306, cfg.conn_pool_size);

    // 4. 线程池（半同步/半反应堆的「半同步」部分）
    m_thread_pool_.reset(new ThreadPool<http_conn>(m_conn_pool_, m_thread_num_, 10000));

    // 5. 分配连接数组、定时器数组、事件数组
    m_users_.reset(new http_conn[MAX_FD]);
    m_users_timer_.reset(new client_data[MAX_FD]());
    m_events_.reset(new epoll_event[MAX_EVENT_NUMBER]);

    // 6. 创建监听 socket
    m_listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenfd_ < 0) return false;
    int opt = 1;
    setsockopt(m_listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_port_);
    if (bind(m_listenfd_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
    // backlog 调大：默认 5 在高并发下连接建立会被 SYN/accept 队列卡死，
    // 大量连接 connect 超时。设成 1024 让内核有足够缓冲吸收连接突发。
    if (listen(m_listenfd_, 1024) < 0) return false;

    // 7. epoll 实例
    m_epollfd_ = epoll_create(5);
    if (m_epollfd_ < 0) return false;
    http_conn::set_epollfd(m_epollfd_);
    http_conn::set_trig_mode(m_conn_trig_);

    // 8. 注册 listenfd（listenfd 不加 EPOLLONESHOT，只有主线程 accept）
    addfd(m_epollfd_, m_listenfd_, false, m_listen_trig_);

    // 9. 统一事件源：socketpair，把信号变成可 epoll 监听的事件
    socketpair(AF_UNIX, SOCK_STREAM, 0, s_pipefd_);
    set_nonblocking(s_pipefd_[0]);
    set_nonblocking(s_pipefd_[1]);
    addfd(m_epollfd_, s_pipefd_[0], false, 0);  // pipe 读端：LT 即可

    // 10. 注册信号
    add_sig(SIGPIPE, SIG_IGN);       // 写已关闭的 socket 会触发 SIGPIPE，必须忽略
    add_sig(SIGTERM, sig_handler);
    add_sig(SIGINT, sig_handler);

    printf("[server] 启动：端口=%d 根目录=%s 触发模式=%s%s 线程数=%d 连接池=%d 日志=%s\n",
           m_port_, cfg.doc_root.c_str(),
           m_listen_trig_ ? "ET" : "LT",
           m_conn_trig_ ? "ET" : "LT",
           m_thread_num_, cfg.conn_pool_size,
           cfg.close_log ? "关" : (cfg.async_log ? "异步" : "同步"));

    if (!m_close_log_) {
        Log::get_instance()->write_log(LOG_INFO, "server start: port=%d thread=%d",
                                       m_port_, m_thread_num_);
    }
    return true;
}


void Server::event_loop()
{
    bool stop_server = false;

    while (!stop_server) {
        // epoll_wait 超时设成 TIMESLOT，用来周期性 tick 定时器
        int number = epoll_wait(m_epollfd_, m_events_.get(), MAX_EVENT_NUMBER, TIMESLOT * 1000);
        if (number < 0 && errno != EINTR) {
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sockfd = m_events_[i].data.fd;
            uint32_t ev = m_events_[i].events;

            if (sockfd == m_listenfd_) {
                handle_accept();
            }
            else if (sockfd == s_pipefd_[0] && (ev & EPOLLIN)) {
                handle_signal(stop_server);
            }
            else if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                close_connection(sockfd);   // 对端关闭 / 出错
            }
            else if (ev & EPOLLIN) {
                handle_read(sockfd);
            }
            else if (ev & EPOLLOUT) {
                handle_write(sockfd);
            }
        }

        // 每轮 tick 一次，剔除超时的非活跃连接
        m_timer_lst_.tick();
    }

    printf("[server] 收到退出信号，正在优雅关闭...\n");
}


void Server::handle_accept()
{
    while (true) {
        sockaddr_in client;
        socklen_t len = sizeof(client);
        int connfd = accept(m_listenfd_, (sockaddr*)&client, &len);
        if (connfd < 0) {
            // ET 模式必须循环 accept 直到 EAGAIN；LT 模式单次即可，这里统一循环
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            continue;
        }
        if (connfd >= MAX_FD) {
            close(connfd);   // 超出数组范围，罕见，直接关闭
            continue;
        }
        init_connection(connfd, client);
    }
}


void Server::handle_read(int sockfd)
{
    adjust_timer(m_users_timer_[sockfd].timer);   // 有活动，续期

    if (m_users_[sockfd].read_once()) {
        // proactor：主线程读数据，解析 + 生成响应交给线程池
        m_thread_pool_->append(&m_users_[sockfd]);
    }
    else {
        close_connection(sockfd);   // 读出错（对端关闭 / 缓冲区满）
    }
}


void Server::handle_write(int sockfd)
{
    adjust_timer(m_users_timer_[sockfd].timer);

    if (!m_users_[sockfd].write()) {
        close_connection(sockfd);   // 写完且非 keep-alive，或发送出错
    }
    // write() 返回 true：要么 keep-alive 已重置并挂 EPOLLIN，要么没发完已挂 EPOLLOUT
}


void Server::handle_signal(bool& stop_server)
{
    char sig[16];
    int ret = recv(s_pipefd_[0], sig, sizeof(sig), 0);
    if (ret <= 0) return;

    for (int i = 0; i < ret; ++i) {
        switch (sig[i]) {
            case SIGTERM:
            case SIGINT:
                stop_server = true;
                break;
        }
    }
}


void Server::init_connection(int connfd, const sockaddr_in& addr)
{
    m_users_[connfd].init(connfd, addr, m_conn_pool_);
    // EPOLLONESHOT：同一时刻只让一个线程处理该连接，避免多线程抢同一 fd
    addfd(m_epollfd_, connfd, true, m_conn_trig_);

    // 挂定时器：超时未活跃就关闭
    m_users_timer_[connfd].address = addr;
    m_users_timer_[connfd].sockfd = connfd;
    m_users_timer_[connfd].epollfd = m_epollfd_;

    util_timer* timer = new util_timer;
    timer->user_data = &m_users_timer_[connfd];
    timer->cb_func = cb_func;
    timer->expire = time(nullptr) + 3 * TIMESLOT;
    m_users_timer_[connfd].timer = timer;
    m_timer_lst_.add_timer(timer);

    if (!m_close_log_) {
        Log::get_instance()->write_log(LOG_INFO, "new connection fd=%d", connfd);
    }
}


void Server::close_connection(int sockfd)
{
    if (sockfd < 0 || sockfd >= MAX_FD) return;

    removefd(m_epollfd_, sockfd);   // 从 epoll 移除

    // 删掉并释放定时器（手动关闭时，定时器还没被 tick 处理）
    util_timer* timer = m_users_timer_[sockfd].timer;
    if (timer) {
        m_timer_lst_.del_timer(timer);
        delete timer;
        m_users_timer_[sockfd].timer = nullptr;
    }

    m_users_[sockfd].close_conn();  // 真正 close(fd) 并置 m_sockfd_ = -1

    if (!m_close_log_) {
        Log::get_instance()->write_log(LOG_INFO, "close connection fd=%d", sockfd);
    }
}


void Server::adjust_timer(util_timer* timer)
{
    if (!timer) return;
    timer->expire = time(nullptr) + 3 * TIMESLOT;
    m_timer_lst_.adjust_timer(timer);
}

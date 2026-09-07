#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http/http_conn.h"
#include "sql_conn/sql_connection_pool.h"

// M6 临时驱动：单线程 accept + poll 循环驱动 http_conn，用 curl 验证。
// 正式的 epoll 主循环在 M7 实现，这里先用 poll 避免忙等。
//
// 注意：这里 poll 只监听 POLLIN（可读）。因为验收只发小文件，
// writev 一次就能发完，不会触发 EAGAIN；「没发完等 POLLOUT」在 M7 补上。

static bool set_nonblocking(int fd)
{
    int old = fcntl(fd, F_GETFL);
    if (old < 0) return false;
    return fcntl(fd, F_SETFL, old | O_NONBLOCK) >= 0;
}

int main(int argc, char* argv[])
{
    const char* doc_root = argc > 1 ? argv[1] : "root";
    http_conn::set_doc_root(doc_root);

    // 初始化数据库连接池（M8 再改成命令行参数，这里先硬编码测试凭据）
    connection_pool* pool = connection_pool::get_instance();
    pool->init("localhost", "root", "786520", "twsuser", 3306, 8);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(9006);

    if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listenfd, 5) < 0) { perror("listen"); return 1; }

    printf("M6 临时服务器监听 127.0.0.1:9006（doc_root=%s），Ctrl+C 退出\n", doc_root);

    while (true) {
        sockaddr_in client;
        socklen_t clen = sizeof(client);
        int connfd = accept(listenfd, (sockaddr*)&client, &clen);
        if (connfd < 0) { perror("accept"); continue; }

        set_nonblocking(connfd);

        http_conn conn;
        conn.init(connfd, client, pool);

        // 处理这个连接直到关闭
        while (!conn.is_closed()) {
            struct pollfd pfd;
            pfd.fd = connfd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int ret = poll(&pfd, 1, 10000);  // 10s 超时
            if (ret <= 0) {
                break;  // 超时或出错，关闭
            }
            if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
                if (!conn.read_once()) break;  // 读到 0（对端关闭）或出错
                conn.process();
            }
        }
        conn.close_conn();
        printf("连接关闭\n");
    }
    return 0;
}

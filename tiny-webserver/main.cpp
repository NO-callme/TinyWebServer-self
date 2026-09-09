#include <cstdio>
#include <cstdlib>

#include "server/server.h"

// M7 入口：组装服务器并进入 epoll 主循环。
// M8 会把端口/线程数/连接池凭据改成 getopt 命令行参数，这里先写死默认值。

int main(int argc, char* argv[])
{
    int port = argc > 1 ? atoi(argv[1]) : 9006;
    const char* doc_root = argc > 2 ? argv[2] : "root";
    int trig_mode = LT_ET;   // listenfd 水平触发 + connfd 边沿触发（见 TRIGMode）
    int thread_num = 8;

    Server server;
    if (!server.init(port, doc_root, trig_mode, thread_num,
                     "root", "786520", "twsuser")) {
        printf("服务器初始化失败\n");
        return 1;
    }

    server.event_loop();
    return 0;
}

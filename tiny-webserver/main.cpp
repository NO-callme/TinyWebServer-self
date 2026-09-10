#include <cstdio>

#include "config/config.h"
#include "server/server.h"

// M8 入口：命令行参数(getopt)解析配置 → 组装服务器 → 进入 epoll 主循环。
// 用法：./server -p 9006 -r root -m 1 -t 8 -c 8 ...

int main(int argc, char* argv[])
{
    Config cfg;
    if (!cfg.parse(argc, argv)) {
        return 1;   // 参数非法或 -h，已打印用法
    }

    Server server;
    if (!server.init(cfg)) {
        printf("服务器初始化失败\n");
        return 1;
    }

    server.event_loop();
    return 0;
}

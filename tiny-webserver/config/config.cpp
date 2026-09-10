#include "config/config.h"

#include <unistd.h>
#include <cstdio>
#include <cstdlib>


bool Config::parse(int argc, char* argv[])
{
    int opt;
    opterr = 0;  // getopt 出错时不自己打印，交给 usage 统一处理

    // 冒号表示该选项带参数
    while ((opt = getopt(argc, argv, "p:r:m:t:c:l:a:u:w:d:h")) != -1) {
        switch (opt) {
            case 'p': port = atoi(optarg); break;
            case 'r': doc_root = optarg; break;
            case 'm': trig_mode = atoi(optarg); break;
            case 't': thread_num = atoi(optarg); break;
            case 'c': conn_pool_size = atoi(optarg); break;
            case 'l': close_log = atoi(optarg); break;
            case 'a': async_log = atoi(optarg); break;
            case 'u': sql_user = optarg; break;
            case 'w': sql_passwd = optarg; break;
            case 'd': sql_dbname = optarg; break;
            case 'h':
            default:
                usage(argv[0]);
                return false;
        }
    }

    // 参数合法性校验
    if (trig_mode < 0 || trig_mode > 3) {
        fprintf(stderr, "错误：触发模式 -m 必须在 0~3 之间（当前 %d）\n", trig_mode);
        usage(argv[0]);
        return false;
    }
    if (thread_num <= 0 || conn_pool_size <= 0) {
        fprintf(stderr, "错误：线程数 -t 和连接池大小 -c 必须大于 0\n");
        return false;
    }
    return true;
}


void Config::usage(const char* prog) const
{
    printf("用法：%s [选项]\n\n", prog);
    printf("服务器：\n");
    printf("  -p <port>    监听端口            默认 %d\n", port);
    printf("  -r <path>    静态资源根目录      默认 %s\n", doc_root.c_str());
    printf("  -m <0-3>     触发模式 0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET  默认 %d\n", trig_mode);
    printf("  -t <n>       线程池线程数        默认 %d\n", thread_num);
    printf("  -c <n>       数据库连接池大小    默认 %d\n", conn_pool_size);
    printf("日志：\n");
    printf("  -l <0|1>     0=开日志 1=关日志   默认 %d\n", close_log);
    printf("  -a <0|1>     0=同步写 1=异步写   默认 %d\n", async_log);
    printf("MySQL：\n");
    printf("  -u <user>    用户名              默认 %s\n", sql_user.c_str());
    printf("  -w <passwd>  密码                默认 %s\n", sql_passwd.c_str());
    printf("  -d <dbname>  数据库名            默认 %s\n", sql_dbname.c_str());
    printf("其他：\n");
    printf("  -h           打印本帮助\n");
}

#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include <string>

// 服务器运行配置：全部从命令行参数（getopt）解析，避免把凭据和参数硬编码在代码里。
// 每个字段都有默认值，不传任何参数也能直接跑。
class Config {
public:
    // ---- 服务器 ----
    int port = 9006;               // 监听端口
    int trig_mode = 1;             // 触发模式 0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET
    int thread_num = 8;            // 线程池线程数
    int conn_pool_size = 8;        // 数据库连接池大小
    std::string doc_root = "root"; // 静态资源根目录

    // ---- 日志 ----
    int close_log = 0;             // 0=开启日志 1=关闭日志
    int async_log = 0;             // 0=同步写 1=异步写

    // ---- MySQL ----
    std::string sql_host = "localhost";
    std::string sql_user = "root";
    std::string sql_passwd = "786520";
    std::string sql_dbname = "twsuser";

    // 解析命令行参数。返回 false 表示需要打印帮助并退出。
    bool parse(int argc, char* argv[]);

    // 打印使用说明
    void usage(const char* prog) const;
};

#endif // CONFIG_CONFIG_H

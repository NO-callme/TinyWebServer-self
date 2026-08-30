#ifndef LOG_LOG_H
#define LOG_LOG_H

#include <cstdio>        //FILE, fopen, fclose, fwrite, snprintf
#include <cstdarg>       //va_list(可变参数), va_start, va_end
#include <mutex>
#include <string>
#include <thread>
#include "block/block_queue.h"

//日志级别,数值越大越严重
enum LogLevel{
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
};

class Log{
public:
    static Log* get_instance();//单例模式

    //初始化：打开文件，配置模式
    //file_name       日志文件路径
    //log_buf_size    日志缓冲区大小
    //spite_lines     单个文件最大行数
    //max_queue_size  日志队列最大容量(当>0异步时使用)
    bool init(const char* file_name, int log_buf_size = 8192, 
              int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char* format, ...);  //...即是可变参数
    
    void flush();//刷新缓冲区
private:
    //构造/析构私有化:防止外部创建对象
    Log();
    ~Log();

    // 拷贝构造函数的删除声明
    // 使用 = delete 显式禁用拷贝构造函数
    // 这是一种防止对象被拷贝的方式，确保类的对象只能通过构造函数创建，而不能被复制
    Log(const Log&) = delete;//
    Log& operator=(const Log&) = delete;//禁止赋值操作

    void write_to_file(const char* line);//把已经格式化的字符串真正写入文件

    void async_write_log();//异步写日志线程函数

private:
    //文件
    int m_seq;//日志序号
    char m_log_name[128];//日志文件路径
    FILE* m_fp; //日志文件

    //缓冲区
    char* m_buf;//格式化缓冲区
    int m_log_buf_size;

    //切分
    int m_split_lines;//单个文件最大行数
    long long m_count;//当前文件行数
    int m_today;//当前日期

    //同步/异步
    bool m_is_async;//是否异步
    std::mutex m_mutex;//同步模式写文件时需要加锁

    BlockQueue<std::string>* m_log_queue;//异步模式时，日志先入队列
    std::thread m_write_thread;//异步写日志线程
};

#endif // LOG_LOG_H
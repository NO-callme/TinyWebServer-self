#include "log.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <mutex>
#include "block/block_queue.h"


Log* Log::get_instance()
{
    static Log log;//static只初始化一次，保证只有一个实例
    return &log;
}

bool Log::init(const char* file_name, int log_buf_size, 
              int split_lines, int max_queue_size)
{
    if(max_queue_size > 0){
        m_is_async = true;
        m_log_queue = new BlockQueue<std::string>(max_queue_size);
        m_write_thread = std::thread(&Log::async_write_log, this);
    }
    m_seq = 0;
    m_count = 0;
    m_buf = new char[log_buf_size];
    memset(m_buf, '\0', log_buf_size);
    m_split_lines = split_lines;
    m_log_buf_size = log_buf_size;

    time_t t = time(nullptr);
    struct tm my_tm;
    localtime_r(&t, &my_tm);
    m_today = my_tm.tm_mday;

    strcpy(m_log_name, file_name);
    m_fp = fopen(file_name, "a");
    if (m_fp == nullptr)
    {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char* format, ...)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    //1.时间戳前缀
    time_t t = time(nullptr);
    struct tm my_tm;
    localtime_r(&t, &my_tm);
    int n = snprintf(m_buf, m_log_buf_size, "[%04d-%02d-%02d %02d:%02d:%02d]",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, 
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec);
    
    //2.日志级别
    const char* tag = "info";
    switch (level){
        case LOG_DEBUG:
            tag = "debug";
            break;
        case LOG_INFO:
            tag = "info";
            break;
        case LOG_WARN:
            tag = "warn";
            break;
        case LOG_ERROR:
            tag = "error";
            break;
        default:
            tag = "info";
            break;
    }
    n += snprintf(m_buf + n, m_log_buf_size - n, "[%s]", tag);
    //3.用户消息：把可变参数格式化到m_buf
    va_list args;
    va_start(args, format);//args指向可变参数的起始位置(format后面的第一个参数)
    n += vsnprintf(m_buf + n, m_log_buf_size - n, format, args);
    va_end(args);//释放args

    //4.换行符
    m_buf[n] = '\n';
    m_buf[n + 1] = '\0';

    //检查异步还是同步，处理方式不一样
    if(m_is_async && m_log_queue){
        m_log_queue->push(std::string(m_buf));//异步写日志
    }else{
        write_to_file(m_buf);//同步写日志
    }
}

void Log::flush()
{
    std::lock_guard<std::mutex> locker(m_mutex);
    fflush(m_fp);
}

Log::Log()
{
    m_fp = nullptr;
    m_buf = nullptr;
    m_log_queue = nullptr;
    m_is_async = false;
}

void Log::write_to_file(const char* line)
{
    //拿今天日期
    time_t t = time(nullptr);
    struct tm my_tm;
    localtime_r(&t, &my_tm);

    //需要切分吗？换天或者是写满了
    if(m_today != my_tm.tm_mday || m_count >= m_split_lines){
        fclose(m_fp);
        char new_name[256] = {0};
        snprintf(new_name, sizeof(new_name), "%s_%04d_%02d_%02d_%04d", 
                    m_log_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, 
                    my_tm.tm_mday, ++m_seq);
        m_fp = fopen(new_name, "a");
        m_today = my_tm.tm_mday;
        m_count = 0;
    }

    fputs(line, m_fp);
    m_count++;
}

void Log::async_write_log()
{
    std::string line;
    while(m_log_queue->pop(line)){
        write_to_file(line.c_str());
    }
}

Log::~Log()
{
    if(m_is_async && m_log_queue){
        m_log_queue->stop();
        if(m_write_thread.joinable()){
            m_write_thread.join();
        }
        delete m_log_queue;
    }
    if(m_fp){
        fclose(m_fp);
    }
    delete[] m_buf;
}


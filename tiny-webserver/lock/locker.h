#ifndef SYNC_LOCKER_H
#define SYNC_LOCKER_H

#include <mutex>
#include <condition_variable>


class Mutex{
private:
    std::mutex m_mutex;

public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;// 禁用拷贝构造函数  使用 = delete 显式删除拷贝构造函数，防止对象被拷贝
    Mutex& operator=(const Mutex&) = delete;// 禁用赋值操作符

    void lock(){
        m_mutex.lock();
    }

    void unlock(){
        m_mutex.unlock();
    }

    std::mutex &getMutex(){
        return m_mutex;
    }
};

class Semaphore{
public:
    explicit Semaphore(int count = 0) : m_count(count) {} //构造函数，初始化信号量

    void wait(){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this](){ return m_count > 0; });//等待条件变量，直到条件满足
        --m_count;
    }

    // 使用互斥锁保护共享资源的访问
    void post(){
        std::unique_lock<std::mutex> lock(m_mutex);
        ++m_count;
        lock.unlock();
        m_cond.notify_one();//唤醒一个等待的线程
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_count;
};

#endif


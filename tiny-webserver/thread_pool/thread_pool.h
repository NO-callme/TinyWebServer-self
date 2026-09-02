#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <mutex>
#include <list>
#include <atomic>
#include "lock/locker.h"
#include "sql_conn/sql_connection_pool.h"


template <typename T>
class ThreadPool{
public:
    ThreadPool(connection_pool* pool, int thread_number = 8, int max_requests = 10000){
        m_stop = false;
        m_thread_number = thread_number;
        m_max_requests = max_requests;
        m_conn_pool = pool;
        for(int i = 0; i < m_thread_number; ++i){
            m_threads.emplace_back([this]{
                this->run();
            });
        }
    }
    ~ThreadPool(){
        stop();
        for(auto& thread : m_threads){
            if(thread.joinable()){
                thread.join();
            }
        }
    }
    //添加任务到请求队列
    bool append(T* request){
        std::lock_guard<std::mutex> locker(m_lock.getMutex());
        if((int)m_workqueue.size() >= m_max_requests){
            return false;
        }
        m_workqueue.push_back(request);
        m_sem.post();
        return true;
    }
    void stop(){
        std::lock_guard<std::mutex> locker(m_lock.getMutex());
        m_stop = true;
        for(int i = 0; i < m_thread_number; ++i){
            m_sem.post(); //唤醒所有线程
        }
    }
private:
    void run(){
        while(true){
            m_sem.wait();
            T* request = nullptr;
            {
                std::lock_guard<std::mutex> locker(m_lock.getMutex());
                if(m_workqueue.empty() && m_stop){
                    break;
                }
                request = m_workqueue.front();
                m_workqueue.pop_front();
            }
            if(request){
                request->process(); //处理请求
            }
        }
    }
private:
    int m_thread_number; //线程池中的线程数量
    int m_max_requests; //请求队列中允许的最大请求数量
    std::list<T*> m_workqueue; //请求队列
    std::vector<std::thread> m_threads; //线程池
    Mutex m_lock; //互斥锁，用于保护请求队列的访问
    Semaphore m_sem; //信号量，用于通知线程有任务需要处理
    bool m_stop; //标志线程池是否停止
    connection_pool* m_conn_pool; //数据库连接池
};

#endif // THREAD_POOL_H
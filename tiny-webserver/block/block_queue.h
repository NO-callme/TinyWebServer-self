#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>


template <typename T>
class BlockQueue {
public:
    explicit BlockQueue(int maxSize = 1000){
        m_maxSize = maxSize;
        m_stop = false;
    }
    ~BlockQueue() = default;
    

    //满则阻塞  被stop()打断则返回false
    bool push(const T &item){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this](){return m_stop || (int)m_queue.size() < m_maxSize;});//不满足条件就阻塞
        if(m_stop) return false;
        m_queue.push(item);
        lock.unlock();
        m_cond.notify_one();
        return true;
    }

    //空则阻塞  被stop()打断则返回false
    bool pop(T &item){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this](){return m_stop || !m_queue.empty();});
        if(m_queue.empty()) return false;
        item = m_queue.front();
        m_queue.pop();
        lock.unlock();
        m_cond.notify_one();
        return true;
    }

    //带超时版本  超时或者被stop()打断则返回false
    bool pop(T &item, int timeout){
        std::unique_lock<std::mutex> lock(m_mutex);
        bool ok = m_cond.wait_for(lock, std::chrono::milliseconds(timeout), 
                                    [this](){return m_stop || !m_queue.empty();});
        if(!ok) return false;//超时
        if(m_queue.empty()) return false;
        item = m_queue.front();
        m_queue.pop();
        lock.unlock();
        m_cond.notify_one();
        return true;
    }

    //停止阻塞  notify all blocked threads
    void stop(){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stop = true;
        lock.unlock();
        m_cond.notify_all();
    }

    //要加锁(会根据情况修改mutex的状态)，所以不能是const
    bool empty(){
        std::lock_guard<std::mutex> lock(m_mutex);//lock_guard会自动加锁和释放锁,不需要手动释放锁
        return m_queue.empty();
    }
    bool full(){
        std::lock_guard<std::mutex> lock(m_mutex);
        return (int)m_queue.size() >= m_maxSize;
    }

    int size(){
        std::lock_guard<std::mutex> lock(m_mutex);
        return (int)m_queue.size();
    }
    int maxSize() const{
        return m_maxSize;//从构造后不会改变，所以是const
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_maxSize;
    bool m_stop;
};


#endif //BLOCK_QUEUE_H
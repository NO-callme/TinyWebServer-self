#include "thread_pool/thread_pool.h"
#include <iostream>


struct Task{
    std::atomic<int>* counter; //计数器，记录当前处理的请求数量
    int id; //请求的唯一标识符

    void process(){
        counter->fetch_add(1, std::memory_order_relaxed); //增加计数器
    }
};

int main(){
    connection_pool* pool = connection_pool::get_instance();
    pool->init("localhost", "root", "786520", "twsuser", 3306, 10); // 初始化连接池，最大连接数为10
    std::atomic<int> counter(0); //计数器，记录当前处理的请求数量
    ThreadPool<Task> threadPool(pool, 8, 500); //创建一个线程池，包含8个线程

    std::vector<Task> tasks; //任务队列
    tasks.reserve(100); //预留100个任务的空间
    for(int i = 0; i < 100; ++i){
        tasks.push_back(Task{&counter, i}); //创建100个任务，并将计数器和唯一标识符传入
    }
    for(int i = 0; i < 100; ++i){
        threadPool.append(&tasks[i]);
    } //将100个任务添加到线程池中

    std::this_thread::sleep_for(std::chrono::seconds(2)); //等待2秒，确保所有任务都被处理完毕
    std::cout << "Total processed tasks: " << counter.load() << std::endl; //输出总共处理的任务数量
    return 0;
}
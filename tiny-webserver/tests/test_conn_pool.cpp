#include "sql_conn/sql_connection_pool.h"
#include <thread>
#include <vector>
#include <cassert>
#include <mysql/mysql.h>
#include <iostream>


// void three(connection_pool* pool){
//     MYSQL* conn = nullptr;
//     connectionRAII connRAII(&conn, pool); // 创建一个RAII对象，获取一个连接
//     assert(conn != nullptr); // 确保获取到了连接
//     int c = pool->get_free_conn(); // 获取当前空闲连接数
//     std::cout << "Free connections after RAII: " << c << std::endl; // 输出当前空闲连接数
// }


std::mutex print_mutex; // 用于保护输出的互斥锁
int held = 0;
int peak = 0; // 用于记录当前持有的连接数和峰值连接数

int main(){
    
    connection_pool* pool = connection_pool::get_instance();
    pool->init("localhost", "root", "786520", "twsuser", 3306, 10); // 初始化连接池，最大连接数为10
    const int MAX = 10;
    //1.测试init是否成功，获取连接数是否正确
    // int m = pool->get_free_conn(); // 获取当前空闲连接数
    // std::cout << "Initial free connections: " << m << std::endl; // 输出当前空闲连接数
    //2.借还是否正确
    // MYSQL* conn = pool->get_connection(); // 获取一个连接
    // int a = pool->get_free_conn(); // 获取当前空闲连接数
    // std::cout << "Free connections after getting one: " << a << std::endl;
    // pool->release_connection(conn); // 释放一个连接
    // int b = pool->get_free_conn(); // 获取当前空闲连接数
    // std::cout << "Free connections after releasing one: " << b << std::endl;
    //3.RAII机制是否正确
    // three(pool); // 调用RAII测试函数，确保连接在RAII对象析构时被释放
    // assert(pool->get_free_conn() == 10); // 确保RAII对象析构后，空闲连接数恢复到初始值
    //4.多线程下是否正确
    std::vector<std::thread> threads;
    for (int i = 0; i < 20; ++i) {
        threads.emplace_back([pool]{
            MYSQL* conn = nullptr;
            connectionRAII connRAII(&conn, pool); // 创建一个RAII对象，获取一个连接
            assert(conn != nullptr); // 确保获取到了连接

            {
                std::lock_guard<std::mutex> lock(print_mutex); // 加锁保护输出 
                ++held; // 增加当前持有的连接数
                peak = std::max(peak, held); // 更新峰值连接数
                std::cout << "占用连接数: " << held << "/" << MAX << std::endl; // 输出当前空闲连接数
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 模拟处理连接的时间: 500ms

            {
                std::lock_guard<std::mutex> lock(print_mutex); // 加锁保护输出
                --held; // 减少当前持有的连接数
                std::cout << "释放连接, 当前占用连接数: " << held << "/" << MAX << std::endl; // 输出当前空闲连接数
            }

        }); // 创建一个线程，获取一个连接并输出空闲连接数
    }
    for (auto& t : threads) {
        t.join(); // 等待所有线程执行完毕
    }

    std::cout << "峰值连接数: " << peak << std::endl; // 输出峰值连接数
    std::cout << "结束后空闲连接数: " << pool->get_free_conn() << std::endl; 

    assert(pool->get_free_conn() == 10); // 确保所有线程结束后，空闲连接数恢复到初始值
    return 0;
}

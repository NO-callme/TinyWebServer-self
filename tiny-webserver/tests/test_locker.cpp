#include "lock/locker.h"
#include <thread>
#include <iostream>
#include <atomic>
#include <cassert>


int main() {
    //测试locker.h wait/post功能
    Semaphore sem(0);
    std::atomic<int> count{0};
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            sem.post();//生产者生产一个资源，循环一千次
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < 1000; ++i) {
            sem.wait();//消费者消费一个资源，循环一千次
            ++count;//消费一个资源，计数器加一
        }
    });

    producer.join();
    consumer.join();

    assert(count == 1000);//断言计数器是否为1000
    std::cout << "count: " << count << std::endl;

    return 0;
}

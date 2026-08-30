#include "block/block_queue.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cassert>

//测试多生产者 多消费者
void test_multi_producer_consumer() {
    BlockQueue<int> blockQueue(1000);
    //多生产者 多消费者
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) {
        producers.emplace_back([&blockQueue, i]{
            for (int k = 0; k < 1000; ++k) {
                blockQueue.push(i * 1000 + k);
            }
        });
    }

    std::vector<std::vector<int>> got(4);
    std::vector<std::thread> consumers;
    for (int j = 0; j < 4; ++j) {
        consumers.emplace_back([&blockQueue, &got, j]{
            for (int m = 0; m < 1000; ++m) {
                int val = 0;
                blockQueue.pop(val);
                got[j].push_back(val);
            }
        });
    }

    for (auto &t : producers) {
        t.join();
    }
    for (auto &t : consumers) {
        t.join();
    }

    std::vector<int> all;
    for (auto &v : got) {
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end());
    assert(all.size() == 4000);
    for (int i = 0; i < 4000; ++i) {
        assert(all[i] == i);
    }
    std::cout << "test block queue success\n";

}

//测试阻塞语义 pop()空队列然后主线程sleep()后push，验证消费者确实在等
void test_block() {
    BlockQueue<int> blockQueue(1000);
    std::atomic<bool> isPushed(false);

    std::thread WaitEmpty([&blockQueue, &isPushed]{   // ← 捕获 &isPushed
        int val = 0;
        blockQueue.pop(val);
        assert(val == 25);          // 顺便验证拿到的是 25
        isPushed.store(true);       // ← 拿到数据后置 true
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(isPushed.load() == false);   // push 之前，消费者还阻塞着
    blockQueue.push(25);

    WaitEmpty.join();
    assert(isPushed.load() == true);    // 消费者确实拿到了
    std::cout << "test block queue block success\n";
}

//测试优雅停止 stop()
void test_graceful_stop(){
    BlockQueue<int> blockQueue(1000);

    std::thread Stop([&]{
        int val = 0;
        blockQueue.pop(val);
    });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    blockQueue.stop();
    Stop.join();
    std::cout << "test block queue graceful stop success\n";
}


int main(){
    test_multi_producer_consumer();
    test_block();
    test_graceful_stop();
    std::cout << "all test success\n";
    return 0;
}
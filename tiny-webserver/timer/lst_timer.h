#ifndef TIMER_LST_TIMER_H_
#define TIMER_LST_TIMER_H_

#include <ctime>

struct client_data{
    int sockfd;//先只放套接字，后面连接服务器再写完整结构体
};

class util_timer{
public:
    util_timer():expire(0), cb_func(nullptr), user_data(nullptr), prev(nullptr), next(nullptr) {}
    time_t expire;//绝对过期时间
    void (*cb_func)(client_data*);//回调函数
    client_data* user_data;//用户数据
    util_timer* prev;//双向链表指针
    util_timer* next;
};

/**
 * @brief 排序定时器链表类
 * 
 * 该类实现了一个升序链表，用于管理定时器，按照到期时间升序排列
 */
class sort_timer_lst{
public:
    /**
     * @brief 构造函数
     * 
     * 初始化链表头尾指针为nullptr
     */
    sort_timer_lst():head(nullptr), tail(nullptr){}
    ~sort_timer_lst();
    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void tick();
private:
    util_timer* head;
    util_timer* tail;
};

#endif // TIMER_LST_TIMER_H_
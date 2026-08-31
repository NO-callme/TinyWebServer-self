#include "timer/lst_timer.h"
#include <vector>
#include <iostream>


std::vector<int> fired_order;
void on_timeout(client_data* data){
    fired_order.push_back(data->sockfd);
    std::cout << "fd: " << data->sockfd << " timeout" << std::endl;
} 

int main(){
    client_data cd1; cd1.sockfd = 1;
    client_data cd2; cd2.sockfd = 2;
    client_data cd3; cd3.sockfd = 3;
    client_data cd4; cd4.sockfd = 4;
    client_data cd5; cd5.sockfd = 5;
    client_data cd6; cd6.sockfd = 6;

    util_timer* timer1 = new util_timer;
    timer1->user_data = &cd1;
    timer1->expire = time(nullptr) - 5;
    // 设置定时器1的回调函数
    // 将on_timeout函数赋值给定时器1的回调函数指针cb_func
    timer1->cb_func = on_timeout;

    util_timer* timer2 = new util_timer;
    timer2->user_data = &cd2;
    timer2->expire = time(nullptr) - 1;
    timer2->cb_func = on_timeout;

    util_timer* timer3 = new util_timer;
    timer3->user_data = &cd3;
    timer3->expire = time(nullptr) - 3;
    timer3->cb_func = on_timeout;

    util_timer* timer4 = new util_timer;
    timer4->user_data = &cd4;
    timer4->expire = time(nullptr) - 2;
    timer4->cb_func = on_timeout;

    util_timer* timer5 = new util_timer;
    timer5->user_data = &cd5;
    timer5->expire = time(nullptr) - 4;
    timer5->cb_func = on_timeout;

    util_timer* timer6 = new util_timer;
    timer6->user_data = &cd6;
    timer6->expire = time(nullptr) + 100;
    timer6->cb_func = on_timeout;

    sort_timer_lst* timer_lst = new sort_timer_lst;
    timer_lst->add_timer(timer1);
    timer_lst->add_timer(timer2);
    timer_lst->add_timer(timer3);
    timer_lst->add_timer(timer4);
    timer_lst->add_timer(timer5);
    timer_lst->add_timer(timer6);
    timer2->expire = time(nullptr) + 50;//修改定时器2的到期时间
    timer_lst->adjust_timer(timer2);// 调整定时器2的到期时间

    timer_lst->del_timer(timer3);
    delete timer3;
    
    timer_lst->tick();
}
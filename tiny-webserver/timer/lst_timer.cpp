#include "timer/lst_timer.h"


//遍历链表释放所有节点
sort_timer_lst::~sort_timer_lst(){
    util_timer *tmp = head;
    while(tmp){
        util_timer *cur = tmp;
        tmp = tmp->next;
        delete cur;
    }
}

void sort_timer_lst::add_timer(util_timer* timer){
    if(!timer) return;
    if(!head){//链表为空
        head = tail = timer;
        head->next = tail->next = nullptr;
        head->prev = tail->prev = nullptr;
    }else if((timer->expire) <= (head->expire)){//头插
        timer->next = head;
        head->prev = timer;
        head = timer;
        timer->prev = nullptr;
    }else{//按照超时顺序进行插入
        util_timer *tmp = head;
        while(tmp->next && (tmp->next->expire) <= (timer->expire)){
            tmp = tmp->next;
        }
        timer->next = tmp->next;
        if(tmp->next == nullptr){
            tail = timer;
            timer->prev = tmp;
            tmp->next = timer;
        }else{
            tmp->next->prev = timer;
            timer->prev = tmp;
            tmp->next = timer;
        }
    }
}

void sort_timer_lst::adjust_timer(util_timer* timer)
{
    //改变之后时间还是比后面的小就不用移动
    if(timer->next == nullptr || (timer->expire) < (timer->next->expire)){
        return;
    }

    del_timer(timer);
    add_timer(timer);
}

void sort_timer_lst::tick()
{
    if(!head) return;
    time_t cur = time(nullptr);//获取当前时间
    util_timer *tmp = head;
    while(tmp){
        if(cur < tmp->expire){//时间还没到
            break;
        }
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        // 检查头节点是否存在
        if(head){
            // 如果头节点存在，将其前驱指针设置为nullptr，表示这是新的头节点
            head->prev = nullptr;
        }else{
            // 如果头节点不存在，说明链表为空，将尾节点也设置为nullptr
            tail = nullptr;
        }
        // 释放临时节点tmp的内存
        delete tmp;
        // 将tmp指针重新指向头节点
        tmp = head;
    }
}


void sort_timer_lst::del_timer(util_timer* timer){
    if(!timer) return;
    if(timer == head && timer == tail){//只有一个节点
        head = tail = nullptr;
    }else if(timer == head){//头节点
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
    }else if(timer == tail){//尾节点
        tail = tail->prev;
        tail->next = nullptr;
        timer->prev = nullptr;
    }else{//中间节点
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
    }
}

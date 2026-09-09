#ifndef UTIL_FD_UTIL_H
#define UTIL_FD_UTIL_H

// 文件描述符 + epoll 辅助工具。
// 这些是纯函数，http_conn 和 server 都要用，抽出来避免重复。

// 把 fd 设为非阻塞（ET 模式必须，否则 recv 可能永远阻塞）
int set_nonblocking(int fd);

// 向 epoll 注册 fd
//   epollfd    epoll 实例
//   fd         要注册的 fd
//   one_shot   是否加 EPOLLONESHOT（连接 fd 用 true，防止多线程抢同一连接）
//   trig_mode  0=LT(水平触发) 1=ET(边沿触发)
void addfd(int epollfd, int fd, bool one_shot, int trig_mode);

// 从 epoll 删除 fd（只做 EPOLL_CTL_DEL，不 close，由调用方决定何时 close）
void removefd(int epollfd, int fd);

// 修改 fd 的监听事件（EPOLLIN / EPOLLOUT），保留 EPOLLONESHOT 和触发模式
//   ev  EPOLLIN 或 EPOLLOUT
void modfd(int epollfd, int fd, int ev, int trig_mode);

#endif // UTIL_FD_UTIL_H

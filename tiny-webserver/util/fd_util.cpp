#include "util/fd_util.h"

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>


int set_nonblocking(int fd)
{
    int old = fcntl(fd, F_GETFL);
    if (old < 0) return -1;
    return fcntl(fd, F_SETFL, old | O_NONBLOCK);
}

void addfd(int epollfd, int fd, bool one_shot, int trig_mode)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    // EPOLLRDHUP：能检测到对端半关闭（read 返回 0），用于及时回收连接
    event.events = EPOLLIN | EPOLLRDHUP;
    if (trig_mode == 1) event.events |= EPOLLET;
    if (one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
}

void modfd(int epollfd, int fd, int ev, int trig_mode)
{
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
    if (trig_mode == 1) event.events |= EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

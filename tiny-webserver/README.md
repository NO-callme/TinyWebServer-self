# TinyWebServer-self

一个从零手写的 **Linux 高并发 Web 服务器**，用 C++11 重新实现经典 TinyWebServer 的核心架构。
项目以教学为导向：代码注释充分、自底向上逐层构建、每层独立验证，重点讲清 `epoll / 线程池 / 定时器 / 状态机 / 连接池` 的原理。

- **构建路线图**：见 [PLAN.md](PLAN.md)（M0~M8 里程碑，自底向上，每步可独立验证）
- **技术栈**：C++11 标准库 + CMake + epoll + MySQL C API

---

## 功能特性

| 能力 | 说明 |
|------|------|
| 静态资源服务 | 返回 `root/` 下的 HTML/图片，`mmap` + `writev` 零拷贝发送 |
| 登录 / 注册 | 对接 MySQL `user` 表，解析 POST 表单（DAO 层） |
| 高并发模型 | 半同步/半反应堆：epoll 负责 I/O，线程池负责业务 |
| 连接管理 | 升序链表定时器剔除非活跃连接，支持 `keep-alive` |
| 触发模式 | listenfd/connfd 支持 LT/ET 切换，`EPOLLONESHOT` 防并发冲突 |
| 日志 | 同步/异步可切换，按天/按行数切分文件 |
| 数据库连接池 | 固定数量连接 + RAII 自动归还 |
| 优雅退出 | SIGINT/SIGTERM 触发，join 线程池、关闭监听、销毁资源 |
| 配置化 | `getopt` 命令行参数，凭据不硬编码 |

---

## 架构：半同步 / 半反应堆（proactor）

```
                 ┌──────────────────────────────────────────────┐
                 │            主线程（I/O 事件循环）              │
 客户端 ─accept─▶ listenfd ─ epoll_wait ─▶ EPOLLIN ─ read_once() │
                 │                     │           │             │
                 │                     │      append(连接指针)     │
                 │                     ▼           ▼             │
                 │              ┌─────────────────────┐          │
                 │              │   线程池（半同步）    │          │
                 │              │  process(): 解析请求 + │         │
                 │              │  构造响应 → modfd(OUT) │         │
                 │              └─────────────────────┘          │
                 │                     │                          │
                 │        EPOLLOUT ◀── modfd(EPOLLOUT) ◀──────────┘
                 │        write() 真正 writev 发送
                 └──────────────────────────────────────────────┘
```

**分工**：主线程只做 I/O（读、写），线程池只做 CPU/DB 业务（解析、构造响应），两者通过 epoll 事件 + 任务队列解耦。

核心设计点：

1. **`EPOLLONESHOT`** —— 每个连接一次只让一个线程碰，处理完在 `process`/`write` 里重新 `modfd` 挂载，杜绝多线程抢同一 fd。
2. **LT vs ET** —— 水平触发：读一次即可，没读完会再次通知；边沿触发：必须循环 `recv` 直到 `EAGAIN`，否则漏数据（见 `http_conn::read_once`）。
3. **统一事件源** —— `socketpair` 把 SIGINT/SIGTERM 变成可 epoll 监听的事件；SIGPIPE 被忽略。
4. **升序链表定时器** —— `epoll_wait` 超时设成 TIMESLOT，每轮 `tick()` 剔除非活跃连接，有活动就 `adjust_timer` 续期。
5. **主从状态机** —— HTTP 解析：主状态机（请求行→头→body）+ 从状态机（`parse_line` 找 `\r\n`），状态跨包持久化，支持分片到达。

---

## 目录结构

```
tiny-webserver/
├── main.cpp                   # 入口：解析配置 → 组装 Server
├── config/                    # M8 命令行配置（getopt）
├── server/                    # M7 epoll 主循环 + 事件分发
├── http/                      # M6 HTTP 连接（读取/解析/响应）
│   ├── http_conn.*            #   I/O + 响应构造（mmap/writev）
│   └── http_parser.*          #   主从状态机解析
├── timer/                     # M3 升序链表定时器
├── thread_pool/               # M5 线程池（模板）
├── sql_conn/                  # M4 MySQL 连接池 + RAII
├── db/                        # M6 用户 DAO（登录/注册）
├── log/                       # M2 同步/异步日志
├── lock/                      # M1 互斥锁/信号量封装
├── block/                     # M1 阻塞队列
├── util/                      # M7 fd 非阻塞 + epoll 增删改
├── root/                      # 静态资源
└── tests/                     # 各里程碑单测 + 压测脚本
```

---

## 环境依赖

- Linux（本项目使用 epoll / mmap / socketpair 等 Linux 特性）
- `g++`（支持 C++11）
- `cmake` ≥ 3.10
- MySQL（`libmysqlclient-dev`，编译期链接 `mysqlclient`）

Ubuntu/Debian 安装：

```bash
sudo apt install g++ cmake libmysqlclient-dev
```

---

## 数据库初始化

启动前先建库建表（默认库名 `twsuser`，表 `user`）：

```sql
CREATE DATABASE IF NOT EXISTS twsuser;
USE twsuser;
CREATE TABLE IF NOT EXISTS user (
    username VARCHAR(64) PRIMARY KEY,
    passwd   VARCHAR(64) NOT NULL
);
```

> 注意：MySQL 必须在启动服务器前已运行，否则连接池初始化会失败退出（经典实现的默认行为）。

---

## 构建

```bash
cd tiny-webserver
cmake -B build -S .          # out-of-source 构建
cmake --build build -j       # 编译（含 server 和所有单测）
```

可执行文件：`build/server`；各里程碑单测在 `build/test_*`。

---

## 运行

```bash
./build/server [选项]
```

| 选项 | 说明 | 默认 |
|------|------|------|
| `-p <port>` | 监听端口 | 9006 |
| `-r <path>` | 静态资源根目录 | root |
| `-m <0-3>` | 触发模式 0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET | 1 |
| `-t <n>` | 线程池线程数 | 8 |
| `-c <n>` | 数据库连接池大小 | 8 |
| `-l <0\|1>` | 0=开日志 1=关日志 | 0 |
| `-a <0\|1>` | 0=同步日志 1=异步日志 | 0 |
| `-u <user>` | MySQL 用户名 | root |
| `-w <passwd>` | MySQL 密码 | 786520 |
| `-d <dbname>` | 数据库名 | twsuser |
| `-h` | 打印帮助 | — |

示例：

```bash
./build/server                          # 全默认
./build/server -p 9006 -m 3 -t 16 -c 8  # ET+ET，16 线程
./build/server -l 1                     # 关闭日志（压测时推荐）
./build/server -a 1                     # 异步日志
```

---

## 验证（curl）

```bash
# 静态文件 / 目录 / 图片 / 404
curl -i http://127.0.0.1:9006/index.html
curl -i http://127.0.0.1:9006/
curl -I http://127.0.0.1:9006/kobe.png
curl -i http://127.0.0.1:9006/nonexistent

# keep-alive：一条连接发多个请求
curl -H "Connection: keep-alive" http://127.0.0.1:9006/index.html http://127.0.0.1:9006/kobe.png -o /dev/null

# 注册 / 登录
curl -X POST -d "user=alice&passwd=123456" http://127.0.0.1:9006/register
curl -X POST -d "user=alice&passwd=123456" http://127.0.0.1:9006/login      # 登录成功
curl -X POST -d "user=alice&passwd=wrong"   http://127.0.0.1:9006/login      # 登录失败
```

---

## 压测

### 自带脚本（功能验证）

自带一个仅依赖标准库的 Python 脚本，用于验证并发正确性（`--threads 200 --conns 2 --reqs 50` = 20000 请求 0 失败、服务器不崩）：

```bash
./tests/stress.py --threads 200 --conns 2 --reqs 50
```

脚本里每个请求都是「发一个→等回一个」的串行往返，QPS 受限于 Python 客户端，**不代表服务端吞吐上限**。

### 真实吞吐（wrk）

测服务端真实吞吐用 `wrk`（`sudo apt install wrk`）。服务器关闭日志、backlog 已调大，压测机与本机同机（4 核）：

```bash
./build/server -l 1                      # 关日志，去掉日志 I/O 瓶颈
wrk -t 4 -c 1000 -d 10s http://127.0.0.1:9006/index.html   # 小文件：测连接处理 QPS
wrk -t 4 -c 2000 -d 10s http://127.0.0.1:9006/kobe.png     # 大文件：测带宽
```

| 目标 | 大小 | 峰值 QPS | 峰值带宽 |
|------|------|---------|---------|
| index.html | 303 B | **≈ 1.7 万 req/s**（并发 500~1000） | — |
| kobe.png | 618 KB | ≈ 4.1 千 req/s | **≈ 2.4 GB/s** |

- 小文件 QPS 峰值在并发 500~1000，继续加到 10000 仍**零超时**（QPS 略降至 ~1.3 万）。
- 并发冲到 20000 会崩：`MAX_EVENT_NUMBER=10000` 限制单轮 epoll 最多返回 1 万事件。
- 大文件 2.4 GB/s 已接近本机内存/loopback 拷贝上限，`mmap`+`writev` 零拷贝生效。

> **两个关键结论**（均已在代码中落实/注明）：
> 1. `listen(fd, 5)` 的 backlog 只有 5，高并发下连接建立被 accept 队列卡死，导致大量 connect 超时；改成 1024 后小文件 QPS 从 ~4300 提到 ~1.7 万（约 4 倍）。
> 2. proactor 模型里主线程单线程串行做全部 read/write，是 ~1.7 万 QPS 的最终天花板；要更高需 reactor 模式或多主线程。

---

## 里程碑一览

| 里程碑 | 内容 | 验证 |
|--------|------|------|
| M0 | 项目骨架 + CMake | 构建通过 |
| M1 | 同步原语 + 阻塞队列 | 多生产者-消费者单测 |
| M2 | 同步/异步日志 | 10 万条日志单测 |
| M3 | 升序链表定时器 | 增删改/超时单测 |
| M4 | MySQL 连接池 + RAII | 取还连接计数单测 |
| M5 | 线程池 | 任务全完成单测 |
| M6 | HTTP 解析 + 响应构造 + DAO | curl GET/POST |
| M7 | epoll 主循环 + 线程池 + 定时器 + 信号 | curl + 超时 + 优雅退出 |
| M8 | 配置 + 压测 + README | 参数组合 + 20000 压测 |

---

## 已知限制与后续扩展

- **SQL 注入**：登录/注册用 `snprintf` 拼 SQL，学习项目对齐经典实现；生产应改用预处理语句（`mysql_stmt_prepare`）或 `mysql_real_escape_string`。
- **URL 编码**：`parse_query` 未做 URL 解码（中文/特殊字符用户名会解析错）。
- **reactor 模式**：当前实现 proactor（主线程读 + 派发），reactor（读写都交给线程池）已预留入口，未实现。
- **定时器结构**：升序链表（插入 O(n)），可扩展为小根堆/时间轮。
- **内存占用**：`MAX_FD=65536` 的 `http_conn` 数组约 220MB，可按 `getrlimit(RLIMIT_NOFILE)` 动态 sizing。

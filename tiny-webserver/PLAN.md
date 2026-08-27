# 从零构建一个 Linux 高并发 Web 服务器 —— 构建规划

> 本文档是从零构建的技术方案与里程碑路线图。以现有 `TinyWebServer` 项目为**功能参照物**，
> 但代码完全从零编写，自底向上逐层构建、每层独立验证，最后组装成一个完整可运行的高并发服务器。

---

## 1. 项目定位

用 **C++11 标准库** 从零重新实现一个经典的 Linux 高并发 Web 服务器，作为教学项目：

- **教学导向**：架构清晰、注释充分、不过度抽象，重点讲清 `epoll / 线程池 / 定时器 / 状态机 / 连接池` 的核心原理。
- **渐进现代化**：用 `std::thread / std::mutex / std::condition_variable / 智能指针 / RAII` 替换手写 pthread 封装，但**不引入 C++14+ 特性**。
- **构建系统**：CMake（out-of-source 构建，`find_library` 检测 `mysqlclient`）。

---

## 2. 功能清单（最终交付物）

| 能力 | 说明 |
|------|------|
| 静态资源服务 | 返回 `root/` 下的 HTML/图片/视频，`mmap` + `writev` 发送 |
| 登录 / 注册 | 对接 MySQL `user` 表，解析 POST 表单 |
| 高并发模型 | 半同步/半反应堆：epoll 负责 I/O，线程池负责业务 |
| 连接管理 | 定时器剔除非活跃连接，支持 `keep-alive` |
| 触发模式 | listenfd/connfd 支持 LT/ET 切换，`EPOLLONESHOT` |
| 异步日志 | 同步/异步可切换，按天/按行数分文件 |
| 数据库连接池 | 固定数量连接 + RAII 自动归还 |
| 优雅退出 | `SIGTERM` 触发，关闭监听、join 线程池、销毁资源 |

---

## 3. 技术选型

| 决策点 | 选择 |
|--------|------|
| 语言标准 | C++11 |
| 构建 | CMake |
| I/O 复用 | epoll（LT/ET + EPOLLONESHOT） |
| 并发模型 | 半同步/半反应堆（先 proactor，后补 reactor） |
| 定时器 | 升序链表（注释引出小根堆/时间轮作为扩展） |
| 日志 | 同步/异步可切换 |
| HTTP 解析 | 主从状态机 |
| 配置 | 命令行参数（getopt），凭据不硬编码 |

---

## 4. 目标目录结构

```
tiny-webserver/
├── CMakeLists.txt                 # 顶层构建
├── README.md
├── PLAN.md                        # 本文档
├── main.cpp                       # 入口：组装各组件
├── config/
│   ├── config.h / config.cpp      # 命令行配置
├── server/
│   ├── server.h / server.cpp      # epoll 主循环 + 事件分发
├── http/
│   ├── http_conn.h / http_conn.cpp    # HTTP 连接（读取/解析/响应）
│   └── http_parser.h / http_parser.cpp # 请求行/头/query 解析
├── timer/
│   └── lst_timer.h / lst_timer.cpp    # 升序链表定时器
├── threadpool/
│   └── threadpool.h               # 线程池
├── sync/
│   ├── locker.h                   # Mutex / Semaphore / 条件变量封装
│   └── block_queue.h              # 阻塞队列
├── log/
│   ├── log.h / log.cpp            # 日志系统
├── db/
│   ├── sql_connection_pool.h / .cpp # MySQL 连接池
│   └── user_model.h / .cpp        # 用户数据访问层（DAO）
├── util/
│   └── fd_util.h / fd_util.cpp    # setnonblocking/addfd/removefd/modfd
└── root/                          # 静态资源
```

---

## 5. 里程碑路线图（自底向上，每步可独立验证）

从零构建的核心思路：**先建最底层、无依赖的原语，逐层向上，每层完成时都有独立可运行的验证方式**，而不是最后一次性联调。

### M0 — 项目骨架 + CMake 构建
- **写什么**：目录结构、`CMakeLists.txt`、`.gitignore`、README 骨架、空 `main.cpp`。
- **概念**：out-of-source 构建、CMake target 分层、`mysqlclient` 依赖检测。
- **验收**：`cmake -B build && cmake --build build` 通过，可执行文件打印版本后正常退出。

### M1 — 同步原语 + 阻塞队列（地基）
- **写什么**：
  - `sync/locker.h`：`Mutex`、`Semaphore`（条件变量实现）、条件变量封装。
  - `sync/block_queue.h`：`std::queue` + `mutex` + `condition_variable`，含 `push/pop/pop_for/stop`。
- **概念**：互斥 vs 条件变量 vs 信号量；生产者-消费者模型；C++11 为何没有信号量、如何用条件变量表达。
- **依赖**：无。
- **验收**：多生产者-多消费者 demo，无数据竞争、不丢任务。

### M2 — 日志系统
- **写什么**：`log/log.h + .cpp`，单例、同步/异步双模式、按天/按行数切分、`localtime_r` 线程安全。
- **概念**：单例、可变参数、异步落盘、日志分级。
- **依赖**：M1（异步模式用阻塞队列）。
- **验收**：压 10 万条日志，异步模式不丢、文件正确切分。

### M3 — 定时器
- **写什么**：`timer/lst_timer.h + .cpp`，升序链表定时器（`add/adjust/del/tick`）。
- **概念**：非活跃连接剔除、升序链表插入复杂度与优化空间。
- **依赖**：M2（回调里打日志）。
- **验收**：单测添加/调整/删除/超时回调正确。

### M4 — MySQL 连接池
- **写什么**：`db/sql_connection_pool.h + .cpp`，固定大小连接池 + `connectionRAII` 自动归还。
- **概念**：为什么需要连接池、RAII 管理资源的价值。
- **依赖**：M1（`Semaphore` 控流）、M2（日志）。
- **验收**：建好 `tinywebserver` 库和 `user` 表后，池初始化成功、取还连接计数正确。

### M5 — 线程池
- **写什么**：`threadpool/threadpool.h`，固定线程 + 任务队列，预留 reactor/proactor 两种任务入口。
- **概念**：半同步/半反应堆，任务队列解耦 I/O 与业务。
- **依赖**：M1（队列）、M4（连接池作为参数传入）。
- **验收**：提交大量任务，全部完成、无竞态。

### M6 — HTTP 连接处理（核心、最重）
- **写什么**：`http/http_conn.h + .cpp`、`http/http_parser.*`：
  - LT/ET 读取；
  - 主从状态机解析请求行/头/body；
  - query 解析（登录/注册）；
  - 响应构造（`mmap` + `writev`）、静态资源路由与 MIME。
- **概念**：主从状态机、`mmap/writev` 零拷贝、`keep-alive`、HTTP 状态码语义。
- **依赖**：M2（日志）、M3（定时器字段）、M4（用户数据）。
- **验收**：临时 `main` 单独驱动 `http_conn`，用 `curl` 发 GET/POST 验证解析与响应。

### M7 — 服务器主循环（epoll 事件驱动）
- **写什么**：`server/server.h + .cpp`：
  - socket/bind/listen、`SO_REUSEADDR`；
  - epoll 事件循环（LT/ET + `EPOLLONESHOT`）；
  - 信号统一事件源（`socketpair`）；
  - 连接超时定时器；
  - 优雅退出。
- **概念**：epoll LT/ET 区别、`EPOLLONESHOT`、统一事件源、reactor/proactor。
- **依赖**：M2/M3/M5/M6。
- **验收**：完整服务器跑起来，浏览器访问首页、注册登录、看图看视频。

### M8 — 配置 + 集成 + 压测
- **写什么**：`config`（端口/线程数/连接池/触发模式/日志开关）、压测脚本、README 完整文档。
- **验收**：压测不崩、不同参数组合行为正确、新人照 README 能独立跑起来。

---

## 6. 构建顺序依赖图

```
M0 骨架
   │
   ▼
M1 同步原语+阻塞队列 ───────────────┐
   │  ├──▶ M2 日志 ────────────────┤
   │  ├──▶ M3 定时器 ──────────────┤
   │  └──▶ M4 连接池 ──▶ M5 线程池 ─┤
   │                                │
   ▼                                ▼
M6 HTTP 连接处理 ────────────▶ M7 主循环 ──▶ M8 集成压测
```

- **M1 是地基**：日志、线程池、连接池全依赖它，必须先做对。
- **M6 可与 M1~M5 并行**：用临时 `main` 独立调 `http_conn` 即可验证，不必等整个服务器搭好。
- **M7 是粘合层**：把前面所有组件在 epoll 事件循环里串起来。

---

## 7. 关键设计决策

| 决策点 | 建议 | 理由 |
|--------|------|------|
| 并发模型 | 先 proactor（主线程读+派发），再补 reactor | 由简入繁 |
| 定时器结构 | 升序链表，注释引出小根堆/时间轮 | 保持教学清晰 |
| 日志模式 | 同步/异步可切换 | 完整演示两种落盘 |
| HTTP 解析 | 主从状态机 | 本项目最有教学价值的部分，务必保留 |
| 配置来源 | 命令行参数（getopt），凭据不硬编码 | 教学项目够用 |

---

## 8. 行为等价验收清单（相对参照项目）

每阶段（尤其 M6/M7）用 `curl` + 压测做行为回归：

- 注册 → 跳 `log.html`
- 重复注册 → `registerError.html`
- 登录成功 → `welcome.html`
- 密码错 → `logError.html`
- 访问 `/0 /1 /5 /6 /7` → 分别落到对应页面
- 访问不存在文件 → 404
- reactor/proactor 两种模式下功能一致

---

## 9. 代码规范（教学导向）

- 类与关键成员**中文注释**，讲清"为什么"，而非"是什么"。
- 关键概念（状态机、RAII、LT/ET、半同步半反应堆）在头文件顶部有概述。
- 资源一律 RAII 管理，禁止裸 `new/delete` 出现在业务逻辑里。
- 每个里程碑完成后更新 README 的对应章节。

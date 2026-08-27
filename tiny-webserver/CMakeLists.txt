cmake_minimum_required(VERSION 3.10)
project(tiny_webserver CXX)

# 使用 C++11 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认 Debug 构建，便于教学时调试
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# 让 #include "sync/locker.h" 这类以项目根目录为基准的包含能找到头文件
include_directories(${CMAKE_SOURCE_DIR})

# 查找 MySQL 客户端库（M4 里程碑才真正用到，这里先接上，找不到也不报错）
find_library(MYSQL_LIB mysqlclient)

# 最终服务器入口（M0 阶段只是占位，M7 之后才是真正的启动流程）
add_executable(server main.cpp)

# M1 验证 demo：同步原语 + 阻塞队列
add_executable(test_locker tests/test_locker.cpp)

add_executable(test_block_queue tests/test_block_queue.cpp)


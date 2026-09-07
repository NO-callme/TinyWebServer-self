#ifndef DB_USER_MODEL_H
#define DB_USER_MODEL_H

#include <string>
#include "sql_conn/sql_connection_pool.h"

// 用户数据访问层（DAO）：封装对 user 表的查询和插入。
// 每个方法内部用 connectionRAII 从连接池借一个连接，方法结束自动归还（RAII）。
class user_model{
public:
    explicit user_model(connection_pool* pool);

    // 登录校验：username + passwd 是否在表中匹配
    bool verify_user(const std::string& username, const std::string& passwd);

    // 注册：插入新用户，用户名已存在时返回 false
    bool add_user(const std::string& username, const std::string& passwd);

private:
    connection_pool* m_pool_;
};

#endif // DB_USER_MODEL_H

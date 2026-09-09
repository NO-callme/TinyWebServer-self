#include "user_model.h"

#include <mysql/mysql.h>
#include <cstdio>

// 注意：下面用 snprintf 拼接 SQL，存在 SQL 注入风险
// 生产环境应该用预处理语句（mysql_stmt_prepare / bind）或 mysql_real_escape_string 转义输入。

user_model::user_model(connection_pool* pool) : m_pool_(pool)
{
}

// 登录校验：查 user 表，看 username + passwd 是否同时命中
bool user_model::verify_user(const std::string& username, const std::string& passwd)
{
    MYSQL* conn = nullptr;
    connectionRAII raii(&conn, m_pool_);   // 借连接，函数结束自动还
    if (!conn) {
        return false;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT username, passwd FROM user WHERE username='%s' AND passwd='%s' LIMIT 1",
             username.c_str(), passwd.c_str());

    if (mysql_query(conn, sql) != 0) {
        return false;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
        return false;
    }
    bool ok = (mysql_num_rows(res) > 0);   // 有结果行 = 匹配
    mysql_free_result(res);
    return ok;
}

// 注册：先查重，再插入
bool user_model::add_user(const std::string& username, const std::string& passwd)
{
    MYSQL* conn = nullptr;
    connectionRAII raii(&conn, m_pool_);
    if (!conn) {
        return false;
    }

    // 查重：用户名是否已存在
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT username FROM user WHERE username='%s' LIMIT 1",
             username.c_str());
    if (mysql_query(conn, sql) != 0) {
        return false;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res) {
        bool exists = (mysql_num_rows(res) > 0);
        mysql_free_result(res);
        if (exists) {
            return false;   // 用户名已存在
        }
    }

    // 插入新用户
    snprintf(sql, sizeof(sql),
             "INSERT INTO user(username, passwd) VALUES('%s','%s')",
             username.c_str(), passwd.c_str());
    return mysql_query(conn, sql) == 0;
}

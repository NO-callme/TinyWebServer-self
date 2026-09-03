#ifndef HTTP_HTTP_PARSER_H
#define HTTP_HTTP_PARSER_H

// HTTP 请求解析器：主从状态机
//
// 主状态机（CHECK_STATE）：请求行 → 头部 → 请求体，线性推进，回答"解析到哪一部分"。
// 从状态机（LINE_STATUS）：parse_line 逐字节找 \r\n，回答"这一行读完整了没有"。
//
// 关键点：解析结果（url/version/host/content）都是指向读缓冲区内部的指针，不是拷贝。
// 缓冲区由 http_conn 持有，必须保证在请求处理完之前不被改写。
// 状态（m_check_state_/m_checked_idx_/m_start_line_）会跨 recv 调用持久化，
// 因为一个请求可能分多个包到达。

class http_parser{
public:
    // 主状态机：当前正在解析请求的哪一部分
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,  // 请求行
        CHECK_STATE_HEADER,           // 头部字段
        CHECK_STATE_CONTENT           // 请求体（body）
    };

    // 从状态机：parse_line 扫描一行的结果
    enum LINE_STATUS{
        LINE_OK = 0,  // 读到完整一行
        LINE_BAD,     // 行格式错误（孤立的 \r 或 \n）
        LINE_OPEN     // 行不完整，需要更多数据
    };

    // parse() 的返回值：一次解析的整体结果
    enum PARSE_RESULT{
        PARSE_MORE = 0,  // 请求不完整，需要继续读
        PARSE_DONE,      // 一个完整请求解析完毕
        PARSE_ERROR      // 请求格式错误
    };

    // 请求方法（本实现只处理 GET / POST）
    enum METHOD{
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH
    };

    http_parser();
    ~http_parser();

    void init();  // 重置所有解析状态（新连接 / 处理完一个请求后调用）

    // 主入口：解析 buf 中 [0, read_idx) 的数据
    PARSE_RESULT parse(char* buf, int read_idx);

    // 解析结果 getter（供 http_conn 构造响应用）
    METHOD method() const;
    const char* url() const;
    const char* version() const;
    const char* host() const;
    int content_length() const;
    bool linger() const;          // keep-alive
    const char* content() const;  // POST body 的起始位置

private:
    LINE_STATUS parse_line();                   // 从状态机：取一行
    PARSE_RESULT parse_request_line(char* text); // 解析请求行
    PARSE_RESULT parse_headers(char* text);      // 解析一行头部
    PARSE_RESULT parse_content();                // 解析 body（按长度，不按行）

private:
    // ---- 解析状态（必须跨 recv 调用持久化） ----
    CHECK_STATE m_check_state_;   // 主状态机当前状态

    // ---- 读缓冲区引用 ----
    char* m_read_buf_;            // 指向 http_conn 的读缓冲区（parse_line 会就地写入 \0）
    int m_read_idx_;              // 已读入的有效字节数
    int m_checked_idx_;           // 已扫描到的位置
    int m_start_line_;            // 当前正在解析的行的起点

    // ---- 解析结果 ----
    METHOD m_method_;
    char* m_url_;                 // 指向缓冲区里 url 的位置
    char* m_version_;
    char* m_host_;
    int m_content_length_;
    bool m_linger_;               // keep-alive
    char* m_content_;             // 指向缓冲区里 body 的位置
};

#endif // HTTP_HTTP_PARSER_H

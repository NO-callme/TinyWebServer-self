#include "http_parser.h"

#include <cstring>   // strpbrk / strspn / strchr / strlen
#include <cstdlib>   // atoi
#include <strings.h> // strcasecmp / strncasecmp


http_parser::http_parser()
{
    init();
}

http_parser::~http_parser()
{
}

void http_parser::init()
{
    m_check_state_ = CHECK_STATE_REQUESTLINE;

    m_read_buf_ = nullptr;
    m_read_idx_ = 0;
    m_checked_idx_ = 0;
    m_start_line_ = 0;

    m_method_ = GET;
    m_url_ = nullptr;
    m_version_ = nullptr;
    m_host_ = nullptr;
    m_content_length_ = 0;
    m_linger_ = false;
    m_content_ = nullptr;
}

// 从状态机：从读缓冲区里取出一行，把行尾的 \r\n 就地替换成 \0\0。
// 这样返回后，这一行就变成了一条合法的 C 字符串（供 strpbrk/strncasecmp 使用），
// 而且没有分配任何新内存。
http_parser::LINE_STATUS http_parser::parse_line()
{
    char temp;
    for (; m_checked_idx_ < m_read_idx_; ++m_checked_idx_) {
        temp = m_read_buf_[m_checked_idx_];
        if (temp == '\r') {
            // \r 是缓冲区最后一个字节，\n 可能在下一段数据里
            if (m_checked_idx_ + 1 == m_read_idx_) {
                return LINE_OPEN;
            }
            else if (m_read_buf_[m_checked_idx_ + 1] == '\n') {
                m_read_buf_[m_checked_idx_++] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;  // 孤立的 \r
        }
        else if (temp == '\n') {
            // 前一个字节是 \r：说明上一次 \r 在包尾触发了 LINE_OPEN，这次 \n 单独到达
            if (m_checked_idx_ > 0 && m_read_buf_[m_checked_idx_ - 1] == '\r') {
                m_read_buf_[m_checked_idx_ - 1] = '\0';
                m_read_buf_[m_checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;  // 孤立的 \n（有些客户端用 LF 换行，这里直接判错）
        }
        // 普通字节：继续向后扫
    }
    return LINE_OPEN;  // 扫到末尾都没找到完整行
}

http_parser::PARSE_RESULT http_parser::parse(char* buf, int read_idx)
{
    m_read_buf_ = buf;
    m_read_idx_ = read_idx;

    // 请求行和头部是按"行"解析的
    while (m_check_state_ != CHECK_STATE_CONTENT) {
        LINE_STATUS ls = parse_line();
        if (ls == LINE_BAD) {
            return PARSE_ERROR;
        }
        if (ls == LINE_OPEN) {
            return PARSE_MORE;  // 行还不完整，等更多数据
        }

        char* text = m_read_buf_ + m_start_line_;  // 当前行（已被 parse_line 用 \0 结尾）
        m_start_line_ = m_checked_idx_;            // 移到下一行的起点

        if (m_check_state_ == CHECK_STATE_REQUESTLINE) {
            if (parse_request_line(text) == PARSE_ERROR) {
                return PARSE_ERROR;
            }
        }
        else {  // CHECK_STATE_HEADER
            PARSE_RESULT ret = parse_headers(text);
            if (ret == PARSE_ERROR) {
                return PARSE_ERROR;
            }
            if (ret == PARSE_DONE) {
                return PARSE_DONE;  // 没有 body，请求完整
            }
            // ret == PARSE_MORE：parse_headers 已把状态切到 CONTENT，跳出循环去读 body
        }
    }

    // 到这里 m_check_state_ == CHECK_STATE_CONTENT
    return parse_content();
}

// 解析请求行，形如 "GET /index.html HTTP/1.1"（行尾已被 \0 结尾）。
// 用 strpbrk/strspn 按空格把 method / url / version 三段切开。
http_parser::PARSE_RESULT http_parser::parse_request_line(char* text)
{
    m_url_ = strpbrk(text, " \t");   // 找 method 和 url 之间的空白
    if (!m_url_) {
        return PARSE_ERROR;
    }
    *m_url_++ = '\0';                // 把空白替换成 \0，text 变成 "GET"
    char* method = text;

    if (strcasecmp(method, "GET") == 0) {
        m_method_ = GET;
    }
    else if (strcasecmp(method, "POST") == 0) {
        m_method_ = POST;
    }
    else {
        return PARSE_ERROR;          // 只支持 GET / POST
    }

    m_url_ += strspn(m_url_, " \t"); // 跳过 url 前面的空白
    m_version_ = strpbrk(m_url_, " \t"); // 找 url 和 version 之间的空白
    if (!m_version_) {
        return PARSE_ERROR;
    }
    *m_version_++ = '\0';            // url 变成独立字符串
    m_version_ += strspn(m_version_, " \t");

    if (strcasecmp(m_version_, "HTTP/1.1") != 0) {
        return PARSE_ERROR;
    }

    // 兼容绝对路径形式 http://host/path 或 https://host/path
    if (strncasecmp(m_url_, "http://", 7) == 0) {
        m_url_ += 7;
        m_url_ = strchr(m_url_, '/');
    }
    else if (strncasecmp(m_url_, "https://", 8) == 0) {
        m_url_ += 8;
        m_url_ = strchr(m_url_, '/');
    }
    if (!m_url_ || m_url_[0] != '/') {
        return PARSE_ERROR;
    }

    // 根路径 "/" 补默认首页的逻辑不放这里（属于路由，交给 http_conn 的 do_request）

    m_check_state_ = CHECK_STATE_HEADER;  // 主状态机前进
    return PARSE_MORE;
}

// 解析一行头部，形如 "Host: localhost:9006"。
// 只关心 Host / Content-Length / Connection，其它一律忽略。
http_parser::PARSE_RESULT http_parser::parse_headers(char* text)
{
    if (text[0] == '\0') {
        // 空行 = 头部结束
        if (m_content_length_ != 0) {
            m_check_state_ = CHECK_STATE_CONTENT;  // 还有 body 要读
            return PARSE_MORE;
        }
        return PARSE_DONE;  // 没有 body，请求完整
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger_ = true;
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length_ = atoi(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host_ = text;
    }

    return PARSE_MORE;
}

// 解析 body。body 是"长度"语义（由 Content-Length 决定），不是"行"语义，
// 所以这里不调用 parse_line，直接判断是否已收满 Content-Length 个字节。
http_parser::PARSE_RESULT http_parser::parse_content()
{
    // m_checked_idx_ 此时指向 body 的起点
    if (m_read_idx_ >= m_checked_idx_ + m_content_length_) {
        m_content_ = m_read_buf_ + m_checked_idx_;
        m_read_buf_[m_checked_idx_ + m_content_length_] = '\0';  // body 收尾
        return PARSE_DONE;
    }
    return PARSE_MORE;  // body 还没收全，等更多数据
}

// ---- 解析结果 getter ----
http_parser::METHOD http_parser::method() const { return m_method_; }
const char* http_parser::url() const { return m_url_; }
const char* http_parser::version() const { return m_version_; }
const char* http_parser::host() const { return m_host_; }
int http_parser::content_length() const { return m_content_length_; }
bool http_parser::linger() const { return m_linger_; }
const char* http_parser::content() const { return m_content_; }

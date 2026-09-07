#include "http_conn.h"

#include <strings.h> // strcasecmp（MIME 表用）
#include <string>

#include "db/user_model.h"


// ---- 错误响应正文 ----
static const char* g_error_400_body = "Your request has bad syntax or is inherently impossible to satisfy.\n";
static const char* g_error_403_body = "You do not have permission to get this file from this server.\n";
static const char* g_error_404_body = "The requested file was not found on this server.\n";
static const char* g_error_500_body = "There was an unusual problem serving the requested file.\n";

// 静态成员定义：默认根目录是当前目录
const char* http_conn::m_doc_root_ = ".";


// 根据文件扩展名返回 MIME 类型，未知扩展名一律 text/plain
static const char* mime_type(const char* path)
{
    const char* dot = strrchr(path, '.');
    if (!dot) {
        return "text/plain";
    }
    dot++;  // 跳过 '.'
    static const struct {
        const char* ext;
        const char* type;
    } table[] = {
        {"html", "text/html"}, {"htm", "text/html"},
        {"css",  "text/css"},  {"js",  "text/javascript"},
        {"jpg",  "image/jpeg"}, {"jpeg","image/jpeg"},
        {"png",  "image/png"},  {"gif", "image/gif"},
        {"ico",  "image/x-icon"}, {"svg","image/svg+xml"},
        {"mp4",  "video/mp4"},  {"mp3", "audio/mpeg"},
        {"txt",  "text/plain"}, {"json","application/json"},
        {"pdf",  "application/pdf"}, {"zip", "application/zip"},
    };
    for (const auto& e : table) {
        if (strcasecmp(e.ext, dot) == 0) {
            return e.type;
        }
    }
    return "text/plain";
}

// 解析 POST body：user=xxx&passwd=yyy（表单编码，学习项目不处理 URL 解码）
static bool parse_query(const char* body, std::string& user, std::string& passwd)
{
    if (!body) {
        return false;
    }
    const char* user_eq = strstr(body, "user=");
    const char* pass_eq = strstr(body, "passwd=");
    if (!user_eq || !pass_eq) {
        return false;
    }

    user_eq += 5;   // 跳过 "user="
    pass_eq += 7;   // 跳过 "passwd="

    const char* amp = strchr(user_eq, '&');
    user.assign(user_eq, amp ? amp - user_eq : strlen(user_eq));

    const char* amp2 = strchr(pass_eq, '&');
    passwd.assign(pass_eq, amp2 ? amp2 - pass_eq : strlen(pass_eq));

    return !user.empty();
}


http_conn::http_conn() : m_sockfd_(-1), m_conn_pool_(nullptr)
{
    init();
}

http_conn::~http_conn()
{
    unmap();
    if (m_sockfd_ != -1) {
        close(m_sockfd_);
    }
}

void http_conn::set_doc_root(const char* root)
{
    m_doc_root_ = root;
}

void http_conn::init(int sockfd, const sockaddr_in &addr, connection_pool *conn_pool)
{
    m_sockfd_ = sockfd;
    m_address_ = addr;
    m_conn_pool_ = conn_pool;
    init();
}

void http_conn::init()
{
    m_read_idx_ = 0;
    m_write_idx_ = 0;
    memset(m_read_buf_, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf_, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file_, '\0', FILENAME_LEN);

    m_file_address_ = nullptr;
    m_iv_[0].iov_base = nullptr; m_iv_[0].iov_len = 0;
    m_iv_[1].iov_base = nullptr; m_iv_[1].iov_len = 0;
    m_iv_count_ = 0;
    m_bytes_to_send_ = 0;
    m_bytes_have_send_ = 0;

    m_parser_.init();  // 重置解析器状态
}

void http_conn::close_conn(bool real_close)
{
    if (real_close && m_sockfd_ != -1) {
        close(m_sockfd_);
        m_sockfd_ = -1;
    }
}

bool http_conn::is_closed() const
{
    return m_sockfd_ == -1;
}

// 读数据。ET 模式下要循环 recv 直到 EAGAIN。
// 注意留 1 字节余量：解析器 parse_content 要给 body 结尾写一个 \0。
bool http_conn::read_once()
{
    if (m_read_idx_ >= READ_BUFFER_SIZE - 1) {
        return false;
    }
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd_, m_read_buf_ + m_read_idx_,
                          READ_BUFFER_SIZE - 1 - m_read_idx_, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;   // 读完了
            }
            return false;  // 真正的错误
        }
        else if (bytes_read == 0) {
            return false;  // 对端关闭
        }
        m_read_idx_ += bytes_read;
    }
    return true;
}

// 主入口：解析 → 生成响应 → 发送
void http_conn::process()
{
    http_parser::PARSE_RESULT ret = m_parser_.parse(m_read_buf_, m_read_idx_);

    if (ret == http_parser::PARSE_MORE) {
        return;   // 数据还没收全，等下次读
    }
    if (ret == http_parser::PARSE_ERROR) {
        build_error(400, "Bad Request", g_error_400_body);
    }
    else {  // PARSE_DONE
        do_request();
    }

    if (!write()) {
        close_conn();
    }
}

// 根据解析出的 url 决定返回什么：静态文件，或 403/404/500
void http_conn::do_request()
{
    const char* url = m_parser_.url();

    // 登录/注册（POST）走 DAO，不走静态文件
    if (m_parser_.method() == http_parser::POST &&
        (strcmp(url, "/login") == 0 || strcmp(url, "/register") == 0)) {
        handle_cgi();
        return;
    }

    // 拼接完整路径：doc_root + url
    snprintf(m_real_file_, FILENAME_LEN, "%s%s", m_doc_root_, url);

    // 目录 → 补 index.html（这样根路径 "/" 也能访问到首页）
    if (stat(m_real_file_, &m_file_stat_) == 0 && S_ISDIR(m_file_stat_.st_mode)) {
        size_t len = strlen(m_real_file_);
        if (len > 0 && m_real_file_[len - 1] != '/') {
            snprintf(m_real_file_ + len, FILENAME_LEN - len, "/index.html");
        }
        else {
            snprintf(m_real_file_ + len, FILENAME_LEN - len, "index.html");
        }
    }

    // 检查文件是否存在、是否可读
    if (stat(m_real_file_, &m_file_stat_) < 0) {
        build_error(404, "Not Found", g_error_404_body);
        return;
    }
    if (!(m_file_stat_.st_mode & S_IROTH)) {
        build_error(403, "Forbidden", g_error_403_body);
        return;
    }

    // 空文件：mmap 不了 0 长度，直接走空 body
    if (m_file_stat_.st_size == 0) {
        m_file_address_ = nullptr;
        build_file_response();
        return;
    }

    // 打开并 mmap 映射文件（零拷贝：文件内容不进用户态缓冲区）
    int fd = open(m_real_file_, O_RDONLY);
    if (fd < 0) {
        build_error(404, "Not Found", g_error_404_body);
        return;
    }
    m_file_address_ = (char*)mmap(0, m_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m_file_address_ == MAP_FAILED) {
        m_file_address_ = nullptr;
        build_error(500, "Internal Error", g_error_500_body);
        return;
    }

    build_file_response();
}

// 处理登录/注册：解析 body → 调 DAO → 根据结果返回不同页面
void http_conn::handle_cgi()
{
    const char* url = m_parser_.url();

    std::string user, passwd;
    if (!parse_query(m_parser_.content(), user, passwd)) {
        build_error(400, "Bad Request", g_error_400_body);
        return;
    }

    user_model model(m_conn_pool_);
    bool ok;
    bool is_login = (strcmp(url, "/login") == 0);
    if (is_login) {
        ok = model.verify_user(user, passwd);
    }
    else {
        ok = model.add_user(user, passwd);
    }

    if (is_login) {
        build_cgi_response(ok ? "登录成功" : "登录失败",
                           ok ? "欢迎回来！" : "用户名或密码错误");
    }
    else {
        build_cgi_response(ok ? "注册成功" : "注册失败",
                           ok ? "注册成功，请登录" : "用户名已存在");
    }
}

// 构造登录/注册的结果页（动态生成 HTML，不读文件）
void http_conn::build_cgi_response(const char* title, const char* msg)
{
    std::string body = "<html><head><meta charset=\"utf-8\"></head><body><h2>";
    body += title;
    body += "</h2><p>";
    body += msg;
    body += "</p></body></html>";

    add_status_line(200, "OK");
    add_response("Content-Type: text/html; charset=utf-8\r\n");
    add_content_length((int)body.size());
    add_linger();
    add_blank_line();
    add_content(body.c_str());

    m_iv_[0].iov_base = m_write_buf_;
    m_iv_[0].iov_len = m_write_idx_;
    m_iv_count_ = 1;
    m_bytes_to_send_ = m_write_idx_;
    m_bytes_have_send_ = 0;
}

// 构造错误响应（状态行 + 头 + 错误正文），整个都在写缓冲区里
void http_conn::build_error(int status, const char* title, const char* body)
{
    add_status_line(status, title);
    add_response("Content-Type: text/html\r\n");
    add_content_length(strlen(body));
    add_linger();
    add_blank_line();
    add_content(body);

    m_iv_[0].iov_base = m_write_buf_;
    m_iv_[0].iov_len = m_write_idx_;
    m_iv_count_ = 1;
    m_bytes_to_send_ = m_write_idx_;
    m_bytes_have_send_ = 0;
}

// 构造 200 文件响应。有内容时用 writev 一次发「响应头 + mmap 文件」两块内存
void http_conn::build_file_response()
{
    add_status_line(200, "OK");
    add_content_type(m_real_file_);

    if (m_file_address_ != nullptr) {
        // 有内容：iv[0]=响应头（写缓冲区），iv[1]=文件（mmap 地址）
        add_content_length(m_file_stat_.st_size);
        add_linger();
        add_blank_line();
        m_iv_[0].iov_base = m_write_buf_;
        m_iv_[0].iov_len = m_write_idx_;
        m_iv_[1].iov_base = m_file_address_;
        m_iv_[1].iov_len = m_file_stat_.st_size;
        m_iv_count_ = 2;
        m_bytes_to_send_ = m_write_idx_ + m_file_stat_.st_size;
    }
    else {
        // 空文件：发一个空 body
        const char* empty = "<html><body></body></html>";
        add_content_length(strlen(empty));
        add_linger();
        add_blank_line();
        add_content(empty);
        m_iv_[0].iov_base = m_write_buf_;
        m_iv_[0].iov_len = m_write_idx_;
        m_iv_count_ = 1;
        m_bytes_to_send_ = m_write_idx_;
    }
    m_bytes_have_send_ = 0;
}

// 发送响应。writev 把「响应头 + 文件」两块内存一次发出去。
// 返回 true 表示连接保持，false 表示该关闭连接。
bool http_conn::write()
{
    while (m_bytes_to_send_ > 0) {
        int n = writev(m_sockfd_, m_iv_, m_iv_count_);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;  // 没发完，稍后重试（M7 会挂 EPOLLOUT）
            }
            unmap();
            return false;  // 发送出错，关闭连接
        }
        m_bytes_have_send_ += n;
        m_bytes_to_send_ -= n;

        // 调整 iovec：响应头发完后，把发送偏移切到文件内容上
        if (m_iv_count_ == 2 && m_bytes_have_send_ >= (int)m_iv_[0].iov_len) {
            m_iv_[0].iov_len = 0;
            m_iv_[1].iov_base = m_file_address_ + (m_bytes_have_send_ - m_write_idx_);
            m_iv_[1].iov_len = m_bytes_to_send_;
        }
        else {
            m_iv_[0].iov_base = m_write_buf_ + m_bytes_have_send_;
            m_iv_[0].iov_len -= n;
        }

        if (m_bytes_to_send_ <= 0) {
            unmap();
            if (m_parser_.linger()) {
                init();   // keep-alive：重置状态，等下一个请求
                return true;
            }
            return false;  // 关闭连接
        }
    }
    return true;
}

void http_conn::unmap()
{
    if (m_file_address_) {
        munmap(m_file_address_, m_file_stat_.st_size);
        m_file_address_ = nullptr;
    }
}

// ---- 往写缓冲区追加内容 ----
bool http_conn::add_response(const char* format, ...)
{
    if (m_write_idx_ >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf_ + m_write_idx_,
                        WRITE_BUFFER_SIZE - m_write_idx_, format, arg_list);
    va_end(arg_list);
    if (len < 0 || len >= WRITE_BUFFER_SIZE - m_write_idx_) {
        return false;  // 缓冲区不够（实际上响应头远小于 1024，几乎不会触发）
    }
    m_write_idx_ += len;
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("HTTP/1.1 %d %s\r\n", status, title);
}

bool http_conn::add_content_length(int len)
{
    return add_response("Content-Length: %d\r\n", len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n",
                        m_parser_.linger() ? "keep-alive" : "close");
}

bool http_conn::add_content_type(const char* path)
{
    return add_response("Content-Type: %s\r\n", mime_type(path));
}

bool http_conn::add_blank_line()
{
    return add_response("\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

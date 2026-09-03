#include "http/http_parser.h"
#include <cstring>
#include <cstdio>

static int fail = 0;
static void check(bool ok, const char* name) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fail++;
}

int main() {
    http_parser p;

    // 1. GET 无 body
    {
        char buf[2048];
        strcpy(buf, "GET /index.html HTTP/1.1\r\nHost: localhost:9006\r\nConnection: keep-alive\r\n\r\n");
        p.init();
        auto r = p.parse(buf, (int)strlen(buf));
        check(r == http_parser::PARSE_DONE, "GET -> DONE");
        check(p.method() == http_parser::GET, "method == GET");
        check(strcmp(p.url(), "/index.html") == 0, "url == /index.html");
        check(strcmp(p.version(), "HTTP/1.1") == 0, "version == HTTP/1.1");
        check(strcmp(p.host(), "localhost:9006") == 0, "host == localhost:9006");
        check(p.linger(), "keep-alive == true");
        check(p.content_length() == 0, "content_length == 0");
    }

    // 2. POST 有 body
    {
        char buf[2048];
        strcpy(buf, "POST /login HTTP/1.1\r\nHost: h\r\nContent-Length: 15\r\n\r\nuser=x&passwd=y");
        p.init();
        auto r = p.parse(buf, (int)strlen(buf));
        check(r == http_parser::PARSE_DONE, "POST -> DONE");
        check(p.method() == http_parser::POST, "method == POST");
        check(p.content_length() == 15, "content_length == 15");
        check(strcmp(p.content(), "user=x&passwd=y") == 0, "content == user=x&passwd=y");
    }

    // 3. 拆包：请求分两个包到达
    {
        const char* full = "GET /a HTTP/1.1\r\nHost: x\r\n\r\n";
        char buf[2048];
        p.init();
        memcpy(buf, full, 19);                 // 只到 "Ho"，行没读完
        check(p.parse(buf, 19) == http_parser::PARSE_MORE, "part1 -> MORE");
        memcpy(buf, full, strlen(full));       // 补全
        check(p.parse(buf, (int)strlen(full)) == http_parser::PARSE_DONE, "part2 -> DONE");
        check(strcmp(p.host(), "x") == 0, "拆包后 host 正确");
    }

    // 4. 不支持的 method
    {
        char buf[2048];
        strcpy(buf, "DELETE /x HTTP/1.1\r\n\r\n");
        p.init();
        check(p.parse(buf, (int)strlen(buf)) == http_parser::PARSE_ERROR, "DELETE -> ERROR");
    }

    // 5. 请求行缺 version
    {
        char buf[2048];
        strcpy(buf, "GET /x\r\n\r\n");
        p.init();
        check(p.parse(buf, (int)strlen(buf)) == http_parser::PARSE_ERROR, "缺 version -> ERROR");
    }

    // 6. 版本不是 HTTP/1.1
    {
        char buf[2048];
        strcpy(buf, "GET /x HTTP/1.0\r\n\r\n");
        p.init();
        check(p.parse(buf, (int)strlen(buf)) == http_parser::PARSE_ERROR, "HTTP/1.0 -> ERROR");
    }

    printf("\n%s (失败 %d 项)\n", fail == 0 ? "全部通过" : "有失败", fail);
    return fail;
}

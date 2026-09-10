#!/usr/bin/env python3
"""TinyWebServer 并发压测脚本（仅依赖标准库）。

用法示例：
  ./tests/stress.py --threads 200 --conns 2 --reqs 50
  # 200 个并发线程，每个线程依次建 2 条 keep-alive 连接、每条发 50 个请求 = 20000 请求

通过条件：所有响应都是 200，否则以非零退出码报错。
"""

import argparse
import http.client
import sys
import threading
import time


def worker(host, port, path, conns, reqs, results, idx):
    ok = 0
    fail = 0
    for _ in range(conns):
        try:
            conn = http.client.HTTPConnection(host, port, timeout=10)
            for _ in range(reqs):
                conn.request("GET", path)
                resp = conn.getresponse()
                resp.read()
                if resp.status == 200:
                    ok += 1
                else:
                    fail += 1
            conn.close()
        except Exception:
            # 连接失败，该连接上的请求全部计为失败
            fail += reqs
    results[idx] = (ok, fail)


def main():
    ap = argparse.ArgumentParser(description="TinyWebServer 并发压测")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9006)
    ap.add_argument("--path", default="/index.html")
    ap.add_argument("--threads", type=int, default=100, help="并发线程数")
    ap.add_argument("--conns", type=int, default=1, help="每线程的连接数")
    ap.add_argument("--reqs", type=int, default=100, help="每连接请求数(keep-alive)")
    args = ap.parse_args()

    results = [(0, 0)] * args.threads
    threads = []
    start = time.time()
    for i in range(args.threads):
        t = threading.Thread(
            target=worker,
            args=(args.host, args.port, args.path, args.conns, args.reqs, results, i))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    elapsed = time.time() - start

    ok = sum(r[0] for r in results)
    fail = sum(r[1] for r in results)
    total = ok + fail

    print(f"并发线程:   {args.threads}")
    print(f"总请求数:   {total}")
    print(f"成功(200):  {ok}")
    print(f"失败:       {fail}")
    print(f"耗时:       {elapsed:.2f}s")
    if elapsed > 0:
        print(f"QPS:        {total / elapsed:.1f}")

    if fail > 0:
        print("压测存在失败请求！")
        sys.exit(1)
    print("压测通过：无失败请求 ✓")


if __name__ == "__main__":
    main()

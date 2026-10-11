#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_mcp_client.py —— 在 C++ 单测之外，先用 Python 验证 mock MCP server 的协议正确性。

如果这个脚本都跑不过，C++ 侧的 MCPClient 单测失败就没意义了。
运行：
    python test_mcp_client.py
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SERVER = os.path.join(ROOT, "scripts", "mock_mcp_server.py")

PROTOCOL_VERSION = "2024-11-05"


class MCPClient:
    """最小 JSON-RPC over stdio 客户端，用于验证 server 行为。"""

    def __init__(self, proc):
        self.proc = proc
        self.next_id = 0

    def request(self, method, params=None, timeout=15):
        self.next_id += 1
        req = {"jsonrpc": "2.0", "id": self.next_id, "method": method,
               "params": params or {}}
        self.proc.stdin.write(json.dumps(req, ensure_ascii=False) + "\n")
        self.proc.stdin.flush()
        deadline_line = self.proc.stdout.readline()
        if not deadline_line:
            raise RuntimeError("server closed connection")
        return json.loads(deadline_line)

    def notify(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        self.proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
        self.proc.stdin.flush()


def main():
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    proc = subprocess.Popen(
        [sys.executable, SERVER],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8",
        env=env, bufsize=1)
    c = MCPClient(proc)

    failures = []

    def check(name, cond, detail=""):
        status = "PASS" if cond else "FAIL"
        print("%s  %s%s" % (status, name, ("  <- " + detail) if
                             (not cond and detail) else ""))
        if not cond:
            failures.append(name)

    try:
        # 1) initialize
        r = c.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "test", "version": "1.0"},
        })
        check("initialize returns protocolVersion",
              r.get("result", {}).get("protocolVersion") == PROTOCOL_VERSION,
              json.dumps(r, ensure_ascii=False)[:200])
        c.notify("notifications/initialized")

        # 2) tools/list
        r = c.request("tools/list")
        tools = r.get("result", {}).get("tools", [])
        names = {t["name"] for t in tools}
        check("tools/list returns 5 tools", len(tools) == 5, str(names))
        check("tools have name/description/inputSchema",
              all({"name", "description", "inputSchema"} <= set(t) for t in tools))

        # 3) tools/call 正常路径
        r = c.request("tools/call", {"name": "echo",
                                     "arguments": {"text": "你好"}})
        content = r.get("result", {}).get("content", [])
        check("echo returns text",
              content and content[0].get("text") == "你好",
              json.dumps(r, ensure_ascii=False)[:200])

        # 4) 业务错误走 isError，不是 JSON-RPC error
        r = c.request("tools/call", {"name": "divide",
                                     "arguments": {"a": 1, "b": 0}})
        check("divide by zero -> isError=true",
              r.get("result", {}).get("isError") is True,
              json.dumps(r, ensure_ascii=False)[:200])
        check("divide by zero has no jsonrpc error", "error" not in r)

        # 5) 未知工具
        r = c.request("tools/call", {"name": "nope", "arguments": {}})
        check("unknown tool -> isError",
              r.get("result", {}).get("isError") is True)

        # 6) 服务器异常 -> JSON-RPC error
        r = c.request("tools/call", {"name": "explode", "arguments": {}})
        check("explode -> jsonrpc error", "error" in r,
              json.dumps(r, ensure_ascii=False)[:200])

        # 7) 未知方法
        r = c.request("nonexistent/method")
        check("unknown method -> error -32601",
              r.get("error", {}).get("code") == -32601,
              json.dumps(r, ensure_ascii=False)[:200])
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print()
    if failures:
        print("FAILED: %s" % ", ".join(failures))
        return 1
    print("all mock mcp server checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
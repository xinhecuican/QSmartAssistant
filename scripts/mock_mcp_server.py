#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mock_mcp_server.py —— 一个最小可用的 MCP server，用于自测 MCPClient。

实现了 MCP stdio transport 的三个方法：
  initialize / notifications/initialized / tools/list / tools/call

暴露两个工具，用来覆盖几种典型情况：
  echo    —— 正常返回 content 数组
  add     —— 带参数校验，参数缺失时用 isError 表达业务失败
  explode —— 抛异常，测试 JSON-RPC error 路径
  slow    —— 故意超时，测试客户端超时处理

用法（stdio，客户端通过管道驱动）：
    python mock_mcp_server.py

在测试里由 MCPClient 以 QProcess 拉起。
"""

import json
import sys
import time


TOOLS = [
    {
        "name": "echo",
        "description": "Echo back the given text.",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string", "description": "text to echo"}},
            "required": ["text"],
        },
    },
    {
        "name": "add",
        "description": "Add two integers and return the sum.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "a": {"type": "integer"},
                "b": {"type": "integer"},
            },
            "required": ["a", "b"],
        },
    },
    {
        "name": "divide",
        "description": "Divide a by b. Fails when b is 0.",
        "inputSchema": {
            "type": "object",
            "properties": {"a": {"type": "number"}, "b": {"type": "number"}},
            "required": ["a", "b"],
        },
    },
    {
        "name": "explode",
        "description": "Always raises an error. For testing error handling.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "slow",
        "description": "Sleeps for a long time. For testing timeouts.",
        "inputSchema": {
            "type": "object",
            "properties": {"seconds": {"type": "number"}},
        },
    },
]


def send(obj):
    """逐行写出一条 JSON-RPC 消息（MCP stdio 用换行分隔，不用 Content-Length）。"""
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def ok(req_id, result):
    send({"jsonrpc": "2.0", "id": req_id, "result": result})


def err(req_id, code, message):
    send({"jsonrpc": "2.0", "id": req_id,
          "error": {"code": code, "message": message}})


def text_content(text, is_error=False):
    result = {"content": [{"type": "text", "text": text}]}
    if is_error:
        result["isError"] = True
    return result


def handle_call(params):
    name = params.get("name", "")
    args = params.get("arguments", {}) or {}

    if name == "echo":
        return text_content(str(args.get("text", "")))
    if name == "add":
        a, b = args.get("a"), args.get("b")
        if not isinstance(a, (int, float)) or not isinstance(b, (int, float)):
            return text_content("error: a and b must be numbers", True)
        return text_content(str(a + b))
    if name == "divide":
        try:
            a = float(args.get("a", 0))
            b = float(args.get("b", 0))
        except (TypeError, ValueError):
            return text_content("error: a and b must be numbers", True)
        if b == 0:
            return text_content("error: division by zero", True)
        return text_content(str(a / b))
    if name == "slow":
        time.sleep(float(args.get("seconds", 999)))
        return text_content("done")
    if name == "explode":
        return None  # 由调用方转成 JSON-RPC error
    return text_content("unknown tool: %s" % name, True)


def main():
    # stderr 是日志通道，stdout 只能是 JSON-RPC
    sys.stderr.write("mock mcp server started\n")
    sys.stderr.flush()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as exc:
            sys.stderr.write("bad json: %s\n" % exc)
            continue

        method = req.get("method", "")
        req_id = req.get("id")
        params = req.get("params", {}) or {}

        # 通知（无 id）不回响应
        if req_id is None:
            continue

        if method == "initialize":
            ok(req_id, {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "mock-mcp", "version": "1.0"},
            })
        elif method == "tools/list":
            ok(req_id, {"tools": TOOLS})
        elif method == "tools/call":
            result = handle_call(params)
            if result is None:
                err(req_id, -32603, "internal error: tool exploded")
            else:
                ok(req_id, result)
        elif method == "ping":
            ok(req_id, {})
        else:
            err(req_id, -32601, "method not found: %s" % method)


if __name__ == "__main__":
    main()
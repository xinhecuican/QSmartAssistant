"""最小的 Rasa HTTP 服务替身，用于在没有安装 Rasa 的机器上验证
Conversation/NLU/rasanlu.cpp 的调用链路（/status + /model/parse）。

安装真正的 Rasa 需要 Python 3.8~3.10 + torch，体积很大；只想确认
NLU 链路是否打通时，用这个替身即可：

用法：
    python scripts/mock_rasa.py [port]     # 默认 5005

它会按 QSmartAssistant 的 rasa 配置返回：
    GET  /status       -> {"model_file": "<--model 参数指定>"}
    POST /model/parse  -> CHANGE_VOL 意图 + trend=大 实体

然后用它跑单测：
    QSmartAssistant.exe rasaNlu
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

MODEL_FILE = os.environ.get("MOCK_RASA_MODEL", "default.tar.gz")


class Handler(BaseHTTPRequestHandler):
    def _send(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/status"):
            self._send({"model_file": MODEL_FILE, "model_id": "mock", "num_active_training_jobs": 0})
        else:
            self._send({"error": "not found"}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b"{}"
        try:
            text = json.loads(raw.decode("utf-8")).get("text", "")
        except Exception:
            text = ""
        print("PARSE <--", text, flush=True)
        if self.path.startswith("/model/parse"):
            self._send({
                "text": text,
                "intent": {"name": "CHANGE_VOL", "confidence": 0.93},
                "intent_ranking": [
                    {"name": "CHANGE_VOL", "confidence": 0.93},
                    {"name": "MUSICINFO", "confidence": 0.04},
                ],
                "entities": [{
                    "entity": "trend",
                    "value": "大",
                    "start": text.find("大") if "大" in text else 0,
                    "end": (text.find("大") + 1) if "大" in text else 1,
                    "confidence": 0.88,
                    "extractor": "DIETClassifier",
                }],
            })
        else:
            self._send({"error": "not found"}, 404)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 5005
    print("mock rasa listening on 127.0.0.1:%d (model_file=%s)" % (port, MODEL_FILE), flush=True)
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()

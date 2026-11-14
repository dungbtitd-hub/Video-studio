#!/usr/bin/env python3
"""Example translator for Video Studio's open subtitle protocol (docs/subtitles.md).

Use it as a "command" connector on desktop (Dịch bằng AI… > Chương trình trên máy này):
    python3 tools/subtitle-translate/echo_translator.py
or serve it over HTTP for the "API mở Video Studio" connector (desktop and phone):
    python3 tools/subtitle-translate/echo_translator.py --serve 8765
    -> address: http://<this computer>:8765/translate

It only tags each line with the target language. Replace `translate()` with a call to the AI
you want (a company model, a local model, a translation service...).
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


def translate(req):
    target = req.get("target_lang", "")
    return {"items": [{"id": it["id"], "text": f"[{target}] {it['text']}"} for it in req.get("items", [])]}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        try:
            out = json.dumps(translate(json.loads(body)), ensure_ascii=False).encode("utf-8")
            self.send_response(200)
        except Exception as e:  # tell Video Studio what went wrong
            out = json.dumps({"error": {"message": str(e)}}).encode("utf-8")
            self.send_response(400)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--serve":
        HTTPServer(("0.0.0.0", int(sys.argv[2])), Handler).serve_forever()
    else:
        req = json.load(sys.stdin)
        sys.stdout.write(json.dumps(translate(req), ensure_ascii=False))

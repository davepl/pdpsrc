#!/usr/bin/env python3
"""A deliberately small, bounded chat API for the public PDP-AI page."""
import collections
import hmac
import ipaddress
import json
import os
from pathlib import Path
import socket
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MODEL = "qwen3.8-flash-next"
UPSTREAM = os.environ.get("PDP_AI_UPSTREAM", "http://127.0.0.1:8080")
KEY_FILE = Path(os.environ.get("PDP_AI_MODEL_KEY", str(Path.home() / ".config/llama-coding/api-key")))
PROXY_FILE = Path(os.environ.get("PDP_AI_PROXY_KEY", str(Path.home() / ".config/pdp-ai/proxy-key")))
MAX_BODY = 65536
MAX_HISTORY = 25
MAX_CONTEXT_CHARS = 24000
ORIGINS = {
    "https://pdp1173.com", "https://www.pdp1173.com",
    "http://pdp1173.com", "http://www.pdp1173.com",
    "https://davepl.dyndns.org", "http://davepl.dyndns.org",
    "http://192.168.1.29", "http://192.168.1.26",
}
# Keep the full, reviewed persona and site facts beside the bridge, not in the browser.
SYSTEM = Path(__file__).with_name("gary-system-prompt.txt").read_text(encoding="utf-8")


def validate_messages(payload):
    if not isinstance(payload, dict) or set(payload) != {"messages"}:
        raise ValueError("Send a conversation in the messages field.")
    messages = payload["messages"]
    if not isinstance(messages, list) or not 1 <= len(messages) <= MAX_HISTORY:
        raise ValueError("This conversation is too long. Start a new chat.")
    result = []
    for index, message in enumerate(messages):
        if not isinstance(message, dict) or set(message) != {"role", "content"}:
            raise ValueError("Invalid conversation format.")
        expected = "user" if index % 2 == 0 else "assistant"
        text = message["content"]
        if message["role"] != expected or not isinstance(text, str) or not text.strip():
            raise ValueError("Invalid conversation sequence.")
        if len(text) > (2000 if expected == "user" else 12000):
            raise ValueError("Please shorten your message.")
        result.append({"role": expected, "content": text})
    if len(result) % 2 != 1:
        raise ValueError("A conversation must end with your message.")
    if sum(len(item["content"]) for item in result) > MAX_CONTEXT_CHARS:
        raise ValueError("This conversation is too long. Start a new chat.")
    return result


class RateLimits:
    def __init__(self):
        self.lock = threading.Lock()
        self.clients = {}
        self.all = collections.deque()

    def allow(self, client):
        now = time.monotonic()
        with self.lock:
            for key in list(self.clients):
                q = self.clients[key]
                while q and q[0] <= now - 60:
                    q.popleft()
                if not q:
                    del self.clients[key]
            while self.all and self.all[0] <= now - 60:
                self.all.popleft()
            if len(self.clients) >= 2048 and client not in self.clients:
                return False
            q = self.clients.setdefault(client, collections.deque())
            if len(q) >= 12 or len(self.all) >= 60:
                return False
            q.append(now)
            self.all.append(now)
            return True


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32

    def __init__(self, address):
        self.proxy_key = PROXY_FILE.read_text().strip()
        if len(self.proxy_key) < 32:
            raise RuntimeError("Missing proxy credential")
        self.generation = threading.BoundedSemaphore(1)
        self.limits = RateLimits()
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "PDP-AI"
    sys_version = ""

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        # Never log prompts, answers, credentials, headers or client IPs.
        pass

    def trusted(self):
        key = self.headers.get("X-PDP-AI-Key", "")
        if not hmac.compare_digest(key, self.server.proxy_key):
            self.reply(403, {"error": "Access through the PDP-AI website."})
            return False
        origin = self.headers.get("Origin")
        if origin and origin not in ORIGINS:
            self.reply(403, {"error": "This origin is not allowed."})
            return False
        return True

    def common_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        origin = self.headers.get("Origin")
        if origin in ORIGINS:
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")

    def reply(self, status, payload):
        data = json.dumps(payload, ensure_ascii=False).encode()
        self.send_response(status)
        self.common_headers()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        if status == 429:
            self.send_header("Retry-After", "10")
        self.end_headers()
        self.close_connection = True
        self.wfile.write(data)

    def do_OPTIONS(self):
        if not self.trusted():
            return
        if self.path not in {"/chat", "/health"}:
            self.reply(404, {"error": "Not found."})
            return
        self.send_response(204)
        self.common_headers()
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Max-Age", "600")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        if not self.trusted():
            return
        if self.path != "/health":
            self.reply(404, {"error": "Not found."})
            return
        try:
            with urllib.request.urlopen(UPSTREAM + "/health", timeout=3) as response:
                ready = response.status == 200
            self.reply(200 if ready else 503, {"ok": ready, "model": MODEL})
        except (OSError, urllib.error.URLError):
            self.reply(503, {"ok": False, "error": "The AI is temporarily offline."})

    def event(self, kind, payload):
        line = "event: " + kind + "\ndata: " + json.dumps(payload, ensure_ascii=False) + "\n\n"
        self.wfile.write(line.encode())
        self.wfile.flush()

    def do_POST(self):
        streaming = False
        acquired = False
        try:
            if not self.trusted():
                return
            if self.path != "/chat":
                self.reply(404, {"error": "Not found."})
                return
            if self.headers.get("Transfer-Encoding"):
                self.reply(400, {"error": "A Content-Length is required."})
                return
            if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
                self.reply(415, {"error": "Use JSON for chat requests."})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                length = 0
            if not 0 < length <= MAX_BODY:
                self.reply(413, {"error": "Please shorten your message."})
                return
            try:
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ValueError("Incomplete request.")
                messages = validate_messages(json.loads(raw))
            except (ValueError, UnicodeError, RecursionError):
                self.reply(400, {"error": "Please use a valid conversation and keep your message under 2,000 characters."})
                return
            client = self.headers.get("X-PDP-AI-Client", "").strip()
            try:
                client = str(ipaddress.ip_address(client))
            except ValueError:
                client = self.client_address[0]
            if not self.server.limits.allow(client):
                self.reply(429, {"error": "A short breather: please try again in a minute."})
                return
            acquired = self.server.generation.acquire(blocking=False)
            if not acquired:
                self.reply(429, {"error": "The AI is answering another visitor. Try again in a few seconds."})
                return
            key = KEY_FILE.read_text().strip()
            payload = {
                "model": MODEL,
                "messages": [{"role": "system", "content": SYSTEM}] + messages,
                "stream": True,
                "max_tokens": 1024,
                "temperature": 0.7,
                "reasoning_effort": "none",
                "chat_template_kwargs": {"enable_thinking": False},
            }
            request = urllib.request.Request(
                UPSTREAM + "/v1/chat/completions",
                data=json.dumps(payload).encode(),
                headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"},
            )
            started = time.monotonic()
            with urllib.request.urlopen(request, timeout=90) as response:
                self.send_response(200)
                self.common_headers()
                self.send_header("Content-Type", "text/event-stream; charset=utf-8")
                self.send_header("X-Accel-Buffering", "no")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                streaming = True
                self.event("ready", {"ok": True})
                produced = False
                finish = "stop"
                for raw_line in response:
                    if time.monotonic() - started > 120:
                        self.event("error", {"error": "The answer took too long. Please try a shorter question."})
                        return
                    if not raw_line.startswith(b"data: "):
                        continue
                    if raw_line.strip() == b"data: [DONE]":
                        break
                    item = json.loads(raw_line[6:])
                    if "error" in item:
                        raise ValueError("Upstream generation error")
                    choices = item.get("choices", [])
                    if not choices:
                        continue
                    delta = choices[0].get("delta", {}).get("content")
                    if isinstance(delta, str) and delta:
                        produced = True
                        self.event("delta", {"text": delta})
                    finish = choices[0].get("finish_reason") or finish
                if not produced:
                    self.event("error", {"error": "The AI returned an empty answer. Please try again."})
                    return
                self.event("done", {"finish_reason": finish, "seconds": round(time.monotonic() - started, 1)})
        except (BrokenPipeError, ConnectionResetError):
            pass  # Closing the upstream response also cancels disconnected generations.
        except (OSError, urllib.error.URLError, ValueError, KeyError, RecursionError):
            payload = {"error": "The AI connection was interrupted. Please try again."}
            try:
                if streaming:
                    self.event("error", payload)
                else:
                    self.reply(503, payload)
            except OSError:
                pass
        finally:
            if acquired:
                self.server.generation.release()


if __name__ == "__main__":
    address = (os.environ.get("PDP_AI_BIND", "127.0.0.1"), int(os.environ.get("PDP_AI_PORT", "8091")))
    print("PDP-AI chat bridge starting", flush=True)
    Server(address).serve_forever()

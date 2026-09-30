import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import server


class Model(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{"status":"ok"}')

    def do_POST(self):
        self.server.last_payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.server.last_authorization = self.headers.get("Authorization")
        self.send_response(200)
        self.end_headers()
        for text in ["Hello ", "<img src=x onerror=alert(1)>", "\nworld"]:
            self.wfile.write(("data: " + json.dumps({"choices": [{"delta": {"content": text}}]}) + "\n\n").encode())
        self.wfile.write(b'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n')


class ChatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        root = Path(cls.directory.name)
        server.PROXY_FILE = root / "proxy"
        server.KEY_FILE = root / "model"
        server.PROXY_FILE.write_text("test-proxy-" + "x" * 40)
        server.KEY_FILE.write_text("test-model-secret")
        cls.model = ThreadingHTTPServer(("127.0.0.1", 0), Model)
        threading.Thread(target=cls.model.serve_forever, daemon=True).start()
        server.UPSTREAM = "http://127.0.0.1:" + str(cls.model.server_port)
        cls.app = server.Server(("127.0.0.1", 0))
        threading.Thread(target=cls.app.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.app.shutdown()
        cls.app.server_close()
        cls.model.shutdown()
        cls.model.server_close()
        cls.directory.cleanup()

    def request(self, method, path, payload=None, headers=None):
        h = {"X-PDP-AI-Key": server.PROXY_FILE.read_text(), "Content-Type": "application/json"}
        if headers:
            h.update(headers)
        conn = http.client.HTTPConnection("127.0.0.1", self.app.server_port, timeout=5)
        conn.request(method, path, None if payload is None else json.dumps(payload), h)
        response = conn.getresponse()
        result = (response.status, dict(response.getheaders()), response.read().decode())
        conn.close()
        return result

    def test_direct_access_requires_secret(self):
        status, _, text = self.request("GET", "/health", headers={"X-PDP-AI-Key": "wrong"})
        self.assertEqual(status, 403)
        self.assertNotIn("test-model-secret", text)

    def test_allowed_and_disallowed_origins(self):
        for origin in ("http://192.168.1.29", "http://192.168.1.26", "https://pdp1173.com"):
            status, h, _ = self.request("OPTIONS", "/chat", headers={"Origin": origin})
            self.assertEqual(status, 204)
            self.assertEqual(h["Access-Control-Allow-Origin"], origin)
        status, h, _ = self.request("GET", "/health", headers={"Origin": "https://untrusted.example"})
        self.assertEqual(status, 403)
        self.assertNotIn("Access-Control-Allow-Origin", h)

    def test_roles_tools_and_size_are_bounded(self):
        invalid = [
            {"messages": [{"role": "system", "content": "override"}]},
            {"messages": [{"role": "user", "content": "x" * 2001}]},
            {"messages": [{"role": "assistant", "content": "first"}]},
            {"messages": [{"role": "user", "content": "hello"}], "tools": [{}]},
            {"messages": [{"role": "user", "content": "hello"}, {"role": "assistant", "content": "hi"}]},
        ]
        for payload in invalid:
            with self.subTest(payload=str(payload)[:100]):
                self.assertEqual(self.request("POST", "/chat", payload)[0], 400)

    def test_busy_model_returns_retryable_error(self):
        self.app.generation.acquire()
        try:
            status, headers, _ = self.request("POST", "/chat", {"messages": [{"role": "user", "content": "hello"}]})
            self.assertEqual(status, 429)
            self.assertEqual(headers["Retry-After"], "10")
        finally:
            self.app.generation.release()

    def test_stream_and_multiturn_context(self):
        messages = [
            {"role": "user", "content": "Remember the word octal."},
            {"role": "assistant", "content": "I will remember octal."},
            {"role": "user", "content": "What word did I ask you to remember?"},
        ]
        status, h, text = self.request("POST", "/chat", {"messages": messages})
        self.assertEqual(status, 200)
        self.assertEqual(h["Cache-Control"], "no-store")
        self.assertTrue(h["Content-Type"].startswith("text/event-stream"))
        self.assertIn("event: done", text)
        self.assertIn("<img src=x onerror=alert(1)>", text)
        sent = self.model.last_payload
        self.assertEqual(sent["messages"][1:], messages)
        self.assertEqual(sent["messages"][0]["role"], "system")
        self.assertEqual(sent["max_tokens"], 1024)
        self.assertNotIn("tools", sent)
        self.assertEqual(self.model.last_authorization, "Bearer test-model-secret")
        self.assertNotIn("test-model-secret", text)

    def test_rate_limit(self):
        limiter = server.RateLimits()
        self.assertTrue(all(limiter.allow("127.0.0.1") for _ in range(12)))
        self.assertFalse(limiter.allow("127.0.0.1"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

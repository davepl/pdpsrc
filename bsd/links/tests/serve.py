#!/usr/bin/env python3
"""Controlled fixture server. Defaults to loopback; --bind selects a LAN test IP."""
import argparse, html, http.server, pathlib, urllib.parse
p = argparse.ArgumentParser()
p.add_argument('--bind', default='127.0.0.1')
p.add_argument('--port', default=8765, type=int)
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parent
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kw):
        super().__init__(*args, directory=str(root), **kw)
    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        if u.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/page.html')
            self.send_header('Content-Length', '0')
            self.end_headers()
        elif u.path == '/chunked':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=us-ascii')
            self.send_header('Transfer-Encoding', 'chunked')
            self.end_headers()
            for chunk in (b'<html><body><h1>Chunked', b' works</h1></body></html>'):
                self.wfile.write(('%x\r\n' % len(chunk)).encode() + chunk + b'\r\n')
            self.wfile.write(b'0\r\n\r\n')
        elif u.path == '/utf8':
            body = '<html><body><h1>Unicode</h1><p>caf\u00e9 \u2014 PDP-11</p></body></html>'.encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif u.path == '/search':
            q = urllib.parse.parse_qs(u.query).get('q', [''])[0]
            body = ('<html><body><h1>Form received</h1><p>q=' + html.escape(q) + '</p><a href="/page.html">Back</a></body></html>').encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=us-ascii')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()
http.server.HTTPServer((a.bind, a.port), Handler).serve_forever()

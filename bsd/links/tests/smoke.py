#!/usr/bin/env python3
"""Modern-host regressions; NOT evidence of native PDP-11 execution."""
import pathlib, subprocess, tempfile, threading, http.server
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/'upstream/links'
page=root/'tests/page.html'
def run(url):
    with tempfile.TemporaryDirectory() as home:
        p=subprocess.run([str(exe), '-no-connect', '-dump', url], env={'HOME':home,'PATH':'/usr/bin:/bin','TERM':'vt100'},capture_output=True,timeout=20)
        assert p.returncode==0,(p.returncode,p.stderr)
        text=p.stdout.decode('ascii',errors='replace')
        for token in ['Minerva','PDP-11/73','2.11BSD','Next page','A & B < C','[ Go ]','Cafe - PDP-11.']:
            assert token in text,(token,text)
        assert '<table' not in text
        return text
print(run(str(page)))
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self,*a,**kw): super().__init__(*a,directory=str(root/'tests'),**kw)
    def log_message(self,*a): pass
server=http.server.HTTPServer(('127.0.0.1',0),Handler)
t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
try: run('http://127.0.0.1:%d/page.html'%server.server_port)
finally: server.shutdown();server.server_close()
print('PASS: compact-profile local HTML and HTTP rendering, table, link, entities, form')

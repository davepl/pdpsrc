#!/usr/bin/env python3
"""Verify compact frame fallbacks and real keyboard HTTP GET form submission."""
import os,pty,select,subprocess,termios,fcntl,struct,time,tempfile,re,sys,threading,http.server
from pathlib import Path
exe=sys.argv[1];requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  requests.append(self.path)
  data=(b'<html><body><h1>RESULT-OK</h1></body></html>' if self.path.startswith('/results') else b'<html><body><form method="GET" action="/results"><input name="q" value="test"><input type="submit" value="Search"></form></body></html>')
  self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
 def log_message(self,*a):pass
server=http.server.HTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory() as home:
 page=Path(home)/'frames.html';page.write_text('<html><frameset cols="*,*"><frame name="Alpha" src="a.html"><frame name="Beta" src="b.html"><noframes>Fallback content</noframes></frameset><iframe name="Gamma" src="c.html"></iframe></html>')
 out=subprocess.check_output([exe,'-dump',str(page)])
 for text in (b'Alpha',b'Beta',b'Gamma',b'Fallback content'):assert text in out,out
 m,s=pty.openpty();fcntl.ioctl(s,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0))
 p=subprocess.Popen([exe,'-no-connect','http://127.0.0.1:%d/'%server.server_port],stdin=s,stdout=s,stderr=s,env={'HOME':home,'TERM':'vt100','PATH':'/usr/bin:/bin'},start_new_session=True);os.close(s)
 def read(seconds):
  end=time.monotonic()+seconds;out=b''
  while time.monotonic()<end:
   if select.select([m],[],[],max(0,end-time.monotonic()))[0]:
    try:out+=os.read(m,65536)
    except OSError:break
  return out
 try:
  out=read(1);os.write(m,b'\x1b');out+=read(.5)
  os.write(m,b'\x1b[B');out+=read(.3);os.write(m,b'\r');out+=read(1)
  assert '/results?q=test' in requests,(requests,out[-1000:])
  assert b'RESULT-OK' in re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',out)
  # Back must reconstruct the released presentation from retained history,
  # including its form field value, then permit another submit.
  os.write(m,b'\x1b[D');back=read(1)
  assert p.poll() is None,back
  clean=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',back)
  assert b'test' in clean,(requests,clean[-1500:])
  os.write(m,b'\r');again=read(1)
  if b'RESULT-OK' not in re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',again):
   # Enter on the text field advances to its explicit submit button.
   os.write(m,b'\r');again+=read(1)
  # A cached response is valid; verify rendering rather than request count.
  assert b'RESULT-OK' in re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',again),(requests,again[-1500:])
  before_reload=len(requests)
  os.write(m,b'\x12');refreshed=read(3)
  assert len(requests)>before_reload,(requests,refreshed[-2000:])
  assert p.poll() is None and b'RESULT-OK' in re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',refreshed),refreshed[-1500:]
  print('PASS:frame fallbacks; keyboard GET form, Back with retained values, resubmit and reload')
 finally:
  p.terminate()
  try:p.wait(timeout=2)
  except subprocess.TimeoutExpired:p.kill();p.wait()
  os.close(m);server.shutdown();server.server_close()

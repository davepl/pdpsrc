#!/usr/bin/env python3
"""Check compact download shortcut is honest and dump/source output still works."""
import os,pty,select,subprocess,termios,fcntl,struct,time,tempfile,re,sys
from pathlib import Path
exe=sys.argv[1];page=Path(__file__).resolve().parent/'page.html'
assert subprocess.check_output([exe,'-source',str(page)])==page.read_bytes()
m,s=pty.openpty();fcntl.ioctl(s,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0))
with tempfile.TemporaryDirectory() as home:
 p=subprocess.Popen([exe,'-no-connect',str(page)],stdin=s,stdout=s,stderr=s,env={'HOME':home,'TERM':'vt100','PATH':'/usr/bin:/bin'},start_new_session=True);os.close(s)
 def read(seconds):
  end=time.monotonic()+seconds;out=b''
  while time.monotonic()<end:
   if select.select([m],[],[],max(0,end-time.monotonic()))[0]:
    try:out+=os.read(m,65536)
    except OSError:break
  return out
 try:
  out=read(1);os.write(m,b'\x1b');out+=read(.5);os.write(m,b'd');out+=read(.7)
  clean=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',out)
  assert b'Downloads are not included' in clean,clean[-2000:]
  assert b'-source' in clean and b'-dump' in clean
  assert p.poll() is None
  print('PASS: download shortcut reports unavailable; source output matches input')
 finally:
  p.terminate()
  try:p.wait(timeout=2)
  except subprocess.TimeoutExpired:p.kill();p.wait()
  os.close(m)

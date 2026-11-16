#!/usr/bin/env python3
"""Check actual compact menu labels in an 80x24 VT100 PTY. Argument: browser executable."""
import sys
from pathlib import Path
import os,pty,select,subprocess,termios,fcntl,struct,time,tempfile,re
m,s=pty.openpty();fcntl.ioctl(s,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0))
with tempfile.TemporaryDirectory() as home:
 p=subprocess.Popen([sys.argv[1],'-no-connect',str(Path(__file__).resolve().parent/'page.html')],stdin=s,stdout=s,stderr=s,env={'HOME':home,'TERM':'vt100','PATH':'/usr/bin:/bin'},start_new_session=True);os.close(s)
 def read(seconds):
  end=time.monotonic()+seconds;out=b''
  while time.monotonic()<end:
   if select.select([m],[],[],max(0,end-time.monotonic()))[0]:
    try:out+=os.read(m,65536)
    except OSError:break
  return out
 try:
  initial=read(1);os.write(m,b'\x1b');initial+=read(.5);os.write(m,b'\x1b');bar=read(.5);os.write(m,b'\r');menu=read(.5)
  allout=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',initial+bar+menu)
  for label in (b'File',b'View',b'Link',b'Downloads',b'Help',b'Go to URL',b'Go back',b'Reload'):
   assert label in allout,(label,allout[-1500:])
  print('PASS: real compact browser opens File/View/Link/Downloads/Help and File navigation menu in VT100 PTY')
 finally:
  p.terminate()
  try:p.wait(timeout=2)
  except subprocess.TimeoutExpired:p.kill();p.wait()
  os.close(m)

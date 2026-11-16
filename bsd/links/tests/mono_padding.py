#!/usr/bin/env python3
"""Synthetic padding stays normal; real selection spaces still reverse."""
from pathlib import Path
import os,pty,select,subprocess,termios,fcntl,struct,time,tempfile,re,sys
root=Path(__file__).resolve().parents[1]
s=(root/'upstream/html_r.c').read_text()
macro=next(x for x in s.splitlines() if x.startswith('#define DOCUMENT_PADDING'))
assert s.count('DOCUMENT_PADDING(p->data->data[y].c)')==2
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'check.c';exe=Path(tmp)/'check'
 c.write_text('#include <assert.h>\nstruct {int col;} options, *d_opt=&options;\n'+macro+'\nint main(void){int bg; for(bg=0;bg<8;bg++){options.col=0;assert(DOCUMENT_PADDING(bg)==0x720);options.col=1;assert(DOCUMENT_PADDING(bg)==((bg<<11)|32));options.col=2;assert(DOCUMENT_PADDING(bg)==((bg<<11)|32));}return 0;}\n')
 subprocess.run(['cc',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('PASS: actual padding macro normalizes all mono backgrounds, preserves all color backgrounds')
if len(sys.argv)<2:sys.exit(0)
class Screen:
 def __init__(self):self.cells=[[(' ',False) for x in range(80)] for y in range(24)];self.x=self.y=0;self.rev=False
 def feed(self,data):
  text=data.decode('latin1');i=0
  while i<len(text):
   if text[i:i+2]=='\x1b[':
    m=re.match(r'\x1b\[([0-?]*)([ -/]*)([@-~])',text[i:])
    if not m:break
    raw,_,op=m.groups();args=[int(x or 0) for x in raw.split(';')] if raw and not raw.startswith('?') else [0];i+=len(m[0])
    if op in 'Hf':self.y=(args[0] or 1)-1;self.x=((args[1] if len(args)>1 else 1) or 1)-1
    elif op=='m':
     for a in args:
      if a in (0,27):self.rev=False
      elif a==7:self.rev=True
    elif op=='J' and args[0]==2:self.cells=[[(' ',False) for x in range(80)] for y in range(24)]
    elif op=='K':
     for x in range(self.x,80):self.cells[self.y][x]=(' ',self.rev)
    continue
   c=text[i];i+=1
   if c=='\x1b':i+=1;continue
   if c=='\r':self.x=0
   elif c=='\n':self.y=min(23,self.y+1)
   elif ord(c)>=32:
    if 0<=self.y<24 and 0<=self.x<80:self.cells[self.y][self.x]=(c,self.rev)
    self.x+=1
 def row(self,label):
  for row in self.cells:
   t=''.join(c for c,r in row)
   if label in t:return row,t.index(label)
  raise AssertionError((label,[''.join(c for c,r in row) for row in self.cells]))
for bg in ('black','white'):
 with tempfile.TemporaryDirectory() as home:
  page=Path(home)/'padding.html';page.write_text('<html><body bgcolor="'+bg+'"><h2 align="center">CENTERED</h2><blockquote><p>INDENTED</p></blockquote><p><a href="next.html">ACTIVE LINK</a></p></body></html>')
  m,sl=pty.openpty();fcntl.ioctl(sl,termios.TIOCSWINSZ,struct.pack('HHHH',24,80,0,0));p=subprocess.Popen([sys.argv[1],'-no-connect',str(page)],stdin=sl,stdout=sl,stderr=sl,env={'HOME':home,'TERM':'vt100','PATH':'/usr/bin:/bin'},start_new_session=True);os.close(sl)
  screen=Screen()
  def read(secs):
   end=time.monotonic()+secs;out=b''
   while time.monotonic()<end:
    if select.select([m],[],[],max(0,end-time.monotonic()))[0]:
     try:out+=os.read(m,65536)
     except OSError:break
   screen.feed(out)
  try:
   read(1);os.write(m,b'\x1b');read(.5)
   for label in ('CENTERED','INDENTED'):
    row,x=screen.row(label);assert x>0
    assert all(c==' ' and not r for c,r in row[:x]),(bg,label,row[:x])
   row,x=screen.row('ACTIVE LINK');assert row[x+6]==(' ',True),(bg,row[x:x+11])
   os.write(m,b'\x1b');read(.3);os.write(m,b'\r');read(.4)
   assert any(r and c!=' ' for row in screen.cells for c,r in row),'menu selection lost'
  finally:
   p.terminate()
   try:p.wait(timeout=2)
   except subprocess.TimeoutExpired:p.kill();p.wait()
   os.close(m)
print('PASS: centered/indented mono margins for black/white HTML backgrounds; active-link internal space and menu reverse preserved')

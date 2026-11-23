#!/usr/bin/env python3
"""Validate native two-byte coordinates, overflow rejection and wrapped links."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='links-points-') as d:
 p=Path(d);c=p/'check.c';c.write_text(r'''
#include "links.h"
#include <assert.h>
void error(unsigned char *s,...) { fprintf(stderr,"%s\n",s); }
int main(int argc,char **argv) {
 struct point p;assert(sizeof(p)==2);set_point(&p,atol(argv[1]),atol(argv[2]));
 assert(p.x==atol(argv[1]) && p.y==atol(argv[2]));return 0;
}
''')
 subprocess.run(['cc','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-DPDP11_COMPACT_POINTS','-Wno-everything','-fsanitize=address,undefined','-I'+str(root/'upstream'),str(c),'-o',str(p/'check')],check=True)
 for x,y in ((0,0),(255,255),(185,89)):
  subprocess.run([str(p/'check'),str(x),str(y)],check=True)
 for x,y in ((-1,0),(0,-1),(256,0),(0,256),(65536,0)):
  r=subprocess.run([str(p/'check'),str(x),str(y)],capture_output=True);assert r.returncode!=0 and b'coordinate limit' in r.stderr
 if len(sys.argv)>1:
  exe=sys.argv[1];page=p/'wrapped.html'
  page.write_text('<html><body>'+''.join('<p><a href="p%d.html">Link%d %s</a></p>'%(i,i,'word '*35) for i in range(20))+'</body></html>')
  out=subprocess.check_output([exe,'-dump','-width','80',str(page)])
  for i in range(20):assert ('Link%d'%i).encode() in out
  page.write_text('<html><body><pre><a href="a.html">'+'x'*256+'</a></pre></body></html>')
  r=subprocess.run([exe,'-dump',str(page)],capture_output=True);assert r.returncode!=0 and b'coordinate limit' in r.stderr
 print('PASS:2-byte point boundaries and explicit overflow rejection'+('; wrapped links' if len(sys.argv)>1 else ''))

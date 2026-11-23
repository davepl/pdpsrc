#!/usr/bin/env python3
"""Exercise compact link-vector and line growth across repeated reallocations."""
from pathlib import Path
import tempfile,subprocess,sys
exe=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='links-render-growth-') as d:
 p=Path(d)/'many.html'
 body='<html><body><h1>Growth</h1>'+''.join('<p><a href="next%d.html">Link%03d</a> text %s</p>'%(i,i,'x'*(i%90)) for i in range(100))
 body+='<table border="1">'+''.join('<tr><td>Row%03d</td><td>value</td></tr>'%i for i in range(30))+'</table>'
 body+='<form><input name="q" value="test"><input type="submit" value="Go"></form></body></html>'
 p.write_text(body)
 for width in (20,80,132):
  out=subprocess.check_output([exe,'-dump','-width',str(width),str(p)])
  for i in range(100):assert ('Link%03d'%i).encode() in out
  for i in range(30):assert ('Row%03d'%i).encode() in out
 print('PASS:100 links,30 table rows and form at20/80/132 columns')

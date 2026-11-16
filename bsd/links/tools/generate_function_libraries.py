#!/usr/bin/env python3
"""Generate native archive members for bfu/types without editing canonical C.
Usage: generate_function_libraries.py [links-root] [unit ...]
Output native-lib/Makefile is invoked from upstream. Requires host clang.
"""
import json,pathlib,re,subprocess,sys
from compact_diagnostics import compact_diagnostics
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parent.parent).resolve()
units=sys.argv[2:] or ['bfu','types']; out=root/'native-lib'; out.mkdir(exist_ok=True)
h=(root/'upstream/language.h').read_text(encoding='latin1')
m=list(re.finditer(r'^#else\n(?=#define T__CHAR_SET\b)',h,re.M))
if not m: raise SystemExit('Missing language ID registry')
ids=dict(re.findall(r'^#define\s+(T_\w+)\s+(\d+)\s*$',h[m[-1].end():],re.M))
pat=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\bT_\w+\b',re.S)
def repl(m):
 t=m.group()
 if re.fullmatch(r'T_\w+',t):
  if t not in ids: raise SystemExit('Unknown language ID '+t)
  return ids[t]
 return t
make=['# Generated; invoke from upstream: make -f ../native-lib/Makefile\nCC=../tools/cc211\nCFLAGS=-O -DPDP11_NATIVE\nAR=ar\nRANLIB=ranlib\n', 'all: '+' '.join('../native-lib/lib'+u+'.a' for u in units)+'\n\n']
for u in units:
 if u not in ['bfu','types','session']: raise SystemExit('Only audited bfu/types/session modules supported')
 subprocess.run([sys.executable,str(root/'tools/split_unit.py'),str(root/'upstream'/ (u+'.c')),str(out)],check=True)
 master=out/(u+'-split.inc'); text=pat.sub(repl,master.read_text(encoding='latin1'))
 text,n,b=compact_diagnostics(text)
 print(u+': compacted '+str(n)+' diagnostics, '+str(b)+' raw bytes removed')
 text=text.replace('#include "links.h"','#include "../upstream/links.h"',1)
 master.write_text('#define PDP11_LITERAL_IDS 1\n#define PDP11_SOURCE_FILE \"'+u+'.c\"\n'+text,encoding='latin1')
 manifest=json.loads((out/(u+'-manifest.json')).read_text())
 objs=['../native-lib/'+pathlib.Path(x['file']).stem+'.o' for x in manifest]
 archive='../native-lib/lib'+u+'.a'
 chunks=[objs[i:i+8] for i in range(0,len(objs),8)]
 for chunk in chunks: make.append(archive+': '+' '.join(chunk)+'\n')
 make.append('\trm -f '+archive+'\n')
 for chunk in chunks: make.append('\t${AR} cr '+archive+' '+' '.join(chunk)+'\n')
 make.append('\t${RANLIB} '+archive+'\n\n')
 for x,obj in zip(manifest,objs):
  c='../native-lib/'+x['file']
  make.append(obj+': '+c+' ../native-lib/'+master.name+' links.h links-native.h ../config.211bsd.h\n\t${CC} ${CFLAGS} -DPDP11_UNIT_'+u+' -c '+c+' -o '+obj+'\n\n')
make.append('sizes:\n\tsize ../native-lib/*.o\n')
(out/'Makefile').write_text(''.join(make))
print('Generated native archives:',', '.join('lib'+u+'.a' for u in units))

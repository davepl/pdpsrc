#!/usr/bin/env python3
"""Classify PDP-11 libc archive members safe for automatic-overlay entry.
Usage: split_libc.py libc.a output-dir [browser-object ...]
Without browser objects selects every unsafe member conservatively. With roots,
resolves archive dependencies and emits only reachable unsafe members. Does not
modify libc or root objects. Native ranlib must index safe-libc.a before use.
"""
import pathlib,struct,sys,json
src=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
def word(b,p):return struct.unpack_from('<H',b,p)[0]
def plong(b,p):return (word(b,p)<<16)|word(b,p+2)
def obj(b):
 if len(b)<16:raise ValueError('short object')
 magic,t,d,bs,ns,entry,unused,flag=struct.unpack_from('<8H',b)
 if magic!=0o407:raise ValueError('expected relocatable0407, got '+oct(magic))
 so=16+(t+d)*(1 if flag else 2);st=so+ns
 sy=[]
 if ns%8 or st+4>len(b):raise ValueError('bad symbol table')
 for p in range(so,st,8):
  ix=plong(b,p);typ=b[p+4];val=word(b,p+6)
  if ix and st+ix<len(b):name=b[st+ix:].split(b'\0',1)[0].decode('ascii')
  else:name=''
  sy.append((name,typ,val))
 unsafe=[]
 for name,typ,v in sy:
  if typ!=0x22:continue
  reason=None
  if v+4>t or word(b,16+v)!=0o4567:reason='entry does not start jsr r5,csv'
  elif flag:reason='missing relocation to verify csv target'
  else:
   rel=word(b,16+t+d+v+2);idx=rel>>4
   if not rel&8 or idx>=len(sy) or sy[idx][0]!='csv':reason='jsr target is not external csv'
  if reason:unsafe.append({'symbol':name,'value':v,'reason':reason})
 return {'text':t,'data':d,'bss':bs,'defined':{n for n,k,v in sy if k&0x20 and (k&0x1f or v)},'undefined':{n for n,k,v in sy if k==0x20 and not v},'unsafe':unsafe}
b=src.read_bytes()
if b[:8]!=b'!<arch>\n':raise SystemExit('not portable ar archive')
members=[];p=8
while p<len(b):
 hdr=b[p:p+60]
 if len(hdr)!=60 or hdr[58:60]!=b'`\n':raise SystemExit('bad archive header')
 size=int(hdr[48:58]);name=hdr[:16].decode().strip().rstrip('/');data=b[p+60:p+60+size];p+=60+size+(size&1)
 if name.startswith('__.SYMDEF'):continue
 if name.startswith('#1/') or name.startswith('/'):raise SystemExit('extended archive name unsupported')
 rec=obj(data);rec.update(name=name,raw=data,header=hdr);members.append(rec)
roots=[obj(pathlib.Path(x).read_bytes()) for x in sys.argv[3:]]
if roots:
 defined=set().union(*(x['defined'] for x in roots));needed=set().union(*(x['undefined'] for x in roots));selected=set()
 while True:
  old=len(selected)
  for i,m in enumerate(members):
   if i not in selected and m['defined'] & (needed-defined):
    selected.add(i);defined |= m['defined'];needed |= m['undefined']
  if len(selected)==old:break
else:selected=set(range(len(members)))
unsafe=[m for i,m in enumerate(members) if i in selected and m['unsafe']]
safe=[m for m in members if not m['unsafe']]
for m in unsafe:(out/m['name']).write_bytes(m['raw'])
# Reuse exact standard member headers and bytes, omitting old ranlib index.
(out/'safe-libc.a').write_bytes(b'!<arch>\n'+b''.join(m['header']+m['raw']+(b'\n' if len(m['raw'])&1 else b'') for m in safe))
(out/'root-objects.txt').write_text(' '.join(m['name'] for m in unsafe)+'\n')
report={'root_objects':[dict(name=m['name'],text=m['text'],data=m['data'],bss=m['bss'],unsafe=m['unsafe']) for m in unsafe],'root_text':sum(m['text'] for m in unsafe),'root_data':sum(m['data'] for m in unsafe),'root_bss':sum(m['bss'] for m in unsafe),'safe_members':len(safe),'selected_members':len(selected),'used_root_objects':bool(roots)}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='root_objects'},indent=2))

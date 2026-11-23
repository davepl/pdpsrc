#!/usr/bin/env python3
"""Emit a declaration-pruned links.h without modifying the source header.
Uses host clang only to locate complete declarations; original C is retained.
Each native unit keeps identifier dependency closure (including macros and
inline bodies). Structs, typedefs, native-only code and CPP directives stay.
Usage: prune_header.py upstream/links.h output.h
"""
import json, pathlib, re, subprocess, sys
src=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2])
extern_inline='--extern-inline' in sys.argv[3:]
s=src.read_text(encoding='latin1')
p=subprocess.run(['clang','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-Wno-everything','-Xclang','-ast-dump=json','-fsyntax-only','-x','c',str(src)],capture_output=True,text=True,check=True)
j=json.loads(p.stdout)
def tokens(x):
 x=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', ' ',x,flags=re.S)
 return set(re.findall(r'\b[A-Za-z_]\w*\b',x))
items=[]
for n in j.get('inner',[]):
 if n['kind'] not in ('FunctionDecl','VarDecl'): continue
 loc=n.get('loc',{})
 if 'includedFrom' in loc or 'offset' not in loc: continue
 if loc.get('file',str(src)) not in (str(src),src.name): continue
 rg=n.get('range',{}); b=rg.get('begin',{}); e=rg.get('end',{})
 if 'offset' not in b or 'offset' not in e: continue
 a=b['offset']; z=e['offset']+e['tokLen']
 if not 0<=a<z<=len(s): continue
 if s[loc['offset']:loc['offset']+loc.get('tokLen',0)] != n.get('name'): continue
 # Only whole line declarations, so no comma-declarator or macro damage.
 if s[s.rfind('\n',0,a)+1:a].strip(): continue
 if s[z:z+1]==';': z+=1
 if s[z:s.find('\n',z) if '\n' in s[z:] else len(s)].strip(): continue
 text=s[a:z]
 if re.search(r'^\s*#',text,re.M): continue
 items.append((a,z,n['name'],tokens(text)))
# Drop ambiguous/overlapping source ranges (e.g. multi-declarator variables).
items.sort(); items=[x for i,x in enumerate(items) if not (i and items[i-1][1]>x[0]) and not(i+1<len(items) and items[i+1][0]<x[1])]
macros={}
for m in re.finditer(r'^\s*#\s*define\s+(\w+)([^\n]*(?:\\\n[^\n]*)*)',s,re.M):
 macros.setdefault(m[1],set()).update(tokens(m[2]))
# Retained declarations may themselves mention symbols or macro expansions.
residual=list(s)
for a,z,_,_ in items: residual[a:z]=' '*(z-a)
retained=''.join(residual)
retained=re.sub(r'^\s*#(?:[^\n]*\\\n)*[^\n]*','',retained,flags=re.M)
base=tokens(retained)
units={}
inline_names={name for a,z,name,_ in items if s[a:z].startswith('static inline ') and '{' in s[a:z]}
for c in sorted(src.parent.glob('*.c')):
 if c.name.endswith('.native.c'): continue
 need=tokens(c.read_text(errors='replace'))|base
 while True:
  old=len(need)
  for _,_,name,deps in items:
   if name in need: need |= deps
  for name,deps in macros.items():
   if name in need: need |= deps
  if len(need)==old: break
 units[c.stem]=need
if extern_inline:
 need=set(inline_names)|base
 while True:
  old=len(need)
  for _,_,name,deps in items:
   if name in need: need |= deps
  for name,deps in macros.items():
   if name in need: need |= deps
  if len(need)==old: break
 units['helpers']=need
pieces=[]; pos=0
for a,z,name,_ in items:
 keep=[u for u,t in units.items() if name in t]
 cond=' || '.join('defined(PDP11_UNIT_'+u+')' for u in keep)
 body=s[a:z]
 if extern_inline and name in inline_names:
  definition=body.replace('static inline ', '', 1)
  prototype='extern '+definition[:definition.index('{')].rstrip()+';'
  body='#if defined(PDP11_NATIVE)\n#ifdef PDP11_UNIT_helpers\n'+definition+'\n#else\n'+prototype+'\n#endif\n#else\n'+body+'\n#endif'
 pieces.extend([s[pos:a], '#if !defined(PDP11_NATIVE)'+(' || '+cond if cond else '')+'\n',body,'\n#endif\n'])
 pos=z
pieces.append(s[pos:]); out.write_text(''.join(pieces),encoding='latin1')
print('Guarded',len(items),'declarations; per-unit kept:')
for u,t in units.items(): print(u,sum(name in t for _,_,name,_ in items))

#!/usr/bin/env python3
"""Split external-linkage C functions into individually measurable objects.
Usage: split_unit.py upstream/bfu.c output-directory
Original source is never edited. Generate a data-owner and one unit/function.
AST-resolved file-scope statics are lifted into unique short external names.
For safety rejects unsupported macro/conditional declarations,
multiple declarators, and anonymous struct/union variable declarations.
Link all generated objects once, then pack measured text into overlay banks.
"""
import json,pathlib,re,subprocess,sys
src=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2]); s=src.read_text(encoding='latin1')
p=subprocess.run(['clang','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-Wno-everything','-Xclang','-ast-dump=json','-fsyntax-only',str(src)],capture_output=True,text=True,check=True)
j=json.loads(p.stdout)
# Lift file-scope internal symbols into collision-resistant private externs.
# Rewrite AST-resolved references, never textual names or struct fields.
import hashlib,tempfile
local=[]
for n in j.get('inner',[]):
 loc=n.get('loc',{})
 if n.get('storageClass')=='static' and 'offset' in loc and 'includedFrom' not in loc and n['kind'] in ('FunctionDecl','VarDecl'):
  local.append(n)
if local:
 names={n['name']:'p'+hashlib.sha256((src.stem+':'+n['name']).encode()).hexdigest()[:6] for n in local}
 if len(set(names.values()))!=len(names): raise SystemExit('Private symbol hash collision')
 ids={n['id']:names[n['name']] for n in local}; changes={}
 for n in local:
  loc=n['loc']; changes[(loc['offset'],loc['offset']+loc['tokLen'])]=names[n['name']]
  begin=n['range']['begin'].get('offset')
  if begin is None: raise SystemExit('Cannot lift macro-generated static')
  match=re.match(r'static\s+(?:inline\s+)?',s[begin:])
  if not match: raise SystemExit('Unsupported static declaration')
  changes[(begin,begin+len(match.group()))]=''
 def visit(n):
  ref=n.get('referencedDecl',{})
  if n.get('kind')=='DeclRefExpr' and ref.get('id') in ids:
   loc=n['range']['begin']
   if 'spellingLoc' in loc:
    if not loc.get('expansionLoc',{}).get('isMacroArgExpansion'): raise SystemExit('Static name embedded in macro; manual lift needed')
    loc=loc['spellingLoc']
   if 'offset' not in loc or 'includedFrom' in loc: raise SystemExit('Static reference outside source')
   changes[(loc['offset'],loc['offset']+loc['tokLen'])]=ids[ref['id']]
  for c in n.get('inner',[]): visit(c)
 visit(j)
 for (a,z),v in sorted(changes.items(),reverse=True): s=s[:a]+v+s[z:]
 out.mkdir(parents=True,exist_ok=True)
 analysis=out/(src.stem+'-lifted.c')
 analysis.write_text(s,encoding='latin1')
 p=subprocess.run(['clang','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-I'+str(src.parent),'-Wno-everything','-Xclang','-ast-dump=json','-fsyntax-only',str(analysis.resolve())],capture_output=True,text=True,check=True)
 j=json.loads(p.stdout)
else: analysis=src
edits=[]; funcs=[]; spans=[]
for n in j.get('inner',[]):
 loc=n.get('loc',{})
 if 'includedFrom' in loc or 'offset' not in loc or n['kind'] not in ('FunctionDecl','VarDecl'): continue
 if loc.get('file',str(analysis.resolve())) not in (str(analysis.resolve()),analysis.name): continue
 if n.get('storageClass')=='static': raise SystemExit('Refusing file-scope static symbol '+n['name']+'; requires explicit shared-symbol lifting')
 rg=n['range']; b=rg['begin']; e=rg['end']
 if 'offset' not in b or 'offset' not in e: raise SystemExit('Macro declaration requires manual handling')
 a=b['offset']; z=e['offset']+e['tokLen']
 if s[loc['offset']:loc['offset']+loc['tokLen']]!=n.get('name'): raise SystemExit('Source offset mismatch')
 body=next((x for x in n.get('inner',[]) if x['kind']=='CompoundStmt'),None)
 if n['kind']=='FunctionDecl':
  if not body: continue
  k=len(funcs)+1; funcs.append(n['name'])
  start=body['range']['begin']['offset']
  if re.search(r'^\s*#',s[a:start],re.M): raise SystemExit('Conditional directive crosses signature: '+n['name']+'; needs manual normalization')
  proto=s[a:start].rstrip()+';'
  text='#if PDP11_SPLIT_PART == '+str(k)+'\n'+s[a:z]+'\n#else\n'+proto+'\n#endif'
 else:
  if n.get('storageClass')=='extern': continue
  if s[z:z+1]!=';': raise SystemExit('Multiple declarators/unsupported variable '+n['name'])
  z+=1; decl=s[a:z]
  # Find initializer assignment outside array dimensions and parentheses.
  depth=0; cut=None
  for i,ch in enumerate(decl):
   if ch in '([': depth+=1
   elif ch in ')]': depth-=1
   elif ch=='=' and depth==0: cut=i; break
  proto=decl[:cut].rstrip()+';' if cut is not None else decl
  if '{' in proto: raise SystemExit('Anonymous record declaration '+n['name'])
  text='#if PDP11_SPLIT_PART == 0\n'+decl+'\n#else\nextern '+proto+'\n#endif'
 edits.append((a,z,text))
edits.sort()
if any(x[1]>y[0] for x,y in zip(edits,edits[1:])): raise SystemExit('Overlapping declarations')
parts=[]; pos=0
for a,z,t in edits: parts.extend([s[pos:a],t]);pos=z
parts.append(s[pos:])
out.mkdir(parents=True,exist_ok=True)
master=src.stem+'-split.inc'; (out/master).write_text(''.join(parts),encoding='latin1')
manifest=[]
for k,name in enumerate(['DATA']+funcs):
 filename=src.stem+'_'+str(k).zfill(3)+'.c'
 (out/filename).write_text('#define PDP11_UNIT_'+src.stem+'\n#define PDP11_SPLIT_PART '+str(k)+'\n#include "'+master+'"\n')
 manifest.append({'file':filename,'function':name})
(out/(src.stem+'-manifest.json')).write_text(json.dumps(manifest,indent=2)+'\n')
print('Generated',len(manifest),'units; measure native object text before overlay grouping.')

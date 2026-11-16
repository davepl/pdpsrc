#!/usr/bin/env python3
"""Regenerate compact native language files deterministically.
Usage: generate_language.py [upstream-directory]
The complete non-native define tail remains the authoritative ID registry.
"""
import pathlib,re,sys
p=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parent.parent/'upstream')
h=(p/'language.h').read_text(encoding='latin1')
# Full registry starts at the LAST #else followed by T__CHAR_SET.
m=list(re.finditer(r'^#else\n(?=#define T__CHAR_SET\b)',h,re.M))
if not m: raise SystemExit('missing complete non-native registry')
tail=h[m[-1].end():]
rows=re.findall(r'^#define\s+(T_\w+)\s+(\d+)\s*$',tail,re.M)
ids=dict(rows)
if len(ids)<300: raise SystemExit('incomplete translation registry')
head=h[:h.index('#ifdef PDP11_NATIVE')]
parts=[head,'#ifdef PDP11_NATIVE\n#ifndef PDP11_LITERAL_IDS\n']
for c in sorted(p.glob('*.c')):
 if c.name.endswith('.native.c'): continue
 # Deliberately only direct source references: language-native.inc is numeric.
 text=c.read_text(encoding='latin1')
 text=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',' ',text,flags=re.S)
 used=set(re.findall(r'\bT_\w+\b',text))
 parts.append('#ifdef PDP11_UNIT_'+c.stem+'\n')
 parts.extend('#define '+name+' '+num+'\n' for name,num in rows if name in used)
 parts.append('#endif\n')
parts.extend(['#endif /* !PDP11_LITERAL_IDS */\n#else\n',tail])
new=''.join(parts)
inc=(p/'language-small.inc').read_text(encoding='latin1')
# Replace identifiers only outside comments/string literals, never prose.
def replace(m):
 t=m.group(0)
 return ids.get(t,t) if re.fullmatch(r'T_\w+',t) else t
inc=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\bT_\w+\b',replace,inc,flags=re.S)
(p/'language.h').write_text(new,encoding='latin1')
(p/'language-native.inc').write_text(inc,encoding='latin1')
print('Generated',len(rows),'IDs;',sum(not c.name.endswith('.native.c') for c in p.glob('*.c')),'per-unit macro sections')

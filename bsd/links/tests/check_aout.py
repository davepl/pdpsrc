#!/usr/bin/env python3
"""Validate a native 2.11BSD auto-overlay executable's on-disk layout.
Usage: check_aout.py executable [--data-symbols]
Structural checks only: a PASS does not establish runtime or ABI correctness.
PDP-11 longs use high 16-bit word first, each word little-endian.
"""
import argparse,pathlib,struct,sys

def validate(path,show_data=False):
 b=path.read_bytes(); errors=[]; warnings=[]
 def check(ok,msg):
  if not ok: errors.append(msg)
 def u16(p):return struct.unpack_from('<H',b,p)[0]
 def u32(p):return (u16(p)<<16)|u16(p+2)
 def round8(x):return (x+8191)&~8191
 if len(b)<48:raise ValueError('truncated executable/overlay header')
 magic,text,data,bss,syms,entry,unused,flag=struct.unpack_from('<8H',b)
 if magic not in (0o430,0o431):raise ValueError('expected auto-overlay0430/0431, got '+oct(magic))
 maxov,*ov=struct.unpack_from('<16H',b,16)
 check(flag in (0,1),'invalid relocation-stripped flag')
 check(text%64==0,'root text is not64-byte aligned')
 check(all(x%64==0 for x in ov),'overlay text size is not64-byte aligned')
 check(maxov==max(ov),'max-overlay header differs from largest overlay')
 check(round8(text)+round8(maxov)<=65536,'root+maximum overlay exceed64KiB instruction space')
 if magic==0o430:check(round8(text)+round8(maxov)+data+bss<=65536,'non-split image exceeds64KiB address space')
 else:check(data+bss<=65536,'data+BSS exceed64KiB data space (before stack/heap)')
 check(entry<text,'entry lies outside root text')
 check(syms%8==0,'symbol table size is not a multiple of8')
 totaltext=text+sum(ov);dataoff=48+totaltext
 symoff=dataoff+data+(totaltext+data if flag==0 else 0)
 stroff=symoff+syms
 check(dataoff+data<=len(b),'text/data extend past EOF')
 check(stroff<=len(b),'symbol table extends past EOF')
 strings_ok=False; strlen=0
 if syms:
  check(stroff+4<=len(b),'missing string-table size')
  if stroff+4<=len(b):
   strlen=u32(stroff);strings_ok=strlen>=4 and stroff+strlen==len(b)
   check(strings_ok,'string-table boundary mismatch: declared end '+str(stroff+strlen)+', file size '+str(len(b)))
 else:check(stroff==len(b) or (stroff+4==len(b) and u32(stroff)==4),'unexpected bytes after stripped executable')
 symbols=[]
 if strings_ok:
  for p in range(symoff,stroff,8):
   ix=u32(p);typ=b[p+4];overlay=b[p+5];value=u16(p+6)
   if not(4<=ix<strlen):
    errors.append('invalid string index '+str(ix)+' at symbol byte '+str(p));continue
   z=b.find(b'\0',stroff+ix,stroff+strlen)
   if z<0:errors.append('unterminated symbol name at '+str(p));continue
   raw=b[stroff+ix:z]
   if not raw or any(c<33 or c>126 for c in raw):
    errors.append('invalid symbol name at '+str(p));continue
   name=raw.decode('ascii');symbols.append((name,typ,overlay,value))
   check(overlay<=15,'invalid overlay number for '+name)
   base=typ&0x1f
   check(base in (0,1,2,3,4,0x14,0x1f),'unknown symbol type for '+name)
   if base==2:
    if typ&0x20:check(value<text,'global text thunk outside root: '+name)
    elif overlay:check(round8(text)<=value<round8(text)+ov[overlay-1],'local overlay text outside overlay: '+name)
   dataorigin=0 if magic==0o431 else round8(text)+round8(maxov)
   if base==3:check(dataorigin<=value<=dataorigin+data,'data symbol outside data: '+name)
   if base==4:check(dataorigin+data<=value<=dataorigin+data+bss,'BSS symbol outside BSS: '+name)
   if overlay and base!=2:warnings.append('non-text symbol has stale overlay metadata: '+name+' (not itself proof of extra emitted thunks)')
 if errors and syms:
  for delta in range(-256,257,2):
   at=stroff+delta
   if delta and 0<=at<=len(b)-4 and u32(at)>=4 and at+u32(at)==len(b):
    warnings.append('possible displaced string/symbol tables: '+str(delta)+' bytes from declared layout; reject executable')
 print(path.name+': '+('FAIL' if errors else 'PASS')+' (structural validation only)')
 print('text='+str(totaltext)+' root='+str(text)+' max_overlay='+str(maxov)+' data='+str(data)+' bss='+str(bss)+' symbols='+str(syms//8))
 print('data_offset='+str(dataoff)+' symbol_offset='+str(symoff)+' string_offset='+str(stroff))
 for m in errors:print('ERROR:',m)
 for m in warnings:print('WARNING:',m)
 if show_data and symbols:
  ds=sorted((v,n,t&31) for n,t,o,v in symbols if t&31 in (3,4))
  print('Largest symbol-to-next-symbol intervals (estimates, not declared object sizes):')
  spans=[]
  for i,(v,n,t) in enumerate(ds):
   end=next((x[0] for x in ds[i+1:] if x[0]>v),data+bss)
   if t==3:end=min(end,data)
   spans.append((end-v,n,v))
  for size,name,v in sorted(spans,reverse=True)[:20]:print(size,name,oct(v))
 return not errors

if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('executable',type=pathlib.Path);ap.add_argument('--data-symbols',action='store_true');a=ap.parse_args()
 try:ok=validate(a.executable,a.data_symbols)
 except (ValueError,OSError,struct.error) as e:print('FAIL:',e);ok=False
 sys.exit(0 if ok else 1)

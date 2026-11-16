#!/usr/bin/env python3
"""Actual loader tests: stable IDs/cache, path selection and corrupt files."""
from pathlib import Path
import tempfile,subprocess,os,struct,shutil
root=Path(__file__).resolve().parents[1];blob=(root/'upstream/language.dat').read_bytes();n,size=struct.unpack('>HH',blob[8:12]);offs=struct.unpack('>'+'H'*(n+1),blob[12:12+2*(n+1)])
with tempfile.TemporaryDirectory(prefix='links-language-') as d:
 p=Path(d);src=p/'test.c';src.write_text(r'''
#include "links.h"
void error(unsigned char *s,...) { fprintf(stderr,"%s\n",s); }
void int_error(unsigned char *s,...) { abort(); }
int errline;unsigned char *errfile;
unsigned char *path_to_exe;
#include "language-disk.inc"
int main(int argc,char **argv) {
 int i;unsigned char *a,*b;path_to_exe=argv[0];init_trans();
 for(i=0;i<LANGUAGE_DATA_COUNT;i++) {a=get_english_translation(dummyarray+i);b=get_english_translation(dummyarray+i);if(a!=b)abort();fwrite(a,1,strlen(a)+1,stdout);}
 shutdown_trans();return 0;
}
''')
 subprocess.run(['cc','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-Wno-everything','-fsanitize=address,undefined','-I'+str(root/'upstream'),str(src),'-o',str(p/'check')],check=True)
 env=dict(os.environ,UBSAN_OPTIONS="halt_on_error=1");env.pop('LINKS_DATA_DIR',None)
 data=p/'language.dat';data.write_bytes(blob)
 expected=b''.join(blob[offs[i]:offs[i+1]] for i in range(n))
 assert subprocess.check_output([str(p/'check')],env=env)==expected
 envpath=dict(env,PATH=str(p)+':'+env.get('PATH',''));assert subprocess.check_output(['check'],env=envpath)==expected
 assert subprocess.check_output(['check'],cwd=p,env=dict(env,PATH='/nonexistent:'))==expected
 other=p/'data';other.mkdir();(other/'language.dat').write_bytes(blob);data.unlink()
 assert subprocess.check_output([str(p/'check')],env=dict(env,LINKS_DATA_DIR=str(other)))==expected
 r=subprocess.run([str(p/'check')],env=env,capture_output=True);assert r.returncode and b'language.dat' in r.stderr
 cases=[]
 for offset,value in ((0,b'BADMAGIC'),(8,b'\0\1'),(10,b'\0\0'),(12,b'\0\0')):
  b=bytearray(blob);b[offset:offset+len(value)]=value;cases.append(bytes(b))
 cases+=[blob[:-1]]
 b=bytearray(blob);b[offs[1]-1]=ord('X');cases.append(bytes(b))
 for b in cases:
  data.write_bytes(b);r=subprocess.run([str(p/'check')],env=env,capture_output=True);assert r.returncode and b'language.dat' in r.stderr
 print('PASS:371 IDs, cached pointers, sibling/PATH/env lookup, missing file and6 corrupt-file cases under sanitizers')

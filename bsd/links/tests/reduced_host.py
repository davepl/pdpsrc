#!/usr/bin/env python3
"""Isolated modern-host reduced-profile integration (not native execution).
Run make host first for config.h. Builds into a temporary directory and retains
the resulting executable for optional VT100 PTY/manual checks.
"""
from pathlib import Path
import subprocess, tempfile, json, concurrent.futures, builtins, shutil
root=Path(__file__).resolve().parents[1];src=root/'upstream';out=Path(tempfile.mkdtemp(prefix='links-reduced-'));print(out,flush=True)
units='af_unix beos cache charsets connect cookies default dns error file html html_r html_tbl http https kbd kbdbind language main menu_compact os_dep sched select session terminal url view win32'.split()
flags=['cc','-std=gnu89','-Os','-DHAVE_CONFIG_H','-DPDP11_SMALL','-DPDP11_MINIMAL','-DPDP11_DIRTY_BITMAP','-DPDP11_STATIC_KEYMAP','-DPDP11_DIRECT_CHARSET','-DPDP11_COMPACT_POINTS','-DPDP11_RENDER_HELPER','-DPDP11_STREAM_REDRAW','-DPDP11_LANGUAGE_SIDECAR','-Wno-everything','-I'+str(src)]
def compile(c,o):
 r=subprocess.run(flags+['-c',str(c),'-o',str(o)],capture_output=True)
 if r.returncode:raise RuntimeError(str(c)+'\n'+r.stderr.decode())
 return o
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:objs=list(ex.map(lambda u:compile(src/(u+'.c'),out/(u+'.o')),units))
archives=[]
for u in ('bfu','types'):
 d=out/u;subprocess.run(['python3',str(root/'tools/split_unit.py'),str(src/(u+'.c')),str(d)],check=True)
 manifest=json.loads((d/(u+'-manifest.json')).read_text())
 with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:members=list(ex.map(lambda m:compile(d/m['file'],d/Path(m['file']).with_suffix('.o')),manifest))
 a=out/('lib'+u+'.a');subprocess.run(['ar','rcs',str(a)]+list(map(str,members)),check=True);archives.append(a)
shutil.copyfile(src/'language.dat',out/'language.dat')
exe=out/'links';r=subprocess.run(['cc','-o',str(exe)]+list(map(str,objs+archives))+['-ldl'],capture_output=True);print(r.stderr.decode());r.check_returncode()
t=(root/'tests/smoke.py').read_text().replace("exe=root/'upstream/links'",'exe=pathlib.Path('+repr(str(exe))+')');exec(builtins.compile(t,str(root/'tests/smoke.py'),'exec'),{'__file__':str(root/'tests/smoke.py')})

subprocess.run(["python3",str(root/"tests/compact_menu.py"),str(exe)],check=True)

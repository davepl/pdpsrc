#!/usr/bin/env python3
"""Exercise actual terminal.c bitmap allocation, setters and redraw on host.
The isolated harness supplies only terminal I/O and string-builder stubs.
"""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'upstream/terminal.c').read_text(encoding='latin1')
def function(name):
    import re
    m = re.search(r'(?m)^(?:static )?(?:inline )?(?:void|int|unsigned) '+name+r'\([^\n]*\)\n\{', s)
    assert m, name
    pos = m.end(); depth = 1
    while depth:
        depth += (s[pos] == '{') - (s[pos] == '}'); pos += 1
    return s[m.start():pos]+'\n'
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PDP11_DIRTY_BITMAP 1
#define MAXINT 32767
#define overalloc() abort()
#define TERM_LINUX 1
#define TERM_VT100 2
#define TERM_KOI8 3
#define TERM_FREEBSD 4
#define TERM_DUMB 5
typedef unsigned chr;
struct term_spec { int mode,m11_hack,restrict_852,col,block_cursor; };
struct terminal { unsigned *screen,*last_screen; int x,y,dirty,master,cx,cy,lcx,lcy,fdout; struct term_spec *spec; };
static int blocked, write_result=-2, writes, output_len;
static int wants, dones, fail_at=-1, max_chunk;
static char output[65536];
static void *mem_realloc(void *p,size_t n) { void *q=realloc(p,n?n:1); assert(q); return q; }
static void mem_free(void *p) { free(p); }
static unsigned char *init_str(void) { return calloc(1,1); }
static void add_to_str(unsigned char **s,int *l,const char *t) { size_t n=strlen(t); *s=realloc(*s,*l+n+1); memcpy(*s+*l,t,n+1); *l+=n; }
static void add_chr_to_str(unsigned char **s,int *l,int c) { char b[2]; b[0]=c;b[1]=0;add_to_str(s,l,b); }
static void add_num_to_str(unsigned char **s,int *l,int n) { char b[30];sprintf(b,"%d",n);add_to_str(s,l,b); }
static int is_blocked(void) { return blocked; }
static void want_draw(void) { wants++; }
static void done_draw(void) { dones++; }
static int hard_write(int fd,unsigned char *s,int l) { (void)fd; writes++; if(l>max_chunk)max_chunk=l; if(writes==fail_at)return -1; assert(l<sizeof(output));memcpy(output,s,l);output[l]=0;output_len=l;return write_result==-2?l:write_result; }
'''
helper = s[s.index('/* The native profile stores'):s.index('void alloc_term_screen')]
tables = s[s.index('unsigned char frame_dumb'):s.index('void redraw_all_terminals')]
code = prefix + helper + function('alloc_term_screen') + function('getcompcode') + tables
for n in ('redraw_screen','set_char','get_char','set_color','set_only_char','set_line','set_line_color','fill_area','set_cursor'):
    code += function(n)
code += r'''
static int dirty(struct terminal *t,int p) { return !!SCREEN_IS_DIRTY(t,p); }
static void clean(struct terminal *t) { memset(t->last_screen,0,SCREEN_DIRTY_BYTES(t->x,t->y)); t->dirty=0; }
int main(void) {
 struct terminal t; struct term_spec sp; unsigned line[3]={ 'a','b','c' }; int i;
 memset(&t,0,sizeof(t));memset(&sp,0,sizeof(sp));sp.mode=TERM_VT100;t.spec=&sp;
 alloc_term_screen(&t,80,24); assert(SCREEN_DIRTY_BYTES(80,24)==240);
 for(i=0;i<1920;i++) assert(dirty(&t,i));
 redraw_screen(&t);assert(!t.dirty);for(i=0;i<1920;i++) assert(!dirty(&t,i));
 set_char(&t,1,0,'A');assert(dirty(&t,1)&&!dirty(&t,0));redraw_screen(&t);assert(strchr(output,'A'));
 set_char(&t,1,0,'A');assert(!dirty(&t,1));output_len=0;redraw_screen(&t);assert(output_len==0);
 set_color(&t,1,0,0x700);assert(dirty(&t,1)&&get_char(&t,1,0)==0x741);clean(&t);
 set_only_char(&t,1,0,'B');assert(dirty(&t,1)&&get_char(&t,1,0)==0x742);clean(&t);
 set_line(&t,-1,1,3,line);assert(dirty(&t,80)&&dirty(&t,81)&&!dirty(&t,82));assert(get_char(&t,0,1)=='b');clean(&t);
 set_line_color(&t,0,1,2,0x700);assert(dirty(&t,80)&&dirty(&t,81));clean(&t);
 fill_area(&t,78,22,8,8,'X');assert(dirty(&t,1838)&&dirty(&t,1839)&&dirty(&t,1918)&&dirty(&t,1919));clean(&t);
 set_char(&t,-1,0,'x');set_char(&t,80,0,'x');assert(!dirty(&t,0));clean(&t);
 set_char(&t,10,2,'Q');write_result=-1;redraw_screen(&t);assert(t.dirty&&dirty(&t,170)&&t.lcx==-1);write_result=-2;redraw_screen(&t);assert(!t.dirty&&!dirty(&t,170));
 set_char(&t,11,2,'R');write_result=1;redraw_screen(&t);assert(t.dirty&&dirty(&t,171));write_result=-2;redraw_screen(&t);assert(!t.dirty);
 t.master=1;blocked=1;set_char(&t,12,2,'S');i=writes;redraw_screen(&t);assert(writes==i&&dirty(&t,172));blocked=0;redraw_screen(&t);assert(!dirty(&t,172));
 set_cursor(&t,5,5,5,5);redraw_screen(&t);assert(strstr(output,"\033[6;6H"));
 alloc_term_screen(&t,9,2);assert(SCREEN_DIRTY_BYTES(9,2)==3);for(i=0;i<18;i++) assert(dirty(&t,i));redraw_screen(&t);

#ifdef STREAM_REDRAW
 alloc_term_screen(&t,80,24);sp.col=1;sp.mode=TERM_VT100;
 for(i=0;i<1919;i++)set_char(&t,i%80,i/80,((i%127)<<8)|((i%2)?0x80b3:'X'));
 max_chunk=0;fail_at=writes+2;wants=dones=0;redraw_screen(&t);
 assert(t.dirty && dirty(&t,0) && dirty(&t,1918) && t.lcx==-1);
 assert(wants==1 && dones==1 && max_chunk<=256);
 fail_at=-1;wants=dones=0;redraw_screen(&t);
 assert(!t.dirty && !dirty(&t,0) && wants==1 && dones==1 && max_chunk<=256);
 assert(strstr(output,"\033[0m") && strchr(output,15));
#endif
 free(t.screen);free(t.last_screen);puts("PASS: bitmap setters, clipping, resize, redraw, cursor, blocked/failed/short writes");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='links-bitmap-') as d:
    c=Path(d)/'test.c'; exe=Path(d)/'test'; c.write_text(code)
    for flags in ([], ['-DPDP11_RENDER_HELPER'], ['-DPDP11_RENDER_HELPER','-DPDP11_STREAM_REDRAW']):
        subprocess.run(['cc','-std=gnu89','-fsanitize=address,undefined','-g']+flags+[str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

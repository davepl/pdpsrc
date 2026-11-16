#!/usr/bin/env python3
"""Sanitizer tests for actual allocation-free native charset path."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'upstream/charsets.c').read_text().replace('#include "links.h"','',1)
prefix=r'''
#include "links.h"
#include <assert.h>
static int allocations;
static void *tracked_alloc(size_t n) { allocations++;return malloc(n); }
static void *tracked_realloc(void *p,size_t n) { return realloc(p,n); }
#define mem_alloc tracked_alloc
#define mem_realloc tracked_realloc
void error(unsigned char *s,...) { abort(); }
void int_error(unsigned char *s,...) { abort(); }
int errline;unsigned char *errfile;
struct document_options opts;
struct document_options *d_opt=&opts;
'''
suffix=r'''
static void check(int from,int to,unsigned char *input,unsigned char *expected) {
 unsigned char *out;
 opts.cp=to;opts.plain=0;
 out=convert_string(get_translation_table(from,to),input,strlen(input));
 if(strcmp(out,expected)) { fprintf(stderr,"got [%s] expected [%s]\n",out,expected);abort(); }
 free(out);
}
int main(void) {
 struct conv_table *ct;int i,j,n;unsigned char *out;unsigned char b[2];
 assert(get_cp_index("utf-8")==2);assert(get_cp_index("iso-8859-1")==1);
 for(i=0;i<3;i++)for(j=0;j<3;j++)assert(get_translation_table(i,j));
 assert(allocations==0);assert(!get_translation_table(-1,0));
 check(2,0,"Caf\303\251 \342\200\224 \342\200\231 \342\202\254","Cafe - ' EUR");
 check(1,0,"Caf\351","Cafe");check(1,2,"Caf\351","Caf\303\251");
 check(2,1,"Caf\303\251","Caf\351");
 check(2,2,"\360\237\220\270","\360\237\220\270");
 check(2,0,"\360\237\220\270","*");
 check(2,0,"A\302\240B\302\255C","A\001BC");
 check(1,0,"A\240B\255C","A\001BC");
 check(2,0,"A&nbsp;B&shy;C","A\001BC");
 check(2,2,"A\302\240B\302\255C","A\302\240B\302\255C");
 check(2,0,"&#26085;&#26412;&#35486;","***");
 check(2,0,"A &amp; B &lt; C &#233; &eacute;","A & B < C e e");
 check(2,0,"\300\257","**");check(2,0,"\342","*");check(2,0,"\342x","*x");
 ct=get_translation_table(1,2);
 for(i=160;i<256;i++) {
  b[0]=i;b[1]=0;out=convert_char(ct,b,1,&n);assert(n==1);assert((out[0]&0xe0)==0xc0);assert((out[1]&0xc0)==0x80);
 }
 i=allocations;
 for(j=0;j<100;j++) { ct=get_translation_table(2,0);out=convert_char(ct,(unsigned char *)"\303\251",2,&n);assert(n==2 && !strcmp(out,"e")); }
 free_conv_table();assert(allocations==i);
 puts("PASS: no conversion-table allocation; ASCII/Latin1/UTF8, entities, malformed input and4-byte codepoints");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='links-charset-') as d:
 p=Path(d);(p/'test.c').write_text(prefix+s+suffix)
 subprocess.run(['cc','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-DPDP11_DIRECT_CHARSET','-Wno-everything','-fsanitize=address,undefined','-I'+str(root/'upstream'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

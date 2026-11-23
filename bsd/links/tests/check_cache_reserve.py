#!/usr/bin/env python3
"""ASan/UBSan host tests of source-extracted native cache reservation logic.
Tests cache algorithms, not the native ABI or full HTTP parser integration.
"""
import pathlib,re,tempfile,subprocess
root=pathlib.Path(__file__).resolve().parent.parent
s=(root/'upstream/cache.c').read_text(encoding='latin1')
def extract(name):
 m=re.search(r'^(?:static )?(?:int|void) '+name+r'\([^\n]*\)\n\{.*?^\}',s,re.M|re.S)
 if not m:raise SystemExit('missing '+name)
 return m.group()
h=r'''
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
struct list_head { void *next,*prev; };
struct fragment { struct fragment *next,*prev; int offset,length,real_length; unsigned char data[1]; };
struct cache_entry { struct list_head frag; int length,data_size,count,incomplete; };
#define MAXINT 32767
#define list_empty(x) ((x).next==&(x))
#define init_list(x) ((x).next=(x).prev=&(x))
#define foreach(x,l) for ((x)=(l).next;(void *)(x)!=&(l);(x)=(x)->next)
#define add_at_pos(p,x) do { struct list_head *p_=(void *)(p); (x)->next=p_->next; (x)->prev=(void *)p_; ((struct list_head *)p_->next)->prev=(x); p_->next=(x); } while(0)
#define add_to_list(l,x) add_at_pos(&(l),x)
#define del_from_list(x) do { ((struct list_head *)(x)->next)->prev=(x)->prev; ((struct list_head *)(x)->prev)->next=(x)->next; } while(0)
#define overalloc() abort()
#define internal(x) abort()
#define sf(x) e->data_size+=(x),cache_size+=(x)
static long cache_size;
static int cache_count,page_size=256,allocations,fail_malloc;
static void *mem_alloc(size_t n) { void *p=malloc(n);assert(p);++allocations;return p; }
static void *mem_realloc(void *p,size_t n) { p=realloc(p,n);assert(p);++allocations;return p; }
#define mem_free free
static void *optional_malloc(size_t n) { if(fail_malloc)return NULL;return malloc(n); }
#define malloc optional_malloc
void truncate_entry(struct cache_entry *,int,int);
'''
h+=extract('fragment_align')+'\n#define C_ALIGN(x) fragment_align((int)(x))\n'
h+='\n'.join(extract(n) for n in ['reserve_cache_fragment','add_fragment','defrag_entry','truncate_entry'])
h+=r'''
static void reset(struct cache_entry *e) { memset(e,0,sizeof(*e));init_list(e->frag);cache_size=0; }
static int count(struct cache_entry *e) { struct fragment *f;int n=0;foreach(f,e->frag)++n;return n; }
static void verify(struct cache_entry *e,const char *data,int n) { struct fragment *f=e->frag.next;assert(count(e)==1);assert(f->offset==0&&f->length==n);assert(e->length==n&&e->data_size==n&&cache_size==n);assert(!memcmp(f->data,data,n)); }
int main(void) {
 struct cache_entry e;char data[8000],change[50];int i,n,before;
 for(i=0;i<8000;++i)data[i]=(char)(i%127);
 reset(&e);
 assert(!reserve_cache_fragment(&e,0));assert(!reserve_cache_fragment(&e,-1));assert(!reserve_cache_fragment(&e,16385));assert(!reserve_cache_fragment(&e,32767));
 assert(reserve_cache_fragment(&e,8000));assert(e.length==0&&e.data_size==0&&cache_size==0);assert(!reserve_cache_fragment(&e,8000));
 before=allocations;
 for(i=0;i<8000;i+=370){n=8000-i;if(n>370)n=370;add_fragment(&e,i,(unsigned char *)data+i,n);}
 verify(&e,data,8000);defrag_entry(&e);assert(allocations==before);
 add_fragment(&e,1000,(unsigned char *)data+1000,50);verify(&e,data,8000);
 memset(change,'Z',50);add_fragment(&e,1000,(unsigned char *)change,50);memcpy(data+1000,change,50);verify(&e,data,1050);
 add_fragment(&e,1050,(unsigned char *)data+1050,6950);verify(&e,data,8000);assert(allocations==before);
 truncate_entry(&e,1000,1);verify(&e,data,1000);truncate_entry(&e,0,1);assert(list_empty(e.frag)&&cache_size==0);
 reset(&e);fail_malloc=1;assert(!reserve_cache_fragment(&e,8000));assert(list_empty(e.frag));fail_malloc=0;
 for(i=0;i<1200;i+=300)add_fragment(&e,i,(unsigned char *)data+i,300);
 assert(count(&e)>1);defrag_entry(&e);verify(&e,data,1200);truncate_entry(&e,0,1);
 reset(&e);assert(reserve_cache_fragment(&e,400));add_fragment(&e,0,(unsigned char *)data,300);add_fragment(&e,300,(unsigned char *)data+300,300);defrag_entry(&e);verify(&e,data,600);truncate_entry(&e,0,1);
 reset(&e);assert(reserve_cache_fragment(&e,8000));add_fragment(&e,0,(unsigned char *)data,100);add_fragment(&e,200,(unsigned char *)data+200,100);assert(count(&e)==2);defrag_entry(&e);assert(count(&e)==2);truncate_entry(&e,0,1);
 puts("PASS: bounded reserve, sequential no-copy fill, overlaps, truncation, allocation-failure fallback, fragmented/gapped fallback");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='links-reserve-') as td:
 p=pathlib.Path(td);(p/'check.c').write_text(h)
 subprocess.run(['cc','-std=c89','-O0','-g','-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)

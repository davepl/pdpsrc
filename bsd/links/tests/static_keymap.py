#!/usr/bin/env python3
"""Exercise actual kbdbind.c defaults, override/tombstone and config semantics."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'upstream/kbdbind.c').read_text().replace('#include "links.h"','',1)
prefix=r'''
#include "links.h"
#include <assert.h>
static int live,allocations;
static void *tracked_alloc(size_t n) { void *p=malloc(n);assert(p);live++;allocations++;return p; }
static void tracked_free(void *p) { assert(p);live--;free(p); }
#define mem_alloc tracked_alloc
#define mem_free tracked_free
void do_not_optimize_here(void *p) { (void)p; }
unsigned char *get_token(unsigned char **line) {
 unsigned char *p=*line,*s;int n;
 while (*p==' ')p++;
 if (!*p)return NULL;
 s=p;while (*p && *p!=' ')p++;n=p-s;*line=p;
 p=tracked_alloc(n+1);memcpy(p,s,n);p[n]=0;return p;
}
'''
suffix=r'''
static int action(int km,long x,long y) { struct event ev;ev.ev=EV_KBD;ev.x=x;ev.y=y;return kbd_action(km,&ev); }
static void test_map(int km,struct default_kb *start) {
 struct default_kb *p,*q;int expected,before;
 for(p=start;p->x;p++) {
  expected=-1;for(q=start;q->x;q++)if(q->x==p->x && q->y==p->y)expected=q->act;
  assert(action(km,p->x,p->y)==expected);
  before=live;
  add_keybinding(km,ACT_QUIT,p->x,p->y);assert(action(km,p->x,p->y)==ACT_QUIT);
  delete_keybinding(km,p->x,p->y);assert(action(km,p->x,p->y)==-1);
  delete_keybinding(km,p->x,p->y);assert(action(km,p->x,p->y)==-1);
  add_keybinding(km,expected,p->x,p->y);assert(action(km,p->x,p->y)==expected);
#ifdef STATIC_KEYMAP
  assert(live==before+1);
#endif
 }
}
int main(void) {
 struct event ev;int before;
 init_keymaps();
#ifdef STATIC_KEYMAP
 assert(live==0 && allocations==0);assert(sizeof(default_main_keymap[0].x)==2);
#endif
 test_map(KM_MAIN,default_main_keymap);test_map(KM_EDIT,default_edit_keymap);test_map(KM_MENU,default_menu_keymap);
 before=live;add_keybinding(KM_MAIN,ACT_ENTER,70000L,70001L);assert(action(KM_MAIN,70000L,70001L)==ACT_ENTER);
 assert(action(KM_MAIN,4464,4465)==-1);delete_keybinding(KM_MAIN,70000L,70001L);assert(live==before);
 delete_keybinding(KM_MAIN,70000L,70001L);assert(live==before);
 assert(!bind_rd(NULL,(unsigned char *)"main Ctrl-R quit"));assert(action(KM_MAIN,'R',KBD_CTRL)==ACT_QUIT);
 assert(!unbind_rd(NULL,(unsigned char *)"main Ctrl-R"));assert(action(KM_MAIN,'R',KBD_CTRL)==-1);
 assert(!bind_rd(NULL,(unsigned char *)"main Ctrl-R reload"));assert(action(KM_MAIN,'R',KBD_CTRL)==ACT_RELOAD);
 assert(unbind_rd(NULL,(unsigned char *)"main")!=NULL);
 ev.ev=EV_INIT;ev.x='q';ev.y=0;assert(kbd_action(KM_MAIN,&ev)==-1);
 free_keymaps();assert(live==0);init_keymaps();assert(action(KM_MAIN,'q',0)==ACT_QUIT);free_keymaps();assert(live==0);
 puts("PASS: all default mappings, overrides, unbind/rebind, config, long dynamic keys and free");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='links-keymap-') as d:
 p=Path(d);(p/'test.c').write_text(prefix+source+suffix)
 for flags in ([],['-DPDP11_STATIC_KEYMAP']):
  subprocess.run(['cc','-std=gnu89','-DHAVE_CONFIG_H','-DPDP11_SMALL','-Wno-everything','-fsanitize=address,undefined','-I'+str(root/'upstream')]+flags+[str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)

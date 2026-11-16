#!/usr/bin/env python3
"""Exhaustive host test of the source-extracted native cache alignment helper.
Models the12-byte PDP fragment header and32767 signed-int limit. Does not
claim to emulate the target compiler; native rendering remains required.
"""
import pathlib,re,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parent.parent
source=(root/'upstream/cache.c').read_text(encoding='latin1')
m=re.search(r'static int fragment_align\(int length\)\s*\{.*?\n\}',source,re.S)
if not m:raise SystemExit('native fragment_align helper not found')
harness=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
struct fragment { unsigned char simulated_pdp_layout[12]; };
#define MAXINT 32767
static int page_size = 256;
static jmp_buf rejected;
#define overalloc() longjmp(rejected,1)
'''+m.group()+r'''
int main(void) {
    int length, aligned, expected, accepted=0, denied=0;
    const int overhead=12+64;
    assert(sizeof(struct fragment)==12);
    for (length=-1; length<=32767; ++length) {
        if (setjmp(rejected)) {
            assert(length<0 || length>32767-overhead);
            ++denied;
            continue;
        }
        aligned=fragment_align(length);
        assert(length>=0 && length<=32767-overhead);
        /* These are the only arithmetic intermediates in the target helper. */
        assert(length+overhead<=32767);
        assert(((length+overhead)|(page_size-1))<=32767);
        expected=((length+overhead)|255)-overhead;
        assert(aligned==expected);
        assert(aligned>=length);
        assert(aligned+12<=32767);
        assert((aligned+overhead+1)%256==0);
        ++accepted;
    }
    assert(accepted==32692 && denied==77);
    assert(fragment_align(0)==179);
    assert(fragment_align(179)==179);
    assert(fragment_align(180)==435);
    assert(fragment_align(370)==435);
    assert(fragment_align(32691)==32691);
    puts("PASS: all signed16 lengths0..32767, negative rejection,256-byte granule and allocation bounds");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='links-align-') as td:
 p=pathlib.Path(td);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c89','-O0','-Wall','-Wextra','-fsanitize=undefined,address',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

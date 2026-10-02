/* SMB wire primitives; no assumptions about PDP-11 long byte order. */
#include "smbd.h"

unsigned
get16(p)
const u8 *p;
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

u32
get32(p)
const u8 *p;
{
    return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24);
}

void
put16(p, v)
u8 *p;
unsigned v;
{
    p[0] = v; p[1] = v >> 8;
}

void
put32(p, v)
u8 *p;
u32 v;
{
    p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24;
}

void
put64(p, lo, hi)
u8 *p;
u32 lo, hi;
{
    put32(p,lo); put32(p+4,hi);
}

int
bounds(off, len, total)
unsigned off, len, total;
{
    return off <= total && len <= total-off;
}

void
filetime(p, seconds)
u8 *p;
long seconds;
{
    /* Unix epoch = 0x019db1ded53e8000 FILETIME ticks. Multiply using
     * base-65536 limbs, keeping every intermediate within 32 bits. */
    u32 limbs[4], carry, v;
    unsigned i;
    if (seconds < 0) seconds=0;
    limbs[0]=(u32)seconds & 65535UL;
    limbs[1]=((u32)seconds>>16)&65535UL;
    limbs[2]=limbs[3]=0;
    /* 10,000,000 = 10,000 * 1,000. */
    carry=0;
    for(i=0;i<4;i++) { v=limbs[i]*10000UL+carry; limbs[i]=v&65535UL; carry=v>>16; }
    carry=0;
    for(i=0;i<4;i++) { v=limbs[i]*1000UL+carry; limbs[i]=v&65535UL; carry=v>>16; }
    v=limbs[0]+32768UL; limbs[0]=v&65535UL;
    v=limbs[1]+54590UL+(v>>16); limbs[1]=v&65535UL;
    v=limbs[2]+45534UL+(v>>16); limbs[2]=v&65535UL;
    v=limbs[3]+413UL+(v>>16); limbs[3]=v&65535UL;
    for(i=0;i<4;i++) put16(p+i*2,(unsigned)limbs[i]);
}

int
utf16_decode(out, cap, in, len)
char *out;
unsigned cap;
const u8 *in;
unsigned len;
{
    unsigned i,c;
    if ((len&1) || len/2 >= cap) return -1;
    for(i=0;i<len/2;i++) {
        c=get16(in+i*2);
        if(c<32 || c>126) return -1;
        out[i]=(char)c;
    }
    out[len/2]=0;
    return 0;
}

unsigned
utf16_encode(out, in, cap)
u8 *out;
const char *in;
unsigned cap;
{
    unsigned n;
    n=0;
    while(*in && n+2<=cap) { put16(out+n,(unsigned char)*in++); n+=2; }
    return n;
}

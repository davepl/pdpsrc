/* Small streaming hashes for NTLMv2 and SMB 2.0.2. No native wire casts.
 * MD4: RFC 1320; MD5: RFC 1321; SHA-256: FIPS 180-4;
 * HMAC: RFC 2104. All long-word operations are reduced to 32 bits. */
#include "smbd.h"
#define ROL(x,n) ((((x) << (n)) | ((x) >> (32-(n)))) & MASK32)
#define ROR(x,n) ((((x) >> (n)) | ((x) << (32-(n)))) & MASK32)

static u32 md5k[64] = {
 0xd76aa478UL,0xe8c7b756UL,0x242070dbUL,0xc1bdceeeUL,
 0xf57c0fafUL,0x4787c62aUL,0xa8304613UL,0xfd469501UL,
 0x698098d8UL,0x8b44f7afUL,0xffff5bb1UL,0x895cd7beUL,
 0x6b901122UL,0xfd987193UL,0xa679438eUL,0x49b40821UL,
 0xf61e2562UL,0xc040b340UL,0x265e5a51UL,0xe9b6c7aaUL,
 0xd62f105dUL,0x02441453UL,0xd8a1e681UL,0xe7d3fbc8UL,
 0x21e1cde6UL,0xc33707d6UL,0xf4d50d87UL,0x455a14edUL,
 0xa9e3e905UL,0xfcefa3f8UL,0x676f02d9UL,0x8d2a4c8aUL,
 0xfffa3942UL,0x8771f681UL,0x6d9d6122UL,0xfde5380cUL,
 0xa4beea44UL,0x4bdecfa9UL,0xf6bb4b60UL,0xbebfbc70UL,
 0x289b7ec6UL,0xeaa127faUL,0xd4ef3085UL,0x04881d05UL,
 0xd9d4d039UL,0xe6db99e5UL,0x1fa27cf8UL,0xc4ac5665UL,
 0xf4292244UL,0x432aff97UL,0xab9423a7UL,0xfc93a039UL,
 0x655b59c3UL,0x8f0ccc92UL,0xffeff47dUL,0x85845dd1UL,
 0x6fa87e4fUL,0xfe2ce6e0UL,0xa3014314UL,0x4e0811a1UL,
 0xf7537e82UL,0xbd3af235UL,0x2ad7d2bbUL,0xeb86d391UL
};
static u8 md5s[16] = {7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21};
static u8 md4s[12] = {3,7,11,19,3,5,9,13,3,9,11,15};
static u8 md4g[16] = {0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15};
static u32 sha_k[64] = {
 0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,
 0x3956c25bUL,0x59f111f1UL,0x923f82a4UL,0xab1c5ed5UL,
 0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
 0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,
 0xe49b69c1UL,0xefbe4786UL,0x0fc19dc6UL,0x240ca1ccUL,
 0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
 0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,
 0xc6e00bf3UL,0xd5a79147UL,0x06ca6351UL,0x14292967UL,
 0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
 0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,
 0xa2bfe8a1UL,0xa81a664bUL,0xc24b8b70UL,0xc76c51a3UL,
 0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
 0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,
 0x391c0cb3UL,0x4ed8aa4aUL,0x5b9cca4fUL,0x682e6ff3UL,
 0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
 0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL
};

static u32 be32(p)
const u8 *p;
{
 return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static void store_be(p,v)
u8 *p;
u32 v;
{
 p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v;
}
static void md_block(c,algorithm)
struct hashctx *c;
int algorithm;
{
 u32 x[16],a,b,d,e,f,t,k;
 unsigned i,g,s;
 a=c->h[0]; b=c->h[1]; d=c->h[2]; e=c->h[3];
 for(i=0;i<16;i++) x[i]=get32(c->block+4*i);
 for(i=0;i<(algorithm==4?48:64);i++) {
  if(algorithm==4) {
   if(i<16) { f=(b&d)|((~b)&e); g=i; k=0; }
   else if(i<32) { f=(b&d)|(b&e)|(d&e); g=((i&3)*4)+((i-16)/4); k=0x5a827999UL; }
   else { f=b^d^e; g=md4g[i-32]; k=0x6ed9eba1UL; }
   s=md4s[(i/16)*4+(i&3)];
  } else {
   if(i<16) { f=(b&d)|((~b)&e); g=i; }
   else if(i<32) { f=(e&b)|((~e)&d); g=(5*i+1)&15; }
   else if(i<48) { f=b^d^e; g=(3*i+5)&15; }
   else { f=d^(b|(~e)); g=(7*i)&15; }
   k=md5k[i]; s=md5s[(i/16)*4+(i&3)];
  }
  t=(a+f+x[g]+k)&MASK32;
  t=ROL(t,s);
  if(algorithm==5) t=(t+b)&MASK32;
  a=e; e=d; d=b; b=t;
 }
 c->h[0]=(c->h[0]+a)&MASK32; c->h[1]=(c->h[1]+b)&MASK32;
 c->h[2]=(c->h[2]+d)&MASK32; c->h[3]=(c->h[3]+e)&MASK32;
 memset(x,0,sizeof(x));
}
static void sha_block(c)
struct hashctx *c;
{
 u32 w[16],a,b,d,e,f,g,h,j,x,y,t1,t2;
 unsigned i;
 a=c->h[0]; b=c->h[1]; d=c->h[2]; e=c->h[3];
 f=c->h[4]; g=c->h[5]; h=c->h[6]; j=c->h[7];
 for(i=0;i<64;i++) {
  if(i<16) w[i]=be32(c->block+4*i);
  else {
   x=w[(i+1)&15]; y=w[(i+14)&15];
   x=ROR(x,7)^ROR(x,18)^(x>>3);
   y=ROR(y,17)^ROR(y,19)^(y>>10);
   w[i&15]=(w[i&15]+x+w[(i+9)&15]+y)&MASK32;
  }
  x=ROR(f,6)^ROR(f,11)^ROR(f,25);
  t1=(j+x+((f&g)^((~f)&h))+sha_k[i]+w[i&15])&MASK32;
  y=ROR(a,2)^ROR(a,13)^ROR(a,22);
  t2=(y+((a&b)^(a&d)^(b&d)))&MASK32;
  j=h; h=g; g=f; f=(e+t1)&MASK32;
  e=d; d=b; b=a; a=(t1+t2)&MASK32;
 }
 c->h[0]=(c->h[0]+a)&MASK32; c->h[1]=(c->h[1]+b)&MASK32;
 c->h[2]=(c->h[2]+d)&MASK32; c->h[3]=(c->h[3]+e)&MASK32;
 c->h[4]=(c->h[4]+f)&MASK32; c->h[5]=(c->h[5]+g)&MASK32;
 c->h[6]=(c->h[6]+h)&MASK32; c->h[7]=(c->h[7]+j)&MASK32;
 memset(w,0,sizeof(w));
}
void hash_init(c,algorithm)
struct hashctx *c;
int algorithm;
{
 memset(c,0,sizeof(*c));
 if(algorithm==256) {
  c->h[0]=0x6a09e667UL; c->h[1]=0xbb67ae85UL;
  c->h[2]=0x3c6ef372UL; c->h[3]=0xa54ff53aUL;
  c->h[4]=0x510e527fUL; c->h[5]=0x9b05688cUL;
  c->h[6]=0x1f83d9abUL; c->h[7]=0x5be0cd19UL;
 } else {
  c->h[0]=0x67452301UL; c->h[1]=0xefcdab89UL;
  c->h[2]=0x98badcfeUL; c->h[3]=0x10325476UL;
 }
}
void hash_update(c,algorithm,p,n)
struct hashctx *c;
int algorithm;
const u8 *p;
unsigned n;
{
 unsigned take;
 u32 prev;
 prev=c->lo; c->lo=(c->lo+(u32)n)&MASK32;
 if(c->lo<prev) c->hi=(c->hi+1)&MASK32;
 while(n) {
  take=64-c->used; if(take>n) take=n;
  memcpy(c->block+c->used,p,take); c->used+=take; p+=take; n-=take;
  if(c->used==64) {
   if(algorithm==256) sha_block(c); else md_block(c,algorithm);
   c->used=0;
  }
 }
}
void hash_final(c,algorithm,out)
struct hashctx *c;
int algorithm;
u8 *out;
{
 u8 tail[72];
 unsigned pad,i;
 u32 lo,hi;
 lo=(c->lo<<3)&MASK32; hi=((c->hi<<3)|(c->lo>>29))&MASK32;
 pad=c->used<56?56-c->used:120-c->used;
 memset(tail,0,sizeof(tail)); tail[0]=0x80;
 if(algorithm==256) { store_be(tail+pad,hi); store_be(tail+pad+4,lo); }
 else { put32(tail+pad,lo); put32(tail+pad+4,hi); }
 hash_update(c,algorithm,tail,pad+8);
 for(i=0;i<(algorithm==256?8:4);i++) {
  if(algorithm==256) store_be(out+4*i,c->h[i]);
  else put32(out+4*i,c->h[i]);
 }
 memset(c,0,sizeof(*c)); memset(tail,0,sizeof(tail));
}
void hmac_init(c,algorithm,key,n)
struct hmacctx *c;
int algorithm;
const u8 *key;
unsigned n;
{
 u8 block[64],digest[32];
 unsigned i;
 memset(block,0,sizeof(block)); memset(digest,0,sizeof(digest));
 if(n>64) {
  hash_init(&c->inner,algorithm); hash_update(&c->inner,algorithm,key,n);
  hash_final(&c->inner,algorithm,digest); n=algorithm==256?32:16; key=digest;
 }
 memcpy(block,key,n); c->algorithm=algorithm;
 for(i=0;i<64;i++) { c->outer[i]=block[i]^0x5c; block[i]^=0x36; }
 hash_init(&c->inner,algorithm); hash_update(&c->inner,algorithm,block,64);
 memset(block,0,sizeof(block)); memset(digest,0,sizeof(digest));
}
void hmac_update(c,p,n)
struct hmacctx *c;
const u8 *p;
unsigned n;
{
 hash_update(&c->inner,c->algorithm,p,n);
}
void hmac_final(c,out)
struct hmacctx *c;
u8 *out;
{
 u8 digest[32];
 int algorithm;
 algorithm=c->algorithm;
 hash_final(&c->inner,algorithm,digest);
 hash_init(&c->inner,algorithm); hash_update(&c->inner,algorithm,c->outer,64);
 hash_update(&c->inner,algorithm,digest,algorithm==256?32:16);
 hash_final(&c->inner,algorithm,out);
 memset(digest,0,sizeof(digest)); memset(c,0,sizeof(*c));
}
int constant_equal(a,b,n)
const u8 *a,*b;
unsigned n;
{
 volatile unsigned diff;
 diff=0;
 while(n--) diff|=*a++ ^ *b++;
 return diff==0;
}

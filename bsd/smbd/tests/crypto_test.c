/* Public known-answer vectors: RFC 1320, RFC 1321, RFC 2202,
 * RFC 4231, FIPS 180-4 and MS-NLMP 4.2.4. Small enough for native cc. */
#include "../smbd.h"
static int failures;
static void check(got,hex,n,label)
const u8 *got;
const char *hex,*label;
unsigned n;
{
 unsigned i,x;
 char pair[3];
 for(i=0;i<n;i++) {
  pair[0]=hex[2*i]; pair[1]=hex[2*i+1]; pair[2]=0;
  x=(unsigned)strtol(pair,(char **)0,16);
  if(got[i]!=x) { printf("FAIL %s byte %u\n",label,i); failures++; return; }
 }
}
static void digest(algorithm,msg,hex)
int algorithm;
const char *msg,*hex;
{
 struct hashctx c;
 u8 out[32];
 unsigned i,n;
 n=(unsigned)strlen(msg);
 hash_init(&c,algorithm); hash_update(&c,algorithm,(const u8 *)msg,n);
 hash_final(&c,algorithm,out); check(out,hex,algorithm==256?32:16,"whole hash");
 hash_init(&c,algorithm);
 for(i=0;i<n;i++) hash_update(&c,algorithm,(const u8 *)msg+i,1);
 hash_final(&c,algorithm,out); check(out,hex,algorithm==256?32:16,"stream hash");
}
int main()
{
 struct hashctx c;
 struct hmacctx h;
 u8 key[131],out[32],encoded[128],nt[16];
 unsigned n,i;
 digest(4,"","31d6cfe0d16ae931b73c59d7e0c089c0");
 digest(4,"a","bde52cb31de33e46245e05fbdbd6fb24");
 digest(4,"abc","a448017aaf21d8525fc10ae87aa6729d");
 digest(4,"abcdefghijklmnopqrstuvwxyz","d79e1c308aa5bbcdeea8ed63df412da9");
 digest(4,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789","043f8582f241db351ce627e153e7f0e4");
 digest(4,"12345678901234567890123456789012345678901234567890123456789012345678901234567890","e33b4ddc9c38f2199c3e7b164fcc0536");
 digest(5,"","d41d8cd98f00b204e9800998ecf8427e");
 digest(5,"a","0cc175b9c0f1b6a831c399e269772661");
 digest(5,"abc","900150983cd24fb0d6963f7d28e17f72");
 digest(5,"abcdefghijklmnopqrstuvwxyz","c3fcd3d76192e4007dfb496cca67e13b");
 digest(5,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789","d174ab98d277d9f5a5611c2c9f419d9f");
 digest(5,"12345678901234567890123456789012345678901234567890123456789012345678901234567890","57edf4a22be3c955ac49da2e2107b67a");
 digest(256,"","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
 digest(256,"abc","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 digest(256,"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
 /* A long streaming input exercises repeated transforms without heap. */
 memset(encoded,'a',100); hash_init(&c,256);
 for(i=0;i<10000;i++) hash_update(&c,256,encoded,100);
 hash_final(&c,256,out);
 check(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",32,"million a SHA256");
 memset(key,0x0b,20); hmac_init(&h,5,key,16); hmac_update(&h,(const u8 *)"Hi There",8); hmac_final(&h,out);
 check(out,"9294727a3638bb1c13f48ef8158bfc9d",16,"HMAC MD5");
 hmac_init(&h,256,key,20); hmac_update(&h,(const u8 *)"Hi There",8); hmac_final(&h,out);
 check(out,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",32,"HMAC SHA256");
 memset(key,0xaa,sizeof(key)); hmac_init(&h,256,key,sizeof(key));
 hmac_update(&h,(const u8 *)"Test Using Larger Than Block-Size Key - Hash Key First",54); hmac_final(&h,out);
 check(out,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",32,"long key HMAC SHA256");
 n=utf16_encode(encoded,"Password",sizeof(encoded));
 hash_init(&c,4); hash_update(&c,4,encoded,n); hash_final(&c,4,nt);
 check(nt,"a4f49c406510bdcab6824ee7c30fd852",16,"NT password hash");
 n=utf16_encode(encoded,"USERDomain",sizeof(encoded));
 hmac_init(&h,5,nt,16); hmac_update(&h,encoded,n); hmac_final(&h,out);
 check(out,"0c868a403bfd7a93a3001ef22ef02e3f",16,"NTLMv2 response key");
 if(!constant_equal(out,out,16)) failures++;
 memcpy(nt,out,16); nt[15]^=1;
 if(constant_equal(out,nt,16)) failures++;
 printf("crypto known-answer tests: %s\n",failures?"FAILED":"PASS");
 return failures?1:0;
}

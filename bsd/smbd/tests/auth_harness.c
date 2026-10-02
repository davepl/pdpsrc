/* Standalone auth test harness; never linked into the daemon. */
#include "../smbd.h"
struct config cfg;
struct authstate auth;
static unsigned long seed=1;
static unsigned rnd()
{
 seed=(seed*1103515245UL+12345UL)&0x7fffffffUL;
 return (unsigned)(seed>>8);
}
void test_init()
{
 struct hashctx c;
 u8 p[32];
 unsigned n;
 memset(&cfg,0,sizeof(cfg)); memset(&auth,0,sizeof(auth)); cfg.user="User";
 n=utf16_encode(p,"Password",32); hash_init(&c,4); hash_update(&c,4,p,n);
 hash_final(&c,4,cfg.nthash); memcpy(auth.challenge,"12345678",8);
}
int test_hash_file(path)
char *path;
{
 test_init(); cfg.hash_file=path; return auth_load();
}
void test_key(key)
u8 *key;
{
 memcpy(key,auth.key,16);
}
#ifndef AUTH_HELPERS_ONLY
int main(argc,argv)
int argc;
char **argv;
{
 unsigned i,j,n,out_n;
 u8 in[4096],out[4096];
 if(argc==3) {
  if(strcmp(argv[1],"pool")==0) {
   cfg.random_file=argv[2];
   return auth_random_init() && auth_random_challenge()?0:1;
  }
  cfg.user="User";
  if(strcmp(argv[1],"hash")==0) cfg.hash_file=argv[2];
  else cfg.password_file=argv[2];
  return auth_load()?0:1;
 }
 cfg.user="User";
 for(i=0;i<50000;i++) {
  memset(&auth,0,sizeof(auth)); n=rnd()%4096;
  for(j=0;j<n;j++) in[j]=(u8)rnd();
  if(i&1) {
   if(n<64) n=64;
   memcpy(in,"NTLMSSP",8); put32(in+8,i&2?3UL:1UL); auth.phase=i&2?1:0;
  }
  (void)auth_session(in,n,out,&out_n);
  if(out_n>4096) return 1;
 }
 puts("auth 50000 bounded malformed tokens PASS"); return 0;
}
#endif

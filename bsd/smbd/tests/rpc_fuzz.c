/* Host sanitizer harness for RPC parser state and arbitrary SMB write splits.
 * Independent integration/NDR checks are in rpc_test.py. */
#include "../smbd.h"
#include <assert.h>
struct config cfg;
struct authstate auth;
u8 request[SMBD_BUFSIZE],response[SMBD_BUFSIZE],related_file[16];
unsigned request_len,response_len;
#include "../rpc.c"

static struct rpcpipe sample;
static u8 bind_packet[72],call_packet[60],mutant[128];
static u32 seed=0x713ac91UL;
static unsigned fuzz_next(void)
{
 seed=(seed*1664525UL+1013904223UL)&MASK32;
 return (unsigned)(seed>>16);
}
static void invariants(void)
{
 assert(sample.stub_len<=RPC_LIMIT);
 assert(sample.rxlen<=RPC_LIMIT-sample.stub_len);
 assert(sample.outpos<=sample.outlen && sample.outlen<=RPC_LIMIT);
 assert(sample.ncontexts<=RPC_CONTEXTS);
}
static void bind_sample(void)
{
 memset(&sample,0,sizeof(sample));
 assert(feed(&sample,bind_packet,sizeof(bind_packet))==ST_OK);
 assert(sample.outlen && sample.data[2]==12);
 sample.outpos=sample.outlen;
}
int main(void)
{
 unsigned i,j,n,split,count;
 u32 status;
 cfg.share="pdp";
 rpc_header(bind_packet,11,sizeof(bind_packet),7UL);
 put16(bind_packet+16,RPC_LIMIT);put16(bind_packet+18,RPC_LIMIT);
 bind_packet[24]=1;bind_packet[30]=1;
 memcpy(bind_packet+32,srv_uuid,20);memcpy(bind_packet+52,ndr_uuid,20);
 rpc_header(call_packet,0,sizeof(call_packet),8UL);
 put32(call_packet+16,36UL);put16(call_packet+22,15);
 put32(call_packet+28,1UL);put32(call_packet+32,1UL);put32(call_packet+36,1UL);
 put32(call_packet+48,0xffffffffUL);put32(call_packet+52,1UL);
 for(i=0;i<=sizeof(bind_packet);i++) {
  memset(&sample,0,sizeof(sample));
  assert(feed(&sample,bind_packet,i)==ST_OK);
  assert(feed(&sample,bind_packet+i,sizeof(bind_packet)-i)==ST_OK || i==sizeof(bind_packet));
  assert(sample.outlen && sample.data[2]==12);invariants();
 }
 for(i=0;i<=sizeof(call_packet);i++) {
  bind_sample();
  assert(feed(&sample,call_packet,i)==ST_OK);
  status=feed(&sample,call_packet+i,sizeof(call_packet)-i);
  assert(status==ST_OK || i==sizeof(call_packet));
  assert(sample.outlen && sample.data[2]==2);invariants();
 }
 for(i=0;i<20000;i++) {
  if(i&1) { bind_sample();memcpy(mutant,call_packet,sizeof(call_packet));n=sizeof(call_packet); }
  else { memset(&sample,0,sizeof(sample));memcpy(mutant,bind_packet,sizeof(bind_packet));n=sizeof(bind_packet); }
  count=fuzz_next()%5+1;
  for(j=0;j<count;j++) mutant[fuzz_next()%n]^=(u8)(fuzz_next()|1);
  if((i%3)==0) n=fuzz_next()%(n+1);
  split=fuzz_next()%(n+1);
  feed(&sample,mutant,split);invariants();
  feed(&sample,mutant+split,n-split);invariants();
 }
 puts("PASS RPC parser: all bind/request byte splits and 20000 bounded malformed mutations");
 return 0;
}

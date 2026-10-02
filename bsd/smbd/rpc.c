/* Bounded srvsvc RPC over SMB2 IPC$ pipes. Original implementation from
 * MS-SRVS 2.2.4/3.1.4.8/3.1.4.10 and MS-RPCE/C706 connection-oriented NDR32.
 * No host structs, pointers, or native long representations appear on wire. */
#include "smbd.h"

#define RPC_LIMIT 4096
#define RPC_PIPES 2
#define RPC_CONTEXTS 4
#define PIPE_EMPTY 0xc00000d9UL
#define PIPE_BUSY 0xc00000aeUL
#define PIPE_TRANSCEIVE 0x0011c017UL
#define PIPE_PEEK 0x0011400cUL
#define PIPE_WAIT 0x00110018UL
#define RPC_PROTOCOL 0x1c01000bUL
#define RPC_OPNUM 0x1c010002UL
#define RPC_CONTEXT 0x1c00001aUL

struct rpcpipe {
 int used, assembling;
 u32 tree, generation, call;
 unsigned contexts[RPC_CONTEXTS], ncontexts, context, opnum;
 unsigned rxlen, stub_len, outlen, outpos;
 u8 data[RPC_LIMIT];
};
static struct rpcpipe pipes[RPC_PIPES];
static u32 next_generation;
static u8 srv_uuid[20] = {
 0xc8,0x4f,0x32,0x4b,0x70,0x16,0xd3,0x01,0x12,0x78,0x5a,0x47,0xbf,0x6e,0xe1,0x88,3,0,0,0
};
static u8 ndr_uuid[20] = {
 0x04,0x5d,0x88,0x8a,0xeb,0x1c,0xc9,0x11,0x9f,0xe8,8,0,0x2b,0x10,0x48,0x60,2,0,0,0
};
struct reader { const u8 *p; unsigned n, pos; int bad; };
struct writer { u8 *p; unsigned pos; int bad; };

static u32 rd(r)
struct reader *r;
{
 u32 value;
 if (!bounds(r->pos,4,r->n)) { r->bad=1; return 0; }
 value=get32(r->p+r->pos); r->pos+=4; return value;
}
static int string_in(r,out,cap)
struct reader *r;
char *out;
unsigned cap;
{
 u32 maximum,offset,count;
 unsigned n,i,c;
 maximum=rd(r); offset=rd(r); count=rd(r);
 if (r->bad || count==0 || maximum>1024UL || offset>maximum ||
     count>maximum-offset || count>1024UL) { r->bad=1; return 0; }
 n=(unsigned)count;
 if (!bounds(r->pos,n*2,r->n) || get16(r->p+r->pos+(n-1)*2)!=0 ||
     (out && n>cap)) { r->bad=1; return 0; }
 for (i=0;i<n-1;i++) {
  c=get16(r->p+r->pos+i*2);
  if (!c || (out && (c<32 || c>126))) { r->bad=1; return 0; }
  if (out) out[i]=(char)c;
 }
 if (out) out[n-1]=0;
 r->pos=(r->pos+n*2+3)&~3;
 if (r->pos>r->n) r->bad=1;
 return !r->bad;
}
static void wr(w,value)
struct writer *w;
u32 value;
{
 if (!bounds(w->pos,4,RPC_LIMIT)) { w->bad=1; return; }
 put32(w->p+w->pos,value); w->pos+=4;
}
static void string_out(w,text)
struct writer *w;
const char *text;
{
 unsigned n,i;
 n=(unsigned)strlen(text)+1;
 wr(w,(u32)n); wr(w,0UL); wr(w,(u32)n);
 if (w->bad || !bounds(w->pos,n*2+3,RPC_LIMIT)) { w->bad=1; return; }
 for (i=0;i<n;i++) put16(w->p+w->pos+i*2,(unsigned char)text[i]);
 w->pos+=n*2;
 while(w->pos&3) w->p[w->pos++]=0;
}
static int same(a,b)
const char *a,*b;
{
 unsigned x,y;
 do {
  x=(unsigned char)*a++; y=(unsigned char)*b++;
  if (x>='A' && x<='Z') x+=32;
  if (y>='A' && y<='Z') y+=32;
  if (x!=y) return 0;
 } while(x);
 return 1;
}
static const char *share_name(index)
unsigned index;
{ return index ? "IPC$" : cfg.share; }
static const char *share_remark(index)
unsigned index;
{ return index ? "Remote IPC" : "PDP-11 files"; }
static u32 share_type(index)
unsigned index;
{ return index ? 0x80000003UL : 0UL; }

/* Fixed portions precede deferred string referents in NDR conformant arrays. */
static void share_fixed(w,level,index)
struct writer *w;
unsigned level,index;
{
 wr(w,0x20000UL+(u32)index*16);
 if (!level) return;
 wr(w,share_type(index)); wr(w,0x20004UL+(u32)index*16);
 if (level==2) {
  wr(w,0UL); wr(w,0xffffffffUL); wr(w,0UL);
  wr(w,0x20008UL+(u32)index*16); wr(w,0UL);
 }
}
static void share_strings(w,level,index)
struct writer *w;
unsigned level,index;
{
 string_out(w,share_name(index));
 if (level) string_out(w,share_remark(index));
 if (level==2) string_out(w,"/");
}
static unsigned share_cost(level,index)
unsigned level,index;
{
 unsigned n;
 n=4+12+(((unsigned)strlen(share_name(index))+1)*2+3)/4*4;
 if (level) n+=8+12+(((unsigned)strlen(share_remark(index))+1)*2+3)/4*4;
 if (level==2) n+=20+16;
 return n;
}

static u32 share_enum(r,w)
struct reader *r;
struct writer *w;
{
 u32 level,tag,container,entries,buffer,maximum,resumeptr,resume,available,cost;
 unsigned start,count,i;
 level=rd(r); tag=rd(r); container=rd(r);
 if (level!=tag || !container) { r->bad=1; return 87UL; }
 entries=rd(r); buffer=rd(r);
 if (entries || (buffer && rd(r))) { r->bad=1; return 87UL; }
 maximum=rd(r); resumeptr=rd(r); resume=resumeptr?rd(r):0UL;
 if (r->bad || r->pos!=r->n) return 87UL;
 if (level>2UL) {
  wr(w,level); wr(w,level); wr(w,0UL); wr(w,0UL);
  wr(w,resumeptr?0x10008UL:0UL); if(resumeptr) wr(w,0UL);
  return 124UL;
 }
 start=resume<2UL?(unsigned)resume:2; count=0; available=0;
 for(i=start;i<2;i++) {
  cost=share_cost((unsigned)level,i);
  if (maximum!=0xffffffffUL && cost>maximum-available) break;
  available+=cost; count++;
 }
 wr(w,level); wr(w,level); wr(w,0x10000UL);
 wr(w,(u32)count); wr(w,count?0x10004UL:0UL);
 if (count) {
  wr(w,(u32)count);
  for(i=0;i<count;i++) share_fixed(w,(unsigned)level,start+i);
  for(i=0;i<count;i++) share_strings(w,(unsigned)level,start+i);
 }
 wr(w,(u32)(2-start)); wr(w,resumeptr?0x10008UL:0UL);
 if (resumeptr) wr(w,(u32)(start+count));
 return start+count<2 ? 234UL : 0UL;
}
static u32 share_get(r,w)
struct reader *r;
struct writer *w;
{
 char name[84];
 u32 level;
 unsigned index;
 if (!string_in(r,name,sizeof(name))) return 87UL;
 level=rd(r);
 if (r->bad || r->pos!=r->n) return 87UL;
 wr(w,level);
 if (level>2 && level!=1005UL) { wr(w,0UL); return 124UL; }
 if (same(name,cfg.share)) index=0;
 else if (same(name,"IPC$")) index=1;
 else { wr(w,0UL); return 2310UL; }
 wr(w,0x10000UL);
 if (level==1005UL) wr(w,index?0UL:0x30UL);
 else { share_fixed(w,(unsigned)level,index); share_strings(w,(unsigned)level,index); }
 return 0UL;
}

static void rpc_header(p,type,length,call)
u8 *p;
unsigned type,length;
u32 call;
{
 memset(p,0,16); p[0]=5; p[2]=(u8)type; p[3]=3; p[4]=0x10;
 put16(p+8,length); put32(p+12,call);
}
static void finish(h,p,n)
struct rpcpipe *h;
const u8 *p;
unsigned n;
{
 memcpy(h->data,p,n); h->outlen=n; h->outpos=0;
 h->rxlen=h->stub_len=0; h->assembling=0;
}
static void fault(h,code)
struct rpcpipe *h;
u32 code;
{
 u8 *out;
 out=response+128; memset(out,0,32);
 rpc_header(out,3,32,h->call); put16(out+20,h->context); put32(out+24,code);
 finish(h,out,32);
}
static int has_context(h,id)
struct rpcpipe *h;
unsigned id;
{
 unsigned i;
 for(i=0;i<h->ncontexts;i++) if(h->contexts[i]==id) return 1;
 return 0;
}
static void bind_rpc(h,p,n,type)
struct rpcpipe *h;
const u8 *p;
unsigned n,type;
{
 unsigned count,i,j,pos,syntaxes,id,match,result,reason,outpos,kept;
 unsigned results[8],reasons[8],ids[8];
 u8 *out;
 const char *endpoint;
 if(n<28 || (p[3]&3)!=3 || (p[3]&~0x17) || p[24]==0 || p[24]>8 || get16(p+10)) {
  fault(h,RPC_PROTOCOL); return;
 }
 count=p[24]; pos=28; kept=0;
 for(i=0;i<count;i++) {
  if(!bounds(pos,24,n)) { fault(h,RPC_PROTOCOL); return; }
  id=get16(p+pos); syntaxes=p[pos+2];
  if(!syntaxes || syntaxes>8 || !bounds(pos+24,syntaxes*20,n)) {
   fault(h,RPC_PROTOCOL); return;
  }
  result=2; reason=1;
  if(!memcmp(p+pos+4,srv_uuid,20)) {
   reason=2; match=0;
   for(j=0;j<syntaxes;j++) if(!memcmp(p+pos+24+j*20,ndr_uuid,20)) match=1;
   if(match && kept==RPC_CONTEXTS) reason=3;
   if(match && kept<RPC_CONTEXTS) {
    result=reason=0; ids[kept++]=id;
   }
  }
  results[i]=result; reasons[i]=reason; pos+=24+syntaxes*20;
 }
 if(pos!=n) { fault(h,RPC_PROTOCOL); return; }
 out=response+128; memset(out,0,256);
 put16(out+16,RPC_LIMIT); put16(out+18,RPC_LIMIT); put32(out+20,1UL);
 endpoint="\\PIPE\\srvsvc"; j=(unsigned)strlen(endpoint)+1;
 put16(out+24,j); memcpy(out+26,endpoint,j);
 outpos=(26+j+3)&~3; out[outpos]=(u8)count; outpos+=4;
 for(i=0;i<count;i++) {
  put16(out+outpos,results[i]); put16(out+outpos+2,reasons[i]);
  if(!results[i]) memcpy(out+outpos+4,ndr_uuid,20);
  outpos+=24;
 }
 h->ncontexts=kept;
 for(i=0;i<kept;i++) h->contexts[i]=ids[i];
 rpc_header(out,type==14?15:12,outpos,h->call); finish(h,out,outpos);
}
static void call_rpc(h)
struct rpcpipe *h;
{
 struct reader r;
 struct writer w;
 u32 status,server;
 u8 *out;
 if(!has_context(h,h->context)) { fault(h,RPC_CONTEXT); return; }
 if(h->opnum!=15 && h->opnum!=16) { fault(h,RPC_OPNUM); return; }
 r.p=h->data; r.n=h->stub_len; r.pos=0; r.bad=0;
 out=response+128; w.p=out; w.pos=24; w.bad=0;
 server=rd(&r); if(server) string_in(&r,(char *)0,0);
 if(r.bad) { fault(h,RPC_PROTOCOL); return; }
 if(h->opnum==15) status=share_enum(&r,&w);
 else if(h->opnum==16) status=share_get(&r,&w);
 else { fault(h,RPC_OPNUM); return; }
 if(r.bad || w.bad) { fault(h,RPC_PROTOCOL); return; }
 /* Invalid input which cannot be represented by the selected union is a fault. */
 if(w.pos==24) { fault(h,RPC_PROTOCOL); return; }
 wr(&w,status);
 rpc_header(out,2,w.pos,h->call); put32(out+16,(u32)(w.pos-24));
 put16(out+20,h->context); out[22]=out[23]=0;
 finish(h,out,w.pos);
}

/* Buffer one logical request, including split SMB writes and DCE fragments.
 * A completed response must be read before the next request can be written. */
static u32 feed(h,p,n)
struct rpcpipe *h;
const u8 *p;
unsigned n;
{
 unsigned take,need,frag,type,flags,i,stub;
 u8 *packet;
 u32 call;
 if(h->outpos<h->outlen) return PIPE_BUSY;
 h->outpos=h->outlen=0;
 while(n) {
  need=h->rxlen<16?16:get16(h->data+h->stub_len+8);
  if(need<16 || need>RPC_LIMIT-h->stub_len) {
   h->call=0; fault(h,RPC_PROTOCOL); return ST_OK;
  }
  take=need-h->rxlen; if(take>n) take=n;
  memcpy(h->data+h->stub_len+h->rxlen,p,take);
  h->rxlen+=take; p+=take; n-=take;
  if(h->rxlen<16) continue;
  packet=h->data+h->stub_len; frag=get16(packet+8);
  if(packet[0]!=5 || packet[1] || packet[4]!=0x10 || packet[5] ||
     packet[6] || packet[7] || frag<16 || frag>RPC_LIMIT-h->stub_len) {
   h->call=get32(packet+12); fault(h,RPC_PROTOCOL); return ST_OK;
  }
  if(h->rxlen<frag) continue;
  call=get32(packet+12); type=packet[2]; flags=packet[3];
  if(type==11 || type==14) {
   h->call=call;
   if(h->assembling) fault(h,RPC_PROTOCOL); else bind_rpc(h,packet,frag,type);
  } else if(type==0) {
   if(frag<24 || get16(packet+10) || (flags&~3)) {
    h->call=call; fault(h,RPC_PROTOCOL);
   } else if(flags&1) {
    if(h->assembling) { h->call=call; fault(h,RPC_PROTOCOL); }
    else {
     h->call=call; h->context=get16(packet+20); h->opnum=get16(packet+22);
     h->assembling=1;
    }
   } else if(!h->assembling || h->call!=call || h->context!=get16(packet+20) ||
             h->opnum!=get16(packet+22)) {
    h->call=call; fault(h,RPC_PROTOCOL);
   }
   if(!h->outlen) {
    stub=frag-24;
    for(i=0;i<stub;i++) h->data[h->stub_len+i]=packet[24+i];
    h->stub_len+=stub; h->rxlen=0;
    if(flags&2) call_rpc(h);
   }
  } else { h->call=call; fault(h,RPC_OPNUM); }
  if(h->outlen && n) return ST_INVALID;
 }
 return ST_OK;
}

void rpc_init(void)
{ memset(pipes,0,sizeof(pipes)); next_generation=0; }
void rpc_close_all(void)
{ memset(pipes,0,sizeof(pipes)); }
void rpc_close_tree(tree)
u32 tree;
{
 unsigned i;
 for(i=0;i<RPC_PIPES;i++) if(pipes[i].used && pipes[i].tree==tree)
  memset(&pipes[i],0,sizeof(pipes[i]));
}
static void file_id(p,h)
u8 *p;
struct rpcpipe *h;
{
 put32(p,0x50435052UL); put32(p+4,(u32)(h-pipes));
 put32(p+8,h->generation); put32(p+12,h->tree);
}
static struct rpcpipe *find_pipe(p)
const u8 *p;
{
 unsigned i;
 u8 expected[16];
 for(i=0;i<16;i++) if(p[i]!=255) break;
 if(i==16) p=related_file;
 for(i=0;i<RPC_PIPES;i++) if(pipes[i].used && pipes[i].tree==get32(request+36)) {
  file_id(expected,&pipes[i]); if(!memcmp(p,expected,16)) return &pipes[i];
 }
 return (struct rpcpipe *)0;
}
static u32 create_pipe(void)
{
 unsigned off,n,i;
 char name[96],*p;
 u8 *b;
 struct rpcpipe *h;
 b=request+64;
 if(request_len<120 || get16(b)!=57) return ST_INVALID;
 off=get16(b+44); n=get16(b+46);
 if(off<120 || !bounds(off,n,request_len) || utf16_decode(name,sizeof(name),request+off,n))
  return ST_INVALID;
 p=name; while(*p=='\\') p++;
 if(!strncmp(p,"PIPE\\",5) || !strncmp(p,"pipe\\",5)) p+=5;
 if(!same(p,"srvsvc")) return ST_NOT_FOUND;
 if(get32(b+40)&1) return 0xc0000103UL;
 if(get32(b+36)!=1 && get32(b+36)!=3) return ST_DENIED;
 for(i=0;i<RPC_PIPES;i++) if(!pipes[i].used) break;
 if(i==RPC_PIPES) return PIPE_BUSY;
 h=&pipes[i]; memset(h,0,sizeof(*h)); h->used=1; h->tree=get32(request+36);
 next_generation=(next_generation+1)&MASK32;
 if(!next_generation) next_generation=1;
 h->generation=next_generation;
 b=response+64; memset(b,0,88); put16(b,89); put32(b+4,1UL);
 put32(b+56,0x80UL); file_id(b+64,h); memcpy(related_file,b+64,16);
 response_len=152; return ST_OK;
}
static unsigned drain(h,p,count)
struct rpcpipe *h;
u8 *p;
u32 count;
{
 unsigned n;
 n=h->outlen-h->outpos; if(count<(u32)n) n=(unsigned)count;
 memcpy(p,h->data+h->outpos,n); h->outpos+=n; return n;
}
static u32 pipe_info(h)
struct rpcpipe *h;
{
 u8 *b,*p;
 unsigned cls,n;
 b=request+64; p=response+72;
 if(request_len<104 || get16(b)!=41 || b[2]!=1) return ST_INVALID;
 cls=b[3]; memset(p,0,40);
 if(cls==5) { n=24; put32(p+16,1UL); }
 else if(cls==4) { n=40; put32(p+32,0x80UL); }
 else if(cls==23) { n=8; put32(p,1UL); }
 else if(cls==24) {
  n=40; put32(p,1UL); put32(p+4,2UL); put32(p+8,RPC_PIPES);
  put32(p+12,1UL); put32(p+16,RPC_LIMIT); put32(p+20,(u32)(h->outlen-h->outpos));
  put32(p+24,RPC_LIMIT); put32(p+28,RPC_LIMIT); put32(p+32,3UL);
 } else return ST_NOT_SUPPORTED;
 if(get32(b+4)<(u32)n) return ST_TOO_SMALL;
 memset(response+64,0,8); put16(response+64,9); put16(response+66,72);
 put32(response+68,(u32)n); response_len=72+n; return ST_OK;
}

u32 rpc_dispatch(command)
unsigned command;
{
 struct rpcpipe *h;
 u8 *b,*r;
 unsigned off,n,remain;
 u32 count,code,status,output;
 b=request+64; r=response+64;
 if(command==5) return create_pipe();
 if(command==11 && request_len>=120 && get16(b)==57 && get32(b+4)==PIPE_WAIT) {
  /* A wait does not consume an instance; only the implemented pipe exists. */
  off=(unsigned)get32(b+24); count=get32(b+28);
  if(get32(b+24)>SMBD_BUFSIZE || count<14 || count>RPC_LIMIT ||
     !bounds(off,(unsigned)count,request_len)) return ST_INVALID;
  n=(unsigned)get32(request+off+8);
  if(get32(request+off+8)>80UL || n!=count-14 ||
     utf16_decode((char *)(response+128),96,request+off+14,n) ||
     !same((char *)(response+128),"srvsvc")) return ST_NOT_FOUND;
  memset(r,0,48); put16(r,49); put32(r+4,PIPE_WAIT); memcpy(r+8,b+8,16);
  response_len=112; return ST_OK;
 }
 off=command==6?72:command==11?72:command==16?88:command==17?80:80;
 if(!bounds(off,16,request_len)) return ST_INVALID;
 h=find_pipe(request+off); if(!h) return ST_BAD_HANDLE;
 if(command==6) {
  if(request_len<88 || get16(b)!=24) return ST_INVALID;
  memset(h,0,sizeof(*h)); memset(r,0,60); put16(r,60); response_len=124; return ST_OK;
 }
 if(command==16) return pipe_info(h);
 if(command==17) {
  if(request_len<96 || get16(b)!=33 || b[2]!=1 || b[3]!=23) return ST_NOT_SUPPORTED;
  off=get16(b+8); count=get32(b+4);
  if(count!=8 || !bounds(off,8,request_len) || get32(request+off)>1 || get32(request+off+4)>1)
   return ST_INVALID;
  put16(r,2); response_len=66; return ST_OK;
 }
 if(command==8) {
  if(request_len<112 || get16(b)!=49) return ST_INVALID;
  count=get32(b+4);
  if(!count || count>SMBD_TRANSFER || get32(b+8) || get32(b+12)) return ST_INVALID;
  if(h->outpos==h->outlen) return PIPE_EMPTY;
  n=drain(h,response+80,count); remain=h->outlen-h->outpos;
  memset(r,0,16); put16(r,17); r[2]=80; put32(r+4,(u32)n);
  response_len=80+n; return remain?ST_OVERFLOW:ST_OK;
 }
 if(command==9) {
  if(request_len<112 || get16(b)!=49) return ST_INVALID;
  off=get16(b+2); count=get32(b+4);
  if(count>RPC_LIMIT || off<112 || !bounds(off,(unsigned)count,request_len) ||
     get32(b+8) || get32(b+12)) return ST_INVALID;
  status=feed(h,request+off,(unsigned)count); if(status!=ST_OK) return status;
  memset(r,0,16); put16(r,17); put32(r+4,count); response_len=80; return ST_OK;
 }
 if(command!=11) return ST_NOT_SUPPORTED;
 if(request_len<120 || get16(b)!=57 || get32(b+48)!=1UL) return ST_INVALID;
 code=get32(b+4); count=get32(b+28); output=get32(b+44);
 if(code!=PIPE_TRANSCEIVE && code!=PIPE_PEEK) return ST_NOT_SUPPORTED;
 if(code==PIPE_TRANSCEIVE) {
  if(get32(b+24)>SMBD_BUFSIZE || count>RPC_LIMIT) return ST_INVALID;
  off=(unsigned)get32(b+24);
  if(off<120 || !bounds(off,(unsigned)count,request_len) || !output) return ST_INVALID;
  status=feed(h,request+off,(unsigned)count); if(status!=ST_OK) return status;
  n=drain(h,response+112,output); remain=h->outlen-h->outpos;
 } else {
  if(output<16) return ST_TOO_SMALL;
  n=h->outlen-h->outpos;
  if((u32)n>output-16) n=(unsigned)(output-16);
  put32(response+112,3UL); put32(response+116,(u32)(h->outlen-h->outpos));
  put32(response+120,h->outlen>h->outpos?1UL:0UL);
  put32(response+124,(u32)(h->outlen-h->outpos));
  memcpy(response+128,h->data+h->outpos,n); remain=h->outlen-h->outpos-n; n+=16;
 }
 memset(r,0,48); put16(r,49); put32(r+4,code); file_id(r+8,h);
 put32(r+24,112UL); put32(r+32,n?112UL:0UL); put32(r+36,(u32)n);
 response_len=112+n; return remain?ST_OVERFLOW:ST_OK;
}

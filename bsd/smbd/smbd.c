/* Small foreground SMB 2.0.2 server for 2.11BSD and modern Unix.
 * One forked worker per connection; private disk spools bound data memory.
 */
#include "smbd.h"
#include <sys/socket.h>
#include <sys/wait.h>
#ifndef PDP11
#include <sys/time.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

struct config cfg;
struct authstate auth;
u8 request[SMBD_BUFSIZE], response[SMBD_BUFSIZE];
unsigned request_len, response_len;
FILE *request_file;
long request_start, request_size;
int fs_state_fd = -1;
int fs_meta_fd = -1;
int read_fd;
long read_offset, read_length;
u8 related_file[16];
static u8 io_buffer[2048];
static u8 server_guid[16] = {'P','D','P','1','1','S','M','B',2,0,2,0,0,0,0,1};
static int negotiated;
static u32 tree_ids[8], next_tree;
static u8 tree_pipe[8];
static u32 session_lo, session_hi;
static int worker_uid, worker_gid;
static int children[16], child_count;
static char state_path[SMBD_PATH];
static char *temporary_directory = "/tmp";
static struct stat state_stat;
static volatile int stopped;
/* Four credits suffice for Windows; a rotating bounded sequence window
 * detects duplicate/replayed message identifiers, including signed ones. */
static u32 credit_lo, credit_hi;
static unsigned credit_count;
static u8 credit_used[32];

/* The target's root filesystem can be small. Let the operator place disk
 * spools and the sharing registry on a larger local filesystem. */
static FILE *
private_spool()
{
    char path[SMBD_PATH];
    int fd;
    FILE *f;
    strcpy(path,temporary_directory); strcat(path,"/smbd-spool.XXXXXX");
    fd=mkstemp(path);
    if(fd<0) return (FILE *)0;
    if(unlink(path)) { close(fd); return (FILE *)0; }
    f=fdopen(fd,"w+");
    if(!f) close(fd);
    return f;
}

static void
stop_server(sig)
int sig;
{
    (void)sig; stopped=1;
}

static int
full_write(fd, p, len)
int fd;
const u8 *p;
unsigned len;
{
    int n;
    while(len) {
        n=write(fd,p,len);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return -1;
        p+=n; len-=n;
    }
    return 0;
}

static int
spool_hash(f, start, len, digest)
FILE *f;
long start, len;
u8 *digest;
{
    struct hmacctx h;
    unsigned n;
    long pos;
    hmac_init(&h,256,auth.key,16);
    if(fseek(f,start,0)) return -1;
    pos=0;
    while(len>0) {
        n=len>(long)sizeof(io_buffer)?sizeof(io_buffer):(unsigned)len;
        if(fread(io_buffer,1,n,f)!=n) return -1;
        if(pos==0) { if(n<64) return -1; memset(io_buffer+48,0,16); }
        hmac_update(&h,io_buffer,n);
        if(transport_prefetch(io_buffer,sizeof(io_buffer))) return -1;
        pos+=n; len-=n;
    }
    hmac_final(&h,digest);
    return 0;
}

static int
credit_take()
{
    u32 lo,hi,delta;
    unsigned i;
    lo=get32(request+24); hi=get32(request+28);
    delta=(lo-credit_lo)&MASK32;
    if(hi != ((credit_hi+(lo<credit_lo?1UL:0UL))&MASK32) ||
       delta>=credit_count || credit_used[(unsigned)delta]) return -1;
    credit_used[(unsigned)delta]=1;
    while(credit_count && credit_used[0]) {
        for(i=1;i<credit_count;i++) credit_used[i-1]=credit_used[i];
        credit_used[--credit_count]=0;
        credit_lo=(credit_lo+1)&MASK32;
        if(!credit_lo) credit_hi=(credit_hi+1)&MASK32;
    }
    return 0;
}

static unsigned
credit_grant()
{
    unsigned n,want;
    want=get16(request+14);
    if(want==0) want=1;
    n=cfg.max_credits-credit_count;
    if(n>want) n=want;
    /* Grant at least four initially for desktop compound requests. */
    if(!negotiated && n<4) n=4;
    if(n>cfg.max_credits-credit_count) n=cfg.max_credits-credit_count;
    credit_count+=n;
    return n;
}

static void
error_body(status)
u32 status;
{
    memset(response+64,0,9);
    put16(response+64,9);
    response_len=73;
    put32(response+8,status);
    read_fd=-1; read_length=0;
}

static u32
negotiate()
{
    unsigned n,i,t;
    int found;
    if(negotiated || request_len<100 || get16(request+64)!=36) return ST_INVALID;
    n=get16(request+66);
    if(n==0 || n>(request_len-100)/2) return ST_INVALID;
    found=0;
    for(i=0;i<n;i++) if(get16(request+100+i*2)==0x202) found=1;
    if(!found) return ST_NOT_SUPPORTED;
    auth.signing_required=!cfg.guest || (get16(request+68)&2)!=0;
    put16(response+64,65);
    put16(response+66,cfg.guest?1:3);
    put16(response+68,0x202);
    memcpy(response+72,server_guid,16);
    put32(response+92,SMBD_TRANSFER);
    put32(response+96,SMBD_TRANSFER);
    put32(response+100,SMBD_TRANSFER);
    filetime(response+104,(long)time((time_t *)0));
    t=auth_negotiate(response+128);
    put16(response+120,128); put16(response+122,t);
    response_len=128+t;
    negotiated=1;
    return ST_OK;
}

static u32
session_setup()
{
    unsigned off,len,out;
    u32 status;
    if(!negotiated || request_len<88) return ST_INVALID;
    if(get16(request+64)!=25 || request[66]!=0 || auth.authenticated) {
        if(cfg.verbose) fprintf(stderr,"smbd: setup rejected size=%u flags=%u phase=%d previous=%d\n",
            get16(request+64),(unsigned)request[66],auth.phase,
            (get32(request+80)!=0 || get32(request+84)!=0));
        return ST_INVALID;
    }
    if((!auth.phase && (get32(request+40) || get32(request+44))) ||
       (auth.phase && (get32(request+40)!=session_lo || get32(request+44)!=session_hi)))
        return 0xc0000203UL;
    off=get16(request+76); len=get16(request+78);
    if(off<88 || len>4096 || !bounds(off,len,request_len)) return ST_INVALID;
    if(request[67]&2) auth.signing_required=1;
    if(cfg.guest && auth.signing_required) return ST_DENIED;
    status=auth_session(request+off,len,response+72,&out);
    if(status!=ST_OK && status!=ST_MORE_AUTH) return status;
    if(status==ST_OK && session_establish(session_lo,session_hi,
       get32(request+80),get32(request+84))) {
        auth.authenticated=0; auth.phase=3; memset(auth.key,0,16);
        return ST_DENIED;
    }
    put64(response+40,session_lo,session_hi);
    put16(response+64,9);
    put16(response+66,auth.guest?1:0);
    put16(response+68,72); put16(response+70,out);
    response_len=72+out;
    return status;
}

static int
same_name(a,b)
const char *a,*b;
{
    unsigned x,y;
    while(*a && *b) {
        x=(unsigned char)*a++; y=(unsigned char)*b++;
        if(x>='A' && x<='Z') x+=32;
        if(y>='A' && y<='Z') y+=32;
        if(x!=y) return 0;
    }
    return !*a && !*b;
}

static u32
tree_connect()
{
    char path[SMBD_PATH],*p,*share;
    unsigned off,len,i;
    int pipe;
    if(request_len<72 || get16(request+64)!=9) return ST_INVALID;
    off=get16(request+68); len=get16(request+70);
    if(off<72 || !bounds(off,len,request_len) || utf16_decode(path,sizeof(path),request+off,len))
        return ST_INVALID;
    if(path[0]!='\\' || path[1]!='\\') return 0xc00000ccUL;
    p=path+2;
    while(*p && *p!='\\') p++;
    if(!*p) return 0xc00000ccUL;
    share=++p;
    while(*p && *p!='\\') p++;
    pipe=same_name(share,"IPC$");
    if(*p || (!pipe && !same_name(share,cfg.share))) return 0xc00000ccUL;
    for(i=0;i<8;i++) if(!tree_ids[i]) break;
    if(i==8) return ST_NO_MEMORY;
    next_tree=(next_tree+1)&MASK32;
    if(!next_tree) next_tree=1;
    tree_ids[i]=next_tree;
    tree_pipe[i]=pipe;
    put32(response+36,next_tree);
    put16(response+64,16); response[66]=pipe?2:1;
    put32(response+68,pipe?0UL:0x30UL); /* manual caching disabled */
    put32(response+76,pipe?0x0012019fUL:cfg.writable?0x001301ffUL:0x001200a9UL);
    response_len=80;
    return ST_OK;
}

static u32
dispatch(cmd)
unsigned cmd;
{
    unsigned i;
    u32 tree;
    if(cmd==0) return negotiate();
    if(cmd==1) return session_setup();
    if(!auth.authenticated || get32(request+40)!=session_lo || get32(request+44)!=session_hi)
        return 0xc0000203UL;
    if(cmd==2 || cmd==13) {
        if(request_len<68 || get16(request+64)!=4) return ST_INVALID;
        if(cmd==2) { fs_close_all(); rpc_close_all(); memset(tree_ids,0,sizeof(tree_ids)); }
        put16(response+64,4); response_len=68;
        return ST_OK;
    }
    if(cmd==3) return tree_connect();
    tree=get32(request+36);
    for(i=0;i<8;i++) if(tree_ids[i] && tree_ids[i]==tree) break;
    if(i==8) return 0xc00000c9UL;
    if(cmd==4) {
        if(request_len<68 || get16(request+64)!=4) return ST_INVALID;
        if(tree_pipe[i]) rpc_close_tree(tree); else fs_close_tree(tree);
        tree_ids[i]=0;
        put16(response+64,4); response_len=68; return ST_OK;
    }
    if(tree_pipe[i]) return rpc_dispatch(cmd);
    if(cmd==11) { /* No disk FSCTL extensions in this SMB 2.0.2 service. */
        return ST_NOT_SUPPORTED;
    }
    if(cmd==15) return ST_NOT_SUPPORTED; /* change notification */
    return fs_dispatch(cmd);
}

/* Validate the entire chain before executing any operation. */
static int
validate_chain(f, total)
FILE *f;
long total;
{
    long pos,next;
    unsigned count;
    u8 h[64];
    pos=0; count=0;
    while(pos<total) {
        if(++count>SMBD_COMPOUNDS || total-pos<64 || fseek(f,pos,0) || fread(h,1,64,f)!=64)
            return -1;
        if(memcmp(h,"\376SMB",4) || get16(h+4)!=64 || (get32(h+16)&3)) return -1;
        next=(long)get32(h+20);
        if(!next) return 0;
        if(next<64 || (next&7) || next>total-pos-64) return -1;
        pos+=next;
    }
    return -1;
}

static int
process_frame(in, out, total)
FILE *in,*out;
long total;
{
    long pos,len,next,start,outlen,pad,left;
    u32 status,previous,flags,prev_tree,read_status;
    unsigned cmd,grant,n;
    int idx,sign,logged_off;
    u8 digest[32],prev_session[8];
    if(validate_chain(in,total)) {
        if(cfg.verbose) {
            rewind(in); fread(io_buffer,1,4,in);
            fprintf(stderr,"smbd: rejected frame length=%ld magic=%02x%02x%02x%02x\n",total,
                io_buffer[0],io_buffer[1],io_buffer[2],io_buffer[3]);
        }
        return -1;
    }
    rewind(out); pos=0; previous=ST_OK; idx=0;
    memset(prev_session,0,8); prev_tree=0;
    memset(related_file,0,16);
    while(pos<total) {
        if(fseek(in,pos,0) || fread(request,1,64,in)!=64) return -1;
        next=(long)get32(request+20); len=next?next:total-pos;
        request_len=len>SMBD_BUFSIZE?SMBD_BUFSIZE:(unsigned)len;
        if(fread(request+64,1,request_len-64,in)!=request_len-64) return -1;
        cmd=get16(request+12); flags=get32(request+16);
        if(cmd==12) { /* Synchronous operations have completed: CANCEL has no response. */
            if(next || idx) return -1;
            return 0;
        }
        if(credit_take()) {
            if(cfg.verbose) fprintf(stderr,"smbd: invalid credit cmd=%u mid=%lu window=%lu+%u\n",
                cmd,get32(request+24),credit_lo,credit_count);
            return -1;
        }
        grant=credit_grant();
        status=ST_OK;
        if((flags&8) && (!auth.authenticated || auth.guest ||
           spool_hash(in,pos,len,digest) || !constant_equal(digest,request+48,16))) {
            if(cfg.verbose) fprintf(stderr,"smbd: invalid signature cmd=%u bytes=%ld\n",cmd,len);
            return -1;
        }
        if(auth.authenticated && auth.signing_required && !(flags&8)) status=ST_DENIED;
        if(flags&4) {
            if(!idx) status=ST_INVALID;
            else {
                memcpy(request+40,prev_session,8); put32(request+36,prev_tree);
                if(previous!=ST_OK) status=previous;
            }
        } else memset(related_file,0,16);
        memset(response,0,sizeof(response));
        memcpy(response,"\376SMB",4); put16(response+4,64);
        put16(response+12,cmd); put16(response+14,grant);
        put32(response+16,1|(flags&4));
        memcpy(response+24,request+24,8);
        memcpy(response+32,request+32,16);
        response_len=64; read_fd=-1; read_length=0;
        request_file=in; request_start=pos; request_size=len;
        if(status==ST_OK && len>SMBD_BUFSIZE && cmd!=9) status=ST_INVALID;
        if(status==ST_OK) status=dispatch(cmd);
        if(status!=ST_OK && status!=ST_MORE_AUTH && !(status==ST_OVERFLOW && response_len>64))
            error_body(status);
        else put32(response+8,status);
        if(cfg.verbose) fprintf(stderr,"smbd[%ld] cmd=%u mid=%lu:%lu status=%08lx bytes=%u+%ld\n",
            (long)getpid(),cmd,get32(request+28),get32(request+24),status,response_len,read_length);
        previous=status; memcpy(prev_session,response+40,8); prev_tree=get32(response+36);
        sign=auth.authenticated && !auth.guest && ((flags&8) || (cmd==1 && status==ST_OK));
        logged_off=cmd==2 && status==ST_OK;
        if(sign) put32(response+16,get32(response+16)|8);
        start=ftell(out);
        if(start<0 || fwrite(response,1,response_len,out)!=response_len) goto read_error;
        if(read_fd>=0 && read_length) {
            if(lseek(read_fd,(off_t)read_offset,0)<0) goto read_error;
            left=read_length;
            while(left>0) {
                int got;
                n=left>(long)sizeof(io_buffer)?sizeof(io_buffer):(unsigned)left;
                got=read(read_fd,io_buffer,n);
                if(got<0 && errno==EINTR) continue;
                if(got<0) goto read_error;
                if(!got) break;
                if(fwrite(io_buffer,1,got,out)!=(unsigned)got) goto read_error;
                left-=got;
            }
            read_length-=left;
            put32(response+68,(u32)read_length);
        }
        read_status=fs_read_complete();
        if(read_status) {
            error_body(read_status); previous=read_status;
            if(fseek(out,start+(long)response_len,0)) return -1;
        }
        outlen=(long)response_len+read_length;
        pad=next?((8-(outlen&7))&7):0;
        memset(io_buffer,0,8);
        if(pad && fwrite(io_buffer,1,(unsigned)pad,out)!=(unsigned)pad) return -1;
        outlen+=pad;
        put32(response+20,next?(u32)outlen:0UL);
        if(fseek(out,start,0) || fwrite(response,1,response_len,out)!=response_len || fflush(out)) return -1;
        if(sign) {
            if(spool_hash(out,start,outlen,digest) || fseek(out,start+48,0) || fwrite(digest,1,16,out)!=16)
                return -1;
        }
        if(fseek(out,start+outlen,0)) return -1;
        if(logged_off) { session_end(); auth.authenticated=0; memset(auth.key,0,16); }
        pos+=len; idx++;
    }
    return 0;
read_error:
    fs_read_complete(); return -1;
}

/* Desktop clients may open with the legacy multi-protocol NEGOTIATE
 * envelope. This accepts only the SMB2 offer; no SMB1 session or file
 * operation is implemented (MS-SMB2 3.3.5.3.2). */
static long
bootstrap(in,total)
FILE *in;
long total;
{
    unsigned pos,end,n;
    int found;
    if(negotiated || total<35 || total>SMBD_BUFSIZE) return total;
    rewind(in);
    if(fread(request,1,(unsigned)total,in)!=(unsigned)total) return -1;
    if(memcmp(request,"\377SMB",4)) return total;
    if(request[4]!=0x72 || request[32]!=0) return -1;
    n=get16(request+33);
    if(n!=(unsigned)total-35) return -1;
    pos=35; end=(unsigned)total; found=0;
    while(pos<end) {
        if(request[pos++]!=2) return -1;
        n=pos;
        while(pos<end && request[pos]) pos++;
        if(pos==end) return -1;
        if(pos-n==9 && !memcmp(request+n,"SMB 2.002",9)) found=1;
        pos++;
    }
    if(!found) return -1;
    memset(request,0,102); memcpy(request,"\376SMB",4);
    put16(request+4,64); put16(request+14,4);
    put16(request+64,36); put16(request+66,1);
    put16(request+68,1); put16(request+100,0x202);
    rewind(in);
    if(fwrite(request,1,102,in)!=102 || fflush(in)) return -1;
    return 102L;
}

static void
serve(fd, in, out, ahead)
int fd;
FILE *in,*out,*ahead;
{
    u8 header[4];
    long total,left,size;
    unsigned n;
    transport_init(fd,ahead);
    fs_init(); rpc_init();
    credit_count=1; credit_lo=credit_hi=0;
    session_lo=get32(auth.challenge); session_hi=get32(auth.challenge+4);
    if(cfg.guest) { session_lo=(u32)(unsigned)getpid(); session_hi=1UL; }
    if(!session_lo && !session_hi) session_lo=1;
    if(session_lo==MASK32 && session_hi==MASK32) session_lo--;
    while(1) {
        /* Mounted desktops keep authenticated transports idle. Do not tear
         * down their handles just because a user pauses between operations.
         * Admission is bounded; partial frames and logins still time out. */
        alarm(auth.authenticated?0:120);
        if(transport_read(header,4)) break;
        alarm(120);
        total=((long)header[1]<<16)|((long)header[2]<<8)|header[3];
        if(cfg.verbose) fprintf(stderr,"smbd[%ld] time=%ld frame=%ld\n",(long)getpid(),(long)time(0),total);
        if(header[0]!=0 || total<64 || total>SMBD_MAXFRAME) break;
        rewind(in); left=total;
        while(left>0) {
            n=left>(long)sizeof(io_buffer)?sizeof(io_buffer):(unsigned)left;
            if(transport_read(io_buffer,n) || fwrite(io_buffer,1,n,in)!=n) goto done;
            left-=n;
        }
        if(fflush(in)) break;
        total=bootstrap(in,total);
        if(total<0 || process_frame(in,out,total)) break;
        size=ftell(out);
        if(cfg.verbose) fprintf(stderr,"smbd[%ld] time=%ld send=%ld\n",(long)getpid(),(long)time(0),size);
        if(size<0 || size>SMBD_MAXREPLY) break;
        if(!size) continue;
        header[0]=0; header[1]=size>>16; header[2]=size>>8; header[3]=size;
        if(full_write(fd,header,4) || fseek(out,0L,0)) break;
        left=size;
        while(left>0) {
            n=left>(long)sizeof(io_buffer)?sizeof(io_buffer):(unsigned)left;
            if(fread(io_buffer,1,n,out)!=n || full_write(fd,io_buffer,n)) goto done;
            left-=n;
        }
        /* Failed NTLM authentication consumes this connection's challenge.
         * Send the failure first, then release its worker for a fresh login. */
        if(auth.phase==3) break;
        /* Clear spool positions, including CANCEL's empty response. */
        rewind(out);
    }
done:
    alarm(0); fs_close_all(); rpc_close_all(); session_end();
    fclose(in); fclose(out); fclose(ahead); close(fd);
}

static void
reap_children()
{
    int pid,i;
#ifdef PDP11
    union wait status;
#else
    int status;
#endif
    if(sessions_lock()) { stopped=1; return; }
    while((pid=wait3(&status,WNOHANG,(struct rusage *)0))>0) {
        if(cfg.verbose) fprintf(stderr,"smbd: worker %d exited status=%x\n",pid,
#ifdef PDP11
            status.w_status
#else
            status
#endif
            );
        fs_reap(pid);
        if(sessions_reaped(pid)) stopped=1;
        for(i=0;i<16;i++) if(children[i]==pid) { children[i]=0; child_count--; break; }
    }
    sessions_unlock();
}

static void
make_guid()
{
    struct hashctx h;
    char host[128];
    memset(host,0,sizeof(host));
    if(gethostname(host,sizeof(host)-1)) strcpy(host,"pdp11");
    hash_init(&h,5);
    hash_update(&h,5,(u8 *)host,(unsigned)strlen(host));
    hash_update(&h,5,(u8 *)cfg.root,(unsigned)strlen(cfg.root));
    hash_update(&h,5,(u8 *)cfg.share,(unsigned)strlen(cfg.share));
    hash_final(&h,5,server_guid);
}

static void
usage()
{
    fprintf(stderr,"usage: smbd -r directory [-s share] [-u smbuser -P password-file | -H hash-file | -g]\n"
        "            [-a IPv4-address] [-p port] [-c connections] [-C credits] [-U unixuser]\n"
        "            [-R random-pool] [-T temporary-directory] [-M metadata-file] [-w] [-v]\n");
    exit(2);
}

int
main(argc,argv)
int argc;
char **argv;
{
    int i,listenfd,fd,pid,one,child_state,child_meta,child_session;
    struct stat st;
    struct sockaddr_in address;
    struct passwd *pw;
    struct timeval timeout;
    fd_set readers;
    char *unixuser;
    FILE *in,*out,*ahead;
    long number;
    char *end;
    memset(&cfg,0,sizeof(cfg));
    cfg.share="pdp"; cfg.user="pdp"; cfg.port=445; cfg.max_connections=4;
    cfg.max_credits=32;
    cfg.bind_address="0.0.0.0"; unixuser="nobody";
    for(i=1;i<argc;i++) {
        if(!strcmp(argv[i],"-g")) cfg.guest=1;
        else if(!strcmp(argv[i],"-v")) cfg.verbose=1;
        else if(!strcmp(argv[i],"-w")) cfg.writable=1;
        else {
            if(strlen(argv[i])!=2 || argv[i][0]!='-' || i+1>=argc) usage();
            switch(argv[i++][1]) {
            case 'r': cfg.root=argv[i]; break;
            case 's': cfg.share=argv[i]; break;
            case 'u': cfg.user=argv[i]; break;
            case 'P': cfg.password_file=argv[i]; break;
            case 'H': cfg.hash_file=argv[i]; break;
            case 'R': cfg.random_file=argv[i]; break;
            case 'T': temporary_directory=argv[i]; break;
            case 'M': cfg.metadata_file=argv[i]; break;
            case 'a': cfg.bind_address=argv[i]; break;
            case 'p':
                number=strtol(argv[i],&end,10);
                if(!*argv[i] || *end || number<1 || number>65535L) usage();
                cfg.port=(unsigned)number; break;
            case 'c':
                number=strtol(argv[i],&end,10);
                if(!*argv[i] || *end || number<1 || number>16) usage();
                cfg.max_connections=(int)number; break;
            case 'C':
                number=strtol(argv[i],&end,10);
                if(!*argv[i] || *end || number<8 || number>32) usage();
                cfg.max_credits=(int)number; break;
            case 'U': unixuser=argv[i]; break;
            default: usage();
            }
        }
    }
    if(!cfg.root || !*cfg.share || strlen(cfg.share)>80 || same_name(cfg.share,"IPC$") ||
        temporary_directory[0]!='/' || strlen(temporary_directory)>SMBD_PATH-32 ||
        cfg.max_connections<1 || cfg.max_connections>16 ||
        (!!cfg.guest + !!cfg.password_file + !!cfg.hash_file != 1)) usage();
    for(i=0;cfg.share[i];i++) {
        unsigned c;
        c=(unsigned char)cfg.share[i];
        if(c<32 || c>126 || strchr("/\\:;|=,+*?<>\"",c)) usage();
    }
    worker_uid=getuid(); worker_gid=getgid();
    if(geteuid()==0) {
        pw=getpwnam(unixuser);
        if(!pw || pw->pw_uid==0) { fprintf(stderr,"smbd: choose a non-root Unix account with -U\n"); return 1; }
        worker_uid=pw->pw_uid; worker_gid=pw->pw_gid;
    }
#ifdef PDP11
    if(geteuid()!=0) { fprintf(stderr,"smbd: native server requires root for chroot confinement\n"); return 1; }
#endif
    if(!auth_load() || !auth_random_init()) {
        fprintf(stderr,"smbd: credential or random source initialization failed\n"); return 1;
    }
    make_guid();
    /* Resolve the export before creating the listener; each child inherits cwd. */
    if(chdir(cfg.root)) { perror("share"); return 1; }
    cfg.root=".";
    if(stat(".",&st) || metadata_open(&st)) { perror("metadata"); return 1; }
    listenfd=socket(AF_INET,SOCK_STREAM,0);
    if(listenfd<0) { perror("socket"); return 1; }
    one=1; setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one));
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET;
    address.sin_port=htons((unsigned short)cfg.port);
    address.sin_addr.s_addr=inet_addr(cfg.bind_address);
    if(address.sin_addr.s_addr==0xffffffffUL || bind(listenfd,(struct sockaddr *)&address,sizeof(address)) ||
        listen(listenfd,4)) { perror("listen"); return 1; }
    strcpy(state_path,temporary_directory); strcat(state_path,"/smbd-state.XXXXXX");
    fs_state_fd=mkstemp(state_path);
    if(fs_state_fd<0 || fstat(fs_state_fd,&state_stat) || fchmod(fs_state_fd,0600)) {
        perror("share state");
        if(fs_state_fd>=0) { close(fs_state_fd); unlink(state_path); }
        close(listenfd); return 1;
    }
    if(sessions_init(temporary_directory)) {
        perror("sessions"); close(fs_state_fd); unlink(state_path); close(listenfd); return 1;
    }
    signal(SIGPIPE,SIG_IGN); signal(SIGINT,stop_server); signal(SIGTERM,stop_server);
    fprintf(stderr,"smbd: SMB 2.0.2 %s share %s on %s:%u; %d workers, %s\n",
        cfg.writable?"read/write":"read-only",cfg.share,cfg.bind_address,cfg.port,
        cfg.max_connections,cfg.guest?"LAB GUEST":"NTLMv2 + signing");
    while(!stopped) {
        reap_children();
        FD_ZERO(&readers); FD_SET(listenfd,&readers); timeout.tv_sec=1; timeout.tv_usec=0;
        i=select(listenfd+1,&readers,(fd_set *)0,(fd_set *)0,&timeout);
        if(i<0) { if(errno==EINTR) continue; break; }
        if(!i) continue;
        fd=accept(listenfd,(struct sockaddr *)0,0);
        if(fd<0) continue;
        if(child_count>=cfg.max_connections) { close(fd); continue; }
        memset(&auth,0,sizeof(auth));
        if(!auth_random_challenge()) { fprintf(stderr,"smbd: random source exhausted or unavailable\n"); close(fd); continue; }
        in=private_spool(); out=private_spool(); ahead=private_spool();
        if(!in || !out || !ahead) {
            if(in) fclose(in); if(out) fclose(out); if(ahead) fclose(ahead);
            close(fd); continue;
        }
        /* flock locks belong to open descriptions. A dup/inherited parent
         * description would let workers pass each other's locks. */
        child_state=open(state_path,O_RDWR);
        if(child_state<0 || fstat(child_state,&st) ||
           st.st_dev!=state_stat.st_dev || st.st_ino!=state_stat.st_ino ||
           st.st_nlink!=1 || (st.st_mode&077)!=0) {
            if(child_state>=0) close(child_state);
            fclose(in); fclose(out); fclose(ahead); close(fd); continue;
        }
        child_meta=cfg.metadata_file?metadata_reopen():-1;
        if(cfg.metadata_file && child_meta<0) {
            close(child_state); fclose(in); fclose(out); fclose(ahead); close(fd); continue;
        }
        child_session=sessions_open();
        if(child_session<0) {
            close(child_state); if(child_meta>=0) close(child_meta);
            fclose(in); fclose(out); fclose(ahead); close(fd); continue;
        }
        pid=fork();
        if(pid==0) {
            sessions_child(child_session,fd);
            close(fs_state_fd); fs_state_fd=child_state;
            if(fs_meta_fd>=0) close(fs_meta_fd);
            fs_meta_fd=child_meta;
            auth_random_close();
            close(listenfd); signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL);
            if(geteuid()==0 && (chroot(".") || chdir("/") || setgroups(0,(gid_t *)0) ||
                setgid(worker_gid) || setuid(worker_uid))) { perror("confinement"); _exit(1); }
            serve(fd,in,out,ahead); _exit(0);
        }
        close(child_session);
        close(child_state); if(child_meta>=0) close(child_meta);
        fclose(in); fclose(out); fclose(ahead); close(fd);
        if(pid<0) { perror("fork"); continue; }
        for(i=0;i<16;i++) if(!children[i]) { children[i]=pid; child_count++; break; }
    }
    close(listenfd);
    for(i=0;i<16;i++) if(children[i]) kill(children[i],SIGTERM);
    if(!sessions_lock()) {
        while((pid=wait((int *)0))>0 || errno==EINTR) if(pid>0) {
            fs_reap(pid); sessions_reaped(pid);
        }
        sessions_unlock();
    }
    sessions_destroy();
    close(fs_state_fd);
    if(fs_meta_fd>=0) close(fs_meta_fd);
    if(!lstat(state_path,&st) && st.st_dev==state_stat.st_dev && st.st_ino==state_stat.st_ino)
        unlink(state_path);
    memset(cfg.nthash,0,16);
    return 0;
}

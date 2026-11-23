/* Bounded, explicit SMB-to-Unix mappings. Read before chroot/fork; never
 * consult passwd files or reopen credentials inside the export. */
#include "smbd.h"
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>

#define MAX_ACCOUNTS 8
struct account {
 char name[65];
 u8 hash[16];
 uid_t uid;
 gid_t gid;
};
static struct account accounts[MAX_ACCOUNTS];
static unsigned account_count;

static void
erase(buffer,n)
void *buffer;
unsigned n;
{
 volatile u8 *p;
 p=(volatile u8 *)buffer;
 while(n--) *p++=0;
}

void
accounts_clear()
{
 erase(accounts,sizeof(accounts));
 account_count=0;
}

static unsigned
fold(c)
unsigned c;
{
 return c>='a' && c<='z'?c-'a'+'A':c;
}

static int
hex(c)
unsigned c;
{
 if(c>='0' && c<='9') return c-'0';
 c=fold(c);
 return c>='A' && c<='F'?(int)(c-'A'+10):-1;
}

unsigned
accounts_find(user,n)
const u8 *user;
unsigned n;
{
 unsigned a,i;
 if((n&1) || !n || n>128) return 0;
 for(a=0;a<account_count;a++) {
  if(strlen(accounts[a].name)!=n/2) continue;
  for(i=0;i<n;i+=2)
   if(user[i+1] || fold(user[i])!=fold((u8)accounts[a].name[i/2])) break;
  if(i==n) return a+1;
 }
 return 0;
}

const u8 *
accounts_hash(a)
unsigned a;
{
 return a && a<=account_count?accounts[a-1].hash:(const u8 *)0;
}

static int
record(line)
char *line;
{
 char *unixname,*hash,*p;
 struct passwd *pw;
 struct account *a;
 unsigned i,n,j;
 int high,low;
 if(account_count==MAX_ACCOUNTS) return 0;
 unixname=strchr(line,':');
 if(!unixname) return 0;
 *unixname++=0; hash=strchr(unixname,':');
 if(!hash) return 0;
 *hash++=0;
 n=(unsigned)strlen(line);
 if(!n || n>64 || !*unixname || strlen(unixname)>64 || strlen(hash)!=32) return 0;
 for(p=line;*p;p++) if((u8)*p<33 || (u8)*p>126) return 0;
 for(p=unixname;*p;p++) if((u8)*p<33 || (u8)*p>126) return 0;
 for(i=0;i<account_count;i++) {
  if(strlen(accounts[i].name)!=n) continue;
  for(j=0;j<n && fold((u8)line[j])==fold((u8)accounts[i].name[j]);j++) ;
  if(j==n) return 0;
 }
 pw=getpwnam(unixname);
 if(!pw || (geteuid()!=0 && (pw->pw_uid!=getuid() || pw->pw_gid!=getgid()))) return 0;
 a=accounts+account_count;
 strcpy(a->name,line); a->uid=pw->pw_uid; a->gid=pw->pw_gid;
 for(i=0;i<16;i++) {
  high=hex((u8)hash[i*2]); low=hex((u8)hash[i*2+1]);
  if(high<0 || low<0) return 0;
  a->hash[i]=(u8)((high<<4)|low);
 }
 account_count++;
 return 1;
}

int
accounts_load()
{
 struct stat before,after;
 FILE *f;
 int fd,c,ok;
 unsigned n;
 char line[164]; /* 64-byte SMB name, 64-byte Unix name, separators, hash. */
 accounts_clear();
 if(!cfg.accounts_file || cfg.accounts_file[0]!='/' ||
    lstat(cfg.accounts_file,&before) || (before.st_mode&S_IFMT)!=S_IFREG ||
    (before.st_mode&0777)!=0600 || before.st_uid!=geteuid() || before.st_nlink!=1) return 0;
 fd=open(cfg.accounts_file,O_RDONLY);
 if(fd<0) return 0;
 if(fstat(fd,&after) || before.st_dev!=after.st_dev || before.st_ino!=after.st_ino ||
    (after.st_mode&S_IFMT)!=S_IFREG || (after.st_mode&0777)!=0600 ||
    after.st_uid!=geteuid() || after.st_nlink!=1) { close(fd); return 0; }
 f=fdopen(fd,"r");
 if(!f) { close(fd); return 0; }
 /* A freed stdio read buffer would otherwise leave every user's hash in
  * the listener's heap and in all subsequently forked workers. */
 setbuf(f,(char *)0);
 n=0; ok=1;
 while((c=getc(f))!=EOF) {
  if(c=='\n') {
   if(n && line[n-1]=='\r') n--;
   line[n]=0;
   if(!record(line)) { ok=0; break; }
   n=0;
  } else {
   if(c<33 || c>126 || n>=sizeof(line)-1) {
    /* CR is accepted only as the final byte of a CRLF line. */
    if(c!='\r' || n>=sizeof(line)-1) { ok=0; break; }
   }
   line[n++]=(char)c;
  }
 }
 if(ferror(f) || n || !account_count) ok=0;
 fclose(f); erase(line,sizeof(line));
 if(!ok) accounts_clear();
 return ok;
}

int
accounts_apply(a)
unsigned a;
{
 uid_t uid;
 gid_t gid;
 int ok;
 if(!a || a>account_count) return -1;
 uid=accounts[a-1].uid; gid=accounts[a-1].gid;
 ok=0;
 if(geteuid()==0) {
  if(setgroups(0,(gid_t *)0) || setgid(gid) || setuid(uid)) ok=-1;
 } else if(getuid()!=uid || getgid()!=gid) ok=-1;
 accounts_clear(); /* Other users' credential hashes must not survive in a worker. */
 return ok;
}

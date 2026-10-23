/* Host-only account-file bounds, ownership and identity-selection checks. */
#include "../smbd.h"
#include <pwd.h>
#include <fcntl.h>

struct config cfg;
static char work[]="/tmp/smbd-accounts-test.XXXXXX";
static char path[256],alias[256],valid[256];
static int checks;

static void
fail(line)
int line;
{
 fprintf(stderr,"accounts test line %d failed, errno=%d\n",line,errno);
 unlink(alias); unlink(path); rmdir(work); exit(1);
}
#define CHECK(c) do { checks++; if(!(c)) fail(__LINE__); } while(0)

static void
put(p,n)
const char *p;
unsigned n;
{
 int fd;
 fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); CHECK(fd>=0);
 CHECK(write(fd,p,n)==(int)n); CHECK(close(fd)==0); CHECK(chmod(path,0600)==0);
}

static unsigned
lookup(name)
const char *name;
{
 u8 wide[128];
 unsigned i,n;
 n=(unsigned)strlen(name); CHECK(n<=64);
 for(i=0;i<n;i++) { wide[i*2]=(u8)name[i]; wide[i*2+1]=0; }
 return accounts_find(wide,n*2);
}

int
main()
{
 struct passwd *pw;
 char text[2048],username[65];
 const char *hash;
 unsigned n,i;
 hash="a4f49c406510bdcab6824ee7c30fd852"; /* Disposable known-answer fixture. */
 pw=getpwuid(getuid()); CHECK(pw!=0 && strlen(pw->pw_name)<=64);
 strcpy(username,pw->pw_name);
 CHECK(mkdtemp(work)!=(char *)0);
 sprintf(path,"%s/accounts",work); sprintf(alias,"%s/alias",work);
 cfg.accounts_file=path;
 sprintf(valid,"User:%s:%s\n",username,hash);
 put(valid,(unsigned)strlen(valid)); CHECK(accounts_load()==1);
 CHECK(lookup("uSeR")==1 && lookup("absent")==0);
 CHECK(accounts_hash(1)!=0 && accounts_hash(0)==0 && accounts_hash(2)==0);
 CHECK(accounts_apply(0)==-1 && accounts_apply(2)==-1);
 CHECK(accounts_apply(1)==0 && lookup("User")==0 && accounts_hash(1)==0);
 CHECK(accounts_load()==1);

 CHECK(chmod(path,0644)==0); CHECK(accounts_load()==0 && lookup("User")==0);
 CHECK(chmod(path,0600)==0); CHECK(accounts_load()==1);
 CHECK(link(path,alias)==0); CHECK(accounts_load()==0); CHECK(unlink(alias)==0);
 CHECK(symlink(path,alias)==0); cfg.accounts_file=alias;
 CHECK(accounts_load()==0); cfg.accounts_file=path; CHECK(unlink(alias)==0);
 cfg.accounts_file="relative"; CHECK(accounts_load()==0); cfg.accounts_file=path;

 put("",0); CHECK(accounts_load()==0);
 n=(unsigned)strlen(valid);
 put(valid,n-1); CHECK(accounts_load()==0); /* Truncated last record. */
 memcpy(text,valid,n); text[2]=0;
 put(text,n); CHECK(accounts_load()==0); /* Embedded NUL. */
 sprintf(text,"User:%s:%s\r\n",username,hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==1);
 sprintf(text,"User:%s:%s\nuser:%s:%s\n",username,hash,username,hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0 && lookup("User")==0);
 sprintf(text,"User:__smbd_absent_user_93761:%s\n",hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0);
 sprintf(text,"User:%s:%sZ\n",username,hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0);
 sprintf(text,"User:%s:Z4f49c406510bdcab6824ee7c30fd852\n",username);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0);
 sprintf(text,"User:%s:%s\n\n",username,hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0);
 memset(text,'A',65); sprintf(text+65,":%s:%s\n",username,hash);
 put(text,(unsigned)strlen(text)); CHECK(accounts_load()==0);
 n=0;
 for(i=0;i<8;i++) n+=(unsigned)sprintf(text+n,"user%u:%s:%s\n",i,username,hash);
 put(text,n); CHECK(accounts_load()==1 && lookup("user7")==8);
 n+=(unsigned)sprintf(text+n,"ninth:%s:%s\n",username,hash);
 put(text,n); CHECK(accounts_load()==0 && lookup("user0")==0);
 accounts_clear(); CHECK(unlink(path)==0 && rmdir(work)==0);
 printf("PASS account-file validation, eight-account bound, mapping and worker credential erasure (%d checks)\n",checks);
 return 0;
}

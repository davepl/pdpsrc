/* Interactive single-account NT-hash administration for smbd -H.
 * No password is accepted on argv or printed. Native getpass historically
 * truncates to eight bytes, so use a bounded no-echo terminal reader. */
#include "smbd.h"
#include <fcntl.h>
#include <signal.h>
#ifdef PDP11
#include <sgtty.h>
#else
#include <termios.h>
#endif

static int prompt_fd = -1;
#ifdef PDP11
static struct sgttyb saved_tty;
#else
static struct termios saved_tty;
#endif

static void erase(p,n)
void *p;
unsigned n;
{
 volatile u8 *q;
 q=(volatile u8 *)p;
 while(n--) *q++=0;
}
static void restore_tty()
{
 if(prompt_fd<0) return;
#ifdef PDP11
 ioctl(prompt_fd,TIOCSETP,&saved_tty);
#else
 tcsetattr(prompt_fd,TCSANOW,&saved_tty);
#endif
}
static void interrupted(sig)
int sig;
{
 restore_tty();
 if(prompt_fd>=0) write(prompt_fd,"\n",1);
 _exit(128+sig);
}
static int password_hash(prompt,out)
const char *prompt;
u8 *out;
{
 char password[257],ch;
 unsigned n,i;
 int r,ok,overflow;
 u8 pair[2];
 struct hashctx h;

#ifdef PDP11
 int (*oldint)(),(*oldterm)();
 struct sgttyb quiet;
#else
 void (*oldint)(),(*oldterm)();
 struct termios quiet;
#endif
 prompt_fd=open("/dev/tty",O_RDWR);
 if(prompt_fd<0) { fprintf(stderr,"smbpwd: a terminal is required\n"); return 0; }
#ifdef PDP11
 if(ioctl(prompt_fd,TIOCGETP,&saved_tty)<0) goto terminal_failed;
 quiet=saved_tty; quiet.sg_flags &= ~ECHO; quiet.sg_flags |= CBREAK;
 if(ioctl(prompt_fd,TIOCSETP,&quiet)<0) goto terminal_failed;
#else
 if(tcgetattr(prompt_fd,&saved_tty)<0) goto terminal_failed;
 quiet=saved_tty; quiet.c_lflag &= ~(ECHO|ECHONL|ICANON);
 quiet.c_cc[VMIN]=1; quiet.c_cc[VTIME]=0;
 if(tcsetattr(prompt_fd,TCSANOW,&quiet)<0) goto terminal_failed;
#endif
 oldint=signal(SIGINT,interrupted); oldterm=signal(SIGTERM,interrupted);
 n=0; ok=1; overflow=0;
 if(write(prompt_fd,prompt,strlen(prompt))!=(int)strlen(prompt)) ok=0;
 while(ok) {
  r=read(prompt_fd,&ch,1);
  if(r<0 && errno==EINTR) continue;
  if(r!=1) { ok=0; break; }
  if(ch=='\n' || ch=='\r') break;
  if(ch==4) { ok=0; break; }
  if(ch==8 || ch==127) { if(!overflow && n) password[--n]=0; continue; }
  if(ch==21) { erase(password,sizeof(password)); n=0; overflow=0; continue; }
  if(n<256) password[n++]=ch; else overflow=1;
 }
 restore_tty(); write(prompt_fd,"\n",1); close(prompt_fd); prompt_fd=-1;
 signal(SIGINT,oldint); signal(SIGTERM,oldterm);
 if(overflow || n==0) ok=0;
 for(i=0;i<n;i++) if((u8)password[i]<32 || (u8)password[i]>126) ok=0;
 if(ok) {
  hash_init(&h,4); pair[1]=0;
  for(i=0;i<n;i++) { pair[0]=(u8)password[i]; hash_update(&h,4,pair,2); }
  hash_final(&h,4,out);
 } else fprintf(stderr,"smbpwd: password must be 1-256 printable ASCII characters\n");
 erase(password,sizeof(password)); erase(pair,sizeof(pair)); erase(&ch,1); erase(&h,sizeof(h));
 return ok;
terminal_failed:
 close(prompt_fd); prompt_fd=-1;
 fprintf(stderr,"smbpwd: cannot disable terminal echo\n"); return 0;
}
static int safe_file(st)
struct stat *st;
{
 return (st->st_mode&S_IFMT)==S_IFREG && (st->st_mode&077)==0 &&
  st->st_uid==geteuid() && st->st_nlink==1;
}
int main(argc,argv)
int argc;
char **argv;
{
 char directory[SMBD_PATH],name[SMBD_PATH],temporary[48],*slash;
 static char hex[]="0123456789abcdef";
 u8 first[16],second[16],line[33];
 struct stat parent,opened,before,after;
 int dirfd,fd,exists,r,ok;
 unsigned i,n;
 if(argc!=2 || getuid()!=geteuid()) {
  fprintf(stderr,"usage: smbpwd hash-file (run as its owner, not setuid)\n"); return 2;
 }
 n=(unsigned)strlen(argv[1]);
 if(!n || n>=sizeof(directory) || argv[1][n-1]=='/') {
  fprintf(stderr,"smbpwd: invalid destination path\n"); return 1;
 }
 strcpy(directory,argv[1]); slash=strrchr(directory,'/');
 if(slash) {
  strcpy(name,slash+1);
  if(slash==directory) directory[1]=0; else *slash=0;
 } else { strcpy(name,directory); strcpy(directory,"."); }
 if(!strcmp(name,".") || !strcmp(name,"..")) return 1;
 /* Pin a protected parent directory before using relative file names.
  * Its owner or root is the only account allowed to replace entries. */
 if(lstat(directory,&parent)<0 || (parent.st_mode&S_IFMT)!=S_IFDIR ||
    (parent.st_mode&022) || (parent.st_uid!=geteuid() && parent.st_uid!=0)) {
  fprintf(stderr,"smbpwd: destination directory must be protected from other writers\n"); return 1;
 }
 dirfd=open(directory,O_RDONLY);
 if(dirfd<0) { perror("smbpwd: directory"); return 1; }
 if(fstat(dirfd,&opened)<0 || opened.st_dev!=parent.st_dev ||
    opened.st_ino!=parent.st_ino || fchdir(dirfd)<0) {
  close(dirfd); fprintf(stderr,"smbpwd: destination directory changed\n"); return 1;
 }
 close(dirfd);
 exists=lstat(name,&before)==0;
 if((!exists && errno!=ENOENT) || (exists && !safe_file(&before))) {
  fprintf(stderr,"smbpwd: refusing unsafe existing hash file\n"); return 1;
 }
 memset(first,0,sizeof(first)); memset(second,0,sizeof(second));
 ok=password_hash("New SMB password: ",first);
 if(ok) ok=password_hash("Retype SMB password: ",second);
 if(ok && !constant_equal(first,second,16)) {
  fprintf(stderr,"smbpwd: passwords do not match\n"); ok=0;
 }
 if(!ok) { erase(first,16); erase(second,16); return 1; }
 for(i=0;i<16;i++) { line[2*i]=hex[first[i]>>4]; line[2*i+1]=hex[first[i]&15]; }
 line[32]='\n'; erase(first,16); erase(second,16);
 sprintf(temporary,".smbpwd-%ld.tmp",(long)getpid());
 fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL,0600);
 if(fd<0) { perror("smbpwd: temporary file"); erase(line,sizeof(line)); return 1; }
 ok=1; n=0;
 while(n<sizeof(line)) {
  r=write(fd,line+n,sizeof(line)-n);
  if(r<0 && errno==EINTR) continue;
  if(r<=0) { ok=0; break; }
  n+=(unsigned)r;
 }
 erase(line,sizeof(line));
 if(fchmod(fd,0600)<0 || fsync(fd)<0) ok=0;
 if(close(fd)<0) ok=0;
 /* Recheck the destination identity and metadata after prompting. */
 r=lstat(name,&after);
 if(exists) {
  if(r<0 || !safe_file(&after) || after.st_dev!=before.st_dev ||
     after.st_ino!=before.st_ino || after.st_mtime!=before.st_mtime ||
     after.st_size!=before.st_size) ok=0;
 } else if(r==0 || errno!=ENOENT) ok=0;
 if(ok && rename(temporary,name)<0) ok=0;
 if(!ok) {
  unlink(temporary); fprintf(stderr,"smbpwd: cannot safely replace hash file\n"); return 1;
 }
 printf("SMB password file updated. Restart smbd to load it.\n");
 return 0;
}

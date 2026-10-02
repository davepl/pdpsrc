/* Native 2.11BSD only. Run as root; creates a private disposable directory
 * in /usr/tmp, drops to nobody, and checks the reserved SMB metadata bit.
 * No exported file or existing inode flag is modified. */
#if !defined(pdp11) && !defined(__pdp11__)
#error This inode flag probe is only for the verified 2.11BSD target
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void die(what)
char *what;
{ perror(what); exit(1); }
int main()
{
 struct passwd *pw;
 struct stat st;
 int fd;
 unsigned short original;
 char directory[80],path[100];
 pw=getpwnam("nobody"); if(!pw) die("nobody");
 sprintf(directory,"/usr/tmp/smbd-flag-probe-%d",getpid());
 sprintf(path,"%s/probe",directory);
 if(mkdir(directory,0700)) die("mkdir");
 if(chown(directory,pw->pw_uid,pw->pw_gid)) die("chown");
 if(setgroups(0,(int *)0)||setgid(pw->pw_gid)||setuid(pw->pw_uid)) die("drop uid");
 fd=open(path,O_RDWR|O_CREAT|O_EXCL,0600); if(fd<0) die("open");
 if(fstat(fd,&st)) die("initial fstat"); original=st.st_flags;
 if(fchflags(fd,original|0x81)||fsync(fd)) die("set flags and fsync");
 close(fd); fd=open(path,O_RDONLY); if(fd<0||fstat(fd,&st)) die("reopen set");
 if((st.st_flags&0x81)!=0x81) { puts("set flag mismatch"); return(1); }
 if(fchflags(fd,st.st_flags&~0x80)||fsync(fd)) die("clear flag and fsync");
 close(fd); fd=open(path,O_RDONLY); if(fd<0||fstat(fd,&st)) die("reopen clear");
 if((st.st_flags&0x81)!=0x01) { puts("clear flag mismatch"); return(1); }
 if(fchflags(fd,original)||fsync(fd)) die("restore flags");
 close(fd); if(unlink(path)||rmdir(directory)) die("cleanup");
 printf("PASS native uid=%d flag0x80 set/fsync/reopen/clear; unrelated UF_NODUMP preserved; cleaned\n",getuid());
 return(0);
}

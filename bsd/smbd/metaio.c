/* Persistent metadata descriptor setup, before worker confinement. */
#include "smbd.h"
#include <fcntl.h>

static struct stat metadata_stat;

/* Metadata outlives a daemon run. Keep it outside the exported tree, in a
 * protected directory, and never follow symlinks while opening it as root.
 * Root-owned sticky ancestors permit /private/tmp/private-test-directory. */
int
metadata_open(share)
struct stat *share;
{
    char path[SMBD_PATH];
    struct stat before, after;
    unsigned i, last;
    int fd, exists, flags;
    if(!cfg.metadata_file) return 0;
    if(cfg.metadata_file[0]!='/' || strlen(cfg.metadata_file)>=sizeof(path)) goto bad;
    if(stat("/",&before) ||
       (before.st_dev==share->st_dev && before.st_ino==share->st_ino)) goto bad;
    strcpy(path,cfg.metadata_file); last=0;
    for(i=1;path[i];i++) if(path[i]=='/') last=i;
    if(!last || !path[last+1]) goto bad;
    for(i=1;i<=last;i++) if(path[i]=='/') {
        path[i]=0;
        if(lstat(path,&before) || !S_ISDIR(before.st_mode) ||
           (before.st_uid!=0 && before.st_uid!=geteuid()) ||
           ((before.st_mode&022) &&
            (i==last || before.st_uid!=0 || !(before.st_mode&01000))) ||
           (before.st_dev==share->st_dev && before.st_ino==share->st_ino)) goto bad;
        path[i]='/';
    }
    exists=lstat(path,&before)==0;
    if(!exists && errno!=ENOENT) return -1;
    if(exists && (!S_ISREG(before.st_mode) || before.st_uid!=geteuid() ||
       before.st_nlink!=1 || (before.st_mode&077))) goto bad;
    flags=cfg.writable?O_RDWR:O_RDONLY;
    if(!exists) {
        if(!cfg.writable) { errno=ENOENT; return -1; }
        flags|=O_CREAT|O_EXCL;
    }
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    fd=open(path,flags,0600);
    if(fd<0) return -1;
    if(fstat(fd,&after) || !S_ISREG(after.st_mode) ||
       after.st_uid!=geteuid() || after.st_nlink!=1 || (after.st_mode&077) ||
       (exists && (before.st_dev!=after.st_dev || before.st_ino!=after.st_ino))) {
        close(fd); goto bad;
    }
    if(metadata_init(fd,share,cfg.writable)<0) { close(fd); return -1; }
    fs_meta_fd=fd; metadata_stat=after;
    return 0;
bad:
    errno=EACCES; return -1;
}

int
metadata_reopen()
{
    struct stat st;
    int fd, flags;
    if(lstat(cfg.metadata_file,&st) || !S_ISREG(st.st_mode) ||
       st.st_dev!=metadata_stat.st_dev || st.st_ino!=metadata_stat.st_ino) {
        errno=EACCES; return -1;
    }
    flags=cfg.writable?O_RDWR:O_RDONLY;
#ifdef O_NOFOLLOW
    flags|=O_NOFOLLOW;
#endif
    fd=open(cfg.metadata_file,flags);
    if(fd<0) return -1;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) ||
       st.st_dev!=metadata_stat.st_dev || st.st_ino!=metadata_stat.st_ino ||
       st.st_uid!=geteuid() || st.st_nlink!=1 || (st.st_mode&077)) {
        close(fd); errno=EACCES; return -1;
    }
    return fd;
}

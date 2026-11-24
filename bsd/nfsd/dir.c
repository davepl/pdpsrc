/*
 * Reading a directory one entry at a time, with for each entry the
 * offset at which the next one starts.  That offset is the NFS cookie.
 * On 2.11BSD the directory is read as the file it is; elsewhere the
 * library does it.
 */
#include "nfsd.h"

#ifdef PDP11
#include <sys/dir.h>

static char dbuf[1024];
static int dfd, dmine, dlen, dpos;
static long dbase, dfrom;

int
dir_start(path, fd, off)
	char *path;
	int fd;
	long off;
{
	dmine = 0;
	if (fd < 0) {
		fd = open(path, O_RDONLY, 0);
		if (fd < 0)
			return -1;
		dmine = 1;
	}
	dfd = fd;
	dfrom = off;
	dbase = off & ~((long)DIRBLKSIZ - 1);
	dlen = dpos = 0;
	if (lseek(fd, dbase, 0) < 0) {
		dir_end();
		return -1;
	}
	return 0;
}

int
dir_read(d)
	struct dent *d;
{
	register struct direct *dp;
	long at;
	int n;

	for (;;) {
		if (dpos >= dlen) {
			dbase += dlen;
			n = read(dfd, dbuf, sizeof(dbuf));
			dlen = dpos = 0;
			if (n <= 0)
				return n;
			dlen = n;
		}
		dp = (struct direct *)(dbuf + dpos);
		if (dp->d_reclen == 0 || dp->d_reclen > dlen - dpos)
			return -1;
		at = dbase + dpos;
		dpos += dp->d_reclen;
		if (at < dfrom || dp->d_ino == 0)
			continue;
		d->ino = dp->d_ino;
		d->name = dp->d_name;
		d->namlen = dp->d_namlen;
		d->next = dbase + dpos;
		return 1;
	}
}

void
dir_end()
{
	if (dmine)
		(void)close(dfd);
	dmine = 0;
}

#else
#include <dirent.h>

/* Test builds: the cookie is simply the entry's position in the list. */
static DIR *dd;
static long dat, dskip;

int
dir_start(path, fd, off)
	char *path;
	int fd;
	long off;
{
	dd = opendir(path);
	if (dd == 0)
		return -1;
	dat = 0;
	dskip = off;
	return 0;
}

int
dir_read(d)
	struct dent *d;
{
	struct dirent *de;

	for (;;) {
		de = readdir(dd);
		if (de == 0)
			return 0;
		if (dat++ < dskip)
			continue;
		d->ino = (u32)de->d_ino & M32;
		d->name = de->d_name;
		d->namlen = strlen(de->d_name);
		d->next = dat;
		return 1;
	}
}

void
dir_end()
{
	if (dd)
		closedir(dd);
	dd = 0;
}
#endif

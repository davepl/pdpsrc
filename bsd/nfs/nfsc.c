/*
 * nfsc - user-mode NFS version 3 client for 2.11BSD.
 *
 *	nfsc [-v] [-u uid] [-g gid] host:/export [command [args...]]
 *
 * Without a command, reads commands from standard input.  Paths are
 * relative to the current remote directory; a leading / means the
 * export root.
 */
#include "nfs.h"
#include <signal.h>
#include <fcntl.h>
#ifndef PDP11
#include <unistd.h>
#endif

static struct nfsmount mnt;
static char cwd[NFS_PATHMAX] = "";
static u8 iobuf[NFS_IOSIZE];
static int status;

static void
fail(what, e)
	char *what;
	int e;
{
	fprintf(stderr, "nfsc: %s: %s\n", what, nfs_strerror(e));
	status = 1;
}

/* Build a normalised export-relative path from cwd and arg. */
static int
resolve(arg, out)
	char *arg, *out;
{
	char tmp[NFS_PATHMAX];
	char *p, *q, *o;
	int n;

	if (arg == 0 || *arg == 0)
		arg = ".";
	if (*arg == '/') {
		if (strlen(arg) >= sizeof(tmp))
			return ENAMETOOLONG;
		strcpy(tmp, arg);
	} else {
		if (strlen(cwd) + strlen(arg) + 2 >= sizeof(tmp))
			return ENAMETOOLONG;
		strcpy(tmp, cwd);
		strcat(tmp, "/");
		strcat(tmp, arg);
	}
	o = out;
	*o = 0;
	p = tmp;
	for (;;) {
		while (*p == '/')
			p++;
		if (*p == 0)
			break;
		q = p;
		while (*q && *q != '/')
			q++;
		n = (int)(q - p);
		if (n == 1 && p[0] == '.') {
			/* skip */
		} else if (n == 2 && p[0] == '.' && p[1] == '.') {
			while (o > out && *--o != '/')
				;
			*o = 0;
		} else {
			*o++ = '/';
			memcpy(o, p, n);
			o += n;
			*o = 0;
		}
		p = q;
	}
	return 0;
}

static char *
typechar(a)
	struct nfsattr *a;
{
	switch ((int)a->type) {
	case NF3DIR: return "d";
	case NF3LNK: return "l";
	case NF3BLK: return "b";
	case NF3CHR: return "c";
	case NF3SOCK: return "s";
	case NF3FIFO: return "p";
	}
	return "-";
}

static void
modestr(a, s)
	struct nfsattr *a;
	char *s;
{
	static char rwx[] = "rwxrwxrwx";
	int i;
	u32 bit;

	strcpy(s, typechar(a));
	for (i = 0, bit = 0400; i < 9; i++, bit >>= 1)
		s[i + 1] = (a->mode & bit) ? rwx[i] : '-';
	if (a->mode & 04000)
		s[3] = (a->mode & 0100) ? 's' : 'S';
	if (a->mode & 02000)
		s[6] = (a->mode & 010) ? 's' : 'S';
	if (a->mode & 01000)
		s[9] = (a->mode & 01) ? 't' : 'T';
	s[10] = 0;
}

static void
datestr(t, s)
	u32 t;
	char *s;
{
	time_t tt;
	char *c;

	tt = (time_t)t;
	c = ctime(&tt);
	/* "Wed Jun 30 21:49:08 1993\n" -> "Jun 30 21:49 1993" */
	sprintf(s, "%.12s %.4s", c + 4, c + 20);
}

static void
sizestr(v, s)
	struct u64 *v;
	char *s;
{
	if (v->hi)
		sprintf(s, "%lu*4G+%lu", v->hi, v->lo);
	else
		sprintf(s, "%lu", v->lo);
}

static void
longline(name, a, dirfh)
	char *name;
	struct nfsattr *a;
	struct nfsfh *dirfh;
{
	char ms[12], ds[24], ss[32], ln[NFS_PATHMAX];
	struct nfsfh fh;

	modestr(a, ms);
	datestr(a->mtime, ds);
	sizestr(&a->size, ss);
	printf("%s %3lu %5lu %5lu %9s %s %s", ms, a->nlink, a->uid, a->gid,
	    ss, ds, name);
	if (a->type == NF3LNK && dirfh &&
	    nfs_lookup(&mnt, dirfh, name, &fh, (struct nfsattr *)0) == 0 &&
	    nfs_readlink(&mnt, &fh, ln, sizeof(ln)) == 0)
		printf(" -> %s", ln);
	printf("\n");
}

struct lsarg {
	struct nfsfh *dir;
	int lflag, aflag, count;
};

static int
lsentry(arg, name, fileid, cookie)
	void *arg;
	char *name;
	struct u64 *fileid, *cookie;
{
	struct lsarg *la;
	struct nfsfh fh;
	struct nfsattr a;
	int e;

	la = (struct lsarg *)arg;
	if (name[0] == '.' && !la->aflag)
		return 0;
	la->count++;
	if (!la->lflag) {
		printf("%s\n", name);
		return 0;
	}
	if ((e = nfs_lookup(&mnt, la->dir, name, &fh, &a)) != 0) {
		fail(name, e);
		return 0;
	}
	longline(name, &a, la->dir);
	return 0;
}

static void
cmd_ls(argc, argv)
	int argc;
	char **argv;
{
	struct lsarg la;
	struct nfsfh fh;
	struct nfsattr a;
	char path[NFS_PATHMAX], *s;
	int e, i, any;

	la.lflag = la.aflag = 0;
	any = 0;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			for (s = argv[i] + 1; *s; s++) {
				if (*s == 'l')
					la.lflag = 1;
				else if (*s == 'a')
					la.aflag = 1;
			}
			continue;
		}
	}
	for (i = 1; i <= argc; i++) {
		if (i < argc && argv[i][0] == '-')
			continue;
		if (i == argc && any)
			break;
		any = 1;
		if ((e = resolve(i < argc ? argv[i] : ".", path)) != 0 ||
		    (e = nfs_walk(&mnt, path, 1, &fh, &a)) != 0) {
			fail(i < argc ? argv[i] : ".", e);
			continue;
		}
		if (a.type != NF3DIR) {
			if (la.lflag)
				longline(i < argc ? argv[i] : ".", &a,
				    (struct nfsfh *)0);
			else
				printf("%s\n", argv[i]);
			continue;
		}
		la.dir = &fh;
		la.count = 0;
		if ((e = nfs_readdir(&mnt, &fh, lsentry, (void *)&la)) != 0)
			fail("readdir", e);
	}
}

static void
cmd_cd(arg)
	char *arg;
{
	char path[NFS_PATHMAX];
	struct nfsfh fh;
	struct nfsattr a;
	int e;

	if ((e = resolve(arg ? arg : "/", path)) != 0 ||
	    (e = nfs_walk(&mnt, path, 1, &fh, &a)) != 0) {
		fail(arg, e);
		return;
	}
	if (a.type != NF3DIR) {
		fail(arg, ENOTDIR);
		return;
	}
	strcpy(cwd, path);
}

/* Copy a remote file to fd. */
static int
fetch(arg, fd)
	char *arg;
	int fd;
{
	char path[NFS_PATHMAX];
	struct nfsfh fh;
	struct nfsattr a;
	u32 off;
	unsigned got;
	int e, eof;

	if ((e = resolve(arg, path)) != 0 ||
	    (e = nfs_walk(&mnt, path, 1, &fh, &a)) != 0)
		return e;
	if (a.type == NF3DIR)
		return EISDIR;
	if (u64small(&a.size) == M32)
		return EFBIG;
	off = 0;
	for (;;) {
		e = nfs_read(&mnt, &fh, off, NFS_IOSIZE, iobuf, &got, &eof,
		    (struct nfsattr *)0);
		if (e)
			return e;
		if (got > 0 && write(fd, (char *)iobuf, got) != (int)got)
			return errno;
		off += got;
		if (eof || got == 0)
			break;
	}
	return 0;
}

static void
cmd_get(argc, argv)
	int argc;
	char **argv;
{
	char *local, *s;
	int fd, e;
	long t0, t1;

	if (argc < 2) {
		fail("get", EINVAL);
		return;
	}
	local = argc > 2 ? argv[2] : ((s = strrchr(argv[1], '/')) ? s + 1 : argv[1]);
	fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fail(local, errno);
		return;
	}
	t0 = (long)time((time_t *)0);
	e = fetch(argv[1], fd);
	t1 = (long)time((time_t *)0);
	close(fd);
	if (e)
		fail(argv[1], e);
	else if (rpc_verbose)
		fprintf(stderr, "%s: %ld s\n", argv[1], t1 - t0);
}

static void
cmd_put(argc, argv)
	int argc;
	char **argv;
{
	char path[NFS_PATHMAX], leaf[NFS_NAMEMAX + 1], *remote, *s;
	struct nfsfh dir, fh;
	struct nfsattr a;
	struct nfssattr sa;
	struct stat st;
	u32 off;
	unsigned done;
	int fd, n, e;

	if (argc < 2) {
		fail("put", EINVAL);
		return;
	}
	remote = argc > 2 ? argv[2] : ((s = strrchr(argv[1], '/')) ? s + 1 : argv[1]);
	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		fail(argv[1], errno);
		return;
	}
	fstat(fd, &st);
	if ((e = resolve(remote, path)) != 0 ||
	    (e = nfs_parent(&mnt, path, &dir, leaf)) != 0) {
		close(fd);
		fail(remote, e);
		return;
	}
	sa.mask = SA_MODE | SA_SIZE;
	sa.mode = (u32)(st.st_mode & 0777);
	sa.size.hi = sa.size.lo = 0;
	if ((e = nfs_create(&mnt, &dir, leaf, &sa, 0, &fh, &a)) != 0) {
		close(fd);
		fail(remote, e);
		return;
	}
	off = 0;
	while ((n = read(fd, (char *)iobuf, NFS_IOSIZE)) > 0) {
		unsigned pos = 0;
		while (pos < (unsigned)n) {
			e = nfs_write(&mnt, &fh, off, (unsigned)n - pos,
			    iobuf + pos, &done, (struct nfsattr *)0);
			if (e == 0 && done == 0)
				e = EIO;
			if (e) {
				close(fd);
				fail(remote, e);
				return;
			}
			pos += done;
			off += done;
		}
	}
	close(fd);
}

static void
cmd_stat(arg)
	char *arg;
{
	char path[NFS_PATHMAX], ms[12];
	struct nfsfh fh;
	struct nfsattr a;
	unsigned i;
	int e;

	if ((e = resolve(arg, path)) != 0 ||
	    (e = nfs_walk(&mnt, path, 0, &fh, &a)) != 0) {
		fail(arg, e);
		return;
	}
	modestr(&a, ms);
	printf("path    %s\nmode    %s (%04lo)\nnlink   %lu\nuid/gid %lu/%lu\n",
	    path[0] ? path : "/", ms, a.mode & 07777, a.nlink, a.uid, a.gid);
	printf("size    %lu:%lu (hi:lo)\nfileid  %lu:%lu\nfsid    %lu:%lu\n",
	    a.size.hi, a.size.lo, a.fileid.hi, a.fileid.lo, a.fsid.hi, a.fsid.lo);
	{
		time_t mt;
		mt = (time_t)a.mtime;
		printf("mtime   %s", ctime(&mt));
	}
	printf("fh      ");
	for (i = 0; i < fh.len; i++)
		printf("%02x", fh.data[i]);
	printf("\n");
}

static void
cmd_df()
{
	struct nfsfsstat s;
	int e;

	if ((e = nfs_fsstat(&mnt, &mnt.root, &s)) != 0) {
		fail("fsstat", e);
		return;
	}
	/* report KB: (hi:lo bytes) >> 10 */
	printf("%s:%s\n", mnt.host, mnt.path);
	printf("total %lu KB, free %lu KB, avail %lu KB\n",
	    (s.tbytes.hi << 22) | (s.tbytes.lo >> 10),
	    (s.fbytes.hi << 22) | (s.fbytes.lo >> 10),
	    (s.abytes.hi << 22) | (s.abytes.lo >> 10));
	printf("files %lu, free %lu\n", s.tfiles.lo, s.ffiles.lo);
}

static void
cmd_dirop(op, argc, argv)
	char *op;
	int argc;
	char **argv;
{
	char path[NFS_PATHMAX], path2[NFS_PATHMAX];
	char leaf[NFS_NAMEMAX + 1], leaf2[NFS_NAMEMAX + 1];
	struct nfsfh dir, dir2, fh;
	struct nfsattr a;
	struct nfssattr sa;
	int e;

	if (argc < 2 || (((op[0] == 'm' && op[1] == 'v') || op[0] == 's') && argc < 3)) {
		fail(op, EINVAL);
		return;
	}
	if ((e = resolve(argv[argc - 1], path)) != 0 ||
	    (e = nfs_parent(&mnt, path, &dir, leaf)) != 0) {
		fail(argv[argc - 1], e);
		return;
	}
	if (strcmp(op, "mkdir") == 0) {
		sa.mask = SA_MODE;
		sa.mode = 0755;
		e = nfs_mkdir(&mnt, &dir, leaf, &sa, &fh, &a);
	} else if (strcmp(op, "rmdir") == 0)
		e = nfs_rmdir(&mnt, &dir, leaf);
	else if (strcmp(op, "rm") == 0)
		e = nfs_remove(&mnt, &dir, leaf);
	else if (strcmp(op, "symlink") == 0)
		e = nfs_symlink(&mnt, &dir, leaf, argv[1], &fh, &a);
	else if (strcmp(op, "mv") == 0) {
		if ((e = resolve(argv[1], path2)) == 0 &&
		    (e = nfs_parent(&mnt, path2, &dir2, leaf2)) == 0)
			e = nfs_rename(&mnt, &dir2, leaf2, &dir, leaf);
	} else
		e = EINVAL;
	if (e)
		fail(argv[argc - 1], e);
}

static void
cmd_chmod(argc, argv)
	int argc;
	char **argv;
{
	char path[NFS_PATHMAX], *s;
	struct nfsfh fh;
	struct nfsattr a;
	struct nfssattr sa;
	int e;

	if (argc < 3) {
		fail("chmod", EINVAL);
		return;
	}
	sa.mask = SA_MODE;
	sa.mode = 0;
	for (s = argv[1]; *s >= '0' && *s <= '7'; s++)
		sa.mode = (sa.mode << 3) | (u32)(*s - '0');
	if ((e = resolve(argv[2], path)) != 0 ||
	    (e = nfs_walk(&mnt, path, 1, &fh, &a)) != 0 ||
	    (e = nfs_setattr(&mnt, &fh, &sa, &a)) != 0)
		fail(argv[2], e);
}

static void
help()
{
	printf("commands: ls [-la] [path]  cd [path]  pwd  cat path\n");
	printf("  get remote [local]  put local [remote]  stat path  df\n");
	printf("  mkdir dir  rmdir dir  rm file  mv from to  symlink target name\n");
	printf("  chmod octal path  null  quit\n");
}

static int
docmd(argc, argv)
	int argc;
	char **argv;
{
	char *c;
	int e;

	c = argv[0];
	if (strcmp(c, "ls") == 0 || strcmp(c, "dir") == 0)
		cmd_ls(argc, argv);
	else if (strcmp(c, "cd") == 0)
		cmd_cd(argc > 1 ? argv[1] : (char *)0);
	else if (strcmp(c, "pwd") == 0)
		printf("%s:%s%s\n", mnt.host, mnt.path, cwd);
	else if (strcmp(c, "cat") == 0) {
		fflush(stdout);
		if (argc < 2)
			fail("cat", EINVAL);
		else if ((e = fetch(argv[1], 1)) != 0)
			fail(argv[1], e);
	} else if (strcmp(c, "get") == 0)
		cmd_get(argc, argv);
	else if (strcmp(c, "put") == 0)
		cmd_put(argc, argv);
	else if (strcmp(c, "stat") == 0)
		cmd_stat(argc > 1 ? argv[1] : ".");
	else if (strcmp(c, "df") == 0)
		cmd_df();
	else if (strcmp(c, "mkdir") == 0 || strcmp(c, "rmdir") == 0 ||
	    strcmp(c, "rm") == 0 || strcmp(c, "mv") == 0 ||
	    strcmp(c, "symlink") == 0)
		cmd_dirop(c, argc, argv);
	else if (strcmp(c, "chmod") == 0)
		cmd_chmod(argc, argv);
	else if (strcmp(c, "null") == 0) {
		if ((e = nfs_null(&mnt)) != 0)
			fail("null", e);
		else
			printf("server alive\n");
	} else if (strcmp(c, "quit") == 0 || strcmp(c, "exit") == 0 ||
	    strcmp(c, "bye") == 0)
		return 1;
	else if (strcmp(c, "help") == 0 || strcmp(c, "?") == 0)
		help();
	else {
		fprintf(stderr, "nfsc: unknown command %s\n", c);
		status = 1;
	}
	fflush(stdout);
	return 0;
}

static void
usage()
{
	fprintf(stderr,
	    "usage: nfsc [-v] [-u uid] [-g gid] host:/export [command [args]]\n");
	exit(2);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char line[512], *av[16], *p, *host, *path;
	int ac, e, i;
	u32 uid, gid;

	signal(SIGPIPE, SIG_IGN);
	uid = (u32)geteuid();
	gid = (u32)getgid();
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-v") == 0)
			rpc_verbose = 1;
		else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc)
			uid = (u32)atol(argv[++i]);
		else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc)
			gid = (u32)atol(argv[++i]);
		else
			usage();
	}
	if (i >= argc)
		usage();
	host = argv[i++];
	path = strchr(host, ':');
	if (path == 0 || path[1] != '/')
		usage();
	*path++ = 0;
	rpc_setcred(uid, gid, 0, (u32 *)0);
	if ((e = nfs_mount(&mnt, host, path)) != 0) {
		fprintf(stderr, "nfsc: mount %s:%s: %s\n", host, path,
		    nfs_strerror(e));
		exit(1);
	}
	if (i < argc) {
		docmd(argc - i, argv + i);
	} else {
		for (;;) {
			if (isatty(0)) {
				printf("nfs> ");
				fflush(stdout);
			}
			if (fgets(line, sizeof(line), stdin) == 0)
				break;
			ac = 0;
			p = line;
			while (ac < 15) {
				while (*p == ' ' || *p == '\t' || *p == '\n')
					p++;
				if (*p == 0)
					break;
				av[ac++] = p;
				while (*p && *p != ' ' && *p != '\t' && *p != '\n')
					p++;
				if (*p)
					*p++ = 0;
			}
			if (ac == 0)
				continue;
			av[ac] = 0;
			if (docmd(ac, av))
				break;
		}
	}
	nfs_unmount(&mnt);
	exit(status);
}

/*
 * mount_nfs - mount an NFS version 3 export on 2.11BSD.
 *
 *	mount_nfs [-o options] host:/path directory
 *
 * Options (comma separated): ro, rw (experimental), nosuid, nodev, noexec, sync,
 * actimeo=N (attribute/directory cache seconds, default 5),
 * inval=N (seconds between kernel cache flushes, default 10, 0 = never),
 * uid=N, gid=N (identity used on the server, default 0/0),
 * tmp=DIR (private work files, default /usr/tmp), debug.
 *
 * The program attaches to a free /dev/nfsdN control device, forks a
 * server process that answers the kernel's block requests for /dev/nfsN
 * from the NFS server, and mounts /dev/nfsN on the directory.  The
 * server exits when the filesystem is unmounted.
 */
#include "vd.h"
#include <signal.h>
#ifndef PDP11
#include <sys/wait.h>
#include <sys/select.h>
#else
#include <sys/wait.h>
#endif

static int ctlfd = -1, unit = -1;
static int invalsecs = 10;
static int debug;
static char *tmpdir = "/usr/tmp";
static u8 iobuf[sizeof(struct nfsd_rep) + MAXBSIZE];
static u8 reqbuf[sizeof(struct nfsd_req) + MAXBSIZE];
static int stopping;

static void
usage()
{
	fprintf(stderr, "usage: mount_nfs [-o options] host:/path directory\n");
	exit(2);
}

static int
numopt(s)
	char *s;
{
	return atoi(s);
}

static int
parseopts(o, flags)
	char *o;
	int *flags;
{
	char *p, *q, *v;

	for (p = o; p && *p; p = q) {
		q = strchr(p, ',');
		if (q)
			*q++ = 0;
		v = strchr(p, '=');
		if (v)
			*v++ = 0;
		if (strcmp(p, "ro") == 0 || strcmp(p, "rdonly") == 0)
			*flags |= MNT_RDONLY;
		else if (strcmp(p, "rw") == 0)
			*flags &= ~MNT_RDONLY;
		else if (strcmp(p, "nosuid") == 0)
			*flags |= MNT_NOSUID;
		else if (strcmp(p, "nodev") == 0)
			*flags |= MNT_NODEV;
		else if (strcmp(p, "noexec") == 0)
			*flags |= MNT_NOEXEC;
		else if (strcmp(p, "sync") == 0)
			*flags |= MNT_SYNCHRONOUS;
		else if (strcmp(p, "actimeo") == 0 && v)
			vd_actimeo = numopt(v);
		else if (strcmp(p, "inval") == 0 && v)
			invalsecs = numopt(v);
		else if (strcmp(p, "uid") == 0 && v)
			rpc_cred.uid = (u32)atol(v);
		else if (strcmp(p, "gid") == 0 && v)
			rpc_cred.gid = (u32)atol(v);
		else if (strcmp(p, "tmp") == 0 && v)
			tmpdir = v;
		else if (strcmp(p, "debug") == 0)
			debug = vd_debug = 1;
		else if (strcmp(p, "nfs") == 0 || strcmp(p, "nfsv3") == 0 ||
		    strcmp(p, "bg") == 0 || strcmp(p, "hard") == 0 ||
		    strcmp(p, "soft") == 0 || strcmp(p, "tcp") == 0 ||
		    strcmp(p, "xx") == 0 || strcmp(p, "noauto") == 0 ||
		    strcmp(p, "noatime") == 0 || strcmp(p, "na") == 0)
			;			/* accepted, no effect */
		else if (strcmp(p, "update") == 0) {
			fprintf(stderr, "mount_nfs: cannot update a mount; unmount it first\n");
			return -1;
		}
		else {
			fprintf(stderr, "mount_nfs: unknown option %s\n", p);
			return -1;
		}
	}
	return 0;
}

static void
onterm(sig)
	int sig;
{
	stopping = 1;
}

/* Answer the kernel's requests until the filesystem is unmounted. */
static void
serve()
{
	struct nfsd_req *rq;
	struct nfsd_rep *rp;
	fd_set rfds;
	struct timeval tv;
	long lastinval, laststat, t;
	int n, e;
	daddr_t bn;

	rq = (struct nfsd_req *)reqbuf;
	rp = (struct nfsd_rep *)iobuf;
	if (!vd_readonly && fork() == 0) {
		/*
		 * Push delayed writes (inodes, partial blocks) to us every
		 * two seconds.  This must not be the server process: sync
		 * may wait for blocks that only the server can read.
		 */
		close(ctlfd);
		itab_close();
		for (;;) {
			sleep(2);
			if (getppid() == 1)
				exit(0);
			sync();
		}
	}
	lastinval = laststat = (long)time((time_t *)0);
	for (;;) {
		if (stopping) {
			syslog(LOG_NOTICE, "terminated; /dev/nfs%d now fails", unit);
			exit(1);
		}
		FD_ZERO(&rfds);
		FD_SET(ctlfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		n = select(ctlfd + 1, &rfds, (fd_set *)0, (fd_set *)0, &tv);
		t = (long)time((time_t *)0);
		if (invalsecs > 0 && t - lastinval >= invalsecs) {
			ioctl(ctlfd, NFSDIOCINVAL, (char *)0);
			lastinval = t;
		}
		if (t - laststat >= 60) {
			struct nfsd_stat st;

			vd_statfs(&st);
			ioctl(ctlfd, NFSDIOCSTAT, (char *)&st);
			laststat = t;
		}
		if (n <= 0) {
			vd_idle();
			continue;
		}
		n = read(ctlfd, (char *)reqbuf, sizeof(reqbuf));
		if (n == 0)
			break;			/* unmounted */
		if (n < 0) {
			if (errno == EINTR || errno == EWOULDBLOCK)
				continue;
			syslog(LOG_ERR, "read /dev/nfsd%d: %m", unit);
			exit(1);
		}
		memset((char *)rp, 0, sizeof(*rp));
		rp->np_blkno = rq->nr_blkno;
		rp->np_seq = rq->nr_seq;
		bn = rq->nr_blkno;
		if (rq->nr_count != DEV_BSIZE)
			e = EINVAL;
		else if (rq->nr_op == NFSD_READ)
			e = vd_read(bn, iobuf + sizeof(*rp));
		else
			e = vd_write(bn, reqbuf + sizeof(*rq));
		if (debug)
			syslog(LOG_DEBUG, "%s %ld -> %d",
			    rq->nr_op == NFSD_READ ? "read" : "write", (long)bn, e);
		rp->np_error = e;
		rp->np_count = (e == 0 && rq->nr_op == NFSD_READ) ? DEV_BSIZE : 0;
		if (write(ctlfd, (char *)iobuf, sizeof(*rp) + rp->np_count) < 0)
			syslog(LOG_ERR, "reply /dev/nfsd%d: %m", unit);
	}
	nfs_unmount(&vd_mnt);
	itab_close();
	syslog(LOG_INFO, "%s:%s unmounted", vd_mnt.host, vd_mnt.path);
	exit(0);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	static char optbuf[256];
	char *opts, *o, *spec, *dir, *host, *path, dev[32], from[100];
	struct nfsd_name nn;
	int flags, i, e, pid, pfd[2], status;
	char ok;

	opts = 0;
	flags = 0;
	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		o = 0;
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			o = argv[++i];
		else if (strncmp(argv[i], "-o", 2) == 0 && argv[i][2])
			o = argv[i] + 2;
		if (o) {
			/* mount(8) passes each option as its own -o */
			if (strlen(optbuf) + strlen(o) + 2 > sizeof(optbuf))
				usage();
			if (optbuf[0])
				strcat(optbuf, ",");
			strcat(optbuf, o);
			opts = optbuf;
		} else if (strcmp(argv[i], "-r") == 0)
			flags |= MNT_RDONLY;
		else if (strcmp(argv[i], "-w") == 0)
			flags &= ~MNT_RDONLY;
		else if (strcmp(argv[i], "-v") == 0)
			rpc_verbose = 1;
		else
			usage();
	}
	if (argc - i != 2)
		usage();
	spec = argv[i];
	dir = argv[i + 1];
	if (geteuid() != 0) {
		fprintf(stderr, "mount_nfs: must be root\n");
		exit(1);
	}
	rpc_setcred((u32)0, (u32)0, 0, (u32 *)0);
	if (opts && parseopts(opts, &flags) < 0)
		exit(2);
	vd_readonly = (flags & MNT_RDONLY) != 0;
	if (!vd_readonly)
		flags |= MNT_NOATIME;	/* atime changes would mean utimes() */
	host = spec;
	path = strchr(spec, ':');
	if (path == 0 || path[1] != '/')
		usage();
	sprintf(from, "%.99s", spec);
	*path++ = 0;

	for (unit = 0; unit < NNFSD; unit++) {
		sprintf(dev, "/dev/nfsd%d", unit);
		if ((ctlfd = open(dev, O_RDWR)) >= 0)
			break;
		if (errno != EBUSY) {
			fprintf(stderr, "mount_nfs: %s: %s\n", dev, nfs_strerror(errno));
			exit(1);
		}
	}
	if (ctlfd < 0) {
		fprintf(stderr, "mount_nfs: all %d NFS units are in use\n", NNFSD);
		exit(1);
	}
	signal(SIGPIPE, SIG_IGN);
	if ((e = nfs_mount(&vd_mnt, host, path)) != 0) {
		fprintf(stderr, "mount_nfs: %s:%s: %s\n", host, path,
		    nfs_strerror(e));
		exit(1);
	}
	vd_ctlfd = ctlfd;
	if ((e = vd_init(tmpdir, unit)) != 0 ||
	    (!vd_readonly && (e = vw_init(tmpdir, unit, ctlfd)) != 0)) {
		fprintf(stderr, "mount_nfs: %s:%s: %s\n", host, path,
		    nfs_strerror(e));
		exit(1);
	}

	if (pipe(pfd) < 0) {
		perror("mount_nfs: pipe");
		exit(1);
	}
	pid = fork();
	if (pid < 0) {
		perror("mount_nfs: fork");
		exit(1);
	}
	if (pid == 0) {
		/* server */
		close(pfd[0]);
		signal(SIGINT, SIG_IGN);
		signal(SIGHUP, SIG_IGN);
		signal(SIGQUIT, SIG_IGN);
		signal(SIGTERM, onterm);
		openlog("mount_nfs", LOG_PID, LOG_DAEMON);
		chdir("/");
#ifdef PDP11
		setpgrp(0, getpid());
#else
		setsid();
#endif
		ok = 1;
		write(pfd[1], &ok, 1);
		close(pfd[1]);
		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDWR);
		dup(0);
		dup(0);
		serve();
		exit(0);
	}
	close(pfd[1]);
	if (read(pfd[0], &ok, 1) != 1) {
		fprintf(stderr, "mount_nfs: server process failed\n");
		exit(1);
	}
	close(pfd[0]);
	sprintf(dev, "/dev/nfs%d", unit);
	if (mount(dev, dir, flags) < 0) {
		e = errno;
		kill(pid, SIGKILL);
		wait(&status);
		fprintf(stderr, "mount_nfs: mount %s on %s: %s\n", dev, dir,
		    nfs_strerror(e));
		exit(1);
	}
	memset(nn.nn_name, 0, sizeof(nn.nn_name));
	strncpy(nn.nn_name, from, sizeof(nn.nn_name) - 1);
	ioctl(ctlfd, NFSDIOCSETFROM, (char *)&nn);
#ifdef NFSDIOCLASTI
	if (!vd_readonly) {
		int lasti = (int)VD_POOLINO;	/* new files get pool inodes */

		ioctl(ctlfd, NFSDIOCLASTI, (char *)&lasti);
	}
#endif
	close(ctlfd);
	exit(0);
}

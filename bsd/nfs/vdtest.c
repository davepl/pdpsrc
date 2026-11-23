/*
 * vdtest: exercise the virtual-disk read path without the kernel.
 *	vdtest host:/path [bn ...]
 * Prints the superblock, the root inode and the root directory, then dumps
 * any extra block numbers given (decimal, or ino/lbn).
 */
#include "vd.h"

static u8 buf[DEV_BSIZE];

static void
showino(ino)
	unsigned ino;
{
	struct dinode *dp;
	int e;

	e = vd_read((daddr_t)((ino - 1) / INOPB + 2), buf);
	dp = (struct dinode *)buf + (ino - 1) % INOPB;
	printf("ino %u: e=%d mode %o nlink %d uid %d size %ld addr0 %lx next=%u\n",
	    ino, e, dp->di_mode, dp->di_nlink, dp->di_uid, (long)dp->di_size,
	    (long)dp->di_addr[0], itab_next);
}

static void
showdir(bn)
	daddr_t bn;
{
	struct direct *d;
	int e, off;

	e = vd_read(bn, buf);
	printf("dir %lx: e=%d next=%u\n", (long)bn, e, itab_next);
	for (off = 0; off < DEV_BSIZE; off += d->d_reclen) {
		d = (struct direct *)(buf + off);
		if (d->d_reclen == 0)
			break;
		if (d->d_ino)
			printf("  %u %.*s\n", (unsigned)d->d_ino, d->d_namlen,
			    d->d_name);
	}
}

static int
pfn(arg, name, fid, a, fh)
	void *arg;
	char *name;
	struct u64 *fid;
	struct nfsattr *a;
	struct nfsfh *fh;
{
	unsigned before = itab_next, ino = 0;

	if (fh && name[0] != '.')
		ino = itab_enter(fid, fh, a, ROOTINO);
	printf("  rdp %s fid %lx:%lx a=%d fh=%d ino %u next %u->%u\n", name,
	    (long)fid->hi, (long)fid->lo, a != 0, fh ? fh->len : -1, ino,
	    before, itab_next);
	return 0;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *host, *path, *s;
	struct fs *fs;
	int i, e;

	if (argc < 2 || (path = strchr(argv[1], ':')) == 0) {
		fprintf(stderr, "usage: vdtest host:/path [ino ...]\n");
		exit(2);
	}
	host = argv[1];
	*path++ = 0;
	rpc_setcred((u32)0, (u32)0, 0, (u32 *)0);
	vd_readonly = 1;
	vd_ctlfd = -1;
	if ((e = nfs_mount(&vd_mnt, host, path)) != 0) {
		printf("mount: %s\n", nfs_strerror(e));
		exit(1);
	}
	printf("mounted, next=%u\n", itab_next);
	if ((e = vd_init("/usr/tmp", 3)) != 0) {
		printf("vd_init: %s\n", nfs_strerror(e));
		exit(1);
	}
	printf("vd_init ok, next=%u sizeof irec %d dinode %d fs %d\n",
	    itab_next, (int)sizeof(struct irec), (int)sizeof(struct dinode),
	    (int)sizeof(struct fs));
	e = vd_read((daddr_t)1, buf);
	fs = (struct fs *)buf;
	printf("sb: e=%d isize %u fsize %ld tfree %ld tinode %u next=%u\n", e,
	    fs->fs_isize, (long)fs->fs_fsize, (long)fs->fs_tfree,
	    fs->fs_tinode, itab_next);
	printf("readdirplus: %d\n", nfs_readdirplus(&vd_mnt, &vd_mnt.root,
	    pfn, (void *)0, (struct nfsattr *)0));
	showino(ROOTINO);
	showdir(VD_BN(ROOTINO, 0));
	for (i = 2; i < argc; i++) {
		unsigned ino = (unsigned)atoi(argv[i]);
		showino(ino);
		if ((s = strchr(argv[i], 'd')) != 0)
			showdir(VD_BN(ino, 0));
	}
	return 0;
}

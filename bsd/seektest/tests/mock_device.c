/* Compile the actual utility against simulated syscalls; no disk is opened. */
#define main seektest_main
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#define fstat mock_fstat
#define lseek mock_lseek
#define read mock_read
#include "../seektest.c"
#undef main
#include <stdarg.h>

static off_t position;
static int seeks, reads;

int mock_open(const char *path, int flags, ...)
{
    int part = path[strlen(path) - 1] - 'a';
    if (flags != O_RDONLY || part < 0 || part >= MAXPARTITIONS)
        abort();
    printf("OPEN %s\n", path);
    return part + 3;
}
int mock_close(int fd) { (void)fd; return 0; }
int mock_fstat(int fd, struct stat *st)
{
    (void)fd;
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFCHR;
    return 0;
}
int mock_ioctl(int fd, unsigned long request, ...)
{
    struct disklabel *p;
    const char *fixture = getenv("SEEKTEST_FIXTURE");
    va_list args;
    (void)fd;
    if (request != DIOCGDINFO) abort();
    va_start(args, request); p = va_arg(args, struct disklabel *); va_end(args);
    memset(p, 0, sizeof *p);
    p->d_secsize = 512; p->d_secpercyl = 870; p->d_npartitions = 3;
    p->d_secperunit = 1218000UL;
    p->d_partitions[0].p_size = 15884;
    p->d_partitions[1].p_size = 16720;
    p->d_partitions[1].p_offset = 15884;
    p->d_partitions[2].p_size = 1216665UL;
    if (!strcmp(fixture, "ra60")) {
        p->d_secpercyl = 640; p->d_secperunit = 0; p->d_npartitions = 1;
        p->d_partitions[0].p_size = 400176;
    } else if (!strcmp(fixture, "label-smaller")) {
        p->d_secperunit = 1000000UL;
    } else if (!strcmp(fixture, "overflow")) {
        p->d_secperunit = p->d_partitions[2].p_size = 5000000UL;
    } else if (!strcmp(fixture, "no-zero-offset")) {
        p->d_partitions[0].p_offset = p->d_partitions[2].p_offset = 1;
    } else if (!strcmp(fixture, "bad-label")) {
        p->d_npartitions = MAXPARTITIONS + 1;
    }
    return 0;
}
off_t mock_lseek(int fd, off_t offset, int whence)
{
    (void)fd; if (whence != 0 || offset < 0) abort();
    position = offset; seeks++;
    return offset;
}
int mock_read(int fd, char *buffer, int length)
{
    (void)fd; (void)buffer;
    printf("READ_SECTOR %ld\n", (long)(position / 512));
    reads++;
    if ((reads == 2 && !strcmp(getenv("SEEKTEST_FIXTURE"), "ra60")) || reads > 2) {
        errno = EIO; return -1;
    }
    return length;
}
int main(int argc, char **argv)
{
    if (!strcmp(getenv("SEEKTEST_FIXTURE"), "guard")) {
        if (readcyl(3, 626L, 640L, 0L, 400176UL) != -1 ||
            readcyl(3, 4194304L, 1L, 0L, 5000000UL) != -1 ||
            seeks || reads) abort();
        puts("PASS per-read bounds reject before seeking"); return 0;
    }
    return seektest_main(argc, argv);
}

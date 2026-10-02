/* Run natively: cc -i -o target-probe target-probe.c; ./target-probe */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/dir.h>
#include <sys/wait.h>
#include <stdio.h>

/* These declarations also test this compiler's ANSI prototype support. */
static unsigned long identity(const unsigned long value);

static unsigned long
identity(value)
const unsigned long value;
{
        return value;
}

int
main()
{
        unsigned long value;
        unsigned char *p;
        int i;

        value = identity(0x12345678L);
        p = (unsigned char *)&value;
        printf("sizeof: char=%d short=%d int=%d long=%d pointer=%d\n",
            sizeof(char), sizeof(short), sizeof(int), sizeof(long),
            sizeof(char *));
        printf("sizeof: off_t=%d time_t=%d size_t=%d ssize_t=%d\n",
            sizeof(off_t), sizeof(time_t), sizeof(size_t), sizeof(ssize_t));
        printf("unsigned long 0x12345678 memory bytes:");
        for (i = 0; i < sizeof(value); i++)
                printf(" %02x", p[i]);
        printf("\nunsigned long shift: %lx\n", value >> 16);
        printf("limits: MAXPATHLEN=%d MAXNAMLEN=%d NOFILE=%d\n",
            MAXPATHLEN, MAXNAMLEN, NOFILE);
        printf("sizeof: stat=%d direct=%d DIR=%d fd_set=%d\n",
            sizeof(struct stat), sizeof(struct direct), sizeof(DIR),
            sizeof(fd_set));
        printf("char signed: %d\n", (char)255 < 0);
        return 0;
}

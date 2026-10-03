/* Read-only RA seek exerciser for 2.11BSD. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/disklabel.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Avoid this installation's unistd.h dependency on missing stdint.h.
 * off_t and delay arithmetic must remain long on the 16-bit compiler. */
extern off_t lseek();
extern int read(), close();

static short sector[256];
static struct disklabel label;

static void
usage()
{
        fprintf(stderr, "usage: seektest [-n] unit [step [delay_ms]]\n");
        fprintf(stderr, "  unit: 1, ra1, or rra1 (BSD unit number, 0..31)\n");
        fprintf(stderr, "  step: cylinders per pair, default 1; 0 = full stroke\n");
        fprintf(stderr, "  delay_ms: 0..60000, default 0 (clock-tick resolution)\n");
        fprintf(stderr, "  -n: show device/geometry only, no sector reads\n");
        fprintf(stderr, "  -h: help; Ctrl-C stops the sweep\n");
}

/* Strict decimal conversion, with no int overflow or strtol dependency. */
static long
number(s, limit)
char *s;
long limit;
{
        long n;
        int digit;

        if (*s == '\0')
                return (-1L);
        n = 0;
        while (*s) {
                if (*s < '0' || *s > '9')
                        return (-1L);
                digit = *s++ - '0';
                if (n > (limit - digit) / 10L)
                        return (-1L);
                n = n * 10L + digit;
        }
        return (n);
}

static int
readcyl(fd, cyl, spc, delay)
int fd;
long cyl, spc, delay;
{
        off_t offset;
        struct timeval tv;
        int count;

        offset = (off_t)(cyl * spc * 512L);
        if (lseek(fd, offset, 0) == (off_t)-1) {
                perror("lseek");
                return (-1);
        }
        count = read(fd, (char *)sector, sizeof sector);
        if (count != sizeof sector) {
                if (count < 0)
                        perror("read");
                else
                        fprintf(stderr, "short read: %d bytes\n", count);
                fprintf(stderr, "at cylinder %ld, byte offset %ld\n",
                    cyl, (long)offset);
                return (-1);
        }
        if (delay) {
                tv.tv_sec = delay / 1000L;
                tv.tv_usec = (delay % 1000L) * 1000L;
                if (select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0,
                    &tv) < 0) {
                        perror("delay");
                        return (-1);
                }
        }
        return (0);
}

int
main(argc, argv)
int argc;
char **argv;
{
        char device[32], *arg;
        int unit, fd, part, chosen, info, first;
        long n, step, delay, spc, last, low, high;
        unsigned long total;
        struct stat st;

        info = 0;
        first = 1;
        if (argc > 1 && (!strcmp(argv[1], "-h") ||
            !strcmp(argv[1], "--help"))) {
                usage();
                return (0);
        }
        if (argc > 1 && !strcmp(argv[1], "-n")) {
                info = 1;
                first++;
        }
        if (argc < first + 1 || argc > first + 3) {
                usage();
                return (1);
        }
        arg = argv[first];
        if (!strncmp(arg, "rra", 3))
                arg += 3;
        else if (!strncmp(arg, "ra", 2))
                arg += 2;
        n = number(arg, 31L);
        step = 1L;
        delay = 0L;
        if (argc > first + 1)
                step = number(argv[first + 1], 65535L);
        if (argc > first + 2)
                delay = number(argv[first + 2], 60000L);
        if (n < 0 || step < 0 || delay < 0) {
                usage();
                return (1);
        }
        unit = (int)n;
        fd = -1;
        /* Get the label through any existing raw partition node. */
        for (part = 0; part < MAXPARTITIONS; part++) {
                sprintf(device, "/dev/rra%d%c", unit, 'a' + part);
                fd = open(device, O_RDONLY);
                if (fd < 0)
                        continue;
                if (fstat(fd, &st) == 0 &&
                    (st.st_mode & S_IFMT) == S_IFCHR &&
                    ioctl(fd, DIOCGDINFO, (char *)&label) == 0)
                        break;
                close(fd);
                fd = -1;
        }
        if (fd < 0) {
                fprintf(stderr, "cannot read raw ra%d disk label (check permissions/device)\n", unit);
                return (1);
        }
        close(fd);
        total = label.d_secperunit;
        /* Unlabelled RA media get a driver-generated label: secperunit
         * is zero, but partition a contains the MSCP-reported size. */
        if (!total && label.d_npartitions <= MAXPARTITIONS) {
                for (part = 0; part < label.d_npartitions; part++)
                        if (label.d_partitions[part].p_offset == 0 &&
                            label.d_partitions[part].p_size > total)
                                total = label.d_partitions[part].p_size;
                fprintf(stderr, "using partition size and synthetic label geometry\n");
        }
        spc = (long)label.d_secpercyl;
        if (label.d_secsize != 512 || !total || spc <= 0 ||
            label.d_npartitions == 0 || label.d_npartitions > MAXPARTITIONS) {
                fprintf(stderr, "need a valid 512-byte-sector disk label with geometry\n");
                return (1);
        }
        /* Do not assume 'c' is the whole disk on 2.11BSD. */
        chosen = -1;
        for (part = 0; part < label.d_npartitions; part++) {
                if (label.d_partitions[part].p_offset != 0 ||
                    label.d_partitions[part].p_size < total)
                        continue;
                sprintf(device, "/dev/rra%d%c", unit, 'a' + part);
                fd = open(device, O_RDONLY);
                if (fd < 0)
                        continue;
                if (fstat(fd, &st) == 0 &&
                    (st.st_mode & S_IFMT) == S_IFCHR) {
                        chosen = part;
                        break;
                }
                close(fd);
        }
        if (chosen < 0) {
                fprintf(stderr, "no readable raw partition covers the whole disk\n");
                return (1);
        }
        last = (long)((total - 1UL) / (unsigned long)spc);
        /* off_t is signed 32-bit on 2.11BSD. Check before multiplying. */
        if (last < 1 || (unsigned long)last >
            (2147483647UL / 512UL) / (unsigned long)spc) {
                fprintf(stderr, "disk range is too small or exceeds 2.11BSD seek offsets\n");
                close(fd);
                return (1);
        }
        printf("%s: %lu sectors, %ld sectors/cylinder, cylinders 0..%ld\n",
            device, total, spc, last);
        printf("step %ld cylinders, delay %ld ms; read-only%s\n",
            step, delay, info ? " (geometry only)" : "; Ctrl-C to stop");
        fflush(stdout);
        if (info) {
                close(fd);
                return (0);
        }
        for (;;) {
                low = 0;
                high = last;
                while (low <= high) {
                        if (readcyl(fd, low, spc, delay) < 0 ||
                            (high != low && readcyl(fd, high, spc, delay) < 0)) {
                                close(fd);
                                return (1);
                        }
                        if (!step)
                                continue;
                        if (step > (high - low) / 2L)
                                break;
                        low += step;
                        high -= step;
                }
        }
}

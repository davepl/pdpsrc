/* 2.11BSD touch lacks -r. Copy a reference mtime without changing contents. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
int main(argc, argv)
int argc;
char **argv;
{
    struct stat st;
    struct timeval tv[2];
    if (argc != 3 || stat(argv[1], &st)) return 1;
    tv[0].tv_sec = st.st_atime; tv[0].tv_usec = 0;
    tv[1].tv_sec = st.st_mtime; tv[1].tv_usec = 0;
    if (utimes(argv[2], tv)) { perror(argv[2]); return 1; }
    return 0;
}

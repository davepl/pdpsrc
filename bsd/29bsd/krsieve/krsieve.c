/* Sieve of Eratosthenes
   by Dave Plummer 08/06/2024
   for the PDP-11 running 2.9BSD
*/

#include <stdio.h>
#include <time.h>

#define DEFAULT_LIMIT 1000L
#define DEFAULT_SECONDS 5
#define BITSPERBYTE 8

#define GET_BIT(array, n) ((array[(n) / BITSPERBYTE] >> ((n) % BITSPERBYTE)) & 1)
#define SET_BIT(array, n) (array[(n) / BITSPERBYTE] |= (1 << ((n) % BITSPERBYTE)))

/* Manual forward declarations */
char *malloc();
long atol();
int atoi();
int free();

struct Result {
    long limit;
    int count;
};

struct Result resultsDictionary[] = {
    {10L, 4},
    {100L, 25},
    {1000L, 168},
    {10000L, 1229},
    {50000L, 5133},
    {100000L, 9592},
    {500000L, 41538},
    {1000000L, 78498},
};

/* Simple getopt */
char *optarg;
int optind = 1;

int getopt(argc, argv, optstring)
int argc;
char **argv;
char *optstring;
{
    static int optpos = 1;
    char *cp;
    int c;

    if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
        return -1;

    if (argv[optind][0] == '-' && argv[optind][1] == '-' && argv[optind][2] == '\0') {
        optind++;
        return -1;
    }

    c = argv[optind][optpos];
    cp = optstring;
    while (*cp && *cp != c)
        cp++;

    if (*cp == '\0') {
        if (argv[optind][++optpos] == '\0') {
            optind++;
            optpos = 1;
        }
        return '?';
    }

    if (cp[1] == ':') {
        if (argv[optind][optpos + 1] != '\0') {
            optarg = &argv[optind++][optpos + 1];
        } else if (++optind >= argc) {
            optpos = 1;
            return '?';
        } else {
            optarg = argv[optind++];
        }
        optpos = 1;
    } else {
        if (argv[optind][++optpos] == '\0') {
            optind++;
            optpos = 1;
        }
    }

    return *cp;
}

int print_help(progname)
char *progname;
{
    printf("Usage: %s [-l limit] [-s seconds] [-1] [-p] [-q] [-h|-?]\n", progname);
    printf("Options:\n");
    printf("  -l limit    Set upper limit (default: 1000)\n");
    printf("  -s seconds  Run duration (default: 5)\n");
    printf("  -1          Oneshot mode\n");
    printf("  -p          Print primes\n");
    printf("  -q          Quiet mode\n");
    printf("  -h, -?      Help\n");
    return 0;
}

int validate_results(limit, count)
long limit;
long count;
{
    int i;
    for (i = 0; i < sizeof(resultsDictionary) / sizeof(resultsDictionary[0]); i++) {
        if (resultsDictionary[i].limit == limit)
            return resultsDictionary[i].count == count;
    }
    return 0;
}

int sieve_of_eratosthenes(limit, print_primes, count_ptr)
long limit;
int print_primes;
long *count_ptr;
{
    long i, j, count;
    long size;  /* Changed from int to long */
    char *sieve;
    long square;

    size = (limit / 2) / BITSPERBYTE + 1;  /* Removed (int) cast */
    count = 1;
    
    sieve = (char *) malloc((unsigned)size);

    if (sieve == 0) {
        printf("Memory allocation failed\n");
        return 0;
    }

    for (i = 0; i < size; i++)
        sieve[i] = 0;

    for (i = 3; (square = (long)i * (long)i) <= limit; i += 2) {
        if (!GET_BIT(sieve, i / 2)) {
            for (j = square; j <= limit; j += 2 * i)
                SET_BIT(sieve, j / 2);
        }
    }

    if (print_primes)
        printf("2 ");

    for (i = 3; i <= limit; i += 2) {
        if (!GET_BIT(sieve, i / 2)) {
            count++;
            if (print_primes)
                printf("%ld ", i);
        }
    }

    if (print_primes)
        printf("\n");

    free(sieve);
    *count_ptr = count;
    return 1;
}

int main(argc, argv)
int argc;
char *argv[];
{
    long limit = DEFAULT_LIMIT;
    int oneshot = 0;
    int print_primes = 0;
    int quiet = 0;
    int seconds = DEFAULT_SECONDS;
    int opt;
    int passes = 0;
    long elapsed_time, total_time = 0;
    long start_time, current_time;
    long prime_count = 0;

    while ((opt = getopt(argc, argv, "l:s:1pq?h")) != -1) {
        switch (opt) {
            case 'l':
                limit = atol(optarg);
                break;
            case 's':
                seconds = atoi(optarg);
                break;
            case '1':
                oneshot = 1;
                break;
            case 'p':
                print_primes = 1;
                break;
            case 'q':
                quiet = 1;
                break;
            case 'h':
            case '?':
                return print_help(argv[0]);
            default:
                return print_help(argv[0]);
        }
    }

    if (!quiet) {
        printf("------------------------------------\n");
        printf("Sieve of Eratosthenes by Davepl 2025\n");
        printf("v2.03 for the PDP-11 running 2.9BSD\n");
        printf("------------------------------------\n");
        printf("Solving primes up to %ld\n", limit);
        printf("------------------------------------\n");
    }

    do {
        start_time = time((long *)0);
        sieve_of_eratosthenes(limit, print_primes, &prime_count);
        passes++;
        current_time = time((long *)0);
        elapsed_time = current_time - start_time;
        total_time += elapsed_time;
    } while (!oneshot && total_time < seconds);

    if (!quiet) {
        printf("Total time taken      : %ld seconds\n", total_time);
        printf("Number of passes      : %d\n", passes);
        printf("Time per pass         : %ld seconds\n", passes ? total_time / passes : 0);
        printf("Count of primes found : %ld\n", prime_count);
        printf("Prime validator       : %s\n", validate_results(limit, prime_count) ? "PASS" : "FAIL");
    }

    return 0;
}

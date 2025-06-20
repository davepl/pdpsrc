/* Sieve of Eratosthenes
  
   by Dave Plummer 08/06/2024
   for the PDP-11 runing 211-BSD

   This program calculates prime numbers using the Sieve of Eratosthenes
   algorithm.  It follows the basic rules of the Primes competition in 
   Dave's Garage, but it is limited to a 500K limit due to available ram.

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
/* getopt is in stdlib.h on 2.11BSD, but in getopt.h on modern systems */
#ifdef __pdp11__
/* getopt already included via stdlib.h on 2.11BSD */
#else
#include <getopt.h>
#endif

#define DEFAULT_LIMIT 1000L
#define DEFAULT_SECONDS 5
#define BITSPERBYTE 8

/* Macros for bit manipulation */
#define GET_BIT(array, n) ((array[(n) / BITSPERBYTE] >> ((n) % BITSPERBYTE)) & 1)
#define SET_BIT(array, n) (array[(n) / BITSPERBYTE] |= (1 << ((n) % BITSPERBYTE)))

/* Structure to hold the expected results for a given limit */
struct Result {
    long limit;
    int count;
};

struct Result resultsDictionary[] = {
    {10L, 4},
    {100L, 25},
    {1000L, 168},
    {10000L, 1229},
    {100000L, 9592},
    {500000L, 41538},
    {1000000L, 78498},
};

/* Program Help */
void print_help(char *progname) {
    printf("Usage: %s [-l limit] [-s seconds] [-1] [-p] [-q] [-h|-?]\n", progname);
    printf("Options:\n");
    printf("  -l limit    Specify the upper limit for prime calculation (default: 1000)\n");
    printf("  -s seconds  Specify the duration to run the sieve (default: 5 seconds)\n");
    printf("  -1          Run the sieve only once (oneshot mode)\n");
    printf("  -p          Print the primes as they are found\n");
    printf("  -q          Suppress banners and extraneous output\n");
    printf("  -h, -?      Print this help message and exit\n");
}

/* Validate a limit versus an expected result */
int validate_results(long limit, long count) {
    int i;
    for (i = 0; i < sizeof(resultsDictionary) / sizeof(resultsDictionary[0]); i++) {
        if (resultsDictionary[i].limit == limit) {
            return resultsDictionary[i].count == count;
        }
    }
    return -1;  /* No matching limit found */
}

void sieve_of_eratosthenes(long limit, int print_primes, long *count_ptr) {
    long i, j, count;
    size_t size;
    char *sieve;

    size = (limit + 1) / 2 / BITSPERBYTE + 1;
    count = 1;  /* 2 is a prime number */
    sieve = (char *) calloc(size, 1);

    if (sieve == NULL) {
        printf("Memory allocation failed\n");
        return;
    }

    for (i = 3; i * i <= limit; i += 2) {
        if (!GET_BIT(sieve, i / 2)) {
            for (j = i * i; j <= limit; j += 2 * i) {
                SET_BIT(sieve, j / 2);
            }
        }
    }

    if (print_primes) {
        printf("2 ");
    }

    for (i = 3; i <= limit; i += 2) {
        if (!GET_BIT(sieve, i / 2)) {
            count++;
            if (print_primes) {
                printf("%ld ", i);
            }
        }
    }

    if (print_primes) {
        printf("\n");
    }

    free(sieve);

    *count_ptr = count;
}

/* Main program - parses args and runs sieve */
int main(int argc, char *argv[]) {
    long limit;
    int oneshot;
    int print_primes;
    int quiet;
    int seconds;
    int opt;
    int passes;
    struct timeval start_time, current_time;
    double elapsed_time, total_time;
    long prime_count;

    limit = DEFAULT_LIMIT;
    oneshot = 0;
    print_primes = 0;
    quiet = 0;
    seconds = DEFAULT_SECONDS;
    passes = 0;
    total_time = 0;
    prime_count = 0;

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
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    if (!quiet) {
        printf("------------------------------------\n");
        printf("Sieve of Eratosthenes by Davepl 2024\n");
        printf("v2.02 for the PDP-11 running 211-BSD\n");
        printf("------------------------------------\n");
    }

    if (!quiet) {
        printf("Solving primes up to %ld\n", limit);
        printf("------------------------------------\n");
    }

    do {
        gettimeofday(&start_time, NULL);
        sieve_of_eratosthenes(limit, print_primes, &prime_count);
        passes++;
        gettimeofday(&current_time, NULL);
        elapsed_time = (current_time.tv_sec - start_time.tv_sec) + 
                       (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
        total_time += elapsed_time;
    } while (!oneshot && total_time < (double) seconds);

    if (!quiet) {
        printf("Total time taken      : %.2f seconds\n", total_time);
        printf("Number of passes      : %d\n", passes);
        printf("Time per pass         : %.2f seconds\n", total_time/passes);
        printf("Count of primes found : %ld\n", prime_count);
        printf("Prime validator       : %s\n", validate_results(limit, prime_count) ? "PASS" : "FAIL");
    }
    return 0;
}
/* Sieve of Eratosthenes
by Dave Plummer 08/06/2024, adapted for 29BSD
Calculates prime numbers using the Sieve of Eratosthenes.
Limited to 500K due to PDP-11 RAM constraints.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEFAULT_LIMIT 1000
#define DEFAULT_SECONDS 5
#define BITSPERBYTE 8

/* Macros for bit manipulation */
#define GET_BIT(array, n) ((array[(n) / BITSPERBYTE] >> ((n) % BITSPERBYTE)) & 1)
#define SET_BIT(array, n) (array[(n) / BITSPERBYTE] |= (1 << ((n) % BITSPERBYTE)))

/* Structure for expected results */
struct Result
{
    int limit;
    int count;
};

struct Result resultsDictionary[] = {
    {10, 4},
    {100, 25},
    {1000, 168},
    {10000, 1229},
    {100000, 9592},
    {500000, 41538},
    {1000000, 78498}};

/* Program Help */
void print_help(progname) char *progname;
{
    printf("Usage: %s [-l limit] [-s seconds] [-1] [-p] [-q] [-h]\n", progname);
    printf("Options:\n");
    printf("  -l limit    Upper limit for primes (default: 1000)\n");
    printf("  -s seconds  Duration to run sieve (default: 5)\n");
    printf("  -1          Run once (oneshot mode)\n");
    printf("  -p          Print primes\n");
    printf("  -q          Suppress banners\n");
    printf("  -h          Print help and exit\n");
}

/* Validate results */
int validate_results(limit, count) int limit, count;
{
    int i;
    for (i = 0; i < sizeof(resultsDictionary) / sizeof(resultsDictionary[0]); i++)
    {
        if (resultsDictionary[i].limit == limit)
        {
            return resultsDictionary[i].count == count;
        }
    }
    return -1;
}

/* Sieve of Eratosthenes */
void sieve_of_eratosthenes(limit, print_primes, count_ptr) int limit, print_primes, *count_ptr;
{
    int i, j, count;
    int size;
    char *sieve;

    size = (limit + 1) / 2 / BITSPERBYTE + 1;
    count = 1; /* 2 is prime */
    sieve = (char *)calloc(size, 1);

    if (sieve == NULL)
    {
        printf("Memory allocation failed\n");
        return;
    }

    for (i = 3; i * i <= limit; i += 2)
    {
        if (!GET_BIT(sieve, i / 2))
        {
            for (j = i * i; j <= limit; j += 2 * i)
            {
                SET_BIT(sieve, j / 2);
            }
        }
    }

    if (print_primes)
    {
        printf("2 ");
    }

    for (i = 3; i <= limit; i += 2)
    {
        if (!GET_BIT(sieve, i / 2))
        {
            count++;
            if (print_primes)
            {
                printf("%d ", i);
            }
        }
    }

    if (print_primes)
    {
        printf("\n");
    }

    free(sieve);
    *count_ptr = count;
}

/* Main program */
int main(argc, argv) int argc;
char *argv[];
{
    int limit, oneshot, print_primes, quiet, seconds, passes;
    int prime_count, i;
    time_t start_time, current_time;
    double elapsed_time, total_time;

    limit = DEFAULT_LIMIT;
    oneshot = 0;
    print_primes = 0;
    quiet = 0;
    seconds = DEFAULT_SECONDS;
    passes = 0;
    total_time = 0;
    prime_count = 0;

    /* Manual argument parsing */
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            switch (argv[i][1])
            {
            case 'l':
                if (++i < argc)
                    limit = atoi(argv[i]);
                break;
            case 's':
                if (++i < argc)
                    seconds = atoi(argv[i]);
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
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
            }
        }
    }

    if (!quiet)
    {
        printf("------------------------------------\n");
        printf("Sieve of Eratosthenes by Davepl 2024\n");
        printf("v2.02 for 29BSD on PDP-11\n");
        printf("------------------------------------\n");
        printf("Solving primes up to %d\n", limit);
        printf("------------------------------------\n");
    }

    do
    {
        start_time = time(NULL);
        sieve_of_eratosthenes(limit, print_primes, &prime_count);
        passes++;
        current_time = time(NULL);
        elapsed_time = (double)(current_time - start_time);
        total_time += elapsed_time;
    } while (!oneshot && total_time < (double)seconds);

    if (!quiet)
    {
        printf("Total time taken      : %.2f seconds\n", total_time);
        printf("Number of passes      : %d\n", passes);
        printf("Time per pass         : %.2f seconds\n", total_time / passes);
        printf("Count of primes found : %d\n", prime_count);
        printf("Prime validator       : %s\n", validate_results(limit, prime_count) ? "PASS" : "FAIL");
    }
    return 0;
}

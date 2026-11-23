/* segsieve benchmark driver, adapted from Dave Plummer's 2.11BSD sieve.
   segsieve.c supplies the sieve pass. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
/* getopt is in stdlib.h on 2.11BSD, but in getopt.h on modern systems */
#if defined(pdp11) || defined(__pdp11__)
/* getopt already included via stdlib.h on 2.11BSD */
#else
#include <getopt.h>
#endif

#define DEFAULT_LIMIT 1000L
#define DEFAULT_SECONDS 5
#define MAX_LIMIT 50000000L
#define MAX_SECONDS 32767L

/* Structure to hold the expected results for a given limit */
struct Result {
    long limit;
    long count;
};

struct Result resultsDictionary[] = {
    {10L, 4L},
    {100L, 25L},
    {1000L, 168L},
    {10000L, 1229L},
    {100000L, 9592L},
    {500000L, 41538L},
    {1000000L, 78498L},
    {5000000L, 348513L},
    {10000000L, 664579L},
    {25000000L, 1565927L},
    {50000000L, 3001134L},
};

/* Program Help */
void print_help(char *progname) {
    printf("Usage: %s [-l limit] [-s seconds] [-1] [-p] [-q] [-h|-?]\n", progname);
    printf("Options:\n");
    printf("  -l limit    Specify the upper limit for prime calculation (default: 1000)\n");
    printf("              Supported limits: 2 through %ld\n", MAX_LIMIT);
    printf("  -s seconds  Specify the duration to run the sieve (default: 5 seconds)\n");
    printf("              Supported durations: 1 through 32767 seconds\n");
    printf("  -1          Run the sieve only once (oneshot mode)\n");
    printf("  -p          Print the primes as they are found\n");
    printf("  -q          Suppress banners and extraneous output\n");
    printf("  -h, -?      Print this help message and exit\n");
}

/* Validate a limit versus an expected result */
int validate_results(long limit, long count) {
    size_t i;
    for (i = 0; i < sizeof(resultsDictionary) / sizeof(resultsDictionary[0]); i++) {
        if (resultsDictionary[i].limit == limit) {
            return resultsDictionary[i].count == count;
        }
    }
    return -1;  /* No matching limit found */
}

extern int sieve_of_eratosthenes(long, int, long *);

/* Do not use long / or % here: 2.11BSD's user-space helpers use FP.
   Shift/subtract division is only needed when formatting the final times. */
static unsigned long divide_integer(unsigned long value,
                                    unsigned long divisor) {
    unsigned long quotient, bit;

    quotient = 0;
    bit = 1;
    if (divisor == 0) {
        return 0;
    }
    while (divisor <= (value >> 1)) {
        divisor <<= 1;
        bit <<= 1;
    }
    while (bit != 0) {
        if (value >= divisor) {
            value -= divisor;
            quotient |= bit;
        }
        divisor >>= 1;
        bit >>= 1;
    }
    return quotient;
}

static int parse_number(char *text, long maximum, long *number) {
    long value;

    value = 0;
    if (*text == '\0') {
        return 0;
    }
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        /* Ten times either supported maximum fits in a signed 32-bit long. */
        value = (value << 3) + (value << 1) + (*text++ - '0');
        if (value > maximum) {
            return 0;
        }
    }
    *number = value;
    return 1;
}

static void print_centiseconds(unsigned long value) {
    unsigned long seconds, fraction;

    seconds = divide_integer(value, 100UL);
    fraction = value - ((seconds << 6) + (seconds << 5) +
                        (seconds << 2));
    printf("%lu.%02u", seconds, (unsigned int) fraction);
}

static void print_milliseconds(unsigned long value) {
    unsigned long seconds, fraction;

    seconds = divide_integer(value, 1000UL);
    fraction = value - ((seconds << 9) + (seconds << 8) +
                        (seconds << 7) + (seconds << 6) +
                        (seconds << 5) + (seconds << 3));
    printf("%lu.%03u", seconds, (unsigned int) fraction);
}

/* Main program - parses args and runs sieve */
int main(int argc, char *argv[]) {
    long limit, argument;
    int oneshot;
    int print_primes;
    int quiet;
    int seconds;
    int opt;
    int validation;
    unsigned long passes, centiseconds, average;
    unsigned int hundredths;
    struct timeval start_time, current_time;
    long elapsed_seconds, elapsed_useconds, prime_count;

    limit = DEFAULT_LIMIT;
    oneshot = 0;
    print_primes = 0;
    quiet = 0;
    seconds = DEFAULT_SECONDS;
    passes = 0;
    elapsed_seconds = 0;
    elapsed_useconds = 0;
    prime_count = 0;

    while ((opt = getopt(argc, argv, "l:s:1pq?h")) != -1) {
        switch (opt) {
            case 'l':
                if (!parse_number(optarg, MAX_LIMIT, &limit) || limit < 2L) {
                    fprintf(stderr, "Limit must be an integer from 2 to %ld\n", MAX_LIMIT);
                    return 1;
                }
                break;
            case 's':
                if (!parse_number(optarg, MAX_SECONDS, &argument) ||
                    argument == 0) {
                    fprintf(stderr, "Seconds must be an integer from 1 to 32767\n");
                    return 1;
                }
                seconds = (int) argument;
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
            case '?':
                print_help(argv[0]);
                return strcmp(argv[optind - 1], "-?") == 0 ? 0 : 1;
            default:
                print_help(argv[0]);
                return 1;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        return 1;
    }

    if (!quiet) {
        printf("------------------------------------\n");
        printf("Sieve of Eratosthenes by Davepl 2024\n");
        printf("segsieve: segmented integer-only sieve for 2.11BSD\n");
        printf("------------------------------------\n");
    }

    if (!quiet) {
        printf("Solving primes up to %ld\n", limit);
        printf("------------------------------------\n");
    }

    if (gettimeofday(&start_time, NULL) < 0) {
        perror("gettimeofday");
        return 1;
    }
    do {
        if (!sieve_of_eratosthenes(limit, print_primes, &prime_count)) {
            return 1;
        }
        passes++;
        if (gettimeofday(&current_time, NULL) < 0) {
            perror("gettimeofday");
            return 1;
        }
        elapsed_seconds = current_time.tv_sec - start_time.tv_sec;
        elapsed_useconds = current_time.tv_usec - start_time.tv_usec;
        if (elapsed_useconds < 0) {
            elapsed_useconds += 1000000L;
            elapsed_seconds--;
        }
        if (elapsed_seconds < 0) {
            fprintf(stderr, "Clock moved backwards during measurement\n");
            return 1;
        }
    } while (!oneshot && elapsed_seconds < seconds);

    validation = validate_results(limit, prime_count);

    if (!quiet) {
        /* Round to hundredths without long division or floating point.
           At most 100 subtractions are required, once after the timed loop. */
        elapsed_useconds += 5000L;
        if (elapsed_useconds >= 1000000L) {
            elapsed_useconds -= 1000000L;
            elapsed_seconds++;
        }
        hundredths = 0;
        while (elapsed_useconds >= 10000L) {
            elapsed_useconds -= 10000L;
            hundredths++;
        }
        centiseconds = ((unsigned long) elapsed_seconds << 6) +
                       ((unsigned long) elapsed_seconds << 5) +
                       ((unsigned long) elapsed_seconds << 2) + hundredths;
        /* Milliseconds keep fast passes visible in the final report. */
        average = divide_integer((centiseconds << 3) +
                                 (centiseconds << 1) + (passes >> 1), passes);
        printf("Total time taken      : ");
        print_centiseconds(centiseconds);
        printf(" seconds\n");
        printf("Number of passes      : %lu\n", passes);
        printf("Time per pass         : ");
        print_milliseconds(average);
        printf(" seconds\n");
        printf("Count of primes found : %ld\n", prime_count);
        printf("Prime validator       : %s\n",
               validation < 0 ? "UNKNOWN" : validation ? "PASS" : "FAIL");
    }
    return validation == 0 ? 1 : 0;
}

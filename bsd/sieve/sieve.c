/* Sieve of Eratosthenes
  
   by Dave Plummer 08/06/2024
   for the PDP-11 runing 211-BSD

   This program calculates prime numbers using the Sieve of Eratosthenes
   algorithm.  It follows the basic rules of the Primes competition in 
   Dave's Garage, but it is limited to a 500K limit due to available ram.
   The sieve and timing use integer arithmetic for machines without an FPU.

*/


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
#define MAX_LIMIT 500000L
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
};

/* Program Help */
void print_help(char *progname) {
    printf("Usage: %s [-l limit] [-s seconds] [-1] [-p] [-q] [-h|-?]\n", progname);
    printf("Options:\n");
    printf("  -l limit    Specify the upper limit for prime calculation (default: 1000)\n");
    printf("              Supported limits: 2 through 500000\n");
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

int sieve_of_eratosthenes(long limit, int print_primes, long *count_ptr) {
    unsigned int size, last_byte, last_bit, count;
    unsigned int prime, square_byte, square_bit, delta;
    unsigned int first_byte, first_bit, step_byte, step_bit, lane;
    unsigned int bit, bits, end;
    register unsigned int byte, mask;
    unsigned char *sieve;
    long value;

    if (limit < 2L || limit > MAX_LIMIT) {
        return 0;
    }

    /* Each bit represents an odd number; the final byte may be partial. */
    last_byte = (unsigned int) ((limit - 1L) >> 4);
    last_bit = (unsigned int) ((limit - 1L) >> 1) & 7;
    size = last_byte + 1;
    sieve = (unsigned char *) calloc(size, 1);
    if (sieve == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 0;
    }

    /* floor(3 * 3 / 2) is bit 4 of byte 0.  Advancing an odd prime
       candidate by 2 advances its square's bit index by 2 * prime + 2.
       Byte/bit pairs avoid long multiply and divide on the PDP-11. */
    prime = 3;
    square_byte = 0;
    square_bit = 4;
    while (square_byte < last_byte ||
           (square_byte == last_byte && square_bit <= last_bit)) {
        if (!(sieve[prime >> 4] & (1U << ((prime >> 1) & 7)))) {
            first_byte = square_byte;
            first_bit = square_bit;
            step_byte = prime >> 3;
            step_bit = prime & 7;

            /* Eight interleaved lanes keep each lane's bit mask fixed.
               Its next multiple is exactly prime bytes away.  Even at
               limit 500000, byte + prime remains below 32768. */
            for (lane = 0; lane < 8; lane++) {
                mask = 1U << first_bit;
                for (byte = first_byte; byte < size; byte += prime) {
                    sieve[byte] |= mask;
                }
                first_byte += step_byte;
                first_bit += step_bit;
                if (first_bit >= 8) {
                    first_bit -= 8;
                    first_byte++;
                }
            }
        }
        delta = prime + prime + 2;
        square_byte += delta >> 3;
        square_bit += delta & 7;
        if (square_bit >= 8) {
            square_bit -= 8;
            square_byte++;
        }
        prime += 2;
    }

    /* The unused bit for 1 stands in for prime 2 while counting.
       The maximum count, 41538, fits an unsigned 16-bit integer. */
    count = 0;
    for (byte = 0; byte < size; byte++) {
        bits = sieve[byte];
        end = byte == last_byte ? last_bit + 1 : 8;
        mask = 1;
        for (bit = 0; bit < end; bit++) {
            if (!(bits & mask)) {
                count++;
            }
            mask <<= 1;
        }
    }

    if (print_primes) {
        printf("2 ");
        value = 3L;
        for (byte = 0; byte < size; byte++) {
            end = byte == last_byte ? last_bit + 1 : 8;
            bit = byte == 0 ? 1 : 0;
            mask = 1U << bit;
            for (; bit < end; bit++) {
                if (!(sieve[byte] & mask)) {
                    printf("%ld ", value);
                }
                value += 2L;
                mask <<= 1;
            }
        }
        printf("\n");
    }

    free(sieve);
    *count_ptr = (long) count;
    return 1;
}

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
        /* The previous value is bounded by maximum, at most 500000. */
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
                    fprintf(stderr, "Limit must be an integer from 2 to 500000\n");
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
        printf("v2.03 integer-only for the PDP-11 running 211-BSD\n");
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

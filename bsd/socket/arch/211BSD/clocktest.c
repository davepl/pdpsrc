/*
 * clocktest.c - Test if hardclock is being called by checking counter
 */

#include <sys/types.h>
#include <sys/file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

typedef unsigned long uintptr_t;

/* Declare functions for 211BSD compatibility */
#ifdef __pdp11__
extern int close();
extern long lseek();
extern int read();
extern int open();
extern void sleep();
extern int printf();
extern int perror();
extern char *memset();
extern int memcmp();
#endif

/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int main()
{
    int kmem_fd;
    void *counter_addr = (void*)0;
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    long counter1, counter2;
    
    printf("Clock Test - checking if hardclock is being called\n");
    printf("=================================================\n\n");
    
    /* Find hardclock_counter symbol */
    printf("1. Finding hardclock_counter symbol:\n");
    fp = popen("nm /unix 2>/dev/null | grep hardclock_counter", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix 2>/dev/null | grep hardclock_counter", "r");
    }
    
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            printf("   %s", line);
            if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
                sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
                if (strcmp(symbol, "hardclock_counter") == 0 || strcmp(symbol, "_hardclock_counter") == 0) {
                    printf("   -> Using: %s at 0x%lx\n", symbol, addr);
                    counter_addr = (void*)addr;
                }
            }
        }
        pclose(fp);
    }
    
    if (counter_addr == 0) {
        printf("   ERROR: hardclock_counter symbol not found\n");
        printf("   This means the kernel wasn't built with the counter\n");
        return 1;
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("   open /dev/kmem");
        return 1;
    }
    
    /* Read counter first time */
    if (lseek(kmem_fd, (off_t)(uintptr_t)counter_addr, SEEK_SET) < 0) {
        perror("   lseek");
        close(kmem_fd);
        return 1;
    }
    
    if (read(kmem_fd, &counter1, sizeof(counter1)) != sizeof(counter1)) {
        perror("   read");
        close(kmem_fd);
        return 1;
    }
    
    printf("2. First counter reading: %ld\n", counter1);
    
    /* Wait and read again */
    printf("3. Waiting 2 seconds...\n");
    sleep(2);
    
    if (lseek(kmem_fd, (off_t)(uintptr_t)counter_addr, SEEK_SET) < 0) {
        perror("   lseek");
        close(kmem_fd);
        return 1;
    }
    
    if (read(kmem_fd, &counter2, sizeof(counter2)) != sizeof(counter2)) {
        perror("   read");
        close(kmem_fd);
        return 1;
    }
    
    printf("4. Second counter reading: %ld\n", counter2);
    printf("5. Counter difference: %ld\n", counter2 - counter1);
    
    if (counter2 > counter1) {
        printf("\nSUCCESS: hardclock is being called!\n");
        printf("Clock ticks in 2 seconds: %ld\n", counter2 - counter1);
        printf("Estimated Hz: %ld\n", (counter2 - counter1) / 2);
    } else if (counter2 == counter1) {
        printf("\nFAILED: hardclock counter is not incrementing\n");
        printf("This means hardclock() is not being called\n");
    } else {
        printf("\nWARNING: Counter went backwards (wraparound?)\n");
    }
    
    close(kmem_fd);
    return 0;
}

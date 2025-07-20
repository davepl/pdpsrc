/*
 * allcounters.c - Test multiple counters to see what gets called
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

int find_symbol(name, addr)
char *name;
void **addr;
{
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t sym_addr;
    char type;
    
    fp = popen("nm /unix 2>/dev/null", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix 2>/dev/null", "r");
        if (fp == NULL) {
            return -1;
        }
    }
    
    *addr = NULL;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%lo %c %s", &sym_addr, &type, symbol) == 3 ||
            sscanf(line, "%lx %c %s", &sym_addr, &type, symbol) == 3) {
            if (strcmp(symbol, name) == 0) {
                *addr = (void*)sym_addr;
                break;
            }
        }
    }
    pclose(fp);
    
    return (*addr != NULL) ? 0 : -1;
}

int read_counter(name, addr)
char *name;
void *addr;
{
    int kmem_fd;
    long counter;
    
    if (addr == NULL) {
        printf("   %s: symbol not found\n", name);
        return -1;
    }
    
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        return -1;
    }
    
    if (lseek(kmem_fd, (off_t)(uintptr_t)addr, SEEK_SET) < 0) {
        perror("lseek");
        close(kmem_fd);
        return -1;
    }
    
    if (read(kmem_fd, &counter, sizeof(counter)) != sizeof(counter)) {
        perror("read");
        close(kmem_fd);
        return -1;
    }
    
    close(kmem_fd);
    printf("   %s: %ld (at 0x%lx)\n", name, counter, (unsigned long)addr);
    return 0;
}

int main()
{
    void *hardclock_addr, *timeout_addr;
    
    printf("All Counters Test\n");
    printf("=================\n\n");
    
    /* Find symbols */
    printf("1. Finding counter symbols:\n");
    if (find_symbol("_hardclock_counter", &hardclock_addr) < 0) {
        printf("   hardclock_counter: not found\n");
    } else {
        printf("   hardclock_counter: found at 0x%lx\n", (unsigned long)hardclock_addr);
    }
    
    if (find_symbol("_timeout_counter", &timeout_addr) < 0) {
        printf("   timeout_counter: not found\n");
    } else {
        printf("   timeout_counter: found at 0x%lx\n", (unsigned long)timeout_addr);
    }
    
    /* Read initial values */
    printf("\n2. Initial readings:\n");
    read_counter("hardclock_counter", hardclock_addr);
    read_counter("timeout_counter", timeout_addr);
    
    /* Wait and read again */
    printf("\n3. Waiting 3 seconds...\n");
    sleep(3);
    
    printf("\n4. Second readings:\n");
    read_counter("hardclock_counter", hardclock_addr);
    read_counter("timeout_counter", timeout_addr);
    
    printf("\nThis tells us which kernel functions are actually being called.\n");
    printf("- hardclock_counter increments if hardclock() is called\n");
    printf("- timeout_counter increments if timeout() is called\n");
    
    return 0;
}

/*
 * memdiag.c - Comprehensive memory diagnostic
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
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    int kmem_fd;
    long value;
    unsigned char bytes[4];
    int i;
    
    printf("Memory Diagnostic Test\n");
    printf("=====================\n\n");
    
    /* Show all relevant symbols */
    printf("1. All panel-related symbols:\n");
    fp = popen("nm /unix | grep panel", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            printf("   %s", line);
        }
        pclose(fp);
    }
    
    fp = popen("nm /unix | grep hardclock", "r");
    if (fp != NULL) {
        printf("   hardclock symbols:\n");
        while (fgets(line, sizeof(line), fp)) {
            printf("   %s", line);
        }
        pclose(fp);
    }
    
    fp = popen("nm /unix | grep timeout", "r");
    if (fp != NULL) {
        printf("   timeout symbols:\n");
        while (fgets(line, sizeof(line), fp)) {
            printf("   %s", line);
        }
        pclose(fp);
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        return 1;
    }
    
    /* Test reading from known addresses */
    printf("\n2. Testing memory reads at specific addresses:\n");
    
    /* Read hardclock_counter */
    printf("   hardclock_counter (0x4522):\n");
    if (lseek(kmem_fd, 0x4522, SEEK_SET) >= 0) {
        if (read(kmem_fd, &value, sizeof(value)) == sizeof(value)) {
            printf("     as long: 0x%08lx (%ld)\n", value, value);
        }
        
        lseek(kmem_fd, 0x4522, SEEK_SET);
        if (read(kmem_fd, bytes, 4) == 4) {
            printf("     as bytes: %02x %02x %02x %02x\n", bytes[0], bytes[1], bytes[2], bytes[3]);
        }
    }
    
    /* Read timeout_counter */
    printf("   timeout_counter (0x4526):\n");
    if (lseek(kmem_fd, 0x4526, SEEK_SET) >= 0) {
        if (read(kmem_fd, &value, sizeof(value)) == sizeof(value)) {
            printf("     as long: 0x%08lx (%ld)\n", value, value);
        }
        
        lseek(kmem_fd, 0x4526, SEEK_SET);
        if (read(kmem_fd, bytes, 4) == 4) {
            printf("     as bytes: %02x %02x %02x %02x\n", bytes[0], bytes[1], bytes[2], bytes[3]);
        }
    }
    
    /* Read panel structure */
    printf("   panel structure (0x4502):\n");
    if (lseek(kmem_fd, 0x4502, SEEK_SET) >= 0) {
        unsigned char panel_bytes[16];
        if (read(kmem_fd, panel_bytes, 16) == 16) {
            printf("     raw bytes: ");
            for (i = 0; i < 16; i++) {
                printf("%02x ", panel_bytes[i]);
                if (i == 3 || i == 7 || i == 11) printf(" ");
            }
            printf("\n");
        }
    }
    
    /* Test reading from a few bytes before and after */
    printf("\n3. Memory context around symbols:\n");
    for (addr = 0x4520; addr <= 0x4530; addr += 2) {
        if (lseek(kmem_fd, addr, SEEK_SET) >= 0) {
            if (read(kmem_fd, bytes, 2) == 2) {
                printf("   0x%04lx: %02x %02x\n", addr, bytes[0], bytes[1]);
            }
        }
    }
    
    printf("\n4. Expected values:\n");
    printf("   hardclock_counter should be: 0x87654321\n");
    printf("   timeout_counter should be:   0x12345678\n");
    printf("   If we're reading zeros, the issue is:\n");
    printf("   - Wrong kernel loaded, or\n");
    printf("   - BSS section not initialized, or\n");
    printf("   - Wrong symbol addresses\n");
    
    close(kmem_fd);
    return 0;
}

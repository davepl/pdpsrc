/*
 * memtest.c - Test /dev/kmem access and panel symbol reading
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
    void *panel_addr = (void*)0;  /* We'll find this dynamically */
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    unsigned char buffer[32];  /* Read more than we need */
    int i;
    
    printf("Memory Test for Panel Symbol\n");
    printf("============================\n\n");
    
    /* Find panel symbol */
    printf("1. Finding panel symbol:\n");
    fp = popen("nm /unix 2>/dev/null | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix 2>/dev/null | grep panel", "r");
    }
    
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            printf("   %s", line);
            if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
                sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
                if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                    printf("   -> Using: %s at 0x%lx\n", symbol, addr);
                    panel_addr = (void*)addr;
                }
            }
        }
        pclose(fp);
    }
    
    if (panel_addr == 0) {
        printf("   ERROR: Panel symbol not found\n");
        return 1;
    }
    
    /* Test /dev/kmem access */
    printf("\n2. Testing /dev/kmem access:\n");
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("   open /dev/kmem");
        return 1;
    }
    printf("   /dev/kmem opened successfully\n");
    
    /* Try to read at the symbol address */
    printf("\n3. Reading from panel address 0x%lx:\n", (unsigned long)panel_addr);
    
    if (lseek(kmem_fd, (off_t)(uintptr_t)panel_addr, SEEK_SET) < 0) {
        perror("   lseek");
        close(kmem_fd);
        return 1;
    }
    
    memset(buffer, 0xAA, sizeof(buffer));  /* Fill with pattern to detect reads */
    
    if (read(kmem_fd, buffer, 16) < 0) {  /* Try to read our structure size */
        perror("   read");
        close(kmem_fd);
        return 1;
    }
    
    printf("   Read successful, first 16 bytes:\n");
    printf("   Hex: ");
    for (i = 0; i < 16; i++) {
        printf("%02x ", buffer[i]);
        if (i == 7) printf(" ");  /* Break after 8 bytes */
    }
    printf("\n");
    
    /* Interpret as our structure */
    printf("\n4. Interpreting as panel_state structure:\n");
    {
        struct {
            long ps_address;
            short ps_data;
            short ps_psw;
            short ps_mser;
            short ps_cpu_err;
            short ps_mmr0;
            short ps_mmr3;
        } *panel_ptr = (void*)buffer;
        
        printf("   ps_address: 0x%08lx (%ld)\n", panel_ptr->ps_address, panel_ptr->ps_address);
        printf("   ps_data:    0x%04x (%d)\n", panel_ptr->ps_data, panel_ptr->ps_data);
        printf("   ps_psw:     0x%04x (%d)\n", panel_ptr->ps_psw, panel_ptr->ps_psw);
        printf("   ps_mser:    0x%04x (%d)\n", panel_ptr->ps_mser, panel_ptr->ps_mser);
        printf("   ps_cpu_err: 0x%04x (%d)\n", panel_ptr->ps_cpu_err, panel_ptr->ps_cpu_err);
        printf("   ps_mmr0:    0x%04x (%d)\n", panel_ptr->ps_mmr0, panel_ptr->ps_mmr0);
        printf("   ps_mmr3:    0x%04x (%d)\n", panel_ptr->ps_mmr3, panel_ptr->ps_mmr3);
    }
    
    /* Read again after a delay to see if it changes */
    printf("\n5. Reading again after delay to check for changes:\n");
    sleep(1);
    
    if (lseek(kmem_fd, (off_t)(uintptr_t)panel_addr, SEEK_SET) < 0) {
        perror("   lseek");
    } else {
        unsigned char buffer2[16];
        if (read(kmem_fd, buffer2, 16) >= 0) {
            if (memcmp(buffer, buffer2, 16) == 0) {
                printf("   Data is identical (static)\n");
            } else {
                printf("   Data changed!\n");
                printf("   New hex: ");
                for (i = 0; i < 16; i++) {
                    printf("%02x ", buffer2[i]);
                    if (i == 7) printf(" ");
                }
                printf("\n");
            }
        }
    }
    
    close(kmem_fd);
    return 0;
}

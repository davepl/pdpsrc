/*
 * paneldump.c - Dump the kernel panel state structure
 * Shows raw panel data and monitors for changes
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

/* Include panel state definitions */
#include "panel_state.h"

/* Declare functions for 211BSD compatibility */
#ifdef __pdp11__
extern int close();
extern long lseek();
extern int read();
extern int open();
extern int atoi();
extern int select();
extern void exit();
extern int printf();
extern int perror();
extern char *strcpy();
extern int memcmp();
/* memset is already declared in string.h */
#else
/* Modern systems */
extern int close();
extern off_t lseek();
extern ssize_t read();
extern int open();
#endif

typedef unsigned long uintptr_t;

/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int find_panel_symbol(panel_addr)
void **panel_addr;
{
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    int found_count = 0;
    
    /* PDP-11 systems use /unix or /vmunix */
    fp = popen("nm /unix | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix | grep panel", "r");
        if (fp == NULL) {
            fprintf(stderr, "Cannot run nm to find panel symbol\n");
            return -1;
        }
    }
    
    *panel_addr = NULL;
    printf("Searching for panel symbol in kernel:\n");
    while (fgets(line, sizeof(line), fp)) {
        printf("  nm output: %s", line);
        if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
            sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
            if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                printf("  Found panel symbol: %s at 0x%lx (type %c)\n", symbol, addr, type);
                *panel_addr = (void*)addr;
                found_count++;
            }
        }
    }
    pclose(fp);
    
    if (*panel_addr == NULL) {
        fprintf(stderr, "Panel symbol not found\n");
        return -1;
    }
    
    if (found_count > 1) {
        printf("WARNING: Found %d panel symbols, using last one\n", found_count);
    }
    
    return 0;
}

int main(argc, argv)
int argc;
char *argv[];
{
    void *panel_addr;
    int kmem_fd;
    struct pdp_panel_state panel, prev_panel;
    int count;
    int changes;
    int samples;
    char status[20];
    
    count = 0;
    changes = 0;
    samples = 10;
    
    if (argc > 1) {
        samples = atoi(argv[1]);
        if (samples <= 0) samples = 10;
    }
    
    printf("Panel State Dumper - taking %d samples\n", samples);
    
    /* Find panel symbol */
    if (find_panel_symbol(&panel_addr) < 0) {
        exit(1);
    }
    
    printf("Panel symbol found at address %x\n", panel_addr);
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        exit(1);
    }
    
    printf("Reading panel data...\n");
    printf("Structure size: %d bytes\n", sizeof(panel));
    printf("Panel address: 0x%lx\n", (unsigned long)panel_addr);
    printf("Sample  ps_address    ps_data  ps_psw   ps_mser  ps_cpu_err ps_mmr0  ps_mmr3  Status\n");
    printf("------  ------------  -------  -------  -------  ---------- -------  -------  ------\n");
    
    memset(&prev_panel, 0, sizeof(prev_panel));
    
    for (count = 0; count < samples; count++) {
        off_t offset;
        
        offset = (off_t)(uintptr_t)panel_addr;
        
        /* Read panel data */
        if (lseek(kmem_fd, offset, SEEK_SET) < 0) {
            perror("lseek");
            break;
        }
        
        if (read(kmem_fd, &panel, sizeof(panel)) != sizeof(panel)) {
            perror("read");
            break;
        }
        
        /* Debug: Show raw bytes read */
        if (count == 0) {
            unsigned char *raw = (unsigned char *)&panel;
            int i;
            printf("Raw bytes: ");
            for (i = 0; i < sizeof(panel); i++) {
                printf("%02x ", raw[i]);
            }
            printf("\n");
        }
        
        /* Check for changes */
        strcpy(status, "");
        if (count > 0) {
            if (memcmp(&panel, &prev_panel, sizeof(panel)) == 0) {
                strcpy(status, "SAME");
            } else {
                strcpy(status, "CHANGED");
                changes++;
            }
        } else {
            strcpy(status, "FIRST");
        }
        
        printf("%6d  0x%08lx    0x%04x   0x%04x   0x%04x   0x%04x     0x%04x   0x%04x  %s\n",
               count + 1,
               (unsigned long)panel.ps_address,
               (unsigned short)panel.ps_data,
               (unsigned short)panel.ps_psw,
               (unsigned short)panel.ps_mser,
               (unsigned short)panel.ps_cpu_err,
               (unsigned short)panel.ps_mmr0,
               (unsigned short)panel.ps_mmr3,
               status);
        
        prev_panel = panel;
        
        /* Wait a bit between samples */
        if (count < samples - 1) {
            struct timeval delay;
            delay.tv_sec = 0;
            delay.tv_usec = 100000;  /* 100ms */
            select(0, (fd_set*)0, (fd_set*)0, (fd_set*)0, &delay);
        }
    }
    
    printf("\nSummary: %d samples taken, %d showed changes", samples, changes);
    if (samples > 1) {
        printf(" (%.1f%%)", (float)changes * 100.0 / (samples - 1));
    }
    printf("\n");
    
    if (changes == 0 && samples > 1) {
        printf("WARNING: Panel data appears to be static - kernel may not be updating it\n");
    }
    
    close(kmem_fd);
    return 0;
}

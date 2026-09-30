/*
 * client.c - Socket client for PDP-11 2.11BSD load testing
 * Sends panel data frames to server 30 times per second
 * PDP-11 specific implementation
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

wait_for_panel();

/* Don't include unistd.h on 211BSD - it may reference stdint.h */
#ifndef __pdp11__
#include <unistd.h>
#endif

/* Include panel state definitions first, before common.c */
#include "panel_state.h"

/* Include common functions - but we need to prevent double-include of headers */
#define COMMON_HEADERS_INCLUDED
#define SERVER_PORT 4000
#include "../../common.c"


/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Declare functions for 211BSD compatibility */
#ifdef __pdp11__
/* These need to be declared on 211BSD */
extern int getopt();
extern char *optarg;
extern int optind;
extern int close();
extern long lseek();
extern int read();
extern int open();
#else
/* Modern systems have these in headers */
extern char *optarg;
extern int optind;
#endif

/* Fallback for systems without uintptr_t */
typedef unsigned long uintptr_t; /* Fallback definition */

/* Function prototypes */
int open_kmem_and_find_panel(void **panel_addr);
int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct pdp_panel_state *panel);
void send_frames(int sockfd, struct sockaddr_in *server_addr);

int main(int argc, char *argv[])
{
    char *server_ip = "127.0.0.1";
    int sockfd;
    int c;
    struct sockaddr_in server_addr;
    void *panel_addr;
    int kmem_fd;
    
    /* Parse command line arguments */
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            server_ip = optarg;
            break;
        case 'h':
        case '?':
            usage(argv[0]);
            exit(1);
        }
    }
    
    printf("PDP-11 2.11BSD Panel Client\n");
    printf("Connecting to server at %s:%d via UDP\n", server_ip, SERVER_PORT);
    
    /* Open /dev/kmem and find panel symbol */
    kmem_fd = open_kmem_and_find_panel(&panel_addr);
    if (kmem_fd < 0) {
        fprintf(stderr, "Failed to open /dev/kmem or find panel symbol\n");
        exit(1);
    }
    
    printf("Panel symbol found at address %p\n", panel_addr);
    
    /* Create UDP socket and set up server address */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
        close(kmem_fd);
        exit(1);
    }
    
    printf("UDP socket created. Sending panel data to %s:%d at %d Hz...\n", 
           server_ip, SERVER_PORT, FRAMES_PER_SECOND);
    printf("Packet size: %d bytes\n", (int)sizeof(struct pdp_panel_packet));
    printf("Note: UDP is connectionless - errors will be reported during transmission\n");
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
    close(kmem_fd);
    return 0;
}

int open_kmem_and_find_panel(void **panel_addr)
{
    FILE *fp;
    char line[256];
    char symbol[64];
    uintptr_t addr;
    char type;
    int kmem_fd;
    
    /* PDP-11 systems use /unix or /vmunix with octal addresses */
    fp = popen("nm /unix | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix | grep panel", "r");
        if (fp == NULL) {
            fprintf(stderr, "Cannot run nm to find panel symbol\n");
            return -1;
        }
    }
    
    *panel_addr = NULL;
    while (fgets(line, sizeof(line), fp)) {
        /* Try both octal and hex formats */
        if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
            sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
            if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                *panel_addr = (void*)addr;
                break;
            }
        }
    }
    pclose(fp);
    
    if (*panel_addr == NULL) {
        fprintf(stderr, "Panel symbol not found in kernel symbol table\n");
        return -1;
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        return -1;
    }
    
    return kmem_fd;
}

int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct pdp_panel_state *panel)
{
    off_t offset = (off_t)(uintptr_t)panel_addr;
    static int read_count = 0;
    struct pdp_panel_state prev_panel;
    int bytes_read;
    
    /* Save previous panel state for comparison */
    if (read_count > 0) {
        prev_panel = *panel;
    }
    
    if (lseek(kmem_fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }
    
    bytes_read = read(kmem_fd, panel, sizeof(*panel));
    if (bytes_read != sizeof(*panel)) {
        perror("read");
        printf("Expected %d bytes, got %d bytes\n", (int)sizeof(*panel), bytes_read);
        return -1;
    }
    
    read_count++;
    
   
    /* Every 100 reads, show if data has been static */
    if (read_count % 100 == 0 && read_count > 1) {
        if (panel->ps_address == prev_panel.ps_address && panel->ps_data == prev_panel.ps_data) {
            printf("WARNING: Panel data has been static for last 100 reads (read #%d)\n", read_count);
        }
    }
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct pdp_panel_state panel;
    struct pdp_panel_packet packet;
    int frame_count = 0;
    static void *panel_addr = NULL;
    static int kmem_fd = -1;
    int send_result;
    
    printf("Entering send_frames function...\n");
    printf("Socket fd: %d\n", sockfd);
    printf("Server address: %s:%d\n", inet_ntoa(server_addr->sin_addr), ntohs(server_addr->sin_port));
    
    /* Open /dev/kmem and find panel address if not already done */
    if (kmem_fd < 0) {
        printf("Opening kmem and finding panel address...\n");
        kmem_fd = open_kmem_and_find_panel(&panel_addr);
        if (kmem_fd < 0) {
            fprintf(stderr, "Cannot access kernel memory\n");
            return;
        }
    printf("Kmem opened successfully, panel at %p\n", panel_addr);
        
        /* Test: Read a few different memory locations to verify our read mechanism works */
        printf("Testing memory read mechanism...\n");
        {
            char test_buf1[16], test_buf2[16];
            off_t test_addr1 = (off_t)(uintptr_t)panel_addr;
            off_t test_addr2 = test_addr1 + 32;  /* Read 32 bytes away */
            
            /* Read from panel address */
            if (lseek(kmem_fd, test_addr1, SEEK_SET) >= 0 && 
                read(kmem_fd, test_buf1, 16) == 16) {
                printf("  Test read 1 (panel addr): %02x %02x %02x %02x %02x %02x %02x %02x\n",
                       test_buf1[0]&0xff, test_buf1[1]&0xff, test_buf1[2]&0xff, test_buf1[3]&0xff,
                       test_buf1[4]&0xff, test_buf1[5]&0xff, test_buf1[6]&0xff, test_buf1[7]&0xff);
            }
            
            /* Read from nearby address */
            if (lseek(kmem_fd, test_addr2, SEEK_SET) >= 0 && 
                read(kmem_fd, test_buf2, 16) == 16) {
                printf("  Test read 2 (+32 bytes): %02x %02x %02x %02x %02x %02x %02x %02x\n",
                       test_buf2[0]&0xff, test_buf2[1]&0xff, test_buf2[2]&0xff, test_buf2[3]&0xff,
                       test_buf2[4]&0xff, test_buf2[5]&0xff, test_buf2[6]&0xff, test_buf2[7]&0xff);
            }
            
            /* Compare to see if we get different data */
            if (memcmp(test_buf1, test_buf2, 16) == 0) {
                printf("  WARNING: Both memory locations contain identical data!\n");
            } else {
                printf("  Good: Memory locations contain different data\n");
            }
        }
    }
    
    printf("Starting packet transmission loop...\n");
    
    while (1) {
        /* Read panel structure from kernel memory */
        if (read_panel_from_kmem(kmem_fd, panel_addr, &panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct pdp_panel_state);
        packet.header.pp_byte_flags = PANEL_PDP1170;  /* Set panel type flag */
        packet.panel_state = panel;
        
        /* Send panel packet via UDP */
        send_result = sendto(sockfd, &packet, sizeof(packet), 0,
                            (struct sockaddr *)server_addr, sizeof(*server_addr));
        if (send_result < 0) {
            fprintf(stderr, "sendto failed after %d packets (errno=%d): ", frame_count, errno);
            perror("");
            break;
        }
        
        frame_count++;
        
        /* Wait for next frame time */
        wait_for_panel();
    }
}

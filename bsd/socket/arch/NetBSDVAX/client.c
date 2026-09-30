/*
 * client.c - Socket client for NetBSD VAX load testing
 * Sends panel data frames to server 30 times per second
 * NetBSD VAX specific implementation - reads from kernel panel symbol
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

/* External getopt declarations for compatibility */
extern char *optarg;
extern int optind, optopt;

/* Include common functions */
#define SERVER_PORT 4002
#include "../../common.c"


/* Include panel state definitions */
#include "panel_state.h"

/* Fallback for systems without uintptr_t */
#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

/* Function prototypes */
int open_kmem_and_find_panel(void **panel_addr);
int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct vax_panel_state *panel);
void send_frames(int sockfd, struct sockaddr_in *server_addr, int kmem_fd, void *panel_addr);

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
    
    printf("NetBSD VAX Panel Client\n");
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
    
    /* Configure socket for immediate transmission - disable batching */
    /* Disable socket send buffering for immediate transmission */
    int sndbuf = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        /* If we can't disable buffering completely, try minimal buffer */
        sndbuf = 1024;
        setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    }
    
    /* Set low latency socket option if available */
    int lowdelay = 1;
    setsockopt(sockfd, IPPROTO_IP, IP_TOS, &lowdelay, sizeof(lowdelay));
    
    printf("UDP socket created. Sending panel data to %s:%d at %d Hz...\n", 
           server_ip, SERVER_PORT, FRAMES_PER_SECOND);
    printf("Packet size: %d bytes\n", (int)sizeof(struct vax_panel_packet));
    printf("Note: UDP is connectionless - errors will be reported during transmission\n");
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr, kmem_fd, panel_addr);
    
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
    int found_panel_symbols = 0;
    int found_any_symbols = 0;
    
    /* First, test if nm works at all */
    printf("Testing nm command on /netbsd...\n");
    printf("Kernel info: ");
    fflush(stdout);
    system("uname -a");
    printf("Kernel file: ");
    fflush(stdout);
    system("ls -la /netbsd");
    
    fp = popen("nm /netbsd | head -5", "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot run nm on /netbsd\n");
        return -1;
    }
    
    printf("First 5 symbols from nm:\n");
    while (fgets(line, sizeof(line), fp) && found_any_symbols < 5) {
        printf("  %s", line);
        found_any_symbols++;
    }
    pclose(fp);
    
    if (found_any_symbols == 0) {
        fprintf(stderr, "nm command returned no output - kernel symbol table may not be available\n");
        return -1;
    }
    
    /* Now search specifically for panel */
    printf("\nSearching for panel symbol...\n");
    fp = popen("nm /netbsd | grep panel", "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot run nm on /netbsd to find panel symbol\n");
        return -1;
    }
    
    *panel_addr = NULL;
    while (fgets(line, sizeof(line), fp)) {
        found_panel_symbols++;
        printf("Found panel-related symbol: %s", line);
        
        /* Try both octal and hex formats */
        if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3 ||
            sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3) {
            printf("  -> Parsed: addr=0x%lx, type='%c', symbol='%s'\n", 
                   (unsigned long)addr, type, symbol);
            
            if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                printf("  -> MATCH! Using symbol '%s' at address 0x%lx\n", 
                       symbol, (unsigned long)addr);
                *panel_addr = (void*)addr;
                break;
            }
        } else {
            printf("  -> Failed to parse line\n");
        }
    }
    pclose(fp);
    
    printf("Total panel-related symbols found: %d\n", found_panel_symbols);
    
    if (*panel_addr == NULL) {
        if (found_panel_symbols == 0) {
            fprintf(stderr, "No panel symbols found in kernel - the kernel may not have panel support compiled in\n");
            fprintf(stderr, "Try checking: nm /netbsd | grep panel\n");
        } else {
            fprintf(stderr, "Panel symbol not found in kernel symbol table\n");
            fprintf(stderr, "Looking specifically for symbol named 'panel' or '_panel'\n");
        }
        return -1;
    }
    
    /* Open /dev/kmem */
    kmem_fd = open("/dev/kmem", O_RDONLY);
    if (kmem_fd < 0) {
        perror("open /dev/kmem");
        /* On NetBSD, try /dev/mem as fallback */
        kmem_fd = open("/dev/mem", O_RDONLY);
        if (kmem_fd < 0) {
            perror("open /dev/mem");
            return -1;
        }
        printf("Using /dev/mem instead of /dev/kmem\n");
    }
    
    return kmem_fd;
}

int read_panel_from_kmem(int kmem_fd, void *panel_addr, struct vax_panel_state *panel)
{
    off_t offset = (off_t)(uintptr_t)panel_addr;
    unsigned char raw_data[128];  /* Buffer for raw kernel panel data */
    size_t bytes_to_read = sizeof(raw_data);
    struct timeval tv;
    
    /* Seek to panel address in kernel memory */
    if (lseek(kmem_fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }
    
    /* Read raw panel data from kernel */
    if (read(kmem_fd, raw_data, bytes_to_read) < 0) {
        perror("read");
        return -1;
    }

    
    /* Get current time for dynamic data */
    gettimeofday(&tv, NULL);
    
    /* Convert raw kernel data to VAX panel format */
    /* Since we're on the same architecture, we can directly extract values */
    
    /* Extract address and data directly from raw memory */
    panel->ps_address = *(uint32_t*)&raw_data[0];
    panel->ps_data = *(uint32_t*)&raw_data[4];
    
    return 0;    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr, int kmem_fd, void *panel_addr)
{
    struct vax_panel_state panel;
    struct vax_panel_packet packet;
    int frame_count = 0;
    
    while (1) {
        /* Read panel structure from kernel memory */
        if (read_panel_from_kmem(kmem_fd, panel_addr, &panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct vax_panel_state);
        packet.header.pp_byte_flags = PANEL_VAX;  /* Set panel type flag */
        packet.panel_state = panel;
        
        /* Send panel packet via UDP with immediate transmission */
        if (sendto(sockfd, &packet, sizeof(packet), MSG_DONTWAIT,
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            fprintf(stderr, "sendto failed after %d packets: ", frame_count);
            perror("");
            if (errno == ENETUNREACH) {
                fprintf(stderr, "Network unreachable - check server IP address\n");
            } else if (errno == EHOSTUNREACH) {
                fprintf(stderr, "Host unreachable - server may be down\n");
            } else if (errno == ECONNREFUSED) {
                fprintf(stderr, "Connection refused - server not listening on port %d\n", SERVER_PORT);
            } else if (errno == EWOULDBLOCK || errno == EAGAIN) {
                /* Non-blocking send would block, continue anyway */
                fprintf(stderr, "Warning: UDP send would block (frame %d)\n", frame_count);
            }
            else {
                break;
            }
        }
        
        frame_count++;
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(packet));
            if (frame_count == 1) {
                printf("DEBUG: Panel contents - ps_address=0x%lx, ps_data=0x%x\n", 
                       (unsigned long)panel.ps_address, (unsigned short)panel.ps_data);
                printf("First packet sent successfully - server appears reachable\n");
            }
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

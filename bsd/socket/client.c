/*
 * client.c - Socket client for PDP-11 load testing
 * Sends 16-word frames to server 30 times per second
 * Compatible with K&R C and 211BSD
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

/* Include unistd.h on modern systems for getopt and system calls */
#ifdef __APPLE__
#include <unistd.h>
#endif

/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Declare functions for 211BSD compatibility if not already declared */
#ifndef __APPLE__  /* Only declare these on older systems */
extern int getopt();
extern char *optarg;
extern int optind;
extern int close();
extern long lseek();
extern int read();
extern int open();
#endif

/* Portable type definitions for PDP-11 compatibility */
typedef unsigned short u16_t;   /* Always 16 bits */
typedef unsigned long  u32_t;   /* 32 bits on modern, but we'll mask appropriately */

#define SERVER_PORT 8080
#define FRAME_SIZE 16
#define FRAMES_PER_SECOND 30
#define USEC_PER_FRAME (1000000 / FRAMES_PER_SECOND)

/* Panel structure - must match kernel definition */
/* PDP-11 uses natural alignment, so we'll send/receive as individual fields */
struct panel_state {
    u32_t ps_address;    /* 22-bit address (stored in 32-bit for portability) */
    u16_t ps_data;       /* 16-bit data */
};

/* Function prototypes */
int create_udp_socket(char *server_ip, struct sockaddr_in *server_addr);
void send_frames(int sockfd, struct sockaddr_in *server_addr);
void usage(char *progname);
void precise_delay(long usec);
int open_kmem_and_find_panel(long *panel_addr);
int read_panel_from_kmem(int kmem_fd, long panel_addr, struct panel_state *panel);

int main(int argc, char *argv[])
{
    char *server_ip = "127.0.0.1";
    int sockfd;
    int c;
    struct sockaddr_in server_addr;
    long panel_addr;
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
    
    printf("Connecting to server at %s:%d via UDP\n", server_ip, SERVER_PORT);
    
    /* Open /dev/kmem and find panel symbol */
    kmem_fd = open_kmem_and_find_panel(&panel_addr);
    if (kmem_fd < 0) {
        fprintf(stderr, "Failed to open /dev/kmem or find panel symbol\n");
        exit(1);
    }
    
    printf("Panel symbol found at address 0x%lx\n", panel_addr);
    
    /* Create UDP socket and set up server address */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
        close(kmem_fd);
        exit(1);
    }
    
    printf("Connected successfully. Sending panel data at %d Hz...\n", FRAMES_PER_SECOND);
    printf("Sending packet size: %d bytes (ps_address: %d, ps_data: %d)\n", 
           (int)sizeof(struct panel_state), (int)sizeof(u32_t), (int)sizeof(u16_t));
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
    close(kmem_fd);
    return 0;
}

int create_udp_socket(char *server_ip, struct sockaddr_in *server_addr)
{
    int sockfd;
    struct hostent *host;
    
    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set up server address */
    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(SERVER_PORT);
    
    /* Convert IP address */
    server_addr->sin_addr.s_addr = inet_addr(server_ip);
    if (server_addr->sin_addr.s_addr == INADDR_NONE) {
        /* Try to resolve hostname */
        host = gethostbyname(server_ip);
        if (host == NULL) {
            fprintf(stderr, "Invalid server address: %s\n", server_ip);
            close(sockfd);
            return -1;
        }
        memcpy(&server_addr->sin_addr, host->h_addr, host->h_length);
    }
    
    return sockfd;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct panel_state panel;
    int frame_count = 0;
    static long panel_addr = 0;
    static int kmem_fd = -1;
    
    /* Open /dev/kmem and find panel address if not already done */
    if (kmem_fd < 0) {
        kmem_fd = open_kmem_and_find_panel(&panel_addr);
        if (kmem_fd < 0) {
            fprintf(stderr, "Cannot access kernel memory\n");
            return;
        }
    }
    
    while (1) {
        /* Read panel structure from kernel memory */
        if (read_panel_from_kmem(kmem_fd, panel_addr, &panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
        
        /* Send panel data via UDP - serialize manually for portability */
        {
            char buffer[6];  /* 4 bytes address + 2 bytes data */
            u32_t addr = (u32_t)panel.ps_address;
            u16_t data = panel.ps_data;
            
            /* Pack data in network byte order for consistency */
            buffer[0] = (addr >> 24) & 0xFF;
            buffer[1] = (addr >> 16) & 0xFF; 
            buffer[2] = (addr >> 8) & 0xFF;
            buffer[3] = addr & 0xFF;
            buffer[4] = (data >> 8) & 0xFF;
            buffer[5] = data & 0xFF;
            
            if (sendto(sockfd, buffer, 6, 0, 
                       (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
                perror("sendto");
                break;
            }
        }
        
        frame_count++;
        if (0 && frame_count % (FRAMES_PER_SECOND * 10) == 0) {  /* Every 10 seconds */
            printf("Sent %d panel updates (addr=0x%lx, data=0x%x)\n", 
                   frame_count, panel.ps_address, (unsigned short)panel.ps_data);
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

void usage(char *progname)
{
    printf("Usage: %s [-s server_ip]\n", progname);
    printf("  -s server_ip   IP address of server (default: 127.0.0.1)\n");
    printf("  -h             Show this help\n");
}

int open_kmem_and_find_panel(long *panel_addr)
{
    FILE *fp;
    char line[256];
    char symbol[64];
    long addr;
    char type;
    int kmem_fd;
    
    /* Try to find panel symbol in kernel symbol table */
    fp = popen("nm /unix | grep panel", "r");
    if (fp == NULL) {
        fp = popen("nm /vmunix | grep panel", "r");
        if (fp == NULL) {
            fprintf(stderr, "Cannot run nm to find panel symbol\n");
            return -1;
        }
    }
    
    *panel_addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Parse octal address format used by 211BSD nm */
        if (sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3) {
            if (strcmp(symbol, "panel") == 0 || strcmp(symbol, "_panel") == 0) {
                *panel_addr = addr;
                break;
            }
        }
    }
    pclose(fp);
    
    if (*panel_addr == 0) {
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

int read_panel_from_kmem(int kmem_fd, long panel_addr, struct panel_state *panel)
{
    if (lseek(kmem_fd, panel_addr, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }
    
    if (read(kmem_fd, panel, sizeof(*panel)) != sizeof(*panel)) {
        perror("read");
        return -1;
    }
    
    return 0;
}

void precise_delay(long usec)
{
    struct timeval timeout;
    
    /* Convert microseconds to seconds and microseconds */
    timeout.tv_sec = usec / 1000000;
    timeout.tv_usec = usec % 1000000;
    
    /* Use select() with no file descriptors as a portable delay */
    select(0, NULL, NULL, NULL, &timeout);
}

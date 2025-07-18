/*
 * client.c - Socket client for NetBSD VAX load testing
 * Sends panel data frames to server 30 times per second
 * NetBSD VAX specific implementation
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
#include <unistd.h>

/* Include common functions */
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
    
    printf("Connected successfully. Sending panel data at %d Hz...\n", FRAMES_PER_SECOND);
    printf("Packet size: %d bytes\n", (int)sizeof(struct vax_panel_packet));
    
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
    
    /* NetBSD VAX systems use /netbsd with hex addresses */
    fp = popen("nm /netbsd | grep panel", "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot run nm on /netbsd to find panel symbol\n");
        return -1;
    }
    
    *panel_addr = NULL;
    while (fgets(line, sizeof(line), fp)) {
        /* Try both octal and hex formats */
        if (sscanf(line, "%lx %c %s", &addr, &type, symbol) == 3 ||
            sscanf(line, "%lo %c %s", &addr, &type, symbol) == 3) {
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
    
    if (lseek(kmem_fd, offset, SEEK_SET) < 0) {
        perror("lseek");
        return -1;
    }
    
    if (read(kmem_fd, panel, sizeof(*panel)) != sizeof(*panel)) {
        perror("read");
        return -1;
    }
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct vax_panel_state panel;
    struct vax_panel_packet packet;
    int frame_count = 0;
    static void *panel_addr = NULL;
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
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct vax_panel_state);
        packet.header.pp_byte_flags = PANEL_VAX;  /* Set panel type flag */
        packet.panel_state = panel;
        
        /* Send panel packet via UDP */
        if (sendto(sockfd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            perror("sendto");
            break;
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 1) == 0) {  /* Every 1 second */
            printf("Sent %d panel updates (ps_address=0x%lx, ps_data=0x%x)\n", 
                   frame_count, (unsigned long)panel.ps_address, (unsigned short)panel.ps_data);
        }
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(packet));
            if (frame_count == 1) {
                printf("DEBUG: Panel contents - ps_address=0x%lx, ps_data=0x%x\n", 
                       (unsigned long)panel.ps_address, (unsigned short)panel.ps_data);
            }
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

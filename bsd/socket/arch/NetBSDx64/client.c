/*
 * client.c - Socket client for NetBSD x64 load testing
 * Sends panel data frames to server 30 times per second
 * NetBSD x64 specific implementation using kvm library
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
#include <stdint.h>
#include <kvm.h>
#include <nlist.h>
#include <limits.h>

/* Include common functions */
#define SERVER_PORT 4001

#include "../../common.c"

/* Include panel state definitions */
#include "panel_state.h"

/* Define _POSIX2_LINE_MAX if not available */
#ifndef _POSIX2_LINE_MAX
#define _POSIX2_LINE_MAX 2048
#endif

/* Global variables for kvm access */
static kvm_t *kd = NULL;
static struct nlist nl[] = {
    { "_panel" },
    { NULL }
};

/* Function prototypes */
int open_kvm_and_find_panel(void);
int read_panel_from_kvm(struct netbsdx64_panel_state *panel);
void send_frames(int sockfd, struct sockaddr_in *server_addr);

int main(int argc, char *argv[])
{
    char *server_ip = "127.0.0.1";
    int sockfd;
    int c;
    struct sockaddr_in server_addr;
    
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
    
    printf("NetBSD x64 Panel Client\n");
    printf("Connecting to server at %s:%d via UDP\n", server_ip, SERVER_PORT);
    
    /* Open kvm and find panel symbol */
    if (open_kvm_and_find_panel() < 0) {
        fprintf(stderr, "Failed to open kvm or find panel symbol\n");
        exit(1);
    }
    
    printf("Panel symbol found at kernel address %p\n", (void*)nl[0].n_value);
    printf("This is the ADDRESS OF the panel structure in kernel memory\n");
    
    /* Create UDP socket and set up server address */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
        if (kd) kvm_close(kd);
        exit(1);
    }
    
    printf("Connected successfully. Sending panel data at %d Hz...\n", FRAMES_PER_SECOND);
    printf("Packet size: %d bytes\n", (int)sizeof(struct netbsdx64_panel_packet));
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
    if (kd) kvm_close(kd);
    return 0;
}

int open_kvm_and_find_panel(void)
{
    char errbuf[_POSIX2_LINE_MAX];
    
    /* Open kernel virtual memory */
    kd = kvm_open(NULL, NULL, NULL, O_RDONLY, errbuf);
    if (kd == NULL) {
        fprintf(stderr, "kvm_open: %s\n", errbuf);
        return -1;
    }
    
    /* Look up the panel symbol */
    if (kvm_nlist(kd, nl) != 0) {
        fprintf(stderr, "kvm_nlist: %s\n", kvm_geterr(kd));
        kvm_close(kd);
        kd = NULL;
        return -1;
    }
    
    if (nl[0].n_value == 0) {
        fprintf(stderr, "Panel symbol not found in kernel symbol table\n");
        kvm_close(kd);
        kd = NULL;
        return -1;
    }
    
    return 0;
}

int read_panel_from_kvm(struct netbsdx64_panel_state *panel)
{
    static int first_read = 1;
    
    if (kd == NULL) {
        fprintf(stderr, "kvm not initialized\n");
        return -1;
    }
    
    /* Read panel structure from kernel memory using kvm_read */
    if (kvm_read(kd, nl[0].n_value, panel, sizeof(*panel)) != sizeof(*panel)) {
        fprintf(stderr, "kvm_read: %s\n", kvm_geterr(kd));
        return -1;
    }
    
    /* Debug output for first few reads */
    if (first_read) {
        printf("DEBUG: Reading from kernel address 0x%lx\n", nl[0].n_value);
        printf("DEBUG: Read %d bytes from kernel\n", (int)sizeof(*panel));
        printf("DEBUG: Raw panel.ps_frame.cf_rip = 0x%lx (CONTENTS of cf_rip field)\n", 
               (unsigned long)panel->ps_frame.cf_rip);
        printf("DEBUG: Raw panel.ps_frame.cf_rsp = 0x%lx (CONTENTS of cf_rsp field)\n", 
               (unsigned long)panel->ps_frame.cf_rsp);
        first_read = 0;
    }
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct netbsdx64_panel_state panel;
    struct netbsdx64_panel_packet packet;
    int frame_count = 0;
    
    while (1) {
        /* Read panel structure from kernel memory */
        if (read_panel_from_kvm(&panel) < 0) {
            fprintf(stderr, "Failed to read panel data from kernel\n");
            break;
        }
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct netbsdx64_panel_state);
        packet.header.pp_byte_flags = PANEL_NETBSDX64;  /* Set panel type flag */
        packet.panel_state = panel;
        
        /* Send panel packet via UDP */
        if (sendto(sockfd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            perror("sendto");
            break;
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 1) == 0) {  /* Every 1 second */
            printf("Sent %d panel updates (cf_rip=0x%lx, cf_rsp=0x%lx)\n", 
                   frame_count, (unsigned long)panel.ps_frame.cf_rip, (unsigned long)panel.ps_frame.cf_rsp);
        }
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(packet));
            if (frame_count == 1) {
                printf("DEBUG: Panel contents - cf_rip=0x%lx, cf_rsp=0x%lx\n", 
                       (unsigned long)panel.ps_frame.cf_rip, (unsigned long)panel.ps_frame.cf_rsp);
            }
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

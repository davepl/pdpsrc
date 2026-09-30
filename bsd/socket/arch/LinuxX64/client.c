/*
 * client.c - Linux x64 panel client
 * Uses ptrace APIs to capture CPU state and proc/sys filesystem for system information
 * Sends panel data frames to server 30 times per second
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

/* External getopt declarations for compatibility */
extern char *optarg;
extern int optind, optopt;

/* Include common functions */
#define SERVER_PORT 4000
#include "../../common.c"

/* Include panel state definitions */
#include "panel_state.h"

/* Function prototypes */
int capture_cpu_state(struct linuxx64_panel_state *panel);
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
    
    printf("Linux Panel Client (x64)\n");
    
    /* Create UDP socket and connect to server */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        exit(1);
    }
    
    printf("Connected to server at %s:%d\n", server_ip, SERVER_PORT);
    printf("Packet size: %d bytes\n", (int)sizeof(struct linuxx64_panel_packet));
    
    /* Send panel data frames */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
    return 0;
}

int capture_cpu_state(struct linuxx64_panel_state *panel)
{
    FILE *fp;
    struct pt_regs *regs = &panel->ps_regs;
    char buffer[512];
    int ret;
    
    /* Try to read real register data from kernel module */
    fp = fopen("/proc/panel_regs", "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot access /proc/panel_regs\n");
        fprintf(stderr, "Please load the kernel module first: sudo make load\n");
        exit(1);
    }
    
    /* Read the first line to check if data is available */
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        fprintf(stderr, "Error: Could not read from /proc/panel_regs\n");
        fclose(fp);
        exit(1);
    }
    
    /* Check if no data has been captured yet */
    if (strncmp(buffer, "No register data captured yet", 29) == 0) {
        fclose(fp);
        /* Return error to retry */
        return -1;
    }
    
    /* Parse all registers from the buffer */
    ret = sscanf(buffer, "RIP=0x%lx RSP=0x%lx RBP=0x%lx RAX=0x%lx RBX=0x%lx RCX=0x%lx RDX=0x%lx RSI=0x%lx RDI=0x%lx R8=0x%lx R9=0x%lx R10=0x%lx R11=0x%lx R12=0x%lx R13=0x%lx R14=0x%lx R15=0x%lx EFLAGS=0x%lx CS=0x%lx SS=0x%lx ORIG_RAX=0x%lx",
             &regs->rip, &regs->rsp, &regs->rbp, &regs->rax, &regs->rbx, &regs->rcx, &regs->rdx,
             &regs->rsi, &regs->rdi, &regs->r8, &regs->r9, &regs->r10, &regs->r11, &regs->r12,
             &regs->r13, &regs->r14, &regs->r15, &regs->eflags, &regs->cs, &regs->ss, &regs->orig_rax);
    
    fclose(fp);
    
    if (ret != 21) {
        fprintf(stderr, "Error: Could not parse register data (got %d fields)\n", ret);
        return -1;
    }
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct linuxx64_panel_state panel;
    struct linuxx64_panel_packet packet;
    int frame_count = 0;
    
    while (1) {
        /* Capture current CPU state - retry if no data available yet */
        if (capture_cpu_state(&panel) < 0) {
            /* Wait a bit and try again */
            precise_delay(USEC_PER_FRAME);
            continue;
        }
        
        /* Populate packet structure */
        packet.header.pp_byte_count = sizeof(struct linuxx64_panel_state);
        packet.header.pp_byte_flags = PANEL_LINUXX64;
        packet.panel_state = panel;
        
        /* Send panel packet via UDP */
        if (sendto(sockfd, &packet, sizeof(packet), 0,
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            perror("sendto");
            break;
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 2) == 0) {  /* Every 2 seconds */
            printf("Frame %d: RIP=0x%lx, RSP=0x%lx, RAX=0x%lx, RBX=0x%lx\n", 
                   frame_count, panel.ps_regs.rip, panel.ps_regs.rsp, 
                   panel.ps_regs.rax, panel.ps_regs.rbx);
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

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
#include "../../common.c"

/* Include panel state definitions */
#include "panel_state.h"

/* Function prototypes */
int capture_cpu_state(struct linuxx64_panel_state *panel);
int get_system_stats(struct linuxx64_panel_state *panel);
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
    /* For demonstration purposes, we'll generate simulated register values
     * In a real implementation, this would use ptrace or kernel modules 
     * to capture actual CPU state from interrupt context */
    static uint64_t counter = 0;
    struct pt_regs *regs = &panel->ps_regs;
    
    /* Generate some varying register values for demonstration */
    counter++;
    
    /* Simulate some realistic register values */
    regs->rip = 0x400000 + (counter * 0x10) % 0x10000;     /* Instruction pointer */
    regs->rsp = 0x7fffffffe000 - (counter * 8) % 0x1000;   /* Stack pointer */
    regs->rbp = regs->rsp + 0x100;                          /* Base pointer */
    regs->rax = counter;                                    /* Accumulator */
    regs->rbx = counter * 2;                               /* Base register */
    regs->rcx = counter * 3;                               /* Counter register */
    regs->rdx = counter * 4;                               /* Data register */
    regs->rsi = counter * 5;                               /* Source index */
    regs->rdi = counter * 6;                               /* Destination index */
    regs->r8  = counter * 7;                               /* Extended registers */
    regs->r9  = counter * 8;
    regs->r10 = counter * 9;
    regs->r11 = counter * 10;
    regs->r12 = counter * 11;
    regs->r13 = counter * 12;
    regs->r14 = counter * 13;
    regs->r15 = counter * 14;
    regs->eflags = 0x202 | ((counter % 2) << 6);          /* Flags with some variation */
    regs->cs = 0x33;                                       /* Code segment */
    regs->ss = 0x2b;                                       /* Stack segment */
    regs->orig_rax = (counter % 256);                      /* Original system call number */
    
    return 0;
}

int get_system_stats(struct linuxx64_panel_state *panel)
{
    /* Linux pt_regs structure only contains register values
     * No additional system stats are stored in this structure
     * This function is kept for compatibility but doesn't modify
     * the panel structure beyond what capture_cpu_state does */
    
    return 0;
}

void send_frames(int sockfd, struct sockaddr_in *server_addr)
{
    struct linuxx64_panel_state panel;
    struct linuxx64_panel_packet packet;
    int frame_count = 0;
    
    while (1) {
        /* Capture current CPU state and system stats */
        if (capture_cpu_state(&panel) < 0) {
            fprintf(stderr, "Failed to capture CPU state\n");
            /* Continue with system stats only */
        }
        
        if (get_system_stats(&panel) < 0) {
            fprintf(stderr, "Failed to get system stats\n");
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
            printf("Frame %d: RIP=0x%llx, RSP=0x%llx, RAX=0x%llx, RBX=0x%llx\n", 
                   frame_count, panel.ps_regs.rip, panel.ps_regs.rsp, 
                   panel.ps_regs.rax, panel.ps_regs.rbx);
        }
        
        /* Debug: Print first few sends */
        if (frame_count <= 5) {
            printf("DEBUG: Sent packet #%d, size=%d bytes\n", frame_count, (int)sizeof(packet));
            printf("DEBUG: RIP=0x%llx, RAX=0x%llx, RBX=0x%llx\n", 
                   panel.ps_regs.rip, panel.ps_regs.rax, panel.ps_regs.rbx);
        }
        
        /* Wait for next frame time */
        precise_delay(USEC_PER_FRAME);
    }
}

/*
 * server.c - Cross-platform socket server for panel data
 * Receives panel data from PDP-11, NetBSD x64, and NetBSD VAX clients
 * Uses common packet header to distinguish client types
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>

/* Include common packet header and panel type definitions */
#include "panel_packet.h"
#include "common.c"

/* Include platform-specific panel definitions */
#include "arch/211BSD/panel_state.h"
#include "arch/NetBSDx64/panel_state.h"
#include "arch/NetBSDVAX/panel_state.h"
#include "arch/macOS/panel_state.h"
#include "arch/LinuxX64/panel_state.h"

#define SERVER_PORT 4000

/* Global variables for signal handling */
static int server_sockfd = -1;

/* Function prototypes */
int create_udp_server_socket(void);
void handle_udp_clients(int sockfd);
void signal_handler(int sig);
void setup_signal_handlers(void);
void format_binary(uint32_t value, int bits, char *buffer);
void format_binary64(uint64_t value, int bits, char *buffer);
void display_pdp_panel(struct pdp_panel_state *panel);
void display_netbsdx64_panel(struct netbsdx64_panel_state *panel);
void display_vax_panel(struct vax_panel_state *panel);
void display_macos_panel(struct macos_panel_state *panel);
void display_linuxx64_panel(struct linuxx64_panel_state *panel);
const char* get_panel_type_name(uint32_t flags);

/* Function prototypes */
int create_udp_server_socket(void);
void handle_udp_clients(int sockfd);
void signal_handler(int sig);
void setup_signal_handlers(void);
void format_binary(uint32_t value, int bits, char *buffer);

/* Format a value as binary string using O for 1 and . for 0 */
void format_binary(uint32_t value, int bits, char *buffer)
{
    int i;
    for (i = bits - 1; i >= 0; i--) {
        buffer[bits - 1 - i] = ((value >> i) & 1) ? 'O' : '.';
    }
    buffer[bits] = '\0';
}

/* Get panel type name from flags */
const char* get_panel_type_name(uint32_t flags)
{
    switch (flags) {
        case PANEL_PDP1170: return "PDP-11/70";
        case PANEL_VAX: return "VAX";
        case PANEL_NETBSDX64: return "NetBSD x64";
        case PANEL_MACOS: return "macOS";
        case PANEL_LINUXX64: return "Linux x64";
        default: return "Unknown";
    }
}

/* Display PDP-11 panel data */
void display_pdp_panel(struct pdp_panel_state *panel)
{
    char addr_bin[23], data_bin[17], psw_bin[17], mmr0_bin[17], mmr3_bin[17];
    
    /* Format each field as binary */
    format_binary(panel->ps_address & 0x3FFFFF, 22, addr_bin);  /* 22-bit address */
    format_binary(panel->ps_data, 16, data_bin);
    format_binary(panel->ps_psw, 16, psw_bin);
    format_binary(panel->ps_mmr0, 16, mmr0_bin);
    format_binary(panel->ps_mmr3, 16, mmr3_bin);
    
    printf("PDP-11: ADDR: %s, DATA: %s, PSW: %s, MMR0: %s, MMR3: %s\n",
           addr_bin, data_bin, psw_bin, mmr0_bin, mmr3_bin);
}

/* Display NetBSD x64 panel data */
void display_netbsdx64_panel(struct netbsdx64_panel_state *panel)
{
    /* Clear screen and move cursor to home position every frame */
    printf("\033[2J\033[H");
    fflush(stdout);

    struct clockframe *cf = &panel->ps_frame;
    char bin[65];
    
    #define PRINT_REG(name, val) do { \
        format_binary64((uint64_t)(val), 64, bin); \
        printf("%-6s= %016llx %s\n", name, (unsigned long long)(val), bin); \
    } while(0)

    PRINT_REG("ESP", cf->cf_rsp);
    PRINT_REG("EIP", cf->cf_rip);
    PRINT_REG("RAX", cf->cf_rax);
    PRINT_REG("RBX", cf->cf_rbx);
    PRINT_REG("RCX", cf->cf_rcx);
    PRINT_REG("RDX", cf->cf_rdx);
    PRINT_REG("RSI", cf->cf_rsi);
    PRINT_REG("RDI", cf->cf_rdi);
    PRINT_REG("RBP", cf->cf_rbp);
    PRINT_REG("R8",  cf->cf_r8);
    PRINT_REG("R9",  cf->cf_r9);
    PRINT_REG("R10", cf->cf_r10);
    PRINT_REG("R11", cf->cf_r11);
    PRINT_REG("R12", cf->cf_r12);
    PRINT_REG("R13", cf->cf_r13);
    PRINT_REG("R14", cf->cf_r14);
    PRINT_REG("R15", cf->cf_r15);
    PRINT_REG("FLAGS", cf->cf_rflags);
    PRINT_REG("CS", cf->cf_cs);
    PRINT_REG("SS", cf->cf_ss);
    PRINT_REG("GS", cf->cf_gs);
    PRINT_REG("FS", cf->cf_fs);
    PRINT_REG("ES", cf->cf_es);
    PRINT_REG("DS", cf->cf_ds);
    PRINT_REG("TRAPNO", cf->cf_trapno);
    PRINT_REG("ERR", cf->cf_err);
    fflush(stdout);
    #undef PRINT_REG
}
/* Format a 64-bit value as binary string using * for 1 and . for 0 */
void format_binary64(uint64_t value, int bits, char *buffer)
{
    int i;
    for (i = bits - 1; i >= 0; i--) {
        buffer[bits - 1 - i] = ((value >> i) & 1) ? '*' : '.';
    }
    buffer[bits] = '\0';
}

/* Display Linux x64 panel data */
void display_linuxx64_panel(struct linuxx64_panel_state *panel)
{
    /* Clear screen and move cursor to home position every frame */
    printf("\033[2J\033[H");
    fflush(stdout);

    struct pt_regs *regs = &panel->ps_regs;
    char bin[65];
    
    #define PRINT_REG(name, val) do { \
        format_binary64((uint64_t)(val), 64, bin); \
        printf("%-6s= %016llx %s\n", name, (unsigned long long)(val), bin); \
    } while(0)

    PRINT_REG("RSP", regs->rsp);
    PRINT_REG("RIP", regs->rip);
    PRINT_REG("RAX", regs->rax);
    PRINT_REG("RBX", regs->rbx);
    PRINT_REG("RCX", regs->rcx);
    PRINT_REG("RDX", regs->rdx);
    PRINT_REG("RSI", regs->rsi);
    PRINT_REG("RDI", regs->rdi);
    PRINT_REG("RBP", regs->rbp);
    PRINT_REG("R8",  regs->r8);
    PRINT_REG("R9",  regs->r9);
    PRINT_REG("R10", regs->r10);
    PRINT_REG("R11", regs->r11);
    PRINT_REG("R12", regs->r12);
    PRINT_REG("R13", regs->r13);
    PRINT_REG("R14", regs->r14);
    PRINT_REG("R15", regs->r15);
    PRINT_REG("FLAGS", regs->eflags);
    PRINT_REG("CS", regs->cs);
    PRINT_REG("SS", regs->ss);
    PRINT_REG("ORIGAX", regs->orig_rax);
    fflush(stdout);
    #undef PRINT_REG
}

/* Display NetBSD VAX panel data */
void display_vax_panel(struct vax_panel_state *panel)
{
    char addr_bin[33], data_bin[17], psw_bin[17], mmr0_bin[17], mmr3_bin[17];
    
    /* Format each field as binary */
    format_binary(panel->ps_address, 32, addr_bin);  /* 32-bit address */
    format_binary(panel->ps_data, 16, data_bin);
    format_binary(panel->ps_psw, 16, psw_bin);
    format_binary(panel->ps_mmr0, 16, mmr0_bin);
    format_binary(panel->ps_mmr3, 16, mmr3_bin);
    
    printf("VAX: ADDR: %s, DATA: %s, PSW: %s, MMR0: %s, MMR3: %s\n",
           addr_bin, data_bin, psw_bin, mmr0_bin, mmr3_bin);
}

/* Display macOS panel data */
void display_macos_panel(struct macos_panel_state *panel)
{
    char pc_bin[33], sp_bin[33], cpu_bin[8], mem_bin[8];
    
    /* Format key values as binary (show lower 32 bits for readability) */
    format_binary((uint32_t)(panel->pc & 0xFFFFFFFF), 32, pc_bin);
    format_binary((uint32_t)(panel->sp & 0xFFFFFFFF), 32, sp_bin);
    format_binary(panel->cpu_usage, 7, cpu_bin);  /* 0-100 fits in 7 bits */
    format_binary(panel->memory_usage, 7, mem_bin);
    
    printf("macOS (ARM64): PC: %s, SP: %s, CPU: %s%%, MEM: %s%%, LOAD: %.2f, THR: %d\n",
           pc_bin, sp_bin, cpu_bin, mem_bin, 
           panel->load_average / 100.0, panel->thread_count);
}

int main(int argc, char *argv[])
{
    printf("Starting UDP server on port %d...\n", SERVER_PORT);
    
    /* Set up signal handlers for clean shutdown */
    setup_signal_handlers();
    
    /* Create and bind UDP server socket */
    server_sockfd = create_udp_server_socket();
    if (server_sockfd < 0) {
        fprintf(stderr, "Failed to create UDP server socket\n");
        exit(1);
    }
    
    printf("UDP server listening on port %d\n", SERVER_PORT);
    printf("Waiting for frames...\n");
    
    /* Handle incoming UDP packets */
    handle_udp_clients(server_sockfd);
    
    if (server_sockfd >= 0) {
        close(server_sockfd);
    }
    
    return 0;
}

int create_udp_server_socket(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    int reuse = 1;
    
    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set socket options */
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }
    
    /* Set up server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    /* Bind socket */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    printf("UDP socket successfully bound to port %d\n", SERVER_PORT);
    fflush(stdout);
    
    return sockfd;
}

void handle_udp_clients(int sockfd)
{
    struct panel_packet_header header;
    char buffer[1024];  /* Buffer for panel data */
    int bytes_received;
    int frame_count = 0;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    
    printf("Receiving UDP panel data with header protocol:\n");
    printf("Header size: %d bytes (2 bytes count + 4 bytes flags)\n", 
           (int)sizeof(header));
    printf("Panel type flags: PDP1170=%d, VAX=%d, NetBSDx64=%d, macOS=%d, LinuxX64=%d\n",
           PANEL_PDP1170, PANEL_VAX, PANEL_NETBSDX64, PANEL_MACOS, PANEL_LINUXX64);
    printf("Binary format: O=1, .=0\n\n");
    
    while (1) {
        /* Receive the complete packet */
        client_addr_len = sizeof(client_addr);
        bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom packet");
            break;
        }
        
        /* Check if we have at least a complete header */
        if (bytes_received < sizeof(header)) {
            printf("[Incomplete header: got %d bytes, expected at least %d]\n",
                   bytes_received, (int)sizeof(header));
            continue;
        }
        
        /* Parse the header from the received buffer */
        memcpy(&header, buffer, sizeof(header));
        
        /* Calculate expected total packet size */
        int expected_packet_size = sizeof(header) + header.pp_byte_count;
        if (bytes_received != expected_packet_size) {
            printf("[Packet size mismatch: got %d bytes, expected %d from %s:%d]\n",
                   bytes_received, expected_packet_size,
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            continue;
        }
        
        /* Process based on panel type */
        switch (header.pp_byte_flags) {
            case PANEL_PDP1170: {
                struct pdp_panel_packet *packet = (struct pdp_panel_packet *)buffer;
                printf("[%s] ", get_panel_type_name(header.pp_byte_flags));
                display_pdp_panel(&packet->panel_state);
                break;
            }
            
            case PANEL_VAX: {
                struct vax_panel_packet *packet = (struct vax_panel_packet *)buffer;
                printf("[%s] ", get_panel_type_name(header.pp_byte_flags));
                display_vax_panel(&packet->panel_state);
                break;
            }
            
            case PANEL_NETBSDX64: {
                struct netbsdx64_panel_packet *packet = (struct netbsdx64_panel_packet *)buffer;
                printf("[%s] ", get_panel_type_name(header.pp_byte_flags));
                display_netbsdx64_panel(&packet->panel_state);
                break;
            }
            
            case PANEL_MACOS: {
                struct macos_panel_packet *packet = (struct macos_panel_packet *)buffer;
                printf("[%s] ", get_panel_type_name(header.pp_byte_flags));
                display_macos_panel(&packet->panel_state);
                break;
            }
            
            case PANEL_LINUXX64: {
                struct linuxx64_panel_packet *packet = (struct linuxx64_panel_packet *)buffer;
                printf("[%s] ", get_panel_type_name(header.pp_byte_flags));
                display_linuxx64_panel(&packet->panel_state);
                break;
            }
            
            default:
                printf("[Unknown panel type: 0x%08lx, %d bytes from %s:%d]\n",
                       (unsigned long)header.pp_byte_flags, header.pp_byte_count,
                       inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                break;
        }
        
        frame_count++;
        if (frame_count % 30 == 0) {  /* Every second at 30 Hz */
            printf("--- Received %d frames ---\n", frame_count);
        }
        
        fflush(stdout);
    }
    
    printf("\nTotal panel updates received: %d\n", frame_count);
}

void signal_handler(int sig)
{
    printf("\nReceived signal %d, shutting down...\n", sig);
    
    if (server_sockfd >= 0) {
        close(server_sockfd);
    }
    
    exit(0);
}

void setup_signal_handlers(void)
{
    signal(SIGINT, signal_handler);   /* Ctrl+C */
    signal(SIGTERM, signal_handler);  /* Termination signal */
}

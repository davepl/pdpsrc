/*
 * server.c - Socket server for PDP-11 load testing
 * Receives 16-word frames from clients and prints * for each frame
 * Compatible with K&R C and 211BSD
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

#define SERVER_PORT 8080

// Detect endianness at compile-time, fallback to runtime check if needed.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define le16toh(x) (x)
        #define le32toh(x) (x)
    #else
        #define le16toh(x) __builtin_bswap16(x)
        #define le32toh(x) __builtin_bswap32(x)
    #endif

#elif defined(_WIN32)
    #include <stdlib.h>
    #define le16toh(x) (x)
    #define le32toh(x) (x)

#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>
    #define le16toh(x) OSSwapLittleToHostInt16(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)

#else
    // Fallback: runtime check, safe but a bit slower
    static inline uint16_t le16toh(uint16_t x) {
        uint16_t test = 1;
        if (*(uint8_t*)&test == 1) return x;
        return (x << 8) | (x >> 8);
    }
    static inline uint32_t le32toh(uint32_t x) {
        uint16_t test = 1;
        if (*(uint8_t*)&test == 1) return x;
        return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
               ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
    }
#endif

/* Panel structure - must match kernel definition */
/* Use pragma pack(1) to match PDP-11's natural 6-byte layout */
#pragma pack(1)
struct panel_state {
	uint32_t ps_address;	/* panel switches - 32-bit address */
	uint16_t ps_data;		/* panel lamps - 16-bit data */
    uint16_t ps_psw;
    uint16_t ps_mser;
    uint16_t ps_cpu_err;
    uint16_t ps_mmr0;
    uint16_t ps_mmr3;
};
#pragma pack()

/* NetBSD panel structure - only 2 fields */
struct netbsd_panel_state {
    uint64_t ps_address;        /* panel switches - 64-bit address on NetBSD */
    uint64_t ps_data;           /* panel lamps - 64-bit data on NetBSD */
};

/* Global variables for signal handling */
static int server_sockfd = -1;

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
    struct panel_state panel;
    int bytes_received;
    int frame_count = 0;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    
    printf("Receiving UDP panel data:\n");
    printf("Expected packet size: %d bytes (packed structure from PDP-11)\n", 
           (int)sizeof(panel));
    printf("Format: ADDR (22-bit), DATA (16-bit), PSW (16-bit), MMR0 (16-bit), MMR3 (16-bit)\n");
    printf("Binary format: O=1, .=0\n\n");
    
    while (1) {
        /* Receive UDP panel data */
        client_addr_len = sizeof(client_addr);
        bytes_received = recvfrom(sockfd, &panel, sizeof(panel), 0,
                                  (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (bytes_received < 0) {
            if (errno == EINTR) {
                /* Interrupted by signal, continue */
                continue;
            }
            perror("recvfrom");
            break;
        }
        
        /* Check if we received a complete panel structure */
        if (bytes_received == sizeof(panel)) {
            /* PDP-11 format - convert from little-endian to host byte order */
            panel.ps_address = le32toh(panel.ps_address);
            panel.ps_data = le16toh(panel.ps_data);
            panel.ps_psw = le16toh(panel.ps_psw);
            panel.ps_mser = le16toh(panel.ps_mser);
            panel.ps_cpu_err = le16toh(panel.ps_cpu_err);
            panel.ps_mmr0 = le16toh(panel.ps_mmr0);
            panel.ps_mmr3 = le16toh(panel.ps_mmr3);
            
            char addr_bin[23], data_bin[17], psw_bin[17], mmr0_bin[17], mmr3_bin[17];
            
            /* Format each field as binary */
            format_binary(panel.ps_address & 0x3FFFFF, 22, addr_bin);  /* 22-bit address */
            format_binary(panel.ps_data, 16, data_bin);
            format_binary(panel.ps_psw, 16, psw_bin);
            format_binary(panel.ps_mmr0, 16, mmr0_bin);
            format_binary(panel.ps_mmr3, 16, mmr3_bin);
            
            /* Print the frame data */
            printf("PDP-11: ADDR: %s, DATA: %s, PSW: %s, MMR0: %s, MMR3: %s\n",
                   addr_bin, data_bin, psw_bin, mmr0_bin, mmr3_bin);
            fflush(stdout);
            
            frame_count++;
        } else if (bytes_received == sizeof(struct netbsd_panel_state)) {
            /* NetBSD format - only 2 fields */
            struct netbsd_panel_state netbsd_panel;
            memcpy(&netbsd_panel, &panel, sizeof(netbsd_panel));
            
            char addr_bin[65], data_bin[65];  /* 64-bit fields */
            
            /* Format each field as binary (show lower 32 bits for readability) */
            format_binary((uint32_t)(netbsd_panel.ps_address & 0xFFFFFFFF), 32, addr_bin);
            format_binary((uint32_t)(netbsd_panel.ps_data & 0xFFFFFFFF), 32, data_bin);
            
            /* Print the frame data */
            printf("NetBSD: ADDR: %s, DATA: %s\n", addr_bin, data_bin);
            fflush(stdout);
            
            frame_count++;
        } else {
            /* Incomplete panel data received - print diagnostic info */
            printf("[Got %d bytes, expected %d (PDP-11) or %d (NetBSD) bytes from %s:%d]\n",
                   bytes_received, (int)sizeof(panel), (int)sizeof(struct netbsd_panel_state),
                   inet_ntoa(client_addr.sin_addr), 
                   ntohs(client_addr.sin_port));
            fflush(stdout);
        }
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

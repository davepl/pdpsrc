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

#define SERVER_PORT 8080
#define FRAME_SIZE 16

/* Global variables for signal handling */
static int server_sockfd = -1;

/* Function prototypes */
int create_udp_server_socket(void);
void handle_udp_clients(int sockfd);
void signal_handler(int sig);
void setup_signal_handlers(void);

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
    
    return sockfd;
}

void handle_udp_clients(int sockfd)
{
    short frame[FRAME_SIZE];
    int bytes_received;
    int frame_count = 0;
    int stars_on_line = 0;
    struct sockaddr_in client_addr;
    int client_addr_len;
    
    printf("Receiving UDP frames (each * = 1 frame):\n");
    
    while (1) {
        /* Receive UDP frame */
        client_addr_len = sizeof(client_addr);
        bytes_received = recvfrom(sockfd, frame, sizeof(frame), 0,
                                  (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (bytes_received < 0) {
            if (errno == EINTR) {
                /* Interrupted by signal, continue */
                continue;
            }
            perror("recvfrom");
            break;
        }
        
        /* Check if we received a complete frame */
        if (bytes_received == sizeof(frame)) {
            /* Print a star for each frame */
            printf("*");
            fflush(stdout);
            
            frame_count++;
            stars_on_line++;
            
            /* Print newline every 60 stars for readability */
            if (stars_on_line >= 60) {
                printf("\n");
                stars_on_line = 0;
            }
            
            /* Print frame count every 150 frames (10 seconds at 15 fps) */
            if (frame_count % 150 == 0) {
                printf(" [%d frames from %s:%d]\n", 
                       frame_count,
                       inet_ntoa(client_addr.sin_addr), 
                       ntohs(client_addr.sin_port));
                stars_on_line = 0;
            }
        } else {
            /* Incomplete frame received */
            printf("?");
            fflush(stdout);
        }
    }
    
    printf("\nTotal frames received: %d\n", frame_count);
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

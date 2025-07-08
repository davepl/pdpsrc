/*
 * client.c - Socket client for PDP-11 load testing
 * Sends 16-word frames to server 30 times per second
 * Compatible with K&R C and 211BSD
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define SERVER_PORT 8080
#define FRAME_SIZE 16
#define FRAMES_PER_SECOND 30
#define USEC_PER_FRAME (1000000 / FRAMES_PER_SECOND)

/* Function prototypes */
int create_udp_socket(char *server_ip, struct sockaddr_in *server_addr);
void send_frames(int sockfd, struct sockaddr_in *server_addr);
void usage(char *progname);
void precise_delay(long usec);

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
    
    printf("Connecting to server at %s:%d via UDP\n", server_ip, SERVER_PORT);
    
    /* Create UDP socket and set up server address */
    sockfd = create_udp_socket(server_ip, &server_addr);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
        exit(1);
    }
    
    printf("Connected successfully. Sending frames at %d Hz...\n", FRAMES_PER_SECOND);
    
    /* Send frames continuously */
    send_frames(sockfd, &server_addr);
    
    close(sockfd);
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
    short frame[FRAME_SIZE];
    int i;
    int frame_count = 0;
    
    /* Initialize frame with test data */
    for (i = 0; i < FRAME_SIZE; i++) {
        frame[i] = i + 1;
    }
    
    while (1) {
        /* Send frame via UDP */
        if (sendto(sockfd, frame, sizeof(frame), 0, 
                   (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
            perror("sendto");
            break;
        }
        
        frame_count++;
        if (frame_count % (FRAMES_PER_SECOND * 10) == 0) {  /* Every 10 seconds */
            printf("Sent %d frames\n", frame_count);
        }
        
        /* Update frame data slightly */
        frame[0] = frame_count & 0xFFFF;
        
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

void precise_delay(long usec)
{
    struct timeval timeout;
    
    /* Convert microseconds to seconds and microseconds */
    timeout.tv_sec = usec / 1000000;
    timeout.tv_usec = usec % 1000000;
    
    /* Use select() with no file descriptors as a portable delay */
    select(0, NULL, NULL, NULL, &timeout);
}

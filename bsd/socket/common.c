/*
 * common.c - Common functions for socket client implementations
 * Compatible with K&R C, 211BSD, NetBSD x64, and NetBSD VAX
 */

#ifndef COMMON_HEADERS_INCLUDED
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#endif

/* Add close() declaration for older systems */
#ifdef __pdp11__
extern int close();
#else
#include <unistd.h>  /* For close() */
#endif

/* Common definitions */
#define FRAMES_PER_SECOND 60
#define USEC_PER_FRAME (1000000 / FRAMES_PER_SECOND)

/* Panel type enumeration for packet flags */
typedef enum {
    PANEL_PDP1170 = 1,      /* PDP-11/70 2.11BSD panel */
    PANEL_VAX = 2,          /* VAX NetBSD panel */
    PANEL_NETBSDX64 = 3,    /* NetBSD x64 panel */
    PANEL_MACOS = 4,        /* macOS panel */
    PANEL_LINUXX64 = 5      /* Linux x64 panel */
} panel_type_t;

/* Function implementations */
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

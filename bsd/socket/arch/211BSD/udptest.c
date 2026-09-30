/*
 * udptest.c - Simple UDP test client for 2.11BSD
 * Sends a test message to specified server and port
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *host;
    char *server_ip;
    int server_port;
    char message[] = "Test UDP message from 2.11BSD";
    int result;
    
    if (argc != 3) {
        printf("Usage: %s <server_ip> <port>\n", argv[0]);
        printf("Example: %s 192.168.1.219 4000\n", argv[0]);
        exit(1);
    }
    
    server_ip = argv[1];
    server_port = atoi(argv[2]);
    
    printf("Testing UDP connection to %s:%d\n", server_ip, server_port);
    
    /* Create UDP socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }
    
    /* Set up server address */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    /* Convert IP address */
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    if (server_addr.sin_addr.s_addr == -1) {
        /* Try to resolve hostname */
        host = gethostbyname(server_ip);
        if (host == NULL) {
            fprintf(stderr, "Invalid server address: %s\n", server_ip);
            close(sockfd);
            exit(1);
        }
        memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);
    }
    
    printf("Sending test message: '%s'\n", message);
    
    /* Send test message */
    result = sendto(sockfd, message, strlen(message), 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    if (result < 0) {
        perror("sendto failed");
        close(sockfd);
        exit(1);
    }
    
    printf("Successfully sent %d bytes to %s:%d\n", result, server_ip, server_port);
    printf("Check your server to see if the message was received.\n");
    
    close(sockfd);
    return 0;
}

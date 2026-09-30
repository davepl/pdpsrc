/* 
 * minimal_wget.c
 * 
 * A (very) minimal single-file "wget-like" HTTP client in old K&R C.
 * 
 * Example usage:
 *    cc minimal_wget.c -o minimal_wget
 *    ./minimal_wget example.com 80 /index.html
 *
 * It will print the HTTP response (headers + body) to stdout.
 *
 * Written to be as close to K&R as possible to compile on very old UNIX.
 * You may have to tweak headers or linking on 2.11BSD or other vintage OSes.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include "../pdp11_unistd.h"
#include <string.h>
#include <stdlib.h>

/* If your 2.11BSD system does not have <unistd.h> or <string.h>, remove them
   and use more traditional declarations (e.g. declare exit(), read(), etc. 
   manually). Also, <arpa/inet.h> might need to be replaced or removed 
   depending on what's available. Adjust as necessary. */

/* Buffer size for read/write operations */
#define BUFSIZE 1024

/* Function to connect to a given host+port; returns socket fd or -1 on error. */
int connect_to_host(host, port)
char *host;
int port;
{
    struct hostent *he;
    struct sockaddr_in sa;
    int sock;

    /* Look up host by name */
    he = gethostbyname(host);
    if (he == (struct hostent *)0) {
        fprintf(stderr, "Error: gethostbyname failed for %s\n", host);
        return -1;
    }

    /* Create a socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Error: socket creation failed.\n");
        return -1;
    }

    /* Zero out sockaddr_in */
    memset((char *)&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    /* Convert hostent->h_addr to in_addr (for IPv4) */
    memcpy((char *)&sa.sin_addr, (char *)he->h_addr, he->h_length);
    /* Convert port from host byte order to network byte order */
    sa.sin_port = htons((unsigned short)port);

    /* Attempt to connect */
    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "Error: connect failed.\n");
        close(sock);
        return -1;
    }

    return sock;
}

/* 
 * Minimal main: usage:
 *   minimal_wget <host> <port> <path>
 * Example:
 *   minimal_wget example.com 80 /index.html
 */
int main(argc, argv)
int argc;
char **argv;
{
    int sock, n;
    char sendbuf[BUFSIZE];
    char recvbuf[BUFSIZE];
    char *host;
    int port;
    char *path;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <path>\n", argv[0]);
        exit(1);
    }

    host = argv[1];
    port = atoi(argv[2]);
    path = argv[3];

    /* Connect to the server */
    sock = connect_to_host(host, port);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect.\n");
        exit(1);
    }

    /* Construct a minimal HTTP/1.0 GET request */
    /* We do NOT handle Host: header or anything beyond basics. */
    sprintf(sendbuf,
        "GET %s HTTP/1.0\r\n"
        "User-Agent: minimal-wget/0.1 (K&R)\r\n"
        "\r\n",
        path);

    /* Write request to the socket */
    write(sock, sendbuf, strlen(sendbuf));

    /* Read the response and print to stdout */
    while ((n = read(sock, recvbuf, BUFSIZE)) > 0) {
        write(1, recvbuf, n); /* write to file descriptor 1 (stdout) */
    }

    close(sock);
    return 0;
}


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define MAXLINE 1024
#define BUFSIZE 1024
#define MAXBUF (MAXLINE * 20)
#define SCREEN_WIDTH 80

int delay_set, debug;
float delay_time;
char buffer[MAXBUF];
int buf_len;
int col_pos;

struct tag_state {
    int bold;
    int center;
    int indent;
    int newline;
};

/* Fetch URL, handle basic redirect */
fetch_url(host, port, path, redirected)
char *host;
int port;
char *path;
int redirected;
{
    struct hostent *he;
    struct sockaddr_in sa;
    int sock, n;
    char sendbuf[BUFSIZE];
    char recvbuf[BUFSIZE];
    char *location;

    if (debug) printf("Fetching %s:%d%s\n", host, port, path);

    he = gethostbyname(host);
    if (he == (struct hostent *)0) {
        fprintf(stderr, "Cannot resolve %s\n", host);
        exit(1);
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Socket creation failed\n");
        exit(1);
    }

    memset((char *)&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    memcpy((char *)&sa.sin_addr, (char *)he->h_addr, he->h_length);
    sa.sin_port = htons((unsigned short)port);

    if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        fprintf(stderr, "Connect failed to %s:%d\n", host, port);
        close(sock);
        exit(1);
    }

    sprintf(sendbuf,
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: webdisplay/0.1 (211BSD)\r\n"
        "\r\n",
        path, host);

    if (debug) printf("Sent request:\n%s", sendbuf);

    write(sock, sendbuf, strlen(sendbuf));

    buf_len = 0;
    location = NULL;
    while ((n = read(sock, recvbuf, BUFSIZE - 1)) > 0) {
        recvbuf[n] = '\0';
        if (buf_len + n < sizeof(buffer)) {
            memcpy(buffer + buf_len, recvbuf, n);
            buf_len += n;
        }
        if (debug) printf("Received %d bytes\n", n);
        if (!redirected && strstr(recvbuf, "301 Moved") && (location = strstr(recvbuf, "Location: "))) {
            char new_host[MAXLINE], new_path[MAXLINE];
            int new_port = 80;
            location += 10;
            if (strncmp(location, "https", 5) == 0) {
                if (debug) printf("HTTPS redirect detected, stopping\n");
                break;  /* Can’t handle HTTPS */
            }
            if (sscanf(location, "http://%[^:/]%s", new_host, new_path) >= 1) {
                close(sock);
                if (debug) printf("Redirecting to %s%s\n", new_host, new_path ? new_path : "/");
                fetch_url(new_host, new_port, new_path ? new_path : "/", 1);
                return;
            }
        }
    }
    buffer[buf_len] = '\0';
    if (debug) printf("Buffer contains %d bytes\n", buf_len);
    close(sock);
}

/* Skip HTTP headers */
skip_headers()
{
    char *p;
    for (p = buffer; p < buffer + buf_len - 4; p++) {
        if (strncmp(p, "\r\n\r\n", 4) == 0) {
            return p + 4 - buffer;
        }
    }
    return 0;
}

/* Display buffer with Google-specific parsing */
display_buffer(start)
int start;
{
    struct tag_state state;
    char *p, *tag_start, *tag_end;
    int in_tag, i, in_script, in_style;

    state.bold = 0;
    state.center = 0;
    state.indent = 0;
    state.newline = 1;
    col_pos = 0;
    in_script = 0;
    in_style = 0;

    in_tag = 0;
    p = buffer + start;
    while (p < buffer + buf_len && *p) {
        if (*p == '<') {
            in_tag = 1;
            tag_start = p + 1;
            p++;
            continue;
        }
        if (*p == '>' && in_tag) {
            in_tag = 0;
            tag_end = p;

            if (*tag_start == '/') {
                tag_start++;
                if (strncmp(tag_start, "p", tag_end - tag_start) == 0 ||
                    strncmp(tag_start, "div", tag_end - tag_start) == 0) {
                    state.newline = 1;
                    state.indent = 0;
                }
                else if (strncmp(tag_start, "b", tag_end - tag_start) == 0) {
                    state.bold = 0;
                }
                else if (strncmp(tag_start, "center", tag_end - tag_start) == 0) {
                    state.center = 0;
                }
                else if (strncmp(tag_start, "script", tag_end - tag_start) == 0) {
                    in_script = 0;
                }
                else if (strncmp(tag_start, "style", tag_end - tag_start) == 0) {
                    in_style = 0;
                }
            } else {
                if (strncmp(tag_start, "p", 1) == 0) {
                    state.newline = 1;
                    state.indent = 2;
                }
                else if (strncmp(tag_start, "br", 2) == 0) {
                    state.newline = 1;
                }
                else if (strncmp(tag_start, "div", 3) == 0) {
                    if (strstr(tag_start, "id=\"gbar\"")) state.newline = 1;  /* Top nav */
                    else if (strstr(tag_start, "id=\"guser\"")) state.newline = 1;
                    else if (strstr(tag_start, "id=\"gog\"")) state.center = 1;  /* Logo */
                }
                else if (strncmp(tag_start, "h1", 2) == 0) {
                    state.bold = 1;
                    state.newline = 1;
                    state.indent = 0;
                }
                else if (strncmp(tag_start, "b", 1) == 0) {
                    state.bold = 1;
                }
                else if (strncmp(tag_start, "center", 6) == 0) {
                    state.center = 1;
                }
                else if (strncmp(tag_start, "script", 6) == 0) {
                    in_script = 1;
                }
                else if (strncmp(tag_start, "style", 5) == 0) {
                    in_style = 1;
                }
            }
            p++;
            continue;
        }
        if (!in_tag && !in_script && !in_style && *p != '\r' && *p != '\n') {
            if (state.newline) {
                putchar('\n');
                col_pos = 0;
                for (i = 0; i < state.indent; i++) {
                    putchar(' ');
                    col_pos++;
                }
                state.newline = 0;
            }
            if (state.center && col_pos == 0) {
                int len = text_length(p);
                int spaces = (SCREEN_WIDTH - len) / 2;
                if (spaces > 0) {
                    for (i = 0; i < spaces; i++) {
                        putchar(' ');
                        col_pos++;
                    }
                }
            }
            putchar(*p);
            col_pos++;
            if (col_pos >= SCREEN_WIDTH) {
                state.newline = 1;
            }
        }
        p++;
    }
    putchar('\n');
}

/* Estimate text length */
text_length(p)
char *p;
{
    int len;
    len = 0;
    while (*p && *p != '<' && *p != '\n' && *p != '\r' && len < SCREEN_WIDTH) {
        len++;
        p++;
    }
    return len;
}

/* Sleep function */
sleep_delay(seconds)
float seconds;
{
    struct timeval tv;
    tv.tv_sec = (int)seconds;
    tv.tv_usec = (int)((seconds - (float)tv.tv_sec) * 1000000.0);
    select(0, 0, 0, 0, &tv);
}

/* Main function */
main(argc, argv)
int argc;
char **argv;
{
    char *host, *path;
    int port, optind, start;

    delay_set = 0;
    debug = 0;
    delay_time = 2.0;
    optind = 1;
    buf_len = 0;

    while (optind < argc && argv[optind][0] == '-') {
        if (strcmp(argv[optind], "-t") == 0) {
            if (optind + 1 >= argc) {
                printf("Usage: %s [-t delay] [-d] [host [port path]]\n", argv[0]);
                exit(1);
            }
            delay_set = 1;
            delay_time = atof(argv[optind + 1]);
            if (delay_time <= 0) {
                printf("Delay must be positive\n");
                exit(1);
            }
            optind += 2;
        } else if (strcmp(argv[optind], "-d") == 0) {
            debug = 1;
            optind++;
        } else {
            printf("Usage: %s [-t delay] [-d] [host [port path]]\n", argv[0]);
            exit(1);
        }
    }

    if (optind < argc) {
        host = argv[optind];
        port = (optind + 1 < argc) ? atoi(argv[optind + 1]) : 80;
        path = (optind + 2 < argc) ? argv[optind + 2] : "/";
        if (strcmp(host, "google.com") == 0) host = "www.google.com";
    } else {
        host = NULL;
    }

    while (1) {
        printf("\033[2J\033[H");

        if (host) {
            fetch_url(host, port, path, 0);
            start = skip_headers();
            if (start < buf_len) {
                display_buffer(start);
            } else {
                printf("No body found in response\n");
            }
        } else {
            char line[MAXLINE];
            while (fgets(line, MAXLINE, stdin) != NULL) {
                char *p;
                int in_tag = 0;
                for (p = line; *p; p++) {
                    if (*p == '<') in_tag = 1;
                    else if (*p == '>') in_tag = 0;
                    else if (!in_tag && *p != '\n') putchar(*p);
                }
            }
        }

        fflush(stdout);

        if (delay_set) {
            sleep_delay(delay_time);
        }
        if (!host) {
            break;
        }
    }
    exit(0);
}
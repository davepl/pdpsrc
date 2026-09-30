/* Native 2.11BSD CGI: one bounded request to the shared counter on caddy.
 * No input, arguments, retry, floating point, or local counter mutation.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static int failed()
{
    printf("HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nConnection: close\r\n\r\nCount unavailable.\n");
    return 1;
}

int main()
{
    struct sockaddr_in addr;
    int fd, n, used, sent, len, i;
    char buf[4096], *body;
#ifdef READ_ONLY
    static char request[] = "GET /visits.txt HTTP/1.0\r\nHost: pdp1173.com\r\nConnection: close\r\n\r\n";
#else
    static char request[] = "GET /cgi-bin/visit HTTP/1.0\r\nHost: pdp1173.com\r\nConnection: close\r\n\r\n";
#endif
    alarm(10);
    memset((char *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = inet_addr("192.168.1.45");
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return failed();
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return failed();
    }
    len = strlen(request);
    for (sent = 0; sent < len; sent += n) {
        n = write(fd, request + sent, len - sent);
        if (n <= 0) { close(fd); return failed(); }
    }
    used = 0;
    while (used < sizeof(buf)-1 && (n = read(fd, buf+used, sizeof(buf)-1-used)) > 0)
        used += n;
    close(fd);
    if (n < 0 || used < 14 || used >= sizeof(buf)-1) return failed();
    buf[used] = 0;
    if (strncmp(buf, "HTTP/1.", 7) || buf[8] != ' ' ||
        strncmp(buf+9, "200 ", 4)) return failed();
    body = strstr(buf, "\r\n\r\n");
    if (body == NULL) return failed();
    body += 4;
    len = strlen(body);
    if (len < 2 || len > 32 || body[len-1] != '\n') return failed();
    for (i = 0; i < len-1; i++)
        if (body[i] < '0' || body[i] > '9') return failed();
    printf("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s", len, body);
    return 0;
}

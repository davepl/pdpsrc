/* The installed httpd rejects setuid CGI files and passes no environment.
 * This unprivileged launcher executes exactly one fixed helper, with no
 * arguments and a fixed environment. The helper supplies the HTTP response.
 */
#include <stdio.h>
#include <unistd.h>
int main()
{
    static char *args[] = { "webtop", 0 };
    static char *env[] = { "GATEWAY_INTERFACE=CGI/1.1", "REQUEST_METHOD=GET", 0 };
    execve("/usr/local/libexec/webtop", args, env);
    printf("HTTP/1.0 503 Service Unavailable\nContent-Type: text/plain\nCache-Control: no-store\n\nSnapshot unavailable.\n");
    return 1;
}

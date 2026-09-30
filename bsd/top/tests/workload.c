/*
 * A bounded, opt-in native workload for top's process-state tests.
 * No files or unrelated processes are changed. The parent retains every
 * direct child's PID until cleanup, so an exited child's PID cannot be
 * reused before we signal/reap it. One zombie is intentional until then.
 */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../pdp11_unistd.h"

#define IDLE_CHILDREN 33
#define STOPPED_CHILD IDLE_CHILDREN
#define ZOMBIE_CHILD (IDLE_CHILDREN + 1)
#define CHURN_CHILD (IDLE_CHILDREN + 2)
#define CHILDREN (IDLE_CHILDREN + 3)

extern pid_t fork(), wait(), getpid();
int kill(), pause();

static pid_t children[CHILDREN];
static int created;
static volatile int finishing;

/* A signal can arrive during fork or stdio. Do cleanup in ordinary code,
 * after the newly returned child PID has been recorded. */
static void
finish(sig)
int sig;
{
    finishing = 1;
}

static void
child(role, seconds)
int role, seconds;
{
    pid_t pid, reaped;
    int status;

    signal(SIGINT, SIG_DFL);
    /* Ctrl-\ goes to the whole terminal group. Let the parent handle it;
     * default SIGQUIT in every child could create unwanted core files. */
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGALRM, SIG_DFL);
    /* Sleeping children have a fallback lifetime if the parent disappears.
     * The stopped child is explicitly killed by the parent's cleanup. */
    alarm(seconds + 60);
    if (role == ZOMBIE_CHILD) _exit(0);
    if (role == STOPPED_CHILD) kill(getpid(), SIGSTOP);
    if (role != CHURN_CHILD)
        for (;;) pause();

    /* A separate manager can reap churn children without accidentally
     * reaping the intentional zombie that belongs to the main parent.
     * Its short-lived children exit immediately, with no ongoing work. */
    for (;;) {
        pid = fork();
        if (pid == 0) _exit(0);
        if (pid > 0) {
            do {
                reaped = wait(&status);
            } while (reaped < 0 && errno == EINTR);
        }
        sleep(1);
    }
}

static void
cleanup()
{
    int i, status;
    pid_t pid;

    /* SIGKILL also terminates a stopped child; SIGTERM alone would stay
     * pending until that child was continued. Only recorded children are
     * targeted, and none has been reaped yet, so PID reuse is impossible. */
    for (i = 0; i < created; i++) kill(children[i], SIGKILL);
    for (;;) {
        pid = wait(&status);
        if (pid > 0 || (pid < 0 && errno == EINTR)) continue;
        break;
    }
}

int
main(argc, argv)
int argc;
char **argv;
{
    int i, seconds, result;
    long requested;
    char *end;
    pid_t pid;
    time_t now, deadline;

    seconds = 30;
    if (argc > 2) {
        fprintf(stderr, "usage: top-workload [seconds: 5..300]\n");
        return 1;
    }
    if (argc == 2) {
        requested = strtol(argv[1], &end, 10);
        if (!*argv[1] || *end || requested < 5 || requested > 300) {
            fprintf(stderr, "usage: top-workload [seconds: 5..300]\n");
            return 1;
        }
        seconds = requested;
    }
    signal(SIGINT, finish);
    signal(SIGQUIT, finish);
    signal(SIGTERM, finish);
    signal(SIGHUP, finish);
    signal(SIGALRM, finish);
    signal(SIGPIPE, SIG_IGN);
    /* Retain the intentional zombie even if our launcher ignored SIGCHLD. */
    signal(SIGCHLD, SIG_DFL);
    result = 0;
    for (i = 0; i < CHILDREN && !finishing; i++) {
        pid = fork();
        if (pid < 0) { perror("top-workload: fork"); result = 1; break; }
        if (pid == 0) child(i, seconds);
        children[created++] = pid;
    }
    if (created == CHILDREN && !finishing) {
        printf("top-workload: parent %d, %d sleeping children\n",
            (int)getpid(), IDLE_CHILDREN);
        printf("stopped PID %d; zombie PID %d; churn manager PID %d\n",
            (int)children[STOPPED_CHILD], (int)children[ZOMBIE_CHILD],
            (int)children[CHURN_CHILD]);
        printf("running for %d seconds; Ctrl-C cleans up early\n", seconds);
        fflush(stdout);
        /* Wall-clock deadline avoids accumulating delays from interrupted
         * sleeps; the one-second wait also avoids a signal/pause race. */
        time(&now);
        deadline = now + seconds;
        while (!finishing && now < deadline) {
            sleep(1);
            time(&now);
        }
    }
    cleanup();
    printf("top-workload: cleaned up %d direct children\n", created);
    return result;
}

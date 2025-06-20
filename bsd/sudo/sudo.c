/*
 * A *very* minimal sudo-like utility for 2.11BSD
 *
 * Build:
 *     cc -o sudo sudo.c
 *     chown root sudo
 *     chmod 4755 sudo
 *
 * Warning:
 *     This does no real command-specific restrictions.
 *     Everyone in group 0 (wheel) can sudo to root without additional checks.
 *     For real usage, you'd parse /etc/sudoers or similar for finer control.
 */

#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>

static void usage(progname) 
char *progname;
{
    fprintf(stderr, "Usage: %s [-u <user>] <command> [args...]\n", progname);
    exit(1);
}

int main(argc, argv)
int argc;
char *argv[];
{
    struct passwd *pwd_cur;   /* user invoking sudo */
    struct passwd *pwd_tgt;   /* target user (defaults to root) */
    char *target_user = "root";
    char *cmd;
    int i;
    int arg_offset = 1;       /* index in argv for the command */

    openlog("sudo", LOG_ODELAY, LOG_AUTH);

    /* Parse arguments:
     *   sudo [-u <user>] <command> [args...]
     */
    if (argc < 2) {
        usage(argv[0]);
    }

    /* Check for -u <user> option */
    if (strcmp(argv[1], "-u") == 0) {
        if (argc < 4) {
            usage(argv[0]);
        }
        target_user = argv[2];
        arg_offset = 3; /* command begins at argv[3] */
    }

    if (arg_offset >= argc) {
        usage(argv[0]);
    }

    cmd = argv[arg_offset];
    if (!cmd || !*cmd) {
        usage(argv[0]);
    }

    /* Who is calling sudo? */
    if ((pwd_cur = getpwuid(getuid())) == NULL) {
        fprintf(stderr, "sudo: Who are you?\n");
        exit(1);
    }

    /* Who do we want to become? */
    if ((pwd_tgt = getpwnam(target_user)) == NULL) {
        fprintf(stderr, "sudo: Unknown user: %s\n", target_user);
        exit(1);
    }

    /* Example policy: only allow members of group 0 to sudo to root.
     * If you want to prompt for a password, do it here (similar to su).
     */
    if (pwd_tgt->pw_uid == 0) {
        /* target is root, check if caller is in group 0 */
        struct group *gr = getgrgid(0);
        int is_wheel = 0;
        if (gr && gr->gr_mem) {
            for (i = 0; gr->gr_mem[i] != NULL; i++) {
                if (strcmp(pwd_cur->pw_name, gr->gr_mem[i]) == 0) {
                    is_wheel = 1;
                    break;
                }
            }
        }
        if (!is_wheel && getuid() != 0) {
            fprintf(stderr, "sudo: you are not in group 0 (wheel)\n");
            syslog(LOG_CRIT, "BAD SUDO attempt by %s on %s",
                   pwd_cur->pw_name, ttyname(STDERR_FILENO));
            exit(1);
        }
        /* else if you want a password check, do it here:
         *
         *   char *p = getpass("Password: ");
         *   if (strcmp(pwd_tgt->pw_passwd, crypt(p, pwd_tgt->pw_passwd)) != 0) {
         *       fprintf(stderr, "Sorry\n");
         *       syslog(LOG_CRIT, "BAD SUDO password by %s", pwd_cur->pw_name);
         *       exit(1);
         *   }
         */
    }

    /* If we get here, we allow the sudo. */
    syslog(LOG_NOTICE, "%s running command as %s", pwd_cur->pw_name, target_user);

    /* Drop to the target user's groups/uid/gid. */
    if (setgid(pwd_tgt->pw_gid) < 0) {
        perror("sudo: setgid");
        exit(2);
    }
    if (initgroups(pwd_tgt->pw_name, pwd_tgt->pw_gid) < 0) {
        fprintf(stderr, "sudo: initgroups failed\n");
        exit(2);
    }
    if (setuid(pwd_tgt->pw_uid) < 0) {
        perror("sudo: setuid");
        exit(2);
    }

    /* Build new argv[] for exec */
    /* We want to pass everything after the command to exec, i.e.:
     *   sudo [-u user] cmd arg1 arg2 ...
     * becomes:
     *   execv("cmd", ["cmd", "arg1", "arg2", ...]);
     */
    {
        int new_argc = argc - arg_offset;
        char **new_argv = (char **) malloc((new_argc + 1) * sizeof(char *));
        if (!new_argv) {
            fprintf(stderr, "sudo: malloc failed\n");
            exit(2);
        }
        for (i = 0; i < new_argc; i++) {
            new_argv[i] = argv[arg_offset + i];
        }
        new_argv[new_argc] = NULL;

        /* Exec the command */
        execvp(cmd, new_argv);
        fprintf(stderr, "sudo: exec of %s failed: %s\n", cmd, strerror(errno));

        free(new_argv);
        exit(1);
    }
    /* NOTREACHED */
}


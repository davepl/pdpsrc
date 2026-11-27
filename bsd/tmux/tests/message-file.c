/*
 * Error text read from the message file.
 *
 * Linked with the milestone objects built from the generated copies, in
 * which message literals are tmux_msg(offset) calls. TMUX_MSGFILE names the
 * generated native/tmux.msg; argv[1] is a directory this test may write in.
 */
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux-full.h"

#ifdef NDEBUG
#error These regression checks require assertions.
#endif

extern char	 tmux_msg_stamp[];
char		*tmux_msg(u_int);
void		 tmux_msg_init(const char *);

#define HEAD 17

static char	*file;
static size_t	 filesize;
static char	 last_error[256];

static void
load(const char *path)
{
	FILE	*f;

	f = fopen(path, "rb");
	assert(f != NULL);
	file = malloc(65536);
	filesize = fread(file, 1, 65536, f);
	fclose(f);
	assert(filesize > HEAD && filesize < 65536);
}

static void
capture_error(unused struct cmd_ctx *ctx, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(last_error, sizeof last_error, fmt, ap);
	va_end(ap);
}

static void
ignore_print(unused struct cmd_ctx *ctx, unused const char *fmt, ...)
{
}

/* Every message in the file comes back whole, and errno is not disturbed. */
static u_int
check_every_message(void)
{
	size_t	 offset, length;
	u_int	 count;
	char	*text;

	assert(memcmp(file, "tmuxmsg ", 8) == 0 && file[HEAD - 1] == '\n');
	assert(memcmp(file + 8, tmux_msg_stamp, 8) == 0);
	count = 0;
	for (offset = HEAD; offset < filesize; offset += length + 1) {
		length = strlen(file + offset);
		assert(length >= 8 && length <= 95);
		errno = ENOTTY;
		text = tmux_msg((u_int)offset);
		assert(errno == ENOTTY);
		assert(strcmp(text, file + offset) == 0);
		count++;
	}
	assert(count > 50);
	return (count);
}

/* A real command error arrives as its text, formatted with its argument. */
static void
check_command_error(void)
{
	struct cmd_ctx	 ctx;
	struct cmd_list	*cmdlist;
	char		*cause;

	memset(&ctx, 0, sizeof ctx);
	ctx.error = capture_error;
	ctx.print = ignore_print;
	ctx.info = ignore_print;

	cause = NULL;
	assert(cmd_string_parse(
	    "kill-session -t nosuchsession", &cmdlist, &cause) == 0);
	assert(cmdlist != NULL && cause == NULL);
	last_error[0] = '\0';
	assert(cmd_list_exec(cmdlist, &ctx) == -1);
	cmd_list_free(cmdlist);
	if (strcmp(last_error, "session not found: nosuchsession") != 0) {
		fprintf(stderr, "command error was \"%s\"\n", last_error);
		abort();
	}
}

/* Missing, foreign and truncated files give the number, never a format. */
static void
check_fallback(const char *directory)
{
	char	 path[512], *text;
	int	 fd;

	/* The path was fixed at the first call; put other files in its place. */
	snprintf(path, sizeof path, "%s/tmux.msg", directory);
	assert(strcmp(getenv("TMUX_MSGFILE"), path) == 0);

	assert(unlink(path) == 0);
	errno = EDOM;
	text = tmux_msg(1234);
	assert(errno == EDOM);
	assert(strcmp(text, "error 1234 (no tmux.msg for this build)") == 0);
	assert(strchr(text, '%') == NULL);

	/* The right layout from a different build: its stamp differs. */
	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	assert(fd != -1);
	file[8] = file[8] == '0' ? '1' : '0';
	assert(write(fd, file, filesize) == (ssize_t)filesize);
	close(fd);
	assert(strncmp(tmux_msg(HEAD), "error 17 (", 10) == 0);

	/* The right stamp again, and every message is back. */
	file[8] = tmux_msg_stamp[0];
	fd = open(path, O_WRONLY|O_TRUNC);
	assert(fd != -1);
	assert(write(fd, file, filesize) == (ssize_t)filesize);
	close(fd);
	assert(strcmp(tmux_msg(HEAD), file + HEAD) == 0);

	/* An offset past the end, and a file cut short of its header. */
	assert(strncmp(tmux_msg(60000), "error 60000 (", 13) == 0);
	fd = open(path, O_WRONLY|O_TRUNC);
	assert(fd != -1);
	assert(write(fd, file, 10) == 10);
	close(fd);
	assert(strncmp(tmux_msg(HEAD), "error 17 (", 10) == 0);
}

/*
 * Without the variable, the file is beside the executable: here argv[0] is
 * relative, as when tmux is started as ./tmux from its build directory.
 */
static void
check_beside_executable(const char *directory)
{
	char	path[512];

	snprintf(path, sizeof path, "%s/tmux.msg", directory);
	load(path);
	assert(chdir(directory) == 0);
	assert(unsetenv("TMUX_MSGFILE") == 0);
	tmux_msg_init("./tmux");
	assert(strcmp(tmux_msg(HEAD), file + HEAD) == 0);
	puts("message file found beside a relative executable path");
}

int
main(int argc, char **argv)
{
	char	path[512], command[1200];
	u_int	count;

	if (argc == 3 && strcmp(argv[2], "beside") == 0) {
		check_beside_executable(argv[1]);
		return (0);
	}
	assert(argc == 2 && getenv("TMUX_MSGFILE") != NULL);
	load(getenv("TMUX_MSGFILE"));

	/* Work on a copy, found the way an installed executable finds it. */
	snprintf(path, sizeof path, "%s/tmux.msg", argv[1]);
	snprintf(command, sizeof command, "cp '%s' '%s'",
	    getenv("TMUX_MSGFILE"), path);
	assert(system(command) == 0);
	setenv("TMUX_MSGFILE", path, 1);
	tmux_msg_init("ignored-when-the-variable-is-set");

	grid_init_default();
	count = check_every_message();
	check_command_error();
	check_fallback(argv[1]);
	printf("message file: %u messages read back, a command error "
	    "formatted, numbered fallback for missing and foreign files\n",
	    count);
	return (0);
}

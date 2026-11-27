/*
 * Exercise milestone payload borrowing through the actual imsg dispatcher.
 * Link a private server-client.o compiled with
 * -Dcmd_list_exec=dispatch_test_cmd_list_exec, excluding normal server-client.o.
 * All other program objects, including cmd-list.o, remain unmodified.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux-full.h"

#ifndef TMUX_MILESTONE
#error Run this regression with the milestone profile enabled.
#endif
#ifndef TMUX_SMALL_MEMORY
#error Run this regression with the small-memory profile enabled.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

int server_client_msg_dispatch(struct client *);
void server_clean_dead(void);

enum execution_mode {
	EXEC_REAL,
	EXEC_PENDING,
	EXEC_ATTACHED_DONE,
	EXEC_ATTACHED_PENDING
};
static enum execution_mode execution_mode;
static struct session dummy_session;
static struct environ copied_environment;

/* Keep real command dispatch except for controlled attachment outcomes. */
int
dispatch_test_cmd_list_exec(struct cmd_list *commands, struct cmd_ctx *ctx)
{
	if (execution_mode == EXEC_REAL)
		return (cmd_list_exec(commands, ctx));
	assert(ctx->cmdclient != NULL);
	if (execution_mode == EXEC_PENDING) {
		assert(ctx->cmdclient->session == NULL);
		return (1);
	}
	ctx->cmdclient->session = &dummy_session;
	if (execution_mode == EXEC_ATTACHED_DONE)
		return (0);
	assert(execution_mode == EXEC_ATTACHED_PENDING);
	environ_copy(&ctx->cmdclient->environ, &copied_environment);
	return (1);
}

struct dispatch_pair {
	int sockets[2];
	struct imsgbuf peer;
	struct client client;
};

static void
pair_init(struct dispatch_pair *pair)
{
	memset(pair, 0, sizeof *pair);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair->sockets) == 0);
	imsg_init(&pair->peer, pair->sockets[0]);
	imsg_init(&pair->client.ibuf, pair->sockets[1]);
	environ_init(&pair->client.environ);
	pair->client.tty.fd = -1;
}

static void
pair_free(struct dispatch_pair *pair)
{
	imsg_clear(&pair->peer);
	imsg_clear(&pair->client.ibuf);
	environ_free(&pair->client.environ);
	assert(close(pair->sockets[0]) == 0);
	assert(close(pair->sockets[1]) == 0);
}

static void
dispatch_packet(struct dispatch_pair *pair, enum msgtype type,
    void *data, size_t length)
{
	assert(imsg_compose(&pair->peer, type, PROTOCOL_VERSION, -1,
	    -1, data, length) == 1);
	assert(imsg_flush(&pair->peer) == 0);
	assert(pair->peer.w.queued == 0);
	assert(server_client_msg_dispatch(&pair->client) == 0);
	assert(pair->client.ibuf.r.wpos == 0);
}

/* Reuse freed payload-sized allocations before inspecting retained data. */
static void
check_environment(struct dispatch_pair *pair, const char *name,
    const char *value)
{
	struct environ_entry *entry;
	void *churn[32];
	size_t i;

	for (i = 0; i < nitems(churn); i++) {
		churn[i] = malloc(sizeof(struct msg_environ_data));
		assert(churn[i] != NULL);
		memset(churn[i], 'X', sizeof(struct msg_environ_data));
	}
	entry = environ_find(&pair->client.environ, name);
	assert(entry != NULL);
	assert(strcmp(entry->name, name) == 0);
	assert(entry->value != NULL && strcmp(entry->value, value) == 0);
	for (i = 0; i < nitems(churn); i++)
		free(churn[i]);
}

static void
check_environment_ownership(struct dispatch_pair *pair)
{
	struct msg_environ_data data;

	memset(&data, 0, sizeof data);
	strcpy(data.var, "DISPATCH_FIRST=first value");
	dispatch_packet(pair, MSG_ENVIRON, &data, sizeof data);
	memset(&data, 'Y', sizeof data);
	check_environment(pair, "DISPATCH_FIRST", "first value");

	memset(&data, 0, sizeof data);
	strcpy(data.var, "DISPATCH_SECOND=second value");
	dispatch_packet(pair, MSG_ENVIRON, &data, sizeof data);
	memset(&data, 'Z', sizeof data);
	check_environment(pair, "DISPATCH_FIRST", "first value");
	check_environment(pair, "DISPATCH_SECOND", "second value");

	memset(&data, 0, sizeof data);
	strcpy(data.var, "DISPATCH_FIRST=replaced value");
	dispatch_packet(pair, MSG_ENVIRON, &data, sizeof data);
	check_environment(pair, "DISPATCH_FIRST", "replaced value");
	check_environment(pair, "DISPATCH_SECOND", "second value");

	memset(&data, 0, sizeof data);
	strcpy(data.var, "DISPATCH_IGNORED");
	dispatch_packet(pair, MSG_ENVIRON, &data, sizeof data);
	assert(environ_find(&pair->client.environ, "DISPATCH_IGNORED") == NULL);
	assert(pair->client.ibuf.w.queued == 0);
}

static void
check_command_reply_ownership(struct dispatch_pair *pair)
{
	struct msg_command_data data;
	struct msg_print_data printed;
	struct imsg_hdr header;
	const struct cmd_entry **entry;
	struct buf *reply;
	char *argv[] = { "list-commands", NULL };
	char expected[PRINT_LENGTH];
	void *churn[32];
	size_t i, count;

	memset(&data, 0, sizeof data);
	data.pid = -1;
	data.argc = 1;
	assert(cmd_pack_argv(1, argv, data.argv, sizeof data.argv) == 0);
	dispatch_packet(pair, MSG_COMMAND, &data, sizeof data);
	memset(&data, 'Q', sizeof data);
	for (i = 0; i < nitems(churn); i++) {
		churn[i] = malloc(sizeof(struct msg_command_data));
		assert(churn[i] != NULL);
		memset(churn[i], 'R', sizeof(struct msg_command_data));
	}

	count = 0;
	reply = TAILQ_FIRST(&pair->client.ibuf.w.bufs);
	for (entry = cmd_table; *entry != NULL; entry++) {
		assert(reply != NULL && reply->rpos == 0 && reply->fd == -1);
		assert(reply->wpos == IMSG_HEADER_SIZE + sizeof printed);
		memcpy(&header, reply->buf, sizeof header);
		assert(header.type == MSG_PRINT && header.peerid == PROTOCOL_VERSION);
		assert(header.flags == 0 && header.len == reply->wpos);
		memcpy(&printed, reply->buf + IMSG_HEADER_SIZE, sizeof printed);
		assert(memchr(printed.msg, '\0', sizeof printed.msg) != NULL);
		assert(snprintf(expected, sizeof expected, "%s %s",
		    (*entry)->name, (*entry)->usage) < (int)sizeof expected);
		assert(strcmp(printed.msg, expected) == 0);
		reply = TAILQ_NEXT(reply, entry);
		count++;
	}
	assert(reply != NULL && reply->wpos == IMSG_HEADER_SIZE);
	memcpy(&header, reply->buf, sizeof header);
	assert(header.type == MSG_EXIT && header.peerid == PROTOCOL_VERSION);
	assert(header.flags == 0 && header.len == IMSG_HEADER_SIZE);
	assert(TAILQ_NEXT(reply, entry) == NULL);
	assert(pair->client.ibuf.w.queued == count + 1);
	for (i = 0; i < nitems(churn); i++)
		free(churn[i]);
	check_environment(pair, "DISPATCH_FIRST", "replaced value");
	check_environment(pair, "DISPATCH_SECOND", "second value");
}

static void
check_bad_payload_size(enum msgtype type)
{
	struct dispatch_pair pair;
	pid_t pid;
	int status;
	char bad = 0;

	pid = fork();
	assert(pid != -1);
	if (pid == 0) {
		alarm(5);
		log_close();
		pair_init(&pair);
		dispatch_packet(&pair, type, &bad, sizeof bad);
		_exit(99); /* A rejected payload must terminate through fatalx. */
	}
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
}

static void
send_list_command(struct dispatch_pair *pair)
{
	struct msg_command_data data;
	char *argv[] = { "list-commands", NULL };

	memset(&data, 0, sizeof data);
	data.pid = -1;
	data.argc = 1;
	assert(cmd_pack_argv(1, argv, data.argv, sizeof data.argv) == 0);
	dispatch_packet(pair, MSG_COMMAND, &data, sizeof data);
}

static void
check_attached_environment_release(void)
{
	struct dispatch_pair pair;
	struct environ_entry *entry;

	pair_init(&pair);
	check_environment_ownership(&pair);
	environ_init(&copied_environment);
	memset(&dummy_session, 0, sizeof dummy_session);

	/* An asynchronous command without attachment still needs its environment. */
	execution_mode = EXEC_PENDING;
	send_list_command(&pair);
	assert(pair.client.session == NULL && pair.client.ibuf.w.queued == 0);
	check_environment(&pair, "DISPATCH_FIRST", "replaced value");
	check_environment(&pair, "DISPATCH_SECOND", "second value");

	/* Attachment alone must not trigger the retval == 1 cleanup branch. */
	execution_mode = EXEC_ATTACHED_DONE;
	send_list_command(&pair);
	assert(pair.client.session == &dummy_session);
	assert(pair.client.ibuf.w.queued == 1);
	check_environment(&pair, "DISPATCH_FIRST", "replaced value");
	check_environment(&pair, "DISPATCH_SECOND", "second value");
	msgbuf_clear(&pair.client.ibuf.w);

	/* A session's independent copy survives releasing the launch environment. */
	execution_mode = EXEC_ATTACHED_PENDING;
	send_list_command(&pair);
	assert(pair.client.ibuf.w.queued == 0);
	assert(RB_EMPTY(&pair.client.environ));
	entry = environ_find(&copied_environment, "DISPATCH_FIRST");
	assert(entry != NULL && strcmp(entry->value, "replaced value") == 0);
	entry = environ_find(&copied_environment, "DISPATCH_SECOND");
	assert(entry != NULL && strcmp(entry->value, "second value") == 0);
	pair.client.session = NULL;
	environ_free(&copied_environment);
	execution_mode = EXEC_REAL;
	pair_free(&pair);
}

static void
check_lost_client_environment_release(void)
{
	struct client *client;
	int sockets[2];

	/* Exercise real status-screen allocation and teardown without any service. */
	ARRAY_INIT(&clients);
	ARRAY_INIT(&dead_clients);
	ARRAY_INIT(&sessions);
	ARRAY_INIT(&dead_sessions);
	ARRAY_INIT(&windows);
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	server_client_create(sockets[1]);
	assert(ARRAY_LENGTH(&clients) == 1);
	client = ARRAY_FIRST(&clients);
	assert(client != NULL && client->status.grid != NULL);
	environ_put(&client->environ, "DISPATCH_LOST=owned value");
	assert(!RB_EMPTY(&client->environ));
	server_client_lost(client);
	assert(ARRAY_FIRST(&clients) == NULL);
	assert(client->flags & CLIENT_DEAD);
	assert(RB_EMPTY(&client->environ));
	assert(ARRAY_LENGTH(&dead_clients) == 1);
	assert(ARRAY_FIRST(&dead_clients) == client);
	server_clean_dead();
	assert(ARRAY_FIRST(&dead_clients) == NULL);
	assert(close(sockets[0]) == 0);
	ARRAY_FREE(&clients);
	ARRAY_FREE(&dead_clients);
	ARRAY_FREE(&sessions);
	ARRAY_FREE(&dead_sessions);
	ARRAY_FREE(&windows);
}

int
main(void)
{
	struct dispatch_pair pair;

	/* Bound accidental blocking without creating a listener or daemon. */
	alarm(15);
	log_close();
	pair_init(&pair);
	check_environment_ownership(&pair);
	check_command_reply_ownership(&pair);
	pair_free(&pair);
	check_attached_environment_release();
	check_lost_client_environment_release();
	check_bad_payload_size(MSG_ENVIRON);
	check_bad_payload_size(MSG_COMMAND);
	alarm(0);
	puts("dispatch payload ownership, replies, environment cleanup and size rejection passed");
	return (0);
}

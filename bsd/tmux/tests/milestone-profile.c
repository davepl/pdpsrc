/* Link with actual TMUX_MILESTONE program objects and renamed tmux main. */
#include <sys/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux-full.h"

#ifndef TMUX_MILESTONE
#error Run this regression with the compact milestone profile enabled.
#endif
#ifdef NDEBUG
#error These regression checks require assertions.
#endif

char *status_prompt_complete(const char *);

static const char *expected_commands[] = {
	"attach-session", "bind-key", "command-prompt", "copy-mode",
	"detach-client", "display-message", "has-session", "kill-server",
	"kill-session", "kill-window", "last-window", "list-clients",
	"list-commands", "list-keys", "list-sessions", "list-windows",
	"new-session", "new-window", "next-window", "paste-buffer",
	"previous-window", "refresh-client", "rename-session",
	"rename-window", "select-window", "send-keys", "send-prefix",
	"unbind-key"
};

static int
entry_is_available(const struct cmd_entry *entry)
{
	const struct cmd_entry **p;

	for (p = cmd_table; *p != NULL; p++) {
		if (*p == entry)
			return (1);
	}
	return (0);
}

static void
check_command_table(void)
{
	const struct cmd_entry **p;
	const char *omitted[] = { "list-buffers", "split-window", "set-option",
	    "set-window-option", "clock-mode", "choose-window", "find-window",
	    "move-window", "select-prompt", "confirm-before" };
	struct cmd *cmd;
	char *argv[2], *cause;
	size_t i;

	for (i = 0, p = cmd_table; *p != NULL; p++, i++) {
		assert(i < nitems(expected_commands));
		assert(strcmp((*p)->name, expected_commands[i]) == 0);
	}
	assert(i == nitems(expected_commands));
	for (i = 0; i < nitems(omitted); i++) {
		argv[0] = (char *)omitted[i];
		argv[1] = NULL;
		cause = NULL;
		cmd = cmd_parse(1, argv, &cause);
		assert(cmd == NULL && cause != NULL);
		assert(strncmp(cause, "unknown command: ", 17) == 0);
		assert(strstr(cause, omitted[i]) != NULL);
		xfree(cause);
	}
}

static void
check_parse(int argc, char **argv, const char *name)
{
	struct cmd *cmd;
	char *cause;

	cause = NULL;
	cmd = cmd_parse(argc, argv, &cause);
	if (cmd == NULL)
		fprintf(stderr, "cannot parse %s: %s\n", name, cause);
	assert(cmd != NULL && cause == NULL);
	assert(strcmp(cmd->entry->name, name) == 0);
	cmd_free(cmd);
}

static void
check_cli_parsing(void)
{
	char *new_session[] = { "new-session", "-d", "-s", "milestone",
	    "-n", "first", NULL };
	char *new_window[] = { "new-window", "-t", "milestone", "-n",
	    "second", NULL };
	char *select_window[] = { "select-window", "-t", "milestone:1", NULL };
	char *rename_window[] = { "rename-window", "-t", "milestone:1",
	    "renamed", NULL };
	char *send_keys[] = { "send-keys", "-t", "milestone:1", "echo hi",
	    "Enter", NULL };
	char *bind_key[] = { "bind-key", "x", "kill-window", NULL };
	char *unbind_key[] = { "unbind-key", "x", NULL };
	char *attach[] = { "attach", NULL };
	char *detach[] = { "detach", NULL };
	char *copy_mode[] = { "copy-mode", "-u", "-t", "milestone:1", NULL };
	char *paste_buffer[] = { "pasteb", "-t", "milestone:1", NULL };

	check_parse(6, new_session, "new-session");
	check_parse(5, new_window, "new-window");
	check_parse(3, select_window, "select-window");
	check_parse(4, rename_window, "rename-window");
	check_parse(5, send_keys, "send-keys");
	check_parse(3, bind_key, "bind-key");
	check_parse(2, unbind_key, "unbind-key");
	check_parse(1, attach, "attach-session");
	check_parse(1, detach, "detach-client");
	check_parse(4, copy_mode, "copy-mode");
	check_parse(3, paste_buffer, "paste-buffer");
}

static void
check_binding(key_code key, const char *name)
{
	struct key_binding *binding;
	struct cmd *cmd;

	binding = key_bindings_lookup(key | KEYC_PREFIX);
	assert(binding != NULL);
	cmd = TAILQ_FIRST(binding->cmdlist);
	assert(cmd != NULL && TAILQ_NEXT(cmd, qentry) == NULL);
	assert(strcmp(cmd->entry->name, name) == 0);
}

static void
check_default_bindings(void)
{
	struct key_binding *binding;
	struct cmd *cmd;
	char printed[128];
	u_int count;
	key_code digit;

	key_bindings_init();
	count = 0;
	SPLAY_FOREACH(binding, key_bindings, &key_bindings) {
		assert(binding->key & KEYC_PREFIX);
		TAILQ_FOREACH(cmd, binding->cmdlist, qentry)
			assert(entry_is_available(cmd->entry));
		count++;
	}
	assert(count == 26);
	for (digit = '0'; digit <= '9'; digit++)
		check_binding(digit, "select-window");
	check_binding('c', "new-window");
	check_binding('d', "detach-client");
	check_binding('l', "last-window");
	check_binding('n', "next-window");
	check_binding('p', "previous-window");
	check_binding('n' | KEYC_ESCAPE, "next-window");
	check_binding('p' | KEYC_ESCAPE, "previous-window");
	check_binding(':', "command-prompt");
	check_binding(',', "command-prompt");
	check_binding('?', "list-keys");
	check_binding('i', "display-message");
	check_binding('r', "refresh-client");
	check_binding('\002', "send-prefix");
	check_binding('[', "copy-mode");
	check_binding(']', "paste-buffer");
	check_binding(KEYC_PPAGE, "copy-mode");
	binding = key_bindings_lookup(',' | KEYC_PREFIX);
	cmd_list_print(binding->cmdlist, printed, sizeof printed);
	assert(strstr(printed, "rename-window") != NULL);
	assert(key_bindings_lookup('.' | KEYC_PREFIX) == NULL);
	assert(key_bindings_lookup('f' | KEYC_PREFIX) == NULL);
	assert(key_bindings_lookup('&' | KEYC_PREFIX) == NULL);
	assert(key_bindings_lookup('#' | KEYC_PREFIX) == NULL);
	key_bindings_free();
}

static void
check_completion_and_modes(void)
{
	const struct mode_key_table *table;
	char *completed;
	u_int count;

	completed = status_prompt_complete("rename-w");
	assert(completed != NULL && strcmp(completed, "rename-window ") == 0);
	xfree(completed);
	completed = status_prompt_complete("copy-m");
	assert(completed != NULL && strcmp(completed, "copy-mode ") == 0);
	xfree(completed);
	assert(status_prompt_complete("set-o") == NULL);
	assert(status_prompt_complete("history-l") == NULL);
	mode_key_init_trees();
	assert(mode_key_findtable("vi-edit") != NULL);
	assert(mode_key_findtable("emacs-edit") != NULL);
	assert(mode_key_findtable("vi-choice") != NULL);
	assert(mode_key_findtable("emacs-choice") != NULL);
	/* mode-keys cannot be changed here, so only the emacs copy table. */
	assert(mode_key_findtable("vi-copy") == NULL);
	assert(mode_key_findtable("emacs-copy") != NULL);
	for (count = 0, table = mode_key_tables; table->name != NULL; table++)
		count++;
	assert(count == 5);
	mode_key_free_trees();
}

int
main(void)
{
	check_command_table();
	check_cli_parsing();
	check_default_bindings();
	check_completion_and_modes();
	puts("milestone commands, CLI parsing, defaults, completion and mode keys passed");
	return (0);
}

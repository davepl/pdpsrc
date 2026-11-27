/* Exercise status tokens, including tokens cut by the small input buffer. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "tmux-full.h"

static void
expect(struct client *client, const char *format, const char *wanted)
{
	char *actual = status_replace(client, format, 0);
	assert(strcmp(actual, wanted) == 0);
	xfree(actual);
}

int
main(void)
{
	struct client client;
	struct session session;
	struct winlink link;
	struct window window;
	struct window_pane pane;
	char format[131], wanted[131];

	memset(&client, 0, sizeof client);
	memset(&session, 0, sizeof session);
	memset(&link, 0, sizeof link);
	memset(&window, 0, sizeof window);
	memset(&pane, 0, sizeof pane);
	client.session = &session;
	session.curw = &link;
	session.name = "session";
	link.window = &window;
	link.idx = 3;
	window.name = "win";
	window.active = &pane;
	TAILQ_INIT(&window.panes);
	TAILQ_INSERT_TAIL(&window.panes, &pane, entry);
	pane.base.title = "title";

	expect(&client, "#S:#I:#P:#W:#T:##", "session:3:0:win:title:#");
	expect(&client, "#2W", "wi");
	expect(&client, "#[fg=red]text", "#[fg=red]text");
	expect(&client, "#", "");
	expect(&client, "a#", "a");
	expect(&client, "a#123", "a");
	expect(&client, "#X", "");
	expect(&client, "#[unterminated", "#[unterminated");

	memset(format, 'x', 126);
	strcpy(format + 126, "#W");
	memset(wanted, 'x', 126);
#ifdef TMUX_MILESTONE
	wanted[126] = '\0';
#else
	strcpy(wanted + 126, "win");
#endif
	expect(&client, format, wanted);
	memset(format, 'x', 124);
	strcpy(format + 124, "#123W");
	memset(wanted, 'x', 124);
#ifdef TMUX_MILESTONE
	wanted[124] = '\0';
	expect(&client, "%Y #W", "%Y win");
#else
	strcpy(wanted + 124, "win");
#endif
	expect(&client, format, wanted);
	return (0);
}

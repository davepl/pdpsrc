/* Compact PDP-11 menu profile, derived from upstream menu.c (GPL). */
#include "links.h"

/*static inline struct session *get_term_session(struct terminal *term)
{
	if ((void *)term->windows.prev == &term->windows) {
		internal("terminal has no windows");
		return NULL;
	}
	return ((struct window *)term->windows.prev)->data;
}*/

void menu_about(struct terminal *term, void *d, struct session *ses)
{
	msg_box(term, NULL, TEXT(T_ABOUT), AL_CENTER, TEXT(T_LINKS__LYNX_LIKE), NULL, 1, TEXT(T_OK), NULL, B_ENTER | B_ESC);
}

void menu_keys(struct terminal *term, void *d, struct session *ses)
{
	msg_box(term, NULL, TEXT(T_KEYS), AL_LEFT, TEXT(T_KEYS_DESC), NULL, 1, TEXT(T_OK), NULL, B_ENTER | B_ESC);
}

void menu_copying(struct terminal *term, void *d, struct session *ses)
{
	msg_box(term, NULL, TEXT(T_COPYING), AL_CENTER, TEXT(T_COPYING_DESC), NULL, 1, TEXT(T_OK), NULL, B_ENTER | B_ESC);
}

void menu_manual(struct terminal *term, void *d, struct session *ses)
{
	goto_url(ses, LINKS_MANUAL_URL);
}

void menu_for_frame(struct terminal *term, void (*f)(struct session *, struct f_data_c *, int), struct session *ses)
{
	do_for_frame(ses, f, 0);
}

void menu_goto_url(struct terminal *term, void *d, struct session *ses)
{
	dialog_goto_url(ses, "");
}

void menu_save_url_as(struct terminal *term, void *d, struct session *ses)
{
	dialog_save_url(ses);
}

void menu_go_back(struct terminal *term, void *d, struct session *ses)
{
	go_back(ses);
}

void menu_reload(struct terminal *term, void *d, struct session *ses)
{
	reload(ses, -1);
}

void really_exit_prog(struct session *ses)
{
	register_bottom_half((void (*)(void *))destroy_terminal, ses->term);
}

void dont_exit_prog(struct session *ses)
{
	ses->exit_query = 0;
}

void query_exit(struct session *ses)
{
	ses->exit_query = 1;
	msg_box(ses->term, NULL, TEXT(T_EXIT_LINKS), AL_CENTER, (ses->term->next == ses->term->prev && are_there_downloads()) ? TEXT(T_DO_YOU_REALLY_WANT_TO_EXIT_LINKS_AND_TERMINATE_ALL_DOWNLOADS) : TEXT(T_DO_YOU_REALLY_WANT_TO_EXIT_LINKS), ses, 2, TEXT(T_YES), (void (*)(void *))really_exit_prog, B_ENTER, TEXT(T_NO), dont_exit_prog, B_ESC);
}

void exit_prog(struct terminal *term, void *d, struct session *ses)
{
	if (!ses) {
		register_bottom_half((void (*)(void *))destroy_terminal, term);
		return;
	}
	if (!ses->exit_query && (!d || (term->next == term->prev && are_there_downloads()))) {
		query_exit(ses);
		return;
	}
	really_exit_prog(ses);
}

void go_backwards(struct terminal *term, void *psteps, struct session *ses)
{
	int steps = (int) psteps;

	/*if (ses->tq_goto_position)
		--steps;
	if (ses->search_word)
		mem_free(ses->search_word), ses->search_word = NULL;*/
	abort_loading(ses);

	while (steps > 1) {
		struct location *loc = ses->history.next;
		if ((void *) loc == &ses->history) return;
		loc = loc->next;
		if ((void *) loc == &ses->history) return;
		destroy_location(loc);

		--steps;
	}

	if (steps)
		go_back(ses);
}

struct menu_item no_hist_menu[] = {
	{ TEXT(T_NO_HISTORY), "", M_BAR, NULL, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 },
};

void history_menu(struct terminal *term, void *ddd, struct session *ses)
{
	struct location *l;
	struct menu_item *mi = NULL;
	int n = 0;
	foreach(l, ses->history) {
		if (n/* || ses->tq_goto_position*/) {
			unsigned char *url;
			if (!mi && !(mi = new_menu(3))) return;
			url = stracpy(l->vs.url);
			if (strchr(url, POST_CHAR)) *strchr(url, POST_CHAR) = 0;
			add_to_menu(&mi, url, "", "", MENU_FUNC go_backwards, (void *) n, 0);
		}
		n++;
	}
	if (n <= 1) do_menu(term, no_hist_menu, ses);
	else do_menu(term, mi, ses);
}

#ifndef PDP11_MINIMAL
struct menu_item no_downloads_menu[] = {
	{ TEXT(T_NO_DOWNLOADS), "", M_BAR, NULL, NULL, 0, 0 },
	{ NULL, NULL, 0, NULL, NULL, 0, 0 },
};

void downloads_menu(struct terminal *term, void *ddd, struct session *ses)
{
	struct download *d;
	struct menu_item *mi = NULL;
	int n = 0;
	foreachback(d, downloads) {
		unsigned char *u;
		if (!mi) if (!(mi = new_menu(3))) return;
		u = stracpy(d->url);
		if (strchr(u, POST_CHAR)) *strchr(u, POST_CHAR) = 0;
		add_to_menu(&mi, u, "", "", MENU_FUNC display_download, d, 0);
		n++;
	}
	if (!n) do_menu(term, no_downloads_menu, ses);
	else do_menu(term, mi, ses);
}

#else
void downloads_menu(struct terminal *term, void *data, struct session *ses)
{
 start_download(ses, NULL);
}
#endif

void menu_doc_info(struct terminal *term, void *ddd, struct session *ses)
{
	state_msg(ses);
}

void menu_head_info(struct terminal *term, void *ddd, struct session *ses)
{
	head_msg(ses);
}

void menu_toggle(struct terminal *term, void *ddd, struct session *ses)
{
	toggle(ses, ses->screen, 0);
}


/* Native compact menus retain browser/navigation dialogs. Configuration
 * remains available through command-line options and the configuration file.
 * There are deliberately no new-window, MIME-editor or settings dialogs. */
struct menu_item compact_file_menu[] = {
 { TEXT(T_GOTO_URL), "g", TEXT(T_HK_GOTO_URL), MENU_FUNC menu_goto_url, NULL, 0, 0 },
 { TEXT(T_GO_BACK), "<-", TEXT(T_HK_GO_BACK), MENU_FUNC menu_go_back, NULL, 0, 0 },
 { TEXT(T_HISTORY), ">", TEXT(T_HK_HISTORY), MENU_FUNC history_menu, NULL, 1, 0 },
 { TEXT(T_RELOAD), "Ctrl-R", TEXT(T_HK_RELOAD), MENU_FUNC menu_reload, NULL, 0, 0 },
#ifndef PDP11_MINIMAL
 { TEXT(T_SAVE_AS), "", TEXT(T_HK_SAVE_AS), MENU_FUNC save_as, NULL, 0, 0 },
 { TEXT(T_SAVE_URL_AS), "", TEXT(T_HK_SAVE_URL_AS), MENU_FUNC menu_save_url_as, NULL, 0, 0 },
 { TEXT(T_SAVE_FORMATTED_DOCUMENT), "", TEXT(T_HK_SAVE_FORMATTED_DOCUMENT), MENU_FUNC menu_save_formatted, NULL, 0, 0 },
#endif
 { TEXT(T_EXIT), "q", TEXT(T_HK_EXIT), MENU_FUNC exit_prog, NULL, 0, 0 },
 { NULL, NULL, 0, NULL, NULL, 0, 0 }
};
struct menu_item compact_file_anon[] = {
 { TEXT(T_GOTO_URL), "g", TEXT(T_HK_GOTO_URL), MENU_FUNC menu_goto_url, NULL, 0, 0 },
 { TEXT(T_GO_BACK), "<-", TEXT(T_HK_GO_BACK), MENU_FUNC menu_go_back, NULL, 0, 0 },
 { TEXT(T_HISTORY), ">", TEXT(T_HK_HISTORY), MENU_FUNC history_menu, NULL, 1, 0 },
 { TEXT(T_RELOAD), "Ctrl-R", TEXT(T_HK_RELOAD), MENU_FUNC menu_reload, NULL, 0, 0 },
 { TEXT(T_EXIT), "q", TEXT(T_HK_EXIT), MENU_FUNC exit_prog, NULL, 0, 0 },
 { NULL, NULL, 0, NULL, NULL, 0, 0 }
};
struct menu_item compact_view_menu[] = {
 { TEXT(T_SEARCH), "/", TEXT(T_HK_SEARCH), MENU_FUNC menu_for_frame, (void *)search_dlg, 0, 0 },
 { TEXT(T_SEARCH_BACK), "?", TEXT(T_HK_SEARCH_BACK), MENU_FUNC menu_for_frame, (void *)search_back_dlg, 0, 0 },
 { TEXT(T_FIND_NEXT), "n", TEXT(T_HK_FIND_NEXT), MENU_FUNC menu_for_frame, (void *)find_next, 0, 0 },
 { TEXT(T_FIND_PREVIOUS), "N", TEXT(T_HK_FIND_PREVIOUS), MENU_FUNC menu_for_frame, (void *)find_next_back, 0, 0 },
 { TEXT(T_TOGGLE_HTML_PLAIN), "\\", TEXT(T_HK_TOGGLE_HTML_PLAIN), MENU_FUNC menu_toggle, NULL, 0, 0 },
 { TEXT(T_DOCUMENT_INFO), "=", TEXT(T_HK_DOCUMENT_INFO), MENU_FUNC menu_doc_info, NULL, 0, 0 },
 { TEXT(T_HEADER_INFO), "|", TEXT(T_HK_HEADER_INFO), MENU_FUNC menu_head_info, NULL, 0, 0 },
 { TEXT(T_FRAME_AT_FULL_SCREEN), "f", TEXT(T_HK_FRAME_AT_FULL_SCREEN), MENU_FUNC menu_for_frame, (void *)set_frame, 0, 0 },
 { NULL, NULL, 0, NULL, NULL, 0, 0 }
};
struct menu_item compact_help_menu[] = {
 { TEXT(T_ABOUT), "", TEXT(T_HK_ABOUT), MENU_FUNC menu_about, NULL, 0, 0 },
 { TEXT(T_KEYS), "", TEXT(T_HK_KEYS), MENU_FUNC menu_keys, NULL, 0, 0 },
 { TEXT(T_MANUAL), "", TEXT(T_HK_MANUAL), MENU_FUNC menu_manual, NULL, 0, 0 },
 { TEXT(T_COPYING), "", TEXT(T_HK_COPYING), MENU_FUNC menu_copying, NULL, 0, 0 },
 { NULL, NULL, 0, NULL, NULL, 0, 0 }
};
void do_file_menu(struct terminal *term, void *xxx, struct session *ses)
{
 if (anonymous) do_menu(term, compact_file_anon, ses);
 else do_menu(term, compact_file_menu, ses);
}
void do_view_menu(struct terminal *term, void *xxx, struct session *ses)
{
 do_menu(term, compact_view_menu, ses);
}
struct menu_item main_menu[] = {
 { TEXT(T_FILE), "", TEXT(T_HK_FILE), MENU_FUNC do_file_menu, NULL, 1, 1 },
 { TEXT(T_VIEW), "", TEXT(T_HK_VIEW), MENU_FUNC do_view_menu, NULL, 1, 1 },
 { TEXT(T_LINK), "", TEXT(T_HK_LINK), MENU_FUNC link_menu, NULL, 1, 1 },
 { TEXT(T_DOWNLOADS), "", TEXT(T_HK_DOWNLOADS), MENU_FUNC downloads_menu, NULL, 1, 1 },
 { TEXT(T_HELP), "", TEXT(T_HK_HELP), MENU_FUNC do_menu, compact_help_menu, 1, 1 },
 { NULL, NULL, 0, NULL, NULL, 0, 0 }
};
void activate_bfu_technology(struct session *ses, int item)
{
 do_mainmenu(ses->term, main_menu, ses, item);
}

struct history goto_url_history = { 0, { &goto_url_history.items, &goto_url_history.items } };

void dialog_goto_url(struct session *ses, char *url)
{
	input_field(ses->term, NULL, TEXT(T_GOTO_URL), TEXT(T_ENTER_URL), TEXT(T_OK), TEXT(T_CANCEL), ses, &goto_url_history, MAX_INPUT_URL_LEN, url, 0, 0, NULL, (void (*)(void *, unsigned char *)) goto_url, NULL);
}

void dialog_save_url(struct session *ses)
{
#ifdef PDP11_MINIMAL
 start_download(ses, NULL);
#else
	input_field(ses->term, NULL, TEXT(T_SAVE_URL), TEXT(T_ENTER_URL), TEXT(T_OK), TEXT(T_CANCEL), ses, &goto_url_history, MAX_INPUT_URL_LEN, "", 0, 0, NULL, (void (*)(void *, unsigned char *)) save_url, NULL);
#endif
}

struct history file_history = { 0, { &file_history.items, &file_history.items } };

void query_file(struct session *ses, unsigned char *url, void (*std)(struct session *, unsigned char *), void (*cancel)(struct session *))
{
#ifdef PDP11_MINIMAL
 start_download(ses, NULL);
 if (cancel) cancel(ses);
#else
	unsigned char *file, *def;
	int dfl = 0;
	int l;
	get_filename_from_url(url, &file, &l);
	def = init_str();
	add_to_str(&def, &dfl, download_dir);
	if (*def && !dir_sep(def[strlen(def) - 1])) add_chr_to_str(&def, &dfl, '/');
	add_bytes_to_str(&def, &dfl, file, l);
	input_field(ses->term, NULL, TEXT(T_DOWNLOAD), TEXT(T_SAVE_TO_FILE), TEXT(T_OK), TEXT(T_CANCEL), ses, &file_history, MAX_INPUT_URL_LEN, def, 0, 0, NULL, (void (*)(void *, unsigned char *))std, (void (*)(void *))cancel);
	mem_free(def);
#endif
}

struct history search_history = { 0, { &search_history.items, &search_history.items } };

void search_back_dlg(struct session *ses, struct f_data_c *f, int a)
{
	input_field(ses->term, NULL, TEXT(T_SEARCH_BACK), TEXT(T_SEARCH_FOR_TEXT), TEXT(T_OK), TEXT(T_CANCEL), ses, &search_history, MAX_INPUT_URL_LEN, "", 0, 0, NULL, (void (*)(void *, unsigned char *)) search_for_back, NULL);
}

void search_dlg(struct session *ses, struct f_data_c *f, int a)
{
	input_field(ses->term, NULL, TEXT(T_SEARCH), TEXT(T_SEARCH_FOR_TEXT), TEXT(T_OK), TEXT(T_CANCEL), ses, &search_history, MAX_INPUT_URL_LEN, "", 0, 0, NULL, (void (*)(void *, unsigned char *)) search_for, NULL);
}

void free_history_lists()
{
	free_list(goto_url_history.items);
	free_list(file_history.items);
	free_list(search_history.items);
}

void auth_layout(struct dialog_data *dlg)
{
	struct terminal *term = dlg->win->term;
	int max = 0, min = 0;
	int w, rw;
	int y = -1;
	max_text_width(term, TEXT(T_USERID), &max);
	min_text_width(term, TEXT(T_USERID), &min);
	max_text_width(term, TEXT(T_PASSWORD), &max);
	min_text_width(term, TEXT(T_PASSWORD), &min);
	max_buttons_width(term, dlg->items + 2, 2,  &max);
	min_buttons_width(term, dlg->items + 2, 2,  &min);
	w = dlg->win->term->x * 9 / 10 - 2 * DIALOG_LB;
	if (w < min) w = min;
	if (w > dlg->win->term->x - 2 * DIALOG_LB) w = dlg->win->term->x - 2 * DIALOG_LB;
	if (w < 1) w = 1;
	rw = 0;
	if (dlg->dlg->udata) {
		dlg_format_text(NULL, term, dlg->dlg->udata, 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
		y++;
	}

	dlg_format_text(NULL, term, TEXT(T_USERID), 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += 2;
	dlg_format_text(NULL, term, TEXT(T_PASSWORD), 0, &y, w, &rw, COLOR_DIALOG_TEXT, AL_LEFT);
	y += 2;
	dlg_format_buttons(NULL, term, dlg->items + 2, 2, 0, &y, w, &rw, AL_CENTER);
	w = rw;
	dlg->xw = w + 2 * DIALOG_LB;
	dlg->yw = y + 2 * DIALOG_TB;
	center_dlg(dlg);
	draw_dlg(dlg);
	y = dlg->y + DIALOG_TB;
	if (dlg->dlg->udata) {
		dlg_format_text(term, term, dlg->dlg->udata, dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
		y++;
	}
	dlg_format_text(term, term, TEXT(T_USERID), dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);
	dlg_format_field(term, term, &dlg->items[0], dlg->x + DIALOG_LB, &y, w, NULL, AL_LEFT);
	y++;
	dlg_format_text(term, term, TEXT(T_PASSWORD), dlg->x + DIALOG_LB, &y, w, NULL, COLOR_DIALOG_TEXT, AL_LEFT);	
	dlg_format_field(term, term, &dlg->items[1], dlg->x + DIALOG_LB, &y, w, NULL, AL_LEFT);
	y++;
	dlg_format_buttons(term, term, &dlg->items[2], 2, dlg->x + DIALOG_LB, &y, w, NULL, AL_CENTER);
}

int auth_ok(struct dialog_data *dlg, struct dialog_item_data *di)
{
	reload(dlg->dlg->refresh_data, -1);
	return ok_dialog(dlg, di);
}

int auth_cancel(struct dialog_data *dlg, struct dialog_item_data *di)
{
	del_auth_entry(dlg->dlg->udata2);
	return cancel_dialog(dlg, di);
}

extern struct list_head http_auth_basic_list;

void do_auth_dialog(struct session *ses)
{
	struct dialog *d;
	struct terminal *term = ses->term;
	struct http_auth_basic *a = NULL;
	if (!list_empty(http_auth_basic_list) && !((struct http_auth_basic*)http_auth_basic_list.next)->valid) a = (struct http_auth_basic*)http_auth_basic_list.next;
	if (!a) return;
	a->valid = 1;
	if (!a->uid) {
		a->uid = mem_alloc(MAX_UID_LEN);
		*a->uid = 0;
	}
	if (!a->passwd) {
		a->passwd = mem_alloc(MAX_PASSWD_LEN);
		*a->passwd = 0;
	}
	d = mem_alloc(sizeof(struct dialog) + 5 * sizeof(struct dialog_item) + strlen(_(TEXT(T_ENTER_USERNAME), term))+(a->realm ? strlen(a->realm) : 0)+strlen(_(TEXT(T_AT), term))+strlen(a->url)+1);
	memset(d, 0, sizeof(struct dialog) + 5 * sizeof(struct dialog_item));
	d->title = TEXT(T_PASSWORD);
	d->fn = auth_layout;

	d->udata = (char *)d + sizeof(struct dialog) + 5 * sizeof(struct dialog_item);
	strcpy(d->udata, _(TEXT(T_ENTER_USERNAME), term));
	if (a->realm) strcat(d->udata, a->realm);
	strcat(d->udata, _(TEXT(T_AT), term));
	strcat(d->udata, a->url);

	d->udata2 = a;
	d->refresh_data = ses;
	
	d->items[0].type = D_FIELD;
	d->items[0].dlen = MAX_UID_LEN;
	d->items[0].data = a->uid;
	
	d->items[1].type = D_FIELD_PASS;
	d->items[1].dlen = MAX_PASSWD_LEN;
	d->items[1].data = a->passwd;
	
	d->items[2].type = D_BUTTON;
	d->items[2].gid = B_ENTER;
	d->items[2].fn = auth_ok;
	d->items[2].text = TEXT(T_OK);
	
	d->items[3].type = D_BUTTON;
	d->items[3].gid = B_ESC;
	d->items[3].fn = auth_cancel;
	d->items[3].text = TEXT(T_CANCEL);

	d->items[4].type = D_END;
	do_dialog(term, d, getml(d, NULL));
}

#ifdef PDP11_MINIMAL
/* Keyboard bookmark shortcuts remain defined, but never pretend to save. */
void menu_bookmark_manager(struct terminal *term, void *unused, struct session *ses)
{
 msg_box(term, NULL, TEXT(T_BOOKMARKS), AL_CENTER,
         "Bookmark editing is not included in the compact PDP-11 build.",
         NULL, 1, TEXT(T_OK), NULL, B_ENTER | B_ESC);
}
void launch_bm_add_doc_dialog(struct terminal *term, struct dialog_data *parent, struct session *ses)
{
 menu_bookmark_manager(term, NULL, ses);
}
#endif

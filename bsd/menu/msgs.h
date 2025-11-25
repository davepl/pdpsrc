#ifndef MSGS_H
#define MSGS_H

#include "data.h"
#include "session.h"
#include "screens.h"

#define POST_MENU_LABEL_LEN 128

enum post_menu_entry_type {
    POST_MENU_ENTRY_MESSAGE = 0,
    POST_MENU_ENTRY_COMPOSE,
    POST_MENU_ENTRY_BACK
};

void msgs_post_index_screen(int *last_highlight);
void msgs_post_view_screen(int message_index);
int msgs_visible_message_count(void);
void msgs_edit_body(char *buffer, int maxlen);

#endif

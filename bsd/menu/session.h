#ifndef SESSION_H
#define SESSION_H

#include "data.h"

struct session {
    char username[MAX_AUTHOR];
    int is_admin;
    int current_group;
};

extern struct session g_session;

#endif

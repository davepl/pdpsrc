#ifndef AUTH_H
#define AUTH_H

#include "data.h"
#include "session.h"

int perform_login(struct session *session);
int change_password_for_user(const char *username, int require_current);
int lock_unlock_user_account(int lock);
void show_user_list(void);

#endif

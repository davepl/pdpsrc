#/******************************************************************************
# *                                                                            *
# *  ░███████  ░█████   ░███████        ░████████   ░████████     ░██████      *
# *  ░██   ░██ ░██ ░██  ░██   ░██       ░██    ░██  ░██    ░██   ░██   ░██     *
# *  ░██   ░██ ░██  ░██ ░██   ░██       ░██    ░██  ░██    ░██  ░██            *
# *  ░███████  ░██  ░██ ░███████  ░████ ░████████   ░████████    ░████████     *
# *  ░██       ░██  ░██ ░██             ░██     ░██ ░██     ░██         ░██    *
# *  ░██       ░██ ░██  ░██             ░██     ░██ ░██     ░██  ░██   ░██     *
# *  ░██       ░█████   ░██             ░█████████  ░█████████    ░██████      *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  PROGRAM:     DAVE'S GARAGE PDP-11 BBS MENU SYSTEM                         *
# *  MODULE:      AUTH.C                                                       *
# *  VERSION:     0.2                                                          *
# *  DATE:        NOVEMBER 2025                                                *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  DESCRIPTION:                                                              *
# *                                                                            *
# *    Centralizes all authentication, login, user management                  *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  AUTHOR:      DAVE PLUMMER                                                 *
# *  LICENSE:     GPL 2.0                                                      *
# *                                                                            *
# ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "auth.h"
#include "menu.h"
#include "platform.h"

extern void draw_layout(const char *title, const char *status);
extern void draw_menu_lines(const char *line1, const char *line2, const char *line3);
extern void wait_for_ack(const char *msg);
extern void prompt_string(const char *label, char *buffer, int maxlen);
extern int is_valid_username(const char *text);
extern void show_layout_login_banner(void); /* helper in main */

static int prompt_and_verify_password(struct user_record *rec);
static int prompt_new_password(char *out_hash, size_t out_len);
static void log_auth_denied(const char *user, const char *action, const char *reason);

int
perform_login(struct session *session)
{
    char user[MAX_AUTHOR];
    char pass[MAX_AUTHOR];
    char confirm[MAX_AUTHOR];
    struct user_record user_rec;
    int has_account;

    if (session == NULL)
        return -1;

    show_layout_login_banner();

    draw_menu_lines("", "", "");
    while (1) {
        prompt_string("User id:", user, sizeof user);
        if (user[0] == '\0')
            return -1;
        if (!is_valid_username(user)) {
            wait_for_ack("User id must be alphanumeric.");
            continue;
        }
        break;
    }
    has_account = (load_user_record(user, &user_rec) == 0);
    if (!has_account) {
        safe_copy(user_rec.username, sizeof user_rec.username, user);
        user_rec.is_admin = (strcasecmp(user, ADMIN_USER) == 0) ? 1 : 0;
        user_rec.locked = 0;
        prompt_string("Set password:", pass, sizeof pass);
        if (pass[0] == '\0')
            return -1;
        if ((int)strlen(pass) < MIN_PASSWORD_LEN) {
            wait_for_ack("Password must be at least 8 characters.");
            return -1;
        }
        prompt_string("Confirm password:", confirm, sizeof confirm);
        if (strcmp(pass, confirm) != 0) {
            wait_for_ack("Passwords do not match.");
            return -1;
        }
        hash_password(pass, user_rec.password_hash, sizeof user_rec.password_hash);
        if (save_user_record(&user_rec) != 0) {
            wait_for_ack("Unable to save account.");
            return -1;
        }
    } else {
        if (user_rec.locked) {
            wait_for_ack("Account locked. Contact an admin.");
            log_auth_denied(user_rec.username, "login", "account locked");
            return -1;
        }
        prompt_string("Password:", pass, sizeof pass);
        if ((int)strlen(pass) < MIN_PASSWORD_LEN) {
            wait_for_ack("Password must be at least 8 characters.");
            return -1;
        }
        if (user_rec.password_hash[0] == '\0' ||
            !verify_password(pass, user_rec.password_hash)) {
            wait_for_ack("Invalid password.");
            log_auth_denied(user_rec.username, "login", "invalid password");
            return -1;
        }
    }
    session->is_admin = user_rec.is_admin;
    safe_copy(session->username, sizeof session->username, user_rec.username);
    session->current_group = -1;
    return 0;
}

int
change_password_for_user(const char *username, int require_current)
{
    struct user_record rec;

    if (username == NULL || username[0] == '\0')
        return -1;
    if (load_user_record(username, &rec) != 0) {
        wait_for_ack("Account not found.");
        return -1;
    }

    if (require_current) {
        if (prompt_and_verify_password(&rec) != 0)
            return -1;
    }

    if (prompt_new_password(rec.password_hash, sizeof rec.password_hash) != 0)
        return -1;
    if (save_user_record(&rec) != 0) {
        wait_for_ack("Unable to save password.");
        return -1;
    }
    wait_for_ack("Password updated.");
    return 0;
}

int
lock_unlock_user_account(int lock)
{
    char name[MAX_AUTHOR];
    struct user_record rec;

    prompt_string(lock ? "Lock which user:" : "Unlock which user:", name, sizeof name);
    if (name[0] == '\0')
        return -1;
    if (!is_valid_username(name)) {
        wait_for_ack("Invalid username.");
        return -1;
    }
    if (strcasecmp(name, ADMIN_USER) == 0 && lock) {
        wait_for_ack("Cannot lock the admin account.");
        return -1;
    }
    if (load_user_record(name, &rec) != 0) {
        wait_for_ack("User not found.");
        return -1;
    }
    rec.locked = lock ? 1 : 0;
    if (save_user_record(&rec) != 0) {
        wait_for_ack("Unable to update user.");
        return -1;
    }
    wait_for_ack(lock ? "User locked." : "User unlocked.");
    return 0;
}

void
show_user_list(void)
{
    struct user_record users[64];
    int count = 0;
    int i;

    if (load_all_users(users, 64, &count) != 0) {
        wait_for_ack("Unable to load users.");
        return;
    }

    draw_layout("Users", "Admin");
    mvprintw(6, 4, "Name               Role    Locked");
    mvprintw(7, 4, "---------------------------------");
    for (i = 0; i < count && 8 + i < LINES - MENU_ROWS - 1; ++i) {
        mvprintw(8 + i, 4, "%-18s %-6s %-6s", users[i].username,
            users[i].is_admin ? "admin" : "user",
            users[i].locked ? "yes" : "no");
    }
    draw_menu_lines("[Space] Continue", "", "");
    wait_for_ack("Press any key to return.");
}

static int
prompt_and_verify_password(struct user_record *rec)
{
    char current[MAX_CONFIG_VALUE];

    if (rec == NULL)
        return -1;
    if (rec->password_hash[0] == '\0')
        return 0;
    prompt_string("Current password:", current, sizeof current);
    if (!verify_password(current, rec->password_hash)) {
        wait_for_ack("Incorrect password.");
        log_auth_denied(rec->username, "change password", "incorrect current password");
        return -1;
    }
    return 0;
}

static int
prompt_new_password(char *out_hash, size_t out_len)
{
    char buf[MAX_CONFIG_VALUE];
    char confirm[MAX_CONFIG_VALUE];

    if (out_hash == NULL || out_len == 0)
        return -1;

    prompt_string("New password:", buf, sizeof buf);
    if (buf[0] == '\0')
        return -1;
    if ((int)strlen(buf) < MIN_PASSWORD_LEN) {
        wait_for_ack("Password must be at least 8 characters.");
        return -1;
    }
    prompt_string("Confirm new password:", confirm, sizeof confirm);
    if (strcmp(buf, confirm) != 0) {
        wait_for_ack("Passwords do not match.");
        return -1;
    }
    hash_password(buf, out_hash, out_len);
    return 0;
}

static void
log_auth_denied(const char *user, const char *action, const char *reason)
{
    FILE *fp;
    time_t now;
    struct tm *tmv;
    char when[32];
    const char *uname;

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv != NULL) {
        snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d:%02d",
            tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
            tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        safe_copy(when, sizeof when, "unknown-time");
    }
    uname = (user && user[0]) ? user : "(unknown)";

    fp = fopen("admin.log", "a");
    if (fp != NULL) {
        fprintf(fp, "[%s] user=%s ip=unknown action=%s denied: %s\n",
            when, uname, action ? action : "(unspecified)",
            reason ? reason : "(no reason)");
        fclose(fp);
    }
}

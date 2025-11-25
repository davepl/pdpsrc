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
# *  MODULE:      DATA.C                                                       *
# *  VERSION:     0.2                                                          *
# *  DATE:        NOVEMBER 2025                                                *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  DESCRIPTION:                                                              *
# *                                                                            *
# *    Centralizes all persistent storage: groups, messages, address book,     *
# *    configuration, and global locking. Provides safe string utilities,      *
# *    disk I/O helpers, and record caching used by the UI layer.              *
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
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#if defined(__APPLE__) || defined(__MACH__) || defined(__linux__)
#include <unistd.h>
#else
extern int unlink();
extern int link();
extern int getpid();
extern unsigned sleep();
#endif

#ifndef HAVE_CRYPT_PROTO
extern char *crypt(const char *, const char *);
#endif

#include "data.h"

struct group g_groups[MAX_GROUPS];
int g_group_count;
struct config_data g_config;
struct message *g_cached_messages = NULL;
int g_cached_message_count = 0;

static struct message g_temp_message;
static int g_lock_depth = 0;
static const char g_salt_chars[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static int build_group_path(const char *group_name, char *path, int maxlen);
static int load_messages_direct(const char *group_name, struct message **messages, int *count);
static int save_messages_direct(const char *group_name, struct message *messages, int count);
static int acquire_lock(void);
static void release_lock(void);

// Trims CR/LF characters off the end of a string read from disk.

void
trim_newline(char *text)
{
    int len;

    if (text == NULL)
        return;
    len = (int)strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[len - 1] = '\0';
        --len;
    }
}

// Safe string copy that always NUL-terminates the destination buffer.

void
safe_copy(char *dst, size_t dstlen, const char *src)
{
    size_t i;

    if (dstlen == 0 || dst == NULL)
        return;
    if (src == NULL)
        src = "";
    for (i = 0; i < dstlen - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

// Appends a string to another string with bounds checking.

void
safe_append(char *dst, size_t dstlen, const char *src)
{
    size_t used;

    if (dst == NULL || dstlen == 0 || src == NULL)
        return;
    for (used = 0; used < dstlen - 1 && dst[used] != '\0'; ++used)
        ;
    while (*src && used < dstlen - 1) {
        dst[used++] = *src++;
    }
    dst[used] = '\0';
}

// Appends a single character to a bounded string buffer.

void
safe_append_char(char *dst, size_t dstlen, char ch)
{
    char tmp[2];

    tmp[0] = ch;
    tmp[1] = '\0';
    safe_append(dst, dstlen, tmp);
}

// Appends an integer in decimal form to the destination string.

void
safe_append_number(char *dst, size_t dstlen, long value)
{
    char tmp[32];
    int i;
    int neg;
    long v;

    if (dst == NULL || dstlen == 0)
        return;
    v = value;
    neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    i = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0 && i < (int)sizeof(tmp) - 1) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg && i < (int)sizeof(tmp) - 1)
        tmp[i++] = '-';
    tmp[i] = '\0';
    while (i > 0) {
        --i;
        safe_append_char(dst, dstlen, tmp[i]);
    }
}

// Appends a two-digit zero-padded number for dates/times.

void
safe_append_two_digit(char *dst, size_t dstlen, int value)
{
    char tmp[3];

    if (value < 0)
        value = 0;
    value %= 100;
    tmp[0] = (char)('0' + ((value / 10) % 10));
    tmp[1] = (char)('0' + (value % 10));
    tmp[2] = '\0';
    safe_append(dst, dstlen, tmp);
}

// Ensures the data directory exists before reading/writing files.

void
ensure_data_dir(void)
{
    struct stat st;

    if (stat(DATA_DIR, &st) == -1) {
        if (mkdir(DATA_DIR, 0755) == -1) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s exists but is not a directory.\n", DATA_DIR);
        exit(EXIT_FAILURE);
    }
}

// Initializes configuration defaults so load failures have sane values.

void
init_config(void)
{
    g_config.signature[0] = '\0';
    g_config.password_hash[0] = '\0';
    g_config.admin_password_hash[0] = '\0';
}

// Creates a short DES-compatible salt for 211BSD crypt(3).
// Uses time and pid to reduce collisions on repeated runs.
static void
make_salt(char *salt, size_t salt_len)
{
    unsigned long v;

    if (salt == NULL || salt_len < 3)
        return;
    v = (unsigned long)time(NULL);
#ifdef __unix__
    v ^= (unsigned long)getpid();
#endif
    salt[0] = g_salt_chars[v % 64];
    salt[1] = g_salt_chars[(v >> 6) % 64];
    salt[2] = '\0';
}

// Hashes a password using traditional crypt(3) so the result
// works on 2.11BSD. Output buffer is cleared on failure.
void
hash_password(const char *password, char *out, size_t outlen)
{
    char salt[3];
    char *enc;

    if (out == NULL || outlen == 0) {
        return;
    }
    if (password == NULL || password[0] == '\0') {
        out[0] = '\0';
        return;
    }
    make_salt(salt, sizeof salt);
    enc = crypt(password, salt);
    if (enc == NULL) {
        out[0] = '\0';
        return;
    }
    safe_copy(out, outlen, enc);
}

// Verifies a password against a stored crypt(3) hash.
int
verify_password(const char *password, const char *hash)
{
    char *enc;

    if (password == NULL || hash == NULL || hash[0] == '\0')
        return 0;
    enc = crypt(password, hash);
    if (enc == NULL)
        return 0;
    return strcmp(enc, hash) == 0;
}

// Loads groups from disk into memory, respecting deletion flags.

int
load_groups(void)
{
    FILE *fp;
    char line[256];
    char *name;
    char *desc;
    char *flag;

    g_group_count = 0;
    lock_guard();
    fp = fopen(GROUPS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return 0;
    }

    while (fgets(line, sizeof line, fp) != NULL && g_group_count < MAX_GROUPS) {
        trim_newline(line);
        name = strtok(line, "|");
        desc = strtok(NULL, "|");
        flag = strtok(NULL, "|");
        if (name == NULL)
            continue;
        safe_copy(g_groups[g_group_count].name, sizeof g_groups[g_group_count].name, name);
        if (desc)
            safe_copy(g_groups[g_group_count].description, sizeof g_groups[g_group_count].description, desc);
        else
            g_groups[g_group_count].description[0] = '\0';
        g_groups[g_group_count].deleted = (flag && *flag == 'D') ? 1 : 0;
        ++g_group_count;
    }
    fclose(fp);
    unlock_guard();
    return 0;
}

// Writes current group metadata back to disk.

int
save_groups(void)
{
    FILE *fp;
    int i;

    lock_guard();
    fp = fopen(GROUPS_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return -1;
    }
    for (i = 0; i < g_group_count; ++i)
        fprintf(fp, "%s|%s|%s\n", g_groups[i].name, g_groups[i].description,
            g_groups[i].deleted ? "D" : "");
    fclose(fp);
    unlock_guard();
    return 0;
}

// Builds the path to a group message file using a sanitized name.

static int
build_group_path(const char *group_name, char *path, int maxlen)
{
    char safe_name[MAX_GROUP_NAME];
    int i;

    for (i = 0; group_name[i] != '\0' && i < MAX_GROUP_NAME - 1; ++i) {
        if (isalnum((unsigned char)group_name[i]))
            safe_name[i] = tolower((unsigned char)group_name[i]);
        else
            safe_name[i] = '_';
    }
    safe_name[i] = '\0';
    if (maxlen <= 0)
        return -1;
    path[0] = '\0';
    safe_copy(path, maxlen, DATA_DIR);
    safe_append(path, maxlen, "/");
    safe_append(path, maxlen, safe_name);
    safe_append(path, maxlen, ".msg");
    return 0;
}

// Loads all messages for a given group into the in-memory cache.

int
load_messages_for_group(int group_index)
{
    struct message *msgs;
    int count;

    if (group_index < 0 || group_index >= g_group_count)
        return -1;
    if (load_messages_direct(g_groups[group_index].name, &msgs, &count) != 0)
        return -1;
    free_cached_messages();
    g_cached_messages = msgs;
    g_cached_message_count = count;
    return 0;
}

// Persists the in-memory message cache for a specific group.

int
save_messages_for_group(int group_index)
{
    if (group_index < 0 || group_index >= g_group_count)
        return -1;
    return save_messages_direct(g_groups[group_index].name, g_cached_messages, g_cached_message_count);
}

// Reads messages from disk into a newly allocated array.

static int
load_messages_direct(const char *group_name, struct message **messages, int *count)
{
    FILE *fp;
    char line[512];
    char path[256];
    struct message *list;
    int capacity;
    struct message msg;

    *messages = NULL;
    *count = 0;
    build_group_path(group_name, path, sizeof path);

    lock_guard();
    fp = fopen(path, "r");
    if (fp == NULL) {
        unlock_guard();
        return 0;
    }

    capacity = 32;
    list = (struct message *)malloc(sizeof(struct message) * capacity);
    if (list == NULL) {
        fclose(fp);
        unlock_guard();
        return -1;
    }

    memset(&msg, 0, sizeof msg);
    while (fgets(line, sizeof line, fp) != NULL) {
        trim_newline(line);
        if (strncmp(line, "MSG ", 4) == 0) {
            msg.id = atoi(line + 4);
        } else if (strncmp(line, "PARENT ", 7) == 0) {
            msg.parent_id = atoi(line + 7);
        } else if (strncmp(line, "THREAD ", 7) == 0) {
            msg.thread_id = atoi(line + 7);
        } else if (strncmp(line, "TIME ", 5) == 0) {
            msg.created = (time_t)atol(line + 5);
        } else if (strncmp(line, "STATUS ", 7) == 0) {
            msg.deleted = (line[7] == 'D');
            msg.answered = (line[7] == 'A');
        } else if (strncmp(line, "AUTHOR ", 7) == 0) {
            safe_copy(msg.author, sizeof msg.author, line + 7);
        } else if (strncmp(line, "SUBJECT ", 8) == 0) {
            safe_copy(msg.subject, sizeof msg.subject, line + 8);
        } else if (strcmp(line, "BODY") == 0) {
            msg.body[0] = '\0';
            while (fgets(line, sizeof line, fp) != NULL) {
                trim_newline(line);
                if (strcmp(line, ".") == 0)
                    break;
                if ((int)strlen(msg.body) + (int)strlen(line) + 2 >= MAX_BODY)
                    break;
                safe_append(msg.body, sizeof msg.body, line);
                safe_append_char(msg.body, sizeof msg.body, '\n');
            }
        } else if (strcmp(line, "END") == 0) {
            if (msg.thread_id == 0) {
                if (msg.parent_id > 0)
                    msg.thread_id = msg.parent_id;
                else
                    msg.thread_id = msg.id;
            }
            if (*count >= capacity) {
                capacity *= 2;
                list = (struct message *)realloc(list, sizeof(struct message) * capacity);
                if (list == NULL) {
                    fclose(fp);
                    unlock_guard();
                    return -1;
                }
            }
            list[*count] = msg;
            ++(*count);
            memset(&msg, 0, sizeof msg);
        }
    }

    fclose(fp);
    unlock_guard();
    *messages = list;
    return 0;
}

// Writes the provided message array to disk for a given group.

static int
save_messages_direct(const char *group_name, struct message *messages, int count)
{
    FILE *fp;
    char path[256];
    int i;

    build_group_path(group_name, path, sizeof path);
    lock_guard();
    fp = fopen(path, "w");
    if (fp == NULL) {
        unlock_guard();
        return -1;
    }
    for (i = 0; i < count; ++i) {
        fprintf(fp, "MSG %d\n", messages[i].id);
        fprintf(fp, "PARENT %d\n", messages[i].parent_id);
        fprintf(fp, "THREAD %d\n", messages[i].thread_id ? messages[i].thread_id : messages[i].id);
        fprintf(fp, "TIME %ld\n", (long)messages[i].created);
        fprintf(fp, "STATUS %c\n", messages[i].deleted ? 'D' : (messages[i].answered ? 'A' : 'N'));
        fprintf(fp, "AUTHOR %s\n", messages[i].author);
        fprintf(fp, "SUBJECT %s\n", messages[i].subject);
        fprintf(fp, "BODY\n%s", messages[i].body);
        if (messages[i].body[0] == '\0' || messages[i].body[strlen(messages[i].body) - 1] != '\n')
            fprintf(fp, "\n");
        fprintf(fp, ".\nEND\n");
    }
    fclose(fp);
    unlock_guard();
    return 0;
}

// Releases the cached message array to free memory.

void
free_cached_messages(void)
{
    if (g_cached_messages != NULL) {
        free(g_cached_messages);
        g_cached_messages = NULL;
    }
    g_cached_message_count = 0;
}

// Computes the next available message ID by scanning cached data.

int
next_message_id(void)
{
    int i;
    int max_id;

    max_id = 0;
    for (i = 0; i < g_cached_message_count; ++i) {
        if (g_cached_messages[i].id > max_id)
            max_id = g_cached_messages[i].id;
    }
    return max_id + 1;
}

// Copies a message into another group's file, used by save/forward flows.

int
copy_message_to_group(struct message *msg, const char *group_name)
{
    struct message *msgs;
    int count;
    struct message *copy;
    int rc;

    if (load_messages_direct(group_name, &msgs, &count) != 0)
        return -1;
    copy = &g_temp_message;
    *copy = *msg;
    copy->id = 0;
    copy->parent_id = 0;
    copy->thread_id = 0;
    copy->deleted = 0;
    copy->answered = 0;
    copy->created = time(NULL);
    copy->id = count > 0 ? msgs[count - 1].id + 1 : 1;
    if (copy->thread_id == 0)
        copy->thread_id = copy->id;
    msgs = (struct message *)realloc(msgs, sizeof(struct message) * (count + 1));
    if (msgs == NULL)
        return -1;
    msgs[count] = *copy;
    rc = save_messages_direct(group_name, msgs, count + 1);
    free(msgs);
    return rc;
}

// Loads key/value configuration file for signature/password.

void
load_config(void)
{
    FILE *fp;
    char line[512];
    char *key;
    char *value;

    lock_guard();
    fp = fopen(CONFIG_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        trim_newline(line);
        key = strtok(line, "=");
        value = strtok(NULL, "");
        if (key == NULL || value == NULL)
            continue;
        if (strcmp(key, "signature") == 0)
            safe_copy(g_config.signature, sizeof g_config.signature, value);
        else if (strcmp(key, "password_hash") == 0)
            safe_copy(g_config.password_hash, sizeof g_config.password_hash, value);
        else if (strcmp(key, "admin_password_hash") == 0)
            safe_copy(g_config.admin_password_hash, sizeof g_config.admin_password_hash, value);
    }
    fclose(fp);
    unlock_guard();
}

// Writes configuration data back to disk.

void
save_config(void)
{
    FILE *fp;

    lock_guard();
    fp = fopen(CONFIG_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return;
    }
    fprintf(fp, "signature=%s\n", g_config.signature);
    fclose(fp);
    unlock_guard();
}

// Looks up a user record by username (case-insensitive).
// Returns 0 on success, -1 if not found or on error.
int
load_user_record(const char *username, struct user_record *out)
{
    FILE *fp;
    char line[512];
    char *name;
    char *hash;
    char *role;
    int found = 0;

    if (username == NULL || *username == '\0')
        return -1;

    lock_guard();
    fp = fopen(USERS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return -1;
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        trim_newline(line);
        name = strtok(line, "|");
        hash = strtok(NULL, "|");
        role = strtok(NULL, "|");
        if (name == NULL || hash == NULL)
            continue;
        if (strcasecmp(name, username) != 0)
            continue;
        found = 1;
        if (out != NULL) {
            safe_copy(out->username, sizeof out->username, name);
            safe_copy(out->password_hash, sizeof out->password_hash, hash);
            out->is_admin = 0;
            out->locked = 0;
            if (role != NULL) {
                if (role[0] == 'A' || role[0] == 'a')
                    out->is_admin = (strcasecmp(name, ADMIN_USER) == 0) ? 1 : 0;
                if (role[0] == 'L' || role[0] == 'l')
                    out->locked = 1;
            }
            {
                char *lock = strtok(NULL, "|");
                if (lock && (lock[0] == 'L' || lock[0] == 'l'))
                    out->locked = 1;
            }
        }
        break;
    }

    fclose(fp);
    unlock_guard();
    return found ? 0 : -1;
}

// Saves or replaces a user record, ensuring only ADMIN_USER can be admin.
// Returns 0 on success, -1 on error.
int
save_user_record(const struct user_record *user)
{
    FILE *in;
    FILE *out;
    char line[512];
    char tmpfile[256];
    int wrote = 0;

    if (user == NULL || user->username[0] == '\0')
        return -1;

    if (user->is_admin && strcasecmp(user->username, ADMIN_USER) != 0) {
        // Never allow non-admin username to persist an admin flag.
        return -1;
    }

    lock_guard();
    safe_copy(tmpfile, sizeof tmpfile, USERS_FILE);
    safe_append(tmpfile, sizeof tmpfile, ".tmp");

    out = fopen(tmpfile, "w");
    if (out == NULL) {
        unlock_guard();
        return -1;
    }

    in = fopen(USERS_FILE, "r");
    if (in != NULL) {
        while (fgets(line, sizeof line, in) != NULL) {
            char copy[512];
            char *name;
            size_t len;

            safe_copy(copy, sizeof copy, line);
            trim_newline(copy);
            name = strtok(copy, "|");
            if (name != NULL && strcasecmp(name, user->username) == 0)
                continue; /* replace existing entry */
            fputs(line, out);
            len = strlen(line);
            if (len > 0 && line[len - 1] != '\n')
                fputc('\n', out);
        }
        fclose(in);
    }

    fprintf(out, "%s|%s|%s|%s\n", user->username, user->password_hash,
        user->is_admin ? "A" : "U", user->locked ? "L" : "U");
    wrote = 1;
    fclose(out);

    if (wrote) {
        if (rename(tmpfile, USERS_FILE) != 0) {
            remove(USERS_FILE);
            rename(tmpfile, USERS_FILE);
        }
    } else {
        remove(tmpfile);
    }
    unlock_guard();
    return wrote ? 0 : -1;
}

// Loads all user records into caller-provided array. Returns 0 on success.
int
load_all_users(struct user_record *users, int max_users, int *out_count)
{
    FILE *fp;
    char line[512];
    int count = 0;

    if (users == NULL || max_users <= 0)
        return -1;
    lock_guard();
    fp = fopen(USERS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        if (out_count)
            *out_count = 0;
        return 0;
    }

    while (fgets(line, sizeof line, fp) != NULL && count < max_users) {
        struct user_record rec;
        char *name;
        char *hash;
        char *role;
        char *locked;

        trim_newline(line);
        name = strtok(line, "|");
        hash = strtok(NULL, "|");
        role = strtok(NULL, "|");
        locked = strtok(NULL, "|");
        if (name == NULL || hash == NULL)
            continue;
        safe_copy(rec.username, sizeof rec.username, name);
        safe_copy(rec.password_hash, sizeof rec.password_hash, hash);
        rec.is_admin = (role && (role[0] == 'A' || role[0] == 'a') && strcasecmp(name, ADMIN_USER) == 0);
        rec.locked = (locked && (locked[0] == 'L' || locked[0] == 'l')) ||
            (role && (role[0] == 'L' || role[0] == 'l'));
        users[count++] = rec;
    }
    fclose(fp);
    unlock_guard();
    if (out_count)
        *out_count = count;
    return 0;
}

// Attempts to acquire a simple lock by creating a .lock file.

static int
acquire_lock(void)
{
    char tmp[128];
    FILE *fp;
    int attempt;
    int pid;

    if (g_lock_depth > 0) {
        ++g_lock_depth;
        return 0;
    }

    pid = 1;
#ifdef __unix__
    pid = getpid();
#endif
    safe_copy(tmp, sizeof tmp, DATA_DIR);
    safe_append(tmp, sizeof tmp, "/.lock.");
    safe_append_number(tmp, sizeof tmp, pid);
    fp = fopen(tmp, "w");
    if (fp == NULL)
        return -1;
    fprintf(fp, "%ld\n", (long)time(NULL));
    fclose(fp);

    for (attempt = 0; attempt < 5; ++attempt) {
        if (link(tmp, LOCK_FILE) == 0) {
            unlink(tmp);
            g_lock_depth = 1;
            return 0;
        }
#ifdef __unix__
        sleep(1);
#endif
    }
    unlink(tmp);
    return -1;
}

// Releases the lock when critical section is complete.

static void
release_lock(void)
{
    if (g_lock_depth == 0)
        return;
    --g_lock_depth;
    if (g_lock_depth == 0)
        unlink(LOCK_FILE);
}

// Public entry point to acquire the guard lock and warn on failure.

void
lock_guard(void)
{
    if (acquire_lock() != 0)
        fprintf(stderr, "Unable to acquire global lock.\n");
}

// Matching unlock for lock_guard, releasing the global lock file.

void
unlock_guard(void)
{
    release_lock();
}

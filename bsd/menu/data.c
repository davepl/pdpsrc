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

#include "data.h"

struct group g_groups[MAX_GROUPS];
int g_group_count;
struct address_entry g_addrbook[MAX_ADDRBOOK];
int g_addr_count;
struct config_data g_config;
struct message *g_cached_messages = NULL;
int g_cached_message_count = 0;

static struct message g_temp_message;
static int g_lock_depth = 0;

static int build_group_path(const char *group_name, char *path, int maxlen);
static int load_messages_direct(const char *group_name, struct message **messages, int *count);
static int save_messages_direct(const char *group_name, struct message *messages, int count);
static int acquire_lock(void);
static void release_lock(void);

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

void
safe_append_char(char *dst, size_t dstlen, char ch)
{
    char tmp[2];

    tmp[0] = ch;
    tmp[1] = '\0';
    safe_append(dst, dstlen, tmp);
}

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

void
init_config(void)
{
    g_config.printer[0] = '\0';
    g_config.signature[0] = '\0';
    g_config.password[0] = '\0';
}

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

int
save_messages_for_group(int group_index)
{
    if (group_index < 0 || group_index >= g_group_count)
        return -1;
    return save_messages_direct(g_groups[group_index].name, g_cached_messages, g_cached_message_count);
}

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

void
free_cached_messages(void)
{
    if (g_cached_messages != NULL) {
        free(g_cached_messages);
        g_cached_messages = NULL;
    }
    g_cached_message_count = 0;
}

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
    copy->deleted = 0;
    copy->answered = 0;
    copy->created = time(NULL);
    copy->id = count > 0 ? msgs[count - 1].id + 1 : 1;
    msgs = (struct message *)realloc(msgs, sizeof(struct message) * (count + 1));
    if (msgs == NULL)
        return -1;
    msgs[count] = *copy;
    rc = save_messages_direct(group_name, msgs, count + 1);
    free(msgs);
    return rc;
}

void
load_address_book(void)
{
    FILE *fp;
    char line[512];
    char *nick;
    char *full;
    char *addr;
    char *flag;

    g_addr_count = 0;
    lock_guard();
    fp = fopen(ADDRESS_FILE, "r");
    if (fp == NULL) {
        unlock_guard();
        return;
    }

    while (fgets(line, sizeof line, fp) != NULL && g_addr_count < MAX_ADDRBOOK) {
        trim_newline(line);
        nick = strtok(line, "|");
        full = strtok(NULL, "|");
        addr = strtok(NULL, "|");
        flag = strtok(NULL, "|");
        if (nick == NULL || addr == NULL)
            continue;
        safe_copy(g_addrbook[g_addr_count].nickname, sizeof g_addrbook[g_addr_count].nickname, nick);
        safe_copy(g_addrbook[g_addr_count].fullname, sizeof g_addrbook[g_addr_count].fullname, full ? full : "");
        safe_copy(g_addrbook[g_addr_count].address, sizeof g_addrbook[g_addr_count].address, addr);
        g_addrbook[g_addr_count].is_list = (flag && *flag == 'L') ? 1 : 0;
        ++g_addr_count;
    }
    fclose(fp);
    unlock_guard();
}

void
save_address_book(void)
{
    FILE *fp;
    int i;

    lock_guard();
    fp = fopen(ADDRESS_FILE, "w");
    if (fp == NULL) {
        unlock_guard();
        return;
    }
    for (i = 0; i < g_addr_count; ++i)
        fprintf(fp, "%s|%s|%s|%c\n", g_addrbook[i].nickname,
            g_addrbook[i].fullname, g_addrbook[i].address,
            g_addrbook[i].is_list ? 'L' : 'A');
    fclose(fp);
    unlock_guard();
}

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
        if (strcmp(key, "printer") == 0)
            safe_copy(g_config.printer, sizeof g_config.printer, value);
        else if (strcmp(key, "signature") == 0)
            safe_copy(g_config.signature, sizeof g_config.signature, value);
        else if (strcmp(key, "password") == 0)
            safe_copy(g_config.password, sizeof g_config.password, value);
    }
    fclose(fp);
    unlock_guard();
}

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
    fprintf(fp, "printer=%s\n", g_config.printer);
    fprintf(fp, "signature=%s\n", g_config.signature);
    fprintf(fp, "password=%s\n", g_config.password);
    fclose(fp);
    unlock_guard();
}

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

static void
release_lock(void)
{
    if (g_lock_depth == 0)
        return;
    --g_lock_depth;
    if (g_lock_depth == 0)
        unlink(LOCK_FILE);
}

void
lock_guard(void)
{
    if (acquire_lock() != 0)
        fprintf(stderr, "Unable to acquire global lock.\n");
}

void
unlock_guard(void)
{
    release_lock();
}

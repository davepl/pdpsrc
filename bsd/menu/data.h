#ifndef DATA_H
#define DATA_H

#ifndef TEST
#define TEST 1
#endif

#include <sys/types.h>

#define DATA_DIR "bbsdata"
#define GROUPS_FILE DATA_DIR "/groups.txt"
#define ADDRESS_FILE DATA_DIR "/addrbook.txt"
#define CONFIG_FILE DATA_DIR "/config.txt"
#define LOCK_FILE DATA_DIR "/.lock"
#define PROGRAM_TITLE "Dave's Garage PDP-11 BBS"
#define PROGRAM_VERSION "0.2"
#define ADMIN_USER "admin"

#define MIN_COLS 80
#define MIN_ROWS 24
#define MENU_ROWS 5

#ifdef __pdp11__
#define MAX_GROUPS 16
#define MAX_GROUP_NAME 32
#define MAX_GROUP_DESC 48
#define MAX_MESSAGES 64
#define MAX_SUBJECT 64
#define MAX_BODY 1024
#define MAX_AUTHOR 32
#define MAX_ADDRESS 64
#define MAX_CONFIG_VALUE 128
#define MAX_MENU_OPTIONS 16
#else
#define MAX_GROUPS 64
#define MAX_GROUP_NAME 48
#define MAX_GROUP_DESC 80
#define MAX_MESSAGES 256
#define MAX_SUBJECT 96
#define MAX_BODY 4096
#define MAX_AUTHOR 48
#define MAX_ADDRESS 128
#define MAX_CONFIG_VALUE 256
#define MAX_MENU_OPTIONS 16
#endif

struct group {
    char name[MAX_GROUP_NAME];
    char description[MAX_GROUP_DESC];
    int deleted;
};

struct message {
    int id;
    int parent_id;
    time_t created;
    int deleted;
    int answered;
    char author[MAX_AUTHOR];
    char subject[MAX_SUBJECT];
    char body[MAX_BODY];
};

struct config_data {
    char signature[MAX_CONFIG_VALUE];
    char password_hash[MAX_CONFIG_VALUE];
    char admin_password_hash[MAX_CONFIG_VALUE];
};

extern struct group g_groups[MAX_GROUPS];
extern int g_group_count;
extern struct config_data g_config;
extern struct message *g_cached_messages;
extern int g_cached_message_count;

void trim_newline(char *text);
void safe_copy(char *dst, size_t dstlen, const char *src);
void safe_append(char *dst, size_t dstlen, const char *src);
void safe_append_char(char *dst, size_t dstlen, char ch);
void safe_append_number(char *dst, size_t dstlen, long value);
void safe_append_two_digit(char *dst, size_t dstlen, int value);
void ensure_data_dir(void);
void init_config(void);
int load_groups(void);
int save_groups(void);
int load_messages_for_group(int group_index);
int save_messages_for_group(int group_index);
void free_cached_messages(void);
int next_message_id(void);
int copy_message_to_group(struct message *msg, const char *group_name);
void load_config(void);
void save_config(void);
void lock_guard(void);
void unlock_guard(void);
void hash_password(const char *password, char *out, size_t outlen);
int verify_password(const char *password, const char *hash);

#endif

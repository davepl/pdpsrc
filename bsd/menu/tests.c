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
# *  MODULE:      TESTS.C                                                       *
# *  VERSION:     0.2                                                          *
# *  DATE:        NOVEMBER 2025                                                *
# *                                                                            *
# *  ════════════════════════════════════════════════════════════════════════  *
# *                                                                            *
# *  DESCRIPTION:                                                              *
# *                                                                            *
# *    Test cases only present when TEST=1 is defined
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
#include "data.h"
#include "menu.h"
#include "platform.h"
#include "session.h"
#include "tests.h"

#ifdef TEST

extern void draw_layout(const char *title, const char *status);
extern void draw_menu_lines(const char *line1, const char *line2, const char *line3);
extern void wait_for_ack(const char *msg);

static void delete_group_messages(const char *group_name);
static void run_group_stress_test(struct session *session);

void
run_tests_menu(struct session *session)
{
    struct menu_item items[2];
    int selected = -1;
    int focus = -1;
    int highlight = 0;
    int choice;

    items[0].key = 'G';
    items[0].label = "Group Stress Test";
    items[1].key = 'B';
    items[1].label = "Back";

    while (1) {
        draw_layout("Tests", "Diagnostics");
        draw_menu_lines("Select a test to run", "", "");
        choice = run_menu(8, items, 2, highlight, &selected, &focus, 0);
        if (focus >= 0)
            highlight = focus;
        if (choice == 0 || choice == 'B') {
            return;
        }
        switch (choice) {
        case 'G':
            run_group_stress_test(session);
            wait_for_ack("Group stress test complete.");
            break;
        default:
            break;
        }
    }
}

static void
run_group_stress_test(struct session *session)
{
    const int group_target = 5;
    const int msgs_per_group = 10;
    int orig_count = g_group_count;
    int added = 0;
    int i, j;
    int saved_admin = session->is_admin;
    int saved_group = session->current_group;
    char msgbuf[64];

    for (i = 0; i < group_target && g_group_count < MAX_GROUPS; ++i) {
        char name[MAX_GROUP_NAME];
        char desc[MAX_GROUP_DESC];
        snprintf(name, sizeof name, "TestGroup%02d", i + 1);
        snprintf(desc, sizeof desc, "Stress test group %d", i + 1);
        safe_copy(g_groups[g_group_count].name, sizeof g_groups[g_group_count].name, name);
        safe_copy(g_groups[g_group_count].description, sizeof g_groups[g_group_count].description, desc);
        g_groups[g_group_count].deleted = 0;
        ++g_group_count;
        ++added;
    }
    save_groups();

    for (i = 0; i < added; ++i) {
        int idx = orig_count + i;
        for (j = 0; j < msgs_per_group; ++j) {
            struct message newmsg;
            memset(&newmsg, 0, sizeof newmsg);
            newmsg.parent_id = 0;
            newmsg.thread_id = 0;
            newmsg.created = time(NULL);
            newmsg.deleted = 0;
            newmsg.answered = 0;
            safe_copy(newmsg.author, sizeof newmsg.author,
                session->username[0] ? session->username : "tester");
            snprintf(msgbuf, sizeof msgbuf, "Message %02d", j + 1);
            safe_copy(newmsg.subject, sizeof newmsg.subject, msgbuf);
            safe_copy(newmsg.body, sizeof newmsg.body, "Stress test message\n");
            copy_message_to_group(&newmsg, g_groups[idx].name);
        }
    }

    for (i = 0; i < added; ++i) {
        int idx = orig_count + i;
        g_groups[idx].deleted = 1;
        delete_group_messages(g_groups[idx].name);
    }
    save_groups();

    {
        int dst = 0;
        for (i = 0; i < g_group_count; ++i) {
            if (i >= orig_count && i < orig_count + added)
                continue;
            if (dst != i)
                g_groups[dst] = g_groups[i];
            ++dst;
        }
        g_group_count = dst;
        save_groups();
    }

    session->is_admin = saved_admin;
    session->current_group = saved_group;
    free_cached_messages();
}

static void
delete_group_messages(const char *group_name)
{
    char safe_name[MAX_GROUP_NAME];
    char path[256];
    int i;

    if (group_name == NULL || group_name[0] == '\0')
        return;
    for (i = 0; group_name[i] != '\0' && i < MAX_GROUP_NAME - 1; ++i) {
        if (isalnum((unsigned char)group_name[i]))
            safe_name[i] = (char)tolower((unsigned char)group_name[i]);
        else
            safe_name[i] = '_';
    }
    safe_name[i] = '\0';
    safe_copy(path, sizeof path, DATA_DIR "/");
    safe_append(path, sizeof path, safe_name);
    safe_append(path, sizeof path, ".msg");
    remove(path);
}

#endif /* TEST */

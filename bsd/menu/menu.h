#ifndef MENU_H
#define MENU_H

struct menu_item {
    char key;
    const char *label;
};

#define MENU_OPT_RETURN_UNHANDLED 1

int run_menu(int start_row,
             const struct menu_item *items,
             int count,
             int initial_highlight,
             int *selected_index,
             int *focus_index,
             int options);

#endif

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tmux-full.h"
int cmd_flags(int, const char *, struct cmd_chflags *);
size_t cmd_print_flags(char *, size_t, size_t, struct cmd_chflags);
uint32_t utf8_combine(const struct utf8_data *);
static void check_utf8(const unsigned char *bytes, size_t length,
    uint32_t codepoint, unsigned int width)
{
    struct utf8_data data;
    size_t i;
    assert(utf8_open(&data, bytes[0]) == 1);
    for (i = 1; i < length; i++)
        assert(utf8_append(&data, bytes[i]) == (i + 1 < length));
    assert(utf8_combine(&data) == codepoint);
    assert(data.width == width);
}
#undef INT_MAX
#define INT_MAX 32767
int main(void)
{
    const char *alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *error;
    struct cmd_chflags flags;
    struct key_binding lower, prefixed;
    char formatted[128];
    key_code key;
    size_t size;
    int i, j;
#ifndef TMUX_MILESTONE
    struct cmd command;
    struct cmd_target_data *layout_data;
    const char *layouts[] = { "even-horizontal", "even-vertical",
        "main-horizontal", "main-vertical" };
#endif

    for (i = 0; i < 52; i++) {
        cmd_clear_flags(&flags);
        cmd_add_flag(&flags, alphabet[i]);
        for (j = 0; j < 52; j++)
            assert(cmd_has_flag(flags, alphabet[j]) == (i == j));
    }
    cmd_clear_flags(&flags);
    for (i = 0; i < 52; i++)
        assert(cmd_flags(alphabet[i], alphabet, &flags) == 0);
    assert(cmd_flags('?', alphabet, &flags) == -1);
    assert(cmd_flags('a', NULL, &flags) == -1);
    assert(cmd_flags(0, alphabet, &flags) == -1);
    assert(cmd_print_flags(formatted, sizeof formatted, 0, flags) == 54);
    assert(strcmp(formatted,
        " -aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ") == 0);
    assert(KEYC_WORD(KEYC_UP) == KEYC_UP);
    assert(KEYC_WORD(KEYC_UP | KEYC_PREFIX) == KEYC_NONE);
    key = key_string_lookup_string("M-C-Up");
    assert(key == (KEYC_UP | KEYC_ESCAPE | KEYC_CTRL));
    assert(strcmp(key_string_lookup_key(key), "M-C-Up") == 0);
    key = xterm_keys_find("\033[1;8A", 6, &size);
    assert(key == (KEYC_UP | KEYC_SHIFT | KEYC_ESCAPE | KEYC_CTRL));
    assert(size == 6);
    assert(strcmp(xterm_keys_lookup(key), "\033[1;8A") == 0);
    lower.key = KEYC_UP;
    prefixed.key = KEYC_UP | KEYC_PREFIX;
    assert(lower.key != prefixed.key);
    assert(key_bindings_cmp(&lower, &prefixed) > 0);
    assert(key_bindings_cmp(&prefixed, &lower) < 0);
    assert(strtonum("2147483647", -2147483647L - 1, 2147483647L, &error) == 2147483647L);
    assert(error == NULL);
    assert(strtonum("2147483648", -2147483647L - 1, 2147483647L, &error) == 0);
    assert(error != NULL && strcmp(error, "too large") == 0);
#ifndef TMUX_MILESTONE
    for (i = 0; i < 4; i++) {
        memset(&command, 0, sizeof command);
        command.entry = &cmd_select_layout_entry;
        command.entry->init(&command, ('1' + i) | KEYC_ESCAPE);
        layout_data = command.data;
        assert(strcmp(layout_data->arg, layouts[i]) == 0);
        command.entry->free(&command);
    }
#endif
    utf8_build();
    check_utf8((const unsigned char *) "\xCC\x81", 2, 0x301, 0);
    check_utf8((const unsigned char *) "\xE2\x82\xAC", 3, 0x20AC, 1);
    check_utf8((const unsigned char *) "\xE4\xB8\x80", 3, 0x4E00, 2);
    check_utf8((const unsigned char *) "\xF0\xA0\x80\x80", 4, 0x20000, 2);
    check_utf8((const unsigned char *) "\xF0\xB0\x80\x80", 4, 0x30000, 2);
    check_utf8((const unsigned char *) "\xF3\xA0\x84\x80", 4, 0xE0100, 0);
    check_utf8((const unsigned char *) "\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 1);
    puts("52 flags, key modifiers/prefix, numeric bounds and Unicode through U+10FFFF passed");
    return 0;
}

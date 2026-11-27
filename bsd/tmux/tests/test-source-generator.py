#!/usr/bin/env python3
"""Regression checks for the native source generator: trace removal and shared strings."""
from pathlib import Path
import importlib.util
import re
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_sources', ROOT / 'tools/generate_native_sources.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class SourceGeneratorTests(unittest.TestCase):
    def test_control_flow_literals_and_declarations(self):
        source = r'''
static int calls;
void log_debug(const char *message, ...) { calls++; }
void log_debug2(const char *message, ...);
void log_debug2(const char *message, ...) { calls++; }
int main(void) {
    int condition = 0;
    const char *text = "log_debug(\"not executable\")";
    /* log_debug("also not executable"); */
    if (0) log_debug("unused"); else condition = 1;
    if (condition)
        log_debug2("nested ) \" /* { ; */", '(', (condition + (2 * 3)));
    else return 2;
    return calls != 0 || condition != 1 || text[0] != 'l';
}
'''
        result, calls, declarations = generator.transform(source, 'fixture.c')
        self.assertEqual(len(calls), 2)
        self.assertEqual(len(declarations), 3)
        self.assertEqual(source.count('\n'), result.count('\n'))
        self.assertIn('void log_debug(const char *message, ...) { calls++; }', result)
        self.assertIn('/* log_debug("also not executable"); */', result)
        with tempfile.TemporaryDirectory(prefix='tmux-trace-fixture-') as temporary:
            folder = Path(temporary)
            (folder / 'fixture.c').write_text(result)
            subprocess.run(['clang', '-Werror=implicit-function-declaration',
                            str(folder / 'fixture.c'), '-o', str(folder / 'fixture')], check=True)
            subprocess.run([str(folder / 'fixture')], check=True)

    def test_side_effects_require_review(self):
        for expression in ['counter++', '--counter', '(counter = 3)',
                           'buffer_read8(buffer)', 'mutate()', '(callback)(value)']:
            source = 'void f(void) { log_debug("%d", ' + expression + '); }'
            with self.subTest(expression=expression):
                with self.assertRaisesRegex(ValueError, 'require review'):
                    generator.transform(source, 'side-effect.c')

    def test_comment_literals_do_not_inflate_payload_count(self):
        source = 'void f(void) { log_debug(/* "ignored" */ "real\\n"); }'
        _, calls, _ = generator.transform(source, 'literal.c')
        self.assertEqual(calls[0]['literal_payload_bytes'], 5)

    def test_balanced_multiline_read_only_arguments(self):
        source = ('void f(void) {\nlog_debug("%d %d",\n'
                  'getpid(), ARRAY_LENGTH(&items));\n}\n')
        result, calls, _ = generator.transform(source, 'multiline.c')
        self.assertEqual(len(calls), 1)
        self.assertEqual(source.count('\n'), result.count('\n'))
        self.assertIn('((void)0)', result)


POOL = generator.pool_strings


def scan(source, defined=(), undefined=(), macros=None):
    conditions = POOL.Conditions({name: True for name in defined}, undefined)
    return POOL.scan(source, conditions, macros or {})


class StringPoolTests(unittest.TestCase):
    def test_only_certain_replacements_are_made(self):
        source = r'''
#include "not-a-string.h"
#define LOCAL "directive literal"
static char array[] = "array initializer";
static char sized[4] = "abc";
static const char *pointer = "pointer initializer";
static const char *table[] = { "first", "second" };
unsigned f(const char *s) {
    unsigned n = sizeof("in sizeof") + sizeof "bare sizeof";
    puts("joined " JOINED " to a macro");
    puts(PREFIX "after a macro");
    puts("two " /* comment */ "pieces\n");
    puts("hex \x41"); puts("alert \a"); puts("nul \0 inside");
    puts(USAGE); puts("[-d] " USAGE);
    if (*s) return n;
    return strlen("returned");
}
const char *g(void) { return "after return"; }
'''
        result = scan(source, macros={'USAGE': b'[-t target]'})
        shared = sorted(run.value for run in result.runs if run.value is not None)
        self.assertEqual(shared, sorted([
            b'pointer initializer', b'first', b'second', b'two pieces\n',
            b'[-t target]', b'[-d] [-t target]', b'returned', b'after return']))
        reasons = sorted(run.reason for run in result.runs if run.value is None)
        self.assertEqual(reasons, sorted([
            'possible char array initializer', 'possible char array initializer',
            'sizeof', 'follows sizeof', 'joined to JOINED', 'follows JOINED',
            'follows PREFIX', 'escape \\x', 'escape \\a', 'embedded NUL']))
        contexts = {run.value: run.context for run in result.runs if run.value}
        self.assertEqual(contexts[b'first'], 'init')
        self.assertEqual(contexts[b'returned'], 'strlen:0')

    def test_conditional_regions(self):
        source = r'''
#ifdef ON
const char *a = "on";
#else
const char *a = "off";
#endif
#if defined(ON) && !defined(OFF)
const char *b = "both";
#elif defined(MAYBE)
const char *b = "elif";
#else
const char *b = "neither";
#endif
#ifdef MAYBE
const char *c = "maybe";
#define LATER 1
#endif
#ifdef LATER
const char *d = "later";
#endif
#if 0
const char *e = "never";
#endif
#ifndef GUARD
#define GUARD
const char *f = "guarded";
#endif
#ifdef GUARD
const char *g = "after guard";
#endif
'''
        result = scan(source, defined=['ON'], undefined=['OFF', 'GUARD'])
        state = {run.value: run.active for run in result.runs}
        self.assertEqual(state, {
            b'on': True, b'off': False, b'both': True, b'elif': False,
            b'neither': False, b'maybe': None, b'later': None, b'never': False,
            b'guarded': True, b'after guard': True})
        self.assertEqual(result.unknown, {'MAYBE', 'LATER'})

    def test_layout_shares_tails_and_keeps_existing_offsets(self):
        uses = {b'window': 1, b'last-window': 1, b'%s': 4, b'zz': 2, b'once': 1,
                b'': 2, b'never compiled': 0}
        offsets, data = POOL.plan_pool(uses)
        self.assertNotIn(b'once', offsets)
        self.assertNotIn(b'never compiled', offsets)
        for value, offset in offsets.items():
            self.assertEqual(data[offset:offset + len(value) + 1], value + b'\0')
        self.assertEqual(offsets[b'window'], offsets[b'last-window'] + 5)
        self.assertEqual(data, b'last-window\0%s\0zz\0')
        self.assertEqual(data[offsets[b'']], 0)
        source = POOL.pool_source(data)
        layout = POOL.parse_pool(source)
        again, same = POOL.plan_pool(uses, layout)
        self.assertEqual((again, same), (offsets, data))
        # A new string is appended; one no longer wanted keeps its bytes so
        # that every other offset stays where compiled objects expect it.
        uses[b'added twice'] = 2
        uses[b'%s'] = 1
        grown, bigger = POOL.plan_pool(uses, layout)
        self.assertEqual(grown[b'last-window'], offsets[b'last-window'])
        self.assertEqual(grown[b'window'], offsets[b'window'])
        self.assertEqual(grown[b'zz'], offsets[b'zz'])
        self.assertNotIn(b'%s', grown)
        self.assertEqual(bigger[:len(data)], data)
        self.assertEqual(grown[b'added twice'], len(data))
        packed, small = POOL.plan_pool(uses)
        self.assertEqual(small, b'added twice\0last-window\0zz\0')
        # Nothing shared: no array content, and the empty string stays private.
        self.assertEqual(POOL.plan_pool({b'': 3, b'solo': 1}), ({}, b''))

    def test_rewritten_program_behaves_the_same(self):
        source = r'''
#include <stdio.h>
#include <string.h>
#define TARGET "[-t target]"
struct entry { const char *name; const char *usage; };
static const struct entry table[] = {
    { "attach", "[-d] " TARGET }, { "detach", TARGET },
    { "detach-all", "[-d] " TARGET },
};
static char writable[] = "attach";
int main(void) {
    char out[128];
    out[0] = 0;
    writable[0] = 'A';
    snprintf(out, sizeof out, "%s %s|%s %s|%s %s|%s", table[0].name,
        table[0].usage, table[1].name, table[1].usage, table[2].name,
        table[2].usage, writable);
    if (strcmp(out, "attach [-d] [-t target]|detach [-t target]|"
        "detach-all [-d] [-t target]|Attach") != 0) {
        puts(out);
        return 1;
    }
    return strcmp("attach", table[0].name) || strlen("detach") != 6;
}
'''
        macros = POOL.literal_macros(source)
        self.assertEqual(macros, {'TARGET': b'[-t target]'})
        scanned = scan(source, macros=macros)
        uses = {}
        for run in scanned.runs:
            if run.value is not None:
                uses[run.value] = uses.get(run.value, 0) + 1
        offsets, data = POOL.plan_pool(uses)
        self.assertIn(b'attach', offsets)
        self.assertIn(b'[-d] [-t target]', offsets)
        self.assertEqual(offsets[b'[-t target]'], offsets[b'[-d] [-t target]'] + 5)
        result, replaced, relocated = POOL.rewrite(source, scanned.runs, offsets)
        self.assertEqual(relocated, 0)
        self.assertEqual(source.count('\n'), result.count('\n'))
        self.assertGreaterEqual(replaced, 6)
        self.assertIn('static char writable[] = "attach";', result)
        with tempfile.TemporaryDirectory(prefix='tmux-pool-fixture-') as temporary:
            folder = Path(temporary)
            (folder / 'fixture.c').write_text('extern char xstr[];\n' + result)
            (folder / 'xstr.c').write_text(POOL.pool_source(data))
            subprocess.run(['clang', '-Werror', '-Wno-format-security',
                            '-DTMUX_XSTR_READONLY', str(folder / 'fixture.c'),
                            str(folder / 'xstr.c'), '-o', str(folder / 'fixture')], check=True)
            subprocess.run([str(folder / 'fixture')], check=True)

    def test_checked_in_copies_are_current_and_use_the_pool(self):
        subprocess.run([sys.executable, str(ROOT / 'tools/generate_native_sources.py'),
                        '--check'], check=True, stdout=subprocess.DEVNULL)
        layout = POOL.parse_pool((ROOT / 'native/xstr.c').read_text())
        self.assertGreater(len(layout), 100)
        size = layout[-1][0] + len(layout[-1][1]) + 1
        for copy in (ROOT / 'native-src').rglob('*.c'):
            text = copy.read_text()
            for offset in re.findall(r'\(&xstr\[(\d+)\]\)', text):
                self.assertLess(int(offset), size, copy.name)
            if '(&xstr[' in text:
                # Declared once, before the headers and any define using it.
                head = text[:text.index('#line 1 ')]
                self.assertEqual(head.count('extern char xstr[];\n'), 1)
                if '(&xstr[' in head:
                    self.assertLess(head.index('extern char xstr[];'),
                                    head.index('(&xstr['))

    def test_message_text_moves_to_the_file(self):
        source = r'''
void f(struct cmd_ctx *ctx, int n) {
    if (n == 0) fatal("first message here");
    if (n == 1) fatalx("short");
    if (n == 2) ctx->error(ctx, "session not found: %s", name);
    if (n == 3) log_warnx("first message here");
    if (n == 4) ctx->print(ctx, "printed text stays put");
    if (n == 5) xasprintf(&cause, "cause text stays put");
    if (n == 6) ctx->error(ctx, "joined " MACRO " text");
    if (n == 7) ctx->error(ctx, "%s", "an argument, not the message");
}
'''
        scanned = scan(source)
        wanted = {run.value for run in scanned.runs if POOL.is_message(run)}
        self.assertEqual(wanted, {b'first message here', b'session not found: %s'})
        messages, body = POOL.plan_messages(wanted)
        data = POOL.message_file(body)
        self.assertTrue(data.startswith(b'tmuxmsg '))
        self.assertEqual(data[POOL.MESSAGE_HEAD - 1:POOL.MESSAGE_HEAD], b'\n')
        self.assertEqual(data[8:16], POOL.message_stamp(body))
        for value, offset in messages.items():
            self.assertGreaterEqual(offset, POOL.MESSAGE_HEAD)
            self.assertEqual(data[offset:offset + len(value) + 1], value + b'\0')
        self.assertEqual(dict((value, offset) for offset, value in POOL.parse_messages(data)),
                         messages)
        result, replaced, relocated = POOL.rewrite(source, scanned.runs, {}, messages)
        self.assertEqual((replaced, relocated), (0, 3))
        first = messages[b'first message here']
        self.assertIn('fatal((tmux_msg(%d)));' % first, result)
        self.assertIn('log_warnx((tmux_msg(%d)));' % first, result)
        self.assertIn('ctx->error(ctx, (tmux_msg(%d)), name);' % messages[b'session not found: %s'],
                      result)
        for kept in ['fatalx("short")', '"printed text stays put"', '"cause text stays put"',
                     '"joined " MACRO " text"', '"%s", "an argument, not the message"']:
            self.assertIn(kept, result)
        # Text keeps its offset when messages are added; the stamp changes.
        more, bigger = POOL.plan_messages(wanted | {b'a message added later'},
                                          POOL.parse_messages(data))
        self.assertEqual({value: more[value] for value in messages}, messages)
        self.assertEqual(more[b'a message added later'], len(data))
        self.assertNotEqual(POOL.message_stamp(bigger), POOL.message_stamp(body))
        # A message that is also wanted elsewhere is shared there as usual.
        result, replaced, relocated = POOL.rewrite(
            source, scanned.runs, {b'printed text stays put': 5}, messages)
        self.assertEqual((replaced, relocated), (1, 3))
        self.assertIn('ctx->print(ctx, (&xstr[5]));', result)

    def test_checked_in_message_file_matches_the_copies(self):
        data = (ROOT / 'native/tmux.msg').read_bytes()
        layout = POOL.parse_messages(data)
        self.assertGreater(len(layout), 50)
        offsets = {offset for offset, _ in layout}
        stamp = data[8:16].decode()
        self.assertIn('char tmux_msg_stamp[] = "%s";' % stamp,
                      (ROOT / 'native/xstr.c').read_text())
        used = set()
        for copy in (ROOT / 'native-src').rglob('*.c'):
            text = copy.read_text()
            found = {int(n) for n in re.findall(r'\(tmux_msg\((\d+)\)\)', text)}
            if found:
                self.assertIn('extern char *tmux_msg(unsigned);\n', text[:text.index('#line 1 ')])
            used |= found
        self.assertTrue(used and used <= offsets)
        for _, value in layout:
            self.assertLessEqual(len(value), POOL.MESSAGE_MAX)


if __name__ == '__main__':
    unittest.main()

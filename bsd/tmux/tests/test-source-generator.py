#!/usr/bin/env python3
"""Regression checks for release trace-expression removal."""
from pathlib import Path
import importlib.util
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


if __name__ == '__main__':
    unittest.main()

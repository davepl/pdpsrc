#!/usr/bin/env python3
"""Test canonical identify functions with local OS/framing mocks only."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CC = 'clang'
SANITIZE = True


def function(text, name):
    """Extract an actual function while ignoring braces in comments/strings."""
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: re.sub(r'[^\n]', ' ', m.group()), text, flags=re.S)
    match = re.search(r'(?m)^(?:static )?(?:int|void)\n' + re.escape(name) + r'\(', masked)
    if match is None:
        raise ValueError('Missing canonical function: ' + name)
    opening = masked.index('{', match.start())
    depth = 1
    cursor = opening + 1
    while depth:
        if cursor == len(masked):
            raise ValueError('Unclosed canonical function: ' + name)
        depth += (masked[cursor] == '{') - (masked[cursor] == '}')
        cursor += 1
    return text[match.start():cursor] + '\n'


class IdentifyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix='tmux-identify-')
        cls.work = Path(cls.scratch.name)
        header = (ROOT / 'upstream/tmux-full.h').read_text()
        start = header.index('#ifdef TMUX_NATIVE_211BSD', header.index('#include "config.h"'))
        end = header.index('#endif', start) + len('#endif')
        definitions = '#define MAXPATHLEN 1024\n' + header[start:end] + '\n'
        # The message bounds differ between the native and host protocols:
        # carry the whole conditional block so each program gets its own.
        bounds = header.index('#ifdef TMUX_NATIVE_211BSD\n#define COMMAND_LENGTH')
        definitions += header[bounds:header.index('#endif', bounds) + len('#endif')] + '\n'
        for name in ['TERMINAL_LENGTH', 'TTY_PATH_LENGTH', 'CWD_LENGTH']:
            if re.search(r'^#define ' + name + r'\b', definitions, re.M) is None:
                raise ValueError('Missing protocol bound: ' + name)
        identify = re.search(r'^struct msg_identify_data\s*\{.*?^\};',
                             header, re.M | re.S).group()
        server = (ROOT / 'upstream/server-client.c').read_text()
        client = (ROOT / 'upstream/client.c').read_text()
        cls.programs = {}
        for name, template, functions, flags in [
                ('server', 'native-identify-open.c.in',
                 function(server, 'server_client_open_tty') +
                 function(server, 'server_client_identify_error'), ['-DTMUX_NATIVE_211BSD']),
                ('native-client', 'native-identify-client.c.in',
                 function(client, 'client_send_identify'), ['-DTMUX_NATIVE_211BSD']),
                ('host-client', 'native-identify-client.c.in',
                 function(client, 'client_send_identify'), [])]:
            text = (ROOT / 'tests' / template).read_text()
            for marker, value in [('PROTOCOL_DEFINITIONS', definitions),
                                  ('IDENTIFY_MESSAGE', identify),
                                  ('TESTED_FUNCTIONS', functions)]:
                text = text.replace('/* ' + marker + ' */', value)
            source = cls.work / (name + '.c')
            binary = cls.work / name
            source.write_text(text)
            command = [CC, '-std=c89', '-D_DEFAULT_SOURCE=1', '-Wall', '-Wextra',
                       '-Wno-unused-function', '-O0', '-g', *flags]
            if SANITIZE:
                command += ['-fsanitize=address']
            subprocess.run([*command, str(source), '-o', str(binary)], check=True)
            cls.programs[name] = binary

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def run_case(self, name):
        environment = os.environ.copy()
        environment['ASAN_OPTIONS'] = 'symbolize=0'
        subprocess.run([str(self.programs[name])], check=True, env=environment)

    def test_native_path_validation_and_cleanup(self):
        self.run_case('server')

    def test_native_encoder_sends_path_without_descriptor(self):
        self.run_case('native-client')

    def test_host_encoder_retains_descriptor_transport(self):
        self.run_case('host-client')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--no-sanitize', action='store_true')
    args, remaining = parser.parse_known_args()
    CC = args.cc
    SANITIZE = not args.no_sanitize
    unittest.main(argv=[__file__, *remaining])

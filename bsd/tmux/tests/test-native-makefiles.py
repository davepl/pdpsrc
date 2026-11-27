#!/usr/bin/env python3
"""Exercise generated native build logic with host make and fake target tools."""

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from pack_overlays import parse_sizes
SELECT_HOST_MILESTONE = runpy.run_path(
    str(ROOT / 'tests/run-host-regressions.py'))['select_milestone_sources']


class NativeMakefiles(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tmux-native-make-test-')
        self.root = Path(self.temp.name)
        self.work = self.root / 'upstream'
        for folder in ['upstream/compat', 'native', 'native/trees', 'native-src/compat',
                       'tools', 'native-mk']:
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        self.seed = (ROOT / 'native/objects-full.list').read_bytes()
        (self.root / 'native/objects-full.list').write_bytes(self.seed)
        self.full = [line.strip() for line in self.seed.decode().splitlines()
                     if line.strip() and not line.lstrip().startswith('#')]
        for obj in self.full:
            source = obj[:-2] + '.c'
            (self.work / source).write_text('/* source sentinel */\n')
            if not source.startswith('../native/'):
                (self.root / 'native-src' / source).write_text('/* generated sentinel */\n')
        shutil.copy2(ROOT / 'upstream/cmd.c', self.work / 'cmd.c')
        # Compare native generation with the separately maintained host filter.
        # Retain any new native helpers without hardcoding either object count.
        oracle = self.root / 'oracle/upstream'
        shutil.copytree(self.work, oracle)
        with redirect_stdout(io.StringIO()):
            SELECT_HOST_MILESTONE(oracle)
        kept = {str(path.relative_to(oracle)) for path in oracle.rglob('*.c')}
        self.expected = {
            'full': self.full,
            'milestone': [obj for obj in self.full if obj.startswith('../native/')
                          or obj[:-2] + '.c' in kept],
        }
        for name in ['tmux.h', 'tmux-full.h', 'tmux-native.h', 'config.h', 'compat.h']:
            (self.work / name).write_text('/* header sentinel */\n')
        for name in ['termios.h', 'utf8-width.inc']:
            (self.root / 'native' / name).write_text('/* native header sentinel */\n')
        # The link step installs the error text beside the executable.
        (self.root / 'native/tmux.msg').write_bytes(b'tmuxmsg 00000000\nmessage sentinel\0')
        for tree in (ROOT / 'native/trees').glob('*.inc'):
            (self.root / 'native/trees' / tree.name).write_text('/* tree sentinel */\n')
        self.commands = self.root / 'commands.jsonl'
        self.links = self.root / 'links.txt'
        self.env = os.environ.copy()
        self.env.update(COMMAND_LOG=str(self.commands), LINK_LOG=str(self.links))
        cc = self.root / 'tools/cc211'
        cc.write_text('#!' + sys.executable + '\n' + '''import json, os, pathlib, sys
args = sys.argv[1:]
obj = args[args.index('-o') + 1]
with open(os.environ['COMMAND_LOG'], 'a') as log:
    log.write(json.dumps({'object': obj, 'args': args}) + '\\n')
if os.environ.get('FAIL_OBJECT') == obj:
    sys.exit(7)
pathlib.Path(obj).write_bytes(b'object')
''')
        cc.chmod(0o755)
        (self.root / 'tools/ld211').write_text('private linker sentinel\n')
        bindir = self.root / 'bin'
        bindir.mkdir()
        size = bindir / 'size'
        size.write_text('#!' + sys.executable + '\n' + '''import os, pathlib, sys
names = sys.argv[1:]
for name in names:
    if not pathlib.Path(name).exists() or os.environ.get('FAIL_SIZE') == name:
        sys.exit(9)
    print('1 2 3 6 6' + (' ' + name if len(names) > 1 else ''))
''')
        size.chmod(0o755)
        self.env['PATH'] = str(bindir) + os.pathsep + self.env['PATH']
        for profile, binary in [('milestone', 'tmux'), ('full', 'tmux-full')]:
            (self.root / 'native' / ('link-' + profile + '.sh')).write_text(
                'echo ' + profile + ' >> "$LINK_LOG"\n: > ' + binary + '\n')
        self.generate('milestone')

    def tearDown(self):
        self.temp.cleanup()

    def generate(self, profile):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/generate_native_makefiles.py'),
                                 '--root', str(self.root), '--profile', profile],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'native/objects-full.list').read_bytes(), self.seed)

    def make(self, target, **environment):
        env = dict(self.env, **environment)
        return subprocess.run(['make', '-f', '../Makefile.211bsd', target],
                              cwd=self.work, env=env, text=True, capture_output=True)

    def records(self):
        if not self.commands.exists():
            return []
        return [json.loads(line) for line in self.commands.read_text().splitlines()]

    def test_profiles_build_and_sizes_match_selected_objects(self):
        for profile, binary in [('milestone', 'tmux'), ('full', 'tmux-full'),
                                ('milestone', 'tmux')]:
            # This explicit cleanup is required when using shared object paths.
            clean = self.make('clean-objects')
            self.assertEqual(clean.returncode, 0, clean.stdout + clean.stderr)
            self.generate(profile)
            selected = (self.root / 'native/objects.list').read_text().splitlines()
            self.assertEqual(selected, self.expected[profile])
            count = len(self.expected[profile])
            self.assertEqual(len(set(selected)), count)
            child_files = sorted((self.root / 'native-mk').glob('G*.mk'))
            self.assertEqual(len(child_files), (count + 7) // 8)
            for child in child_files:
                text = child.read_text()
                members = re.search(r'^OBJS=(.*)$', text, re.M)[1].split()
                self.assertLessEqual(len(members), 8)
                self.assertEqual('-DTMUX_MILESTONE' in text, profile == 'milestone')
            self.commands.unlink(missing_ok=True)
            result = self.make('all')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((self.work / binary).exists())
            self.assertEqual((self.work / 'tmux.msg').read_bytes(),
                             (self.root / 'native/tmux.msg').read_bytes())
            rows = self.records()
            self.assertEqual([row['object'] for row in rows], selected)
            for row in rows:
                self.assertEqual('-DTMUX_MILESTONE' in row['args'], profile == 'milestone')
            sizes = self.make('sizes')
            self.assertEqual(sizes.returncode, 0, sizes.stdout + sizes.stderr)
            self.assertEqual(set(parse_sizes(sizes.stdout)), set(selected))
            self.assertEqual(self.links.read_text().splitlines()[-1], profile)

    def test_singleton_size_record_keeps_object_name(self):
        # The current manifests need not end in one object, so force that case.
        self.full = self.full[:9]
        self.seed = ('\n'.join(self.full) + '\n').encode()
        (self.root / 'native/objects-full.list').write_bytes(self.seed)
        self.generate('full')
        result = self.make('objects')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(set(parse_sizes(result.stdout)), set(self.full))
        self.assertIn('\n' + self.full[-1] + ':\n', result.stdout)

    def test_compile_failure_propagates_and_prevents_link(self):
        result = self.make('all', FAIL_OBJECT='attributes.o')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.links.exists())
        # Later groups can still report errors, but aggregate status stays bad.
        self.assertIn('../native/protocol-check.o', [row['object'] for row in self.records()])

    def test_size_failure_propagates_and_prevents_link(self):
        result = self.make('all', FAIL_SIZE='attributes.o')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.links.exists())

    def test_clean_objects_preserves_binaries_and_sources(self):
        for obj in self.full:
            (self.work / obj).write_bytes(b'stale object')
        for binary in ['tmux', 'tmux-full']:
            (self.work / binary).write_bytes(binary.encode())
        before = (self.work / 'cmd.c').read_bytes()
        result = self.make('clean-objects')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(all(not (self.work / obj).exists() for obj in self.full))
        for binary in ['tmux', 'tmux-full']:
            self.assertEqual((self.work / binary).read_bytes(), binary.encode())
        self.assertEqual((self.work / 'cmd.c').read_bytes(), before)
        self.assertTrue((self.root / 'tools/cc211').exists())

    def test_generated_width_dependency_rebuilds_utf8_only(self):
        result = self.make('objects')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.commands.unlink()
        old = (self.work / 'utf8.o').stat().st_mtime_ns
        width = self.root / 'native/utf8-width.inc'
        os.utime(width, ns=(old + 2_000_000_000, old + 2_000_000_000))
        result = self.make('objects')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual([row['object'] for row in self.records()], ['utf8.o'])


if __name__ == '__main__':
    unittest.main()

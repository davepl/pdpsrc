#!/usr/bin/env python3
"""Build and test a temporary host copy, leaving repository configuration intact."""
from pathlib import Path
import argparse
import platform
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd, log):
    result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        log.flush()
        raise subprocess.CalledProcessError(result.returncode, command)


def select_milestone_sources(work):
    """Use the canonical command allowlist, matching the native object filter."""
    text = (work / 'cmd.c').read_text()
    start = text.index('#ifdef TMUX_MILESTONE', text.index('cmd_table[]'))
    branch = text[start:].split('#else', 1)[0]
    commands = re.findall(r'&cmd_([a-z_]+)_entry', branch)
    if len(commands) != 26 or len(set(commands)) != 26:
        raise ValueError('Expected the explicit 26-command milestone allowlist')
    keep = {'cmd-' + name.replace('_', '-') + '.c' for name in commands}
    keep.update({'cmd-generic.c', 'cmd-list.c', 'cmd-string.c'})
    missing = [name for name in keep if not (work / name).exists()]
    if missing:
        raise ValueError('Missing milestone source files: ' + ', '.join(missing))
    removed = []
    for source in work.glob('cmd-*.c'):
        if source.name not in keep:
            removed.append(source.name)
            source.unlink()
    for name in ['window-copy.c', 'window-choose.c', 'window-clock.c',
                 'layout-set.c', 'options-cmd.c']:
        (work / name).unlink()
        removed.append(name)
    # clock.c is retained: screen-redraw.c still uses its pane-number glyphs.
    (work.parent / 'milestone-sources.txt').write_text(
        '\n'.join(sorted(p.name for p in work.glob('*.c'))) + '\n')
    print(f'Milestone build: {len(commands)} commands; {len(removed)} excluded sources')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--milestone', action='store_true',
                        help='build the explicit 26-command two-window profile')
    parser.add_argument('--keep', action='store_true', help='retain the scratch build and log')
    args = parser.parse_args()
    subprocess.run(['python3', str(ROOT / 'tests/test-native-allocator.py'),
                    '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-allocator-stats.py'),
                    '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-native-identify.py'),
                    '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-client-tty-access.py'),
                    '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-native-input-args.py'), '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-source-generator.py')], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-asprintf.py'), '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-xsnprintf-native.py'), '--cc', args.cc], check=True)
    subprocess.run(['python3', str(ROOT / 'tests/test-imsg-native.py'), '--cc', args.cc], check=True)
    system = platform.system()
    if system not in {'Darwin', 'Linux'}:
        parser.error('the isolated host build currently supports macOS and Linux')
    scratch = Path(tempfile.mkdtemp(prefix='tmux-host-regression-'))
    work = scratch / 'upstream'
    log_path = scratch / 'build.log'
    passed = False
    try:
        shutil.copytree(ROOT / 'upstream', work,
                        ignore=shutil.ignore_patterns('*.o', 'tmux', '*.dSYM'))
        # The generator also inspects native source/header dependencies, but
        # the host build does not use or execute the target compiler binaries.
        for source in (ROOT / 'native').rglob('*'):
            if source.suffix not in {'.c', '.h', '.inc'}:
                continue
            target = scratch / 'native' / source.relative_to(ROOT / 'native')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        with log_path.open('w') as log:
            run(['python3', str(ROOT / 'tools/generate_native_widths.py'),
                 '--root', str(scratch)], ROOT, log)
            run(['python3', str(ROOT / 'tools/generate_native_header.py'),
                 '--root', str(scratch)], ROOT, log)
            run(['python3', str(ROOT / 'tools/generate_native_sources.py'),
                 '--root', str(scratch)], ROOT, log)
            # Build precisely the release-generator output. The source files
            # replace only C copies inside this temporary host build.
            for generated in (scratch / 'native-src').rglob('*.c'):
                target = work / generated.relative_to(scratch / 'native-src')
                shutil.copy2(generated, target)
            # Exercise pre-expanded trees while retaining host ABI/system types.
            for source in work.rglob('*.c'):
                text = source.read_text()
                text = re.sub(r'#if defined\(TMUX_NATIVE_211BSD\) \|\| defined\(PDP11_NATIVE\)\n'
                              r'(?=#include "\.\./native/trees/)', '#if 1\n', text)
                source.write_text(text)
            if args.milestone:
                select_milestone_sources(work)
            run(['sh', 'configure'], work, log)
            # This intentional scratch-only selector exercises native per-unit
            # declaration pruning with the host compiler and host system types.
            (work / 'tmux.h').write_text('#include "tmux-native.h"\n')
            with (work / 'config.h').open('a') as config:
                config.write('\n#define BUILD "host-regression"\n')
            profile = ['-DTMUX_SMALL_MEMORY=1', '-DTMUX_IO_SIZE=256',
                       '-DTMUX_NATIVE_WIDTH_CODE=1']
            if args.milestone:
                profile += ['-DTMUX_MILESTONE=1', '-DTMUX_NATIVE_GRID_ALLOC=1',
                            '-DTMUX_NATIVE_HEAP_TEST=1']
            flags = ('-O0 -g -Wno-pointer-sign ' + ' '.join(profile) + ' '
                     '-Werror=implicit-function-declaration '
                     '-Werror=incompatible-function-pointer-types -Werror=format '
                     '-DPDP11_UNIT_$(subst -,_,$(notdir $*))')
            run(['make', '-f', 'GNUmakefile', 'CC=' + args.cc,
                 'CFLAGS=' + flags, '-j4'], work, log)
            run([args.cc, '-O0', '-g', '-iquote.', *profile, '-DPDP11_UNIT_tmux',
                 '-Dmain=tmux_program_main', '-c', 'tmux.c',
                 '-o', 'regression-globals.o'], work, log)
            objects = [str(p.relative_to(work)) for p in sorted(work.glob('*.o'))
                       if p.name != 'tmux.o']
            objects += [str(p.relative_to(work)) for p in sorted((work / 'compat').glob('*.o'))]
            test_names = ['type-portability', 'zero-history', 'grid-compare',
                          'heap-profile',
                          'status-replace', 'tty-term-strip']
            if args.milestone:
                test_names += ['milestone-profile', 'milestone-cells',
                               'compact-rows']
            for name in test_names:
                run([args.cc, '-O0', '-g', '-iquote.', *profile, str(ROOT / 'tests' / (name + '.c')),
                     *objects, '-lcurses', '-o', name], work, log)
                subprocess.run([str(work / name)], cwd=work, check=True)
            subprocess.run([str(work / 'zero-history'), '--one-row'], cwd=work, check=True)
            capacity_profiles = [profile]
            if not args.milestone:
                capacity_profiles.append(profile + ['-DTMUX_NATIVE_GRID_ALLOC=1'])
            for capacity_profile in capacity_profiles:
                # Guard only grid allocations, leaving normal screen/program
                # objects available for logical resize and ownership checks.
                hooks = ['-D' + name + '=grid_capacity_' + name
                         for name in ['xmalloc', 'xcalloc', 'xrealloc', 'xfree']]
                run([args.cc, '-O0', '-g', '-iquote.', *capacity_profile,
                     '-DPDP11_UNIT_grid', *hooks, '-c', 'grid.c',
                     '-o', 'capacity-grid.o'], work, log)
                capacity_objects = [obj for obj in objects if obj != 'grid.o']
                run([args.cc, '-O0', '-g', '-iquote.', *capacity_profile,
                     str(ROOT / 'tests/grid-capacity.c'), *capacity_objects,
                     'capacity-grid.o', '-lcurses', '-o', 'grid-capacity'], work, log)
                subprocess.run([str(work / 'grid-capacity')], cwd=work, check=True)
            # Fault injection is limited to this test buffer object; libc and
            # the full-program regression objects keep their real realloc.
            run([args.cc, '-O0', '-g', '-iquote.', *profile,
                 '-DPDP11_UNIT_buffer', '-Drealloc=memory_profile_realloc',
                 '-c', 'buffer.c', '-o', 'memory-buffer.o'], work, log)
            run([args.cc, '-O0', '-g', '-iquote.', *profile,
                 str(ROOT / 'tests/memory-profile.c'), 'memory-buffer.o',
                 'buffer-poll.o', 'mode-key.o', '-o', 'memory-profile'], work, log)
            subprocess.run([str(work / 'memory-profile')], cwd=work, check=True)
            if args.milestone:
                # Keep the real packet dispatcher; control command completion
                # only to exercise attachment/environment cleanup boundaries.
                run([args.cc, '-O0', '-g', '-iquote.', *profile,
                     '-DPDP11_UNIT_server_client',
                     '-Dcmd_list_exec=dispatch_test_cmd_list_exec',
                     '-c', 'server-client.c', '-o', 'dispatch-server-client.o'], work, log)
                dispatch_objects = [obj for obj in objects if obj != 'server-client.o']
                run([args.cc, '-O0', '-g', '-iquote.', *profile,
                     str(ROOT / 'tests/dispatch-payload.c'), *dispatch_objects,
                     'dispatch-server-client.o', '-lcurses', '-o', 'dispatch-payload'], work, log)
                subprocess.run([str(work / 'dispatch-payload')], cwd=work, check=True)
            # Keep one renamed baseline object with the original table. All
            # ordinary test/program objects above use the generated width code.
            table_profile = [flag for flag in profile if 'TMUX_NATIVE_WIDTH_CODE' not in flag]
            aliases = ['-Dutf8_' + name + '=utf8_table_' + name
                       for name in ['build', 'open', 'append', 'combine', 'width', 'overlap']]
            run([args.cc, '-O0', '-g', '-iquote.', *table_profile, *aliases,
                 '-DPDP11_UNIT_utf8', '-c', 'utf8.c', '-o', 'width-table-baseline.o'], work, log)
            run([args.cc, '-O0', '-g', '-iquote.', *profile,
                 str(ROOT / 'tests/utf8-width-code.c'), *objects,
                 'width-table-baseline.o', '-lcurses', '-o', 'utf8-width-code'], work, log)
            subprocess.run([str(work / 'utf8-width-code')], cwd=work, check=True)
        passed = True
        print(('Milestone' if args.milestone else 'Full-profile') +
              ' host regressions passed; repository configuration was not changed.')
    except subprocess.CalledProcessError:
        if log_path.exists():
            print(log_path.read_text(errors='replace')[-12000:])
        print('Scratch build retained:', scratch)
        raise
    finally:
        if passed and not args.keep:
            shutil.rmtree(scratch)
        elif passed:
            print('Scratch build retained:', scratch)


if __name__ == '__main__':
    main()

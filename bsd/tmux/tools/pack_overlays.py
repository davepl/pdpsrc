#!/usr/bin/env python3
"""Plan 2.11BSD overlays from measured native object sizes; never run a link.

Run on the modern host. The JSON plan and optional shell script are candidates,
not proof that startup code, library extraction and linker thunks fit.
"""

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import sys

SEGMENT = 8192
TEXT_SPACE = 65536
NOVL = 15
PROJECT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class ObjectSize:
    name: str
    text: int
    data: int
    bss: int


def normalize(name):
    return str(PurePosixPath(name))


def object_names(path):
    """Read an object list, or the current object arguments in a shell script."""
    tokens = shlex.split(path.read_text().replace('\\\n', ' '), comments=True)
    names = [normalize(token) for token in tokens if token.endswith('.o')]
    if not names:
        raise ValueError('No .o input names found in ' + str(path))
    if len(names) != len(set(names)):
        raise ValueError('Object input list contains duplicates')
    return names


def parse_sizes(text):
    """Accept native size output, name:text/data/bss rows, or a JSON manifest."""
    records = {}

    def add(name, values):
        name = normalize(name)
        if not name.endswith('.o'):
            raise ValueError('Expected an object filename, got ' + name)
        values = tuple(int(value) for value in values)
        if len(values) != 3 or any(value < 0 or value > 65535 for value in values):
            raise ValueError('Invalid 16-bit native section size for ' + name)
        row = ObjectSize(name, *values)
        if name in records and records[name] != row:
            raise ValueError('Conflicting size records for ' + name)
        records[name] = row

    if text.lstrip().startswith(('{', '[')):
        manifest = json.loads(text)
        if isinstance(manifest, dict):
            manifest = manifest.get('objects', manifest)
        if isinstance(manifest, dict):
            manifest = [dict(value, name=name) for name, value in manifest.items()]
        for row in manifest:
            add(row.get('name', row.get('object')), [row[key] for key in ('text', 'data', 'bss')])
    else:
        pending_name = None
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            # Native size: text data bss dec hex filename.o.
            match = re.fullmatch(r'(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+(?:0x)?[0-9a-fA-F]+(?:\s+(.+\.o))?', line)
            if match:
                name = match[4] or pending_name
                if name is None:
                    raise ValueError('Size row has no filename; run size with multiple inputs or prefix each invocation with filename.o:')
                add(name, match.group(1, 2, 3))
                pending_name = None
                continue
            # Also accept explicit manifests: filename.o text data bss.
            match = re.fullmatch(r'(.+\.o):?\s+(\d+)\s+(\d+)\s+(\d+)', line)
            if match:
                add(match[1], match.group(2, 3, 4))
                continue
            match = re.fullmatch(r'(.+\.o):', line)
            if match:
                pending_name = match[1]
    if not records:
        raise ValueError('No native object size records found')
    return records


def select_objects(records, expected):
    selected = []
    used = set()
    for name in expected:
        row = records.get(name)
        if row is None:
            # Permit absolute native paths when the relative suffix is unique.
            suffix = '/'.join(part for part in PurePosixPath(name).parts if part != '..')
            matches = [value for key, value in records.items() if key.endswith('/' + suffix)]
            if len(matches) == 1:
                row = matches[0]
        if row is None:
            raise ValueError('Missing native size for ' + name)
        if row.name in used:
            raise ValueError('One size record matched multiple inputs: ' + row.name)
        used.add(row.name)
        selected.append(ObjectSize(name, row.text, row.data, row.bss))
    return selected, sorted(set(records) - used)


def pack(objects, resident_names, bank_size, slots):
    if slots < 1 or slots > NOVL:
        raise ValueError('Application overlay count must be between 1 and 15')
    names = {obj.name for obj in objects}
    missing = set(resident_names) - names
    if missing:
        raise ValueError('Resident objects absent from inputs: ' + ', '.join(sorted(missing)))
    pinned = set(resident_names)
    resident = [obj for obj in objects if obj.name in pinned or obj.text == 0]
    work = [obj for obj in objects if obj not in resident]
    order = {obj.name: index for index, obj in enumerate(objects)}
    banks = []
    for obj in sorted(work, key=lambda item: (-item.text, order[item.name])):
        if obj.text > bank_size:
            raise ValueError(f'{obj.name} text {obj.text} exceeds bank limit {bank_size}; measure a larger window or split that native unit')
        fits = [(bank_size - sum(item.text for item in bank) - obj.text, index)
                for index, bank in enumerate(banks)
                if sum(item.text for item in bank) + obj.text <= bank_size]
        if fits:
            banks[min(fits)[1]].append(obj)
        elif len(banks) < slots:
            banks.append([obj])
        else:
            raise ValueError(f'Best-fit packing exceeds {slots} application overlays; try a 32768-byte bank or revise the measured partition')
    for bank in banks:
        bank.sort(key=lambda obj: order[obj.name])
    return resident, banks


def round_segment(value):
    return (value + SEGMENT - 1) // SEGMENT * SEGMENT


def make_plan(objects, resident, banks, args, extras):
    largest = max((sum(obj.text for obj in bank) for bank in banks), default=0)
    overlay_window = round_segment(largest)
    if args.overlay_library:
        overlay_window = max(overlay_window, args.library_window)
    warnings = [
        'Raw object text excludes loader thunks, startup code, library extraction and alignment.',
        'Only the final native linker/header check and runtime session tests can validate the layout.',
    ]
    if args.overlay_library:
        warnings.append('The library bank is unmeasured; its configured window is a planning allowance, not an extracted native size.')
    data_bss = sum(obj.data + obj.bss for obj in objects)
    if data_bss > 7 * SEGMENT:
        warnings.append('Application data+BSS alone exceeds 56 KiB, before library data, heap and stack.')
    resident_text = sum(obj.text for obj in resident)
    if resident_text + overlay_window > TEXT_SPACE:
        raise ValueError('Resident raw application text plus the reserved overlay window already exceeds 64 KiB')
    return {
        'algorithm': 'deterministic best-fit decreasing on measured native text',
        'bank_limit': args.bank_size,
        'novl': NOVL,
        'reserved_unused_banks': args.reserve_banks,
        'application_object_count': len(objects),
        'application_raw_text': sum(obj.text for obj in objects),
        'application_data_plus_bss': data_bss,
        'resident': {'raw_text': resident_text, 'objects': [asdict(obj) for obj in resident]},
        'banks': [{'number': index + 1, 'raw_text': sum(obj.text for obj in bank),
                   'objects': [asdict(obj) for obj in bank]} for index, bank in enumerate(banks)],
        'overlay_libraries': args.overlay_library,
        'library_window_allowance': args.library_window if args.overlay_library else None,
        'resident_libraries': args.resident_library,
        'reserved_overlay_window': overlay_window,
        'resident_window_budget': TEXT_SPACE - overlay_window,
        'unmeasured_resident_space_remaining': TEXT_SPACE - overlay_window - resident_text,
        'extra_size_records_ignored': extras,
        'warnings': warnings,
    }


def candidate_script(plan, args):
    words = [args.linker, '-i', '-o', args.executable, *args.link_arg, args.startup]
    words += [obj['name'] for obj in plan['resident']['objects']]
    for bank in plan['banks']:
        words += ['-Z', *[obj['name'] for obj in bank['objects']]]
    if args.overlay_library:
        words += ['-Z', *args.overlay_library]
    if plan['banks'] or args.overlay_library:
        words.append('-Y')
    words += args.resident_library
    lines = []
    line = ''
    for word in words:
        quoted = shlex.quote(word)
        if line and len(line) + len(quoted) + 1 > 100:
            lines.append(line)
            line = '    ' + quoted
        else:
            line += (' ' if line else '') + quoted
    lines.append(line)
    command = ' \\\n'.join(lines)
    return ('#!/bin/sh\n# Generated candidate only. Run from bsd/tmux/upstream.\n'
            '# Validate final native sizes and runtime behavior before adoption.\n'
            + command + '\nresult=$?\necho TMUX_LINK_STATUS=$result\nexit "$result"\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sizes', type=Path)
    parser.add_argument('--objects-from', type=Path, default=PROJECT / 'native/objects.list')
    parser.add_argument('--resident', action='append', default=[])
    parser.add_argument('--bank-size', type=int, choices=(16384, 24576, 32768), default=24576)
    parser.add_argument('--reserve-banks', type=int, default=0)
    parser.add_argument('--overlay-library', action='append', default=[])
    parser.add_argument('--library-window', type=int, choices=(8192, 16384, 24576, 32768), default=32768)
    parser.add_argument('--resident-library', action='append')
    parser.add_argument('--linker', default='../tools/ld211')
    parser.add_argument('--link-arg', action='append', default=[])
    parser.add_argument('--startup', default='/lib/crt0.o')
    parser.add_argument('--executable', default='tmux')
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--script', type=Path)
    args = parser.parse_args()
    try:
        if args.reserve_banks < 0:
            raise ValueError('--reserve-banks cannot be negative')
        if args.overlay_library and args.resident_library is None:
            raise ValueError('Supply --resident-library explicitly when adding an overlay library bank')
        if args.resident_library is None:
            # libvmf holds the milestone's window history; see grid-history.c.
            args.resident_library = ['-lvmf', '-ltermlib', '-lc']
        expected = object_names(args.objects_from)
        objects, extras = select_objects(parse_sizes(args.sizes.read_text()), expected)
        slots = NOVL - args.reserve_banks - bool(args.overlay_library)
        resident_names = list(dict.fromkeys(['tmux.o', *map(normalize, args.resident)]))
        resident, banks = pack(objects, resident_names, args.bank_size, slots)
        plan = make_plan(objects, resident, banks, args, extras)
        encoded = json.dumps(plan, indent=2) + '\n'
        if args.plan:
            args.plan.parent.mkdir(parents=True, exist_ok=True)
            args.plan.write_text(encoded)
        else:
            print(encoded, end='')
        if args.script:
            args.script.parent.mkdir(parents=True, exist_ok=True)
            args.script.write_text(candidate_script(plan, args))
    except (OSError, ValueError, TypeError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()

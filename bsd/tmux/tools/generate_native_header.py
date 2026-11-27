#!/usr/bin/env python3
"""Generate the per-translation-unit 2.11BSD tmux declaration header.

The canonical source is upstream/tmux-full.h. Structs, enums, typedefs and
preprocessor directives are retained verbatim. Only its declaration tail is
filtered, using source identifiers plus transitive macro/declaration uses.
No host compiler, target headers or preprocessor are needed.

Run from any directory: python3 tools/generate_native_header.py
Optional: --root PATH --output PATH --report PATH
"""
from __future__ import annotations
import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path

IDENT = re.compile(r'\b[A-Za-z_]\w*\b')
NO_CODE = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
DIRECTIVE = re.compile(r'^[ \t]*#(?:[^\n]*\\\n)*[^\n]*(?:\n|$)', re.M)
MACRO = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)([^\n]*(?:\\\n[^\n]*)*)', re.M)
TREE = re.compile(r'\b(SPLAY|RB)_([A-Z_]+)\s*\(([^)]*)\)')


def blank(match: re.Match[str]) -> str:
    return ''.join('\n' if c == '\n' else ' ' for c in match.group())


def code(text: str) -> str:
    return NO_CODE.sub(blank, text)


def tokens(text: str) -> set[str]:
    return set(IDENT.findall(code(text)))


def tree_uses(text: str) -> set[str]:
    uses = set()
    for m in TREE.finditer(code(text)):
        family, op, arguments = m.groups()
        if op in {'HEAD', 'ENTRY', 'INIT', 'ROOT', 'EMPTY', 'LEFT', 'RIGHT',
                  'PARENT', 'COLOR', 'SET', 'SET_BLACKRED', 'INITIALIZER',
                  'PROTOTYPE', 'PROTOTYPE_STATIC', 'PROTOTYPE_INTERNAL'}:
            continue
        arguments = arguments.split(',')
        pos = 1 if op.startswith('FOREACH') else 0
        if len(arguments) <= pos:
            continue
        name = arguments[pos].strip()
        if IDENT.fullmatch(name):
            uses.add('__tmux_' + family.lower() + '_' + name)
    return uses


@dataclass
class Declaration:
    start: int
    end: int
    names: set[str]
    dependencies: set[str]


def declarations(text: str) -> list[Declaration]:
    marker = text.index('/* tmux.c */')
    # These two externs precede the marker but follow all type definitions.
    prefix = text.rfind('extern const struct set_option_entry', 0, marker)
    start = prefix if prefix >= 0 else marker
    masked = DIRECTIVE.sub(blank, code(text))
    items = []
    cursor = start
    for semicolon in re.finditer(';', masked[start:]):
        end = start + semicolon.end()
        while cursor < end and masked[cursor].isspace():
            cursor += 1
        if cursor == end:
            continue
        body = masked[cursor:end]
        macro = re.match(r'(SPLAY|RB)_PROTOTYPE(?:_STATIC)?\s*\(\s*(\w+)', body)
        if macro:
            names = {'__tmux_' + macro[1].lower() + '_' + macro[2]}
        else:
            function = re.search(r'\b([A-Za-z_]\w*)\s*\(', body)
            if function:
                names = {function[1]}
            elif body.lstrip().startswith('extern '):
                before_array = body.split('[', 1)[0].rstrip('; \t\n')
                names = {IDENT.findall(before_array)[-1]}
            else:
                raise ValueError('Unrecognized tail declaration: ' + text[cursor:end])
        deps = tokens(text[cursor:end]) - names
        # In addition to function declarations, generated tree prototypes need
        # the comparison declaration and helper macros referenced by their body.
        if macro:
            deps.add(macro[1] + '_PROTOTYPE')
        items.append(Declaration(cursor, end, names, deps))
        cursor = end
    if masked[cursor:].strip():
        raise ValueError('Unparsed header tail: ' + masked[cursor:cursor + 120])
    return items


def sanitize(stem: str) -> str:
    return re.sub(r'\W', '_', stem)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument('--output', type=Path)
    ap.add_argument('--report', type=Path)
    args = ap.parse_args()
    root = args.root.resolve()
    canonical = root / 'upstream/tmux-full.h'
    output = args.output or root / 'upstream/tmux-native.h'
    text = canonical.read_text()
    items = declarations(text)
    residual = list(text)
    for item in items:
        residual[item.start:item.end] = ' ' * (item.end - item.start)
    retained = DIRECTIVE.sub(blank, ''.join(residual))
    base = tokens(retained) | tree_uses(retained)
    macros: dict[str, set[str]] = {}
    headers = [canonical]
    for folder in [root / 'upstream', root / 'native']:
        headers += [p for p in folder.rglob('*.h')
                    if p.name not in {'tmux.h', 'tmux-full.h', 'tmux-native.h'}]
    for header in headers:
        logical = re.sub(r'\\\r?\n', ' ', header.read_text(errors='replace'))
        for m in MACRO.finditer(logical):
            macros.setdefault(m[1], set()).update(tokens(m[2]))
    units = {}
    paths = {}
    for folder in [root / 'upstream', root / 'native']:
        for source in sorted(folder.rglob('*.c')):
            if source.name.endswith('.native.c'):
                continue
            unit = sanitize(source.stem)
            if unit in paths:
                raise ValueError(f'Duplicate unit name {unit}: {paths[unit]}, {source}')
            paths[unit] = str(source.relative_to(root))
            source_text = source.read_text(errors='replace')
            need = tokens(source_text) | tree_uses(source_text) | base
            while True:
                before = len(need)
                for item in items:
                    if item.names & need:
                        need |= item.dependencies
                for name, dependencies in macros.items():
                    if name in need:
                        need |= dependencies
                if len(need) == before:
                    break
            units[unit] = need
    pieces = ['/* Generated by tools/generate_native_header.py; edit tmux-full.h. */\n']
    pos = 0
    for item in items:
        keep = [unit for unit in sorted(units) if item.names & units[unit]]
        pieces.append(text[pos:item.start])
        # Small continued physical lines also work with the old native cpp.
        guards = ['defined(PDP11_UNIT_' + unit + ')' for unit in keep]
        if guards:
            pieces.append('#if ' + ' || \\\n    '.join(guards) + '\n')
        else:
            pieces.append('#if 0\n')
        pieces.append(text[item.start:item.end])
        pieces.append('\n#endif\n')
        pos = item.end
    pieces.append(text[pos:])
    output.write_text(''.join(pieces))
    report = {
        'declarations': len(items),
        'units': {unit: {'source': paths[unit],
                         'kept': sum(bool(item.names & units[unit]) for item in items)}
                  for unit in sorted(units)},
    }
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Guarded {len(items)} declarations for {len(units)} source units')
    for name in ['attributes', 'tmux', 'cmd', 'server_client', 'imsg_buffer', 'termios']:
        if name in report['units']:
            print(f'  {name}: {report["units"][name]["kept"]} retained')


if __name__ == '__main__':
    main()

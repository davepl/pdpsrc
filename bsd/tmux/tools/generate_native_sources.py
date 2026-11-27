#!/usr/bin/env python3
"""Generate native release C copies with log_debug/log_debug2 calls omitted.

Canonical upstream C and all logging function declarations/definitions remain
unchanged. The generator replaces complete call expressions, preserving their
semicolons and line count. It refuses to omit argument expressions with possible
side effects. Run after canonical source changes; outputs live in native-src/.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

IGNORED = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
TRACE = re.compile(r'\blog_debug2?\b')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
# Only accessors currently found in trace arguments are accepted. Unknown calls
# fail the audit rather than silently removing a possible required side effect.
PURE = {'getpid', 'ARRAY_LENGTH', 'screen_size_x', 'screen_size_y', 'sizeof'}
DECL = re.compile(r'^\s*(?:extern\s+|static\s+)?(?:void|int|char|short|long|unsigned|signed)\b')


def blank(match):
    return ''.join('\n' if c == '\n' else ' ' for c in match.group())


def mask(text):
    return IGNORED.sub(blank, text)


def literal_bytes(token):
    """Count C string payload bytes, excluding terminator and macro expansions."""
    text = token[1:-1]
    total = 0
    i = 0
    while i < len(text):
        if text[i] != '\\':
            total += len(text[i].encode('utf-8'))
            i += 1
            continue
        i += 1
        if i == len(text):
            raise ValueError('Incomplete C string escape')
        if text[i] == '\n':
            i += 1
            continue
        if text[i] in '01234567':
            end = i + 1
            while end < min(i + 3, len(text)) and text[end] in '01234567':
                end += 1
            i = end
        elif text[i] == 'x':
            i += 1
            end = i
            while end < len(text) and text[end] in '0123456789abcdefABCDEF':
                end += 1
            if end == i:
                raise ValueError('Incomplete hexadecimal C escape')
            i = end
        else:
            i += 1
        total += 1
    return total


def transform(text, source):
    clean = mask(text)
    changes = []
    skipped = []
    depth = 0
    cursor = 0
    for match in TRACE.finditer(clean):
        before = clean[cursor:match.start()]
        depth += before.count('{') - before.count('}')
        cursor = match.end()
        opening = match.end()
        while opening < len(clean) and clean[opening].isspace():
            opening += 1
        if opening == len(clean) or clean[opening] != '(':
            continue
        nesting = 1
        closing = opening + 1
        while closing < len(clean) and nesting:
            if clean[closing] == '(':
                nesting += 1
            elif clean[closing] == ')':
                nesting -= 1
            closing += 1
        if nesting:
            raise ValueError(f'{source}: unbalanced trace call')
        after = closing
        while after < len(clean) and clean[after].isspace():
            after += 1
        line = text.count('\n', 0, match.start()) + 1
        boundary = max(clean.rfind(';', 0, match.start()),
                       clean.rfind('{', 0, match.start()),
                       clean.rfind('}', 0, match.start()))
        prefix = clean[boundary + 1:match.start()]
        # At file scope these are declarations/definitions, not executable
        # expressions. A following body also recognizes multiline definitions.
        if depth == 0 or (after < len(clean) and clean[after] == '{') or DECL.match(prefix):
            skipped.append({'line': line, 'name': match.group()})
            continue
        arguments = clean[opening + 1:closing - 1]
        hazards = []
        if '++' in arguments or '--' in arguments:
            hazards.append('increment/decrement')
        if re.search(r'(?<![=!<>])=(?!=)', arguments):
            hazards.append('assignment')
        callees = sorted(set(CALL.findall(arguments)))
        for callee in callees:
            if callee not in PURE:
                hazards.append('unreviewed function/mutator: ' + callee)
        if re.search(r'\)\s*\(', arguments):
            hazards.append('possible indirect function call')
        original = text[match.start():closing]
        literals = [m.group() for m in IGNORED.finditer(original)
                    if m.group().startswith('"')]
        changes.append({'start': match.start(), 'end': closing, 'line': line,
                        'name': match.group(), 'literal_payload_bytes': sum(map(literal_bytes, literals)),
                        'literal_tokens': len(literals), 'argument_calls': callees,
                        'hazards': hazards})
    hazards = [item for item in changes if item['hazards']]
    if hazards:
        detail = '; '.join(f'{source}:{item["line"]}: {", ".join(item["hazards"])}'
                           for item in hazards)
        raise ValueError('Trace arguments require review: ' + detail)
    generated = text
    for item in reversed(changes):
        original = text[item['start']:item['end']]
        generated = (generated[:item['start']] + '((void)0)' +
                     '\n' * original.count('\n') + generated[item['end']:])
    for item in changes:
        del item['start'], item['end']
    return generated, changes, skipped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--check', action='store_true', help='fail if checked-in outputs are stale')
    args = parser.parse_args()
    root = args.root.resolve()
    source_dir = root / 'upstream'
    output_dir = root / 'native-src'
    outputs = {}
    report = {'profile': 'native release; debug trace calls omitted', 'files': {},
              'removed_calls': 0, 'literal_payload_bytes': 0, 'hazards': []}
    for source in sorted(source_dir.rglob('*.c')):
        relative = source.relative_to(source_dir)
        text = source.read_text()
        generated, changes, skipped = transform(text, str(relative))
        result = ('/* Generated by tools/generate_native_sources.py; edit upstream/' +
                  str(relative) + '. */\n#line 1 "' + str(relative) + '"\n' + generated)
        outputs[output_dir / relative] = result
        report['files'][str(relative)] = {
            'source_sha256': hashlib.sha256(text.encode()).hexdigest(),
            'removed_calls': changes,
            'preserved_declarations': skipped,
        }
        report['removed_calls'] += len(changes)
        report['literal_payload_bytes'] += sum(item['literal_payload_bytes'] for item in changes)
    report['literal_count_note'] = ('Literal payload bytes are counted before preprocessing, excluding NUL '
                                    'terminators and macro expansions; this is not a measured linked-data saving '
                                    'because literals may be concatenated or pooled.')
    outputs[output_dir / 'TRACE-AUDIT.json'] = json.dumps(report, indent=2) + '\n'
    stale = []
    for target, text in outputs.items():
        if args.check:
            if not target.exists() or target.read_text() != text:
                stale.append(str(target.relative_to(root)))
        else:
            if not target.exists() or target.read_text() != text:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text)
    if stale:
        raise SystemExit('Stale generated sources: ' + ', '.join(stale))
    print(f'{len(report["files"])} C copies; {report["removed_calls"]} trace calls omitted; '
          f'{report["literal_payload_bytes"]} literal payload bytes; no argument side effects found')


if __name__ == '__main__':
    main()

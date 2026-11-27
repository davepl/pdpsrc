#!/usr/bin/env python3
"""Generate the native release C copies of the canonical tmux sources.

Two changes are made to each copy; canonical upstream C is never altered.

1. log_debug/log_debug2 calls are omitted. The generator replaces complete
   call expressions, preserving their semicolons and line count, and refuses
   to omit argument expressions with possible side effects.
2. String literals repeated in the native milestone build are replaced by
   references into one shared array, native/xstr.c (see pool_strings.py), as
   are each unit's file name for fatal()/fatalx() and the ARRAY_ENSURE text.
3. The text of error messages is replaced by tmux_msg(offset), which reads
   it from the message file native/tmux.msg when the error is reported.

Run after canonical source changes; outputs live in native-src/,
native/xstr.c and native/tmux.msg. --repack-strings lays the shared array
and the message file out afresh, which changes most copies and so recompiles
most of the native build.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pool_strings  # noqa: E402

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


# Names that cannot be defined when the native compiler runs. Anything else
# not defined by the build flags or the port's own headers counts as unknown,
# and a literal under an unknown condition is treated as compiled.
NEVER_DEFINED = {
    'DEBUG', 'TMUX_ALLOC_STATS', 'TMUX_NATIVE_HEAP_TEST', 'TMUX_NATIVE_GRID_ALLOC',
    'TMUX_NATIVE_WIDTH_CODE', 'BROKEN_VSNPRINTF', 'TMUX_XSTR_READONLY',
    '__OpenBSD__', '__FreeBSD__', '__NetBSD__', '__DragonFly__', '__APPLE__',
    '__linux__', '__GLIBC__', '__sun', '_AIX', '__hpux', '__CYGWIN__', '__GNUC__',
    '__STDC__', 'TIOCSCTTY',
}
# Defined by the port only when the system does not: unknown, not undefined.
SYSTEM_FALLBACKS = {
    'nitems', 'EAGAIN', 'SIZE_MAX', 'UINT16_MAX', 'INFTIM', 'WAIT_ANY', 'CMSG_SPACE',
    'CMSG_LEN', 'CMSG_ALIGN', 'TTY_NAME_MAX', 'MAXPATHLEN', 'PATH_MAX', 'IOV_MAX',
    'timercmp', 'timeradd', 'timersub', 'O_NOCTTY', 'TIOCFLUSH',
}
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')
DEFINE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)([^\n]*(?:\\\n[^\n]*)*)', re.M)


def native_conditions(root, profile_flags):
    """Macro state after the port's own headers, for one native profile."""
    # A name that only this port defines is undefined until that definition
    # is reached, which is what makes include guards and feature switches
    # such as GRID_COMPACT_ROWS decidable without the target's headers.
    own = set()
    for folder in [root / 'upstream', root / 'native']:
        for source in list(folder.rglob('*.h')) + list(folder.rglob('*.c')):
            own.update(match[1] for match in DEFINE.finditer(source.read_text(errors='replace')))
    own -= SYSTEM_FALLBACKS
    conditions = pool_strings.Conditions({name: True for name in profile_flags},
                                         (NEVER_DEFINED | own) - set(profile_flags))
    for header in ['upstream/config.h', 'upstream/compat.h', 'upstream/tmux-full.h']:
        pool_strings.scan((root / header).read_text(), conditions, {})
    if conditions.stack:
        raise ValueError('Unbalanced conditionals in the port headers')
    return conditions


def unit_conditions(base, unit):
    defined, undefined = base.copy_macros()
    conditions = pool_strings.Conditions(defined, undefined)
    conditions.defined['PDP11_UNIT_' + unit] = True
    conditions.is_defined = lambda name, inner=conditions.is_defined: (
        False if name.startswith(('PDP11_UNIT_', 'HAVE_')) and name not in conditions.defined
        else inner(name))
    return conditions


def fatal_reachers(root):
    """Identifiers through which a source file can expand fatal()/fatalx()."""
    return macro_reachers(root, {'fatal', 'fatalx'})


def macro_reachers(root, targets):
    """The targets and every header macro that can expand to one of them."""
    macros = {}
    for folder in [root / 'upstream', root / 'native']:
        for header in folder.rglob('*.h'):
            if header.name in {'tmux.h', 'tmux-native.h'}:
                continue
            logical = re.sub(r'\\\r?\n', ' ', header.read_text(errors='replace'))
            for match in DEFINE.finditer(logical):
                macros.setdefault(match[1], set()).update(IDENT.findall(mask(match[2])))
    reach = set(targets)
    while True:
        more = {name for name, uses in macros.items() if uses & reach} - reach
        if not more:
            return reach
        reach |= more


def milestone_units(root):
    """Generated sources that the checked-in native object list links."""
    listing = root / 'native/objects.list'
    if not listing.exists():
        return None
    return {name[:-2] + '.c' for name in listing.read_text().split()
            if not name.startswith('../')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--check', action='store_true', help='fail if checked-in outputs are stale')
    parser.add_argument('--repack-strings', action='store_true',
                        help='lay out the shared string array afresh')
    parser.add_argument('--no-string-pool', action='store_true',
                        help='keep every literal private, so a host compiler can check '
                             'format strings; native/xstr.c is not rewritten')
    args = parser.parse_args()
    root = args.root.resolve()
    source_dir = root / 'upstream'
    output_dir = root / 'native-src'
    pool_path = root / 'native/xstr.c'
    message_path = root / 'native/tmux.msg'
    outputs = {}
    report = {'profile': 'native release; debug trace calls omitted', 'files': {},
              'removed_calls': 0, 'literal_payload_bytes': 0, 'hazards': []}
    traced = {}
    for source in sorted(source_dir.rglob('*.c')):
        relative = source.relative_to(source_dir)
        text = source.read_text()
        generated, changes, skipped = transform(text, str(relative))
        traced[str(relative)] = generated
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

    # Shared strings. Uses are counted in the milestone units, under the
    # milestone configuration; every copy is then rewritten against that pool.
    base = native_conditions(root, ['TMUX_NATIVE_211BSD', 'PDP11_NATIVE', 'TMUX_MILESTONE'])
    macros = pool_strings.literal_macros((root / 'upstream/tmux-full.h').read_text())
    counted = milestone_units(root)
    scans = {}
    uses = {}
    unknown = set()
    wanted_messages = set()
    message_bytes = {}
    pool_report = {'units': {}, 'left_alone': {}}
    for relative, generated in traced.items():
        unit = re.sub(r'\W', '_', Path(relative).stem)
        scanned = pool_strings.scan(generated, unit_conditions(base, unit), macros)
        scans[relative] = scanned
        relocating = not args.no_string_pool
        if relocating:
            # Every profile's message text has a place in the file.
            wanted_messages.update(run.value for run in scanned.runs
                                   if pool_strings.is_message(run))
        if counted is not None and relative not in counted:
            continue
        unknown |= scanned.unknown
        for run in scanned.runs:
            if run.active is False:
                continue
            if run.value is None:
                pool_report['left_alone'][run.reason] = pool_report['left_alone'].get(run.reason, 0) + 1
            elif relocating and pool_strings.is_message(run):
                message_bytes[run.value] = len(run.value) + 1
            else:
                uses[run.value] = uses.get(run.value, 0) + 1
    existing = []
    existing_messages = []
    if not args.repack_strings:
        if pool_path.exists():
            existing = pool_strings.parse_pool(pool_path.read_text())
        if message_path.exists():
            existing_messages = pool_strings.parse_messages(message_path.read_bytes())
    messages, message_body = pool_strings.plan_messages(wanted_messages, existing_messages)
    # Text that reaches a unit through header macros, which the scan of
    # the unit cannot see: its own name for fatal() and fatalx(), and the
    # two messages of ARRAY_ENSURE. Each is defined, ahead of the headers,
    # as a reference into the shared array.
    reach = fatal_reachers(root)
    array_reach = macro_reachers(root, {'ARRAY_ENSURE'})
    header_text = {}
    for relative, generated in traced.items():
        names = set(IDENT.findall(mask(generated)))
        defines = []
        if reach & names:
            defines.append(('TMUX_UNIT', relative.encode()))
        if array_reach & names:
            defines += [('ARRAY_NUMBER_BIG', b'number too big'),
                        ('ARRAY_SIZE_BIG', b'size too big')]
        header_text[relative] = defines
    always = {value for relative, defines in header_text.items()
              if counted is None or relative in counted for _, value in defines}
    if args.no_string_pool:
        offsets, data = {}, b''
    else:
        offsets, data = pool_strings.plan_pool(uses, existing, always)
    private = sum((len(value) + 1) * number for value, number in uses.items())
    shared = sum((len(value) + 1) * number for value, number in uses.items() if value in offsets)
    for relative, generated in traced.items():
        pooled, replaced, relocated = pool_strings.rewrite(
            generated, scans[relative].runs, offsets, messages)
        prefix = ('/* Generated by tools/generate_native_sources.py; edit upstream/' +
                  relative + '. */\n')
        defines = [(name, offsets[value]) for name, value in header_text[relative]
                   if value in offsets]
        if 'TMUX_GENERATED_COPY' in generated and not args.no_string_pool:
            # Selects the code that reads the message file; see log.c.
            prefix += '#define TMUX_GENERATED_COPY 1\n'
        if relocated:
            prefix += 'extern char *' + pool_strings.MESSAGE_CALL + '(unsigned);\n'
        if replaced or defines:
            prefix += 'extern char ' + pool_strings.POOL_NAME + '[];\n'
        for name, offset in defines:
            prefix += '#define %s (&%s[%d])\n' % (name, pool_strings.POOL_NAME, offset)
        outputs[output_dir / relative] = prefix + '#line 1 "' + relative + '"\n' + pooled
        if counted is None or relative in counted:
            pool_report['units'][relative] = replaced
    pool_report.update({
        'counted_literal_bytes': private,
        'bytes_replaced_by_references': shared,
        'pool_bytes': len(data),
        'saving_bytes': shared - len(data),
        'shared_strings': len(offsets),
        'message_strings_in_file': len(messages),
        'message_file_bytes': pool_strings.MESSAGE_HEAD + len(message_body),
        'message_bytes_out_of_data_space': sum(message_bytes.values()),
        'conditions_of_unknown_truth': sorted(unknown),
        'note': ('Counts cover literals in the milestone units that the milestone configuration '
                 'compiles, before preprocessing. Literals produced by macro expansion in headers '
                 'are not seen. The linked saving is whatever native size(1) reports.'),
    })
    outputs[output_dir / 'TRACE-AUDIT.json'] = json.dumps(report, indent=2) + '\n'
    if not args.no_string_pool:
        # Without the pool the existing array and its layout are left alone.
        outputs[output_dir / 'STRING-POOL.json'] = json.dumps(pool_report, indent=2) + '\n'
        outputs[pool_path] = pool_strings.pool_source(
            data, pool_strings.message_stamp(message_body))
        outputs[message_path] = pool_strings.message_file(message_body)
    stale = []
    for target, text in outputs.items():
        content = text if isinstance(text, bytes) else text.encode()
        if target.exists() and target.read_bytes() == content:
            continue
        if args.check:
            stale.append(str(target.relative_to(root)))
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
    if stale:
        raise SystemExit('Stale generated sources: ' + ', '.join(stale))
    print(f'{len(report["files"])} C copies; {report["removed_calls"]} trace calls omitted; '
          f'{report["literal_payload_bytes"]} literal payload bytes; no argument side effects found')
    print(f'{len(offsets)} shared strings in {len(data)} bytes replace {shared} bytes of '
          f'private literals ({shared - len(data)} saved of {private} counted)')
    print(f'{len(messages)} messages in native/tmux.msg; {sum(message_bytes.values())} bytes of '
          f'message text leave the milestone data space')


if __name__ == '__main__':
    main()

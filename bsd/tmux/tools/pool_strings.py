#!/usr/bin/env python3
"""Share repeated string literals between the native tmux objects.

The 2.11BSD compiler emits every string literal separately, and string data
competes with the heap for the 56 KiB data space. This module does for the
generated native sources what xstr(1) does for preprocessed C: literals that
occur more than once in the native milestone build move to one array,

    char xstr[] = { ... };

and each use becomes (&xstr[N]). A string that is the tail of another shares
its bytes. Unlike xstr this works on unpreprocessed source, so it only
replaces a literal where the replacement is certain to mean the same thing:

  * not in a preprocessor directive, sizeof, or a char array initializer;
  * not joined to a macro whose value this module does not know;
  * only escapes the old compiler and this module read the same way.

Everything else keeps its private literal. Offsets of strings already in
native/xstr.c are kept, and new strings are appended, so a small source change
does not force the whole slow native build to recompile; --repack-strings
rebuilds the array from nothing.

Message text goes a step further, as mkstr(1) does for error("...") calls:
a literal that is only ever the text of an error leaves the data space
altogether. It is stored in native/tmux.msg, and its use becomes
(tmux_msg(N)), which reads it back from that file at byte offset N when the
error is reported. Only the argument positions listed in MESSAGE_CONTEXTS are
treated this way: each prints or copies its text before anything else can ask
for a message.

Used by generate_native_sources.py; not a command by itself.
"""
from __future__ import annotations
from dataclasses import dataclass, field
import re

POOL_NAME = 'xstr'
MESSAGE_CALL = 'tmux_msg'
# Callee and argument index of text that is used once, at once, as a message.
MESSAGE_CONTEXTS = {
    'fatal:0', 'fatalx:0', 'log_fatal:0', 'log_fatalx:0', 'log_warn:0',
    'log_warnx:0', 'error:1', 'server_client_msg_error:1',
    'server_client_identify_error:1',
}
MESSAGE_MIN = 8         # shorter text is not worth a file read
MESSAGE_MAX = 95        # the reader's buffer is 96 bytes
MESSAGE_MAGIC = b'tmuxmsg '
MESSAGE_HEAD = len(MESSAGE_MAGIC) + 8 + 1       # magic, stamp, newline
TOKEN = re.compile(
    r'(?P<comment>/\*.*?\*/|//[^\n]*)'
    r'|(?P<string>"(?:[^"\\\n]|\\.|\\\n)*")'
    r"|(?P<char>'(?:[^'\\\n]|\\.)*')"
    r'|(?P<word>[A-Za-z_]\w*)'
    r'|(?P<number>\.?\d(?:[\w.]|[eEpP][+-])*)'
    r'|(?P<space>[ \t\r\f\v]+)'
    r'|(?P<newline>\n)'
    r'|(?P<continued>\\\n)'
    r'|(?P<punct>.)', re.S)
SIMPLE_ESCAPES = {'n': 10, 't': 9, 'b': 8, 'r': 13, 'f': 12, '\\': 92,
                  "'": 39, '"': 34}
# A value may follow these keywords; any other word before a literal is
# sizeof or a macro that is joined to it.
VALUE_KEYWORDS = {'return'}


class Unsupported(ValueError):
    """A literal this module leaves alone."""


def decode(token):
    """Bytes of one string literal token, or Unsupported."""
    body = token[1:-1]
    out = bytearray()
    i = 0
    while i < len(body):
        c = body[i]
        if c != '\\':
            if ord(c) > 126 or ord(c) < 32:
                raise Unsupported('non-ASCII source character')
            out.append(ord(c))
            i += 1
            continue
        i += 1
        c = body[i]
        if c == '\n':
            i += 1
        elif c in SIMPLE_ESCAPES:
            out.append(SIMPLE_ESCAPES[c])
            i += 1
        elif c in '01234567':
            end = i + 1
            while end < min(i + 3, len(body)) and body[end] in '01234567':
                end += 1
            value = int(body[i:end], 8)
            if value > 255:
                raise Unsupported('octal escape out of range')
            out.append(value)
            i = end
        else:
            # \a, \v, \x.., \e and \? are not read alike by every compiler.
            raise Unsupported('escape \\' + c)
    if 0 in out:
        raise Unsupported('embedded NUL')
    return bytes(out)


class Conditions:
    """Three-valued evaluation of #if nesting for one configuration."""

    def __init__(self, defined, undefined):
        self.defined = dict(defined)
        self.undefined = set(undefined)
        self.stack = []          # [parent, taken, current]
        self.unknown = set()

    def copy_macros(self):
        return dict(self.defined), set(self.undefined)

    @property
    def active(self):
        return self.stack[-1][2] if self.stack else True

    @staticmethod
    def both(a, b):
        if a is False or b is False:
            return False
        if a is None or b is None:
            return None
        return True

    @staticmethod
    def either(a, b):
        if a is True or b is True:
            return True
        if a is None or b is None:
            return None
        return False

    @staticmethod
    def negate(a):
        return None if a is None else not a

    def is_defined(self, name):
        if name in self.defined:
            return True
        if name in self.undefined:
            return False
        self.unknown.add(name)
        return None

    def evaluate(self, text):
        tokens = re.findall(r'defined|[A-Za-z_]\w*|\d\w*|&&|\|\||[!()]|\S', text)
        pos = 0

        def peek():
            return tokens[pos] if pos < len(tokens) else None

        def take():
            nonlocal pos
            pos += 1
            return tokens[pos - 1]

        def primary():
            token = take() if peek() is not None else None
            if token == '(':
                value = disjunction()
                if peek() == ')':
                    take()
                return value
            if token == '!':
                return self.negate(primary())
            if token == 'defined':
                if peek() == '(':
                    take()
                    name = take()
                    if peek() == ')':
                        take()
                else:
                    name = take()
                return self.is_defined(name)
            if token is None:
                return None
            if token[0].isdigit():
                try:
                    return int(token.rstrip('uUlL'), 0) != 0
                except ValueError:
                    return None
            if re.fullmatch(r'[A-Za-z_]\w*', token):
                if token in self.undefined:
                    return False
                value = self.defined.get(token)
                if isinstance(value, int):
                    return value != 0
                if token not in self.defined:
                    self.unknown.add(token)
                return None
            return None

        def conjunction():
            value = primary()
            while peek() == '&&':
                take()
                value = self.both(value, primary())
            return value

        def disjunction():
            value = conjunction()
            while peek() == '||':
                take()
                value = self.either(value, conjunction())
            return value

        value = disjunction()
        if pos != len(tokens):
            return None        # comparison or arithmetic: not modelled
        return value

    def directive(self, text):
        """Apply one logical directive line (without its leading '#')."""
        match = re.match(r'\s*(\w+)\s*(.*)', text, re.S)
        if not match:
            return
        name, rest = match.groups()
        rest = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', rest, flags=re.S).strip()
        parent = self.active
        if name in ('if', 'ifdef', 'ifndef'):
            if name == 'if':
                value = self.evaluate(rest)
            else:
                value = self.is_defined(rest.split()[0]) if rest else None
                if name == 'ifndef':
                    value = self.negate(value)
            self.stack.append([parent, value, self.both(parent, value)])
        elif name in ('elif', 'else'):
            if not self.stack:
                return
            entry = self.stack[-1]
            value = True if name == 'else' else self.evaluate(rest)
            here = self.both(self.negate(entry[1]), value)
            entry[2] = self.both(entry[0], here)
            entry[1] = self.either(entry[1], value)
        elif name == 'endif':
            if self.stack:
                self.stack.pop()
        elif name == 'define' and parent is not False:
            macro = re.match(r'([A-Za-z_]\w*)(\(?)\s*(.*)', rest, re.S)
            if macro and parent is True:
                body = macro.group(3).strip()
                value = True
                if not macro.group(2) and re.fullmatch(r'\d+[uUlL]*', body):
                    value = int(body.rstrip('uUlL'))
                self.defined[macro.group(1)] = value
                self.undefined.discard(macro.group(1))
            elif macro:
                # Defined on a path of unknown truth: no longer known either way.
                self.undefined.discard(macro.group(1))
                self.defined.pop(macro.group(1), None)
        elif name == 'undef' and parent is True:
            macro = rest.split()[0] if rest else ''
            self.defined.pop(macro, None)
            self.undefined.add(macro)


@dataclass
class Run:
    """Adjacent literal pieces that the compiler joins into one string."""
    start: int
    end: int
    value: bytes | None        # None: left alone
    reason: str                # why it is left alone, or ''
    active: bool | None
    line: int
    context: str = ''          # 'init', or the called name and argument index


@dataclass
class Scan:
    runs: list = field(default_factory=list)
    unknown: set = field(default_factory=set)


def scan(text, conditions, macros):
    """Find every literal run in C source and say whether it can be shared.

    conditions is consumed: directives in text update it.
    macros maps the names of object-like macros to their single literal value.
    """
    tokens = []                # (kind, text, start, end, active)
    pos = 0
    line_start = True
    length = len(text)
    while pos < length:
        match = TOKEN.match(text, pos)
        kind = match.lastgroup
        value = match.group()
        if kind == 'punct' and value == '#' and line_start:
            # One logical directive line; comments may carry it across lines.
            end = pos + 1
            while end < length:
                if text.startswith('/*', end):
                    close = text.find('*/', end + 2)
                    end = length if close < 0 else close + 2
                elif text.startswith('\\\n', end):
                    end += 2
                elif text[end] == '\n':
                    break
                else:
                    end += 1
            conditions.directive(text[pos + 1:end].replace('\\\n', ' '))
            pos = end
            continue
        if kind == 'newline':
            line_start = True
        elif kind not in ('space', 'comment', 'continued'):
            line_start = False
            tokens.append((kind, value, pos, match.end(), conditions.active))
        pos = match.end()

    def piece(token):
        return token[0] == 'string' or (token[0] == 'word' and token[1] in macros)

    result = Scan()
    depth = []                 # open brackets: (char, callee, argument index)
    index = 0
    count = len(tokens)
    while index < count:
        token = tokens[index]
        kind, value = token[0], token[1]
        if not piece(token):
            if kind == 'punct':
                if value in '({[':
                    callee = ''
                    if value == '(' and index and tokens[index - 1][0] == 'word':
                        callee = tokens[index - 1][1]
                    depth.append([value, callee, 0])
                elif value in ')}]':
                    opener = {')': '(', '}': '{', ']': '['}[value]
                    while depth and depth[-1][0] != opener:
                        depth.pop()
                    if depth:
                        depth.pop()
                elif value == ',' and depth:
                    depth[-1][2] += 1
            index += 1
            continue
        last = index
        while last + 1 < count and piece(tokens[last + 1]):
            last += 1
        pieces = tokens[index:last + 1]
        before = tokens[index - 1] if index else None
        before2 = tokens[index - 2] if index > 1 else None
        after = tokens[last + 1] if last + 1 < count else None
        states = {item[4] for item in pieces}
        active = False if False in states else (None if None in states else True)
        reason = ''
        data = None
        if all(item[0] == 'word' for item in pieces) and len(pieces) > 1:
            reason = 'adjacent macros'
        elif after is not None and after[0] in ('word', 'number'):
            reason = 'joined to ' + after[1]
        elif before is not None and before[0] in ('word', 'number') \
                and before[1] not in VALUE_KEYWORDS:
            reason = 'follows ' + before[1]
        elif before is not None and before[1] == '(' and before2 is not None \
                and before2[1] == 'sizeof':
            reason = 'sizeof'
        elif before is not None and before[1] == '=' and before2 is not None \
                and before2[1] == ']':
            reason = 'possible char array initializer'
        else:
            try:
                data = b''.join(macros[item[1]] if item[0] == 'word'
                                else decode(item[1]) for item in pieces)
            except Unsupported as error:
                reason = str(error)
        calls = [entry for entry in depth if entry[0] == '(']
        if calls:
            context = '%s:%d' % (calls[-1][1] or '?', calls[-1][2])
        else:
            context = 'init'
        result.runs.append(Run(pieces[0][2], pieces[-1][3], data, reason, active,
                               text.count('\n', 0, pieces[0][2]) + 1, context))
        index = last + 1
    result.unknown = set(conditions.unknown)
    return result


def literal_macros(header_text):
    """Object-like macros defined exactly once, as exactly one string literal."""
    logical = header_text.replace('\\\n', ' ')
    seen = {}
    for match in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)(.*)$', logical, re.M):
        seen.setdefault(match.group(1), []).append(match.group(2))
    macros = {}
    for name, bodies in seen.items():
        if len(bodies) != 1 or bodies[0].startswith('('):
            continue
        body = re.sub(r'/\*.*?\*/|//[^\n]*', ' ', bodies[0], flags=re.S).strip()
        if re.fullmatch(r'"(?:[^"\\\n]|\\.)*"', body):
            try:
                macros[name] = decode(body)
            except Unsupported:
                pass
    return macros


def parse_pool(text):
    """Strings and offsets in an existing pool file, in file order."""
    match = re.search(r'char\s+' + POOL_NAME + r'\[\]\s*=\s*\{(.*?)\};', text, re.S)
    if not match:
        return []
    data = bytes(int(number) for number in re.findall(r'\d+', match.group(1)))
    layout = []
    offset = 0
    for chunk in data.split(b'\0')[:-1]:
        layout.append((offset, chunk))
        offset += len(chunk) + 1
    return layout


def plan_pool(uses, existing=(), always=()):
    """Choose shared strings and their offsets.

    uses maps each candidate string to its count in the configuration being
    optimized. A string is shared when it occurs twice, when it is the tail
    of another string that occurs at all, or when it is in always (strings
    the generated sources refer to by offset). Returns (offsets, data).
    """
    uses = dict(uses)
    for value in always:
        uses[value] = max(uses.get(value, 0), 2)
    present = sorted((value for value, number in uses.items() if number > 0 and value),
                     key=lambda value: value[::-1])
    wanted = {value for value, number in uses.items() if number > 1 and value}
    for index, value in enumerate(present[:-1]):
        host = present[index + 1]
        if host.endswith(value):
            wanted.add(value)
            wanted.add(host)
    # The empty string is the tail of whatever else is shared.
    if uses.get(b'', 0) > 0 and wanted:
        wanted.add(b'')
    offsets = {}
    data = bytearray()
    # Keep the place of every string that is still wanted; drop nothing from
    # the middle, so offsets used by unchanged sources stay valid.
    # The empty string is always a tail of something else (and the padding
    # byte of an odd-sized array reads back as one), so it never holds a place.
    last_needed = -1
    for position, (offset, value) in enumerate(existing):
        if value and value in wanted:
            last_needed = position
    for offset, value in list(existing)[:last_needed + 1]:
        if offset != len(data):
            raise ValueError('Existing pool layout is not contiguous')
        if value and value in wanted and value not in offsets:
            offsets[value] = offset
        data += value + b'\0'
    # A tail of a string already placed costs nothing.
    placed = sorted(offsets, key=len, reverse=True)

    def find_host(value):
        for host in placed:
            if len(host) > len(value) and host.endswith(value):
                return offsets[host] + len(host) - len(value)
        return None

    remaining = sorted(wanted - set(offsets), key=lambda value: (-len(value), value))
    for value in remaining:
        hosted = find_host(value)
        if hosted is None:
            offsets[value] = len(data)
            data += value + b'\0'
            placed.append(value)
            placed.sort(key=len, reverse=True)
        else:
            offsets[value] = hosted
    return offsets, bytes(data)


def pool_source(data, stamp=None):
    lines = ['/* Generated by tools/generate_native_sources.py; do not edit.\n'
             ' * String literals shared by the native objects; see\n'
             ' * tools/pool_strings.py. Each use is (&' + POOL_NAME + '[offset]).\n'
             ' */\n']
    if stamp is not None:
        lines.append('/* The message file this build reads must carry this stamp. */\n'
                     'char tmux_msg_stamp[] = "' + stamp.decode() + '";\n')
    lines += ['#ifdef TMUX_XSTR_READONLY\nconst\n#endif\n',
              'char ' + POOL_NAME + '[] = {\n']
    padded = data + (b'\0' if len(data) % 2 else b'')
    if not padded:
        padded = b'\0\0'
    for start in range(0, len(padded), 16):
        lines.append(','.join(str(byte) for byte in padded[start:start + 16]) + ',\n')
    lines.append('};\n')
    return ''.join(lines)


def is_message(run):
    """Whether a run is message text that can be kept in the message file."""
    return (run.value is not None and run.context in MESSAGE_CONTEXTS and
            MESSAGE_MIN <= len(run.value) <= MESSAGE_MAX)


def message_stamp(body):
    """Eight hex digits identifying one message file's contents."""
    import zlib
    return b'%08x' % (zlib.crc32(body) & 0xffffffff)


def message_file(body):
    return MESSAGE_MAGIC + message_stamp(body) + b'\n' + body


def parse_messages(data):
    """Message strings and their file offsets in an existing message file."""
    if not data.startswith(MESSAGE_MAGIC) or len(data) < MESSAGE_HEAD:
        return []
    layout = []
    offset = MESSAGE_HEAD
    for chunk in data[MESSAGE_HEAD:].split(b'\0')[:-1]:
        layout.append((offset, chunk))
        offset += len(chunk) + 1
    return layout


def plan_messages(wanted, existing=()):
    """Offsets for message strings; text already in the file keeps its place.

    Returns (offsets, body) where body is the file content after its header.
    Nothing is removed from the middle, so objects compiled against an older
    file of the same lineage still find their text; a repack starts afresh.
    """
    offsets = {}
    body = bytearray()
    last_needed = -1
    for position, (offset, value) in enumerate(existing):
        if value in wanted:
            last_needed = position
    for offset, value in list(existing)[:last_needed + 1]:
        if offset != MESSAGE_HEAD + len(body):
            raise ValueError('Existing message file layout is not contiguous')
        if value in wanted and value not in offsets:
            offsets[value] = offset
        body += value + b'\0'
    for value in sorted(set(wanted) - set(offsets)):
        offsets[value] = MESSAGE_HEAD + len(body)
        body += value + b'\0'
    return offsets, bytes(body)


def rewrite(text, runs, offsets, messages=None):
    """Replace message runs by file references and pooled runs by array ones.

    Returns the new text and the counts of pool and message replacements.
    """
    messages = messages or {}
    pieces = []
    cursor = 0
    replaced = 0
    relocated = 0
    for run in runs:
        if is_message(run) and run.value in messages:
            reference = '(%s(%d))' % (MESSAGE_CALL, messages[run.value])
            relocated += 1
        elif run.value is not None and run.value in offsets:
            reference = '(&%s[%d])' % (POOL_NAME, offsets[run.value])
            replaced += 1
        else:
            continue
        original = text[run.start:run.end]
        pieces.append(text[cursor:run.start])
        pieces.append(reference)
        pieces.append('\n' * original.count('\n'))
        cursor = run.end
    pieces.append(text[cursor:])
    return ''.join(pieces), replaced, relocated

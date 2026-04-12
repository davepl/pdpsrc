import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
outdir = Path(__file__).resolve().parent
outdir.mkdir(exist_ok=True)

files = [
    root / 'sample/nn11/FXMATH.MAC',
    root / 'sample/nn11/VECOP.MAC',
    root / 'sample/nn11/MATOP.MAC',
    root / 'sample/nn11/ACTFN.MAC',
    root / 'sample/nn11/LAYER.MAC',
    root / 'sample/model/FORWRD.MAC',
    root / 'sample/model/BKWRD.MAC',
    root / 'sample/model/UPDAT.MAC',
    root / 'sample/TRAIN.MAC',
]

DOT_NAME = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*\.[A-Za-z0-9_.]*)\b')
LABEL_DEF = re.compile(r'^(\s*)([A-Za-z_][A-Za-z0-9_]*):')
LOCAL_DEF = re.compile(r'^(\s*)(\d+)\$:')
LOCAL_REF = re.compile(r'(?<![A-Za-z0-9_])(\d+)\$')
ASCII_DIR = re.compile(r'^(\s*)([A-Za-z_][A-Za-z0-9_]*:)?\s*\.ascii\s+"([^"]*)"\s*$', re.I)
BYTE_DIR = re.compile(r'^(\s*)([A-Za-z_][A-Za-z0-9_]*:)?\s*\.byte\s+(.*?)\s*$', re.I)
WORD_DIR = re.compile(r'^(\s*)([A-Za-z_][A-Za-z0-9_]*:)?\s*\.word\s+(.*?)\s*$', re.I)
BLKW_DIR = re.compile(r'^(\s*)([A-Za-z_][A-Za-z0-9_]*:)?\s*\.blkw\s+(.*?)\s*$', re.I)
GLOBL_DIR = re.compile(r'^(\s*)\.globl\b', re.I)
TITLE_IDENT = re.compile(r'^\s*\.(title|ident|end)\b', re.I)
INCLUDE_ASECT = re.compile(r'^\s*\.(include|asect)\b', re.I)
ABS_ORIGIN = re.compile(r'^\s*\.\s*=\s*1000\s*$', re.I)
ASSIGN_DIR = re.compile(r'^\s*[A-Za-z_][A-Za-z0-9_.]*\s*=')

KNOWN_CONSTS = {
    'd_modl': '16.',
    'seq_ln': '8.',
    'vocab': '10.',
    'sqrtsh': '2.',
    'nstep': '350.',
    'rprt': '50.',
    'ntest': '10.',
    'dm2': '32.',
    'ss2': '16.',
    'vv2': '20.',
    'lr_emb': '4.',
    'lr_atn': '1.',
    'lr_out': '6.',
}


def dot_to_underscore(line):
    def repl(m):
        return m.group(1).replace('.', '_')
    return DOT_NAME.sub(repl, line)


def split_comment(line):
    if ';' not in line:
        return line.rstrip(), ''
    code, comment = line.split(';', 1)
    return code.rstrip(), '/ ' + comment.strip()


def norm_local(name):
    return name.replace('.', '_').replace('$', '_').lower()


def normalize_expr(expr):
    expr = expr.replace('<', '(').replace('>', ')')
    return expr


def substitute_known_consts(code):
    for name, value in KNOWN_CONSTS.items():
        code = re.sub(r'\b' + re.escape(name) + r'\b', value, code)
    return code


def fold_constant_expr(expr):
    probe = expr.replace(' ', '')
    for name, value in KNOWN_CONSTS.items():
        probe = re.sub(r'\b' + re.escape(name) + r'\b', value, probe)
    if not re.match(r'^[0-9.()+\-*/]+$', probe):
        return expr
    try:
        value = eval(probe.replace('.', ''), {'__builtins__': None}, {})
    except Exception:
        return expr
    if not isinstance(value, int):
        return expr
    return f'{value}.'


def split_args(expr):
    parts = []
    cur = []
    depth = 0
    for ch in expr:
        if ch == ',' and depth == 0:
            part = ''.join(cur).strip()
            if part:
                parts.append(part)
            cur = []
            continue
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            if depth > 0:
                depth -= 1
        cur.append(ch)
    part = ''.join(cur).strip()
    if part:
        parts.append(part)
    return parts


def emit_data(indent, label, directive, values, comment=''):
    out = []
    if comment:
        out.append((indent + comment).rstrip())
    for idx, value in enumerate(values):
        prefix = indent
        if idx == 0 and label:
            prefix += label + ' '
        elif idx > 0:
            prefix += ' ' * 8
        line = f'{prefix}{directive} {value}'
        out.append(line.rstrip())
    return out


def translate_lines(path, lines):
    out = []
    scope = path.stem.replace('.', '_').lower()
    local_map = {}
    in_macro = False

    for raw in lines:
        line = raw.rstrip('\n').rstrip('\r').replace('\u2028', '')
        if TITLE_IDENT.match(line) or INCLUDE_ASECT.match(line) or ABS_ORIGIN.match(line) or ASSIGN_DIR.match(line):
            continue
        if re.match(r'^\s*\.macro\b', line, re.I):
            in_macro = True
            continue
        if re.match(r'^\s*\.endm\b', line, re.I):
            in_macro = False
            continue
        if in_macro:
            continue
        if not line.strip():
            out.append('')
            continue

        code, comment = split_comment(line)
        code = dot_to_underscore(code).lower()

        m = LABEL_DEF.match(code)
        if m:
            label = m.group(2)
            if not re.match(r'^\d+$', label):
                scope = norm_local(label)

        m = LOCAL_DEF.match(code)
        if m:
            num = m.group(2)
            local_map[(scope, num)] = f'{scope}_l{num}'
            code = LOCAL_DEF.sub(r'\1' + local_map[(scope, num)] + ':', code)

        def repl_local(m):
            num = m.group(1)
            return local_map.get((scope, num), f'{scope}_l{num}')
        code = LOCAL_REF.sub(repl_local, code)

        code = code.replace('@#', '*$').replace('#', '$')
        code = GLOBL_DIR.sub(r'\1.globl', code)
        code = substitute_known_consts(code)

        m = ASCII_DIR.match(code)
        if m:
            indent, label, text = m.group(1), (m.group(2) or '').strip(), m.group(3)
            out.extend(emit_data(indent, label, '.byte', [f'{ord(ch)}.' for ch in text], comment))
            continue

        m = BYTE_DIR.match(code)
        if m:
            indent, label, expr = m.group(1), (m.group(2) or '').strip(), m.group(3).strip()
            out.extend(emit_data(indent, label, '.byte', split_args(normalize_expr(expr)), comment))
            continue

        m = WORD_DIR.match(code)
        if m:
            indent, label, expr = m.group(1), (m.group(2) or '').strip(), m.group(3).strip()
            out.extend(emit_data(indent, label, '', split_args(normalize_expr(expr)), comment))
            continue

        m = BLKW_DIR.match(code)
        if m:
            indent, label, expr = m.group(1), (m.group(2) or '').strip(), normalize_expr(m.group(3).strip())
            expr = fold_constant_expr(expr)
            if label:
                out.append((indent + label).rstrip())
            if re.match(r'^[0-9.]+$', expr):
                units = int(expr[:-1]) if expr.endswith('.') else int(expr)
                s = indent + f'.=.+{2 * units}.'
            else:
                s = indent + f'.=.+2*{expr}'
            if comment:
                out.append((indent + comment).rstrip())
            out.append(s.rstrip())
            continue

        stripped = code.strip()
        if stripped.startswith(';'):
            out.append('/ ' + stripped[1:].strip())
            continue
        if not code.strip() and comment:
            out.append(comment)
            continue
        if comment:
            out.append(comment)
        out.append(code.rstrip())

    return out


sections = [
    '.globl _main',
    '.globl _write',
    '.globl _exit',
    '',
    '/ BSD user-mode entry point for the ATTN/11 training sample.',
    '/ This keeps the model/math code in PDP-11 assembly but runs as a',
    '/ normal 2.11BSD process instead of a fixed-address bare-metal image.',
    '',
    '_main:',
    '\tjmp\tmain',
    '',
]

for path in files:
    translated = translate_lines(path, path.read_text(errors='ignore').splitlines())
    if path.name == 'TRAIN.MAC':
        filtered = []
        skip_stack = False
        for line in translated:
            if '.include' in line:
                continue
            if line.strip() == 'jmp main':
                continue
            if re.search(r'\bmov\s+\$stack,\s*sp\b', line, re.I):
                m = re.match(r'^\s*([A-Za-z_][A-Za-z0-9_]*:)', line)
                if m:
                    filtered.append(m.group(1))
                filtered.append('\t/ use the BSD process stack; do not reset sp into static data')
                continue
            if re.match(r'^\s*halt\b', line, re.I):
                filtered.append('\tclr\t-(sp)')
                filtered.append('\tjsr\tpc,_exit')
                continue
            if re.search(r'^stack:', line):
                skip_stack = True
                continue
            if skip_stack:
                if '.=.+'.lower() in line:
                    skip_stack = False
                    continue
                skip_stack = False
            filtered.append(line)
        translated = filtered
    sections.append(f'/ ===== {path.relative_to(root)} =====')
    sections.extend(translated)
    sections.append('')

io_block = '''
/ ===== attn/io_bsd replacement =====

putc:
\tmovb\tr0, putc_ch
\tmov\t$1,-(sp)
\tmov\t$putc_ch,-(sp)
\tmov\t$1,-(sp)
\tjsr\tpc,_write
\tadd\t$6,sp
\trts\tpc

\t.globl\tputs
puts:
\tmov\tr1,-(sp)
\tmov\tr0,r1
puts_l1:
\ttstb\t(r1)+
\tbne\tputs_l1
\tsub\tr0,r1
\tdec\tr1
\tmov\tr1,-(sp)
\tmov\tr0,-(sp)
\tmov\t$1,-(sp)
\tjsr\tpc,_write
\tadd\t$6,sp
\tmov\t(sp)+,r1
\trts\tpc

\t.globl\tnewln
newln:
\tmov\t$1,-(sp)
\tmov\t$newline_str,-(sp)
\tmov\t$1,-(sp)
\tjsr\tpc,_write
\tadd\t$6,sp
\trts\tpc

\t.globl\tputoct
putoct:
\tmov\tr0,r2
\tmov\t$6.,r1
\tash\t$-15.,r0
\tbr\tputoct_l3
putoct_l3:
\tmov\tr2,r0
\tmov\t$6.,r1
putoct_l2:
\tmov\tr0,-(sp)
\tbic\t$177770,(sp)
\tadd\t$'0,(sp)
\tash\t$-3.,r0
\tsob\tr1,putoct_l2
\tmov\t$6.,r1
putoct_l4:
\tmov\t(sp)+,r0
\tjsr\tpc,putc
\tsob\tr1,putoct_l4
\trts\tpc

\t.globl\tputdec
putdec:
\ttst\tr0
\tbpl\tputdec_l1
\tmov\tr0,-(sp)
\tmov\t$'-,r0
\tjsr\tpc,putc
\tmov\t(sp)+,r0
\tneg\tr0
putdec_l1:
\tclr\tr3
putdec_l2:
\tclr\tr1
\tmov\tr0,r1
\tclr\tr0
\tdiv\t$10.,r0
\tadd\t$'0,r1
\tmov\tr1,-(sp)
\tinc\tr3
\ttst\tr0
\tbne\tputdec_l2
putdec_l3:
\tmov\t(sp)+,r0
\tjsr\tpc,putc
\tsob\tr3,putdec_l3
\trts\tpc

\t.globl\tputq8
putq8:
\tmov\tr0,r2
\ttst\tr0
\tbpl\tputq8_l1
\tmov\t$'-,r0
\tjsr\tpc,putc
\tmov\tr2,r0
\tneg\tr0
\tmov\tr0,r2
putq8_l1:
\tmov\tr2,r0
\tash\t$-8.,r0
\tjsr\tpc,putdec
\tmov\t$'.,r0
\tjsr\tpc,putc
\tmov\tr2,r0
\tbic\t$177400,r0
\tmul\t$1000.,r0
\tashc\t$-8.,r0
\tmov\tr1,r2
\tmov\tr2,r1
\tclr\tr0
\tdiv\t$100.,r0
\tmov\tr1,r2
\tadd\t$'0,r0
\tjsr\tpc,putc
\tmov\tr2,r1
\tclr\tr0
\tdiv\t$10.,r0
\tmov\tr1,r2
\tadd\t$'0,r0
\tjsr\tpc,putc
\tmov\tr2,r0
\tadd\t$'0,r0
\tjsr\tpc,putc
\trts\tpc

\t.globl\tputspc
putspc:
\tmov\t$40,r0
\tjsr\tpc,putc
\trts\tpc

\t.globl\tputvec
putvec:
\tmov\tr4,-(sp)
\tmov\tr5,-(sp)
\tmov\tr0,r4
\tmov\tr1,r5
\tmov\t$'[,r0
\tjsr\tpc,putc
putvec_l1:
\tmov\t(r4)+,r0
\tjsr\tpc,putq8
\tdec\tr5
\tbeq\tputvec_l2
\tmov\t$54,r0
\tjsr\tpc,putc
\tmov\t$40,r0
\tjsr\tpc,putc
\tbr\tputvec_l1
putvec_l2:
\tmov\t$'],r0
\tjsr\tpc,putc
\tmov\t(sp)+,r5
\tmov\t(sp)+,r4
\trts\tpc

\t.data
newline_str:\t.byte 10.
putc_ch:\t.byte 0.
\t.even
\t.text
'''.strip('\n').splitlines()

final = []
inserted_io = False
for line in sections:
    if line.startswith('/ ===== sample/model/FORWRD.MAC =====') and not inserted_io:
        final.extend(io_block)
        final.append('')
        inserted_io = True
    final.append(line)

out_text = '\n'.join(final) + '\n'
(outdir / 'attn.s').write_text(out_text)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "object.h"
#include "obj2bsd.h"

#define MAX_PSECTS 64
#define MAX_GLOBALS 512
#define REC_MAX 2048

#define SEG_TEXT 1
#define SEG_DATA 2

#define A_MAGIC1 0407

#define N_UNDF 0x00
#define N_ABS 0x01
#define N_TEXT 0x02
#define N_DATA 0x03
#define N_BSS 0x04
#define N_EXT  0x20

#define RELFLG 000001
#define RABS   000000
#define RTEXT  000002
#define RDATA  000004
#define RBSS   000006
#define REXT   000010

typedef struct psect {
    char            name[32];
    unsigned        flags;
    unsigned        length;
    unsigned        seg_offset;
    unsigned        init_top;
    int             number;
    int             segment;
    int             assigned;
} PSECT;

typedef struct global_sym {
    char            name[8];
    unsigned        value;
    unsigned        flags;
    PSECT          *psect;
    int             defined;
    int             sym_index;
} GLOBAL_SYM;

typedef struct reloc_expr {
    int             kind;
#define EXPR_CONST 0
#define EXPR_PSECT 1
#define EXPR_GLOBAL 2
#define EXPR_INVALID 3
    long            addend;
    PSECT          *psect;
    GLOBAL_SYM     *global;
} RELOC_EXPR;

static PSECT     psects[MAX_PSECTS];
static int       psect_count = 0;
static GLOBAL_SYM globals[MAX_GLOBALS];
static int       global_count = 0;
static char      module_prefix[4];

static unsigned  text_size = 0;
static unsigned  data_size = 0;
static unsigned char *text_bytes = NULL;
static unsigned char *data_bytes = NULL;
static unsigned short *text_reloc = NULL;
static unsigned short *data_reloc = NULL;

static PSECT    *current_psect = NULL;
static unsigned  textaddr = 0;

static FILE     *active_fp = NULL;
static FILE     *out_fp = NULL;
static jmp_buf   convert_fail;

static unsigned word_at(
    unsigned char *cp)
{
    return ((unsigned) cp[1] << 8) | cp[0];
}

static void cleanup_state(
    void)
{
    if (active_fp != NULL) {
        fclose(active_fp);
        active_fp = NULL;
    }
    if (out_fp != NULL) {
        fclose(out_fp);
        out_fp = NULL;
    }
    if (text_bytes != NULL) {
        free(text_bytes);
        text_bytes = NULL;
    }
    if (data_bytes != NULL) {
        free(data_bytes);
        data_bytes = NULL;
    }
    if (text_reloc != NULL) {
        free(text_reloc);
        text_reloc = NULL;
    }
    if (data_reloc != NULL) {
        free(data_reloc);
        data_reloc = NULL;
    }
}

static void fail(
    char *msg,
    char *arg)
{
    if (arg != NULL)
        fprintf(stderr, msg, arg);
    else
        fprintf(stderr, "%s", msg);
    cleanup_state();
    longjmp(convert_fail, 1);
}

static void unrad50_word(
    unsigned value,
    char *out)
{
    static char     table[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

    out[0] = table[(value / 1600) % 40];
    out[1] = table[(value / 40) % 40];
    out[2] = table[value % 40];
}

static void decode_rad50_name(
    unsigned char *cp,
    char *out)
{
    unrad50_word(word_at(cp), out);
    unrad50_word(word_at(cp + 2), out + 3);
    out[6] = 0;
}

static void trim_name(
    char *dst,
    char *src)
{
    int             len;

    strcpy(dst, src);
    len = strlen(dst);
    while (len > 0 && dst[len - 1] == ' ') {
        dst[len - 1] = 0;
        len--;
    }
}

static void make_psect_name(
    char *out,
    char *module,
    char *sym)
{
    sprintf(out, "%s:%-6.6s", module, sym);
}

static void reset_state(
    void)
{
    memset(psects, 0, sizeof(psects));
    memset(globals, 0, sizeof(globals));
    psect_count = 0;
    global_count = 0;
    text_size = 0;
    data_size = 0;
    current_psect = NULL;
    textaddr = 0;
    strcpy(module_prefix, "01");
}

static PSECT *find_psect(
    char *name)
{
    int             i;

    for (i = 0; i < psect_count; i++)
        if (strcmp(psects[i].name, name) == 0)
            return &psects[i];
    return NULL;
}

static PSECT *find_psect_number(
    int number)
{
    int             i;

    for (i = 0; i < psect_count; i++)
        if (psects[i].number == number)
            return &psects[i];
    return NULL;
}

static PSECT *add_psect(
    char *name)
{
    PSECT          *ps;

    ps = find_psect(name);
    if (ps != NULL)
        return ps;
    if (psect_count >= MAX_PSECTS)
        fail("Too many PSECTs\n", NULL);

    ps = &psects[psect_count];
    memset(ps, 0, sizeof(*ps));
    strcpy(ps->name, name);
    ps->number = psect_count + 1;
    psect_count++;
    return ps;
}

static GLOBAL_SYM *find_global(
    char *name)
{
    int             i;

    for (i = 0; i < global_count; i++)
        if (strcmp(globals[i].name, name) == 0)
            return &globals[i];
    return NULL;
}

static GLOBAL_SYM *add_global(
    char *name)
{
    GLOBAL_SYM     *sym;

    sym = find_global(name);
    if (sym != NULL)
        return sym;
    if (global_count >= MAX_GLOBALS)
        fail("Too many global symbols\n", NULL);

    sym = &globals[global_count];
    memset(sym, 0, sizeof(*sym));
    strcpy(sym->name, name);
    sym->sym_index = -1;
    global_count++;
    return sym;
}

static int read_rt11_record(
    FILE *fp,
    unsigned char *rec,
    int *reclen)
{
    int             c;
    int             i;
    int             len;
    unsigned char   hdr[4];
    unsigned        sum;
    unsigned char   got;

    do {
        c = fgetc(fp);
        if (c == EOF)
            return 0;
    } while (c == 0);

    hdr[0] = (unsigned char) c;
    hdr[1] = (unsigned char) fgetc(fp);
    hdr[2] = (unsigned char) fgetc(fp);
    hdr[3] = (unsigned char) fgetc(fp);

    if (hdr[0] != FBR_LEAD1 || hdr[1] != FBR_LEAD2)
        fail("Bad RT-11 object record\n", NULL);

    len = ((int) hdr[3] << 8) | hdr[2];
    if (len < 4 || len - 4 > REC_MAX)
        fail("Oversize RT-11 object record\n", NULL);

    for (i = 0; i < len - 4; i++) {
        c = fgetc(fp);
        if (c == EOF)
            fail("Unexpected EOF in object file\n", NULL);
        rec[i] = (unsigned char) c;
    }

    c = fgetc(fp);
    if (c == EOF)
        fail("Missing RT-11 checksum\n", NULL);
    got = (unsigned char) c;

    sum = hdr[0] + hdr[1] + hdr[2] + hdr[3];
    for (i = 0; i < len - 4; i++)
        sum += rec[i];
    sum += got;
    if ((sum & 0xff) != 0)
        fprintf(stderr, "Warning: bad RT-11 checksum\n");

    *reclen = len - 4;
    return 1;
}

static int psect_segment_type(
    PSECT *ps)
{
    if (ps->segment == SEG_DATA)
        return RDATA;
    return RTEXT;
}

static unsigned char *segment_bytes(
    int segment)
{
    if (segment == SEG_DATA)
        return data_bytes;
    return text_bytes;
}

static unsigned short *segment_reloc(
    int segment)
{
    if (segment == SEG_DATA)
        return data_reloc;
    return text_reloc;
}

static unsigned segment_size(
    int segment)
{
    if (segment == SEG_DATA)
        return data_size;
    return text_size;
}

static void ensure_segment_capacity(
    int segment,
    unsigned need)
{
    unsigned char  **bytesp;
    unsigned short **relocp;
    unsigned        *sizep;
    unsigned         oldsize;
    unsigned         newsize;
    unsigned char   *newbytes;
    unsigned short  *newreloc;
    unsigned         oldwords;
    unsigned         newwords;

    if (segment == SEG_DATA) {
        bytesp = &data_bytes;
        relocp = &data_reloc;
        sizep = &data_size;
    } else {
        bytesp = &text_bytes;
        relocp = &text_reloc;
        sizep = &text_size;
    }

    oldsize = *sizep;
    if (need <= oldsize)
        return;

    newsize = oldsize;
    if (newsize == 0)
        newsize = 64;
    while (newsize < need)
        newsize <<= 1;

    if (*bytesp == NULL)
        newbytes = malloc(newsize);
    else
        newbytes = realloc(*bytesp, newsize);
    if (newbytes == NULL)
        fail("Out of memory\n", NULL);
    memset(newbytes + oldsize, 0, newsize - oldsize);
    *bytesp = newbytes;

    oldwords = (oldsize + 1) >> 1;
    newwords = (newsize + 1) >> 1;
    if (*relocp == NULL)
        newreloc = malloc(newwords * sizeof(unsigned short));
    else
        newreloc = realloc(*relocp, newwords * sizeof(unsigned short));
    if (newreloc == NULL)
        fail("Out of memory\n", NULL);
    memset(newreloc + oldwords, 0, (newwords - oldwords) * sizeof(unsigned short));
    *relocp = newreloc;

    *sizep = newsize;
}

static void store_segment_word(
    int segment,
    unsigned addr,
    unsigned value)
{
    unsigned char  *bytes;
    unsigned        limit;

    bytes = segment_bytes(segment);
    limit = segment_size(segment);
    if (addr + 1 >= limit)
        fail("Relocation address out of range\n", NULL);
    bytes[addr] = value & 0xff;
    bytes[addr + 1] = (value >> 8) & 0xff;
}

static void store_segment_reloc(
    int segment,
    unsigned addr,
    unsigned value)
{
    unsigned short *reloc;
    unsigned        limit;

    if (addr & 1)
        fail("Odd relocation address in BSD object output\n", NULL);

    reloc = segment_reloc(segment);
    limit = (segment_size(segment) + 1) >> 1;
    if ((addr >> 1) >= limit)
        fail("Relocation table overflow\n", NULL);
    reloc[addr >> 1] = value & 0xffff;
}

static void parse_gsd(
    unsigned char *rec,
    int reclen)
{
    int             i;
    char            sym[7];
    char            psect_name[32];
    char            global_name[8];
    unsigned        flg;
    unsigned        ent;
    unsigned        val;
    PSECT          *ps;
    GLOBAL_SYM     *gsym;
    unsigned       *segsize;

    for (i = 2; i + 7 < reclen; i += 8) {
        decode_rad50_name(rec + i, sym);
        flg = rec[i + 4];
        ent = rec[i + 5];
        val = word_at(rec + i + 6);

        if (ent == GSD_PSECT) {
            make_psect_name(psect_name, module_prefix, sym);
            ps = add_psect(psect_name);
            ps->flags = flg;
            ps->length = val;
            ps->segment = (flg & PSECT_DATA) ? SEG_DATA : SEG_TEXT;
            if (!ps->assigned) {
                segsize = ps->segment == SEG_DATA ? &data_size : &text_size;
                *segsize = (*segsize + 1) & ~1;
                ps->seg_offset = *segsize;
                *segsize += val;
                ps->assigned = 1;
            }
            current_psect = ps;
        } else if (ent == GSD_GLOBAL) {
            trim_name(global_name, sym);
            gsym = add_global(global_name);
            gsym->flags = flg;
            if ((flg & GLOBAL_DEF) && current_psect != NULL) {
                gsym->defined = 1;
                gsym->psect = current_psect;
                gsym->value = val;
            }
        }
    }
}

static void parse_text(
    unsigned char *rec,
    int reclen)
{
    unsigned        off;
    int             len;
    int             i;
    unsigned char  *bytes;
    unsigned        adr;

    if (current_psect == NULL)
        fail("TEXT record without active PSECT\n", NULL);

    off = word_at(rec + 2);
    len = reclen - 4;
    adr = current_psect->seg_offset + off;
    ensure_segment_capacity(current_psect->segment, adr + len);

    bytes = segment_bytes(current_psect->segment);
    for (i = 0; i < len; i++)
        bytes[adr + i] = rec[4 + i];

    if (off + len > current_psect->init_top)
        current_psect->init_top = off + len;

    textaddr = adr;
}

static void expr_const(
    RELOC_EXPR *expr,
    long value)
{
    expr->kind = EXPR_CONST;
    expr->addend = value;
    expr->psect = NULL;
    expr->global = NULL;
}

static void expr_psect(
    RELOC_EXPR *expr,
    PSECT *ps,
    long value)
{
    expr->kind = EXPR_PSECT;
    expr->addend = value;
    expr->psect = ps;
    expr->global = NULL;
}

static void expr_global(
    RELOC_EXPR *expr,
    GLOBAL_SYM *global,
    long value)
{
    expr->kind = EXPR_GLOBAL;
    expr->addend = value;
    expr->psect = NULL;
    expr->global = global;
}

static void expr_invalid(
    RELOC_EXPR *expr)
{
    expr->kind = EXPR_INVALID;
    expr->addend = 0;
    expr->psect = NULL;
    expr->global = NULL;
}

static void expr_add(
    RELOC_EXPR *left,
    RELOC_EXPR *right)
{
    if (left->kind == EXPR_CONST && right->kind == EXPR_CONST) {
        left->addend += right->addend;
        return;
    }
    if ((left->kind == EXPR_PSECT || left->kind == EXPR_GLOBAL) && right->kind == EXPR_CONST) {
        left->addend += right->addend;
        return;
    }
    if (left->kind == EXPR_CONST && (right->kind == EXPR_PSECT || right->kind == EXPR_GLOBAL)) {
        right->addend += left->addend;
        *left = *right;
        return;
    }
    expr_invalid(left);
}

static void expr_sub(
    RELOC_EXPR *left,
    RELOC_EXPR *right)
{
    if (left->kind == EXPR_CONST && right->kind == EXPR_CONST) {
        left->addend -= right->addend;
        return;
    }
    if ((left->kind == EXPR_PSECT || left->kind == EXPR_GLOBAL) && right->kind == EXPR_CONST) {
        left->addend -= right->addend;
        return;
    }
    if (left->kind == EXPR_PSECT && right->kind == EXPR_PSECT && left->psect == right->psect) {
        expr_const(left, left->addend - right->addend);
        return;
    }
    if (left->kind == EXPR_GLOBAL && right->kind == EXPR_GLOBAL && left->global == right->global) {
        expr_const(left, left->addend - right->addend);
        return;
    }
    expr_invalid(left);
}

static RELOC_EXPR eval_complex_expr(
    unsigned char *rec,
    int *idx)
{
    RELOC_EXPR      stack[32];
    int             sp;
    unsigned        op;
    unsigned        con;
    char            name[7];
    char            trim[8];
    PSECT          *ps;
    GLOBAL_SYM     *gsym;

    sp = 0;

    for (;;) {
        op = rec[*idx];
        (*idx)++;

        switch (op) {
        case CPLX_NOP:
            break;
        case CPLX_ADD:
            expr_add(&stack[sp - 2], &stack[sp - 1]);
            sp--;
            break;
        case CPLX_SUB:
            expr_sub(&stack[sp - 2], &stack[sp - 1]);
            sp--;
            break;
        case CPLX_MUL:
            if (stack[sp - 2].kind == EXPR_CONST && stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 2].addend *= stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 2]);
            sp--;
            break;
        case CPLX_DIV:
            if (stack[sp - 2].kind == EXPR_CONST && stack[sp - 1].kind == EXPR_CONST && stack[sp - 1].addend != 0)
                stack[sp - 2].addend /= stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 2]);
            sp--;
            break;
        case CPLX_AND:
            if (stack[sp - 2].kind == EXPR_CONST && stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 2].addend &= stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 2]);
            sp--;
            break;
        case CPLX_OR:
            if (stack[sp - 2].kind == EXPR_CONST && stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 2].addend |= stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 2]);
            sp--;
            break;
        case CPLX_XOR:
            if (stack[sp - 2].kind == EXPR_CONST && stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 2].addend ^= stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 2]);
            sp--;
            break;
        case CPLX_NEG:
            if (stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 1].addend = -stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 1]);
            break;
        case CPLX_COM:
            if (stack[sp - 1].kind == EXPR_CONST)
                stack[sp - 1].addend = ~stack[sp - 1].addend;
            else
                expr_invalid(&stack[sp - 1]);
            break;
        case CPLX_STORE:
        case CPLX_STORE_DISP:
            return stack[sp - 1];
        case CPLX_GLOBAL:
            decode_rad50_name(rec + *idx, name);
            *idx += 4;
            trim_name(trim, name);
            gsym = add_global(trim);
            expr_global(&stack[sp], gsym, 0);
            sp++;
            break;
        case CPLX_REL:
            ps = find_psect_number(rec[*idx]);
            con = word_at(rec + *idx + 1);
            *idx += 3;
            if (ps == NULL)
                fail("Unknown complex relocation PSECT\n", NULL);
            expr_psect(&stack[sp], ps, con);
            sp++;
            break;
        case CPLX_CONST:
            con = word_at(rec + *idx);
            *idx += 2;
            expr_const(&stack[sp], con);
            sp++;
            break;
        default:
            fail("Unknown complex relocation opcode\n", NULL);
        }
    }
}

static void apply_expr(
    int segment,
    unsigned addr,
    RELOC_EXPR *expr,
    int pcrel)
{
    unsigned        reloc;
    unsigned        value;

    reloc = RABS;
    value = expr->addend & 0xffff;

    if (expr->kind == EXPR_PSECT) {
        value = (expr->psect->seg_offset + expr->addend) & 0xffff;
        reloc = psect_segment_type(expr->psect);
    } else if (expr->kind == EXPR_GLOBAL) {
        if (expr->global->sym_index < 0)
            fail("External symbol index was not assigned\n", NULL);
        reloc = REXT | ((expr->global->sym_index & 0x0fff) << 4);
        value = expr->addend & 0xffff;
    } else if (expr->kind == EXPR_INVALID) {
        fail("Unsupported complex relocation for BSD object\n", NULL);
    }

    if (pcrel)
        reloc |= RELFLG;

    store_segment_word(segment, addr, value);
    store_segment_reloc(segment, addr, reloc);
}

static void parse_rld(
    unsigned char *rec,
    int reclen)
{
    int             i;
    unsigned        ent;
    unsigned        dis;
    unsigned        con;
    unsigned        adr;
    char            name[7];
    char            trim[8];
    char            psect_name[32];
    PSECT          *ps;
    GLOBAL_SYM     *gsym;
    RELOC_EXPR      expr;

    i = 2;
    while (i < reclen) {
        ent = rec[i] & 0x7f;
        dis = rec[i + 1];
        if (current_psect == NULL && ent != RLD_LOCDEF && ent != RLD_LOCMOD)
            fail("RLD entry without active PSECT\n", NULL);

        switch (ent) {
        case RLD_INT:
            con = word_at(rec + i + 2);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, current_psect, con);
            apply_expr(current_psect->segment, adr, &expr, 0);
            i += 4;
            break;
        case RLD_INT_DISP:
            con = word_at(rec + i + 2);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, current_psect, con);
            apply_expr(current_psect->segment, adr, &expr, 1);
            i += 4;
            break;
        case RLD_GLOBAL:
            decode_rad50_name(rec + i + 2, name);
            trim_name(trim, name);
            adr = (textaddr + dis - 4) & 0xffff;
            gsym = add_global(trim);
            expr_global(&expr, gsym, 0);
            apply_expr(current_psect->segment, adr, &expr, 0);
            i += 6;
            break;
        case RLD_GLOBAL_DISP:
            decode_rad50_name(rec + i + 2, name);
            trim_name(trim, name);
            adr = (textaddr + dis - 4) & 0xffff;
            gsym = add_global(trim);
            expr_global(&expr, gsym, 0);
            apply_expr(current_psect->segment, adr, &expr, 1);
            i += 6;
            break;
        case RLD_GLOBAL_OFFSET:
            decode_rad50_name(rec + i + 2, name);
            trim_name(trim, name);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            gsym = add_global(trim);
            expr_global(&expr, gsym, con);
            apply_expr(current_psect->segment, adr, &expr, 0);
            i += 8;
            break;
        case RLD_GLOBAL_OFFSET_DISP:
            decode_rad50_name(rec + i + 2, name);
            trim_name(trim, name);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            gsym = add_global(trim);
            expr_global(&expr, gsym, con);
            apply_expr(current_psect->segment, adr, &expr, 1);
            i += 8;
            break;
        case RLD_LOCDEF:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            current_psect = find_psect(psect_name);
            if (current_psect == NULL)
                fail("Unknown PSECT in location definition\n", NULL);
            textaddr = current_psect->seg_offset + word_at(rec + i + 6);
            i += 8;
            break;
        case RLD_LOCMOD:
            if (current_psect == NULL)
                fail("Location modification without active PSECT\n", NULL);
            textaddr = current_psect->seg_offset + word_at(rec + i + 2);
            i += 4;
            break;
        case RLD_LIMITS:
            adr = (textaddr + dis - 4) & 0xffff;
            store_segment_word(current_psect->segment, adr, 0);
            store_segment_reloc(current_psect->segment, adr, RABS);
            store_segment_word(current_psect->segment, adr + 2, 0);
            store_segment_reloc(current_psect->segment, adr + 2, RABS);
            i += 2;
            break;
        case RLD_PSECT:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                fail("Unknown PSECT relocation\n", NULL);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, ps, 0);
            apply_expr(current_psect->segment, adr, &expr, 0);
            i += 6;
            break;
        case RLD_PSECT_DISP:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                fail("Unknown PSECT displaced relocation\n", NULL);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, ps, 0);
            apply_expr(current_psect->segment, adr, &expr, 1);
            i += 6;
            break;
        case RLD_PSECT_OFFSET:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                fail("Unknown PSECT additive relocation\n", NULL);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, ps, con);
            apply_expr(current_psect->segment, adr, &expr, 0);
            i += 8;
            break;
        case RLD_PSECT_OFFSET_DISP:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                fail("Unknown PSECT additive displaced relocation\n", NULL);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            expr_psect(&expr, ps, con);
            apply_expr(current_psect->segment, adr, &expr, 1);
            i += 8;
            break;
        case RLD_COMPLEX:
            i += 2;
            adr = (textaddr + dis - 4) & 0xffff;
            expr = eval_complex_expr(rec, &i);
            apply_expr(current_psect->segment, adr, &expr, 0);
            break;
        default:
            fail("Unsupported relocation type in BSD converter\n", NULL);
        }
    }
}

static void pass_object(
    char *filename,
    int pass)
{
    unsigned char   rec[REC_MAX];
    int             reclen;

    active_fp = fopen(filename, "rb");
    if (active_fp == NULL)
        fail("Can't open input file '%s'\n", filename);

    while (read_rt11_record(active_fp, rec, &reclen)) {
        switch (rec[0]) {
        case OBJ_GSD:
            if (pass == 1)
                parse_gsd(rec, reclen);
            break;
        case OBJ_TEXT:
            if (pass == 2)
                parse_text(rec, reclen);
            break;
        case OBJ_RLD:
            if (pass == 2)
                parse_rld(rec, reclen);
            break;
        default:
            break;
        }
    }

    fclose(active_fp);
    active_fp = NULL;
}

static void assign_symbol_indices(
    void)
{
    int             i;

    for (i = 0; i < global_count; i++)
        globals[i].sym_index = i;
}

static void put_u16(
    FILE *fp,
    unsigned value)
{
    fputc(value & 0xff, fp);
    fputc((value >> 8) & 0xff, fp);
}

static void put_pdp_long(
    FILE *fp,
    unsigned long value)
{
    put_u16(fp, value & 0xffff);
    put_u16(fp, (value >> 16) & 0xffff);
}

static void write_exec_header(
    FILE *fp)
{
    unsigned        trelsz;
    unsigned        drelsz;
    unsigned        symsz;

    trelsz = ((text_size + 1) >> 1) << 1;
    drelsz = ((data_size + 1) >> 1) << 1;
    symsz = global_count * 8;

    put_u16(fp, A_MAGIC1);
    put_u16(fp, text_size);
    put_u16(fp, data_size);
    put_u16(fp, 0);
    put_u16(fp, symsz);
    put_u16(fp, 0);
    put_u16(fp, trelsz);
    put_u16(fp, drelsz);
}

static void write_bytes(
    FILE *fp,
    unsigned char *bytes,
    unsigned size)
{
    if (size != 0 && fwrite(bytes, 1, size, fp) != size)
        fail("Short write to BSD object\n", NULL);
}

static void write_relocs(
    FILE *fp,
    unsigned short *reloc,
    unsigned size)
{
    unsigned        i;
    unsigned        count;

    count = (size + 1) >> 1;
    for (i = 0; i < count; i++)
        put_u16(fp, reloc[i]);
}

static unsigned symbol_type(
    GLOBAL_SYM *sym)
{
    if (!sym->defined)
        return N_UNDF | N_EXT;
    if (sym->psect == NULL)
        return N_ABS | N_EXT;
    if (sym->psect->segment == SEG_DATA)
        return N_DATA | N_EXT;
    return N_TEXT | N_EXT;
}

static unsigned symbol_value(
    GLOBAL_SYM *sym)
{
    if (!sym->defined)
        return sym->value;
    if (sym->psect == NULL)
        return sym->value;
    return (sym->psect->seg_offset + sym->value) & 0xffff;
}

static void write_symbols(
    FILE *fp)
{
    int             i;
    unsigned long   stroff;
    unsigned long   strsize;
    GLOBAL_SYM     *sym;

    stroff = 4;
    for (i = 0; i < global_count; i++) {
        sym = &globals[i];
        put_pdp_long(fp, stroff);
        fputc(symbol_type(sym), fp);
        fputc(0, fp);
        put_u16(fp, symbol_value(sym));
        stroff += strlen(sym->name) + 1;
    }

    strsize = stroff;
    put_pdp_long(fp, strsize);
    for (i = 0; i < global_count; i++) {
        sym = &globals[i];
        fwrite(sym->name, 1, strlen(sym->name) + 1, fp);
    }
}

int obj2bsd_convert(
    char *infile,
    char *outfile)
{
    if (setjmp(convert_fail) != 0)
        return 0;

    reset_state();
    pass_object(infile, 1);
    assign_symbol_indices();

    if (text_size != 0) {
        text_bytes = calloc(text_size, 1);
        text_reloc = calloc((text_size + 1) >> 1, sizeof(unsigned short));
        if (text_bytes == NULL || text_reloc == NULL)
            fail("Out of memory\n", NULL);
    }
    if (data_size != 0) {
        data_bytes = calloc(data_size, 1);
        data_reloc = calloc((data_size + 1) >> 1, sizeof(unsigned short));
        if (data_bytes == NULL || data_reloc == NULL)
            fail("Out of memory\n", NULL);
    }

    current_psect = NULL;
    textaddr = 0;
    pass_object(infile, 2);

    out_fp = fopen(outfile, "wb");
    if (out_fp == NULL)
        fail("Can't open output file '%s'\n", outfile);

    write_exec_header(out_fp);
    write_bytes(out_fp, text_bytes, text_size);
    write_bytes(out_fp, data_bytes, data_size);
    write_relocs(out_fp, text_reloc, text_size);
    write_relocs(out_fp, data_reloc, data_size);
    write_symbols(out_fp);

    fclose(out_fp);
    out_fp = NULL;
    cleanup_state();
    return 1;
}

static void usage(
    void)
{
    fprintf(stderr, "usage: obj2bsd [-o outfile] infile.obj\n");
    exit(1);
}

int obj2bsd_main(
    int argc,
    char **argv)
{
    char           *infile;
    char           *outfile;
    int             i;

    infile = NULL;
    outfile = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc)
                usage();
            outfile = argv[i];
        } else if (strncmp(argv[i], "--outfile=", 10) == 0) {
            outfile = argv[i] + 10;
        } else if (strcmp(argv[i], "--bsd") == 0 || strcmp(argv[i], "--aout") == 0) {
            continue;
        } else if (argv[i][0] == '-') {
            usage();
        } else if (infile == NULL) {
            infile = argv[i];
        } else if (outfile == NULL) {
            outfile = argv[i];
        } else {
            usage();
        }
    }

    if (infile == NULL || outfile == NULL)
        usage();

    return obj2bsd_convert(infile, outfile) ? 0 : 1;
}

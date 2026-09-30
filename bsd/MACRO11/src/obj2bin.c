#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"

#define MEMSIZE 65536
#define MAX_PSECTS 64
#define MAX_GLOBALS 256
#define REC_MAX 2048
#define BLOCK_SIZE 128

typedef struct psect {
    char            name[32];
    unsigned        start;
    unsigned        length;
    unsigned        flags;
    int             number;
} PSECT;

typedef struct global_sym {
    char            name[8];
    unsigned        address;
} GLOBAL_SYM;

static unsigned char *memory;
static unsigned memory_size;
static PSECT    psects[MAX_PSECTS];
static int      psect_count = 0;
static GLOBAL_SYM globals[MAX_GLOBALS];
static int      global_count = 0;
static int      have_text = 0;
static unsigned adrmin = 0;
static unsigned adrmax = 0;
static unsigned program_start = 1;
static unsigned program_start_value = 1;
static unsigned program_end = 0;
static char     program_start_psect[32];
static char     module_prefix[4];
static PSECT   *current_psect = NULL;
static unsigned textaddr = 0;

static unsigned word_at(
    unsigned char *cp)
{
    return ((unsigned) cp[1] << 8) | cp[0];
}

static unsigned char record_checksum(
    unsigned char *buf,
    unsigned len)
{
    unsigned        i;
    unsigned        sum;

    sum = 0;
    for (i = 0; i < len; i++)
        sum += buf[i];
    return (unsigned char) ((-sum) & 0xff);
}

static void die(
    char *msg,
    char *arg)
{
    if (arg != NULL)
        fprintf(stderr, msg, arg);
    else
        fprintf(stderr, "%s", msg);
    exit(1);
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
    sprintf(out, "%02d:%-6.6s", 0, "");
    sprintf(out, "%s:%-6.6s", module, sym);
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
        die("Too many PSECTs\n", NULL);
    ps = &psects[psect_count];
    memset(ps, 0, sizeof(*ps));
    strcpy(ps->name, name);
    ps->number = psect_count + 1;
    psect_count++;
    return ps;
}

static void add_global(
    char *name,
    unsigned address)
{
    int             i;

    for (i = 0; i < global_count; i++) {
        if (strcmp(globals[i].name, name) == 0) {
            globals[i].address = address;
            return;
        }
    }
    if (global_count >= MAX_GLOBALS)
        die("Too many globals\n", NULL);
    strcpy(globals[global_count].name, name);
    globals[global_count].address = address;
    global_count++;
}

static unsigned get_global(
    char *name)
{
    int             i;

    for (i = 0; i < global_count; i++)
        if (strcmp(globals[i].name, name) == 0)
            return globals[i].address;
    return 0;
}

static void ensure_memory(
    unsigned need)
{
    unsigned char  *newmem;
    unsigned        newsize;

    if (need <= memory_size)
        return;
    if (need > MEMSIZE)
        die("Address out of range\n", NULL);

    newsize = memory_size;
    if (newsize == 0)
        newsize = 1024;
    while (newsize < need) {
        newsize <<= 1;
        if (newsize > MEMSIZE)
            newsize = MEMSIZE;
    }

    if (memory == NULL)
        newmem = calloc(newsize, 1);
    else
        newmem = realloc(memory, newsize);
    if (newmem == NULL)
        die("Out of memory\n", NULL);
    if (newsize > memory_size)
        memset(newmem + memory_size, 0, newsize - memory_size);
    memory = newmem;
    memory_size = newsize;
}

static void store_word(
    unsigned addr,
    unsigned value)
{
    ensure_memory(addr + 2);
    memory[addr & 0xffff] = value & 0xff;
    memory[(addr + 1) & 0xffff] = (value >> 8) & 0xff;
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
        die("Bad RT-11 object record\n", NULL);

    len = ((int) hdr[3] << 8) | hdr[2];
    if (len < 4 || len - 4 > REC_MAX)
        die("Oversize RT-11 object record\n", NULL);

    for (i = 0; i < len - 4; i++) {
        c = fgetc(fp);
        if (c == EOF)
            die("Unexpected EOF in object file\n", NULL);
        rec[i] = (unsigned char) c;
    }

    c = fgetc(fp);
    if (c == EOF)
        die("Missing RT-11 checksum\n", NULL);
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

static void parse_gsd(
    unsigned char *rec,
    int reclen,
    unsigned *psectaddr)
{
    int             i;
    char            sym[7];
    char            psect_name[32];
    char            global_name[8];
    unsigned        flg;
    unsigned        ent;
    unsigned        val;
    PSECT          *ps;

    for (i = 2; i + 7 < reclen; i += 8) {
        decode_rad50_name(rec + i, sym);
        flg = rec[i + 4];
        ent = rec[i + 5];
        val = word_at(rec + i + 6);

        if (ent == GSD_XFER) {
            make_psect_name(program_start_psect, module_prefix, sym);
            program_start_value = val;
        } else if (ent == GSD_GLOBAL) {
            trim_name(global_name, sym);
            if ((flg & GLOBAL_DEF) && current_psect != NULL)
                add_global(global_name, current_psect->start + val);
        } else if (ent == GSD_PSECT) {
            make_psect_name(psect_name, module_prefix, sym);
            ps = add_psect(psect_name);
            ps->flags = flg;
            current_psect = ps;
            if (flg & PSECT_REL) {
                *psectaddr = (*psectaddr + 1) & ~1;
                ps->start = *psectaddr;
                ps->length = val;
                *psectaddr += val;
            } else {
                ps->start = 0;
                ps->length = val;
            }
        }
    }
}

static void finish_pass1(
    void)
{
    int             i;
    unsigned        end;
    PSECT          *ps;

    for (i = 0; i < psect_count; i++) {
        end = psects[i].length ? psects[i].start + psects[i].length - 1 : psects[i].start;
        if (end > program_end)
            program_end = end;
    }

    if (program_start == 1) {
        ps = find_psect(program_start_psect);
        if (ps != NULL)
            program_start = ps->start + program_start_value;
        else
            program_start = program_start_value;
    }
}

static void parse_text(
    unsigned char *rec,
    int reclen)
{
    unsigned        off;
    unsigned        adr;
    unsigned        base;
    int             len;
    int             i;

    if (current_psect == NULL)
        die("TEXT record without active PSECT\n", NULL);

    off = word_at(rec + 2);
    len = reclen - 4;
    base = current_psect->start;
    adr = (base + off) & 0xffff;
    ensure_memory(adr + len);
    for (i = 0; i < len; i++) {
        memory[(adr + i) & 0xffff] = rec[4 + i];
    }

    if (!have_text || adr < adrmin)
        adrmin = adr;
    if (!have_text || (adr + len - 1) > adrmax)
        adrmax = adr + len - 1;
    have_text = 1;

    textaddr = adr;
}

static unsigned eval_complex(
    unsigned char *rec,
    int *idx,
    unsigned dis)
{
    unsigned        stack[32];
    int             sp;
    unsigned        adr;
    unsigned        value;
    unsigned        con;
    unsigned        op;
    char            name[7];
    PSECT          *ps;

    sp = 0;
    adr = (textaddr + dis - 4) & 0xffff;

    for (;;) {
        op = rec[*idx];
        (*idx)++;

        switch (op) {
        case CPLX_NOP:
            break;
        case CPLX_ADD:
            stack[sp - 2] = (stack[sp - 2] + stack[sp - 1]) & 0xffff;
            sp--;
            break;
        case CPLX_SUB:
            stack[sp - 2] = (stack[sp - 2] - stack[sp - 1]) & 0xffff;
            sp--;
            break;
        case CPLX_MUL:
            stack[sp - 2] = (stack[sp - 2] * stack[sp - 1]) & 0xffff;
            sp--;
            break;
        case CPLX_DIV:
            if (stack[sp - 1] == 0)
                stack[sp - 2] = 0;
            else
                stack[sp - 2] = (stack[sp - 2] / stack[sp - 1]) & 0xffff;
            sp--;
            break;
        case CPLX_AND:
            stack[sp - 2] &= stack[sp - 1];
            sp--;
            break;
        case CPLX_OR:
            stack[sp - 2] |= stack[sp - 1];
            sp--;
            break;
        case CPLX_XOR:
            stack[sp - 2] ^= stack[sp - 1];
            sp--;
            break;
        case CPLX_NEG:
            stack[sp - 1] = (-stack[sp - 1]) & 0xffff;
            break;
        case CPLX_COM:
            stack[sp - 1] = (~stack[sp - 1]) & 0xffff;
            break;
        case CPLX_STORE:
            return stack[sp - 1] & 0xffff;
        case CPLX_STORE_DISP:
            return (stack[sp - 1] - (adr + 2)) & 0xffff;
        case CPLX_GLOBAL:
            decode_rad50_name(rec + *idx, name);
            *idx += 4;
            stack[sp++] = get_global(name);
            break;
        case CPLX_REL:
            ps = find_psect_number(rec[*idx]);
            con = word_at(rec + *idx + 1);
            *idx += 3;
            if (ps == NULL)
                die("Unknown complex relocation PSECT\n", NULL);
            stack[sp++] = (ps->start + con) & 0xffff;
            break;
        case CPLX_CONST:
            value = word_at(rec + *idx);
            *idx += 2;
            stack[sp++] = value;
            break;
        default:
            die("Unknown complex relocation opcode\n", NULL);
        }
    }
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
    unsigned        value;
    char            name[7];
    char            psect_name[32];
    PSECT          *ps;

    i = 2;
    while (i < reclen) {
        ent = rec[i] & 0x7f;
        dis = rec[i + 1];

        switch (ent) {
        case RLD_INT:
            con = word_at(rec + i + 2);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (current_psect->start + con) & 0xffff;
            store_word(adr, value);
            i += 4;
            break;
        case RLD_INT_DISP:
            con = word_at(rec + i + 2);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (con - (adr + 2)) & 0xffff;
            store_word(adr, value);
            i += 4;
            break;
        case RLD_GLOBAL:
            decode_rad50_name(rec + i + 2, name);
            adr = (textaddr + dis - 4) & 0xffff;
            store_word(adr, get_global(name));
            i += 6;
            break;
        case RLD_GLOBAL_DISP:
            decode_rad50_name(rec + i + 2, name);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (get_global(name) - (adr + 2)) & 0xffff;
            store_word(adr, value);
            i += 6;
            break;
        case RLD_GLOBAL_OFFSET:
            decode_rad50_name(rec + i + 2, name);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (get_global(name) + con) & 0xffff;
            store_word(adr, value);
            i += 8;
            break;
        case RLD_GLOBAL_OFFSET_DISP:
            decode_rad50_name(rec + i + 2, name);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (get_global(name) + con - (adr + 2)) & 0xffff;
            store_word(adr, value);
            i += 8;
            break;
        case RLD_LOCDEF:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            current_psect = find_psect(psect_name);
            textaddr = word_at(rec + i + 6) & 0xffff;
            if (current_psect == NULL)
                die("Unknown PSECT in location definition\n", NULL);
            i += 8;
            break;
        case RLD_LOCMOD:
            textaddr = word_at(rec + i + 2) & 0xffff;
            i += 4;
            break;
        case RLD_LIMITS:
            adr = (textaddr + dis - 4) & 0xffff;
            store_word(adr, 01000);
            store_word(adr + 2, program_end);
            i += 2;
            break;
        case RLD_PSECT:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                die("Unknown PSECT relocation\n", NULL);
            adr = (textaddr + dis - 4) & 0xffff;
            store_word(adr, ps->start);
            i += 6;
            break;
        case RLD_PSECT_DISP:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                die("Unknown PSECT displaced relocation\n", NULL);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (ps->start - (adr + 2)) & 0xffff;
            store_word(adr, value);
            i += 6;
            break;
        case RLD_PSECT_OFFSET:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                die("Unknown PSECT additive relocation\n", NULL);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (ps->start + con) & 0xffff;
            store_word(adr, value);
            i += 8;
            break;
        case RLD_PSECT_OFFSET_DISP:
            decode_rad50_name(rec + i + 2, name);
            make_psect_name(psect_name, module_prefix, name);
            ps = find_psect(psect_name);
            if (ps == NULL)
                die("Unknown PSECT additive displaced relocation\n", NULL);
            con = word_at(rec + i + 6);
            adr = (textaddr + dis - 4) & 0xffff;
            value = (ps->start + con - (adr + 2)) & 0xffff;
            store_word(adr, value);
            i += 8;
            break;
        case RLD_COMPLEX:
            i += 2;
            adr = (textaddr + dis - 4) & 0xffff;
            value = eval_complex(rec, &i, dis);
            store_word(adr, value);
            break;
        default:
            die("Unsupported relocation type\n", NULL);
        }
    }
}

static void pass_object(
    char *filename,
    int pass)
{
    FILE           *fp;
    unsigned char   rec[REC_MAX];
    int             reclen;
    unsigned        psectaddr;

    fp = fopen(filename, "rb");
    if (fp == NULL)
        die("Can't open input file '%s'\n", filename);

    if (pass == 1) {
        psectaddr = 0;
        current_psect = NULL;
    }

    while (read_rt11_record(fp, rec, &reclen)) {
        switch (rec[0]) {
        case OBJ_GSD:
            if (pass == 1)
                parse_gsd(rec, reclen, &psectaddr);
            break;
        case OBJ_ENDGSD:
            if (pass == 1)
                finish_pass1();
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

    fclose(fp);
}

static void write_loader_record(
    FILE *fp,
    unsigned addr,
    unsigned char *data,
    unsigned count)
{
    unsigned char   rec[BLOCK_SIZE + 7];
    unsigned        len;

    len = count + 6;
    rec[0] = FBR_LEAD1;
    rec[1] = FBR_LEAD2;
    rec[2] = len & 0xff;
    rec[3] = (len >> 8) & 0xff;
    rec[4] = addr & 0xff;
    rec[5] = (addr >> 8) & 0xff;
    memcpy(rec + 6, data, count);
    rec[6 + count] = record_checksum(rec, 6 + count);
    fwrite(rec, 1, 7 + count, fp);
}

static void write_output(
    char *filename)
{
    FILE           *fp;
    unsigned        addr;
    unsigned        count;
    unsigned char   endrec[7];

    if (!have_text)
        die("No text records found\n", NULL);

    fp = fopen(filename, "wb");
    if (fp == NULL)
        die("Can't open output file '%s'\n", filename);

    for (addr = adrmin; addr <= adrmax; addr += BLOCK_SIZE) {
        count = BLOCK_SIZE;
        if (addr + count - 1 > adrmax)
            count = adrmax - addr + 1;
        write_loader_record(fp, addr, memory + addr, count);
    }

    endrec[0] = FBR_LEAD1;
    endrec[1] = FBR_LEAD2;
    endrec[2] = 6;
    endrec[3] = 0;
    endrec[4] = program_start & 0xff;
    endrec[5] = (program_start >> 8) & 0xff;
    endrec[6] = record_checksum(endrec, 6);
    fwrite(endrec, 1, 7, fp);

    fclose(fp);
}

static void usage(
    void)
{
    fprintf(stderr, "usage: obj2bin [--binary] [--rt11] [-o outfile] infile.obj\n");
    exit(1);
}

int main(
    int argc,
    char **argv)
{
    char           *infile;
    char           *outfile;
    int             i;

    infile = NULL;
    outfile = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--binary") == 0 || strcmp(argv[i], "--rt11") == 0) {
            continue;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc)
                usage();
            outfile = argv[i];
        } else if (strncmp(argv[i], "--outfile=", 10) == 0) {
            outfile = argv[i] + 10;
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

    strcpy(module_prefix, "01");
    pass_object(infile, 1);
    current_psect = NULL;
    textaddr = 0;
    pass_object(infile, 2);
    write_output(outfile);
    free(memory);

    return 0;
}

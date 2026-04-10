
#ifndef SYMBOLS__H
#define SYMBOLS__H

/* max symbol_len can be adjusted between SYMMAX_DEFAULT and SYMMAX_MAX*/
#define SYMMAX_DEFAULT 6               /* I will honor this many character symbols */

#define SYMMAX_MAX 64


/* Program sections: */
typedef struct section {
    char           *label;      /* Section name */
    unsigned        type;       /* Section type */
#define SECTION_USER 1          /* user-defined */
#define SECTION_SYSTEM 2        /* A system symbol (like "."; value is an enum) */
#define SECTION_INSTRUCTION 3   /* An instruction code (like "MOV"; value is an enum) */
#define SECTION_PSEUDO 4        /* A pseudo-op (.PSECT, .TITLE, .MACRO, .IF; value is an enum) */
#define SECTION_REGISTER 5      /* Symbol is a register (value 0=$0, value 1=$1, ... $7) */
#define SECTION_USERMACRO 6     /* Symbol is a user macro */

    unsigned        flags;      /* Flags, defined in object.h */
    unsigned        pc;         /* Current offset in the section */
    unsigned        size;       /* Current section size */
    unsigned        sector;     /* Used for complex relocation, and naught else */
} SECTION;

/* Symbol table entries */

typedef struct symbol {
    char           *label;      /* Symbol name */
    unsigned        value;      /* Symbol value */
    int             stmtno;     /* Statement number of symbol's definition */
    unsigned        flags;      /* Symbol flags */
#define SYMBOLFLAG_PERMANENT 1  /* Symbol may not be redefined */
#define SYMBOLFLAG_GLOBAL 2     /* Symbol is global */
#define SYMBOLFLAG_WEAK 4       /* Symbol definition is weak */
#define SYMBOLFLAG_DEFINITION 8 /* Symbol is a global definition, not reference */
#define SYMBOLFLAG_UNDEFINED 16 /* Symbol is a phony, undefined */
#define SYMBOLFLAG_LOCAL 32     /* Set if this is a local label (i.e. 10$) */
#define SYMBOLFLAG_STATIC 64    /* Storage is static, not heap */
#define SYMBOLFLAG_POOL 128     /* Struct storage came from a pool */

    SECTION        *section;    /* Section in which this symbol is defined */
    struct symbol  *next;       /* Next symbol with the same hash value */
} SYMBOL;




enum pseudo_ops { P_ASCII,
    P_ASCIZ,
    P_ASECT,
    P_BLKB,
    P_BLKW,
    P_BYTE,
    P_CSECT,
    P_DSABL,
    P_ENABL,
    P_END,
    P_ENDC,
    P_ENDM,
    P_ENDR,
    P_EOT,
    P_ERROR,
    P_EVEN,
    P_FLT2,
    P_FLT4,
    P_GLOBL,
    P_IDENT,
    P_IF,
    P_IFF,
    P_IFT,
    P_IFTF,
    P_IIF,
    P_IRP,
    P_IRPC,
    P_LIMIT,
    P_LIST,
    P_MCALL,
    P_MEXIT,
    P_NARG,
    P_NCHR,
    P_NLIST,
    P_NTYPE,
    P_ODD,
    P_PACKED,
    P_PAGE,
    P_PRINT,
    P_PSECT,
    P_RADIX,
    P_RAD50,
    P_REM,
    P_REPT,
    P_RESTORE,
    P_SAVE,
    P_SBTTL,
    P_TITLE,
    P_WORD,
    P_MACRO,
    P_INCLUDE,
    P_WEAK,
    P_IFDF
};

/*
 * Instruction opcodes are macros rather than enum members because
 * old 16-bit PDP-11 compilers store enums in signed int, which
 * overflows for values above 077777.
 */
#define I_ADC 0005500
#define I_ADCB (-072300)
#define I_ADD 0060000
#define I_ASH 0072000
#define I_ASHC 0073000
#define I_ASL 0006300
#define I_ASLB (-071500)
#define I_ASR 0006200
#define I_ASRB (-071600)
#define I_BCC (-075000)
#define I_BCS (-074400)
#define I_BEQ 0001400
#define I_BGE 0002000
#define I_BGT 0003000
#define I_BHI (-077000)
#define I_BHIS (-075000)
#define I_BIC 0040000
#define I_BICB (-040000)
#define I_BIS 0050000
#define I_BISB (-030000)
#define I_BIT 0030000
#define I_BITB (-050000)
#define I_BLE 0003400
#define I_BLO (-074400)
#define I_BLOS (-076400)
#define I_BLT 0002400
#define I_BMI (-077400)
#define I_BNE 0001000
#define I_BPL (-077777-1)
#define I_BPT 0000003
#define I_BR 0000400
#define I_BVC (-076000)
#define I_BVS (-075400)
#define I_CALL 0004700
#define I_CALLR 0000100
#define I_CCC 0000257
#define I_CLC 0000241
#define I_CLN 0000250
#define I_CLR 0005000
#define I_CLRB (-073000)
#define I_CLV 0000242
#define I_CLZ 0000244
#define I_CMP 0020000
#define I_CMPB (-060000)
#define I_COM 0005100
#define I_COMB (-072700)
#define I_DEC 0005300
#define I_DECB (-072500)
#define I_DIV 0071000
#define I_EMT (-074000)
#define I_FADD 0075000
#define I_FDIV 0075030
#define I_FMUL 0075020
#define I_FSUB 0075010
#define I_HALT 0000000
#define I_INC 0005200
#define I_INCB (-072600)
#define I_IOT 0000004
#define I_JMP 0000100
#define I_JSR 0004000
#define I_MARK 0006400
#define I_MED6X 0076600
#define I_MED74C 0076601
#define I_MFPD (-071300)
#define I_MFPI 0006500
#define I_MFPS (-071100)
#define I_MOV 0010000
#define I_MOVB (-070000)
#define I_MTPD (-071200)
#define I_MTPI 0006600
#define I_MTPS (-071400)
#define I_MUL 0070000
#define I_NEG 0005400
#define I_NEGB (-072400)
#define I_NOP 0000240
#define I_RESET 0000005
#define I_RETURN 0000207
#define I_ROL 0006100
#define I_ROLB (-071700)
#define I_ROR 0006000
#define I_RORB (-072000)
#define I_RTI 0000002
#define I_RTS 0000200
#define I_RTT 0000006
#define I_SBC 0005600
#define I_SBCB (-072200)
#define I_SCC 0000277
#define I_SEC 0000261
#define I_SEN 0000270
#define I_SEV 0000262
#define I_SEZ 0000264
#define I_SOB 0077000
#define I_SPL 0000230
#define I_SUB (-020000)
#define I_SWAB 0000300
#define I_SXT 0006700
#define I_TRAP (-073400)
#define I_TST 0005700
#define I_TSTB (-072100)
#define I_WAIT 0000001
#define I_XFC 0076700
#define I_XOR 0074000
#define I_MFPT 0000007
/* CIS not implemented - maybe later */
/* FPU */
#define I_ABSD (-07200)
#define I_ABSF (-07200)
#define I_ADDD (-06000)
#define I_ADDF (-06000)
#define I_CFCC (-010000)
#define I_CLRD (-07400)
#define I_CLRF (-07400)
#define I_CMPD (-04400)
#define I_CMPF (-04400)
#define I_DIVD (-03400)
#define I_DIVF (-03400)
#define I_LDCDF (-0400)
#define I_LDCFD (-0400)
#define I_LDCID (-01000)
#define I_LDCIF (-01000)
#define I_LDCLD (-01000)
#define I_LDCLF (-01000)
#define I_LDD (-05400)
#define I_LDEXP (-01400)
#define I_LDF (-05400)
#define I_LDFPS (-07700)
#define I_MODD (-06400)
#define I_MODF (-06400)
#define I_MULD (-07000)
#define I_MULF (-07000)
#define I_NEGD (-07100)
#define I_NEGF (-07100)
#define I_SETD (-07767)
#define I_SETF (-07777)
#define I_SETI (-07776)
#define I_SETL (-07766)
#define I_STA0 (-07773)
#define I_STB0 (-07772)
#define I_STCDF (-02000)
#define I_STCDI (-02400)
#define I_STCDL (-02400)
#define I_STCFD (-02000)
#define I_STCFI (-02400)
#define I_STCFL (-02400)
#define I_STD (-04000)
#define I_STEXP (-03000)
#define I_STF (-04000)
#define I_STFPS (-07600)
#define I_STST (-07500)
#define I_SUBD (-05000)
#define I_SUBF (-05000)
#define I_TSTD (-07300)
#define I_TSTF (-07300)

/* mask over flags for operand types */
#define OC_MASK (-0400)
#define OC_NONE 0x0000      /* No operands */
#define OC_1GEN 0x0100      /* One general operand (CLR, TST, etc.) */
#define OC_2GEN 0x0200      /* Two general operand (MOV, CMP, etc.) */
#define OC_BR 0x0300        /* Branch */
#define OC_ASH 0x0400       /* ASH and ASHC (one gen, one reg) */
#define OC_MARK 0x0500      /* MARK instruction operand */
#define OC_JSR 0x0600       /* JSR, XOR (one reg, one gen) */
#define OC_1REG 0x0700      /* FADD, FSUB, FMUL, FDIV, RTS */
#define OC_SOB 0x0800       /* SOB */
#define OC_1FIS 0x0900      /* FIS (reg, gen) */
#define OC_2FIS 0x0a00      /* FIS (gen, reg) */
#define OC__LAST (-0400)



/* symbol tables */

#ifdef SMALL_MEMORY
#define HASH_SIZE 127
#else
#define HASH_SIZE 1023
#endif

typedef struct symbol_table {
    SYMBOL         *hash[HASH_SIZE];
} SYMBOL_TABLE;


/* SYMBOL_ITER is used for iterating thru a symbol table. */
typedef struct symbol_iter {
    int             subscript;  /* Current hash subscript */
    SYMBOL         *current;    /* Current symbol */
} SYMBOL_ITER;


#ifndef SYMBOLS__C

extern int      symbol_len;     /* max. len of symbols. default = 6 */
extern int      symbol_allow_underscores;       /* allow "_" in symbol names */

extern SYMBOL  *reg_sym[8];     /* Keep the register symbols in a handy array */

extern SYMBOL_TABLE system_st;  /* System symbols (Instructions,
                                   pseudo-ops, registers) */

extern SYMBOL_TABLE section_st; /* Program sections */

extern SYMBOL_TABLE symbol_st;  /* User symbols */

extern SYMBOL_TABLE macro_st;   /* Macros */

extern SYMBOL_TABLE implicit_st;        /* The symbols which may be implicit globals */

#endif

int             hash_name(
    char *label);

SYMBOL         *add_sym(
    char *label,
    unsigned value,
    unsigned flags,
    SECTION *section,
    SYMBOL_TABLE *table);
SYMBOL         *first_sym(
    SYMBOL_TABLE *table,
    SYMBOL_ITER *iter);

SYMBOL         *lookup_sym(
    char *label,
    SYMBOL_TABLE *table);
SYMBOL         *next_sym(
    SYMBOL_TABLE *table,
    SYMBOL_ITER *iter);
void            free_sym(
    SYMBOL *sym);

void            remove_sym(
    SYMBOL *sym,
    SYMBOL_TABLE *table);

char           *symflags(
    SYMBOL *sym);

void            add_table(
    SYMBOL *sym,
    SYMBOL_TABLE *table);


void            add_symbols(
    SECTION *current_section);

#endif

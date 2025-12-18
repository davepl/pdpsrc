#if defined(__pdp11__) || defined(pdp11) || defined(__PDP11) || defined(__pdp11)
#define PDP11 1
#else
#define PDP11 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#if !PDP11
#include <sys/select.h>
#include <unistd.h>
#else
int select(int, void *, void *, void *, struct timeval *);
unsigned int sleep(unsigned int);
#endif

#define MAX_GOSUB 64
#define MAX_FOR_DEPTH 32

struct line {
    int number;
    char *text;
    struct line *next;
};

struct program {
    struct line **lines;
    int count;
};

struct for_frame {
    int var;
    double limit;
    double step;
    int line_index;
    char *position;
};

static struct program prog;
static double vars[26];
struct return_frame {
    int line_index;
    char *position;
};
static struct return_frame gosub_stack[MAX_GOSUB];
static int gosub_top = 0;
static struct for_frame for_stack[MAX_FOR_DEPTH];
static int for_top = 0;

static int pc_index = 0;
static char *pc_pos = NULL;

static int running = 1;

static char *read_file_line(FILE *fp);
static char *dup_string(const char *s);
static char *trim_left(char *s);
static void add_line(struct line **head, struct line *ln);
static int finalize_program(struct line *head, struct program *out);
static int compare_line(const void *a, const void *b);
static void free_program(struct program *p);
static void reset_state(void);
static void run_program(void);
static char *execute_statement(struct line *ln, char *pos);
static double parse_expression(char **p);
static double parse_relational(char **p);
static double parse_term(char **p);
static double parse_factor(char **p);
static double parse_primary(char **p);
static int parse_variable(char **p);
static double parse_number(char **p);
static double parse_function(const char *name, double arg);
static void expect_char(char **p, char ch);
static void skip_spaces(char **p);
static int match_keyword(char **p, const char *kw);
static void do_print(char **p);
static void do_input(char **p);
static void do_if(struct line *ln, char **p);
static void do_goto(char **p);
static void do_gosub(char **p, char *ret_pos);
static void do_return(void);
static void do_for(struct line *ln, char **p, char *start_pos);
static void do_next(char **p);
static void do_sleep_cmd(char **p);
static void sleep_ticks(int ticks);
static int handle_tab(char **p, int *col);
static struct line *line_for_index(int idx);
static int find_line_index(int number);
static char *next_statement(char *p);
static int ci_compare_n(const char *a, const char *b, size_t n);
static int ci_compare(const char *a, const char *b);
static int ascii_upper(int c);
static int ascii_isalpha(int c);

int
main(int argc, char **argv)
{
    FILE *fp;
    char *linebuf;
    struct line *head;
    int ok;

    if (argc < 2) {
        fprintf(stderr, "usage: %s program.bas\n", argv[0]);
        return 1;
    }

    fp = fopen(argv[1], "r");
    if (!fp) {
        perror("open program");
        return 1;
    }

    head = NULL;
    while ((linebuf = read_file_line(fp)) != NULL) {
        char *p;
        struct line *ln;
        int number;

        p = linebuf;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!isdigit((unsigned char)*p)) {
            fprintf(stderr, "Line missing number: %s\n", linebuf);
            free(linebuf);
            free_program(&prog);
            fclose(fp);
            return 1;
        }
        number = atoi(p);
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        p = trim_left(p);

        ln = (struct line *)malloc(sizeof(struct line));
        if (!ln) {
            fprintf(stderr, "Out of memory\n");
            free(linebuf);
            fclose(fp);
            return 1;
        }
        ln->number = number;
        ln->text = dup_string(p);
        ln->next = NULL;
        add_line(&head, ln);
        free(linebuf);
    }
    fclose(fp);

    ok = finalize_program(head, &prog);
    if (!ok) {
        free_program(&prog);
        return 1;
    }

    reset_state();
    run_program();
    free_program(&prog);
    return 0;
}

static char *
read_file_line(FILE *fp)
{
    size_t cap;
    size_t len;
    int ch;
    char *buf;
    char *tmp;

    cap = 128;
    buf = (char *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    len = 0;
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\r') {
            continue;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            tmp = (char *)realloc(buf, cap);
            if (!tmp) {
                free(buf);
                return NULL;
            }
            buf = tmp;
        }
        if (ch == '\n') {
            break;
        }
        buf[len++] = (char)ch;
    }
    if (len == 0 && ch == EOF) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static char *
dup_string(const char *s)
{
    size_t n;
    char *r;

    n = strlen(s);
    r = (char *)malloc(n + 1);
    if (!r) {
        return NULL;
    }
    memcpy(r, s, n + 1);
    return r;
}

static char *
trim_left(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static int
ci_compare_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca;
        int cb;
        ca = ascii_upper((unsigned char)a[i]);
        cb = ascii_upper((unsigned char)b[i]);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
    return 0;
}

static int
ci_compare(const char *a, const char *b)
{
    size_t i;
    i = 0;
    while (a[i] && b[i]) {
        int ca;
        int cb;
        ca = ascii_upper((unsigned char)a[i]);
        cb = ascii_upper((unsigned char)b[i]);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        i++;
    }
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int
ascii_upper(int c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    }
    return c;
}

static int
ascii_isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void
add_line(struct line **head, struct line *ln)
{
    struct line *cur;
    struct line *prev;

    prev = NULL;
    cur = *head;
    while (cur && cur->number < ln->number) {
        prev = cur;
        cur = cur->next;
    }
    if (cur && cur->number == ln->number) {
        free(cur->text);
        cur->text = ln->text;
        free(ln);
        return;
    }
    if (!prev) {
        ln->next = *head;
        *head = ln;
    } else {
        ln->next = cur;
        prev->next = ln;
    }
}

static int
compare_line(const void *a, const void *b)
{
    const struct line *la;
    const struct line *lb;

    la = *(const struct line * const *)a;
    lb = *(const struct line * const *)b;
    if (la->number < lb->number) {
        return -1;
    }
    if (la->number > lb->number) {
        return 1;
    }
    return 0;
}

static int
finalize_program(struct line *head, struct program *out)
{
    int count;
    struct line *cur;
    int i;

    count = 0;
    cur = head;
    while (cur) {
        count++;
        cur = cur->next;
    }
    out->lines = (struct line **)malloc(sizeof(struct line *) * count);
    if (!out->lines) {
        fprintf(stderr, "Out of memory\n");
        return 0;
    }
    out->count = count;
    cur = head;
    i = 0;
    while (cur) {
        out->lines[i++] = cur;
        cur = cur->next;
    }
    qsort(out->lines, count, sizeof(struct line *), compare_line);
    for (i = 1; i < count; i++) {
        out->lines[i - 1]->next = out->lines[i];
    }
    if (count > 0) {
        out->lines[count - 1]->next = NULL;
    }
    return 1;
}

static void
free_program(struct program *p)
{
    int i;
    if (!p->lines) {
        return;
    }
    for (i = 0; i < p->count; i++) {
        free(p->lines[i]->text);
        free(p->lines[i]);
    }
    free(p->lines);
    p->lines = NULL;
    p->count = 0;
}

static void
reset_state(void)
{
    int i;
    for (i = 0; i < 26; i++) {
        vars[i] = 0.0;
    }
    gosub_top = 0;
    for_top = 0;
    running = 1;
    pc_index = 0;
    pc_pos = NULL;
}

static struct line *
line_for_index(int idx)
{
    if (idx < 0 || idx >= prog.count) {
        return NULL;
    }
    return prog.lines[idx];
}

static int
find_line_index(int number)
{
    int lo;
    int hi;

    lo = 0;
    hi = prog.count - 1;
    while (lo <= hi) {
        int mid;
        int num;

        mid = (lo + hi) / 2;
        num = prog.lines[mid]->number;
        if (number < num) {
            hi = mid - 1;
        } else if (number > num) {
            lo = mid + 1;
        } else {
            return mid;
        }
    }
    return -1;
}

static char *
next_statement(char *p)
{
    int in_string;
    if (!p) {
        return NULL;
    }
    in_string = 0;
    while (*p) {
        if (*p == '"') {
            in_string = !in_string;
        } else if (*p == ':' && !in_string) {
            break;
        }
        p++;
    }
    if (*p == ':') {
        p++;
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            return NULL;
        }
        return p;
    }
    return NULL;
}

static void
run_program(void)
{
    while (running && pc_index < prog.count) {
        struct line *ln;
        char *pos;

        ln = line_for_index(pc_index);
        pos = pc_pos ? pc_pos : ln->text;
        if (!pos) {
            pc_index++;
            pc_pos = NULL;
            continue;
        }
        pc_pos = execute_statement(ln, pos);
        if (!pc_pos) {
            pc_index++;
        }
    }
}

static char *
execute_statement(struct line *ln, char *pos)
{
    char *p;

    p = pos;
    p = trim_left(p);
    if (*p == '\0') {
        return next_statement(pos);
    }

    if (match_keyword(&p, "REM")) {
        return NULL;
    } else if (match_keyword(&p, "LET")) {
        int var;
        double val;

        var = parse_variable(&p);
        if (var < 0) {
            fprintf(stderr, "Syntax error in line %d\n", ln->number);
            running = 0;
            return NULL;
        }
        skip_spaces(&p);
        expect_char(&p, '=');
        val = parse_expression(&p);
        vars[var] = val;
    } else if (isalpha((unsigned char)*p) && isupper((unsigned char)*p) && p[1] == '=') {
        int var;
        double val;

        var = toupper((unsigned char)*p) - 'A';
        p += 2;
        val = parse_expression(&p);
        vars[var] = val;
    } else if (match_keyword(&p, "PRINT") || match_keyword(&p, "?")) {
        do_print(&p);
    } else if (match_keyword(&p, "INPUT")) {
        do_input(&p);
    } else if (match_keyword(&p, "IF")) {
        do_if(ln, &p);
        return pc_pos;
    } else if (match_keyword(&p, "GOTO")) {
        do_goto(&p);
        return pc_pos;
    } else if (match_keyword(&p, "GOSUB")) {
        do_gosub(&p, next_statement(pos));
        return pc_pos;
    } else if (match_keyword(&p, "RETURN")) {
        do_return();
        return pc_pos;
    } else if (match_keyword(&p, "FOR")) {
        do_for(ln, &p, next_statement(pos));
        return next_statement(pos);
    } else if (match_keyword(&p, "NEXT")) {
        do_next(&p);
        return pc_pos;
    } else if (match_keyword(&p, "END") || match_keyword(&p, "STOP")) {
        running = 0;
        return NULL;
    } else if (match_keyword(&p, "SLEEP")) {
        do_sleep_cmd(&p);
    } else {
        fprintf(stderr, "Unknown statement at line %d: %s\n", ln->number, p);
        running = 0;
        return NULL;
    }
    return next_statement(pos);
}

static void
skip_spaces(char **p)
{
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int
match_keyword(char **p, const char *kw)
{
    size_t n;
    char *s;

    s = *p;
    skip_spaces(&s);
    n = strlen(kw);
    if (ci_compare_n(s, kw, n) == 0) {
        if (ascii_isalpha((unsigned char)kw[n - 1])) {
            if (ascii_isalpha((unsigned char)s[n])) {
                return 0;
            }
        }
        s += n;
        *p = s;
        skip_spaces(p);
        return 1;
    }
    return 0;
}

static void
expect_char(char **p, char ch)
{
    skip_spaces(p);
    if (**p != ch) {
        fprintf(stderr, "Expected '%c'\n", ch);
        running = 0;
        return;
    }
    (*p)++;
    skip_spaces(p);
}

static int
parse_variable(char **p)
{
    int idx;
    skip_spaces(p);
    if (!isalpha((unsigned char)**p)) {
        return -1;
    }
    idx = toupper((unsigned char)**p) - 'A';
    (*p)++;
    skip_spaces(p);
    return idx;
}

static double
parse_expression(char **p)
{
    return parse_relational(p);
}

static double
parse_relational(char **p)
{
    double left;
    double right;
    int op;

    left = parse_term(p);
    skip_spaces(p);
    op = 0;
    if (**p == '=' || **p == '<' || **p == '>') {
        if (**p == '=') {
            op = 1;
            (*p)++;
        } else if (**p == '<') {
            (*p)++;
            if (**p == '=') {
                op = 2;
                (*p)++;
            } else if (**p == '>') {
                op = 3;
                (*p)++;
            } else {
                op = 4;
            }
        } else if (**p == '>') {
            (*p)++;
            if (**p == '=') {
                op = 5;
                (*p)++;
            } else {
                op = 6;
            }
        }
        right = parse_term(p);
        if (op == 1) {
            left = (left == right) ? -1.0 : 0.0;
        } else if (op == 2) {
            left = (left <= right) ? -1.0 : 0.0;
        } else if (op == 3) {
            left = (left != right) ? -1.0 : 0.0;
        } else if (op == 4) {
            left = (left < right) ? -1.0 : 0.0;
        } else if (op == 5) {
            left = (left >= right) ? -1.0 : 0.0;
        } else if (op == 6) {
            left = (left > right) ? -1.0 : 0.0;
        }
    }
    return left;
}

static double
parse_term(char **p)
{
    double value;
    int op;

    value = parse_factor(p);
    skip_spaces(p);
    while (**p == '+' || **p == '-') {
        op = **p;
        (*p)++;
        skip_spaces(p);
        if (op == '+') {
            value += parse_factor(p);
        } else {
            value -= parse_factor(p);
        }
        skip_spaces(p);
    }
    return value;
}

static double
parse_factor(char **p)
{
    double value;
    int op;

    value = parse_primary(p);
    skip_spaces(p);
    while (**p == '*' || **p == '/' || **p == '^') {
        op = **p;
        (*p)++;
        skip_spaces(p);
        if (op == '*') {
            value *= parse_primary(p);
        } else if (op == '/') {
            value /= parse_primary(p);
        } else {
            value = pow(value, parse_primary(p));
        }
        skip_spaces(p);
    }
    return value;
}

static double
parse_primary(char **p)
{
    double value;
    char *s;

    skip_spaces(p);
    s = *p;
    if (*s == '+') {
        (*p)++;
        value = parse_primary(p);
    } else if (*s == '-') {
        (*p)++;
        value = -parse_primary(p);
    } else if (*s == '(') {
        (*p)++;
        value = parse_expression(p);
        expect_char(p, ')');
    } else if (isdigit((unsigned char)*s) || *s == '.') {
        value = parse_number(p);
    } else if (isalpha((unsigned char)*s)) {
        char name[16];
        int idx;
        int n;

        n = 0;
        while (isalpha((unsigned char)*s) && n < 15) {
            name[n++] = toupper((unsigned char)*s);
            s++;
        }
        name[n] = '\0';
        *p += n;
        skip_spaces(p);
        if (**p == '(') {
            (*p)++;
            value = parse_expression(p);
            expect_char(p, ')');
            value = parse_function(name, value);
        } else if (n > 1) {
            if (ci_compare(name, "RND") == 0) {
                value = parse_function(name, 0.0);
            } else {
                fprintf(stderr, "Function %s needs parentheses\n", name);
                running = 0;
                value = 0.0;
            }
        } else {
            idx = name[0] - 'A';
            value = vars[idx];
        }
    } else {
        fprintf(stderr, "Syntax error near '%s'\n", *p);
        running = 0;
        value = 0.0;
    }
    return value;
}

static double
parse_function(const char *name, double arg)
{
    if (strcmp(name, "ABS") == 0) {
        return fabs(arg);
    } else if (strcmp(name, "INT") == 0) {
        return floor(arg);
    } else if (strcmp(name, "SQR") == 0) {
        return sqrt(arg);
    } else if (strcmp(name, "EXP") == 0) {
        return exp(arg);
    } else if (strcmp(name, "LOG") == 0) {
        return log(arg);
    } else if (strcmp(name, "SIN") == 0) {
        return sin(arg);
    } else if (strcmp(name, "COS") == 0) {
        return cos(arg);
    } else if (strcmp(name, "TAN") == 0) {
        return tan(arg);
    } else if (strcmp(name, "RND") == 0) {
        if (arg <= 0.0) {
            return (double)rand() / (double)RAND_MAX;
        }
        return ((double)(rand() % (int)arg)) / arg;
    }
    fprintf(stderr, "Unknown function %s\n", name);
    running = 0;
    return 0.0;
}

static void
do_print(char **p)
{
    int newline;
    int at_start;
    int col;

    newline = 1;
    at_start = 1;
    col = 0;
    skip_spaces(p);
    while (**p && **p != ':') {
        if (**p == '"') {
            (*p)++;
            while (**p && **p != '"') {
                putchar(**p);
                (*p)++;
                col++;
            }
            if (**p == '"') {
                (*p)++;
            }
            newline = 1;
            at_start = 0;
        } else if (**p == ';' || **p == ',') {
            char sep;
            sep = **p;
            (*p)++;
            if (sep == ';') {
                newline = 0;
            } else {
                int spaces;
                newline = 0;
                spaces = 14 - (col % 14);
                while (spaces-- > 0) {
                    putchar(' ');
                    col++;
                }
            }
        } else {
            double v;
            int written;
            if (handle_tab(p, &col)) {
                newline = 0;
                at_start = 0;
                continue;
            }
            v = parse_expression(p);
            written = printf("%g", v);
            col += written;
            newline = 1;
            at_start = 0;
        }
        skip_spaces(p);
    }
    if (newline || at_start) {
        putchar('\n');
    }
    fflush(stdout);
}

static void
do_input(char **p)
{
    char prompt[64];
    int got_prompt;

    got_prompt = 0;
    skip_spaces(p);
    if (**p == '"') {
        int idx;
        (*p)++;
        idx = 0;
        while (**p && **p != '"' && idx < (int)sizeof(prompt) - 1) {
            prompt[idx++] = **p;
            (*p)++;
        }
        prompt[idx] = '\0';
        if (**p == '"') {
            (*p)++;
        }
        skip_spaces(p);
        if (**p == ';' || **p == ',') {
            (*p)++;
        }
        got_prompt = 1;
    }
    if (!got_prompt) {
        strcpy(prompt, "? ");
    }
    printf("%s", prompt);
    fflush(stdout);
    while (**p) {
        int var;
        double val;
        char buf[128];

        var = parse_variable(p);
        if (var < 0) {
            fprintf(stderr, "Bad variable in INPUT\n");
            running = 0;
            return;
        }
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            running = 0;
            return;
        }
        val = atof(buf);
        vars[var] = val;
        skip_spaces(p);
        if (**p == ',') {
            (*p)++;
            skip_spaces(p);
            printf("? ");
            fflush(stdout);
            continue;
        }
        break;
    }
}

static void
do_if(struct line *ln, char **p)
{
    double cond;

    cond = parse_expression(p);
    skip_spaces(p);
    if (!match_keyword(p, "THEN")) {
        fprintf(stderr, "Missing THEN in line %d\n", ln->number);
        running = 0;
        return;
    }
    if (cond != 0.0) {
        if (isdigit((unsigned char)**p)) {
            do_goto(p);
        } else if (**p != '\0') {
            pc_pos = *p;
        }
    } else {
        pc_pos = NULL;
    }
}

static void
do_goto(char **p)
{
    int target;
    int idx;

    skip_spaces(p);
    target = atoi(*p);
    idx = find_line_index(target);
    if (idx < 0) {
        fprintf(stderr, "GOTO target not found: %d\n", target);
        running = 0;
        return;
    }
    pc_index = idx;
    pc_pos = prog.lines[idx]->text;
}

static void
do_gosub(char **p, char *ret_pos)
{
    int target;
    int idx;

    if (gosub_top >= MAX_GOSUB) {
        fprintf(stderr, "GOSUB stack overflow\n");
        running = 0;
        return;
    }
    target = atoi(*p);
    idx = find_line_index(target);
    if (idx < 0) {
        fprintf(stderr, "GOSUB target not found: %d\n", target);
        running = 0;
        return;
    }
    gosub_stack[gosub_top].line_index = pc_index;
    gosub_stack[gosub_top].position = ret_pos;
    gosub_top++;
    pc_index = idx;
    pc_pos = prog.lines[idx]->text;
}

static void
do_return(void)
{
    if (gosub_top <= 0) {
        fprintf(stderr, "RETURN without GOSUB\n");
        running = 0;
        return;
    }
    gosub_top--;
    pc_index = gosub_stack[gosub_top].line_index;
    pc_pos = gosub_stack[gosub_top].position;
    if (!pc_pos) {
        pc_index++;
    }
}

static void
do_for(struct line *ln, char **p, char *start_pos)
{
    int var;
    double start_val;
    double limit;
    double step;
    char *pos_after;

    var = parse_variable(p);
    if (var < 0) {
        fprintf(stderr, "Bad FOR variable in line %d\n", ln->number);
        running = 0;
        return;
    }
    expect_char(p, '=');
    start_val = parse_expression(p);
    if (!match_keyword(p, "TO")) {
        fprintf(stderr, "Missing TO in FOR at line %d\n", ln->number);
        running = 0;
        return;
    }
    limit = parse_expression(p);
    step = 1.0;
    if (match_keyword(p, "STEP")) {
        step = parse_expression(p);
    }
    vars[var] = start_val;
    pos_after = start_pos;
    if (for_top >= MAX_FOR_DEPTH) {
        fprintf(stderr, "FOR stack overflow\n");
        running = 0;
        return;
    }
    for_stack[for_top].var = var;
    for_stack[for_top].limit = limit;
    for_stack[for_top].step = step;
    if (pos_after) {
        for_stack[for_top].line_index = pc_index;
        for_stack[for_top].position = pos_after;
    } else {
        if (pc_index + 1 < prog.count) {
            for_stack[for_top].line_index = pc_index + 1;
            for_stack[for_top].position = prog.lines[pc_index + 1]->text;
        } else {
            for_stack[for_top].line_index = pc_index;
            for_stack[for_top].position = NULL;
        }
    }
    for_top++;
}

static void
do_next(char **p)
{
    int var;
    struct for_frame *fr;
    double v;
    int i;

    var = -1;
    skip_spaces(p);
    if (isalpha((unsigned char)**p)) {
        var = toupper((unsigned char)**p) - 'A';
        (*p)++;
    }
    if (for_top <= 0) {
        fprintf(stderr, "NEXT without FOR\n");
        running = 0;
        return;
    }
    i = for_top - 1;
    if (var >= 0) {
        while (i >= 0 && for_stack[i].var != var) {
            i--;
        }
        if (i < 0) {
            fprintf(stderr, "NEXT variable mismatch\n");
            running = 0;
            return;
        }
    }
    fr = &for_stack[i];
    v = vars[fr->var] + fr->step;
    vars[fr->var] = v;
    if ((fr->step > 0 && v <= fr->limit) || (fr->step < 0 && v >= fr->limit)) {
        pc_index = fr->line_index;
        if (pc_index >= 0 && pc_index < prog.count) {
            if (fr->position) {
                pc_pos = fr->position;
            } else {
                pc_pos = prog.lines[pc_index]->text;
            }
        } else {
            running = 0;
            fprintf(stderr, "FOR resume target out of range\n");
        }
    } else {
        for_top = i;
        pc_pos = next_statement(prog.lines[pc_index]->text);
        if (!pc_pos) {
            pc_index++;
        }
    }
}

static void
do_sleep_cmd(char **p)
{
    double v;
    int ticks;

    v = parse_expression(p);
    ticks = (int)v;
    if (ticks < 0) {
        ticks = 0;
    }
    sleep_ticks(ticks);
}

static void
sleep_ticks(int ticks)
{
    struct timeval tv;
    int sec;
    int usec;

    if (ticks <= 0) {
        return;
    }
    sec = ticks / 60;
    usec = (ticks % 60) * (1000000 / 60);
    tv.tv_sec = sec;
    tv.tv_usec = usec;
#if PDP11
    if (sec > 0) {
        sleep((unsigned int)sec);
    }
    if (usec > 0) {
        struct timeval start;
        struct timeval now;
        long target;
        if (gettimeofday(&start, (struct timezone *)0) == 0) {
            target = start.tv_sec * 1000000L + start.tv_usec + usec;
            for (;;) {
                if (gettimeofday(&now, (struct timezone *)0) != 0) {
                    break;
                }
                if (now.tv_sec * 1000000L + now.tv_usec >= target) {
                    break;
                }
            }
        }
    }
#else
    if (select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0, &tv) < 0) {
        if (sec > 0) {
            sleep((unsigned int)sec);
        }
    }
#endif
}

static int
handle_tab(char **p, int *col)
{
    int target;
    int spaces;
    char *s;

    s = *p;
    skip_spaces(&s);
    if (ci_compare_n(s, "TAB", 3) != 0) {
        return 0;
    }
    s += 3;
    skip_spaces(&s);
    if (*s != '(') {
        return 0;
    }
    s++;
    *p = s;
    target = (int)parse_expression(p);
    expect_char(p, ')');
    if (target < 0) {
        target = 0;
    }
    if (target <= *col) {
        spaces = 1;
    } else {
        spaces = target - *col;
    }
    while (spaces-- > 0) {
        putchar(' ');
        (*col)++;
    }
    return 1;
}
static double
parse_number(char **p)
{
    char *s;
    double value;
    double frac;
    double scale;
    int sign;
    int exp_sign;
    int exponent;

    s = *p;
    skip_spaces(&s);
    sign = 1;
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        sign = -1;
        s++;
    }
    value = 0.0;
    while (isdigit((unsigned char)*s)) {
        value = value * 10.0 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        frac = 0.0;
        scale = 1.0;
        while (isdigit((unsigned char)*s)) {
            frac = frac * 10.0 + (*s - '0');
            scale *= 10.0;
            s++;
        }
        value += frac / scale;
    }
    exponent = 0;
    exp_sign = 1;
    if (*s == 'E' || *s == 'e') {
        s++;
        if (*s == '+') {
            s++;
        } else if (*s == '-') {
            exp_sign = -1;
            s++;
        }
        while (isdigit((unsigned char)*s)) {
            exponent = exponent * 10 + (*s - '0');
            s++;
        }
    }
    if (exponent != 0) {
        double pow10;
        int i;
        pow10 = 1.0;
        for (i = 0; i < exponent; i++) {
            pow10 *= 10.0;
        }
        if (exp_sign < 0) {
            value /= pow10;
        } else {
            value *= pow10;
        }
    }
    *p = s;
    skip_spaces(p);
    return value * (double)sign;
}

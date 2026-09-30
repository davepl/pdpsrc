#define UTIL__C


/* Some generally useful routines */
/* The majority of the non-portable code is in here. */

/*

Copyright (c) 2001, Richard Krehbiel
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "util.h"                      /* own defintions */

#undef getenv
#undef putenv
#ifdef MEMTRACE
#undef malloc
#undef realloc
#undef free
#endif

#ifdef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#define stat _stat
#else
#include <sys/stat.h>
#endif

typedef struct env_override {
    struct env_override *next;
    char           *name;
    char           *value;
} ENV_OVERRIDE;

static ENV_OVERRIDE *env_overrides = NULL;

extern char    *sbrk();

#ifdef MEMTRACE
typedef struct memhdr {
    unsigned        size;
    unsigned        magic;
} MEMHDR;

#define MEMHDR_MAGIC 012345

static long      memtrace_current = 0;
static long      memtrace_peak = 0;
static long      memtrace_allocs = 0;
static long      memtrace_frees = 0;
static int       memtrace_checked = 0;
static int       memtrace_enabled = 0;
static int       memtrace_verbose = 0;

static void memtrace_init(
    void)
{
    char           *mode;

    if (memtrace_checked)
        return;

    memtrace_checked = 1;
    mode = getenv("MACRO11_MEMLOG");
    if (mode && *mode) {
        memtrace_enabled = 1;
        if (strcmp(mode, "verbose") == 0 || strcmp(mode, "2") == 0)
            memtrace_verbose = 1;
    }
}

static void memtrace_report(
    char *what,
    unsigned size,
    char *file,
    int line)
{
    memtrace_init();
    fprintf(stderr,
            "memtrace: %s %u bytes at %s:%d current=%ld peak=%ld allocs=%ld frees=%ld\n",
            what, size, file, line, memtrace_current, memtrace_peak, memtrace_allocs, memtrace_frees);
}

static void memtrace_note_alloc(
    unsigned size,
    char *file,
    int line)
{
    memtrace_init();
    memtrace_current += size;
    memtrace_allocs++;
    if (memtrace_current > memtrace_peak)
        memtrace_peak = memtrace_current;
    if (memtrace_verbose)
        memtrace_report("alloc", size, file, line);
}

static void memtrace_note_free(
    unsigned size,
    char *file,
    int line)
{
    memtrace_init();
    memtrace_current -= size;
    memtrace_frees++;
    if (memtrace_verbose)
        memtrace_report("free", size, file, line);
}

void           *xmalloc(
    unsigned size,
    char *file,
    int line)
{
    MEMHDR         *hdr;

    memtrace_init();
    hdr = malloc(sizeof(MEMHDR) + size);
    if (hdr == NULL) {
        memtrace_report("oom malloc", size, file, line);
        fprintf(stderr, "Out of memory.\n");
        exit(EXIT_FAILURE);
    }

    hdr->size = size;
    hdr->magic = MEMHDR_MAGIC;
    memtrace_note_alloc(size, file, line);
    return hdr + 1;
}

void           *xrealloc(
    void *ptr,
    unsigned size,
    char *file,
    int line)
{
    MEMHDR         *hdr;
    MEMHDR         *newhdr;
    unsigned        oldsize;

    if (ptr == NULL)
        return xmalloc(size, file, line);

    hdr = ((MEMHDR *) ptr) - 1;
    if (hdr->magic != MEMHDR_MAGIC) {
        fprintf(stderr, "memtrace: bad realloc at %s:%d\n", file, line);
        exit(EXIT_FAILURE);
    }

    oldsize = hdr->size;
    newhdr = realloc(hdr, sizeof(MEMHDR) + size);
    if (newhdr == NULL) {
        memtrace_report("oom realloc", size, file, line);
        fprintf(stderr, "memtrace: old block %u bytes\n", oldsize);
        fprintf(stderr, "Out of memory.\n");
        exit(EXIT_FAILURE);
    }

    newhdr->size = size;
    newhdr->magic = MEMHDR_MAGIC;
    memtrace_current += (long) size - (long) oldsize;
    if (memtrace_current > memtrace_peak)
        memtrace_peak = memtrace_current;
    if (memtrace_verbose)
        memtrace_report("realloc", size, file, line);
    return newhdr + 1;
}

void xfree(
    void *ptr,
    char *file,
    int line)
{
    MEMHDR         *hdr;

    if (ptr == NULL)
        return;

    hdr = ((MEMHDR *) ptr) - 1;
    if (hdr->magic != MEMHDR_MAGIC) {
        fprintf(stderr, "memtrace: bad free at %s:%d\n", file, line);
        exit(EXIT_FAILURE);
    }

    memtrace_note_free(hdr->size, file, line);
    hdr->magic = 0;
    free(hdr);
}
#endif

/* Sure, the library typically provides some kind of
    ultoa or _ultoa function.  But since it's merely typical
    and not standard, and since the function is so simple,
    I'll write my own.

    It's significant feature is that it'll produce representations in
    any number base from 2 to 36.
*/

char           *my_ultoa(
    unsigned long val,
    char *buf,
    unsigned int base)
{
    static char     digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char           *strt = buf,
            *end;

    do {
        *buf++ = digits[val % base];
        val /= base;
    } while (val != 0);

    *buf = 0;                          /* delimit */
    end = buf + 1;

    /* Now reverse the bytes */

    while (buf > strt) {
        char            temp;

        temp = *--buf;
        *buf = *strt;
        *strt++ = temp;
    }

    return end;
}

/* Ditto my_ultoa.  This actually emits
    a signed representation in other number bases. */

char           *my_ltoa(
    long val,
    char *buf,
    unsigned int base)
{
    unsigned long   uval;

    if (val < 0)
        uval = -val, *buf++ = '-';
    else
        uval = val;

    return my_ultoa(uval, buf, base);
}

/*
  _searchenv is a function provided by the MSVC library that finds
  files which may be anywhere along a path which appears in an
  environment variable.  I duplicate that function for portability.
  Note also that mine avoids destination buffer overruns.

  Note: uses strtok.  This means it'll screw you up if you
  expect your strtok context to remain intact when you use
  this function.
*/

void my_searchenv(
    char *name,
    char *envname,
    char *hitfile,
    int hitlen)
{
    char           *env;
    char           *envcopy;
    char           *cp;
    char           *concat;
    struct stat     info;

    *hitfile = 0;                      /* Default failure indication */

    /* Note: If the given name is absolute, then don't search the
       path, but use it as is. */

    if (
#ifdef WIN32
           strchr(name, ':') != NULL || /* Contain a drive spec? */
           name[0] == '\\' ||          /* Start with absolute ref? */
#endif
           name[0] == '/') {           /* Start with absolute ref? */
        strncpy(hitfile, name, hitlen); /* Copy to target */
        return;
    }

    env = xgetenv(envname);
    if (env == NULL)
        return;                        /* Variable not defined, no search. */

    envcopy = strdup(env);             /* strtok destroys it's text
                                          argument.  I don't want the return
                                          value from getenv destroyed. */

    cp = strtok(envcopy, PATHSEP);
    while (cp != NULL) {
        concat = malloc(strlen(cp) + strlen(name) + 2);

        if (concat == NULL) {
            free(envcopy);
            return;
        }
        strcpy(concat, cp);
        if (concat[strlen(concat) - 1] != '/')
            strcat(concat, "/");
        strcat(concat, name);
        if (!stat(concat, &info)) {
            /* Copy the file name to hitfile.  Assure that it's really
               zero-delimited. */
            strncpy(hitfile, concat, hitlen - 1);
            hitfile[hitlen - 1] = 0;
            free(concat);
            free(envcopy);
            return;
        }

        free(concat);
        cp = strtok(NULL, PATHSEP);
    }

    /* If I fall out of that loop, then hitfile indicates no match,
       and return. */
    free(envcopy);
}

char           *xgetenv(
    char *name)
{
    ENV_OVERRIDE   *ovr;

    for (ovr = env_overrides; ovr != NULL; ovr = ovr->next)
        if (strcmp(ovr->name, name) == 0)
            return ovr->value;

    return getenv(name);
}

int xputenv(
    char *str)
{
    char           *eq;
    int             namelen;
    ENV_OVERRIDE   *ovr;

    eq = strchr(str, '=');
    if (eq == NULL)
        return 1;

    namelen = eq - str;

    for (ovr = env_overrides; ovr != NULL; ovr = ovr->next)
        if (strncmp(ovr->name, str, namelen) == 0 && ovr->name[namelen] == 0) {
            free(ovr->value);
            ovr->value = memcheck(strdup(eq + 1));
            return 0;
        }

    ovr = memcheck(malloc(sizeof(ENV_OVERRIDE)));
    ovr->name = memcheck(malloc(namelen + 1));
    memcpy(ovr->name, str, namelen);
    ovr->name[namelen] = 0;
    ovr->value = memcheck(strdup(eq + 1));
    ovr->next = env_overrides;
    env_overrides = ovr;

    return 0;
}




/* memcheck - crash out if a pointer (returned from malloc) is NULL. */

void           *memcheck(
    void *ptr)
{
    if (ptr == NULL) {
#ifdef MEMTRACE
        memtrace_report("oom memcheck", 0, "memcheck", 0);
#endif
        fprintf(stderr, "Out of memory.\n");
        exit(EXIT_FAILURE);
    }

    return ptr;
}

char           *xstrdup(
    char *str)
{
    char           *copy;

    if (str == NULL)
        return NULL;

#ifdef MEMTRACE
    copy = xmalloc((unsigned) (strlen(str) + 1), "strdup", 0);
#else
    copy = malloc(strlen(str) + 1);
    copy = memcheck(copy);
#endif
    strcpy(copy, str);

    return copy;
}

int xstrcasecmp(
    char *left,
    char *right)
{
    int             lch;
    int             rch;

    do {
        lch = tolower((unsigned char) *left++);
        rch = tolower((unsigned char) *right++);
    } while (lch != 0 && lch == rch);

    return lch - rch;
}

/* upcase turns a string to upper case */

void upcase(
    char *str)
{
    while (*str) {
        if (*str >= 'a' && *str <= 'z')
            *str += 'A' - 'a';
        str++;
    }
}

/* padto adds blanks to the end of a string until it's the given
   length. */

void padto(
    char *str,
    int to)
{
    int             needspace = to - strlen(str);

    str += strlen(str);
    while (needspace > 0)
        *str++ = ' ', needspace--;
    *str = 0;
}

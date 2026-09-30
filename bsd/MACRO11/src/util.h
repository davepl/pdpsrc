#ifndef UTIL__H
#define UTIL__H

#ifndef __STDC__
#define const
#endif

#ifndef offsetof
#define offsetof(type, member) ((int)&(((type *)0)->member))
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#ifndef SEEK_END
#define SEEK_END 2
#endif

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

#include "stream2.h"

char           *my_ultoa(
    unsigned long val,
    char *buf,
    unsigned int base);
char           *my_ltoa(
    long val,
    char *buf,
    unsigned int base);
void            my_searchenv(
    char *name,
    char *envname,
    char *hitfile,
    int hitlen);
char           *xgetenv(
    char *name);
int             xputenv(
    char *str);
char           *xstrdup(
    char *str);
int             xstrcasecmp(
    char *left,
    char *right);
void           *xmalloc(
    unsigned size,
    char *file,
    int line);
void           *xrealloc(
    void *ptr,
    unsigned size,
    char *file,
    int line);
void            xfree(
    void *ptr,
    char *file,
    int line);

/* Cover a few platform-dependencies */

#ifdef WIN32
#define PATHSEP ";"
#else
#define PATHSEP ":"
#endif

#define getenv xgetenv
#define putenv xputenv
#define strdup xstrdup
#define strcasecmp xstrcasecmp
#define stricmp xstrcasecmp

#ifdef MEMTRACE
#define malloc(sz) xmalloc((unsigned) (sz), __FILE__, __LINE__)
#define realloc(ptr, sz) xrealloc((ptr), (unsigned) (sz), __FILE__, __LINE__)
#define free(ptr) xfree((ptr), __FILE__, __LINE__)
#endif


#define FALSE 0                        /* Everybody needs FALSE and TRUE */
#define TRUE 1


/* EOL says whether a char* is pointing at the end of a line */
#define EOL(c) (!(c) || (c) == '\n' || (c) == ';')

#define SIZEOF_MEMBER(s, m) (sizeof((s *)0)->m)

void            upcase(
    char *str);

void            padto(
    char *str,
    int to);

void           *memcheck(
    void *ptr);


#endif /* UTIL__H */

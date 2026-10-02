#define MACRO11__C


/*
    Assembler compatible with MACRO-11.

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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#ifndef L_tmpnam
#define L_tmpnam 64
#endif

#include "macro11.h"

#include "util.h"

#include "assemble_globals.h"
#include "assemble.h"
#include "assemble_aux.h"
#include "listing.h"
#include "object.h"
#include "symbols.h"

extern int      unlink();



#include "command.h"

int main(argc, argv)
    int argc;
    char *argv[];
{
    struct command_options options;
    FILE           *obj = NULL;
    TEXT_RLD        tr;
    char            tmpobj[L_tmpnam];
    int             convert_ok = 1;
    char           *toolname;
    char           *slash;
    char            obj2bsd_path[128];
    char            cmd[256];
    int             i;
    STACK           stack;
    int             errcount;

    tmpobj[0] = 0;

    memset(&options, 0, sizeof(options));
    parse_options(argc, argv, &options);

    if (options.objname) {
        if (options.bsd_obj) {
            if (tmpnam(tmpobj) == NULL)
                return EXIT_FAILURE;
            obj = fopen(tmpobj, "wb");
        } else {
            obj = fopen(options.objname, "wb");
        }
        if (obj == NULL)
            return EXIT_FAILURE;
    }

    add_symbols(&blank_section);

    text_init(&tr, NULL, 0);

    module_name = memcheck(strdup(""));

    xfer_address = new_ex_lit(1);      /* The undefined transfer address */

    stack_init(&stack);
    /* Push the files onto the input stream in reverse order */
    for (i = options.nr_files - 1; i >= 0; --i) {
        STREAM         *str = new_file_stream(options.fnames[i]);

        if (str == NULL) {
            report(NULL, "Unable to open file %s\n", options.fnames[i]);
            exit(EXIT_FAILURE);
        }
        stack_push(&stack, str);
    }

    DOT = 0;
    current_pc->section = &blank_section;
    last_dot_section = NULL;
    pass = 0;
    stmtno = 0;
    lsb = 0;
    last_lsb = -1;
    last_locsym = 32767;
    last_cond = -1;
    sect_sp = -1;
    suppressed = 0;

    assemble_stack(&stack, &tr);

#if 0
    if (enabl_debug)
        dump_all_macros();
#endif

    assert(stack.top == NULL);

    migrate_implicit();                /* Migrate the implicit globals */
    write_globals(obj);                /* Write the global symbol dictionary */

#if 0
    sym_hist(&symbol_st, "symbol_st"); /* Draw a symbol table histogram */
#endif


    text_init(&tr, obj, 0);

    stack_init(&stack);                /* Superfluous... */
    /* Re-push the files onto the input stream in reverse order */
    for (i = options.nr_files - 1; i >= 0; --i) {
        STREAM         *str = new_file_stream(options.fnames[i]);

        if (str == NULL) {
            report(NULL, "Unable to open file %s\n", options.fnames[i]);
            exit(EXIT_FAILURE);
        }
        stack_push(&stack, str);
    }

    DOT = 0;
    current_pc->section = &blank_section;
    last_dot_section = NULL;

    pass = 1;
    stmtno = 0;
    lsb = 0;
    last_lsb = -1;
    last_locsym = 32767;
    pop_cond(-1);
    sect_sp = -1;
    suppressed = 0;

    errcount = assemble_stack(&stack, &tr);

    text_flush(&tr);

    while (last_cond >= 0) {
        report(NULL, "%s:%d: Unterminated conditional\n", conds[last_cond].file, conds[last_cond].line);
        pop_cond(last_cond - 1);
        errcount++;
    }

    for (i = 0; i < nr_mlbs; i++)
        mlb_close(mlbs[i]);

    write_endmod(obj);

    if (obj != NULL)
        fclose(obj);

    if (options.bsd_obj && options.objname != NULL) {
        obj2bsd_path[0] = 0;
        toolname = argv[0];
        slash = strrchr(toolname, '/');
        if (slash != NULL) {
            memcpy(obj2bsd_path, toolname, slash - toolname + 1);
            obj2bsd_path[slash - toolname + 1] = 0;
            strcat(obj2bsd_path, "obj2bsd");
        } else {
            strcpy(obj2bsd_path, "obj2bsd");
        }

        if (errcount == 0) {
            sprintf(cmd, "%s %s -o %s", obj2bsd_path, tmpobj, options.objname);
            convert_ok = system(cmd) == 0;
        }
        unlink(tmpobj);
        if (!convert_ok)
            return EXIT_FAILURE;
    }

    if (errcount > 0)
        fprintf(stderr, "%d Errors\n", errcount);

    if (lstfile && strcmp(options.lstname, "-") != 0)
        fclose(lstfile);

    return errcount > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

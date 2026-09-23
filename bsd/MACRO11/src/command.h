#ifndef COMMAND_H
#define COMMAND_H

struct command_options {
    char *fnames[32];
    int nr_files;
    char *objname;
    char *lstname;
    int bsd_obj;
};

void parse_options();

#endif

CC=cc
CPPFLAGS=
CFLAGS=-O
LDFLAGS=
LIBM=-lm
PORTCHECK_CFLAGS=-std=c89 -pedantic -Wdeclaration-after-statement
SMALLCPP=-DSMALL_MEMORY

MACRO11_OBJS= \
	macro11.o \
	assemble.o \
	assemble_globals.o \
	assemble_aux.o \
	extree.o \
	listing.o \
	macros.o \
	parse.o \
	rept_irpc.o \
	symbols.o \
	mlb.o \
	object.o \
	stream2.o \
	util.o \
	rad50.o

DUMPOBJ_OBJS= \
	dumpobj.o \
	rad50.o \
	util.o

OBJ2BIN_OBJS= \
	obj2bin.o

OBJ2BSD_OBJS= \
	obj2bsd.o \
	obj2bsd_main.o

all: macro11 dumpobj obj2bin obj2bsd

memtrace:
	make clean
	make CPPFLAGS="${CPPFLAGS} -DMEMTRACE" all

macro11: ${MACRO11_OBJS}
	case "`uname -s 2>/dev/null`" in \
	Darwin) ${CC} ${LDFLAGS} -o macro11 ${MACRO11_OBJS} ${LIBM} ;; \
	*) ${CC} -i ${LDFLAGS} -o macro11 ${MACRO11_OBJS} ${LIBM} ;; \
	esac

macro11.ovl: ${MACRO11_OBJS}
	${CC} -i ${LDFLAGS} -o macro11 \
		macro11.o assemble_globals.o listing.o stream2.o util.o rad50.o \
		-Z assemble.o assemble_aux.o object.o \
		-Z parse.o symbols.o extree.o macros.o rept_irpc.o mlb.o \
		-Y ${LIBM} -lovc

obj2bsd: ${OBJ2BSD_OBJS} obj2bsd.o
	case "`uname -s 2>/dev/null`" in \
	Darwin) ${CC} ${LDFLAGS} -o obj2bsd obj2bsd.o obj2bsd_main.o ;; \
	*) ${CC} -i ${LDFLAGS} -o obj2bsd obj2bsd.o obj2bsd_main.o ;; \
	esac

dumpobj: ${DUMPOBJ_OBJS}
	${CC} ${LDFLAGS} -o dumpobj ${DUMPOBJ_OBJS}

obj2bin: ${OBJ2BIN_OBJS}
	${CC} ${LDFLAGS} -o obj2bin ${OBJ2BIN_OBJS}

smoke: macro11
	./macro11 test.mac

portcheck:
	${MAKE} clean
	${MAKE} CC="${CC}" CPPFLAGS="${CPPFLAGS}" CFLAGS="${CFLAGS} ${PORTCHECK_CFLAGS}" LDFLAGS="${LDFLAGS}" all

clean:
	rm -f ${MACRO11_OBJS} ${DUMPOBJ_OBJS} ${OBJ2BIN_OBJS} ${OBJ2BSD_OBJS} macro11 dumpobj obj2bin obj2bsd

.c.o:
	${CC} ${SMALLCPP} ${CPPFLAGS} ${CFLAGS} -c $<

macro11.o: macro11.c macro11.h rad50.h object.h stream2.h mlb.h util.h
assemble.o: assemble.c assemble.h assemble_globals.h assemble_aux.h util.h mlb.h object.h listing.h parse.h symbols.h extree.h macros.h rept_irpc.h rad50.h
	${CC} ${SMALLCPP} ${CPPFLAGS} -c assemble.c
assemble_globals.o: assemble_globals.c assemble_globals.h object.h
assemble_aux.o: assemble_aux.c util.h assemble_aux.h assemble_globals.h macros.h assemble.h listing.h symbols.h parse.h
extree.o: extree.c extree.h util.h assemble_globals.h object.h
listing.o: listing.c listing.h util.h assemble_globals.h
macros.o: macros.c macros.h util.h assemble_globals.h assemble_aux.h listing.h parse.h stream2.h symbols.h
parse.o: parse.c parse.h util.h rad50.h assemble_globals.h
rept_irpc.o: rept_irpc.c rept_irpc.h util.h assemble_aux.h parse.h listing.h macros.h assemble_globals.h
symbols.o: symbols.c symbols.h util.h assemble_globals.h listing.h
mlb.o: mlb.c rad50.h stream2.h mlb.h macro11.h util.h
object.o: object.c rad50.h object.h macro11.h
obj2bsd.o: obj2bsd.c object.h obj2bsd.h
obj2bsd_main.o: obj2bsd_main.c obj2bsd.h
stream2.o: stream2.c util.h stream2.h
util.o: util.c util.h
rad50.o: rad50.c rad50.h
dumpobj.o: dumpobj.c rad50.h util.h
obj2bin.o: obj2bin.c object.h

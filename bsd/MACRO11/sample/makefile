MACRO11=../src/macro11
OBJ2BIN=../src/obj2bin

OBJ=ATTN-11.obj
BIN=ATTN-11.bin

SOURCES= \
	TRAIN.MAC \
	IO.MAC \
	nn11/FXMATH.MAC \
	nn11/VECOP.MAC \
	nn11/MATOP.MAC \
	nn11/ACTFN.MAC \
	nn11/LAYER.MAC \
	model/FORWRD.MAC \
	model/BKWRD.MAC \
	model/UPDAT.MAC

all: ${BIN}

${BIN}: ${OBJ} ${OBJ2BIN}
	${OBJ2BIN} ${OBJ} --binary --rt11 --outfile=${BIN}

${OBJ}: ${SOURCES} ${MACRO11}
	${MACRO11} TRAIN.MAC -o ${OBJ}

${MACRO11} ${OBJ2BIN}:
	cd ../src && ${MAKE} macro11 obj2bin

clean:
	rm -f ${OBJ} ${BIN}

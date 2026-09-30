/* Minimal stdint.h for PDP-11 builds without a native header. */
#ifndef STDINT_H
#define STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef long               int32_t;
typedef unsigned long      uint32_t;

typedef int                intptr_t;
typedef unsigned int       uintptr_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255U
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535U
#define INT32_MIN  (-2147483647L - 1L)
#define INT32_MAX  2147483647L
#define UINT32_MAX 4294967295UL

#define INTPTR_MAX 32767
#define INTPTR_MIN (-32768)
#define UINTPTR_MAX 65535U

#endif /* STDINT_H */

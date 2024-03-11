// borrowed from https://github.com/riscv/riscv-pk : util/snprintf.c
#ifndef _STD_H
#define _STD_H

#include <stdarg.h>
#include <string.h>
#include "util/types.h"

#ifndef isdigit
#define isdigit(c) (c >= '0' && c <= '9')
#endif

#ifndef isspace
#define isspace(c) (c == ' ' || c == '\f' || c == '\n' || \
                    c == '\r' || c == '\t' || c == '\v')
#endif

/* Flags */
#define F_SKIP   0001   // don't save matched value - '*'
#define F_ALLOC  0002   // Allocate memory for the pointer - 'm'
#define F_SIGNED 0004   // is this a signed number (%d,%i)?

/* Format states */
#define S_DEFAULT   0
#define S_FLAGS     1
#define S_WIDTH     2
#define S_PRECIS    3
#define S_LENGTH    4
#define S_CONV      5

/* Lenght flags */
#define L_CHAR      1
#define L_SHORT     2
#define L_LONG      3
#define L_LLONG     4
#define L_DOUBLE    5

int vsnprintf(char* out, size_t n, const char* s, va_list vl);
int vsnscanf(const char *str, const char *fmt, va_list ap);

#endif
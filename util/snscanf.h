#include <stddef.h>     /* size_t */
#include <stdarg.h>     /* va_args */
#include <string.h>     /* memset, strchr */
#include <assert.h>
#include <ctype.h>

int snscanf(const char *str, const char *fmt, va_list ap);
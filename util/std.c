/*
 * vsnprintf() is borrowed from pk.
 */

#include "util/std.h"

int32 vsnprintf(char* out, size_t n, const char* s, va_list vl) {
  bool format = FALSE;
  bool longarg = FALSE;
  size_t pos = 0;

  for (; *s; s++) {
    if (format) {
      switch (*s) {
        case 'l':
          longarg = TRUE;
          break;
        case 'p':
          longarg = TRUE;
          if (++pos < n) out[pos - 1] = '0';
          if (++pos < n) out[pos - 1] = 'x';
        case 'x': {
          long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
          for (int i = 2 * (longarg ? sizeof(long) : sizeof(int)) - 1; i >= 0; i--) {
            int d = (num >> (4 * i)) & 0xF;
            if (++pos < n) out[pos - 1] = (d < 10 ? '0' + d : 'a' + d - 10);
          }
          longarg = FALSE;
          format = FALSE;
          break;
        }
        case 'd': {
          long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
          if (num < 0) {
            num = -num;
            if (++pos < n) out[pos - 1] = '-';
          }
          long digits = 1;
          for (long nn = num; nn /= 10; digits++)
            ;
          for (int i = digits - 1; i >= 0; i--) {
            if (pos + i + 1 < n) out[pos + i] = '0' + (num % 10);
            num /= 10;
          }
          pos += digits;
          longarg = FALSE;
          format = FALSE;
          break;
        }
        case 's': {
          const char* s2 = va_arg(vl, const char*);
          while (*s2) {
            if (++pos < n) out[pos - 1] = *s2;
            s2++;
          }
          longarg = FALSE;
          format = FALSE;
          break;
        }
        case 'c': {
          if (++pos < n) out[pos - 1] = (char)va_arg(vl, int);
          longarg = FALSE;
          format = FALSE;
          break;
        }
        default:
          break;
      }
    } else if (*s == '%')
      format = TRUE;
    else if (++pos < n)
      out[pos - 1] = *s;
  }
  if (pos < n)
    out[pos] = 0;
  else if (n)
    out[n - 1] = 0;
  return pos;
}

static int valid_sint(char c)
{
    if ((c >= '0' && c <= '9') || (c == '-' || c == '+'))
        return 0;
    return -1;
}

/**
 * Having a string, consumes width chars from it, and return an integer
 * in base base, having sign or not.
 * Will work for base 2, 8, 10, 16
 * For base 16 will skip 0x infront of number.
 * For base 10, if signed, will consider - infront of number.
 * For base 8, should skip 0
 * I should reimplement this using sets. 
 */
static long long get_int(const char **str)
{
    long long n = 0;
    int neg = 0;
    char c;

    for (n = 0; **str; (*str)++)
    {
        c = **str;
        if (neg == 0 && c == '-')
        {
            neg = 1;
            continue;
        }
        if (!isdigit(c))
        {
            if (c < 'A' || c > 'F')
                break;
        }
        n = 10 * n + c - '0';
    }
    if (neg && n > 0)
        n = -n;

    return n;
}

/**
 * Gets a string from str and puts it into ptr, if skip is not set
 * if ptr is NULL, it will consume the string, but not save it
 * if width is set, it will stop after max width characters.
 * if set[256] is set, it will only accept characters from the set,
 * eg: set['a'] = 1 - it will accept 'a'
 * otherwise it will stop on first space, or end of string.
 * Returns the number of characters matched
 */
static int get_str(const char **str, char *ptr, char *set, int width)
{
    int n, w, skip;
    unsigned char c;
    w = (width > 0);
    skip = (ptr == NULL);

    for (n = 0; **str; (*str)++, n++) {
        c = **str;
        if ((w && width-- == 0) || (!set && isspace(c)))
            break;
        if (set && (set[c] == 0))
            break;
        if (!skip)
            *ptr++ = c;
    }
    if (!skip)
        *ptr = 0;

    return n;
}

/**
 * Shrinked down, vsscanf implementation.
 *  This will not handle floating numbers (yet), nor allocated (gnu) pointers.
 */
int vsnscanf(const char *str, const char *fmt, va_list ap)
{
    size_t n = 0; // number of matched input items
    char state = S_DEFAULT;
    void *ptr;
    long long num;
    int base, sign, flags, width = 0, lflags;

    if (!fmt)
        return 0;

    for (; *fmt && *str; fmt++)
    {
        if (state == S_DEFAULT)
        {
            if (*fmt == '%')
            {
                flags = 0;
                state = S_CONV;
            }
            else if (isspace(*fmt))
            {
                while (isspace(*str))
                    str++;
            }
            else
            {
                if (*fmt != *str++)
                    break;
            }
            continue;
        }
        if (state == S_CONV)
        {
            if (strchr("di", *fmt))
            {
                state = S_DEFAULT;

                /* Numbers should skip starting spaces "  123l",
                 *  strings, chars not
                 */
                while (isspace(*str))
                    str++;

                if (valid_sint(*str) < 0)
                    break;

                num = get_int(&str);
                if (flags & F_SKIP)
                {
                    continue;
                }
                ptr = va_arg(ap, void *);
                *(int *)ptr = num;
                n++;
            }
            else if ('c' == *fmt)
            {
                state = S_DEFAULT;
                if (flags & F_SKIP)
                {
                    str++;
                    continue;
                }
                ptr = va_arg(ap, void *);
                *(char *)ptr = *(str)++;
                n++;
            }
            else if ('s' == *fmt)
            {
                state = S_DEFAULT;
                if (flags & F_SKIP)
                {
                    get_str(&str, NULL, NULL, width);
                    continue;
                }
                ptr = va_arg(ap, void *);
                get_str(&str, (char *)ptr, NULL, width);
                n++;
            }
            else if ('%' == *fmt)
            {
                state = S_DEFAULT;
                if (*str != '%')
                    break;
                str++;
            }
            else
            {
                break;
            }
        }
    }

    return n;
}
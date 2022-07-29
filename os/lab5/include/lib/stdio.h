#ifndef __STDIO_H__
#define __STDIO_H__
#include <stdarg.h>
#include <syscall.h>

/* get_char() in scanf.c:57 */
#define put_char(c) (syscall(NR_char, (uint64_t) (c)))
#define puts(str) do { \
    for (char *cp = str; *cp; cp++) put_char(*cp); \
} while (0)

int printf(const char* fmt, ...);
int scanf(const char *format_str, ...);

#endif

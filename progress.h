#ifndef GRUB_SET_PROGRESS_H
#define GRUB_SET_PROGRESS_H
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

/* Emit diagnostics immediately, including when output is redirected. */
static inline void grub_progress(const char *format, ...)
{
    int saved_errno = errno;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
    errno = saved_errno;
}
#endif

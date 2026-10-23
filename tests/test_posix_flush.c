/* Compile the actual helper on Linux with Apple's disk-cache command enabled. */
#include "posix_flush.h"
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

static int sync_error, cache_error, sync_calls, cache_calls;
static int sync_interrupts, cache_interrupts, synced;
int mock_fsync(int fd)
{
    assert(fd == 42); sync_calls++;
    if (sync_interrupts) { sync_interrupts--; errno = EINTR; return -1; }
    if (sync_error) { errno = sync_error; return -1; }
    synced = 1;
    return 0;
}
int mock_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    assert(fd == 42 && command == DKIOCSYNCHRONIZECACHE);
    assert(synced); cache_calls++;
    va_start(args, command);
    assert(va_arg(args, void *) == NULL);
    va_end(args);
    if (cache_interrupts) { cache_interrupts--; errno = EINTR; return -1; }
    if (cache_error) { errno = cache_error; return -1; }
    return 0;
}
static void check(int raw, int sync_failure, int cache_failure,
                  int sync_signals, int cache_signals, int success)
{
    sync_error = sync_failure; cache_error = cache_failure;
    sync_interrupts = sync_signals; cache_interrupts = cache_signals;
    sync_calls = cache_calls = synced = 0;
    assert(grub_posix_flush(42, raw) == success);
    assert(sync_calls == 1 + sync_signals);
    assert(cache_calls == (raw && !sync_failure ? 1 + cache_signals : 0));
    if (!success) assert(errno == (sync_failure ? sync_failure : cache_failure));
}
int main(void)
{
    check(1, 0, 0, 0, 0, 1);
    check(1, 0, ENOTTY, 0, 0, 0);
    check(1, 0, EINVAL, 0, 0, 0);
    check(1, 0, ENOTSUP, 0, 0, 0);
    check(1, 0, EIO, 0, 0, 0);
    check(1, 0, EACCES, 0, 0, 0);
    check(1, ENOSPC, 0, 0, 0, 0);
    check(1, EIO, ENOTTY, 0, 0, 0);
    check(0, 0, EIO, 0, 0, 1);
    check(0, EIO, 0, 0, 0, 0);
    check(1, 0, 0, 2, 3, 1);
    check(1, 0, EIO, 1, 2, 0);
    check(1, ENOSPC, 0, 1, 0, 0);
    check(0, 0, EIO, 2, 0, 1);
    puts("Passed disk-cache flush regression: fsync then disk ioctl, fatal unsupported/I/O errors, EINTR retries, preserved errno and no ioctl for images.");
    return 0;
}

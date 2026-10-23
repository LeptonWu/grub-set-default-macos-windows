#define _DARWIN_C_SOURCE
#include "posix_flush.h"
#include <unistd.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/disk.h>
#endif
#include <errno.h>

int grub_posix_flush(int fd, int raw_device)
{
    int status;
    do { status = fsync(fd); } while (status < 0 && errno == EINTR);
    if (status) return 0;
    if (!raw_device) return 1;
#ifdef DKIOCSYNCHRONIZECACHE
    /* Raw disk descriptors bypass filesystem F_FULLFSYNC handling. Ask the
     * disk driver to synchronize its media cache, with no options/arguments. */
    do { status = ioctl(fd, DKIOCSYNCHRONIZECACHE, (void *)0); }
    while (status < 0 && errno == EINTR);
    return status == 0;
#else
    errno = ENOTSUP;
    return 0;
#endif
}

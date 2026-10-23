#ifndef GRUB_SET_POSIX_FLUSH_H
#define GRUB_SET_POSIX_FLUSH_H
/* Mandatory fsync followed by DKIOCSYNCHRONIZECACHE for macOS raw devices.
 * Image files only need fsync. Preserves errno on failure, including unsupported
 * device-cache flushes, so the caller retains its backup and reports failure. */
int grub_posix_flush(int fd, int raw_device);
#endif

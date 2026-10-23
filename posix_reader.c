/* Read-only libext2fs adapter, with aligned raw reads on macOS. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/disk.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "posix_reader.h"
struct reader { int fd; uint64_t size; unsigned int sector; };
static errcode_t open_reader(const char *path, int flags, io_channel *out)
{
    io_channel channel;
    struct reader *r;
    struct stat st;
    int fd;
    if (flags & IO_FLAG_RW) return EROFS;
    fd = open(path, O_RDONLY);
    if (fd < 0) return errno;
    channel = calloc(1, sizeof(*channel));
    r = calloc(1, sizeof(*r));
    if (!channel || !r) { free(channel); free(r); close(fd); return ENOMEM; }
    r->fd = fd; r->sector = 1;
    if (fstat(fd, &st)) goto fail;
    if (S_ISREG(st.st_mode) && st.st_size > 0) r->size = (uint64_t)st.st_size;
#ifdef __APPLE__
    else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        uint32_t sector;
        uint64_t blocks;
        if (ioctl(fd, DKIOCGETBLOCKSIZE, &sector) || ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) ||
            !sector || sector > 65536 || (sector & (sector - 1)) || blocks > INT64_MAX / sector)
            goto fail;
        r->sector = sector; r->size = blocks * sector;
    }
#endif
    else goto fail;
    channel->name = strdup(path);
    if (!channel->name) goto fail;
    channel->magic = EXT2_ET_MAGIC_IO_CHANNEL; channel->manager = grub_posix_reader;
    channel->block_size = 1024; channel->refcount = 1; channel->private_data = r;
    *out = channel;
    return 0;
fail:
    close(fd); free(r); free(channel); return EIO;
}
static errcode_t close_reader(io_channel c)
{
    struct reader *r = c->private_data;
    if (--c->refcount) return 0;
    close(r->fd); free(r); free(c->name); free(c); return 0;
}
static errcode_t block_size(io_channel c, int size)
{
    if (size < 1 || size > 65536) return EINVAL;
    c->block_size = size; return 0;
}
static errcode_t read64(io_channel c, unsigned long long block, int count, void *data)
{
    struct reader *r = c->private_data;
    uint64_t offset, left;
    unsigned char *bounce, *dest = data;
    if (block > INT64_MAX / (unsigned)c->block_size) return EINVAL;
    offset = block * (unsigned)c->block_size;
    left = count < 0 ? (uint64_t)-(int64_t)count : (uint64_t)count * (unsigned)c->block_size;
    if (offset > r->size || left > r->size - offset) return EXT2_ET_SHORT_READ;
    if (posix_memalign((void **)&bounce, 65536, 65536)) return ENOMEM;
    while (left) {
        uint64_t start = offset - offset % r->sector;
        size_t skip = (size_t)(offset - start);
        size_t copy = left < 65536 - skip ? (size_t)left : 65536 - skip;
        size_t request = (skip + copy + r->sector - 1) / r->sector * r->sector;
        ssize_t got;
        if (request > r->size - start) { free(bounce); return EXT2_ET_SHORT_READ; }
        do { got = pread(r->fd, bounce, request, (off_t)start); } while (got < 0 && errno == EINTR);
        if (got < 0 || (size_t)got != request) { free(bounce); return EIO; }
        memcpy(dest, bounce + skip, copy); dest += copy; offset += copy; left -= copy;
    }
    free(bounce); return 0;
}
static errcode_t read32(io_channel c, unsigned long block, int n, void *data)
{ return read64(c, block, n, data); }
static errcode_t write32(io_channel c, unsigned long b, int n, const void *d)
{ (void)c; (void)b; (void)n; (void)d; return EROFS; }
static errcode_t write64(io_channel c, unsigned long long b, int n, const void *d)
{ (void)c; (void)b; (void)n; (void)d; return EROFS; }
static errcode_t flush(io_channel c) { (void)c; return 0; }
static errcode_t option(io_channel c, const char *n, const char *v)
{ (void)c; (void)n; (void)v; return EXT2_ET_INVALID_ARGUMENT; }
static struct struct_io_manager manager = {
    .magic = EXT2_ET_MAGIC_IO_MANAGER, .name = "GRUB read-only POSIX I/O",
    .open = open_reader, .close = close_reader, .set_blksize = block_size,
    .read_blk = read32, .write_blk = write32, .flush = flush, .set_option = option,
    .read_blk64 = read64, .write_blk64 = write64,
};
io_manager grub_posix_reader = &manager;

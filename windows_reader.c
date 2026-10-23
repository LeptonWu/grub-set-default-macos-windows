/* Read-only Win32 adapter for libext2fs. Raw reads are sector-aligned even
 * when the library requests e.g. the 1024-byte superblock on a 4Kn disk. */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <malloc.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "windows_reader.h"

struct reader {
    HANDLE handle;
    DWORD sector_size;
    uint64_t length;
    int raw;
};

static errcode_t reader_close(io_channel channel)
{
    struct reader *reader = channel->private_data;
    if (--channel->refcount) return 0;
    CloseHandle(reader->handle);
    free(reader);
    free(channel->name);
    free(channel);
    return 0;
}

static errcode_t reader_set_block_size(io_channel channel, int size)
{
    if (size <= 0 || size > 65536) return EINVAL;
    channel->block_size = size;
    return 0;
}

static errcode_t reader_read64(io_channel channel, unsigned long long block,
                              int count, void *destination)
{
    struct reader *reader = channel->private_data;
    unsigned char *target = destination, *bounce;
    uint64_t offset, remaining;
    const DWORD capacity = 65536;
    DWORD sector = reader->sector_size;
    if (block > INT64_MAX / (unsigned int)channel->block_size) return EINVAL;
    offset = block * (unsigned int)channel->block_size;
    remaining = count < 0 ? (uint64_t)-(int64_t)count :
                           (uint64_t)count * (unsigned int)channel->block_size;
    if (offset > reader->length || remaining > reader->length - offset) return EXT2_ET_SHORT_READ;
    if (!remaining) return 0;
    bounce = _aligned_malloc(capacity, 65536);
    if (!bounce) return ENOMEM;
    while (remaining) {
        uint64_t start = offset - offset % sector;
        DWORD skip = (DWORD)(offset - start);
        DWORD copy = remaining < capacity - skip ? (DWORD)remaining : capacity - skip;
        DWORD request = skip + copy;
        DWORD actual = 0;
        LARGE_INTEGER position;
        if (reader->raw) request = (request + sector - 1) / sector * sector;
        position.QuadPart = (LONGLONG)start;
        if (start > reader->length || request > reader->length - start ||
            !SetFilePointerEx(reader->handle, position, NULL, FILE_BEGIN) ||
            !ReadFile(reader->handle, bounce, request, &actual, NULL) || actual != request) {
            _aligned_free(bounce);
            return EIO;
        }
        memcpy(target, bounce + skip, copy);
        target += copy;
        offset += copy;
        remaining -= copy;
    }
    _aligned_free(bounce);
    return 0;
}

static errcode_t reader_read(io_channel channel, unsigned long block, int count, void *data)
{
    return reader_read64(channel, block, count, data);
}

static errcode_t reader_write(io_channel channel, unsigned long block, int count,
                             const void *data)
{
    (void)channel; (void)block; (void)count; (void)data;
    return EROFS;
}

static errcode_t reader_write64(io_channel channel, unsigned long long block, int count,
                               const void *data)
{
    (void)channel; (void)block; (void)count; (void)data;
    return EROFS;
}

static errcode_t reader_flush(io_channel channel)
{
    (void)channel;
    return 0;
}

/* ext2fs_open2 with explicit empty options still requires this callback.
 * No nonempty options are supported: in particular, never accept an offset. */
static errcode_t reader_set_option(io_channel channel, const char *option,
                                   const char *argument)
{
    (void)channel; (void)option; (void)argument;
    return EXT2_ET_INVALID_ARGUMENT;
}

static errcode_t reader_open(const char *name, int flags, io_channel *result)
{
    io_channel channel;
    struct reader *reader;
    DISK_GEOMETRY geometry;
    GET_LENGTH_INFORMATION length;
    LARGE_INTEGER file_size;
    DWORD received;
    if (flags & IO_FLAG_RW) return EROFS;
    channel = calloc(1, sizeof(*channel));
    reader = calloc(1, sizeof(*reader));
    if (!channel || !reader) { free(channel); free(reader); return ENOMEM; }
    reader->raw = !strncmp(name, "\\\\?\\GLOBALROOT\\Device\\", 22);
    reader->handle = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING,
                                reader->raw ? FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, NULL);
    if (reader->handle == INVALID_HANDLE_VALUE) {
        free(channel); free(reader); return EACCES;
    }
    if (reader->raw) {
        if (!DeviceIoControl(reader->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                             &geometry, sizeof(geometry), &received, NULL) ||
            received < sizeof(geometry) ||
            !geometry.BytesPerSector || geometry.BytesPerSector > 65536 ||
            (geometry.BytesPerSector & (geometry.BytesPerSector - 1)) ||
            !DeviceIoControl(reader->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                             &length, sizeof(length), &received, NULL) ||
            received < sizeof(length) || length.Length.QuadPart <= 0)
            goto fail;
        reader->sector_size = geometry.BytesPerSector;
        reader->length = (uint64_t)length.Length.QuadPart;
    } else {
        if (!GetFileSizeEx(reader->handle, &file_size) || file_size.QuadPart <= 0) goto fail;
        reader->sector_size = 1;
        reader->length = (uint64_t)file_size.QuadPart;
    }
    channel->name = malloc(strlen(name) + 1);
    if (!channel->name) goto fail;
    strcpy(channel->name, name);
    channel->magic = EXT2_ET_MAGIC_IO_CHANNEL;
    channel->manager = grub_windows_reader;
    channel->block_size = 1024;
    channel->refcount = 1;
    channel->private_data = reader;
    if (reader->raw) channel->flags = CHANNEL_FLAGS_BLOCK_DEVICE;
    *result = channel;
    return 0;
fail:
    CloseHandle(reader->handle);
    free(reader); free(channel);
    return EIO;
}

static struct struct_io_manager manager = {
    .magic = EXT2_ET_MAGIC_IO_MANAGER,
    .name = "GRUB switch read-only Win32 I/O",
    .open = reader_open,
    .close = reader_close,
    .set_blksize = reader_set_block_size,
    .read_blk = reader_read,
    .write_blk = reader_write,
    .flush = reader_flush,
    .set_option = reader_set_option,
    .read_blk64 = reader_read64,
    .write_blk64 = reader_write64,
};

io_manager grub_windows_reader = &manager;

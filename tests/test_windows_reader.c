/* Compile the actual Windows reader on Linux with a strict fake Win32 disk.
 * ReadFile rejects any raw read with unaligned buffer, offset or length. */
#define _POSIX_C_SOURCE 200112L
#include "windows.h"
#include "winioctl.h"
#include "windows_reader.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fake_handle { uint64_t offset; int raw; };
static DWORD sector_size;
static const uint64_t disk_length = UINT64_C(8) * 1024 * 1024 * 1024 * 1024;
static unsigned int reads;

HANDLE CreateFileA(const char *name, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template)
{
    struct fake_handle *handle = calloc(1, sizeof(*handle));
    (void)name; (void)share; (void)security; (void)disposition; (void)template;
    assert(access == GENERIC_READ);
    handle->raw = flags == FILE_FLAG_NO_BUFFERING;
    return handle;
}
BOOL CloseHandle(HANDLE handle) { free(handle); return 1; }
BOOL SetFilePointerEx(HANDLE pointer, LARGE_INTEGER position, LARGE_INTEGER *end, DWORD method)
{
    struct fake_handle *handle = pointer;
    (void)end; assert(method == FILE_BEGIN && position.QuadPart >= 0);
    handle->offset = (uint64_t)position.QuadPart;
    return 1;
}
BOOL ReadFile(HANDLE pointer, void *buffer, DWORD length, DWORD *actual, void *overlapped)
{
    struct fake_handle *handle = pointer;
    unsigned char *bytes = buffer;
    DWORD i;
    (void)overlapped;
    if (handle->raw && ((uintptr_t)buffer % sector_size ||
                        handle->offset % sector_size || length % sector_size)) return 0;
    if (handle->offset > disk_length || length > disk_length - handle->offset) return 0;
    for (i = 0; i < length; i++) bytes[i] = (unsigned char)((handle->offset + i) % 251);
    *actual = length;
    handle->offset += length;
    reads++;
    return 1;
}
BOOL GetFileSizeEx(HANDLE handle, LARGE_INTEGER *size)
{
    (void)handle; size->QuadPart = (LONGLONG)disk_length; return 1;
}
BOOL DeviceIoControl(HANDLE handle, DWORD operation, void *in, DWORD in_size,
                      void *out, DWORD out_size, DWORD *actual, void *overlapped)
{
    (void)handle; (void)in; (void)in_size; (void)overlapped;
    if (operation == IOCTL_DISK_GET_DRIVE_GEOMETRY) {
        assert(out_size == sizeof(DISK_GEOMETRY));
        ((DISK_GEOMETRY *)out)->BytesPerSector = sector_size;
    } else {
        assert(operation == IOCTL_DISK_GET_LENGTH_INFO);
        assert(out_size == sizeof(GET_LENGTH_INFORMATION));
        ((GET_LENGTH_INFORMATION *)out)->Length.QuadPart = (LONGLONG)disk_length;
    }
    *actual = out_size;
    return 1;
}
void *_aligned_malloc(size_t size, size_t alignment)
{
    void *memory = NULL;
    assert(!posix_memalign(&memory, alignment, size));
    return memory;
}
void _aligned_free(void *memory) { free(memory); }

static void verify(io_channel channel, uint64_t block, int count)
{
    size_t size = count < 0 ? (size_t)-(int64_t)count : (size_t)count * channel->block_size;
    uint64_t offset = block * channel->block_size;
    unsigned char *allocation = malloc(size + 1), *buffer = allocation + 1;
    size_t i;
    assert(!channel->manager->read_blk64(channel, block, count, buffer));
    for (i = 0; i < size; i++) assert(buffer[i] == (unsigned char)((offset + i) % 251));
    free(allocation);
}

int main(void)
{
    const char *paths[] = {"\\\\?\\GLOBALROOT\\Device\\Harddisk0\\Partition3", "fixture.img"};
    const DWORD sectors[] = {512, 4096};
    size_t i, j;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        for (j = 0; j < sizeof(sectors) / sizeof(sectors[0]); j++) {
            io_channel channel = NULL;
            unsigned char buffer[1];
            sector_size = sectors[j];
            assert(grub_windows_reader->open(paths[i], IO_FLAG_RW, &channel) == EROFS);
            assert(!channel);
            assert(!grub_windows_reader->open(paths[i], 0, &channel));
            /* Use the real libext2fs option dispatcher used by ext2fs_open2. */
            assert(!io_channel_set_options(channel, ""));
            assert(io_channel_set_options(channel, "offset=1024") == EXT2_ET_INVALID_ARGUMENT);
            verify(channel, 1, 1); /* Superblock at byte 1024, including on 4Kn. */
            verify(channel, 3, -150000); /* Unaligned start; crosses multiple chunks. */
            verify(channel, (UINT64_C(1) << 32) + 7, 1); /* Offset above 4 TiB. */
            assert(!channel->manager->set_blksize(channel, 4096));
            verify(channel, 2099, 1);
            assert(channel->manager->read_blk64(channel, UINT64_MAX, 1, buffer) == EINVAL);
            assert(channel->manager->write_blk(channel, 0, 1, buffer) == EROFS);
            assert(channel->manager->write_blk64(channel, 0, 1, buffer) == EROFS);
            assert(!channel->manager->close(channel));
        }
    }
    assert(reads >= 16);
    puts("Passed actual Windows-reader code with strict 512-byte/4Kn alignment mocks, unaligned buffers, chunked reads, 64-bit offsets and refused writes.");
    return 0;
}

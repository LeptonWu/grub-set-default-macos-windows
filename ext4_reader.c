#include "ext4_reader.h"
#include "grubenv.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ext2fs/ext2fs.h>
#ifdef _WIN32
#include "windows_reader.h"
#elif defined(__APPLE__) || defined(GRUB_USE_POSIX_READER)
#include "posix_reader.h"
#endif

static errcode_t open_readonly_fs(const char *device, ext2_filsys *ret_fs,
                                  const char **message)
{
    ext2_filsys fs;
    errcode_t code;
    /* No EXT2_FLAG_RW, FORCE, or IGNORE_CSUM_ERRORS: this never writes ext4. */
    /* Explicit empty options preserve literal '?' in Windows GLOBALROOT paths.
     * ext2fs_open() otherwise splits the device name at that character. */
    code = ext2fs_open2(device, "", EXT2_FLAG_64BITS, 0, 0,
#ifdef _WIN32
                       grub_windows_reader,
#elif defined(__APPLE__) || defined(GRUB_USE_POSIX_READER)
                       grub_posix_reader,
#else
                       default_io_manager,
#endif
                       ret_fs);
    if (code) return code;
    fs = *ret_fs;
    if (!(fs->super->s_state & EXT2_VALID_FS) ||
        (fs->super->s_state & EXT2_ERROR_FS) ||
        (fs->super->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER)) {
        *message = "The filesystem is not clean or needs journal recovery.";
        return 0;
    }
    if (!ext2fs_superblock_csum_verify(fs, fs->super)) {
        *message = "Superblock checksum verification failed.";
        return 0;
    }
    return 0;
}

int grub_locate_at(const char *device, const char *path, struct grub_location *location,
                char *error, size_t error_size)
{
    ext2_filsys fs = NULL;
    ext2_file_t file = NULL;
    struct ext2_inode inode;
    ext2_ino_t ino;
    blk64_t physical;
    int map_flags = 0, ok = 0;
    unsigned int got = 0;
    unsigned char contents[GRUB_ENV_SIZE];
    errcode_t code;
    const char *operation = "open filesystem";
    const char *message = NULL;

    memset(location, 0, sizeof(*location));
    code = open_readonly_fs(device, &fs, &message);
    if (code || message) goto out;
    operation = "resolve /boot/grub/grubenv";
    code = ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, &ino);
    if (code) goto out;
    operation = "read grubenv inode";
    code = ext2fs_read_inode(fs, ino, &inode);
    if (code) goto out;
    if ((inode.i_mode & LINUX_S_IFMT) != LINUX_S_IFREG ||
        EXT2_I_SIZE(&inode) != GRUB_ENV_SIZE) {
        message = "Expected a regular 1024-byte /boot/grub/grubenv file.";
        goto out;
    }
    /* Encryption, verity and inline data require a different write strategy. */
    if (inode.i_flags & (0x800U | 0x100000U | 0x10000000U)) {
        message = "Encrypted, verity-protected and inline grubenv files are unsupported.";
        goto out;
    }
    if (fs->blocksize < GRUB_ENV_SIZE || fs->blocksize > 65536 ||
        (fs->blocksize & (fs->blocksize - 1))) {
        message = "Unsupported filesystem block size.";
        goto out;
    }
    operation = "map grubenv block";
    code = ext2fs_bmap2(fs, ino, &inode, NULL, 0, 0, &map_flags, &physical);
    if (code) goto out;
    if (!physical || map_flags || physical >= ext2fs_blocks_count(fs->super) ||
        physical > INT64_MAX / fs->blocksize) {
        message = "Invalid, sparse or unwritten grubenv block mapping.";
        goto out;
    }
    location->block = malloc(fs->blocksize);
    if (!location->block) { message = "Out of memory."; goto out; }
    operation = "read raw grubenv block";
    code = io_channel_read_blk64(fs->io, physical, 1, location->block);
    if (code) goto out;
    operation = "open grubenv by inode";
    code = ext2fs_file_open(fs, ino, 0, &file);
    if (code) goto out;
    operation = "read grubenv by inode";
    code = ext2fs_file_read(file, contents, sizeof(contents), &got);
    if (code) goto out;
    if (got != sizeof(contents) || memcmp(contents, location->block, sizeof(contents))) {
        message = "The raw block does not match grubenv read by inode.";
        goto out;
    }
    location->block_size = fs->blocksize;
    location->block_number = physical;
    location->byte_offset = physical * fs->blocksize;
    memcpy(location->filesystem_uuid, fs->super->s_uuid, 16);
    ok = 1;
out:
    if (file) ext2fs_file_close(file);
    if (fs) ext2fs_close_free(&fs);
    if (!ok) {
        if (message) snprintf(error, error_size, "%s", message);
        else snprintf(error, error_size, "Could not %s (libext2fs error %ld).", operation, (long)code);
        grub_location_free(location);
    }
    return ok;
}

void grub_location_free(struct grub_location *location)
{
    free(location->block);
    memset(location, 0, sizeof(*location));
}

int grub_locate(const char *device, struct grub_location *location, char *error, size_t size)
{
    return grub_locate_at(device, "/boot/grub/grubenv", location, error, size);
}

int grub_read_file(const char *device, const char *path, size_t limit,
                   unsigned char **data, size_t *size, char *error, size_t error_size)
{
    ext2_filsys fs = NULL;
    ext2_file_t file = NULL;
    struct ext2_inode inode;
    ext2_ino_t ino;
    unsigned char *buffer = NULL;
    const char *message = NULL;
    errcode_t code;
    unsigned int got = 0;
    uint64_t length;
    int ok = 0;
    *data = NULL; *size = 0;
    code = open_readonly_fs(device, &fs, &message);
    if (code || message) goto out;
    code = ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, &ino);
    if (code) goto out;
    code = ext2fs_read_inode(fs, ino, &inode);
    if (code) goto out;
    length = EXT2_I_SIZE(&inode);
    if ((inode.i_mode & LINUX_S_IFMT) != LINUX_S_IFREG ||
        (inode.i_flags & 0x800U) || length > limit || length > UINT32_MAX) {
        message = "Expected a regular, unencrypted configuration file within the size limit.";
        goto out;
    }
    buffer = malloc((size_t)length + 1);
    if (!buffer) { message = "Out of memory."; goto out; }
    code = ext2fs_file_open(fs, ino, 0, &file);
    if (code) goto out;
    code = ext2fs_file_read(file, buffer, (unsigned int)length, &got);
    if (code) goto out;
    if (got != length) { message = "Short configuration file read."; goto out; }
    buffer[got] = 0;
    *data = buffer; *size = got; buffer = NULL; ok = 1;
out:
    if (file) ext2fs_file_close(file);
    if (fs) ext2fs_close_free(&fs);
    free(buffer);
    if (!ok) {
        if (message) snprintf(error, error_size, "%s: %s", path, message);
        else snprintf(error, error_size, "Could not read %s (libext2fs error %ld).", path, (long)code);
    }
    return ok;
}

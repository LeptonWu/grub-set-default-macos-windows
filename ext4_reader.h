#ifndef GRUB_SWITCH_EXT4_READER_H
#define GRUB_SWITCH_EXT4_READER_H

#include <stddef.h>
#include <stdint.h>

struct grub_location {
    uint32_t block_size;
    uint64_t block_number;
    uint64_t byte_offset;
    unsigned char filesystem_uuid[16];
    unsigned char *block;
};

/* Read-only libext2fs traversal, with metadata checksum verification enabled. */
int grub_locate(const char *device, struct grub_location *location,
                char *error, size_t error_size);
int grub_locate_at(const char *device, const char *path, struct grub_location *location,
                   char *error, size_t error_size);
int grub_read_file(const char *device, const char *path, size_t limit,
                   unsigned char **data, size_t *size, char *error, size_t error_size);
void grub_location_free(struct grub_location *location);

#endif

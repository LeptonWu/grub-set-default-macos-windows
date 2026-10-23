#ifndef GRUB_SET_MACOS_DISKS_H
#define GRUB_SET_MACOS_DISKS_H
#include <CoreFoundation/CoreFoundation.h>
#include <stddef.h>
struct grub_macos_partitions {
    char **devices;
    size_t count;
    size_t physical_disks;
};
int grub_macos_linux_type(const char *content);
/* Consume only direct Linux GPT partitions of physical whole disks from
 * diskutil list -plist physical. Caller frees the list even after failure. */
int grub_macos_collect_partitions(CFDictionaryRef plist, struct grub_macos_partitions *list,
                                  char *error, size_t error_size);
void grub_macos_free_partitions(struct grub_macos_partitions *list);
#endif

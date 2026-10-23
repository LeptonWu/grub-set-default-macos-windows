#ifndef GRUB_SWITCH_DISCOVERY_H
#define GRUB_SWITCH_DISCOVERY_H
#include <windows.h>
#include <stddef.h>
#include "grubenv.h"

struct grub_candidate {
    unsigned int disk;
    unsigned int partition;
    GUID guid;
    struct grub_change change;
};

struct grub_scan_filter {
    const char *path;
    int config_only;
    int have_disk, have_partition, have_guid;
    unsigned int disk, partition;
    GUID guid;
};

/* Read-only enumeration. Caller frees *items, even on a failed/partial scan. */
int grub_discover(const struct grub_scan_filter *filter,
                  struct grub_candidate **items, size_t *count);

#endif

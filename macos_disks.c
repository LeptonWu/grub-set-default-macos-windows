#include "macos_disks.h"
#include "progress.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int grub_macos_linux_type(const char *content)
{
    const char *types[] = {"Linux Filesystem", "0FC63DAF-8483-4772-8E79-3D69D8477DE4",
        "4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709", "44479540-F297-41B2-9AF7-D131D5F0458A",
        "B921B045-1DF0-41C3-AF44-4C6F280D3FAE", "BC13C2FF-59E6-4262-A352-B275FD6F7172"};
    size_t i;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (!strcasecmp(content, types[i])) return 1;
    return 0;
}
static int string_property(CFDictionaryRef dict, CFStringRef key, char *out, size_t size)
{
    CFTypeRef value = CFDictionaryGetValue(dict, key);
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString(value, out, (CFIndex)size, kCFStringEncodingUTF8);
}
static int whole_disk(const char *id)
{
    const char *p;
    if (strlen(id) <= 4 || strncmp(id, "disk", 4)) return 0;
    p = id + 4;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    return !*p;
}
static int direct_partition(const char *disk, const char *part)
{
    size_t size = strlen(disk);
    const char *p;
    if (strncmp(part, disk, size) || part[size] != 's') return 0;
    p = part + size + 1;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    return !*p;
}
static int add_partition(struct grub_macos_partitions *list, const char *id)
{
    char path[128], **grown, *copy;
    size_t i;
    if (snprintf(path, sizeof(path), "/dev/r%s", id) >= (int)sizeof(path)) return 0;
    for (i = 0; i < list->count; i++) if (!strcmp(list->devices[i], path)) return 1;
    if (list->count >= 4096) return 0;
    copy = malloc(strlen(path) + 1);
    if (!copy) return 0;
    strcpy(copy, path);
    grown = realloc(list->devices, (list->count + 1) * sizeof(*grown));
    if (!grown) { free(copy); return 0; }
    list->devices = grown; list->devices[list->count++] = copy;
    return 1;
}
int grub_macos_collect_partitions(CFDictionaryRef plist, struct grub_macos_partitions *list,
                                  char *error, size_t error_size)
{
    CFArrayRef disks;
    CFIndex i, j;
    const char *reason = "Malformed physical disk list; automatic discovery stopped.";
    memset(list, 0, sizeof(*list));
    if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) goto fail;
    disks = CFDictionaryGetValue(plist, CFSTR("AllDisksAndPartitions"));
    if (!disks || CFGetTypeID(disks) != CFArrayGetTypeID()) goto fail;
    for (i = 0; i < CFArrayGetCount(disks); i++) {
        CFDictionaryRef disk = CFArrayGetValueAtIndex(disks, i);
        CFArrayRef parts;
        char disk_id[64], content[128];
        if (!disk || CFGetTypeID(disk) != CFDictionaryGetTypeID() ||
            !string_property(disk, CFSTR("DeviceIdentifier"), disk_id, sizeof(disk_id)) ||
            !whole_disk(disk_id) || !string_property(disk, CFSTR("Content"), content, sizeof(content))) goto fail;
        list->physical_disks++;
        if (strcmp(content, "GUID_partition_scheme")) {
            grub_progress("Skipping /dev/%s: not a GPT partition table.", disk_id); continue;
        }
        grub_progress("Reading Linux partition candidates on physical disk /dev/%s...", disk_id);
        parts = CFDictionaryGetValue(disk, CFSTR("Partitions"));
        /* Empty/unpartitioned physical disks may omit the array. */
        if (!parts) continue;
        if (CFGetTypeID(parts) != CFArrayGetTypeID()) goto fail;
        for (j = 0; j < CFArrayGetCount(parts); j++) {
            CFDictionaryRef part = CFArrayGetValueAtIndex(parts, j);
            char part_id[64];
            if (!part || CFGetTypeID(part) != CFDictionaryGetTypeID()) goto fail;
            /* Free-space rows have no device identifier. */
            if (!CFDictionaryGetValue(part, CFSTR("DeviceIdentifier"))) continue;
            if (!string_property(part, CFSTR("DeviceIdentifier"), part_id, sizeof(part_id)) ||
                !direct_partition(disk_id, part_id) ||
                !string_property(part, CFSTR("Content"), content, sizeof(content))) goto fail;
            if (!grub_macos_linux_type(content)) continue;
            if (!add_partition(list, part_id)) {
                reason = "Too many partition candidates or out of memory."; goto fail;
            }
        }
    }
    return 1;
fail:
    snprintf(error, error_size, "%s", reason);
    return 0;
}
void grub_macos_free_partitions(struct grub_macos_partitions *list)
{
    size_t i;
    for (i = 0; i < list->count; i++) free(list->devices[i]);
    free(list->devices); memset(list, 0, sizeof(*list));
}

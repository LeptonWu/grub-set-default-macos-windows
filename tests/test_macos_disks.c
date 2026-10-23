/* Exercise the actual mapper with CoreFoundation object-tree fixtures, not disks. */
#include "macos_disks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { STRING = 1, DICT, ARRAY, BOOLEAN };
struct value;
struct pair { const char *key; struct value *value; };
struct value {
    int type;
    const char *string;
    struct pair *pairs;
    struct value **items;
    size_t count;
};
CFTypeID CFGetTypeID(CFTypeRef value) { return (CFTypeID)((const struct value *)value)->type; }
CFTypeID CFDictionaryGetTypeID(void) { return DICT; }
CFTypeID CFArrayGetTypeID(void) { return ARRAY; }
CFTypeID CFStringGetTypeID(void) { return STRING; }
CFTypeID CFBooleanGetTypeID(void) { return BOOLEAN; }
unsigned char CFBooleanGetValue(CFBooleanRef boolean)
{
    const struct value *b = boolean;
    assert(b->type == BOOLEAN);
    return b->count != 0;
}
CFIndex CFStringGetLength(CFStringRef string)
{
    const struct value *s = string;
    assert(s->type == STRING);
    return (CFIndex)strlen(s->string);
}
CFTypeRef CFDictionaryGetValue(CFDictionaryRef dict, const void *key)
{
    const struct value *d = dict;
    size_t i;
    assert(d->type == DICT);
    for (i = 0; i < d->count; i++) if (!strcmp(d->pairs[i].key, key)) return d->pairs[i].value;
    return NULL;
}
CFIndex CFArrayGetCount(CFArrayRef array)
{
    const struct value *a = array; assert(a->type == ARRAY); return (CFIndex)a->count;
}
const void *CFArrayGetValueAtIndex(CFArrayRef array, CFIndex index)
{
    const struct value *a = array;
    assert(a->type == ARRAY && index >= 0 && (size_t)index < a->count);
    return a->items[index];
}
int CFStringGetCString(CFStringRef string, char *buffer, CFIndex size, unsigned int encoding)
{
    const struct value *s = string;
    assert(s->type == STRING && encoding == kCFStringEncodingUTF8);
    if (strlen(s->string) >= (size_t)size) return 0;
    strcpy(buffer, s->string); return 1;
}
#define STR(text) {STRING, text, NULL, NULL, 0}
#define DICTIONARY(p) {DICT, NULL, p, NULL, sizeof(p) / sizeof((p)[0])}
#define LIST(a) {ARRAY, NULL, NULL, a, sizeof(a) / sizeof((a)[0])}
static void check_mount_state(void)
{
    struct value yes = {BOOLEAN, NULL, NULL, NULL, 1};
    struct value no = {BOOLEAN, NULL, NULL, NULL, 0};
    struct value empty = STR(""), path = STR("/Volumes/Linux"), text = STR("false");
    struct pair pairs[] = {{"Mounted", &no}, {"MountPoint", &empty}};
    struct value info = DICTIONARY(pairs);
    int mounted;
    /* Explicit false permits access unless MountPoint reports a mount. */
    assert(grub_macos_mount_state(&info, &mounted) && !mounted);
    info.count = 1;
    assert(grub_macos_mount_state(&info, &mounted) && !mounted);
    pairs[0].value = &yes;
    assert(grub_macos_mount_state(&info, &mounted) && mounted);
    info.count = 2;
    assert(grub_macos_mount_state(&info, &mounted) && mounted);
    pairs[1].value = &path;
    assert(grub_macos_mount_state(&info, &mounted) && mounted);
    /* A nonempty MountPoint wins over a contradictory Mounted=false. */
    pairs[0].value = &no;
    assert(grub_macos_mount_state(&info, &mounted) && mounted);
    /* Real Linux-partition diskutil output can omit Mounted altogether. */
    pairs[0].key = "Other";
    assert(grub_macos_mount_state(&info, &mounted) && mounted);
    pairs[1].value = &empty;
    assert(grub_macos_mount_state(&info, &mounted) && !mounted);
    /* Missing Mounted with mistyped MountPoint remains unknown. */
    pairs[1].value = &yes;
    mounted = 0;
    assert(!grub_macos_mount_state(&info, &mounted) && mounted);
    /* Both absent must not be interpreted as unmounted. */
    info.count = 0;
    mounted = 0;
    assert(!grub_macos_mount_state(&info, &mounted) && mounted);
    info.count = 2; pairs[0].key = "Mounted"; pairs[0].value = &text;
    pairs[1].value = &empty;
    mounted = 0;
    assert(!grub_macos_mount_state(&info, &mounted) && mounted);
    pairs[0].value = &no; pairs[1].value = &yes;
    mounted = 0;
    assert(!grub_macos_mount_state(&info, &mounted) && mounted);
    mounted = 0;
    assert(!grub_macos_mount_state(NULL, &mounted) && mounted);
    mounted = 0;
    assert(!grub_macos_mount_state(&path, &mounted) && mounted);
    puts("Passed macOS mount-state checks: explicit booleans, optional/empty mount points, mounted and conflicting reports, missing/mistyped properties and invalid dictionaries.");
}
int main(void)
{
    check_mount_state();
    struct value disk0_id = STR("disk0"), disk1_id = STR("disk1");
    struct value scheme = STR("GUID_partition_scheme"), mbr = STR("FDisk_partition_scheme");
    struct value linux_type = STR("Linux Filesystem"), apfs_type = STR("Apple_APFS");
    struct value root_type = STR("4f68bce3-e8cd-4db1-96e7-fbcaf984b709");
    struct value p0_id = STR("disk0s3"), p1_id = STR("disk1s8"), apfs_id = STR("disk0s2"), synthetic_id = STR("disk6s6");
    struct pair p0_pairs[] = {{"DeviceIdentifier", &p0_id}, {"Content", &linux_type}};
    struct pair p1_pairs[] = {{"DeviceIdentifier", &p1_id}, {"Content", &root_type}};
    struct pair apfs_pairs[] = {{"DeviceIdentifier", &apfs_id}, {"Content", &apfs_type}};
    struct pair synthetic_pairs[] = {{"DeviceIdentifier", &synthetic_id}, {"Content", &linux_type}};
    struct value p0 = DICTIONARY(p0_pairs), p1 = DICTIONARY(p1_pairs), apfs = DICTIONARY(apfs_pairs), synthetic = DICTIONARY(synthetic_pairs);
    struct value *synth_items[] = {&synthetic};
    struct value synth_volumes = LIST(synth_items);
    struct value *parts0_items[] = {&apfs, &p0, &p0}; /* A duplicate is inspected once. */
    struct value *parts1_items[] = {&p1};
    struct value parts0 = LIST(parts0_items), parts1 = LIST(parts1_items);
    struct pair disk0_pairs[] = {{"DeviceIdentifier", &disk0_id}, {"Content", &scheme}, {"Partitions", &parts0}, {"APFSVolumes", &synth_volumes}};
    struct pair disk1_pairs[] = {{"DeviceIdentifier", &disk1_id}, {"Content", &scheme}, {"Partitions", &parts1}};
    struct value disk0 = DICTIONARY(disk0_pairs), disk1 = DICTIONARY(disk1_pairs);
    struct value *disk_items[] = {&disk0, &disk1};
    struct value disks = LIST(disk_items);
    struct pair root_pairs[] = {{"AllDisksAndPartitions", &disks}};
    struct value root = DICTIONARY(root_pairs);
    struct grub_macos_partitions result;
    char error[256];
    assert(grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    assert(result.physical_disks == 2 && result.count == 2);
    assert(!strcmp(result.devices[0], "/dev/rdisk0s3"));
    assert(!strcmp(result.devices[1], "/dev/rdisk1s8"));
    grub_macos_free_partitions(&result);
    assert(!result.devices && !result.count);
    /* An unknown parent and a shell-like identifier cannot become candidates. */
    p0_id.string = "disk6s6";
    assert(!grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    grub_macos_free_partitions(&result);
    p0_id.string = "disk0s3;touch bad";
    assert(!grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    grub_macos_free_partitions(&result);
    p0_id.string = "disk0s3s1";
    assert(!grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    grub_macos_free_partitions(&result);
    p0_id.string = "disk0s3";
    parts0.type = STRING;
    assert(!grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    grub_macos_free_partitions(&result); parts0.type = ARRAY;
    root.count = 0;
    assert(!grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    grub_macos_free_partitions(&result); root.count = 1;
    /* Non-GPT physical disks are counted but not searched for Linux entries. */
    disk0_pairs[1].value = &mbr;
    assert(grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    assert(result.physical_disks == 2 && result.count == 1);
    assert(!strcmp(result.devices[0], "/dev/rdisk1s8"));
    grub_macos_free_partitions(&result);
    disks.count = 0;
    assert(grub_macos_collect_partitions(&root, &result, error, sizeof(error)));
    assert(result.physical_disks == 0 && result.count == 0);
    grub_macos_free_partitions(&result);
    assert(!grub_macos_linux_type("Apple_APFS") && grub_macos_linux_type("Linux Filesystem"));
    puts("Passed macOS physical-disk mapping: two disks, Linux-only candidates, ignored synthesized volumes, deduplication, parent/identifier validation, malformed lists, non-GPT and empty lists.");
    return 0;
}

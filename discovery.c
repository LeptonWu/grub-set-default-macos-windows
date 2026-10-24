#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "discovery.h"
#include "ext4_reader.h"
#include "windows_reader.h"
#include "config_reader.h"
#include "progress.h"

/* GUID_DEVINTERFACE_DISK and common Linux GPT filesystem partition types. */
static const GUID disk_interface =
    {0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
static const GUID linux_types[] = {
    {0x0fc63daf, 0x8483, 0x4772, {0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4}}, /* data */
    {0x4f68bce3, 0xe8cd, 0x4db1, {0x96, 0xe7, 0xfb, 0xca, 0xf9, 0x84, 0xb7, 0x09}}, /* root x64 */
    {0x44479540, 0xf297, 0x41b2, {0x9a, 0xf7, 0xd1, 0x31, 0xd5, 0xf0, 0x45, 0x8a}}, /* root x86 */
    {0xb921b045, 0x1df0, 0x41c3, {0xaf, 0x44, 0x4c, 0x6f, 0x28, 0x0d, 0x3f, 0xae}}, /* root ARM64 */
    {0xbc13c2ff, 0x59e6, 0x4262, {0xa3, 0x52, 0xb2, 0x75, 0xfd, 0x6f, 0x71, 0x72}}, /* XBOOTLDR */
};

static int linux_partition(const GUID *type)
{
    size_t i;
    for (i = 0; i < sizeof(linux_types) / sizeof(linux_types[0]); i++)
        if (!memcmp(type, &linux_types[i], sizeof(*type))) return 1;
    return 0;
}

static DRIVE_LAYOUT_INFORMATION_EX *read_layout(HANDLE disk)
{
    DWORD capacity = 4096;
    const DWORD header = (DWORD)offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
    while (capacity <= 4 * 1024 * 1024) {
        DRIVE_LAYOUT_INFORMATION_EX *layout = malloc(capacity);
        DWORD received = 0, error;
        if (!layout) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
        if (DeviceIoControl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
                            layout, capacity, &received, NULL)) {
            if (received < header ||
                layout->PartitionCount > (received - header) / sizeof(PARTITION_INFORMATION_EX)) {
                free(layout); SetLastError(ERROR_INVALID_DATA); return NULL;
            }
            return layout;
        }
        error = GetLastError();
        free(layout);
        if (error != ERROR_INSUFFICIENT_BUFFER && error != ERROR_MORE_DATA) {
            SetLastError(error); return NULL;
        }
        capacity *= 2;
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return NULL;
}

static int probe_ext4(const char *device)
{
    io_channel channel = NULL;
    struct ext2_super_block super;
    errcode_t result = grub_windows_reader->open(device, 0, &channel);
    if (result) return -1;
    result = channel->manager->read_blk(channel, 1, -1024, &super);
    channel->manager->close(channel);
    if (result) return -1;
    /* ext2/ext3 share the magic; require an ext4 feature as well. */
    return super.s_magic == EXT2_SUPER_MAGIC &&
        ((super.s_feature_incompat & (EXT3_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT)) ||
         (super.s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_METADATA_CSUM));
}

static int probe_candidate(unsigned int disk, const PARTITION_INFORMATION_EX *partition,
                           const char *path, int config_only, struct grub_candidate **items, size_t *count)
{
    struct grub_path_match matches[GRUB_COMMON_PATHS];
    struct grub_candidate *grown;
    char device[128];
    size_t i, found;
    int signature;
    for (i = 0; i < *count; i++) {
        if ((*items)[i].disk == disk && (*items)[i].partition == partition->PartitionNumber)
            return 1;
    }
    snprintf(device, sizeof(device), "\\\\?\\GLOBALROOT\\Device\\Harddisk%u\\Partition%lu",
             disk, (unsigned long)partition->PartitionNumber);
    grub_progress("Checking disk %u partition %lu for ext4...", disk, (unsigned long)partition->PartitionNumber);
    signature = probe_ext4(device);
    if (signature < 0) {
        fprintf(stderr, "Cannot read disk %u partition %lu during discovery.\n",
                disk, (unsigned long)partition->PartitionNumber);
        return 0;
    }
    if (!signature) {
        grub_progress("Skipping disk %u partition %lu: no ext4 signature.", disk, (unsigned long)partition->PartitionNumber);
        return 1;
    }
    found = grub_probe_paths(device, path, config_only, matches);
    if (!found) return 1;
    grown = realloc(*items, (*count + found) * sizeof(**items));
    if (!grown) return 0;
    *items = grown;
    for (i = 0; i < found; i++) {
        struct grub_candidate *candidate = &(*items)[(*count)++];
        memset(candidate, 0, sizeof(*candidate));
        candidate->disk = disk;
        candidate->partition = partition->PartitionNumber;
        candidate->guid = partition->Gpt.PartitionId;
        strcpy(candidate->env_path, matches[i].env_path);
        strcpy(candidate->change.current, matches[i].current);
    }
    return 1;
}

int grub_discover(const struct grub_scan_filter *filter,
                  struct grub_candidate **items, size_t *count)
{
    HDEVINFO devices;
    DWORD index;
    int ok = 1;
    *items = NULL;
    *count = 0;
    grub_progress("Enumerating disks...");
    devices = SetupDiGetClassDevsA(&disk_interface, NULL, NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE) {
        fputs("Could not enumerate disks. Run in an Administrator terminal.\n", stderr);
        return 0;
    }
    for (index = 0; ; index++) {
        SP_DEVICE_INTERFACE_DATA interface;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail;
        DWORD required = 0, received, i, layout_error;
        STORAGE_DEVICE_NUMBER number;
        HANDLE disk;
        DRIVE_LAYOUT_INFORMATION_EX *layout;
        memset(&interface, 0, sizeof(interface));
        interface.cbSize = sizeof(interface);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &disk_interface, index, &interface)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) ok = 0;
            break;
        }
        SetupDiGetDeviceInterfaceDetailA(devices, &interface, NULL, 0, &required, NULL);
        if (required < sizeof(*detail) || required > 1024 * 1024) { ok = 0; break; }
        detail = malloc(required);
        if (!detail) { ok = 0; break; }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailA(devices, &interface, detail, required, NULL, NULL)) {
            free(detail); ok = 0; break;
        }
        grub_progress("Inspecting disk interface %lu...", (unsigned long)index + 1);
        disk = CreateFileA(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        free(detail);
        if (disk == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_READY || error == ERROR_FILE_NOT_FOUND || error == ERROR_DEVICE_NOT_CONNECTED)
                continue;
            fprintf(stderr, "Cannot read a disk during discovery (Windows error %lu).\n",
                    (unsigned long)error);
            ok = 0; continue;
        }
        if (!DeviceIoControl(disk, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
                             &number, sizeof(number), &received, NULL) || received < sizeof(number)) {
            CloseHandle(disk); ok = 0; continue;
        }
        if (filter->have_disk && filter->disk != number.DeviceNumber) { CloseHandle(disk); continue; }
        grub_progress("Reading the partition table for disk %lu...", (unsigned long)number.DeviceNumber);
        layout = read_layout(disk);
        layout_error = layout ? 0 : GetLastError();
        CloseHandle(disk);
        if (!layout) {
            if (layout_error == ERROR_NOT_READY) continue;
            fprintf(stderr, "Cannot read partition table for disk %lu.\n", (unsigned long)number.DeviceNumber);
            ok = 0; continue;
        }
        if (layout->PartitionStyle == PARTITION_STYLE_GPT) {
            for (i = 0; i < layout->PartitionCount; i++) {
                const PARTITION_INFORMATION_EX *partition = &layout->PartitionEntry[i];
                if (partition->PartitionStyle != PARTITION_STYLE_GPT ||
                    !partition->PartitionNumber || partition->PartitionLength.QuadPart < 2048 ||
                    !linux_partition(&partition->Gpt.PartitionType)) continue;
                if (filter->have_partition && filter->partition != partition->PartitionNumber) continue;
                if (filter->have_guid && memcmp(&filter->guid, &partition->Gpt.PartitionId, sizeof(GUID))) continue;
                if (!probe_candidate(number.DeviceNumber, partition, filter->path, filter->config_only, items, count)) ok = 0;
            }
        }
        free(layout);
    }
    SetupDiDestroyDeviceInfoList(devices);
    grub_progress("Partition scan complete: %zu matching GRUB target(s).", *count);
    if (!ok) fputs("Discovery was incomplete; automatic selection has been stopped.\n", stderr);
    return ok;
}

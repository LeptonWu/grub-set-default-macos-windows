/* Run the real discovery code against deterministic, read-only Windows API
 * mocks. The filesystem traversal itself is covered by real-image tests. */
#include <sys/types.h>
#include "windows.h"
#include "winioctl.h"
#include "setupapi.h"
#include "discovery.h"
#include "ext4_reader.h"
#include "windows_reader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GUID linux_type =
    {0x0fc63daf, 0x8483, 0x4772, {0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4}};
static int inaccessible, malformed, no_matches, invalid_environment, duplicate;
static int full_environment;
static int ext4_features = 1;
static DWORD last_error;
static unsigned int locate_calls, resizes;
static struct struct_io_manager probe_manager;
io_manager grub_windows_reader = &probe_manager;

DWORD GetLastError(void) { return last_error; }
void SetLastError(DWORD error) { last_error = error; }
HDEVINFO SetupDiGetClassDevsA(const GUID *guid, const char *name, void *window, DWORD flags)
{
    (void)guid; (void)name; (void)window;
    assert(flags == (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    return (HDEVINFO)(uintptr_t)1;
}
BOOL SetupDiEnumDeviceInterfaces(HDEVINFO devices, void *dev, const GUID *guid,
                                 DWORD index, SP_DEVICE_INTERFACE_DATA *interface)
{
    (void)devices; (void)dev; (void)guid;
    assert(interface->cbSize == sizeof(*interface));
    if (index >= (duplicate ? 3U : 2U)) { SetLastError(ERROR_NO_MORE_ITEMS); return 0; }
    interface->index = index == 2 ? 0 : index;
    return 1;
}
BOOL SetupDiGetDeviceInterfaceDetailA(HDEVINFO devices, SP_DEVICE_INTERFACE_DATA *interface,
                                       SP_DEVICE_INTERFACE_DETAIL_DATA_A *detail,
                                       DWORD size, DWORD *required, void *dev)
{
    char name[16];
    DWORD wanted;
    (void)devices; (void)dev;
    snprintf(name, sizeof(name), "disk%u", (unsigned int)interface->index);
    wanted = (DWORD)(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath) + strlen(name) + 1);
    if (wanted < sizeof(*detail)) wanted = sizeof(*detail);
    if (required) *required = wanted;
    if (!detail) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    assert(detail->cbSize == sizeof(*detail) && size >= wanted);
    strcpy(detail->DevicePath, name);
    return 1;
}
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO devices) { (void)devices; return 1; }
HANDLE CreateFileA(const char *name, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template)
{
    unsigned int disk;
    (void)share; (void)security; (void)disposition; (void)flags; (void)template;
    assert(access == GENERIC_READ);
    assert(sscanf(name, "disk%u", &disk) == 1 && disk < 2);
    if (inaccessible && disk == 0) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return (HANDLE)(uintptr_t)(disk + 1);
}
BOOL CloseHandle(HANDLE handle) { (void)handle; return 1; }
BOOL DeviceIoControl(HANDLE handle, DWORD operation, void *in, DWORD in_size,
                      void *out, DWORD out_size, DWORD *received, void *overlapped)
{
    unsigned int disk = (unsigned int)((uintptr_t)handle - 1);
    (void)in; (void)in_size; (void)overlapped;
    if (operation == IOCTL_STORAGE_GET_DEVICE_NUMBER) {
        STORAGE_DEVICE_NUMBER *number = out;
        assert(out_size >= sizeof(*number));
        memset(number, 0, sizeof(*number)); number->DeviceNumber = disk;
        *received = sizeof(*number);
        return 1;
    }
    assert(operation == IOCTL_DISK_GET_DRIVE_LAYOUT_EX);
    if (out_size < 8192) { resizes++; SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    {
        DRIVE_LAYOUT_INFORMATION_EX *layout = out;
        DWORD count = disk == 0 ? 3 : 1, i;
        size_t length = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) +
                        count * sizeof(PARTITION_INFORMATION_EX);
        assert(out_size >= length);
        memset(out, 0, length);
        layout->PartitionStyle = PARTITION_STYLE_GPT;
        layout->PartitionCount = malformed ? 1000000 : count;
        for (i = 0; i < count; i++) {
            PARTITION_INFORMATION_EX *part = &layout->PartitionEntry[i];
            part->PartitionStyle = PARTITION_STYLE_GPT;
            part->PartitionNumber = i + 3;
            part->PartitionLength.QuadPart = 64 * 1024 * 1024;
            part->Gpt.PartitionType = linux_type;
            part->Gpt.PartitionId.Data1 = disk * 10 + i + 1;
            if (disk == 0 && i == 2) memset(&part->Gpt.PartitionType, 0, sizeof(GUID));
        }
        *received = (DWORD)length;
        return 1;
    }
}

struct probe { unsigned int disk, partition; };
static errcode_t probe_open(const char *device, int flags, io_channel *result)
{
    io_channel channel = calloc(1, sizeof(*channel));
    struct probe *probe = calloc(1, sizeof(*probe));
    assert(flags == 0);
    assert(sscanf(device, "\\\\?\\GLOBALROOT\\Device\\Harddisk%u\\Partition%u",
                  &probe->disk, &probe->partition) == 2);
    channel->private_data = probe;
    channel->manager = &probe_manager;
    *result = channel;
    return 0;
}
static errcode_t probe_read(io_channel channel, unsigned long block, int count, void *out)
{
    struct probe *probe = channel->private_data;
    struct ext2_super_block *super = out;
    assert(block == 1 && count == -1024);
    memset(super, 0, sizeof(*super));
    if (!no_matches && probe->partition == 3) {
        super->s_magic = EXT2_SUPER_MAGIC;
        super->s_feature_incompat = ext4_features ? EXT3_FEATURE_INCOMPAT_EXTENTS : 0;
    }
    return 0;
}
static errcode_t probe_close(io_channel channel)
{
    free(channel->private_data); free(channel); return 0;
}
static struct struct_io_manager probe_manager = {
    .open = probe_open, .read_blk = probe_read, .close = probe_close,
};
static int config_calls;
int grub_load_config(const char *device, const char *path, unsigned char **data,
                     size_t *size, char *error, size_t error_size)
{
    (void)device; (void)path; (void)error; (void)error_size;
    config_calls++; *size = 1; *data = malloc(1); return *data != NULL;
}
int grub_locate_at(const char *device, const char *path, struct grub_location *location, char *error, size_t size)
{
    const char *text = invalid_environment ? "malformed\n" : "saved_entry=2\n";
    (void)device; (void)path; (void)error; (void)size;
    locate_calls++;
    memset(location, 0, sizeof(*location));
    location->block_size = 4096;
    location->block = malloc(4096);
    memset(location->block, '#', 4096);
    memcpy(location->block, text, strlen(text));
    if (full_environment) {
        memset(location->block, 'x', GRUB_ENV_SIZE);
        memcpy(location->block, "custom=", 7);
        location->block[GRUB_ENV_SIZE - 1] = '\n';
    }
    return 1;
}
void grub_location_free(struct grub_location *location) { free(location->block); }

int main(void)
{
    struct grub_scan_filter filter = {0};
    struct grub_candidate *items = NULL;
    size_t count;
    assert(grub_discover(&filter, &items, &count));
    assert(count == 2 && locate_calls == 2 && resizes == 2);
    assert(items[0].disk == 0 && items[1].disk == 1);
    assert(items[0].partition == 3 && !strcmp(items[0].change.current, "2"));
    free(items);
    full_environment = 1;
    assert(grub_discover(&filter, &items, &count));
    assert(count == 2 && !strcmp(items[0].change.current, "(unset)"));
    free(items);
    full_environment = 0;
    filter.have_disk = 1; filter.disk = 1;
    assert(grub_discover(&filter, &items, &count));
    assert(count == 1 && items[0].disk == 1); free(items);
    filter.have_disk = 0; filter.have_guid = 1; filter.guid.Data1 = 1;
    assert(grub_discover(&filter, &items, &count));
    assert(count == 1 && items[0].disk == 0); free(items);
    memset(&filter, 0, sizeof(filter));
    duplicate = 1;
    assert(grub_discover(&filter, &items, &count)); assert(count == 2); free(items);
    duplicate = 0; inaccessible = 1;
    assert(!grub_discover(&filter, &items, &count)); assert(count == 1); free(items);
    inaccessible = 0; malformed = 1;
    assert(!grub_discover(&filter, &items, &count)); assert(count == 0); free(items);
    malformed = 0; no_matches = 1;
    assert(grub_discover(&filter, &items, &count)); assert(count == 0); free(items);
    no_matches = 0; invalid_environment = 1;
    assert(grub_discover(&filter, &items, &count)); assert(count == 0); free(items);
    invalid_environment = 0; ext4_features = 0;
    assert(grub_discover(&filter, &items, &count)); assert(count == 0); free(items);
    ext4_features = 1; filter.config_only = 1; locate_calls = 0;
    assert(grub_discover(&filter, &items, &count));
    assert(count == 2 && config_calls == 2 && locate_calls == 0); free(items);
    puts("Passed discovery mocks: GPT/type/signature/grubenv filters, disk/GUID filters, multiple/no matches, duplicate interfaces, layout resizing, malformed tables and incomplete scans.");
    return 0;
}

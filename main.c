/* Native Windows console front end. Filesystem traversal is read-only; the
 * single block write below is the only operation that changes the partition. */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <rpc.h>
#include <malloc.h>
#include <io.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ext4_reader.h"
#include "grubenv.h"
#include "discovery.h"
#include "config_reader.h"
#include "progress.h"

struct options {
    unsigned int disk, partition;
    int have_disk, have_partition, have_guid, dry_run, scan_only, list_entries, show_default;
    GUID guid;
    const char *image;
    const char *entry;
    char env_path[1024];
};

static void usage(void)
{
    puts("Usage: grub-set-default.exe [OPTIONS] MENU_ENTRY\n"
         "       grub-set-default.exe --list-entries [OPTIONS]\n"
         "       grub-set-default.exe --show-default [OPTIONS]\n"
         "Set the persistent GRUB default by number, title, or menu ID.\n"
         "  --dry-run                 Preview and confirm without writing\n"
         "  --show-default            Show stored GRUB defaults (read-only)\n"
         "  --list-entries            List static grub.cfg menu definitions (read-only)\n"
         "  --scan                    List matching Linux GPT partitions\n"
         "  --disk N                  Restrict discovery to disk N\n"
         "  --partition N --partition-guid UUID  Select an explicit GPT partition\n"
         "  --partition-guid UUID     Restrict discovery by partition UUID\n"
         "  --image FILE              Use a partition-only ext4 image\n"
         "  --boot-directory DIR      Look for DIR/grub/grubenv (default /boot)\n"
         "  -h, --help                Show this help\n"
         "  -V, --version             Show version\n"
         "Requires GRUB_DEFAULT=saved in the existing GRUB configuration.\n"
         "Clears next_entry and prev_saved_entry, preserving other variables.\n"
         "Setting a default requires confirmation, including for a dry run. No reboot performed.\n"
         "Raw partition access requires an Administrator terminal.");
}

static void win_error(const char *operation)
{
    DWORD code = GetLastError();
    char *message = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, (char *)&message, 0, NULL);
    fprintf(stderr, "%s: Windows error %lu%s%s\n", operation, (unsigned long)code,
            message ? ": " : "", message ? message : "");
    if (message) LocalFree(message);
}

static int number(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;
    if (!*text || strspn(text, "0123456789") != strlen(text)) return 0;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > 65535) return 0;
    *value = (unsigned int)parsed;
    return 1;
}

static int parse_guid(const char *text, GUID *guid)
{
    char normalized[37];
    size_t length = strlen(text);
    if (length == 38 && text[0] == '{' && text[37] == '}') {
        memcpy(normalized, text + 1, 36);
        normalized[36] = 0;
        text = normalized;
    } else if (length != 36) return 0;
    return UuidFromStringA((RPC_CSTR)text, guid) == RPC_S_OK;
}

static void guid_text(const GUID *g, char text[37])
{
    snprintf(text, 37, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             (unsigned long)g->Data1, g->Data2, g->Data3,
             g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
             g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static int parse_options(int argc, char **argv, struct options *options)
{
    int i;
    memset(options, 0, sizeof(*options));
    strcpy(options->env_path, "/boot/grub/grubenv");
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--dry-run")) options->dry_run = 1;
        else if (!strcmp(arg, "--scan")) options->scan_only = 1;
        else if (!strcmp(arg, "--list-entries")) options->list_entries = 1;
        else if (!strcmp(arg, "--show-default")) options->show_default = 1;
        else if (!strcmp(arg, "--version") || !strcmp(arg, "-V")) { puts("grub-set-default (raw ext4 edition) 2.0"); return 2; }
        else if (!strcmp(arg, "--")) {
            if (i + 2 != argc || options->entry) return 0;
            options->entry = argv[++i];
        }
        else if (!strncmp(arg, "--boot-directory=", 17) || !strcmp(arg, "--boot-directory")) {
            const char *dir;
            size_t dir_size;
            if (arg[16] == '=') dir = arg + 17;
            else { if (++i == argc) return 0; dir = argv[i]; }
            dir_size = strlen(dir);
            while (dir_size && dir[dir_size - 1] == '/') dir_size--;
            if (*dir != '/' || dir_size >= sizeof(options->env_path) ||
                snprintf(options->env_path, sizeof(options->env_path), "%.*s/grub/grubenv", (int)dir_size, dir) >= (int)sizeof(options->env_path)) return 0;
        }
        else if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) { usage(); return 2; }
        else if (i + 1 < argc && !strcmp(arg, "--disk")) {
            if (options->have_disk || !number(argv[++i], &options->disk)) return 0;
            options->have_disk = 1;
        } else if (i + 1 < argc && !strcmp(arg, "--partition")) {
            if (options->have_partition || !number(argv[++i], &options->partition) ||
                !options->partition) return 0;
            options->have_partition = 1;
        } else if (i + 1 < argc && !strcmp(arg, "--partition-guid")) {
            if (options->have_guid || !parse_guid(argv[++i], &options->guid)) return 0;
            options->have_guid = 1;
        } else if (i + 1 < argc && !strcmp(arg, "--image")) {
            if (options->image) return 0;
            options->image = argv[++i];
        } else if (*arg != '-' && !options->entry) options->entry = arg;
        else return 0;
    }
    if ((!options->entry || !*options->entry) && !options->scan_only && !options->list_entries && !options->show_default) return 0;
    if ((options->scan_only || options->list_entries || options->show_default) && options->entry) return 0;
    if (options->list_entries && (options->scan_only || options->dry_run || options->show_default)) return 0;
    if (options->show_default && (options->scan_only || options->dry_run)) return 0;
    if (options->image)
        return !options->have_disk && !options->have_partition && !options->have_guid && !options->scan_only;
    return !options->have_partition || (options->have_disk && options->have_guid);
}

/* Return 1 for a selected candidate, 2 for list-only/cancel, 0 for an error. */
static int discover_target(struct options *options)
{
    struct grub_scan_filter filter;
    struct grub_candidate *items = NULL;
    size_t count = 0, i, selected = 0;
    int ok;
    char guid[37], answer[128];
    unsigned int choice;
    memset(&filter, 0, sizeof(filter));
    filter.path = options->env_path; filter.config_only = options->list_entries;
    filter.have_disk = options->have_disk; filter.disk = options->disk;
    filter.have_partition = options->have_partition; filter.partition = options->partition;
    filter.have_guid = options->have_guid; filter.guid = options->guid;
    printf("Scanning Linux GPT partitions for ext4 and %s...\n", options->list_entries ? "grub.cfg" : options->env_path);
    ok = grub_discover(&filter, &items, &count);
    for (i = 0; i < count; i++) {
        guid_text(&items[i].guid, guid);
        if (options->list_entries) printf("[%zu] Disk %u partition %u, GUID %s, grub.cfg found\n",
                i + 1, items[i].disk, items[i].partition, guid);
        else printf("[%zu] Disk %u partition %u, GUID %s, saved_entry=%s\n",
                i + 1, items[i].disk, items[i].partition, guid, items[i].change.current);
    }
    if (!ok) { free(items); return 0; }
    if (!count) {
        puts("No matching ext4 partition with the requested GRUB file was found.");
        free(items); return options->scan_only ? 2 : 0;
    }
    if (options->scan_only) { free(items); return 2; }
    if (count > 1) {
        printf("Choose a partition [1-%zu], or press Enter to cancel: ", count);
        fflush(stdout);
        if (!fgets(answer, sizeof(answer), stdin)) { free(items); return 2; }
        answer[strcspn(answer, "\r\n")] = 0;
        if (!*answer) { free(items); return 2; }
        if (!number(answer, &choice) || !choice || choice > count) {
            fputs("Invalid partition selection. No writes performed.\n", stderr);
            free(items); return 0;
        }
        selected = choice - 1;
    } else puts("Found one matching partition; selecting it for inspection.");
    options->disk = items[selected].disk;
    options->partition = items[selected].partition;
    options->guid = items[selected].guid;
    options->have_disk = options->have_partition = options->have_guid = 1;
    free(items);
    return 1;
}

static int partition_identity(HANDLE handle, const GUID *expected, uint64_t *size)
{
    PARTITION_INFORMATION_EX info;
    DWORD received;
    char actual[37], wanted[37];
    if (!DeviceIoControl(handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                         &info, sizeof(info), &received, NULL)) {
        win_error("Read partition identity"); return 0;
    }
    if (received < sizeof(info) || info.PartitionStyle != PARTITION_STYLE_GPT ||
        info.PartitionLength.QuadPart <= 0) {
        fputs("Expected a GPT partition with a positive length.\n", stderr); return 0;
    }
    if (memcmp(&info.Gpt.PartitionId, expected, sizeof(*expected))) {
        guid_text(&info.Gpt.PartitionId, actual);
        guid_text(expected, wanted);
        fprintf(stderr, "Partition GUID mismatch: expected %s; found %s.\n", wanted, actual);
        return 0;
    }
    *size = (uint64_t)info.PartitionLength.QuadPart;
    return 1;
}

static int transfer(HANDLE handle, uint64_t offset, void *buffer, DWORD length, int write)
{
    LARGE_INTEGER position;
    DWORD count = 0;
    BOOL result;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(handle, position, NULL, FILE_BEGIN)) {
        win_error("Seek"); return 0;
    }
    if (write) result = WriteFile(handle, buffer, length, &count, NULL);
    else result = ReadFile(handle, buffer, length, &count, NULL);
    if (!result) { win_error(write ? "Write block" : "Read block"); return 0; }
    if (count != length) {
        fprintf(stderr, "Short %s: expected %lu bytes, got %lu.\n",
                write ? "write" : "read", (unsigned long)length, (unsigned long)count);
        return 0;
    }
    return 1;
}

static int backup_file(const char *directory, const char *name, const void *data, size_t size)
{
    char path[MAX_PATH];
    FILE *file;
    int ok;
    if (snprintf(path, sizeof(path), "%s\\%s", directory, name) >= (int)sizeof(path)) return 0;
    file = fopen(path, "wb");
    if (!file) { perror("Create backup"); return 0; }
    ok = fwrite(data, 1, size, file) == size;
    if (fflush(file) || _commit(_fileno(file))) ok = 0;
    if (fclose(file)) ok = 0;
    if (!ok) fputs("Could not finish writing the backup.\n", stderr);
    return ok;
}

static int make_backup(const char *device, const struct grub_location *location,
                       const unsigned char *replacement, const struct grub_change *change,
                       char directory[MAX_PATH])
{
    char temp[MAX_PATH], metadata[4096];
    DWORD length = GetTempPathA(sizeof(temp), temp);
    int count;
    if (!length || length >= sizeof(temp)) { win_error("Find temporary directory"); return 0; }
    count = snprintf(directory, MAX_PATH, "%sgrubenv-backup-%lu-%llu", temp,
                     (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    if (count < 0 || count >= MAX_PATH || !CreateDirectoryA(directory, NULL)) {
        win_error("Create backup directory"); directory[0] = 0; return 0;
    }
    printf("Backup directory: %s\n", directory);
    count = snprintf(metadata, sizeof(metadata),
             "device=%s\nblock_size=%" PRIu32 "\nblock_number=%" PRIu64
             "\npartition_relative_offset=%" PRIu64 "\nfrom=%s\nto=%s\n",
             device, location->block_size, location->block_number,
             location->byte_offset, change->current, change->target);
    if (count < 0 || count >= (int)sizeof(metadata)) return 0;
    return backup_file(directory, "original-block.bin", location->block, location->block_size) &&
           backup_file(directory, "replacement-block.bin", replacement, location->block_size) &&
           backup_file(directory, "original-grubenv.bin", location->block, GRUB_ENV_SIZE) &&
           backup_file(directory, "location.txt", metadata, (size_t)count);
}

static int delete_backup(const char *directory)
{
    const char *names[] = {"original-block.bin", "replacement-block.bin",
                          "original-grubenv.bin", "location.txt"};
    char path[MAX_PATH];
    size_t i;
    if (!*directory) return 1;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s\\%s", directory, names[i]);
        if (!DeleteFileA(path) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            win_error("Remove backup file"); return 0;
        }
    }
    if (!RemoveDirectoryA(directory)) { win_error("Remove backup directory"); return 0; }
    return 1;
}

static int confirm(int dry_run, const struct grub_change *change)
{
    char answer[128];
    printf("%sChange saved_entry from '%s' to '%s'? [y/N] ",
            dry_run ? "Dry run: " : "", change->current, change->target);
    fflush(stdout);
    if (!fgets(answer, sizeof(answer), stdin)) return 0;
    answer[strcspn(answer, "\r\n")] = 0;
    return !_stricmp(answer, "y") || !_stricmp(answer, "yes");
}

int main(int argc, char **argv)
{
    struct options options;
    struct grub_location location = {0}, fresh = {0};
    struct grub_change change;
    char device[MAX_PATH], backup[MAX_PATH] = "", error[512];
    HANDLE handle = INVALID_HANDLE_VALUE;
    unsigned char *replacement = NULL, *check = NULL;
    int parsed, status = 1, locked = 0, wrote = 0;
    uint64_t device_size = 0;
    DWORD returned;
    LARGE_INTEGER image_size;
    DISK_GEOMETRY geometry;

    parsed = parse_options(argc, argv, &options);
    if (parsed == 2) return 0;
    if (!parsed) { usage(); return 1; }
    if (!options.image && (!options.have_partition || options.scan_only)) {
        int discovery = discover_target(&options);
        if (!discovery) return 1;
        if (discovery == 2) { puts("No disk writes performed."); return 0; }
    }
    if (options.image) {
        DWORD attributes;
        DWORD path_size = GetFullPathNameA(options.image, sizeof(device), device, NULL);
        if (!path_size || path_size >= sizeof(device) || device[0] == '\\') {
            fputs("Image mode requires a local, non-device file path shorter than MAX_PATH.\n", stderr);
            goto out;
        }
        attributes = GetFileAttributesA(device);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fputs("Image file not found.\n", stderr); goto out;
        }
    } else {
        snprintf(device, sizeof(device), "\\\\?\\GLOBALROOT\\Device\\Harddisk%u\\Partition%u",
                 options.disk, options.partition);
    }
    printf("Device: %s\n", device);
    grub_progress("Opening %s for reading...", device);
    handle = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        win_error("Open for reading (raw partitions require Administrator)"); goto out;
    }
    grub_progress("Checking target size and identity...");
    if (options.image) {
        if (!GetFileSizeEx(handle, &image_size) || image_size.QuadPart <= 0) {
            win_error("Read image size"); goto out;
        }
        device_size = (uint64_t)image_size.QuadPart;
    } else if (!partition_identity(handle, &options.guid, &device_size)) goto out;
    CloseHandle(handle); handle = INVALID_HANDLE_VALUE;

    if (options.list_entries) {
        if (!grub_list_config(device, options.env_path, stdout, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error); goto out;
        }
        status = 0; goto out;
    }
    if (options.show_default) {
        if (!grub_show_default(device, options.env_path, stdout, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error); goto out;
        }
        status = 0; goto out;
    }
    grub_progress("Locating %s and checking its file data...", options.env_path);
    if (!grub_locate_at(device, options.env_path, &location, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); goto out;
    }
    if (device_size < location.block_size ||
        location.byte_offset > device_size - location.block_size) {
        fputs("grubenv block falls outside the selected partition/image.\n", stderr); goto out;
    }
    replacement = _aligned_malloc(location.block_size, 65536);
    check = _aligned_malloc(location.block_size, 65536);
    if (!replacement || !check) { fputs("Out of memory.\n", stderr); goto out; }
    if (!grub_prepare(location.block, location.block_size, options.entry, replacement, &change, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); goto out;
    }
    printf("Block size: %" PRIu32 "\nPhysical block: %" PRIu64
           "\nPartition-relative byte offset: %" PRIu64 "\n",
           location.block_size, location.block_number, location.byte_offset);
    printf("Proposed saved_entry: %s\n", change.target);
    if (change.cleared_next_entry || change.cleared_prev_saved_entry)
        puts("Will remove next_entry and prev_saved_entry if present, like GNU grub-set-default.");
    grub_progress("Saving backup files...");
    if (!make_backup(device, &location, replacement, &change, backup)) goto out;
    if (!confirm(options.dry_run, &change)) {
        puts("Cancelled. No disk writes performed."); status = 0; goto out;
    }
    if (options.dry_run) {
        puts("Dry-run change confirmed. No disk writes performed."); status = 0; goto out;
    }

    if (!change.changed) { puts("Already set. No writes needed."); status = 0; goto out; }

    grub_progress("Rechecking the target and original file before writing...");
    if (!grub_locate_at(device, options.env_path, &fresh, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); goto out;
    }
    if (fresh.block_size != location.block_size || fresh.byte_offset != location.byte_offset ||
        memcmp(fresh.filesystem_uuid, location.filesystem_uuid, 16) ||
        memcmp(fresh.block, location.block, location.block_size)) {
        fputs("Filesystem or grubenv changed since inspection. No write performed.\n", stderr);
        goto out;
    }

    /* Obtain write access only after confirmation, and lock the partition. */
    handle = CreateFileA(device, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         options.image ? FILE_FLAG_WRITE_THROUGH :
                         FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (handle == INVALID_HANDLE_VALUE) { win_error("Open for writing"); goto out; }
    if (!options.image) {
        if (!partition_identity(handle, &options.guid, &device_size)) goto out;
        if (device_size < location.block_size ||
            location.byte_offset > device_size - location.block_size) {
            fputs("Partition size changed since the initial read. No write performed.\n", stderr);
            goto out;
        }
        if (!DeviceIoControl(handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &returned, NULL)) {
            win_error("Lock partition; no write performed"); goto out;
        }
        locked = 1;
        if (!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                             &geometry, sizeof(geometry), &returned, NULL)) {
            win_error("Read sector size"); goto out;
        }
        if (!geometry.BytesPerSector || location.block_size % geometry.BytesPerSector ||
            location.byte_offset % geometry.BytesPerSector) {
            fputs("The filesystem block is not aligned to the device sector size.\n", stderr); goto out;
        }
    }
    if (!transfer(handle, location.byte_offset, check, location.block_size, 0)) goto out;
    if (memcmp(check, location.block, location.block_size)) {
        fputs("The block changed since the initial read. No write performed.\n", stderr); goto out;
    }
    grub_progress("Writing the updated GRUB environment block...");
    wrote = 1;
    if (!transfer(handle, location.byte_offset, replacement, location.block_size, 1)) goto out;
    grub_progress("Flushing the device write cache...");
    if (!FlushFileBuffers(handle)) { win_error("Flush partition"); goto out; }
    grub_progress("Reading the block back to verify the change...");
    if (!transfer(handle, location.byte_offset, check, location.block_size, 0)) goto out;
    if (memcmp(check, replacement, location.block_size)) {
        fputs("Read-back verification FAILED. Inspect the disk before rebooting.\n", stderr); goto out;
    }
    printf("Verified: saved_entry=%s; other variables and bytes beyond grubenv are preserved.\n", change.target);
    puts("No reboot performed.");
    status = 0;
out:
    if (handle != INVALID_HANDLE_VALUE) {
        if (locked) DeviceIoControl(handle, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &returned, NULL);
        CloseHandle(handle);
    }
    if (status && *backup)
        fprintf(stderr, "%sBackups retained: %s\n", wrote ? "A write was attempted. " : "", backup);
    if (!status && !delete_backup(backup)) status = 1;
    _aligned_free(check);
    _aligned_free(replacement);
    grub_location_free(&location);
    grub_location_free(&fresh);
    return status;
}

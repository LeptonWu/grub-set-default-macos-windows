/* Native macOS front end. Also builds on Linux for image-only integration tests. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/disk.h>
#include <CoreFoundation/CoreFoundation.h>
#include "macos_disks.h"
#endif
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ext4_reader.h"
#include "grubenv.h"
#include "config_reader.h"
#include "posix_flush.h"
#include "progress.h"
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
struct options {
    const char *entry, *image, *device, *guid;
    char env_path[1024];
    int scan, dry, list, show_default;
};
struct candidate { char device[PATH_MAX], uuid[128], current[GRUB_ENV_SIZE]; };
static void usage(void)
{
    puts("Usage: grub-set-default [OPTIONS] MENU_ENTRY\n"
         "       grub-set-default --list-entries [OPTIONS]\n"
         "       grub-set-default --show-default [OPTIONS]\n"
         "Set saved_entry by number, title, or menu ID; requires GRUB_DEFAULT=saved.\n"
         "  --show-default         Show stored GRUB defaults (read-only)\n"
         "  --list-entries         List static grub.cfg menu definitions (read-only)\n"
         "  --scan                 List matching Linux GPT partitions (no entry needed)\n"
         "  --device /dev/rdiskNsP  Select a macOS partition explicitly\n"
         "  --partition-guid UUID  Restrict discovery by GPT partition UUID\n"
         "  --image FILE           Use a partition-only ext4 image\n"
         "  --boot-directory DIR   Locate DIR/grub/grubenv inside ext4 (default /boot)\n"
         "  --dry-run              Preview and confirm without writing\n"
         "  -h, --help             Show help\n"
         "  -V, --version          Show version\n"
         "Clears next_entry and prev_saved_entry like GNU grub-set-default.\n"
         "Setting a default requires confirmation. No reboot performed.\n"
         "Raw partition access requires root; filesystem must be clean and unmounted.");
}
static int parse(int argc, char **argv, struct options *o)
{
    int i;
    memset(o, 0, sizeof(*o)); strcpy(o->env_path, "/boot/grub/grubenv");
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 2; }
        else if (!strcmp(a, "--version") || !strcmp(a, "-V")) { puts("grub-set-default (raw ext4 edition) 2.0"); return 2; }
        else if (!strcmp(a, "--dry-run")) o->dry = 1;
        else if (!strcmp(a, "--scan")) o->scan = 1;
        else if (!strcmp(a, "--list-entries")) o->list = 1;
        else if (!strcmp(a, "--show-default")) o->show_default = 1;
        else if (!strcmp(a, "--")) {
            if (o->entry || i + 2 != argc) return 0;
            o->entry = argv[++i];
        } else if (!strncmp(a, "--boot-directory=", 17) || !strcmp(a, "--boot-directory")) {
            const char *dir;
            size_t dir_size;
            if (a[16] == '=') dir = a + 17;
            else { if (++i == argc) return 0; dir = argv[i]; }
            dir_size = strlen(dir);
            while (dir_size && dir[dir_size - 1] == '/') dir_size--;
            if (*dir != '/' || dir_size >= sizeof(o->env_path) ||
                snprintf(o->env_path, sizeof(o->env_path), "%.*s/grub/grubenv", (int)dir_size, dir) >= (int)sizeof(o->env_path)) return 0;
        } else if (i + 1 < argc && !strcmp(a, "--image")) { if (o->image) return 0; o->image = argv[++i]; }
        else if (i + 1 < argc && !strcmp(a, "--device")) { if (o->device) return 0; o->device = argv[++i]; }
        else if (i + 1 < argc && !strcmp(a, "--partition-guid")) { if (o->guid) return 0; o->guid = argv[++i]; }
        else if (*a != '-' && !o->entry) o->entry = a;
        else return 0;
    }
    if ((o->scan || o->list || o->show_default) ? o->entry != NULL : !o->entry || !*o->entry) return 0;
    if (o->list && (o->scan || o->dry || o->show_default)) return 0;
    if (o->show_default && (o->scan || o->dry)) return 0;
    if (o->image && (o->scan || o->device || o->guid)) return 0;
    return 1;
}
#ifdef __APPLE__
static int valid_device(const char *path)
{
    const char *p;
    if (!strncmp(path, "/dev/rdisk", 10)) p = path + 10;
    else if (!strncmp(path, "/dev/disk", 9)) p = path + 9;
    else return 0;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    if (*p++ != 's' || *p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') p++;
    return !*p;
}
static int string_property(CFDictionaryRef dict, CFStringRef key, char *out, size_t size)
{
    CFTypeRef value = CFDictionaryGetValue(dict, key);
    return value && CFGetTypeID(value) == CFStringGetTypeID() &&
        CFStringGetCString(value, out, (CFIndex)size, kCFStringEncodingUTF8);
}
/* Commands are fixed literals or contain only a validated device identifier. */
static CFDictionaryRef diskutil_dictionary(const char *command)
{
    char buffer[4096];
    FILE *pipe;
    size_t got;
    CFMutableDataRef data;
    CFPropertyListRef plist;
    pipe = popen(command, "r");
    if (!pipe) return NULL;
    data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    if (!data) { pclose(pipe); return NULL; }
    while ((got = fread(buffer, 1, sizeof(buffer), pipe))) {
        CFDataAppendBytes(data, (const UInt8 *)buffer, (CFIndex)got);
        if (CFDataGetLength(data) > 1024 * 1024) break;
    }
    {
        int read_error = ferror(pipe), result = pclose(pipe);
        if (read_error || result || CFDataGetLength(data) > 1024 * 1024) { CFRelease(data); return NULL; }
    }
    plist = CFPropertyListCreateWithData(kCFAllocatorDefault, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);
    if (!plist) return NULL;
    if (CFGetTypeID(plist) != CFDictionaryGetTypeID()) { CFRelease(plist); return NULL; }
    return (CFDictionaryRef)plist;
}
static int disk_info(const char *path, char uuid[128], int *linux_type, int *mounted)
{
    char command[256], block_path[PATH_MAX], content[128] = "";
    CFDictionaryRef plist;
    *mounted = 1;
    if (!valid_device(path)) return 0;
    if (!strncmp(path, "/dev/rdisk", 10)) {
        snprintf(block_path, sizeof(block_path), "/dev/%s", path + 6);
        path = block_path;
    }
    if (snprintf(command, sizeof(command), "/usr/sbin/diskutil info -plist %s", path) >= (int)sizeof(command)) return 0;
    plist = diskutil_dictionary(command);
    if (!plist) return 0;
    string_property(plist, CFSTR("Content"), content, sizeof(content));
    *linux_type = grub_macos_linux_type(content);
    uuid[0] = 0;
    string_property(plist, CFSTR("DiskUUID"), uuid, 128);
    if (!grub_macos_mount_state(plist, mounted)) {
        fprintf(stderr, "%s: missing or malformed mount state; raw access refused.\n", path);
        CFRelease(plist);
        return 0;
    }
    CFRelease(plist);
    return 1;
}
static int discover(const struct options *o, char path[PATH_MAX], char uuid[128])
{
    struct grub_macos_partitions disks = {0};
    struct candidate *items = NULL;
    size_t count = 0, i, selected = 0;
    char answer[64];
    int incomplete = 0, result;
    if (o->device) {
        grub_progress("Inspecting explicitly selected partition %s...", o->device);
        disks.devices = malloc(sizeof(*disks.devices));
        if (!disks.devices) return 0;
        disks.devices[0] = strdup(o->device);
        if (!disks.devices[0]) { free(disks.devices); return 0; }
        disks.count = 1;
    } else {
        CFDictionaryRef plist;
        char error[512];
        grub_progress("Enumerating physical disks (diskutil list -plist physical)...");
        plist = diskutil_dictionary("/usr/sbin/diskutil list -plist physical");
        if (!plist) { fputs("Could not enumerate physical disks.\n", stderr); return 0; }
        result = grub_macos_collect_partitions(plist, &disks, error, sizeof(error));
        CFRelease(plist);
        if (!result) {
            fprintf(stderr, "%s\n", error); grub_macos_free_partitions(&disks); return 0;
        }
        grub_progress("Found %zu physical disk(s), %zu Linux partition(s) to inspect.", disks.physical_disks, disks.count);
    }
    printf("Scanning Linux GPT partitions for %s...\n", o->list ? "grub.cfg" : o->env_path);
    for (i = 0; i < disks.count; i++) {
        char id[128], error[512];
        int linux_type, mounted;
        struct grub_location loc;
        struct grub_change change;
        struct candidate *grown;
        const char *device = disks.devices[i];
        if (!valid_device(device) || (o->device && strcmp(o->device, device))) continue;
        grub_progress("Checking partition %zu/%zu: %s (diskutil)...", i + 1, disks.count, device);
        if (!disk_info(device, id, &linux_type, &mounted)) {
            grub_progress("Could not read partition information for %s.", device);
            incomplete = 1; continue;
        }
        if (!linux_type) {
            grub_progress("Skipping %s: not a supported Linux GPT partition.", device); continue;
        }
        if (!*id) { grub_progress("Skipping %s: partition UUID unavailable.", device); continue; }
        if (o->guid && strcasecmp(id, o->guid)) {
            grub_progress("Skipping %s: partition UUID does not match.", device); continue;
        }
        if (mounted) { fprintf(stderr, "%s: mounted; excluded.\n", device); continue; }
        if (o->list) {
            unsigned char *config; size_t config_size;
            if (!grub_load_config(device, o->env_path, &config, &config_size, error, sizeof(error))) {
                fprintf(stderr, "%s: skipped: %s\n", device, error); continue;
            }
            free(config); strcpy(change.current, "(not read)");
        } else {
            grub_progress("Inspecting %s on %s...", o->env_path, device);
            if (!grub_locate_at(device, o->env_path, &loc, error, sizeof(error))) {
                fprintf(stderr, "%s: skipped: %s\n", device, error); continue;
            }
            result = grub_read_saved_entry(loc.block, GRUB_ENV_SIZE, change.current, error, sizeof(error));
            grub_location_free(&loc);
            if (!result) { fprintf(stderr, "%s: skipped: %s\n", device, error); continue; }
        }
        grown = realloc(items, (count + 1) * sizeof(*items));
        if (!grown) { incomplete = 1; break; }
        items = grown;
        snprintf(items[count].device, PATH_MAX, "%s", device);
        snprintf(items[count].uuid, sizeof(items[count].uuid), "%s", id);
        snprintf(items[count].current, sizeof(items[count].current), "%s", change.current);
        if (o->list) printf("[%zu] %s, UUID %s, grub.cfg found\n", count + 1, device, id);
        else printf("[%zu] %s, UUID %s, saved_entry=%s\n", count + 1, device, id, change.current);
        count++;
    }
    grub_macos_free_partitions(&disks);
    grub_progress("Partition scan complete: %zu matching partition(s).", count);
    if (incomplete) { free(items); fputs("Incomplete partition scan; no target selected.\n", stderr); return 0; }
    if (o->scan) { free(items); return 2; }
    if (!count) { free(items); fputs("No matching partition found.\n", stderr); return 0; }
    if (count > 1) {
        char *end;
        unsigned long choice;
        printf("Choose a partition [1-%zu], or Enter to cancel: ", count); fflush(stdout);
        if (!fgets(answer, sizeof(answer), stdin) || answer[0] == '\n') { free(items); return 2; }
        errno = 0; choice = strtoul(answer, &end, 10);
        if (errno || end == answer || (*end != '\n' && *end) || !choice || choice > count) { free(items); return 0; }
        selected = choice - 1;
    }
    strcpy(path, items[selected].device); strcpy(uuid, items[selected].uuid);
    grub_progress("Selected %s (UUID %s).", path, uuid);
    free(items); return 1;
}
#endif
static int save_file(const char *dir, const char *name, const void *data, size_t size)
{
    char path[PATH_MAX];
    FILE *f;
    int ok;
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 0;
    f = fopen(path, "wb");
    if (!f) return 0;
    ok = fwrite(data, 1, size, f) == size;
    if (fflush(f) || fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    return ok;
}
static int cleanup(const char *dir)
{
    const char *names[] = {"original-block.bin", "replacement-block.bin", "location.txt"};
    char path[PATH_MAX];
    size_t i;
    if (!*dir) return 1;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (unlink(path) && errno != ENOENT) return 0;
    }
    return rmdir(dir) == 0;
}
static int transfer(int fd, uint64_t offset, void *data, size_t size, int write_mode)
{
    ssize_t n;
    do { n = write_mode ? pwrite(fd, data, size, (off_t)offset) : pread(fd, data, size, (off_t)offset); }
    while (n < 0 && errno == EINTR);
    if (n < 0) perror(write_mode ? "Write block" : "Read block");
    else if ((size_t)n != size) fputs("Short block transfer.\n", stderr);
    return n >= 0 && (size_t)n == size;
}
int main(int argc, char **argv)
{
    struct options o;
    struct grub_location loc = {0}, fresh = {0};
    struct grub_change change;
    struct stat initial, current;
    char path[PATH_MAX], backup[PATH_MAX] = "", error[512], answer[32], metadata[PATH_MAX + 256];
    unsigned char *replacement = NULL, *check = NULL;
    int parsed = parse(argc, argv, &o), status = 1, fd = -1;
    uint64_t size;
#ifdef __APPLE__
    char uuid[128] = "";
    uint32_t sector = 1;
#endif
    if (parsed == 2) return 0;
    if (!parsed) { usage(); return 1; }
    if (o.image) {
        if (snprintf(path, sizeof(path), "%s", o.image) >= (int)sizeof(path)) return 1;
    } else {
#ifdef __APPLE__
        if (o.device && !valid_device(o.device)) { fputs("Expected /dev/rdiskNsP or /dev/diskNsP.\n", stderr); return 1; }
        if (o.device && !strncmp(o.device, "/dev/disk", 9)) {
            snprintf(path, sizeof(path), "/dev/r%s", o.device + 5); o.device = path;
        }
        if (geteuid()) { fputs("Run with sudo for raw partition access.\n", stderr); return 1; }
        parsed = discover(&o, path, uuid);
        if (parsed != 1) return parsed == 2 ? 0 : 1;
#else
        fputs("This host build supports --image only. Use native GRUB tools on Linux.\n", stderr); return 1;
#endif
    }
    printf("Device: %s\n", path);
    grub_progress("Opening %s for reading...", path);
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &initial)) { perror("Open target"); goto out; }
    grub_progress("Checking target size and identity...");
    if (o.image) {
        if (!S_ISREG(initial.st_mode) || initial.st_size <= 0) { fputs("Expected a partition-only regular image file.\n", stderr); goto out; }
        size = (uint64_t)initial.st_size;
    } else {
#ifdef __APPLE__
        uint64_t blocks;
        if ((!S_ISCHR(initial.st_mode) && !S_ISBLK(initial.st_mode)) ||
            ioctl(fd, DKIOCGETBLOCKSIZE, &sector) || ioctl(fd, DKIOCGETBLOCKCOUNT, &blocks) ||
            !sector || blocks > INT64_MAX / sector) { fputs("Cannot read partition geometry.\n", stderr); goto out; }
        size = blocks * sector;
#else
        goto out;
#endif
    }
    close(fd); fd = -1;
    if (o.list) {
        if (!grub_list_config(path, o.env_path, stdout, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error); goto out;
        }
        status = 0; goto out;
    }
    if (o.show_default) {
        if (!grub_show_default(path, o.env_path, stdout, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error); goto out;
        }
        status = 0; goto out;
    }
    grub_progress("Locating %s and checking its file data...", o.env_path);
    if (!grub_locate_at(path, o.env_path, &loc, error, sizeof(error))) { fprintf(stderr, "%s\n", error); goto out; }
    if (size < loc.block_size || loc.byte_offset > size - loc.block_size) { fputs("Block outside target.\n", stderr); goto out; }
#ifdef __APPLE__
    if (loc.byte_offset % sector || loc.block_size % sector) { fputs("Block is not sector aligned.\n", stderr); goto out; }
#endif
    if (posix_memalign((void **)&replacement, 65536, loc.block_size) ||
        posix_memalign((void **)&check, 65536, loc.block_size)) { fputs("Out of memory.\n", stderr); goto out; }
    if (!grub_prepare(loc.block, loc.block_size, o.entry, replacement, &change, error, sizeof(error))) { fprintf(stderr, "%s\n", error); goto out; }
    printf("saved_entry: '%s' -> '%s'\nBlock size: %u; partition-relative offset: %" PRIu64 "\n",
           change.current, change.target, loc.block_size, loc.byte_offset);
    puts("next_entry and prev_saved_entry will be removed if present.");
    umask(077);
    {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || !*tmp) tmp = "/tmp";
        if (snprintf(backup, sizeof(backup), "%s/grubenv-backup-XXXXXXXX", tmp) >= (int)sizeof(backup)) { backup[0] = 0; goto out; }
        if (!mkdtemp(backup)) { backup[0] = 0; perror("Create backup directory"); goto out; }
    }
    printf("Backup directory: %s\n", backup);
    grub_progress("Saving backup files...");
    snprintf(metadata, sizeof(metadata), "device=%s\nblock_size=%u\npartition_relative_offset=%" PRIu64 "\n", path, loc.block_size, loc.byte_offset);
    if (!save_file(backup, "original-block.bin", loc.block, loc.block_size) ||
        !save_file(backup, "replacement-block.bin", replacement, loc.block_size) ||
        !save_file(backup, "location.txt", metadata, strlen(metadata))) { perror("Save backups"); goto out; }
    printf("%sSet GRUB default to '%s'? [y/N] ", o.dry ? "Dry run: " : "", change.target); fflush(stdout);
    if (!fgets(answer, sizeof(answer), stdin)) answer[0] = 0;
    answer[strcspn(answer, "\r\n")] = 0;
    if (strcasecmp(answer, "y") && strcasecmp(answer, "yes")) { puts("Cancelled. No writes performed."); status = 0; goto out; }
    if (o.dry) { puts("Dry run confirmed. No writes performed."); status = 0; goto out; }
    if (!change.changed) { puts("Already set. No writes needed."); status = 0; goto out; }
    grub_progress("Rechecking the target and original file before writing...");
#ifdef __APPLE__
    if (!o.image) {
        char now[128]; int linux_type, mounted;
        if (!disk_info(path, now, &linux_type, &mounted) || !linux_type || mounted || strcmp(uuid, now)) {
            fputs("Partition identity or mount state changed; no write performed.\n", stderr); goto out;
        }
    }
#endif
    /* Re-resolve file metadata after confirmation; no cached physical offset. */
    if (!grub_locate_at(path, o.env_path, &fresh, error, sizeof(error))) { fprintf(stderr, "%s\n", error); goto out; }
    if (fresh.block_size != loc.block_size || fresh.byte_offset != loc.byte_offset ||
        memcmp(fresh.filesystem_uuid, loc.filesystem_uuid, 16) || memcmp(fresh.block, loc.block, loc.block_size)) {
        fputs("Filesystem or grubenv changed since inspection; no write performed.\n", stderr); goto out;
    }
    fd = open(path, O_RDWR);
    if (fd < 0 || fstat(fd, &current)) { perror("Open for writing"); goto out; }
    if (current.st_dev != initial.st_dev || current.st_ino != initial.st_ino || current.st_rdev != initial.st_rdev ||
        (o.image && current.st_size != initial.st_size)) { fputs("Target identity changed; no write performed.\n", stderr); goto out; }
    if (!transfer(fd, loc.byte_offset, check, loc.block_size, 0) || memcmp(check, loc.block, loc.block_size)) {
        fputs("Original block changed; no write performed.\n", stderr); goto out;
    }
    grub_progress("Writing the updated GRUB environment block...");
    if (!transfer(fd, loc.byte_offset, replacement, loc.block_size, 1)) goto out;
    grub_progress(o.image ? "Flushing the image file..." : "Flushing the device write cache...");
    if (!grub_posix_flush(fd, !o.image)) {
        perror(o.image ? "Flush image" : "Flush device cache"); goto out;
    }
    if (!o.image) puts("Device cache flush completed.");
    grub_progress("Reading the block back to verify the change...");
    if (!transfer(fd, loc.byte_offset, check, loc.block_size, 0) || memcmp(check, replacement, loc.block_size)) {
        fputs("Read-back verification failed. Inspect backup before rebooting.\n", stderr); goto out;
    }
    printf("Verified saved_entry=%s. No reboot performed.\n", change.target); status = 0;
out:
    if (fd >= 0) close(fd);
    if (status && *backup) fprintf(stderr, "Backups retained: %s\n", backup);
    if (!status && !cleanup(backup)) { perror("Remove backups"); status = 1; }
    free(check); free(replacement); grub_location_free(&fresh); grub_location_free(&loc);
    return status;
}

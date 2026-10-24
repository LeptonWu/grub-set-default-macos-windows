#include "path_probe.h"
#include "ext4_reader.h"
#include "config_reader.h"
#include "progress.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t grub_probe_paths(const char *device, const char *path, int config_only,
                        struct grub_path_match matches[GRUB_COMMON_PATHS])
{
    const char *common[] = {"/boot/grub/grubenv", "/boot/grub2/grubenv",
                            "/grub/grubenv", "/grub2/grubenv"};
    size_t i, count = 0, limit = path && *path ? 1 : GRUB_COMMON_PATHS;
    for (i = 0; i < limit; i++) {
        const char *candidate = path && *path ? path : common[i];
        char error[512];
        struct grub_path_match match = {0};
        grub_progress("Inspecting GRUB layout %s on %s...", candidate, device);
        if (config_only) {
            unsigned char *data;
            size_t size;
            if (!grub_load_config(device, candidate, &data, &size, error, sizeof(error))) {
                fprintf(stderr, "%s, %s: skipped: %s\n", device, candidate, error); continue;
            }
            free(data);
            strcpy(match.current, "(not read)");
        } else {
            struct grub_location loc;
            int valid;
            if (!grub_locate_at(device, candidate, &loc, error, sizeof(error))) {
                fprintf(stderr, "%s, %s: skipped: %s\n", device, candidate, error); continue;
            }
            valid = grub_read_saved_entry(loc.block, GRUB_ENV_SIZE, match.current, error, sizeof(error));
            grub_location_free(&loc);
            if (!valid) {
                fprintf(stderr, "%s, %s: skipped: %s\n", device, candidate, error); continue;
            }
        }
        if (snprintf(match.env_path, sizeof(match.env_path), "%s", candidate) >= (int)sizeof(match.env_path)) continue;
        matches[count++] = match;
    }
    return count;
}

int grub_select_path(const char *device, char path[GRUB_PATH_SIZE], int config_only, int yes)
{
    struct grub_path_match matches[GRUB_COMMON_PATHS];
    size_t count = grub_probe_paths(device, path, config_only, matches), i, selected = 0;
    if (!count) {
        fputs("No supported GRUB layout found on the selected target.\n", stderr); return 0;
    }
    for (i = 0; i < count; i++)
        printf("[%zu] %s, %s, saved_entry=%s%s\n", i + 1, device, matches[i].env_path,
               matches[i].current, config_only ? " (grub.cfg found)" : "");
    if (count > 1) {
        char answer[64], *end;
        unsigned long choice;
        if (yes) {
            fputs("Multiple GRUB layouts match; --yes requires a unique target. No writes performed.\n", stderr);
            return 0;
        }
        printf("Choose a GRUB target [1-%zu], or Enter to cancel: ", count); fflush(stdout);
        if (!fgets(answer, sizeof(answer), stdin) || answer[0] == '\n' || answer[0] == '\r') return 2;
        answer[strcspn(answer, "\r\n")] = 0;
        if (!*answer || strspn(answer, "0123456789") != strlen(answer)) {
            fputs("Invalid GRUB target selection. No writes performed.\n", stderr); return 0;
        }
        errno = 0; choice = strtoul(answer, &end, 10);
        if (errno || *end || !choice || choice > count) {
            fputs("Invalid GRUB target selection. No writes performed.\n", stderr); return 0;
        }
        selected = choice - 1;
    }
    strcpy(path, matches[selected].env_path);
    grub_progress("Selected %s on %s.", path, device);
    return 1;
}

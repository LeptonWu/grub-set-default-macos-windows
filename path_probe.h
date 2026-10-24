#ifndef GRUB_SET_PATH_PROBE_H
#define GRUB_SET_PATH_PROBE_H
#include <stddef.h>
#include "grubenv.h"
#define GRUB_PATH_SIZE 1024
#define GRUB_COMMON_PATHS 4
struct grub_path_match {
    char env_path[GRUB_PATH_SIZE];
    char current[GRUB_ENV_SIZE];
};
/* An empty/null path probes all common layouts. Config-only mode does not
 * require grubenv. Each returned match belongs to this device and exact path. */
size_t grub_probe_paths(const char *device, const char *path, int config_only,
                        struct grub_path_match matches[GRUB_COMMON_PATHS]);
/* Select a path on an already chosen device/image. Return 1 for a selected
 * path, 2 for cancellation, and 0 for failure. Never choose an ambiguous path
 * unattended. path is both the optional restriction and the selected result. */
int grub_select_path(const char *device, char path[GRUB_PATH_SIZE], int config_only, int yes);
#endif

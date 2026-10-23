#include "config_reader.h"
#include "ext4_reader.h"
#include "menu_entries.h"
#include "grubenv.h"
#include "progress.h"
#include <stdlib.h>
#include <string.h>
static int config_path(const char *env_path, char *path, size_t size)
{
    size_t length = strlen(env_path);
    if (length < 7 || strcmp(env_path + length - 7, "grubenv")) return 0;
    return snprintf(path, size, "%.*sgrub.cfg", (int)(length - 7), env_path) < (int)size;
}
int grub_load_config(const char *device, const char *env_path,
                       unsigned char **data, size_t *size, char *error, size_t error_size)
{
    char path[1100];
    if (!config_path(env_path, path, sizeof(path))) {
        snprintf(error, error_size, "Cannot derive grub.cfg path."); return 0;
    }
    grub_progress("Reading %s on %s...", path, device);
    return grub_read_file(device, path, 4 * 1024 * 1024, data, size, error, error_size);
}
int grub_list_config(const char *device, const char *env_path, FILE *out,
                       char *error, size_t error_size)
{
    char path[1100];
    unsigned char *data;
    size_t size;
    int ok;
    if (!grub_load_config(device, env_path, &data, &size, error, error_size)) return 0;
    if (!config_path(env_path, path, sizeof(path))) { free(data); return 0; }
    grub_progress("Read %zu bytes; listing menu definitions...", size);
    fprintf(out, "Configuration: %s\n", path);
    ok = grub_print_entries(data, size, out, error, error_size);
    free(data); return ok;
}

int grub_show_default(const char *device, const char *env_path, FILE *out,
                       char *error, size_t error_size)
{
    unsigned char *data;
    size_t size;
    int ok;
    grub_progress("Reading stored defaults from %s on %s...", env_path, device);
    if (!grub_read_file(device, env_path, GRUB_ENV_SIZE, &data, &size, error, error_size)) return 0;
    grub_progress("Read %zu bytes; checking stored default values...", size);
    fprintf(out, "Environment: %s\n", env_path);
    ok = grub_print_default(data, size, out, error, error_size);
    free(data); return ok;
}

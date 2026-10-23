#ifndef GRUB_SET_CONFIG_READER_H
#define GRUB_SET_CONFIG_READER_H
#include <stddef.h>
#include <stdio.h>
int grub_load_config(const char *device, const char *env_path,
                       unsigned char **data, size_t *size, char *error, size_t error_size);
int grub_list_config(const char *device, const char *env_path, FILE *out,
                       char *error, size_t error_size);
int grub_show_default(const char *device, const char *env_path, FILE *out,
                       char *error, size_t error_size);
#endif

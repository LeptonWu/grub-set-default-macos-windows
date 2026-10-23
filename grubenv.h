#ifndef GRUB_SET_ENV_H
#define GRUB_SET_ENV_H
#include <stddef.h>
#include <stdio.h>
#define GRUB_ENV_SIZE 1024
struct grub_change {
    char current[GRUB_ENV_SIZE];
    char target[GRUB_ENV_SIZE];
    int cleared_next_entry;
    int cleared_prev_saved_entry;
    int changed;
};
/* Like GNU grub-set-default: unset next_entry and prev_saved_entry, then set
 * saved_entry. Preserve other variables and every byte past the environment. */
int grub_prepare(const unsigned char *original, size_t block_size,
                 const char *entry, unsigned char *replacement,
                 struct grub_change *change, char *error, size_t error_size);
/* Validate existing contents without requiring room for an edit. If non-NULL,
 * current receives saved_entry or "(unset)" and must hold GRUB_ENV_SIZE bytes. */
int grub_read_saved_entry(const unsigned char *original, size_t size,
                          char current[GRUB_ENV_SIZE], char *error, size_t error_size);
/* Print stored defaults without changing the environment. */
int grub_print_default(const unsigned char *original, size_t size, FILE *out,
                       char *error, size_t error_size);
#endif

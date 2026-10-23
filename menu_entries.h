#ifndef GRUB_SET_MENU_ENTRIES_H
#define GRUB_SET_MENU_ENTRIES_H
#include <stddef.h>
#include <stdio.h>
/* Static listing only: no GRUB commands or included files are executed. */
int grub_print_entries(const unsigned char *data, size_t size, FILE *out,
                        char *error, size_t error_size);
#endif

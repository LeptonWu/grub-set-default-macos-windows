/* Host-side integration test driver: never writes the input image. */
#include "ext4_reader.h"
#include "grubenv.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    struct grub_location location;
    struct grub_change change;
    unsigned char *replacement;
    char error[512];
    if (argc != 2) return 2;
    if (!grub_locate(argv[1], &location, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); return 1;
    }
    replacement = malloc(location.block_size);
    if (!replacement) { grub_location_free(&location); return 1; }
    if (!grub_prepare(location.block, location.block_size, "test-entry", replacement, &change, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        free(replacement); grub_location_free(&location); return 1;
    }
    printf("%" PRIu32 " %" PRIu64 " %" PRIu64 " %s %s\n", location.block_size,
           location.block_number, location.byte_offset, change.current, change.target);
    free(replacement);
    grub_location_free(&location);
    return 0;
}

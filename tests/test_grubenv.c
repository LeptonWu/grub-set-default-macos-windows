#include "grubenv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check_mode(const char *prefix, const char *entry, const char *expected, int valid, int next_boot)
{
    unsigned char original[4096], replacement[4096], wanted[GRUB_ENV_SIZE];
    struct grub_change change;
    char error[256];
    size_t i;
    memset(original, '#', GRUB_ENV_SIZE);
    memcpy(original, prefix, strlen(prefix));
    for (i = GRUB_ENV_SIZE; i < sizeof(original); i++) original[i] = (unsigned char)i;
    memset(replacement, 0xaa, sizeof(replacement));
    assert((next_boot ? grub_prepare_next : grub_prepare)(original, sizeof(original), entry, replacement, &change, error, sizeof(error)) == valid);
    if (!valid) {
        for (i = 0; i < sizeof(replacement); i++) assert(replacement[i] == 0xaa);
        return;
    }
    memset(wanted, '#', sizeof(wanted)); memcpy(wanted, expected, strlen(expected));
    assert(!memcmp(replacement, wanted, sizeof(wanted)));
    assert(!memcmp(replacement + GRUB_ENV_SIZE, original + GRUB_ENV_SIZE, sizeof(original) - GRUB_ENV_SIZE));
    assert(!strcmp(change.target, entry));
    assert(change.changed == (memcmp(original, wanted, GRUB_ENV_SIZE) != 0));
}
static void check(const char *prefix, const char *entry, const char *expected, int valid)
{
    check_mode(prefix, entry, expected, valid, 0);
}
static void check_next(void)
{
    check_mode("# GRUB Environment Block\nsaved_entry=0\nnext_entry=2\nprev_saved_entry=4\nrecordfail=1\n", "opencore",
               "# GRUB Environment Block\nsaved_entry=0\nnext_entry=opencore\nrecordfail=1\n", 1, 1);
    check_mode("saved_entry=2\n", "0", "saved_entry=2\nnext_entry=0\n", 1, 1);
    check_mode("next_entry=4\nsaved_entry=2\n", "1>2", "next_entry=1>2\nsaved_entry=2\n", 1, 1);
    check_mode("# GRUB Environment Block\n", "Advanced options>Recovery", "# GRUB Environment Block\nnext_entry=Advanced options>Recovery\n", 1, 1);
    check_mode("saved_entry=\nnext_entry=\nprev_saved_entry=\n", "12", "saved_entry=\nnext_entry=12\n", 1, 1);
    check_mode("saved_entry=2\nnext_entry=4\n", "4", "saved_entry=2\nnext_entry=4\n", 1, 1);
    check_mode("custom=a=b#c\nnext_entry=old\n", "id", "custom=a=b#c\nnext_entry=id\n", 1, 1);
    check_mode("saved_entry=0\n", "", NULL, 0, 1);
    check_mode("saved_entry=0\n", "2\nnext_entry=0", NULL, 0, 1);
    check_mode("next_entry=2\nnext_entry=4\n", "0", NULL, 0, 1);
    check_mode("saved_entry=4\n#bad padding", "0", NULL, 0, 1);
    {
        unsigned char original[1024], replacement[1024];
        char error[256];
        struct grub_change change;
        const char *prefix = "saved_entry=0\nnext_entry=4\nprev_saved_entry=2\n";
        memset(original, '#', sizeof(original)); memcpy(original, prefix, strlen(prefix));
        assert(grub_prepare_next(original, sizeof(original), "4", replacement, &change, error, sizeof(error)));
        assert(!strcmp(change.current, "4") && change.changed);
        assert(!change.cleared_next_entry && change.cleared_prev_saved_entry);
    }
    {
        char huge[1024]; memset(huge, 'a', sizeof(huge) - 1); huge[1023] = 0;
        check_mode("# GRUB Environment Block\nsaved_entry=0\n", huge, NULL, 0, 1);
    }
    puts("Passed next-boot editor cases: preserved/unset/empty saved_entry, legacy override removal, IDs/titles/submenus, current override, no-op, preserved tail and invalid/oversized input.");
}
static void check_default(const char *prefix, const char *expected, int valid)
{
    unsigned char original[GRUB_ENV_SIZE], before[GRUB_ENV_SIZE];
    char error[256], output[4096];
    FILE *file = tmpfile();
    size_t count;
    assert(file);
    memset(original, '#', sizeof(original)); memcpy(original, prefix, strlen(prefix));
    memcpy(before, original, sizeof(before));
    assert(grub_print_default(original, sizeof(original), file, error, sizeof(error)) == valid);
    assert(!memcmp(before, original, sizeof(before)));
    rewind(file); count = fread(output, 1, sizeof(output) - 1, file); output[count] = 0;
    if (valid) assert(strstr(output, expected));
    else assert(count == 0);
    fclose(file);
}
int main(void)
{
    check_next();
    check("# GRUB Environment Block\nsaved_entry=0\nnext_entry=2\nprev_saved_entry=4\nrecordfail=1\n", "2",
          "# GRUB Environment Block\nsaved_entry=2\nrecordfail=1\n", 1);
    check("saved_entry=2\n", "0", "saved_entry=0\n", 1);
    check("saved_entry=4\nnext_entry=\n", "10", "saved_entry=10\n", 1);
    check("# GRUB Environment Block\n", "Advanced options>Linux 6.1", "# GRUB Environment Block\nsaved_entry=Advanced options>Linux 6.1\n", 1);
    check("recordfail=1\nsaved_entry=old title\ncustom=a=b#c\n", "gnulinux-id", "recordfail=1\nsaved_entry=gnulinux-id\ncustom=a=b#c\n", 1);
    check("saved_entry=2\n", "2", "saved_entry=2\n", 1);
    check("saved_entry=2\nnext_entry=\n", "2", "saved_entry=2\n", 1);
    check("saved_entry=\n", "1>2", "saved_entry=1>2\n", 1);
    check("saved_entry=0\n", "", NULL, 0);
    check("saved_entry=0\n", "2\nnext_entry=0", NULL, 0);
    check("saved_entry=4\r\n", "0", NULL, 0);
    check("saved_entry=4\nsaved_entry=2\n", "0", NULL, 0);
    check("saved_entry=4\nnext_entry=unterminated", "0", NULL, 0);
    check("saved_entry=4\n#not-padding", "0", NULL, 0);
    check("bad-line\n", "0", NULL, 0);
    {
        char huge[1024]; memset(huge, 'a', sizeof(huge) - 1); huge[1023] = 0;
        check("# GRUB Environment Block\nsaved_entry=0\n", huge, NULL, 0);
    }
    check_default("# GRUB Environment Block\nsaved_entry=4\nnext_entry=2\nprev_saved_entry=0\n",
                  "saved_entry=4\nnext_entry=2\nprev_saved_entry=0\nA nonempty next_entry", 1);
    check_default("recordfail=1\nsaved_entry=Advanced options>Recovery\nnext_entry=\n",
                  "saved_entry=Advanced options>Recovery\nnext_entry=(empty)\nprev_saved_entry=(unset)", 1);
    check_default("# GRUB Environment Block\n", "saved_entry=(unset)\nnext_entry=(unset)", 1);
    check_default("saved_entry=\n", "saved_entry=(empty)\n", 1);
    check_default("saved_entry=abc\033def\n", "saved_entry=abc\\x1bdef\n", 1);
    check_default("saved_entry=0\nsaved_entry=2\n", NULL, 0);
    check_default("saved_entry=0\n#bad padding", NULL, 0);
    {
        const char *prefixes[] = {
            "# GRUB Environment Block\ncustom=",
            "# GRUB Environment Block\nsaved_entry=\ncustom=",
            "custom="
        };
        size_t i, padding;
        for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            for (padding = 0; padding <= 3; padding += 3) {
                char full[GRUB_ENV_SIZE + 1], current[GRUB_ENV_SIZE], error[256];
                size_t used = GRUB_ENV_SIZE - padding;
                memset(full, 'x', used);
                memcpy(full, prefixes[i], strlen(prefixes[i]));
                full[used - 1] = '\n';
                memset(full + used, '#', padding);
                full[GRUB_ENV_SIZE] = 0;
                /* Reading succeeds even when adding/changing saved_entry cannot fit. */
                check_default(full, i == 1 ? "saved_entry=(empty)" : "saved_entry=(unset)", 1);
                assert(grub_read_saved_entry((unsigned char *)full, GRUB_ENV_SIZE,
                                             current, error, sizeof(error)));
                assert(!strcmp(current, i == 1 ? "" : "(unset)"));
                check(full, "test-entry", NULL, 0);
                check_mode(full, "test-entry", NULL, 0, 1);
            }
        }
    }
    puts("Passed 16 editor cases: numbers/titles/IDs/submenus, override removal, preserved variables/tail, no-op, malformed input and overflow rejection.");
    puts("Passed 7 read-only default cases: saved/next/previous values, titles/submenus, unset/empty values, control escaping and invalid environments.");
    puts("Passed 6 capacity regressions: full/nearly full environments, unset/empty saved_entry, headerless input, unchanged reads and rejected oversized edits.");
    return 0;
}

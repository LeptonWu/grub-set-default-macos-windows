#include "menu_entries.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *listing(const char *config, int valid)
{
    FILE *file = tmpfile();
    char error[512], *text;
    long size;
    assert(file);
    assert(grub_print_entries((const unsigned char *)config, strlen(config), file, error, sizeof(error)) == valid);
    assert(!fflush(file) && !fseek(file, 0, SEEK_END)); size = ftell(file); assert(size >= 0);
    text = malloc((size_t)size + 1); assert(text);
    rewind(file); assert(fread(text, 1, (size_t)size, file) == (size_t)size);
    text[size] = 0; fclose(file); return text;
}
int main(void)
{
    char *text = listing(
        "# menuentry 'Commented' { }\n"
        "echo menuentry 'Echoed'\n"
        "echo then menuentry 'Also echoed'\n"
        "set value=${root}\n"
        "menuentry 'Debian GNU/Linux' --class debian $menuentry_id_option 'linux-id' {\n"
        " echo 'a } brace { menuentry fake'; echo ${root}\n"
        " menuentry 'Defined inside entry body' { echo ignored; }\n"
        "}\n"
        "submenu \"Advanced options\" --id=advanced {\n"
        " menuentry --id child \"Linux \\\"rescue\\\"\" { echo foo#bar; }\n"
        " submenu 'Nested' { menuentry 'Deep' { } }\n"
        "}\n"
        "if false; then menuentry 'Conditional' --id conditional { true; }; fi\n"
        "menuentry 'Open'\\\n'Core' --id opencore\n{ chainloader /EFI/OC/OpenCore.efi; }\n"
        "menuentry \"Windows ${suffix}\" --id windows { }\n", 1);
    assert(strstr(text, "0  entry  title=\"Debian GNU/Linux\"  id=\"linux-id\""));
    assert(strstr(text, "1  submenu  title=\"Advanced options\"  id=\"advanced\""));
    assert(strstr(text, "1>0  entry  title=\"Linux \\\"rescue\\\"\"  id=\"child\""));
    assert(strstr(text, "1>1>0  entry  title=\"Deep\""));
    assert(strstr(text, "2  entry  title=\"Conditional\""));
    assert(strstr(text, "3  entry  title=\"OpenCore\"  id=\"opencore\""));
    assert(strstr(text, "4  entry  title=\"Windows ${suffix}\""));
    assert(strstr(text, "Found 6 menu entries and 2 submenus"));
    assert(!strstr(text, "Commented") && !strstr(text, "Echoed") && !strstr(text, "Also echoed") && !strstr(text, "Defined inside"));
    free(text);
    text = listing("menuentry 'missing brace'", 0); free(text);
    text = listing("menuentry 'unclosed quote { }", 0); free(text);
    text = listing("submenu 'unclosed' {", 0); free(text);
    text = listing("}\n", 0); free(text);
    text = listing("menuentry 'broken' --id { }", 0); free(text);
    text = listing("# no entries\nsource /other.cfg\n", 1);
    assert(strstr(text, "Found 0 menu entries")); free(text);
    puts("Passed static menu parser: quotes/escapes, comments, IDs/options, continuations, variables, nested paths, false-positive avoidance and malformed input.");
    return 0;
}

#include "grubenv.h"
#include <stdio.h>
#include <string.h>

static int fail(char *error, size_t size, const char *message)
{
    if (size) snprintf(error, size, "%s", message);
    return 0;
}

int grub_prepare(const unsigned char *original, size_t block_size,
                 const char *entry, unsigned char *replacement,
                 struct grub_change *change, char *error, size_t error_size)
{
    static const char header[] = "# GRUB Environment Block\n";
    unsigned char env[GRUB_ENV_SIZE];
    size_t starts[GRUB_ENV_SIZE / 3], lengths[GRUB_ENV_SIZE / 3], count = 0;
    size_t pos = 0, used = 0, entry_size, i;
    int have_saved = 0;
    struct grub_change result;
    if (!original || !replacement || !change || !entry || block_size < GRUB_ENV_SIZE)
        return fail(error, error_size, "Invalid environment buffer or menu entry.");
    entry_size = strlen(entry);
    if (!entry_size || entry_size >= GRUB_ENV_SIZE || strpbrk(entry, "\r\n"))
        return fail(error, error_size, "Menu entry must be nonempty and contain no newline.");
    memset(&result, 0, sizeof(result));
    strcpy(result.current, "(unset)");
    strcpy(result.target, entry);
    memset(env, '#', sizeof(env));
    if (!memcmp(original, header, sizeof(header) - 1)) {
        used = pos = sizeof(header) - 1;
        memcpy(env, header, used);
    }
    while (pos < GRUB_ENV_SIZE && original[pos] != '#') {
        size_t start = pos, equal, end, name_size;
        while (pos < GRUB_ENV_SIZE && original[pos] != '\n') {
            if (!original[pos] || original[pos] == '\r')
                return fail(error, error_size, "Invalid byte in environment variable.");
            pos++;
        }
        if (pos == GRUB_ENV_SIZE)
            return fail(error, error_size, "Unterminated environment variable.");
        end = pos++;
        equal = start;
        while (equal < end && original[equal] != '=') equal++;
        if (equal == start || equal == end || count >= GRUB_ENV_SIZE / 3)
            return fail(error, error_size, "Malformed environment variable.");
        name_size = equal - start;
        for (i = 0; i < count; i++) {
            if (lengths[i] == name_size && !memcmp(original + starts[i], original + start, name_size))
                return fail(error, error_size, "Duplicate environment variable.");
        }
        starts[count] = start; lengths[count++] = name_size;
        if (name_size == 10 && !memcmp(original + start, "next_entry", 10)) {
            result.cleared_next_entry = 1;
            continue;
        }
        if (name_size == 16 && !memcmp(original + start, "prev_saved_entry", 16)) {
            result.cleared_prev_saved_entry = 1;
            continue;
        }
        if (name_size == 11 && !memcmp(original + start, "saved_entry", 11)) {
            size_t value_size = end - equal - 1;
            memcpy(result.current, original + equal + 1, value_size);
            result.current[value_size] = 0;
            have_saved = 1;
            if (used + 12 + entry_size + 1 > GRUB_ENV_SIZE)
                return fail(error, error_size, "New menu entry does not fit in grubenv.");
            memcpy(env + used, "saved_entry=", 12); used += 12;
            memcpy(env + used, entry, entry_size); used += entry_size;
            env[used++] = '\n';
        } else {
            size_t size = pos - start;
            if (used + size > GRUB_ENV_SIZE)
                return fail(error, error_size, "Environment does not fit in grubenv.");
            memcpy(env + used, original + start, size); used += size;
        }
    }
    for (; pos < GRUB_ENV_SIZE; pos++)
        if (original[pos] != '#') return fail(error, error_size, "Invalid environment padding.");
    if (!have_saved) {
        if (used + 12 + entry_size + 1 > GRUB_ENV_SIZE)
            return fail(error, error_size, "New menu entry does not fit in grubenv.");
        memcpy(env + used, "saved_entry=", 12); used += 12;
        memcpy(env + used, entry, entry_size); used += entry_size;
        env[used++] = '\n';
    }
    result.changed = memcmp(original, env, GRUB_ENV_SIZE) != 0;
    memcpy(replacement, original, block_size);
    memcpy(replacement, env, GRUB_ENV_SIZE);
    *change = result;
    return 1;
}

int grub_print_default(const unsigned char *original, size_t size, FILE *out,
                       char *error, size_t error_size)
{
    const char *names[] = {"saved_entry", "next_entry", "prev_saved_entry"};
    unsigned char scratch[GRUB_ENV_SIZE];
    struct grub_change change;
    size_t i;
    int next_nonempty = 0;
    if (size != GRUB_ENV_SIZE)
        return fail(error, error_size, "Expected a 1024-byte GRUB environment.");
    /* Reuse the editor's strict validation; the proposed change stays in RAM. */
    if (!grub_prepare(original, size, "0", scratch, &change, error, error_size)) return 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t pos = 0, name_size = strlen(names[i]);
        int found = 0;
        if (!memcmp(original, "# GRUB Environment Block\n", 25)) pos = 25;
        fprintf(out, "%s=", names[i]);
        while (pos < size && original[pos] != '#') {
            size_t end = pos;
            while (end < size && original[end] != '\n') end++;
            if (end - pos > name_size && !memcmp(original + pos, names[i], name_size) && original[pos + name_size] == '=') {
                size_t value = pos + name_size + 1;
                found = 1;
                if (i == 1) next_nonempty = value != end;
                if (value == end) fputs("(empty)", out);
                for (; value < end; value++) {
                    unsigned char c = original[value];
                    if (c < 32 || c == 127) fprintf(out, "\\x%02x", c);
                    else fputc(c, out);
                }
                break;
            }
            pos = end + 1;
        }
        if (!found) fputs("(unset)", out);
        fputc('\n', out);
    }
    if (next_nonempty) fputs("A nonempty next_entry can override saved_entry for the next boot.\n", out);
    fputs("Stored values only; grub.cfg determines how they are used. No writes performed.\n", out);
    if (ferror(out)) return fail(error, error_size, "Could not write default listing.");
    return 1;
}

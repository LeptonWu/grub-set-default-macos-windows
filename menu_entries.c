#include "menu_entries.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 64
#define PATH_SIZE 1024
/* Tokens retain unexpanded variables but strip shell quoting/escaping. */
enum kind { END, WORD, SEP, OPEN, CLOSE, BAD };
struct lexer {
    const unsigned char *data;
    size_t size, pos, line;
    char *text;
    int dynamic, command_word;
};
static enum kind token(struct lexer *l)
{
    size_t used = 0;
    int quote = 0;
    l->dynamic = 0; l->command_word = 1;
    while (l->pos < l->size) {
        unsigned char c = l->data[l->pos];
        if (c == ' ' || c == '\t' || c == '\r') { l->pos++; continue; }
        if (c == '#') { while (l->pos < l->size && l->data[l->pos] != '\n') l->pos++; continue; }
        if (c == '\n' || c == ';') { l->pos++; l->line += c == '\n'; return SEP; }
        if (c == '{' || c == '}') { l->pos++; return c == '{' ? OPEN : CLOSE; }
        break;
    }
    if (l->pos == l->size) return END;
    while (l->pos < l->size) {
        unsigned char c = l->data[l->pos];
        if (!c) return BAD;
        if (!quote && (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' || c == '{' || c == '}')) break;
        l->pos++;
        if (c == '\\' && quote != '\'') {
            unsigned char next;
            if (l->pos == l->size) return BAD;
            next = l->data[l->pos++];
            l->command_word = 0;
            if (next == '\n') { l->line++; continue; }
            if (!next) return BAD;
            if (quote == '"' && next != '$' && next != '"' && next != '\\') l->text[used++] = '\\';
            l->text[used++] = (char)next;
            continue;
        }
        if (c == '\'' || c == '"') {
            if (!quote) { quote = c; l->command_word = 0; continue; }
            if (quote == c) { quote = 0; continue; }
        }
        l->line += c == '\n';
        if (c == '$' && quote != '\'') {
            l->dynamic = 1;
            /* Braces within ${variables} are not menu body delimiters. */
            if (!quote && l->pos < l->size && l->data[l->pos] == '{') {
                int nesting = 1;
                l->text[used++] = '$'; l->text[used++] = '{'; l->pos++;
                while (l->pos < l->size && nesting) {
                    c = l->data[l->pos++];
                    if (!c) return BAD;
                    nesting += c == '{'; nesting -= c == '}';
                    l->line += c == '\n'; l->text[used++] = (char)c;
                }
                if (nesting) return BAD;
                continue;
            }
        }
        l->text[used++] = (char)c;
    }
    if (quote) return BAD;
    l->text[used] = 0;
    return WORD;
}
static char *copy_text(const char *text)
{
    char *copy = malloc(strlen(text) + 1);
    if (copy) strcpy(copy, text);
    return copy;
}
static void quoted(FILE *out, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    fputc('"', out);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
        else if (*p < 32 || *p == 127) fprintf(out, "\\x%02x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}
/* Consume a declaration header through its opening brace. */
static int header(struct lexer *l, char **title, char **id, int *dynamic)
{
    enum kind k;
    int expect = 0; /* 1: ID, 2: another option's argument */
    *title = NULL; *id = NULL; *dynamic = 0;
    while ((k = token(l)) != OPEN) {
        const char *text;
        if (k == SEP) continue; /* Permit a brace/header on following lines. */
        if (k != WORD) return 0;
        text = l->text;
        *dynamic |= l->dynamic;
        if (expect) {
            if (expect == 1) { free(*id); *id = copy_text(text); if (!*id) return 0; }
            expect = 0;
        } else if (!strcmp(text, "--id") || !strcmp(text, "$menuentry_id_option") || !strcmp(text, "${menuentry_id_option}")) expect = 1;
        else if (!strncmp(text, "--id=", 5)) { free(*id); *id = copy_text(text + 5); if (!*id) return 0; }
        else if (!strcmp(text, "--class") || !strcmp(text, "--users") || !strcmp(text, "--hotkey")) expect = 2;
        else if (!strncmp(text, "--", 2)) continue;
        else if (!*title) { *title = copy_text(text); if (!*title) return 0; }
    }
    return *title != NULL && !expect;
}
int grub_print_entries(const unsigned char *data, size_t size, FILE *out,
                        char *error, size_t error_size)
{
    struct context { char path[PATH_SIZE]; size_t next, depth; } contexts[MAX_DEPTH];
    struct lexer l;
    size_t depth = 0, level = 0, entry_body = 0, entries = 0, submenus = 0;
    int start = 1, ok = 0;
    enum kind k;
    const char *message = "Malformed or unsupported declaration, quote, or brace nesting";
    char *title = NULL, *id = NULL;
    if (size > 4 * 1024 * 1024 || memchr(data, 0, size)) {
        snprintf(error, error_size, "Configuration is too large or contains NUL bytes."); return 0;
    }
    memset(contexts, 0, sizeof(contexts)); memset(&l, 0, sizeof(l));
    l.data = data; l.size = size; l.line = 1; l.text = malloc(size + 1);
    if (!l.text) { snprintf(error, error_size, "Out of memory."); return 0; }
    fputs("Static definitions only: conditions, variables and included files are not evaluated.\n"
          "Index paths below are provisional, based on definitions found in this file.\n", out);
    while ((k = token(&l)) != END) {
        if (k == BAD) goto done;
        if (k == SEP) { start = 1; continue; }
        if (k == OPEN) { if (++depth >= MAX_DEPTH) goto done; start = 1; continue; }
        if (k == CLOSE) {
            if (!depth) goto done;
            if (entry_body == depth) entry_body = 0;
            if (level && contexts[level].depth == depth) level--;
            depth--; start = 1; continue;
        }
        if (start && !entry_body && l.command_word &&
            (!strcmp(l.text, "menuentry") || !strcmp(l.text, "submenu"))) {
            int submenu = !strcmp(l.text, "submenu"), dynamic;
            size_t line = l.line;
            char path[PATH_SIZE];
            int length;
            if (!header(&l, &title, &id, &dynamic)) goto done;
            if (++depth >= MAX_DEPTH) goto done;
            if (level) length = snprintf(path, sizeof(path), "%s>%zu", contexts[level].path, contexts[level].next++);
            else length = snprintf(path, sizeof(path), "%zu", contexts[0].next++);
            if (length < 0 || length >= (int)sizeof(path)) goto done;
            fprintf(out, "%s  %s  title=", path, submenu ? "submenu" : "entry"); quoted(out, title);
            if (id) { fputs("  id=", out); quoted(out, id); }
            fprintf(out, "  line=%zu%s\n", line, dynamic ? "  [contains variables; not evaluated]" : "");
            if (submenu) {
                if (++level >= MAX_DEPTH) goto done;
                strcpy(contexts[level].path, path); contexts[level].next = 0; contexts[level].depth = depth;
                submenus++;
            } else { entries++; entry_body = depth; }
            free(title); free(id); title = id = NULL;
            start = 1;
        } else {
            start = start && l.command_word && (!strcmp(l.text, "then") || !strcmp(l.text, "else") || !strcmp(l.text, "do"));
        }
    }
    if (depth) goto done;
    fprintf(out, "Found %zu menu entries and %zu submenus. No writes performed.\n", entries, submenus);
    ok = 1;
done:
    if (!ok) snprintf(error, error_size, "%s near line %zu; listing may be incomplete.", message, l.line);
    free(title); free(id); free(l.text);
    if (ferror(out)) { snprintf(error, error_size, "Could not write menu listing."); return 0; }
    return ok;
}

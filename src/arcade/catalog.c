/* Builds the menu's game list from the drivers compiled into the module.
 * No libc: links into the Melis module and the host tests alike. */
#include "x6_catalog.h"

static uint32_t copy_span(char *out, uint32_t capacity, const char *in, uint32_t length)
{
    uint32_t i = 0;
    if (!capacity) return 0;
    for (; in && i < length && in[i] && i + 1 < capacity; ++i) out[i] = in[i];
    out[i] = 0;
    return i;
}

static uint32_t length_of(const char *text)
{
    uint32_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

void x6_catalog_split_title(const char *description, char *title, uint32_t title_capacity,
                            char *variant, uint32_t variant_capacity)
{
    uint32_t length = length_of(description), open = length;
    if (length && description[length - 1] == ')') {
        uint32_t depth = 0;
        for (uint32_t i = length; i-- > 0;) {
            if (description[i] == ')') ++depth;
            else if (description[i] == '(' && --depth == 0) {open = i;break;}
        }
    }
    uint32_t title_end = open;
    while (title_end > 0 && description[title_end - 1] == ' ') --title_end;
    if (!title_end) {title_end = length;open = length;}  /* "(x)" alone stays the title */
    copy_span(title, title_capacity, description, title_end);
    if (open < length) copy_span(variant, variant_capacity, description + open + 1, length - open - 2);
    else copy_span(variant, variant_capacity, "", 0);
}

static char lower(char c) {return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;}

static int32_t compare(const struct x6_game *a, const struct x6_game *b)
{
    for (uint32_t i = 0;; ++i) {
        char x = lower(a->title[i]), y = lower(b->title[i]);
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    for (uint32_t i = 0;; ++i) {
        if (a->id[i] != b->id[i]) return (unsigned char)a->id[i] < (unsigned char)b->id[i] ? -1 : 1;
        if (!a->id[i]) return 0;
    }
}

uint32_t x6_catalog_build(struct x6_game *out, uint32_t capacity, const char *rom_directory)
{
    uint32_t count = 0, total = x6_core_driver_count();
    char path[256];
    uint32_t base = copy_span(path, sizeof(path), rom_directory, sizeof(path));
    for (uint32_t i = 0; i < total && count < capacity; ++i) {
        struct x6_driver_info d = {0};
        if (!x6_core_driver(i, &d) || d.clone || !d.name || !d.name[0]) continue;
        uint32_t name_length = length_of(d.name);
        if (name_length >= sizeof(out->id)) continue;
        struct x6_game *g = &out[count];
        *g = (struct x6_game){0};
        copy_span(g->id, sizeof(g->id), d.name, name_length);
        x6_catalog_split_title(d.description ? d.description : d.name, g->title, sizeof(g->title),
                               g->variant, sizeof(g->variant));
        copy_span(g->maker, sizeof(g->maker), d.maker, sizeof(g->maker));
        copy_span(g->year, sizeof(g->year), d.year, sizeof(g->year));
        g->vertical = d.vertical;g->imperfect = d.imperfect;
        if (base && base + 1 + name_length + 4 < sizeof(path)) {
            uint32_t n = base;
            path[n++] = '/';
            for (uint32_t k = 0; k < name_length; ++k) path[n++] = d.name[k];
            path[n++] = '.';path[n++] = 'z';path[n++] = 'i';path[n++] = 'p';path[n] = 0;
            g->available = x6_file_size(path) > 0;
            path[base] = 0;
        }
        /* Insertion sort keeps the list ordered without libc. */
        uint32_t j = count;
        struct x6_game item = *g;
        while (j > 0 && compare(&item, &out[j - 1]) < 0) {out[j] = out[j - 1];--j;}
        out[j] = item;
        ++count;
    }
    return count;
}

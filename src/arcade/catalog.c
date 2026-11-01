/* Builds the menu's game list from the drivers compiled into the module.
 * No libc: links into the Melis module and the host tests alike. */
#include "x6_catalog.h"
#include "x6_rom_file.h"

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
    if (!out || !capacity) return 0;
    uint32_t count = 0, total = x6_core_driver_count();
    char path[256];
    for (uint32_t i = 0; i < total && count < capacity; ++i) {
        struct x6_driver_info d = {0};
        if (!x6_core_driver(i, &d) || d.clone || !d.name || !d.name[0]) continue;
        uint32_t name_length = length_of(d.name);
        if (name_length >= sizeof(out->id)) continue;
        /* The compiled driver pool is independent of the initial image's ROM
         * selection. Discover only installed supported files at every load. */
        const char *directories[2] = {"F:/roms/mame", rom_directory};
        uint32_t available = 0;
        for (uint32_t root = 0; root < 2 && !available; ++root) {
            uint32_t base = copy_span(path, sizeof path, directories[root], sizeof path);
            if (!base || base + 1 + name_length + 4 >= sizeof path) continue;
            uint32_t n = base;path[n++] = '/';
            n += copy_span(path + n, sizeof path - n, d.name, name_length);
            copy_span(path + n, sizeof path - n, ".zip", 4);
            available = x6_file_size(path) > 0;
        }
        if (!available) continue;
        struct x6_game *g = &out[count];
        *g = (struct x6_game){0};
        copy_span(g->id, sizeof(g->id), d.name, name_length);
        x6_catalog_split_title(d.description ? d.description : d.name, g->title, sizeof(g->title),
                               g->variant, sizeof(g->variant));
        copy_span(g->maker, sizeof(g->maker), d.maker, sizeof(g->maker));
        copy_span(g->year, sizeof(g->year), d.year, sizeof(g->year));
        g->vertical = d.vertical;g->imperfect = d.imperfect;
        g->available = 1;copy_span(g->path, sizeof g->path, path, sizeof path);
        /* Insertion sort keeps the list ordered without libc. */
        uint32_t j = count;
        struct x6_game item = *g;
        while (j > 0 && compare(&item, &out[j - 1]) < 0) {out[j] = out[j - 1];--j;}
        out[j] = item;
        ++count;
    }
    return count;
}

struct scan {
    struct x6_game *out;
    uint32_t count, capacity;
    const char *directory;
};

static const char *filename(const char *path)
{
    const char *name = path;
    while (*path) {if (*path == '/' || *path == '\\') name = path + 1;++path;}
    return name;
}

static int32_t extra_file(const struct x6_directory_entry *e, void *context)
{
    struct scan *s = context;
    uint32_t stem;
    unsigned type = x6_rom_type(e->name, &stem);
    if (!type || s->count == s->capacity) return 0;
    for (uint32_t i = 0; i < s->count; ++i)
        if (x6_rom_equal(filename(s->out[i].path), e->name)) return 0;
    char path[256];
    uint32_t base = length_of(s->directory), name_length = length_of(e->name);
    if (!base || base + 1 + name_length >= sizeof path) return 0;
    copy_span(path, sizeof path, s->directory, base);path[base++] = '/';
    copy_span(path + base, sizeof path - base, e->name, name_length);
    struct x6_game *g = &s->out[s->count++];
    *g = (struct x6_game){0};
    copy_span(g->id, sizeof g->id, e->name, stem);
    copy_span(g->title, sizeof g->title, e->name, stem);
    copy_span(g->path, sizeof g->path, path, sizeof path);
    g->available = (!e->size_low && !e->size_high) ? X6_GAME_INVALID
        : type == X6_ROM_ZIP ? X6_GAME_UNSUPPORTED : X6_GAME_FORMAT;
    copy_span(g->maker, sizeof g->maker, "MAME Arcade", sizeof g->maker);
    if (type == X6_ROM_ZIP && g->available != X6_GAME_INVALID && stem < sizeof g->id) {
        for (uint32_t i = 0; i < x6_core_driver_count(); ++i) {
            struct x6_driver_info d = {0};
            if (!x6_core_driver(i, &d) || d.clone || !d.name || !x6_rom_equal(g->id, d.name)) continue;
            copy_span(g->id, sizeof g->id, d.name, sizeof g->id);
            x6_catalog_split_title(d.description ? d.description : d.name, g->title, sizeof g->title,
                g->variant, sizeof g->variant);
            copy_span(g->maker, sizeof g->maker, d.maker, sizeof g->maker);
            copy_span(g->year, sizeof g->year, d.year, sizeof g->year);
            g->vertical = d.vertical;g->imperfect = d.imperfect;g->available = X6_GAME_READY;
            break;
        }
    }
    return 0;
}

uint32_t x6_catalog_scan(struct x6_game *out, uint32_t capacity, const char *rom_directory,
                         x6_directory_walker walk)
{
    struct scan s = {out, x6_catalog_build(out, capacity, rom_directory), capacity, 0};
    if (!out || !capacity || !walk) return s.count;
    const char *directories[] = {"F:/roms/mame", rom_directory};
    for (uint32_t i = 0; i < 2; ++i) {
        if (!directories[i] || (i && x6_rom_equal(directories[0], directories[1]))) continue;
        s.directory = directories[i];uint32_t before = s.count;
        int32_t result = walk(s.directory, extra_file, &s);
        if (result < 0) s.count = before;
        if (result == -4) break;
    }
    for (uint32_t i = 1; i < s.count; ++i) {
        struct x6_game item = out[i];uint32_t at = i;
        while (at && compare(&item, &out[at - 1]) < 0) {out[at] = out[at - 1];--at;}
        out[at] = item;
    }
    return s.count;
}

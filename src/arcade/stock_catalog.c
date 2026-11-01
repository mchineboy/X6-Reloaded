/* Discover original-FBA games from SD; ROM contents remain external. */
#include "x6_directory.h"
#include "x6_stock_catalog.h"
#include "x6_runtime.h"
#include "x6_rom_file.h"

static char lower(char c) {return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;}
static int32_t compare(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) {++a;++b;}
    return (unsigned char)lower(*a) - (unsigned char)lower(*b);
}
static void copy(char *out, uint32_t size, const char *in)
{
    uint32_t n = 0;
    while (in[n] && n + 1 < size) {out[n] = in[n];++n;}
    out[n] = 0;
}
struct scan {struct x6_game *out;uint32_t capacity, count;const char *directory;uint32_t zip;};
static uint32_t identify(const char *path, const char *name, uint32_t stem, uint32_t type, char *id)
{
    if (type != X6_ROM_ZIP && type != X6_ROM_FBA) return X6_GAME_FORMAT;
    uint8_t header[16];
    void *f = x6_fs_open(path, "rb");
    if (!f) return X6_GAME_INVALID;
    uint32_t n = x6_fs_read(header, 1, sizeof header, f);
    int32_t closed = x6_fs_close(f);
    if (closed || n != sizeof header) return X6_GAME_INVALID;
    if (type == X6_ROM_FBA) {
        if (header[0] != 0x71 || header[1] != 0x96 || header[2] != 2 || header[3]) return X6_GAME_INVALID;
        uint32_t i;
        for (i = 0; i < 12 && header[i + 4]; ++i) {
            char c = lower((char)header[i + 4]);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return X6_GAME_INVALID;
            id[i] = c;
        }
        if (!i || i == 12) return X6_GAME_INVALID;
    } else {
        if (header[0] != 'P' || header[1] != 'K' || header[2] != 3 || header[3] != 4) return X6_GAME_INVALID;
        if (stem >= 16) return X6_GAME_FORMAT;
        for (uint32_t i = 0; i < stem; ++i) {
            char c = lower(name[i]);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return X6_GAME_FORMAT;
            id[i] = c;
        }
    }
    return X6_GAME_READY;
}

static int32_t found(const struct x6_directory_entry *e, void *context)
{
    struct scan *s = context;
    uint32_t stem, type = x6_rom_type(e->name, &stem);
    if (!type || (!s->zip && type != X6_ROM_FBA)) return 0;
    char path[256];
    if (!x6_directory_join(path, sizeof path, s->directory, e->name)) return 0;
    /* FAT is case-insensitive; the original engine's extension/ZIP-driver
       parser is not assumed to be. Keep display names, normalize launch names. */
    uint32_t path_length = 0;
    while (path[path_length]) ++path_length;
    uint32_t name_length = 0;
    while (e->name[name_length]) ++name_length;
    uint32_t normalized = type == X6_ROM_FBA ? name_length - stem : name_length;
    for (uint32_t i = path_length - normalized; i < path_length; ++i) path[i] = lower(path[i]);
    char id[16] = {0};
    uint32_t state = (!e->size_low && !e->size_high) ? X6_GAME_INVALID
        : identify(path, e->name, stem, type, id);
    if (state == X6_GAME_READY && (!compare(id, "neogeo") || !compare(id, "pgm"))) return 0;
    /* Earlier folders win; a ROM copied into the new folder replaces a legacy
       listing without adding a duplicate. Packed and ZIP variants share an ID. */
    for (uint32_t i = 0; i < s->count; ++i) {
        const struct x6_game *g = &s->out[i];
        const char *name = g->path;
        for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
        if (!compare(name, e->name) || (state == X6_GAME_READY && g->available == X6_GAME_READY
            && !compare(g->id, id))) return 0;
    }
    if (s->count >= s->capacity) return 1;
    struct x6_game *g = &s->out[s->count++];
    *g = (struct x6_game){.available = (uint8_t)state};
    if (state != X6_GAME_READY) {
        uint32_t n = stem < sizeof id - 1 ? stem : sizeof id - 1;
        for (uint32_t i = 0; i < n; ++i) id[i] = lower(e->name[i]);
        id[n] = 0;
    }
    copy(g->id, sizeof g->id, id);copy(g->path, sizeof g->path, path);
    uint32_t title_size = stem < sizeof g->title - 1 ? stem : sizeof g->title - 1;
    for (uint32_t i = 0; i < title_size; ++i) g->title[i] = e->name[i];
    g->title[title_size] = 0;
    if (state == X6_GAME_READY && !compare(id, "2020bb")) copy(g->title, sizeof g->title, "2020 Super Baseball");
    copy(g->maker, sizeof g->maker, "Original Arcade");
    if (state == X6_GAME_READY) copy(g->variant, sizeof g->variant, "ROM set unverified");
    return s->count == s->capacity ? 1 : 0;
}

uint32_t x6_stock_catalog_build(struct x6_game *out, uint32_t capacity)
{
    if (!out || !capacity) return 0;
    struct scan s = {out, capacity, 0, 0, 0};
    const char *directories[] = {"F:/roms/original", "F:/game/mame", "F:/x6/stock"};
    for (uint32_t i = 0; i < 3 && s.count < capacity; ++i) {
        s.directory = directories[i];s.zip = i == 0;
        uint32_t before = s.count;
        int32_t result = x6_directory_walk(s.directory, found, &s);
        if (result < 0) s.count = before;
        if (result == -4) break; /* Do not accumulate uncertain directory handles. */
    }
    for (uint32_t i = 1; i < s.count; ++i) {
        struct x6_game item = out[i];uint32_t at = i;
        while (at && compare(out[at - 1].title, item.title) > 0) {out[at] = out[at - 1];--at;}
        out[at] = item;
    }
    return s.count;
}

uint32_t x6_stock_catalog_legacy(const struct x6_game *games, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        if (games[i].available == X6_GAME_READY && !compare(games[i].id, "2020bb")) return i;
    return count;
}

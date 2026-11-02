#include "x6_titles.h"
#include "x6_runtime.h"
#include "x6_rom_file.h"
static char arcade[131072], overrides[524288];
struct alias {uint32_t hash, offset;};
static struct alias aliases[16384];
static uint32_t hash(const char *p)
{
    uint32_t h = 2166136261u;
    while (*p && *p != '\t' && *p != '\n' && *p != '\r') {
        h = (h ^ (uint8_t)x6_rom_lower(*p == '\\' ? '/' : *p)) * 16777619u;++p;
    }
    return h;
}
static void copy(char *out, uint32_t cap, const char *in, uint32_t length)
{
    uint32_t i = 0;
    while (i < length && in[i] && i + 1 < cap) {out[i] = in[i];++i;}
    out[i] = 0;
}
static uint32_t load(char *out, uint32_t cap, const char *path)
{
    out[0] = 0;void *f = x6_fs_open(path, "rb");if (!f) return 0;
    uint32_t n = x6_fs_read(out, 1, cap - 1, f);char extra;
    uint32_t more = x6_fs_read(&extra, 1, 1, f);int32_t closed = x6_fs_close(f);
    if (more || closed) {out[0] = 0;return 1;}
    out[n] = 0;return 0;
}
uint32_t x6_titles_load(void)
{
    uint32_t status = load(arcade, sizeof arcade, "F:/x6/stock/arcade-titles.tsv");
    status |= load(overrides, sizeof overrides, "F:/x6/titles.tsv");
    for (uint32_t i = 0; i < 16384; ++i) aliases[i] = (struct alias){0};
    uint32_t count = 0;
    for (uint32_t at = 0; overrides[at];) {
        uint32_t start = at;while (overrides[at] && overrides[at] != '\n') ++at;
        if (overrides[start] != '#' && overrides[start] != '\r' && overrides[start] != '\n') {
            if (++count > 8192) {status = 1;break;}
            uint32_t h = hash(overrides + start), slot = h & 16383u;
            while (aliases[slot].offset) slot = (slot + 1) & 16383u;
            aliases[slot] = (struct alias){h, start + 1};
        }
        if (overrides[at]) ++at;
    }
    return status;
}
static void lookup(const char *data, const char *key, struct x6_game *g, uint32_t extra)
{
    while (*data) {
        const char *line = data;while (*data && *data != '\n') ++data;
        const char *end = data;if (*data) ++data;
        if (*line == '#') continue;
        const char *p = line,*k = key;
        while (p < end && *p != '\t' && *k && x6_rom_lower(*p == '\\' ? '/' : *p) == x6_rom_lower(*k == '\\' ? '/' : *k)) {++p;++k;}
        if (*k || p == end || *p != '\t') continue;
        ++p;const char *title = p;while (p < end && *p != '\t' && *p != '\r') ++p;
        if (p == title) continue;
        copy(g->title, sizeof g->title, title, (uint32_t)(p - title));
        if (extra && p < end && *p == '\t') {
            const char *year = ++p;while (p < end && *p != '\t' && *p != '\r') ++p;
            copy(g->year, sizeof g->year, year, (uint32_t)(p - year));
            if (p < end && *p == '\t') {const char *maker = ++p;while (p < end && *p != '\r') ++p;
                copy(g->maker, sizeof g->maker, maker, (uint32_t)(p - maker));}
        }
        return;
    }
}
void x6_titles_apply(struct x6_game *g, uint32_t library)
{
    if (library == X6_LIBRARY_STOCK && g->available == X6_GAME_READY) lookup(arcade, g->id, g, 1);
    const char *key = g->path;
    if (key[0] && key[1] == ':' && (key[2] == '/' || key[2] == '\\')) key += 3;
    uint32_t h = hash(key), slot = h & 16383u;
    for (uint32_t i = 0; i < 16384 && aliases[slot].offset; ++i, slot = (slot + 1) & 16383u) {
        if (aliases[slot].hash != h) continue;
        const char *row = overrides + aliases[slot].offset - 1,*p = row,*k = key;
        while (*k && *p && *p != '\t' && x6_rom_lower(*p == '\\' ? '/' : *p) == x6_rom_lower(*k == '\\' ? '/' : *k)) {++p;++k;}
        if (!*k && *p == '\t') {lookup(row, key, g, 0);break;}
    }
}
void x6_titles_filename(struct x6_game *g, const char *name)
{
    uint32_t n = 0, dot = 0;
    while (name[n]) {if (name[n] == '.') dot = n;++n;}
    if (!dot) dot = n;
    uint32_t start = 0;while (start < dot && name[start] >= '0' && name[start] <= '9') ++start;
    if (start && start < dot && (name[start] == '.' || name[start] == '-' || name[start] == ' ' || name[start] == '_')) {
        while (start < dot && (name[start] == '.' || name[start] == '-' || name[start] == ' ' || name[start] == '_')) ++start;
        if (start == dot) start = 0;
    } else start = 0;
    copy(g->title, sizeof g->title, name + start, dot - start);
    for (uint32_t i = 0; g->title[i]; ++i) if (g->title[i] == '_') g->title[i] = ' ';
}
static uint32_t read_at(void *f, uint32_t offset, uint8_t *out, uint32_t size)
{
    return x6_fs_seek(f, (int32_t)offset, 0) == 0 && x6_fs_read(out, 1, size, f) == size;
}
static uint32_t text(struct x6_game *g, const uint8_t *bytes, uint32_t n)
{
    uint32_t end = 0, letters = 0;
    while (end < n && bytes[end]) {
        if (bytes[end] < 32 || bytes[end] > 126) return 0;
        if ((bytes[end] >= 'A' && bytes[end] <= 'Z') || (bytes[end] >= 'a' && bytes[end] <= 'z')) ++letters;
        ++end;
    }
    while (end && bytes[end - 1] == ' ') --end;
    if (!letters || !end) return 0;
    copy(g->title, sizeof g->title, (const char *)bytes, end);return 1;
}
uint32_t x6_titles_console(struct x6_game *g, uint32_t library, uint32_t size)
{
    void *f = x6_fs_open(g->path, "rb");if (!f) return X6_GAME_INVALID;
    uint8_t h[256];uint32_t valid = size != 0;
    if (library == X6_LIBRARY_NES) {
        valid = read_at(f, 0, h, 16) && h[0] == 'N' && h[1] == 'E' && h[2] == 'S' && h[3] == 0x1a;
        /* Standard iNES has no dependable title; use filename or TSV override. */
    } else if (library == X6_LIBRARY_GBA) {
        valid = read_at(f, 0xa0, h, 32) && h[18] == 0x96;
        if (valid) {text(g, h, 12);copy(g->id, sizeof g->id, (const char *)h + 12, 4);}
    } else if (library == X6_LIBRARY_GB || library == X6_LIBRARY_GBC) {
        valid = read_at(f, 0x134, h, 28);
        if (valid) text(g, h, (h[15] & 0x80u) ? 11u : 16u);
    } else if (library == X6_LIBRARY_SNES) {
        uint32_t base = size % 32768u == 512u ? 512u : 0;
        const uint32_t offsets[] = {0x7fc0, 0xffc0, 0x40ffc0};
        uint32_t best = 0;valid = size >= 32768u;
        for (uint32_t i = 0; i < 3; ++i) {
            if (offsets[i] + base + 32 > size || !read_at(f, offsets[i] + base, h, 32)) continue;
            struct x6_game candidate = {0};
            if (!text(&candidate, h, 21)) continue;
            uint32_t score = 1 + ((h[28] ^ h[30]) == 255 && (h[29] ^ h[31]) == 255 ? 4u : 0u);
            if (score > best) {best = score;copy(g->title, sizeof g->title, candidate.title, sizeof candidate.title);}
        }
    } else if (library == X6_LIBRARY_MD) {
        valid = size >= 512;
        if (read_at(f, 0x100, h, 4) && h[0] == 'S' && h[1] == 'E' && h[2] == 'G' && h[3] == 'A') {
            if (read_at(f, 0x150, h, 48) && !text(g, h, 48) && read_at(f, 0x120, h, 48)) text(g, h, 48);
        } else if (size % 16384u == 512u) {
            uint8_t odd[24], even[24];
            if (read_at(f, 512 + 8192 + 0x150 / 2, even, 24) && read_at(f, 512 + 0x150 / 2, odd, 24)) {
                for (uint32_t i = 0; i < 24; ++i) {h[i * 2] = even[i];h[i * 2 + 1] = odd[i];}
                text(g, h, 48);
            }
        }
    }
    if (x6_fs_close(f) != 0) valid = 0;
    return valid ? X6_GAME_READY : X6_GAME_INVALID;
}

/* V1.241017 directory entry layout traced in the original SD FBA module:
 * attributes +4, 64-bit size +8, NUL-terminated name +16. */
#include "x6_directory.h"

static uint32_t normalized(char *out, const char *path)
{
    if (!path || (path[0] != 'F' && path[0] != 'f') || path[1] != ':'
        || (path[2] != '/' && path[2] != '\\')) return 0;
    uint32_t start = 3;
    for (uint32_t i = 0; i < 256; ++i) {
        unsigned char c = (unsigned char)path[i];
        if ((c && c < 32) || (i != 1 && c == ':')) return 0;
        out[i] = c == '/' ? '\\' : (char)c;
        if (!c || c == '/' || c == '\\') {
            if (i >= start && i - start == 2 && path[start] == '.' && path[start + 1] == '.') return 0;
            start = i + 1;
        }
        if (!c) return i >= 3;
    }
    return 0;
}

uint32_t x6_directory_join(char *out, uint32_t capacity, const char *directory, const char *name)
{
    uint32_t n = 0;
    char base[256];
    if (!out || !capacity || !directory || !name || !name[0] || name[0] == '.') return 0;
    if (!normalized(base, directory)) return 0;
    for (; base[n] && n + 1 < capacity; ++n) out[n] = base[n];
    if (base[n] || !n) return 0;
    if (out[n - 1] != '/' && out[n - 1] != '\\') {
        if (n + 1 >= capacity) return 0;
        out[n++] = '\\';
    }
    for (uint32_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == ':' || c == '/' || c == '\\' || n + 1 >= capacity) return 0;
        out[n++] = (char)c;
    }
    out[n] = 0;return 1;
}

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int32_t x6_directory_walk(const char *path, x6_directory_visitor visitor, void *context)
{
    char directory[256];
    if (!visitor || !normalized(directory, path)) return -2;
    void *handle = x6_directory_open_raw(directory);
    if (!handle) return -1;
    int32_t result = 0;
    uint32_t count;
    for (count = 0; count < X6_DIRECTORY_LIMIT; ++count) {
        const uint8_t *raw = x6_directory_read_raw(handle);
        if (!raw) break;
        struct x6_directory_entry e;
        e.attributes = raw[4];e.size_low = word(raw + 8);e.size_high = word(raw + 12);
        uint32_t n;
        for (n = 0; n + 1 < sizeof e.name && raw[16 + n]; ++n) e.name[n] = (char)raw[16 + n];
        if (raw[16 + n]) continue; /* Too long: never truncate a launch path. */
        e.name[n] = 0;
        if (!n || e.name[0] == '.' || (e.attributes & 0x18u)) continue; /* hidden names, folders, volume labels */
        int32_t chosen = visitor(&e, context);
        if (chosen) {result = chosen < 0 ? chosen : 0;break;}
    }
    if (count == X6_DIRECTORY_LIMIT) result = -3;
    if (x6_directory_close_raw(handle) != 0) result = -4;
    return result;
}

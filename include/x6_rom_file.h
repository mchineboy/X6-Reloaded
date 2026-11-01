#ifndef X6_ROM_FILE_H
#define X6_ROM_FILE_H
#include <stdint.h>
/* ROM-looking files only: exclude instructions, saves and BIOS dependencies.
 * This classifies extensions, not archive contents or engine compatibility. */
static inline char x6_rom_lower(char c) {return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;}
static inline int x6_rom_equal(const char *a, const char *b)
{
    while (*a && x6_rom_lower(*a) == x6_rom_lower(*b)) {++a;++b;}
    return x6_rom_lower(*a) == x6_rom_lower(*b);
}
enum {X6_ROM_NONE, X6_ROM_ZIP, X6_ROM_FBA, X6_ROM_OTHER};
static inline uint32_t x6_rom_type(const char *name, uint32_t *stem)
{
    uint32_t n = 0, dot = 0;
    if (!name || !name[0] || name[0] == '.') return X6_ROM_NONE;
    while (name[n]) {
        unsigned char c = (unsigned char)name[n];
        if (c < 32 || c == '/' || c == '\\' || c == ':') return X6_ROM_NONE;
        if (c == '.') dot = n;
        ++n;
    }
    if (!dot || x6_rom_equal(name, "X6-ARCADE.zip") || x6_rom_equal(name, "neogeo.zip")
        || x6_rom_equal(name, "pgm.zip")) return X6_ROM_NONE;
    *stem = dot;
    const char *ext = name + dot;
    if (x6_rom_equal(ext, ".zip")) return X6_ROM_ZIP;
    if (x6_rom_equal(ext, ".fba")) return X6_ROM_FBA;
    if (x6_rom_equal(ext, ".7z") || x6_rom_equal(ext, ".rar")
        || x6_rom_equal(ext, ".bin") || x6_rom_equal(ext, ".rom")) return X6_ROM_OTHER;
    return X6_ROM_NONE;
}
#endif

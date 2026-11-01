#ifndef X6_CATALOG_H
#define X6_CATALOG_H
#include "x6_ui.h"
#include "x6_directory.h"

#define X6_CATALOG_MAX 512u

/* Discover installed files for the compiled driver pool (parents only).
 * New F:/roms/mame takes priority over the legacy rom_directory. Only nonempty
 * installed ZIPs are returned, sorted by title, with their actual path.
 * This checks supported filenames; it does not enable uncompiled drivers. */
uint32_t x6_catalog_build(struct x6_game *out, uint32_t capacity, const char *rom_directory);
/* A v5 host can enumerate extra ROM files as disabled entries. Older hosts
 * retain supported-filename probing through x6_catalog_build. */
uint32_t x6_catalog_scan(struct x6_game *out, uint32_t capacity, const char *rom_directory,
                         x6_directory_walker walk);

/* Split "Title (Variant)" into its two parts. Exposed for host tests. */
void x6_catalog_split_title(const char *description, char *title, uint32_t title_capacity,
                            char *variant, uint32_t variant_capacity);

/* One compiled driver, as reported by the emulator core. */
struct x6_driver_info {
    const char *name, *description, *year, *maker;
    uint8_t vertical, imperfect, clone;
};

/* Supplied by src/arcade/catalog_mame.c in the module, by doubles in tests. */
uint32_t x6_core_driver_count(void);
uint32_t x6_core_driver(uint32_t index, struct x6_driver_info *out);
int32_t x6_file_size(const char *path);

#endif

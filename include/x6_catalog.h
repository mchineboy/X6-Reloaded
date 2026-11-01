#ifndef X6_CATALOG_H
#define X6_CATALOG_H
#include "x6_ui.h"

#define X6_CATALOG_MAX 64u

/* Fill out[] from the drivers compiled into this module (parents only),
 * sorted by title. rom_directory uses '/' separators, without a trailing
 * slash; each entry is marked available when <rom_directory>/<id>.zip opens.
 * Returns the entry count. */
uint32_t x6_catalog_build(struct x6_game *out, uint32_t capacity, const char *rom_directory);

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

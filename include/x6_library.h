#ifndef X6_LIBRARY_H
#define X6_LIBRARY_H
#include "x6_ui.h"

enum x6_library {X6_LIBRARY_MAME, X6_LIBRARY_STOCK, X6_LIBRARY_NES, X6_LIBRARY_SNES,
    X6_LIBRARY_MD, X6_LIBRARY_GB, X6_LIBRARY_GBC, X6_LIBRARY_GBA, X6_LIBRARY_COUNT};
#define X6_LIBRARY_BUCKETS 5u
#define X6_LIBRARY_ALL X6_LIBRARY_BUCKETS
#define X6_LIBRARY_PAGE 128u
struct x6_library_provider {
    uint32_t (*stats)(uint32_t library, uint32_t buckets[X6_LIBRARY_BUCKETS]);
    int32_t (*page)(uint32_t library, uint32_t bucket, uint32_t offset,
        struct x6_game *out, uint32_t *indices, uint32_t capacity);
    int32_t (*position)(uint32_t library, uint32_t index, uint32_t *bucket, uint32_t *offset);
};
const char *x6_library_name(uint32_t library);
uint32_t x6_library_bucket(const char *title);

/* Host-only catalog. Metadata is cached on the card, with a small sort index
 * in RAM. Negative status means incomplete discovery, never silent truncation. */
int32_t x6_libraries_build(void);
void x6_libraries_progress(void (*callback)(uint32_t library, uint32_t count));
extern const struct x6_library_provider x6_libraries_provider;
int32_t x6_libraries_game(uint32_t library, uint32_t index, struct x6_game *out);
const char *x6_library_module(uint32_t library);
#endif

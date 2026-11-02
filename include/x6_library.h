#ifndef X6_LIBRARY_H
#define X6_LIBRARY_H
#include "x6_ui.h"

enum x6_library {X6_LIBRARY_MAME, X6_LIBRARY_STOCK, X6_LIBRARY_NES, X6_LIBRARY_SNES,
    X6_LIBRARY_MD, X6_LIBRARY_GB, X6_LIBRARY_GBC, X6_LIBRARY_GBA, X6_LIBRARY_COUNT};
#define X6_LIBRARY_BUCKETS 5u
#define X6_LIBRARY_ALL X6_LIBRARY_BUCKETS
#define X6_LIBRARY_PAGE 128u
enum x6_scan_phase {X6_SCAN_PREPARING, X6_SCAN_TITLES, X6_SCAN_OPENING,
    X6_SCAN_SCANNING, X6_SCAN_FINALIZING, X6_SCAN_SORTING, X6_SCAN_READY,
    X6_SCAN_FAILED, X6_SCAN_MENU, X6_SCAN_CACHED};
enum x6_scan_error {X6_SCAN_LIMIT = 1u, X6_SCAN_WRITE = 2u, X6_SCAN_PATH = 4u,
    X6_SCAN_TITLE = 8u, X6_SCAN_DIRECTORY = 16u, X6_SCAN_CLOSE_DIR = 32u,
    X6_SCAN_OPEN = 64u, X6_SCAN_CLOSE_FILE = 128u};
struct x6_scan_info {
    uint32_t phase, library, checked, indexed, errors;
    int32_t error_code;
    char directory[96], error_path[128];
    uint32_t log_status; /* v8: enum x6_scan_log_status; diagnostic failure is nonfatal */
    uint32_t cache_hit; /* v9: loaded a complete saved index without ROM discovery */
};
const char *x6_scan_error_message(const struct x6_scan_info *scan);
void x6_ui_draw_scan(struct x6_surface *surface, const struct x6_scan_info *scan);
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
/* Normal boot reuses a validated saved index; build() explicitly rescans. */
int32_t x6_libraries_start(void);
void x6_libraries_progress(void (*callback)(const struct x6_scan_info *));
const struct x6_scan_info *x6_libraries_scan_info(void);
extern const struct x6_library_provider x6_libraries_provider;
int32_t x6_libraries_game(uint32_t library, uint32_t index, struct x6_game *out);
const char *x6_library_module(uint32_t library);
#endif

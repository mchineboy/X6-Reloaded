#ifndef X6_BROWSER_H
#define X6_BROWSER_H
#include "x6_catalog.h"

#define X6_BROWSER_GROUP_AT 40u
#define X6_BROWSER_BUCKETS 5u
#define X6_BROWSER_ALL X6_BROWSER_BUCKETS
enum x6_library { X6_LIBRARY_MAME, X6_LIBRARY_STOCK, X6_LIBRARY_COUNT };
enum x6_browser_level { X6_BROWSER_ROOT, X6_BROWSER_GROUPS, X6_BROWSER_GAMES };
enum x6_browser_action { X6_BROWSER_NONE, X6_BROWSER_ENTER, X6_BROWSER_PLAY };

/* Views copy metadata only. map[] retains the source index so filtered row
 * numbers can never be confused with emulator-driver indices. */
struct x6_browser {
    const struct x6_game *games[X6_LIBRARY_COUNT];
    uint32_t totals[X6_LIBRARY_COUNT];
    struct x6_game entries[X6_CATALOG_MAX];
    uint32_t map[X6_CATALOG_MAX];
    uint32_t count, selected, library, bucket, level, root_selected;
    uint32_t group_selected[X6_LIBRARY_COUNT];
    uint32_t game_selected[X6_LIBRARY_COUNT][X6_BROWSER_BUCKETS + 1];
    char location[48];
};
void x6_browser_init(struct x6_browser *b, const struct x6_game *mame, uint32_t mame_count,
                       const struct x6_game *stock, uint32_t stock_count);
enum x6_browser_action x6_browser_choose(struct x6_browser *b, uint32_t selected, uint32_t *game_index);
uint32_t x6_browser_back(struct x6_browser *b, uint32_t selected);
/* Reopen the actual game's folder after leaving an emulator or pause screen. */
void x6_browser_focus(struct x6_browser *b, uint32_t library, uint32_t game_index);
#endif

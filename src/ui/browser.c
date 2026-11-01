/* Logical library folders. No OS, filesystem writes or emulator calls. */
#include "x6_browser.h"

static const char *const names[X6_LIBRARY_COUNT] = {"MAME Arcade", "Original Arcade"};
static const char *const groups[X6_BROWSER_BUCKETS] = {"0-9", "A-F", "G-L", "M-R", "S-Z"};

static void copy(char *out, uint32_t capacity, const char *text)
{
    uint32_t n = 0;
    while (text && *text && n + 1 < capacity) out[n++] = *text++;
    if (capacity) out[n] = 0;
}

static uint32_t bucket_for(const char *title)
{
    uint32_t c = (unsigned char)title[0];
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'F') return 1;
    if (c >= 'G' && c <= 'L') return 2;
    if (c >= 'M' && c <= 'R') return 3;
    if (c >= 'S' && c <= 'Z') return 4;
    return 0; /* Digits, punctuation and non-ASCII titles. */
}

static void directory(struct x6_game *entry, const char *title, uint32_t count)
{
    *entry = (struct x6_game){.available = 1, .folder = 1};
    copy(entry->title, sizeof entry->title, title);
    char reverse[12];uint32_t n = 0, used = 0, value = count;
    do {reverse[n++] = (char)('0' + value % 10);value /= 10;} while (value);
    while (n) entry->year[used++] = reverse[--n];
    entry->year[used] = 0;
    copy(entry->maker, sizeof entry->maker, entry->year);
    while (entry->maker[used]) ++used;
    copy(entry->maker + used, sizeof entry->maker - used, count == 1 ? " game" : " games");
}

static uint32_t grouped(const struct x6_browser *b)
{
    return b->totals[b->library] > X6_BROWSER_GROUP_AT;
}

static void root(struct x6_browser *b)
{
    b->level = X6_BROWSER_ROOT;b->count = 0;
    copy(b->location, sizeof b->location, "Library");
    for (uint32_t i = 0; i < X6_LIBRARY_COUNT; ++i) {
        if (i == X6_LIBRARY_STOCK && !b->totals[i]) continue;
        b->map[b->count] = i;
        directory(&b->entries[b->count++], names[i], b->totals[i]);
    }
    b->selected = b->root_selected < b->count ? b->root_selected : 0;
}

static void list_games(struct x6_browser *b)
{
    b->level = X6_BROWSER_GAMES;b->count = 0;
    copy(b->location, sizeof b->location, names[b->library]);
    if (b->bucket < X6_BROWSER_BUCKETS) {
        uint32_t n = 0;
        while (b->location[n]) ++n;
        copy(b->location + n, sizeof b->location - n, " / ");
        n += 3;
        copy(b->location + n, sizeof b->location - n, groups[b->bucket]);
    }
    for (uint32_t i = 0; i < b->totals[b->library]; ++i) {
        const struct x6_game *g = &b->games[b->library][i];
        if (b->bucket < X6_BROWSER_BUCKETS && bucket_for(g->title) != b->bucket) continue;
        b->entries[b->count] = *g;b->map[b->count++] = i;
    }
    uint32_t selected = b->game_selected[b->library][b->bucket];
    b->selected = selected < b->count ? selected : 0;
}

static void list_groups(struct x6_browser *b)
{
    b->level = X6_BROWSER_GROUPS;b->count = 0;
    copy(b->location, sizeof b->location, names[b->library]);
    uint32_t counts[X6_BROWSER_BUCKETS] = {0};
    for (uint32_t i = 0; i < b->totals[b->library]; ++i)
        ++counts[bucket_for(b->games[b->library][i].title)];
    for (uint32_t i = 0; i < X6_BROWSER_BUCKETS; ++i) {
        if (!counts[i]) continue;
        b->map[b->count] = i;
        directory(&b->entries[b->count++], groups[i], counts[i]);
    }
    b->selected = b->group_selected[b->library] < b->count ? b->group_selected[b->library] : 0;
}

void x6_browser_init(struct x6_browser *b, const struct x6_game *mame, uint32_t mame_count,
                       const struct x6_game *stock, uint32_t stock_count)
{
    *b = (struct x6_browser){0};
    b->games[0] = mame;b->games[1] = stock;
    b->totals[0] = !mame ? 0 : mame_count > X6_CATALOG_MAX ? X6_CATALOG_MAX : mame_count;
    b->totals[1] = !stock ? 0 : stock_count > X6_CATALOG_MAX ? X6_CATALOG_MAX : stock_count;
    root(b);
}

enum x6_browser_action x6_browser_choose(struct x6_browser *b, uint32_t selected, uint32_t *game_index)
{
    if (selected >= b->count) return X6_BROWSER_NONE;
    if (b->level == X6_BROWSER_ROOT) {
        b->root_selected = selected;b->library = b->map[selected];
        if (grouped(b)) list_groups(b);
        else {b->bucket = X6_BROWSER_ALL;list_games(b);}
        return X6_BROWSER_ENTER;
    }
    if (b->level == X6_BROWSER_GROUPS) {
        b->group_selected[b->library] = selected;b->bucket = b->map[selected];
        list_games(b);return X6_BROWSER_ENTER;
    }
    if (!game_index || b->entries[selected].available != X6_GAME_READY) return X6_BROWSER_NONE;
    b->selected = selected;b->game_selected[b->library][b->bucket] = selected;
    *game_index = b->map[selected];
    return X6_BROWSER_PLAY;
}

uint32_t x6_browser_back(struct x6_browser *b, uint32_t selected)
{
    if (b->level == X6_BROWSER_ROOT) return 0;
    if (b->level == X6_BROWSER_GAMES) {
        b->game_selected[b->library][b->bucket] = selected;
        if (grouped(b)) {list_groups(b);return 1;}
    } else b->group_selected[b->library] = selected;
    root(b);return 1;
}

void x6_browser_focus(struct x6_browser *b, uint32_t library, uint32_t game_index)
{
    if (library >= X6_LIBRARY_COUNT || game_index >= b->totals[library]) {root(b);return;}
    b->library = library;b->root_selected = library;
    b->bucket = grouped(b) ? bucket_for(b->games[library][game_index].title) : X6_BROWSER_ALL;
    if (grouped(b)) {
        list_groups(b);
        for (uint32_t i = 0; i < b->count; ++i)
            if (b->map[i] == b->bucket) b->group_selected[library] = i;
    }
    list_games(b);
    for (uint32_t i = 0; i < b->count; ++i)
        if (b->map[i] == game_index) {b->selected = i;b->game_selected[library][b->bucket] = i;break;}
}

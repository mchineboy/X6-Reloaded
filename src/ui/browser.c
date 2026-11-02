/* Paged logical libraries. No OS, filesystem writes or emulator calls. */
#include "x6_browser.h"
static const char *const groups[X6_BROWSER_BUCKETS] = {"0-9", "A-F", "G-L", "M-R", "S-Z"};
static void copy(char *out, uint32_t capacity, const char *text)
{
    uint32_t n = 0;
    while (text && *text && n + 1 < capacity) out[n++] = *text++;
    if (capacity) out[n] = 0;
}
static void number(char *out, uint32_t value)
{
    char reverse[12];uint32_t n = 0, used = 0;
    do {reverse[n++] = (char)('0' + value % 10);value /= 10;} while (value);
    while (n) out[used++] = reverse[--n];out[used] = 0;
}
static void directory(struct x6_game *entry, const char *title, uint32_t count)
{
    *entry = (struct x6_game){.available = X6_GAME_READY, .folder = 1};
    copy(entry->title, sizeof entry->title, title);number(entry->year, count);
    copy(entry->maker, sizeof entry->maker, entry->year);
    uint32_t n = 0;while (entry->maker[n]) ++n;
    copy(entry->maker + n, sizeof entry->maker - n, count == 1 ? " game" : " games");
}
static uint32_t grouped(const struct x6_browser *b) {return b->totals[b->library] > X6_BROWSER_GROUP_AT;}
static uint32_t total(const struct x6_browser *b)
{
    return b->bucket < X6_BROWSER_BUCKETS ? b->buckets[b->library][b->bucket] : b->totals[b->library];
}
static void location(struct x6_browser *b)
{
    copy(b->location, sizeof b->location, x6_library_name(b->library));
    if (b->bucket < X6_BROWSER_BUCKETS) {
        uint32_t n = 0;while (b->location[n]) ++n;
        copy(b->location + n, sizeof b->location - n, " / ");n += 3;
        copy(b->location + n, sizeof b->location - n, groups[b->bucket]);
    }
}
static void root(struct x6_browser *b)
{
    b->level = X6_BROWSER_ROOT;b->count = 0;
    copy(b->location, sizeof b->location, "Library");
    for (uint32_t i = 0; i < X6_LIBRARY_COUNT; ++i) {
        if (i && !b->totals[i]) continue;
        b->map[b->count] = i;
        directory(&b->entries[b->count++], x6_library_name(i), b->totals[i]);
    }
    if (b->incomplete) {
        b->entries[b->count] = (struct x6_game){.title = "Library scan stopped",
            .id = "See SD guide", .available = X6_GAME_CATALOG};
        copy(b->entries[b->count].maker, sizeof b->entries[b->count].maker,
            b->error_reason[0] ? b->error_reason : "Library scan did not finish");
        copy(b->entries[b->count].path, sizeof b->entries[b->count].path, b->error_path);
        copy(b->entries[b->count].variant, sizeof b->entries[b->count].variant, b->error_code);
        b->map[b->count++] = X6_LIBRARY_COUNT;
    }
    b->selected = b->root_selected < b->count ? b->root_selected : 0;
}
void x6_browser_incomplete(struct x6_browser *b)
{
    b->incomplete = 1;if (b->level == X6_BROWSER_ROOT) root(b);
}
void x6_browser_error(struct x6_browser *b, const char *reason, const char *path, uint32_t code)
{
    copy(b->error_reason, sizeof b->error_reason, reason);
    copy(b->error_path, sizeof b->error_path, path);number(b->error_code, code);
    x6_browser_incomplete(b);
}
static void list_games(struct x6_browser *b)
{
    b->level = X6_BROWSER_GAMES;b->count = 0;b->read_error = 0;location(b);
    if (b->provider && b->library != X6_LIBRARY_MAME) {
        int32_t n = b->provider->page(b->library, b->bucket, b->offset, b->entries, b->map, X6_LIBRARY_PAGE);
        if (n > 0 && (uint32_t)n <= X6_LIBRARY_PAGE) b->count = (uint32_t)n;
        else if (n < 0) b->read_error = 1;
    } else {
        uint32_t matched = 0;
        for (uint32_t i = 0; i < b->totals[b->library] && b->count < X6_LIBRARY_PAGE; ++i) {
            const struct x6_game *g = &b->games[b->library][i];
            if (b->bucket < X6_BROWSER_BUCKETS && x6_library_bucket(g->title) != b->bucket) continue;
            if (matched++ < b->offset) continue;
            b->entries[b->count] = *g;b->map[b->count++] = i;
        }
    }
    uint32_t selected = b->game_selected[b->library][b->bucket];
    b->selected = selected < b->count ? selected : 0;
}
static void list_pages(struct x6_browser *b)
{
    b->level = X6_BROWSER_PAGES;b->count = 0;location(b);
    uint32_t n = total(b);
    for (uint32_t start = 0; start < n && b->count < X6_LIBRARY_PAGE; start += X6_LIBRARY_PAGE) {
        char title[40] = "Games ", value[12];uint32_t end = start + X6_LIBRARY_PAGE;
        if (end > n) end = n;
        number(value, start + 1);copy(title + 6, sizeof title - 6, value);
        uint32_t at = 0;while (title[at]) ++at;title[at++] = '-';
        number(value, end);copy(title + at, sizeof title - at, value);
        b->map[b->count] = start;directory(&b->entries[b->count++], title, end - start);
    }
    b->selected = b->page_selected[b->library][b->bucket];
    if (b->selected >= b->count) b->selected = 0;
}
static void open_bucket(struct x6_browser *b)
{
    if (total(b) > X6_LIBRARY_PAGE) list_pages(b);
    else {b->offset = 0;list_games(b);}
}
static void list_groups(struct x6_browser *b)
{
    b->level = X6_BROWSER_GROUPS;b->count = 0;
    copy(b->location, sizeof b->location, x6_library_name(b->library));
    for (uint32_t i = 0; i < X6_BROWSER_BUCKETS; ++i) {
        uint32_t n = b->buckets[b->library][i];if (!n) continue;
        b->map[b->count] = i;directory(&b->entries[b->count++], groups[i], n);
    }
    b->selected = b->group_selected[b->library] < b->count ? b->group_selected[b->library] : 0;
}
void x6_browser_init(struct x6_browser *b, const struct x6_game *mame, uint32_t mame_count,
                       const struct x6_game *stock, uint32_t stock_count)
{
    *b = (struct x6_browser){0};b->games[0] = mame;b->games[1] = stock;
    b->totals[0] = !mame ? 0 : mame_count > X6_CATALOG_MAX ? X6_CATALOG_MAX : mame_count;
    b->totals[1] = !stock ? 0 : stock_count > X6_CATALOG_MAX ? X6_CATALOG_MAX : stock_count;
    for (uint32_t lib = 0; lib < 2; ++lib)
        for (uint32_t i = 0; i < b->totals[lib]; ++i) ++b->buckets[lib][x6_library_bucket(b->games[lib][i].title)];
    root(b);
}
void x6_browser_init_provider(struct x6_browser *b, const struct x6_game *mame,
                               uint32_t count, const struct x6_library_provider *provider)
{
    x6_browser_init(b, mame, count, 0, 0);
    if (provider && provider->stats && provider->page && provider->position) {
        b->provider = provider;
        for (uint32_t lib = 1; lib < X6_LIBRARY_COUNT; ++lib) b->totals[lib] = provider->stats(lib, b->buckets[lib]);
    }
    root(b);
}
enum x6_browser_action x6_browser_choose(struct x6_browser *b, uint32_t selected, uint32_t *game_index)
{
    if (selected >= b->count || b->entries[selected].available != X6_GAME_READY) return X6_BROWSER_NONE;
    if (b->level == X6_BROWSER_ROOT) {
        b->root_selected = selected;b->library = b->map[selected];
        if (grouped(b)) list_groups(b);
        else {b->bucket = X6_BROWSER_ALL;open_bucket(b);}
        return X6_BROWSER_ENTER;
    }
    if (b->level == X6_BROWSER_GROUPS) {
        b->group_selected[b->library] = selected;b->bucket = b->map[selected];
        open_bucket(b);return X6_BROWSER_ENTER;
    }
    if (b->level == X6_BROWSER_PAGES) {
        b->page_selected[b->library][b->bucket] = selected;b->offset = b->map[selected];
        list_games(b);return X6_BROWSER_ENTER;
    }
    if (!game_index || b->entries[selected].available != X6_GAME_READY) return X6_BROWSER_NONE;
    b->selected = selected;b->game_selected[b->library][b->bucket] = selected;
    *game_index = b->map[selected];return X6_BROWSER_PLAY;
}
uint32_t x6_browser_back(struct x6_browser *b, uint32_t selected)
{
    if (b->level == X6_BROWSER_ROOT) return 0;
    if (b->level == X6_BROWSER_GAMES) {
        b->game_selected[b->library][b->bucket] = selected;
        if (total(b) > X6_LIBRARY_PAGE) {list_pages(b);return 1;}
        if (grouped(b)) {list_groups(b);return 1;}
    } else if (b->level == X6_BROWSER_PAGES) {
        b->page_selected[b->library][b->bucket] = selected;
        if (grouped(b)) {list_groups(b);return 1;}
    } else b->group_selected[b->library] = selected;
    root(b);return 1;
}
void x6_browser_focus(struct x6_browser *b, uint32_t library, uint32_t game_index)
{
    if (library >= X6_LIBRARY_COUNT || game_index >= b->totals[library]) {root(b);return;}
    uint32_t bucket = 0, ordinal = 0;
    if (b->provider && library != X6_LIBRARY_MAME) {
        if (b->provider->position(library, game_index, &bucket, &ordinal) != 0) {root(b);return;}
    } else {
        bucket = x6_library_bucket(b->games[library][game_index].title);
        for (uint32_t i = 0; i < game_index; ++i)
            if (x6_library_bucket(b->games[library][i].title) == bucket) ++ordinal;
    }
    b->library = library;
    for (uint32_t i = 0, row = 0; i < X6_LIBRARY_COUNT; ++i) {
        if (i && !b->totals[i]) continue;
        if (i == library) b->root_selected = row;
        ++row;
    }
    b->bucket = grouped(b) ? bucket : X6_BROWSER_ALL;
    if (!grouped(b)) ordinal = game_index;
    if (grouped(b)) {
        list_groups(b);
        for (uint32_t i = 0; i < b->count; ++i)
            if (b->map[i] == bucket) b->group_selected[library] = i;
    }
    b->offset = ordinal / X6_LIBRARY_PAGE * X6_LIBRARY_PAGE;
    b->page_selected[library][b->bucket] = ordinal / X6_LIBRARY_PAGE;
    b->game_selected[library][b->bucket] = ordinal % X6_LIBRARY_PAGE;
    list_games(b);
}

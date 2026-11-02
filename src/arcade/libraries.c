/* Reuse a validated library at boot; discover ROMs on first boot or explicit
 * rescan. Keep title/offset keys in RAM and page full metadata from SD. */
#include "x6_library.h"
#include "x6_catalog.h"
#include "x6_stock_catalog.h"
#include "x6_titles.h"
#include "x6_directory.h"
#include "x6_runtime.h"
#include "x6_rom_file.h"
#include "x6_scan_log.h"
#define LIMIT 8192u
#define CACHE "F:/x6/LIBRARY.IDX" /* 8.3 creation name for the stock filesystem. */
struct slot {char title[40];uint32_t offset, library, checksum;};
/* Single-file commit: records, sorted slots, then this checked footer. A scan
 * that stops early never appends a footer. Bump the version when record or
 * title/discovery semantics change and require automatic regeneration. */
#define CACHE_MAGIC 0x58464958u
#define CACHE_VERSION 1u
struct footer {uint32_t magic, version, count, game_size, slot_size,
    data_hash, slots_hash, engines, hash;};
static struct slot slots[LIMIT];
static struct x6_game stock_staging[X6_CATALOG_MAX];
static uint32_t used, status, counts[X6_LIBRARY_COUNT], starts[X6_LIBRARY_COUNT];
static uint32_t data_hash, engines;
static struct x6_scan_info scan_info;
static void (*progress)(const struct x6_scan_info *);
void x6_libraries_progress(void (*callback)(const struct x6_scan_info *)) {progress = callback;}
const struct x6_scan_info *x6_libraries_scan_info(void) {return &scan_info;}
static uint32_t hash_bytes(uint32_t h, const void *data, uint32_t size)
{
    const uint8_t *p = data;
    for (uint32_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t checksum(const struct x6_game *g) {return hash_bytes(2166136261u, g, sizeof *g);}
static const char *const modules[X6_LIBRARY_COUNT] = {0, "F:/x6/stock/fba.mod", "F:/x6/stock/fc.mod",
    "F:/x6/stock/sfc.mod", "F:/x6/stock/md.mod", "F:/x6/stock/gbc.mod", "F:/x6/stock/gbc.mod", "F:/x6/stock/gba.mod"};
const char *x6_library_module(uint32_t library) {return library < X6_LIBRARY_COUNT ? modules[library] : 0;}
static void copy(char *out, uint32_t cap, const char *in)
{
    uint32_t i = 0;while (in[i] && i + 1 < cap) {out[i] = in[i];++i;}out[i] = 0;
}
static void publish(uint32_t phase, uint32_t library, const char *directory)
{
    scan_info.phase = phase;scan_info.library = library;
    scan_info.indexed = used;scan_info.errors = status;
    if (directory) copy(scan_info.directory, sizeof scan_info.directory, directory);
    scan_info.log_status = x6_scan_log_checkpoint(&scan_info);
    if (progress) progress(&scan_info);
}
static void failure(uint32_t flag, int32_t code, const char *path)
{
    uint32_t fresh = flag & ~status;
    status |= flag;
    if (!scan_info.error_code || (flag & (X6_SCAN_OPEN | X6_SCAN_WRITE | X6_SCAN_CLOSE_FILE))) {
        scan_info.error_code = code;copy(scan_info.error_path, sizeof scan_info.error_path, path);
    }
    scan_info.indexed = used;scan_info.errors = status;
    if (fresh) x6_scan_log_failure(&scan_info, flag, code, path);
}
static uint32_t present(const char *path)
{
    void *f = x6_fs_open(path, "rb");
    if (!f) {x6_scan_log_io("module-open", path, 0, 1, &scan_info);return 0;}
    int32_t result = x6_fs_close(f);
    x6_scan_log_io("module-close", path, result, 0, &scan_info);return result == 0;
}
static uint32_t engine_mask(void)
{
    uint32_t mask = 0;
    for (uint32_t lib = X6_LIBRARY_STOCK; lib < X6_LIBRARY_COUNT; ++lib)
        if (present(modules[lib])) mask |= 1u << lib;
    return mask;
}
static int32_t append(void *file, uint32_t library, struct x6_game *g)
{
    if (used == LIMIT) {failure(X6_SCAN_LIMIT, -5, g->path);return -1;}
    x6_titles_apply(g, library);
    uint32_t written = x6_fs_write(g, 1, sizeof *g, file);
    if (written != sizeof *g) {
        x6_scan_log_io("cache-write", CACHE, (int32_t)written, sizeof *g, &scan_info);
        failure(X6_SCAN_WRITE, -2, CACHE);return -1;
    }
    data_hash = hash_bytes(data_hash, g, sizeof *g);
    struct slot *s = &slots[used];copy(s->title, sizeof s->title, g->title);
    s->offset = used * sizeof *g;s->library = library;s->checksum = checksum(g);++used;
    if (used % 16u == 0) publish(X6_SCAN_SCANNING, scan_info.library, 0);
    return 0;
}
struct scan {void *file;uint32_t library, engine;const char *directory;};
static uint32_t extension(const char *name, uint32_t library)
{
    const char *dot = 0;for (const char *p = name; *p; ++p) if (*p == '.') dot = p;
    if (!dot) return 0;
    if (library == X6_LIBRARY_NES) return x6_rom_equal(dot, ".nes");
    if (library == X6_LIBRARY_SNES) return x6_rom_equal(dot, ".sfc") || x6_rom_equal(dot, ".smc");
    if (library == X6_LIBRARY_MD) return x6_rom_equal(dot, ".bin") || x6_rom_equal(dot, ".smd") || x6_rom_equal(dot, ".md") || x6_rom_equal(dot, ".gen");
    if (library == X6_LIBRARY_GB) return x6_rom_equal(dot, ".gb");
    if (library == X6_LIBRARY_GBC) return x6_rom_equal(dot, ".gbc");
    return library == X6_LIBRARY_GBA && x6_rom_equal(dot, ".gba");
}
static int32_t found(const struct x6_directory_entry *e, void *context)
{
    struct scan *s = context;uint32_t library = s->library, supported = extension(e->name, library), stem;
    ++scan_info.checked;
    if (scan_info.checked % 16u == 0) publish(X6_SCAN_SCANNING, s->library, s->directory);
    if (!supported) {
        uint32_t rom = x6_rom_type(e->name, &stem);
        for (uint32_t lib = X6_LIBRARY_NES; lib < X6_LIBRARY_COUNT; ++lib) {
            if (extension(e->name, lib)) {library = lib;supported = rom = 1;break;}
        }
        if (!rom) return 0;
    }
    struct x6_game g = {0};
    if (!x6_directory_join(g.path, sizeof g.path, s->directory, e->name)) {failure(X6_SCAN_PATH, -2, s->directory);return 0;}
    copy(g.id, sizeof g.id, e->name);copy(g.maker, sizeof g.maker, x6_library_name(library));
    copy(g.variant, sizeof g.variant, "Playback unverified");x6_titles_filename(&g, e->name);
    g.available = !e->size_low && !e->size_high ? X6_GAME_INVALID
        : !supported ? X6_GAME_FORMAT : x6_titles_console(&g, library, e->size_low);
    uint32_t engine = library == s->library ? s->engine : engines & (1u << library);
    if (g.available == X6_GAME_READY && !engine) g.available = X6_GAME_ENGINE;
    return append(s->file, library, &g) < 0 ? -5 : 0;
}
static int32_t compare(const struct slot *a, const struct slot *b)
{
    if (a->library != b->library) return a->library < b->library ? -1 : 1;
    for (uint32_t i = 0;; ++i) {
        char x = x6_rom_lower(a->title[i]), y = x6_rom_lower(b->title[i]);
        if (x != y) return x < y ? -1 : 1;if (!x) break;
    }
    return a->offset < b->offset ? -1 : a->offset != b->offset;
}
static void sift(uint32_t at, uint32_t end)
{
    while (at * 2 + 1 < end) {
        uint32_t child = at * 2 + 1;
        if (child + 1 < end && compare(&slots[child], &slots[child + 1]) < 0) ++child;
        if (compare(&slots[at], &slots[child]) >= 0) return;
        struct slot tmp = slots[at];slots[at] = slots[child];slots[child] = tmp;at = child;
    }
}
static void index_counts(void)
{
    for (uint32_t i = 0; i < used; ++i) {
        uint32_t lib = slots[i].library;if (!counts[lib]) starts[lib] = i;++counts[lib];
    }
}
static void begin(void)
{
    used = status = 0;scan_info = (struct x6_scan_info){0};
    data_hash = 2166136261u;
    scan_info.log_status = x6_scan_log_begin();
    for (uint32_t i = 0; i < X6_LIBRARY_COUNT; ++i) counts[i] = starts[i] = 0;
}
/* Validate all metadata with sequential reads, never reopen or enumerate ROMs.
 * Page reads retain their independent record seals after loading this index. */
static int32_t load_saved(void)
{
    void *f = x6_fs_open(CACHE, "rb");if (!f) return -1;
    struct footer end;uint32_t valid = 0;uint8_t seen[LIMIT / 8] = {0};
    if (x6_fs_seek(f, -(int32_t)sizeof end, 2) != 0
        || x6_fs_read(&end, 1, sizeof end, f) != sizeof end) goto done;
    if (end.magic != CACHE_MAGIC || end.version != CACHE_VERSION || end.count > LIMIT
        || end.game_size != sizeof(struct x6_game) || end.slot_size != sizeof(struct slot)
        || end.hash != hash_bytes(2166136261u, &end, sizeof end - sizeof end.hash)) goto done;
    uint32_t records = end.count * sizeof(struct x6_game), keys = end.count * sizeof(struct slot);
    if (x6_fs_tell(f) != (int32_t)(records + keys + sizeof end)
        || x6_fs_seek(f, 0, 0) != 0) goto done;
    uint32_t h = 2166136261u;
    for (uint32_t at = 0; at < records;) {
        uint32_t n = records - at;if (n > 32768u) n = 32768u;
        if (x6_fs_read(stock_staging, 1, n, f) != n) goto done;
        h = hash_bytes(h, stock_staging, n);at += n;
    }
    if (h != end.data_hash || x6_fs_read(slots, 1, keys, f) != keys
        || hash_bytes(2166136261u, slots, keys) != end.slots_hash) goto done;
    for (uint32_t i = 0; i < end.count; ++i) {
        struct slot *s = &slots[i];
        if (!s->library || s->library >= X6_LIBRARY_COUNT || s->title[39]
            || s->offset >= records || s->offset % sizeof(struct x6_game)
            || (i && compare(&slots[i - 1], s) >= 0)) goto done;
        uint32_t ordinal = s->offset / sizeof(struct x6_game), bit = 1u << (ordinal % 8);
        if (seen[ordinal / 8] & bit) goto done;
        seen[ordinal / 8] |= (uint8_t)bit;
    }
    engines = engine_mask();if (end.engines != engines) goto done;
    valid = 1;
done:;
    int32_t closed = x6_fs_close(f);
    if (!valid || closed) return -2;
    used = end.count;index_counts();scan_info.cache_hit = 1;return 0;
}
int32_t x6_libraries_start(void)
{
    begin();publish(X6_SCAN_CACHED, X6_LIBRARY_STOCK, CACHE);
    int32_t result = load_saved();
    if (!result) {
        publish(X6_SCAN_TITLES, X6_LIBRARY_STOCK, "F:/x6/");
        if (!x6_titles_load()) {publish(X6_SCAN_READY, X6_LIBRARY_STOCK, CACHE);return 0;}
        result = -3;
    }
    int32_t rebuilt = x6_libraries_build();
    x6_scan_log_io("saved-index-rejected", CACHE, result, 0, &scan_info);
    return rebuilt;
}
int32_t x6_libraries_build(void)
{
    begin();
    publish(X6_SCAN_TITLES, X6_LIBRARY_STOCK, "F:/x6/");
    if (x6_titles_load()) failure(X6_SCAN_TITLE, -1, "F:/x6/");
    publish(X6_SCAN_OPENING, X6_LIBRARY_STOCK, CACHE);
    void *file = x6_fs_open(CACHE, "wb");
    x6_scan_log_io("cache-open", CACHE, file ? 1 : 0, 1, &scan_info);
    if (!file) {failure(X6_SCAN_OPEN, -1, CACHE);publish(X6_SCAN_FAILED, X6_LIBRARY_STOCK, CACHE);return -1;}
    publish(X6_SCAN_SCANNING, X6_LIBRARY_STOCK, "F:/game/mame");
    engines = engine_mask();
    if (engines & (1u << X6_LIBRARY_STOCK)) {
        uint32_t n = x6_stock_catalog_build(stock_staging, X6_CATALOG_MAX);
        if (n == X6_CATALOG_MAX) failure(X6_SCAN_LIMIT, -5, "F:/game/mame");
        for (uint32_t i = 0; i < n; ++i) {
            ++scan_info.checked;
            if (append(file, X6_LIBRARY_STOCK, &stock_staging[i]) < 0) break;
        }
    }
    static const char *const roots[X6_LIBRARY_COUNT][3] = {
        {0}, {0}, {"F:/roms/nes", "F:/game/fc", "F:/game/nes"},
        {"F:/roms/snes", "F:/game/sfc", "F:/game/smc"},
        {"F:/roms/md", "F:/game/md", "F:/game/smd"}, {"F:/roms/gb", "F:/game/gb", 0},
        {"F:/roms/gbc", "F:/game/gbc", 0}, {"F:/roms/gba", "F:/game/gba", 0}
    };
    for (uint32_t lib = X6_LIBRARY_NES; lib < X6_LIBRARY_COUNT && !(status & 35u); ++lib) {
        struct scan s = {file, lib, engines & (1u << lib), 0};
        for (uint32_t root = 0; root < 3 && roots[lib][root]; ++root) {
            s.directory = roots[lib][root];
            publish(X6_SCAN_SCANNING, lib, s.directory);
            int32_t result = x6_directory_walk(s.directory, found, &s);
            x6_scan_log_io("directory-scan", s.directory, result, 0, &scan_info);
            if (result < -1) failure(X6_SCAN_DIRECTORY, result, s.directory);
            if (result == -4) failure(X6_SCAN_CLOSE_DIR, result, s.directory);
            if (result == -4 || result == -5) break;
        }
    }
    publish(X6_SCAN_SORTING, scan_info.library, 0);
    for (uint32_t i = used / 2; i > 0; --i) sift(i - 1, used);
    for (uint32_t end = used; end > 1;) {
        struct slot tmp = slots[0];slots[0] = slots[--end];slots[end] = tmp;sift(0, end);
    }
    publish(X6_SCAN_FINALIZING, scan_info.library, CACHE);
    if (!status) {
        uint32_t size = used * sizeof(struct slot);
        struct footer end = {CACHE_MAGIC, CACHE_VERSION, used, sizeof(struct x6_game), sizeof(struct slot),
            data_hash, hash_bytes(2166136261u, slots, size), engines, 0};
        end.hash = hash_bytes(2166136261u, &end, sizeof end - sizeof end.hash);
        uint32_t written = x6_fs_write(slots, 1, size, file);
        if (written == size) {written = x6_fs_write(&end, 1, sizeof end, file);size = sizeof end;}
        if (written != size) {
            x6_scan_log_io("cache-index-write", CACHE, (int32_t)written, size, &scan_info);
            failure(X6_SCAN_WRITE, -2, CACHE);
        }
    }
    int32_t closed = x6_fs_close(file);
    x6_scan_log_io("cache-close", CACHE, closed, 0, &scan_info);
    if (closed != 0) failure(X6_SCAN_CLOSE_FILE, -3, CACHE);
    if (status & (X6_SCAN_WRITE | X6_SCAN_CLOSE_FILE)) {used = 0;publish(X6_SCAN_FAILED, scan_info.library, CACHE);return -1;}
    index_counts();
    publish(status ? X6_SCAN_FAILED : X6_SCAN_READY, scan_info.library, 0);
    return status ? (int32_t)status : 0;
}
static uint32_t stats(uint32_t lib, uint32_t buckets[X6_LIBRARY_BUCKETS])
{
    if (lib >= X6_LIBRARY_COUNT || !buckets) return 0;
    for (uint32_t i = 0; i < X6_LIBRARY_BUCKETS; ++i) buckets[i] = 0;
    for (uint32_t i = 0; i < counts[lib]; ++i) ++buckets[x6_library_bucket(slots[starts[lib] + i].title)];
    return counts[lib];
}
static int32_t read_game(void *f, uint32_t lib, uint32_t index, struct x6_game *g)
{
    if (!g || lib >= X6_LIBRARY_COUNT || index >= counts[lib]) return -1;
    if (x6_fs_seek(f, (int32_t)slots[starts[lib] + index].offset, 0) != 0
        || x6_fs_read(g, 1, sizeof *g, f) != sizeof *g) return -1;
    /* Match the record to the seal from this boot's validated saved index. */
    if (checksum(g) != slots[starts[lib] + index].checksum
        || g->path[0] != 'F' || g->path[1] != ':' || g->path[255] || g->title[39]
        || g->id[15] || g->maker[31] || g->year[7] || g->variant[23]) return -1;
    return 0;
}
int32_t x6_libraries_game(uint32_t lib, uint32_t index, struct x6_game *g)
{
    void *f = x6_fs_open(CACHE, "rb");if (!f) return -1;
    int32_t result = read_game(f, lib, index, g);if (x6_fs_close(f) != 0) result = -1;return result;
}
static int32_t page(uint32_t lib, uint32_t bucket, uint32_t offset, struct x6_game *out, uint32_t *indices, uint32_t capacity)
{
    if (lib >= X6_LIBRARY_COUNT || bucket > X6_LIBRARY_ALL || !out || !indices || capacity > X6_LIBRARY_PAGE) return -1;
    void *f = x6_fs_open(CACHE, "rb");if (!f) return -1;
    uint32_t matched = 0, n = 0;int32_t result = 0;
    for (uint32_t i = 0; i < counts[lib] && n < capacity; ++i) {
        if (bucket < X6_LIBRARY_BUCKETS && x6_library_bucket(slots[starts[lib] + i].title) != bucket) continue;
        if (matched++ < offset) continue;
        if (read_game(f, lib, i, &out[n]) != 0) {result = -1;break;}
        indices[n++] = i;
    }
    if (x6_fs_close(f) != 0) result = -1;
    return result < 0 ? -1 : (int32_t)n;
}
static int32_t position(uint32_t lib, uint32_t index, uint32_t *bucket, uint32_t *offset)
{
    if (lib >= X6_LIBRARY_COUNT || index >= counts[lib] || !bucket || !offset) return -1;
    *bucket = x6_library_bucket(slots[starts[lib] + index].title);*offset = 0;
    for (uint32_t i = 0; i < index; ++i)
        if (x6_library_bucket(slots[starts[lib] + i].title) == *bucket) ++*offset;
    return 0;
}
const struct x6_library_provider x6_libraries_provider = {stats, page, position};

/* Filesystem doubles only. The production directory decoder and stock/MAME
 * catalog builders perform all traversal, validation, deduplication and sorting. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "x6_catalog.h"
#include "x6_directory.h"
#include "x6_stock_catalog.h"
#include "x6_runtime.h"

static struct x6_game catalog[512];
static uint32_t installed_dkong = 1, installed_galaga, new_dkong;
uint32_t x6_core_driver_count(void) {return 3;}
uint32_t x6_core_driver(uint32_t i, struct x6_driver_info *out)
{
    static const char *const ids[] = {"dkong", "galaga", "rygar"};
    static const char *const titles[] = {"Donkey Kong", "Galaga", "Rygar"};
    assert(i < 3);*out = (struct x6_driver_info){ids[i], titles[i], "1981", "Fixture", 0, 0, 0};return 1;
}
int32_t x6_file_size(const char *p)
{
    if (!strcmp(p, "F:/roms/mame/dkong.zip")) return new_dkong ? 100 : -1;
    if (!strcmp(p, "F:/game/cps/dkong.zip")) return installed_dkong ? 100 : -1;
    if (!strcmp(p, "F:/roms/mame/galaga.zip")) return installed_galaga ? 100 : -1;
    if (!strcmp(p, "F:/game/cps/rygar.zip")) return 100;
    return -1;
}

struct file {const char *directory, *name, *id;uint32_t attributes, short_read, bad;};
static const struct file files[] = {
    {"F:\\roms\\original", "2020 DEMO.FBA", "2020bb", 0, 0, 0},
    {"F:\\roms\\original", "Friendly Name.fba", "aof", 0, 0, 0},
    {"F:\\roms\\original", "NEMO.ZIP", 0, 0, 0, 0},
    {"F:\\roms\\original", "neogeo.zip", 0, 0, 0, 0},
    {"F:\\roms\\original", "bad.fba", "bad", 0, 0, 1},
    {"F:\\roms\\original", "short.fba", "short", 0, 1, 0},
    {"F:\\roms\\original", "compressed.7z", 0, 0, 0, 0},
    {"F:\\roms\\original", "empty.zip", 0, 0, 0, 0},
    {"F:\\roms\\original", "._Finder.fba", "hidden", 0, 0, 0},
    {"F:\\roms\\original", "directory.fba", "folder", 0x10, 0, 0},
    {"F:\\roms\\original", "bad/name.fba", "badname", 0, 0, 0},
    {"F:\\game\\mame", "2020bb.fba", "2020bb", 0, 0, 0},
    {"F:\\game\\mame", "alpham2.fba", "alpham2", 0, 0, 0},
    {"F:\\x6\\stock", "2020bb.fba", "2020bb", 0, 0, 0},
    {"F:\\roms\\mame", "uncompiled.zip", 0, 0, 0, 0},
    {"F:\\roms\\mame", "empty.zip", 0, 0, 0, 0},
    {"F:\\roms\\mame", "altbeast.fba", "altbeast", 0, 0, 0},
    {"F:\\roms\\mame", "compressed.7z", 0, 0, 0, 0},
    {"F:\\roms\\mame", "GALAGA.ZIP", 0, 0, 0, 0},
    {"F:\\roms\\mame", "neogeo.zip", 0, 0, 0, 0},
    {"F:\\roms\\mame", "X6-ARCADE.zip", 0, 0, 0, 0},
    {"F:\\roms\\mame", "README.TXT", 0, 0, 0, 0},
    {"F:\\roms\\mame", "dkong.x6d", 0, 0, 0, 0},
    {"F:\\roms\\mame", "directory.zip", 0, 0x10, 0, 0},
    {"F:\\game\\cps", "UNCOMPILED.ZIP", 0, 0, 0, 0},
    {"F:\\game\\cps", "legacyonly.zip", 0, 0, 0, 0},
    {"F:\\game\\cps", "averylongunknownromname1.zip", 0, 0, 0, 0},
    {"F:\\game\\cps", "averylongunknownromname2.zip", 0, 0, 0, 0},
};
static const char *const dirs[] = {"F:\\roms\\original", "F:\\game\\mame", "F:\\x6\\stock",
    "F:\\roms\\mame", "F:\\game\\cps"};
struct directory {uint32_t root, cursor;};
static struct directory handles[5];
static uint8_t entry[16 + 256];
static uint32_t opens, closes, reads, file_opens, file_closes, endless, close_failure, long_name, no_primary;
static int same(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}
void *x6_directory_open_raw(const char *path)
{
    for (uint32_t i = 0; i < 5; ++i) if (!strcmp(path, dirs[i])) {
        if (no_primary && i == 0) return 0;
        handles[i] = (struct directory){i, 0};++opens;return &handles[i];
    }
    return 0;
}
const uint8_t *x6_directory_read_raw(void *handle)
{
    struct directory *d = handle;++reads;
    while (d->cursor < sizeof files / sizeof *files || endless) {
        const struct file *f = &files[endless ? 0 : d->cursor++];
        if (strcmp(f->directory, dirs[d->root])) continue;
        memset(entry, 0, sizeof entry);entry[4] = (uint8_t)f->attributes;entry[8] = 32;
        if (!strcmp(f->name, "empty.zip")) entry[8] = 0;
        if (long_name) memset(entry + 16, 'x', 256);
        else strcpy((char *)entry + 16, f->name);
        return entry;
    }
    return 0;
}
int32_t x6_directory_close_raw(void *handle) {assert(handle);++closes;return close_failure ? -1 : 0;}
void *x6_fs_open(const char *path, const char *mode)
{
    assert(!strcmp(mode, "rb"));
    char actual[256];
    for (uint32_t i = 0; i < sizeof files / sizeof *files; ++i) {
        snprintf(actual, sizeof actual, "%s\\%s", files[i].directory, files[i].name);
        if (same(path, actual)) {++file_opens;return (void *)&files[i];}
    }
    return 0;
}
uint32_t x6_fs_read(void *out, uint32_t size, uint32_t count, void *handle)
{
    const struct file *f = handle;uint8_t *p = out;
    assert(size == 1 && count == 16);memset(p, 0, 16);
    if (f->id) {p[0] = 0x71;p[1] = 0x96;p[2] = 2;strcpy((char *)p + 4, f->id);}
    else {p[0] = 'P';p[1] = 'K';p[2] = 3;p[3] = 4;}
    if (f->bad) p[0] = 0;
    return f->short_read ? 7 : count;
}
int32_t x6_fs_close(void *handle) {assert(handle);++file_closes;return 0;}
static int32_t visitor(const struct x6_directory_entry *e, void *context)
{
    assert(e && (e->size_low == 32 || e->size_low == 0) && e->size_high == 0);
    ++*(uint32_t *)context;return 0;
}
static int32_t abort_visit(const struct x6_directory_entry *e, void *context) {(void)e;(void)context;return -9;}

int main(void)
{
    /* Adding/removing supported files updates the catalog with the SAME pool. */
    assert(x6_catalog_build(catalog, 512, "F:/game/cps") == 2);
    assert(!strcmp(catalog[0].id, "dkong") && !strcmp(catalog[0].path, "F:/game/cps/dkong.zip"));
    installed_galaga = 1;installed_dkong = 0;
    assert(x6_catalog_build(catalog, 512, "F:/game/cps") == 2);
    assert(!strcmp(catalog[0].id, "galaga") && !strcmp(catalog[0].path, "F:/roms/mame/galaga.zip"));
    installed_dkong = new_dkong = 1;
    assert(x6_catalog_build(catalog, 512, "F:/game/cps") == 3);
    assert(!strcmp(catalog[0].path, "F:/roms/mame/dkong.zip"));

    uint32_t n = x6_catalog_scan(catalog, 512, "F:/game/cps", x6_directory_walk);
    assert(n == 10 && opens == closes);
    uint32_t ready = 0, unsupported = 0, invalid = 0, format = 0, unknown = 0;
    for (uint32_t i = 0; i < n; ++i) {
        ready += catalog[i].available == X6_GAME_READY;
        unsupported += catalog[i].available == X6_GAME_UNSUPPORTED;
        invalid += catalog[i].available == X6_GAME_INVALID;
        format += catalog[i].available == X6_GAME_FORMAT;
        if (!strcmp(catalog[i].id, "uncompiled")) {
            assert(!strcmp(catalog[i].path, "F:/roms/mame/uncompiled.zip"));++unknown;
        }
    }
    assert(ready == 3 && unsupported == 4 && invalid == 1 && format == 2 && unknown == 1);
    assert(x6_catalog_scan(catalog, 1, "F:/game/cps", x6_directory_walk) == 1);
    assert(catalog[0].available == X6_GAME_READY && opens == closes);
    close_failure = 1;
    assert(x6_catalog_scan(catalog, 512, "F:/game/cps", x6_directory_walk) == 3);
    close_failure = 0;
    /* Without a directory service an older host keeps supported probing only. */
    assert(x6_catalog_scan(catalog, 512, "F:/game/cps", 0) == 3);

    n = x6_stock_catalog_build(catalog, 512);
    assert(n == 8 && opens == closes && file_opens == file_closes);
    assert(!strcmp(catalog[0].id, "2020bb") && !strcmp(catalog[0].title, "2020 Super Baseball"));
    assert(!strcmp(catalog[0].path, "F:\\roms\\original\\2020 DEMO.fba"));
    assert(x6_stock_catalog_legacy(catalog, n) == 0);
    uint32_t nemo = 0;
    for (uint32_t i = 0; i < n; ++i) if (!strcmp(catalog[i].id, "nemo")) {
        assert(!strcmp(catalog[i].path, "F:\\roms\\original\\nemo.zip"));nemo = 1;
    }
    assert(nemo);
    ready = invalid = format = 0;
    for (uint32_t i = 0; i < n; ++i) {
        ready += catalog[i].available == X6_GAME_READY;
        invalid += catalog[i].available == X6_GAME_INVALID;
        format += catalog[i].available == X6_GAME_FORMAT;
        if (catalog[i].available == X6_GAME_READY) assert(strstr(catalog[i].variant, "unverified"));
    }
    assert(ready == 4 && invalid == 3 && format == 1);
    no_primary = 1;assert(x6_stock_catalog_build(catalog, 512) == 2);
    assert(!strcmp(catalog[0].path, "F:\\game\\mame\\2020bb.fba"));
    no_primary = 0;assert(x6_stock_catalog_build(catalog, 1) == 1 && opens == closes);
    uint32_t visits = 0, before = opens;
    assert(x6_directory_walk("F:/../bad", visitor, &visits) == -2 && opens == before);
    assert(x6_directory_walk("E:/roms", visitor, &visits) == -2 && opens == before);
    assert(x6_directory_walk("F:/missing", visitor, &visits) == -1);
    assert(x6_directory_walk("F:/roms/original", abort_visit, 0) == -9 && opens == closes);
    close_failure = 1;assert(x6_directory_walk("F:/roms/original", visitor, &visits) == -4);close_failure = 0;
    long_name = 1;visits = 0;assert(x6_directory_walk("F:/roms/original", visitor, &visits) == 0 && !visits);long_name = 0;
    endless = 1;visits = 0;assert(x6_directory_walk("F:/roms/original", visitor, &visits) == -3);
    assert(visits == X6_DIRECTORY_LIMIT && opens == closes);endless = 0;
    char path[256];
    assert(!x6_directory_join(path, sizeof path, "F:/roms", "../bad.fba"));
    assert(!x6_directory_join(path, sizeof path, "F:/roms", "dir/bad.fba"));
    assert(!x6_directory_join(path, sizeof path, "F:/../bad", "rom.fba"));
    assert(!x6_directory_join(path, 8, "F:/roms", "game.fba"));
    puts("PASS: ROM add/remove; unsupported/invalid/format entries; new/legacy precedence; stock headers; duplicate/BIOS/save filtering; bounded traversal and cleanup");
    return 0;
}

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "x6_library.h"
#include "x6_browser.h"
#include "x6_path.h"
#include "x6_runtime.h"
extern const char *x6_test_root, *x6_test_output;
extern uint32_t x6_test_opens, x6_test_closes;
extern uint32_t x6_test_file_opens, x6_test_file_closes, x6_test_fail_write;
extern uint32_t x6_test_fail_cache_open;
static uint32_t observed_phases, progress_calls, saw_running_count;
static void progress(const struct x6_scan_info *p)
{
    assert(p->phase <= X6_SCAN_MENU && p->indexed <= 8192);
    observed_phases |= 1u << p->phase;++progress_calls;
    if (p->phase == X6_SCAN_SCANNING && p->checked && p->indexed) saw_running_count = 1;
    if (p->phase == X6_SCAN_READY) assert(!p->errors);
}
static struct x6_browser browser;
static struct x6_game page[X6_LIBRARY_PAGE];
static uint32_t indices[X6_LIBRARY_PAGE], seen[8192];
int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);x6_test_root = argv[1];x6_test_output = argv[2];
    char normalized[256];
    assert(x6_path_normalize(normalized, "f:/x6/LIBRARY.IDX") && !strcmp(normalized, "F:\\x6\\LIBRARY.IDX"));
    assert(!x6_fs_open("F:/../elsewhere", "rb") && !x6_fs_open("B:/x6/LIBRARY.IDX", "rb"));
    assert(!x6_path_normalize(normalized, "F:/x6/../../elsewhere"));
    x6_libraries_progress(progress);
    int32_t status = x6_libraries_build();assert(status == 0);
    const struct x6_scan_info *scan = x6_libraries_scan_info();
    assert(scan->phase == X6_SCAN_READY && !scan->errors && saw_running_count && progress_calls > 10);
    assert((observed_phases & ((1u << X6_SCAN_TITLES) | (1u << X6_SCAN_OPENING) | (1u << X6_SCAN_SORTING) | (1u << X6_SCAN_FINALIZING)))
        == ((1u << X6_SCAN_TITLES) | (1u << X6_SCAN_OPENING) | (1u << X6_SCAN_SORTING) | (1u << X6_SCAN_FINALIZING)));
    uint32_t buckets[5], counts[X6_LIBRARY_COUNT] = {0};
    for (uint32_t lib = 1; lib < X6_LIBRARY_COUNT; ++lib) {
        counts[lib] = x6_libraries_provider.stats(lib, buckets);
        printf("Library %s: %u entries\n", x6_library_name(lib), counts[lib]);
        uint32_t reached = 0, ready = 0;memset(seen, 0, sizeof seen);
        for (uint32_t bucket = 0; bucket < 5; ++bucket)
            for (uint32_t offset = 0; offset < buckets[bucket]; offset += X6_LIBRARY_PAGE) {
                int32_t n = x6_libraries_provider.page(lib, bucket, offset, page, indices, X6_LIBRARY_PAGE);
                assert(n > 0 && n <= (int32_t)X6_LIBRARY_PAGE);
                for (int32_t row = 0; row < n; ++row) {
                    assert(indices[row] < counts[lib] && !seen[indices[row]]++);
                    uint32_t actual_bucket, actual_offset;
                    assert(x6_libraries_provider.position(lib, indices[row], &actual_bucket, &actual_offset) == 0);
                    assert(actual_bucket == bucket && actual_offset == offset + (uint32_t)row);
                    assert(x6_library_bucket(page[row].title) == bucket);++reached;
                    ready += page[row].available == X6_GAME_READY;
                    if (strstr(page[row].path, "1114 ADVISL3")) printf("  Identified %s\n", page[row].title);
                }
            }
        assert(reached == counts[lib]);
        printf("  %u launch-eligible; %u disabled\n", ready, reached - ready);
        if (!counts[lib]) continue;
        x6_browser_init_provider(&browser, 0, 0, &x6_libraries_provider);
        x6_browser_focus(&browser, lib, counts[lib] - 1);
        assert(browser.level == X6_BROWSER_GAMES && browser.count <= X6_LIBRARY_PAGE);
        assert(browser.map[browser.selected] == counts[lib] - 1);
        uint32_t index;
        if (browser.entries[browser.selected].available == X6_GAME_READY)
            assert(x6_browser_choose(&browser, browser.selected, &index) == X6_BROWSER_PLAY && index == counts[lib] - 1);
        assert(x6_browser_back(&browser, browser.selected));
    }
    if (argc == 3) {
        assert(counts[X6_LIBRARY_NES] == 6003);
        assert(counts[X6_LIBRARY_STOCK] == 2);
        assert(counts[X6_LIBRARY_SNES] == 2 && counts[X6_LIBRARY_MD] == 2);
        struct x6_game g;uint32_t found = 0;
        for (uint32_t i = 0; i < counts[X6_LIBRARY_STOCK]; ++i) {
            assert(x6_libraries_game(X6_LIBRARY_STOCK, i, &g) == 0);
            if (!strcmp(g.id, "doubledr")) {
                assert(!strcmp(g.title, "Double Dragon (Neo-Geo)"));
                assert(strstr(g.path, "Diplodocus.fba") && !strcmp(g.year, "1995"));++found;
            }
        }
        assert(found == 1);
        assert(x6_libraries_game(X6_LIBRARY_GBA, 0, &g) == 0 && !strcmp(g.title, "KURURIN"));
        assert(x6_libraries_game(X6_LIBRARY_GB, 0, &g) == 0 && !strcmp(g.title, "DUCK TALES"));
        assert(x6_libraries_game(X6_LIBRARY_GBC, 0, &g) == 0 && !strcmp(g.title, "DW1&2"));
        assert(x6_libraries_game(X6_LIBRARY_NES, counts[X6_LIBRARY_NES] - 1, &g) == 0);
        assert(!strcmp(g.title, "Zelda friendly override"));
    }
    assert(x6_test_opens == x6_test_closes);
    assert(x6_test_file_opens == x6_test_file_closes);
    /* Cache edits cannot redirect a launch through stale metadata. */
    if (counts[X6_LIBRARY_STOCK]) {
        char path[2048];snprintf(path, sizeof path, "%s/x6/LIBRARY.IDX", x6_test_output);
        FILE *f = fopen(path, "r+b");assert(f);assert(fputc('!', f) != EOF);assert(fclose(f) == 0);
        struct x6_game g;uint32_t failures = 0;
        for (uint32_t i = 0; i < counts[X6_LIBRARY_STOCK]; ++i) failures += x6_libraries_game(X6_LIBRARY_STOCK, i, &g) != 0;
        assert(failures == 1);
    }
    if (argc == 3) {
        x6_test_fail_cache_open = 1;assert(x6_libraries_build() < 0);x6_test_fail_cache_open = 0;
        assert(x6_libraries_scan_info()->phase == X6_SCAN_FAILED && x6_libraries_scan_info()->errors == X6_SCAN_OPEN);
        assert(!strcmp(x6_scan_error_message(x6_libraries_scan_info()), "Cannot open library cache"));
        assert(strstr(x6_libraries_scan_info()->error_path, "LIBRARY.IDX"));
        x6_test_fail_write = 1;assert(x6_libraries_build() < 0);x6_test_fail_write = 0;
        assert(x6_libraries_scan_info()->phase == X6_SCAN_FAILED && x6_libraries_scan_info()->errors & X6_SCAN_WRITE);
        assert(x6_libraries_provider.stats(X6_LIBRARY_NES, buckets) == 0);
        char module[2048];snprintf(module, sizeof module, "%s/x6/stock/sfc.mod", x6_test_output);
        assert(remove(module) == 0);assert(x6_libraries_build() == 0);
        struct x6_game g;assert(x6_libraries_game(X6_LIBRARY_SNES, 0, &g) == 0);
        assert(g.available == X6_GAME_ENGINE);
        assert(x6_test_opens == x6_test_closes && x6_test_file_opens == x6_test_file_closes);
    }
    puts("PASS: bounded SD cache, complete multi-thousand catalog, every page/index reachable once, metadata/overrides, per-library return and closed handles; no emulator execution");
    return 0;
}

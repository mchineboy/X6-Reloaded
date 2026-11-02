#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "x6_library.h"
#include "x6_browser.h"
#include "x6_path.h"
#include "x6_runtime.h"
#include "x6_scan_log.h"
#include "x6_titles.h"
extern const char *x6_test_root, *x6_test_output;
extern uint32_t x6_test_opens, x6_test_closes;
extern uint32_t x6_test_directory_attempts, x6_test_rom_opens;
extern uint32_t x6_test_file_opens, x6_test_file_closes, x6_test_fail_write;
extern uint32_t x6_test_fail_cache_open;
extern uint32_t x6_test_fail_log_open, x6_test_fail_log_write, x6_test_fail_cache_close;
extern uint32_t x6_test_fail_title_read;
extern uint32_t x6_test_fail_log_close;
extern uint32_t x6_test_fail_cache_read, x6_test_fail_index_write, x6_test_fail_footer_write;
extern uint32_t x6_test_cache_writes;
static char log_text[65537];
static void read_log(void)
{
    char path[2048];snprintf(path, sizeof path, "%s/x6/SCAN.LOG", x6_test_output);
    FILE *f = fopen(path, "rb");assert(f);size_t n = fread(log_text, 1, sizeof log_text - 1, f);
    assert(n <= 65536 && !ferror(f));log_text[n] = 0;assert(fclose(f) == 0);
}
static uint32_t observed_phases, progress_calls, saw_running_count;
static void progress(const struct x6_scan_info *p)
{
    assert(p->phase <= X6_SCAN_CACHED && p->indexed <= 8192);
    observed_phases |= 1u << p->phase;++progress_calls;
    if (p->phase == X6_SCAN_SCANNING && p->checked && p->indexed) saw_running_count = 1;
    if (p->phase == X6_SCAN_READY) assert(!p->errors);
}
static struct x6_browser browser;
static struct x6_game page[X6_LIBRARY_PAGE];
static uint32_t indices[X6_LIBRARY_PAGE], seen[8192];
static void damage_cache(long offset, int origin)
{
    char path[2048];snprintf(path, sizeof path, "%s/x6/LIBRARY.IDX", x6_test_output);
    FILE *f = fopen(path, "r+b");assert(f);assert(fseek(f, offset, origin) == 0);
    int c = fgetc(f);assert(c != EOF);assert(fseek(f, offset, origin) == 0);
    assert(fputc(c ^ 0x80, f) != EOF);assert(fclose(f) == 0);
}
static void check_saved_reload(const uint32_t *counts)
{
    struct x6_game last[X6_LIBRARY_COUNT];
    for (uint32_t lib = 1; lib < X6_LIBRARY_COUNT; ++lib)
        if (counts[lib]) assert(x6_libraries_game(lib, counts[lib] - 1, &last[lib]) == 0);
    uint32_t directories = x6_test_directory_attempts, roms = x6_test_rom_opens, writes = x6_test_cache_writes;
    assert(x6_libraries_start() == 0 && x6_libraries_scan_info()->cache_hit);
    assert(x6_libraries_scan_info()->checked == 0);
    assert(x6_test_directory_attempts == directories && x6_test_rom_opens == roms);
    assert(x6_test_cache_writes == writes);
    for (uint32_t lib = 1; lib < X6_LIBRARY_COUNT; ++lib) {
        uint32_t buckets[5];struct x6_game g;
        assert(x6_libraries_provider.stats(lib, buckets) == counts[lib]);
        if (counts[lib]) {
            assert(x6_libraries_game(lib, counts[lib] - 1, &g) == 0);
            assert(!memcmp(&g, &last[lib], sizeof g));
        }
    }
    read_log();assert(strstr(log_text, "phase=loading-saved") && strstr(log_text, "cache_hit=1"));
}
static void check_filename_titles(void)
{
    static const struct {const char *filename, *title;uint32_t bucket;} cases[] = {
        {"8-Eyes.nes", "8-Eyes", 0},
        {"1943-kai.nes", "1943-kai", 0},
        {"1943_Kai.nes", "1943 Kai", 0},
        {"10-Yard Fight.nes", "10-Yard Fight", 0},
        {"1943 The Battle of Midway.nes", "1943 The Battle of Midway", 0},
        {"1943. The Battle of Midway.nes", "1943. The Battle of Midway", 0},
        {"1942.nes", "1942", 0},
        {"0008.8-Eyes.nes", "8-Eyes", 0},
        {"0012 1943-kai.nes", "1943-kai", 0},
        {"0212.Ikari (J).nes", "Ikari (J)", 2},
        {"001 Cryptic.nes", "Cryptic", 1},
        {"043_-_Super_Dodgeball_Advance.nes", "Super Dodgeball Advance", 4},
        {"0000.nes", "0000", 0},
        {"0001_.nes", "0001 ", 0},
    };
    for (uint32_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        struct x6_game g = {0};x6_titles_filename(&g, cases[i].filename);
        assert(!strcmp(g.title, cases[i].title));
        assert(x6_library_bucket(g.title) == cases[i].bucket);
    }
}
int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);x6_test_root = argv[1];x6_test_output = argv[2];
    check_filename_titles();
    char normalized[256];
    assert(x6_path_normalize(normalized, "f:/x6/LIBRARY.IDX") && !strcmp(normalized, "F:\\x6\\LIBRARY.IDX"));
    assert(!x6_fs_open("F:/../elsewhere", "rb") && !x6_fs_open("B:/x6/LIBRARY.IDX", "rb"));
    assert(!x6_path_normalize(normalized, "F:/x6/../../elsewhere"));
    x6_libraries_progress(progress);
    int32_t status = x6_libraries_start();assert(status == 0);
    assert(!x6_libraries_scan_info()->cache_hit); /* No saved index on first boot. */
    const struct x6_scan_info *scan = x6_libraries_scan_info();
    assert(scan->phase == X6_SCAN_READY && !scan->errors && saw_running_count && progress_calls > 10);
    assert(scan->log_status == X6_LOG_SAVED);read_log();
    assert(strstr(log_text, "X6 Reloaded scan log v1") && strstr(log_text, "phase=ready"));
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
        struct x6_game g;uint32_t found = 0, numeric = 0;
        const char *numeric_titles[] = {"8-Eyes", "1943-kai", "1943 Kai", "10-Yard Fight"};
        for (uint32_t i = 0; i < counts[X6_LIBRARY_NES]; ++i) {
            assert(x6_libraries_game(X6_LIBRARY_NES, i, &g) == 0);
            for (uint32_t j = 0; j < 4; ++j) if (!strcmp(g.title, numeric_titles[j])) {
                uint32_t bucket, offset;
                assert(g.available == X6_GAME_READY);
                assert(x6_libraries_provider.position(X6_LIBRARY_NES, i, &bucket, &offset) == 0 && bucket == 0);
                assert(!(numeric & (1u << j)));numeric |= 1u << j;
            }
        }
        assert(numeric == 15);
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
    check_saved_reload(counts);
    if (argc == 3) {
        /* A user's new/removal ROM operation takes effect on explicit rescan;
           cached boots keep their saved view without probing source folders. */
        char new_rom[2048];snprintf(new_rom, sizeof new_rom, "%s/roms/nes/New Arrival.nes", x6_test_root);
        FILE *added = fopen(new_rom, "wb");assert(added);
        const unsigned char header[16] = {'N', 'E', 'S', 0x1a};
        assert(fwrite(header, 1, sizeof header, added) == sizeof header);assert(fclose(added) == 0);
        check_saved_reload(counts);
        assert(x6_libraries_build() == 0);++counts[X6_LIBRARY_NES];check_saved_reload(counts);
        assert(remove(new_rom) == 0);check_saved_reload(counts);
        assert(x6_libraries_build() == 0);--counts[X6_LIBRARY_NES];check_saved_reload(counts);
        /* Broken footer/version, metadata and saved sort keys all rebuild.
           These edits touch only the temporary output index. */
        const long corrupt[] = {-36, -32, -28, -1};
        for (uint32_t i = 0; i < sizeof corrupt / sizeof corrupt[0]; ++i) {
            damage_cache(corrupt[i], SEEK_END);
            assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
            check_saved_reload(counts);
        }
        damage_cache(0, SEEK_SET);
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        uint32_t records = 0;for (uint32_t lib = 1; lib < X6_LIBRARY_COUNT; ++lib) records += counts[lib];
        damage_cache((long)records * sizeof(struct x6_game), SEEK_SET);
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        char cache_path[2048];snprintf(cache_path, sizeof cache_path, "%s/x6/LIBRARY.IDX", x6_test_output);
        FILE *f = fopen(cache_path, "wb");assert(f);assert(fputs("old or interrupted index", f) >= 0);assert(fclose(f) == 0);
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        x6_test_fail_cache_read = 1;assert(x6_libraries_start() == 0);x6_test_fail_cache_read = 0;
        assert(!x6_libraries_scan_info()->cache_hit);
        x6_test_fail_index_write = 1;assert(x6_libraries_build() < 0);x6_test_fail_index_write = 0;
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        x6_test_fail_footer_write = 1;assert(x6_libraries_build() < 0);x6_test_fail_footer_write = 0;
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        check_saved_reload(counts);
    }
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
        assert(x6_libraries_scan_info()->log_status == X6_LOG_SAVED);read_log();
        assert(strstr(log_text, "operation=cache-open result=0 expected=1"));
        assert(strstr(log_text, "error_flag=64 operation_code=-1 path=F:/x6/LIBRARY.IDX"));
        assert(strstr(log_text, "phase=failed"));
        x6_test_fail_write = 1;assert(x6_libraries_build() < 0);x6_test_fail_write = 0;
        assert(x6_libraries_scan_info()->phase == X6_SCAN_FAILED && x6_libraries_scan_info()->errors & X6_SCAN_WRITE);
        read_log();char expected[96];snprintf(expected, sizeof expected, "operation=cache-write result=0 expected=%zu", sizeof(struct x6_game));
        assert(strstr(log_text, expected) && strstr(log_text, "error_flag=2 operation_code=-2"));
        assert(x6_libraries_provider.stats(X6_LIBRARY_NES, buckets) == 0);
        x6_test_fail_cache_close = 1;assert(x6_libraries_build() < 0);x6_test_fail_cache_close = 0;
        read_log();assert(strstr(log_text, "operation=cache-close result=-7 expected=0"));
        assert(x6_libraries_scan_info()->errors & X6_SCAN_CLOSE_FILE);
        x6_test_fail_title_read = 1;assert(x6_libraries_build() > 0);x6_test_fail_title_read = 0;
        read_log();assert(strstr(log_text, "operation=title-read result=-1"));
        assert(strstr(log_text, "arcade-titles.tsv") && x6_libraries_scan_info()->errors & X6_SCAN_TITLE);
        assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        /* Diagnostics cannot turn successful discovery into an error if the
           log itself cannot be created or written. */
        x6_test_fail_log_open = 1;assert(x6_libraries_build() == 0);x6_test_fail_log_open = 0;
        assert(x6_libraries_scan_info()->log_status == X6_LOG_OPEN_FAILED);
        x6_test_fail_log_write = 1;assert(x6_libraries_build() == 0);x6_test_fail_log_write = 0;
        assert(x6_libraries_scan_info()->log_status == X6_LOG_IO_FAILED);
        x6_test_fail_log_close = 1;assert(x6_libraries_build() == 0);x6_test_fail_log_close = 0;
        assert(x6_libraries_scan_info()->log_status == X6_LOG_IO_FAILED);read_log();
        assert(strstr(log_text, "event=log-final-retry") && strstr(log_text, "previous_close=5") && strstr(log_text, "phase=ready"));
        char module[2048];snprintf(module, sizeof module, "%s/x6/stock/sfc.mod", x6_test_output);
        assert(remove(module) == 0);assert(x6_libraries_start() == 0 && !x6_libraries_scan_info()->cache_hit);
        struct x6_game g;assert(x6_libraries_game(X6_LIBRARY_SNES, 0, &g) == 0);
        assert(g.available == X6_GAME_ENGINE);
        assert(x6_test_opens == x6_test_closes && x6_test_file_opens == x6_test_file_closes);
        struct x6_scan_info failure = *x6_libraries_scan_info();
        x6_scan_log_failure(&failure, X6_SCAN_PATH, -2, "first-error-preserved");
        for (uint32_t i = 0; i < 1000; ++i) x6_scan_log_io("bounded-checkpoint", "F:/game/nes/fixture", 0, 0, &failure);
        failure.phase = X6_SCAN_FAILED;failure.errors = X6_SCAN_PATH;failure.error_code = -2;
        assert(x6_scan_log_checkpoint(&failure) == X6_LOG_SAVED);read_log();
        assert(strstr(log_text, "first-error-preserved") && strstr(log_text, "phase=failed"));
        const char *original_root = x6_test_root;x6_test_root = x6_test_output;
        assert(x6_libraries_build() == 0);
        uint32_t empty[X6_LIBRARY_COUNT] = {0};check_saved_reload(empty);
        x6_test_root = original_root;
    }
    puts("PASS: bounded SD cache, complete multi-thousand catalog, every page/index reachable once, metadata/overrides, per-library return and closed handles; no emulator execution");
    puts("PASS: persisted startup/error checkpoints, raw I/O results, first-error preservation, 64 KiB log bound, and nonfatal log failures");
    puts("PASS: saved index reload without ROM opens or directory scans; corrupt/old/interrupted index recovery, write failures and engine changes");
    return 0;
}

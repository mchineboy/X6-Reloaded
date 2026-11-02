#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "x6_browser.h"

static struct x6_game games[512], stock[1];
static struct x6_browser browser;

static void fill(uint32_t count)
{
    static const char *const prefixes[] = {"1942", "Alpha", "galaga", "Mappy", "Zaxxon"};
    for (uint32_t i = 0; i < count; ++i) {
        games[i] = (struct x6_game){.available = 1};
        snprintf(games[i].id, sizeof games[i].id, "game%u", i);
        snprintf(games[i].title, sizeof games[i].title, "%s %03u", prefixes[i % 5], i);
    }
    stock[0] = (struct x6_game){.id = "2020bb", .title = "2020 Super Baseball", .available = 1};
}

int main(void)
{
    uint32_t index = 9999;
    fill(400);
    x6_browser_init(&browser, games, 400, stock, 1);
    assert(browser.level == X6_BROWSER_ROOT && browser.count == 2);
    assert(browser.entries[0].folder && !strcmp(browser.entries[0].title, "MAME Arcade"));
    assert(browser.entries[1].folder && !strcmp(browser.entries[1].year, "1"));
    assert(!x6_browser_back(&browser, 0));
    assert(x6_browser_choose(&browser, 99, &index) == X6_BROWSER_NONE && index == 9999);
    assert(x6_browser_choose(&browser, 0, &index) == X6_BROWSER_ENTER);
    assert(browser.level == X6_BROWSER_GROUPS && browser.count == 5 && index == 9999);
    uint32_t seen[400] = {0}, total = 0;
    for (uint32_t group = 0; group < 5; ++group) {
        assert(x6_browser_choose(&browser, group, &index) == X6_BROWSER_ENTER);
        assert(browser.level == X6_BROWSER_GAMES && browser.count == 80);
        for (uint32_t row = 0; row < browser.count; ++row) {
            assert(!browser.entries[row].folder);
            assert(x6_browser_choose(&browser, row, &index) == X6_BROWSER_PLAY);
            assert(index < 400 && !seen[index]);seen[index] = 1;++total;
            assert(!strcmp(browser.entries[row].id, games[index].id));
        }
        assert(x6_browser_back(&browser, 37));
        assert(browser.level == X6_BROWSER_GROUPS && browser.selected == group);
        assert(x6_browser_choose(&browser, group, &index) == X6_BROWSER_ENTER);
        assert(browser.selected == 37); /* Each group retains its own cursor. */
        assert(x6_browser_back(&browser, 37));
    }
    assert(total == 400);
    for (uint32_t i = 0; i < 400; ++i) {
        x6_browser_focus(&browser, X6_LIBRARY_MAME, i);
        assert(x6_browser_choose(&browser, browser.selected, &index) == X6_BROWSER_PLAY && index == i);
    }
    assert(x6_browser_back(&browser, browser.selected));
    assert(x6_browser_back(&browser, browser.selected));
    assert(browser.level == X6_BROWSER_ROOT && browser.selected == 0);
    assert(x6_browser_choose(&browser, 1, &index) == X6_BROWSER_ENTER);
    assert(browser.level == X6_BROWSER_GAMES && browser.count == 1 && browser.library == X6_LIBRARY_STOCK);
    assert(x6_browser_choose(&browser, 0, &index) == X6_BROWSER_PLAY && index == 0);
    assert(x6_browser_back(&browser, 0) && browser.selected == 1);

    /* Empty libraries, threshold boundary, missing games, and sparse groups. */
    x6_browser_init(&browser, 0, 0, 0, 0);
    assert(browser.count == 1);
    assert(x6_browser_choose(&browser, 0, &index) == X6_BROWSER_ENTER && browser.count == 0);
    assert(x6_browser_back(&browser, 0));
    fill(41);x6_browser_init(&browser, games, 40, 0, 0);
    assert(x6_browser_choose(&browser, 0, &index) == X6_BROWSER_ENTER && browser.level == X6_BROWSER_GAMES);
    assert(browser.count == 40);
    games[0].available = 0;x6_browser_focus(&browser, X6_LIBRARY_MAME, 0);
    assert(x6_browser_choose(&browser, browser.selected, &index) == X6_BROWSER_NONE);
    for (uint32_t state = X6_GAME_UNSUPPORTED; state <= X6_GAME_FORMAT; ++state) {
        games[0].available = (uint8_t)state;x6_browser_focus(&browser, X6_LIBRARY_MAME, 0);
        index = 999;
        assert(x6_browser_choose(&browser, browser.selected, &index) == X6_BROWSER_NONE && index == 999);
        assert(browser.entries[browser.selected].available == state);
    }
    for (uint32_t i = 0; i < 41; ++i) strcpy(games[i].title, "zebra");
    x6_browser_init(&browser, games, 41, 0, 0);
    x6_browser_choose(&browser, 0, &index);
    assert(browser.count == 1 && !strcmp(browser.entries[0].title, "S-Z"));
    x6_browser_choose(&browser, 0, &index);assert(browser.count == 41);
    x6_browser_focus(&browser, X6_LIBRARY_STOCK, 0);assert(browser.level == X6_BROWSER_ROOT);

    fill(512);x6_browser_init(&browser, games, 512, stock, 1);
    x6_browser_focus(&browser, X6_LIBRARY_MAME, 511);
    assert(x6_browser_choose(&browser, browser.selected, &index) == X6_BROWSER_PLAY && index == 511);
    x6_browser_init(&browser, 0, 0, 0, 0);x6_browser_incomplete(&browser);
    assert(browser.count == 2 && browser.entries[1].available == X6_GAME_CATALOG);
    assert(x6_browser_choose(&browser, 1, &index) == X6_BROWSER_NONE);
    assert(x6_browser_choose(&browser, 0, &index) == X6_BROWSER_ENTER);
    assert(x6_browser_back(&browser, 0) && browser.count == 2);
    puts("PASS: 400 games reachable exactly once, folder/back/cursor behavior, driver-index mapping, sparse/empty groups, missing ROMs and 512-entry capacity");
    return 0;
}

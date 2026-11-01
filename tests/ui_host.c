/* Host tests for the arcade menu: behaviour assertions plus PPM screenshots.
 * Game entries are fixtures; nothing here claims a ROM or driver exists. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "x6_ui.h"

static uint32_t frame[X6_UI_WIDTH * X6_UI_HEIGHT + 2];
static struct x6_surface surface = {frame + 1, X6_UI_WIDTH, X6_UI_HEIGHT, X6_UI_WIDTH};

static struct x6_game game(const char *id, const char *title, const char *variant,
                           const char *maker, const char *year, int vertical, int available)
{
    struct x6_game g;memset(&g, 0, sizeof(g));
    strcpy(g.id, id);strcpy(g.title, title);strcpy(g.variant, variant);
    strcpy(g.maker, maker);strcpy(g.year, year);
    g.vertical = (uint8_t)vertical;g.available = (uint8_t)available;
    return g;
}

static void save(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *f = fopen(path, "wb");assert(f);
    fprintf(f, "P6\n%d %d\n255\n", X6_UI_WIDTH, X6_UI_HEIGHT);
    for (int i = 0; i < X6_UI_WIDTH * X6_UI_HEIGHT; ++i) {
        uint32_t p = surface.pixels[i];
        unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
        fwrite(rgb, 1, 3, f);
    }
    assert(fclose(f) == 0);
}

static void guard_check(void)
{
    assert(frame[0] == 0x13579bdfu && frame[X6_UI_WIDTH * X6_UI_HEIGHT + 1] == 0x2468ace0u);
}

static void settle(struct x6_menu *m, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i) assert(x6_menu_update(m, 0) == X6_MENU_NONE);
}

static enum x6_menu_action tap(struct x6_menu *m, uint32_t mask)
{
    enum x6_menu_action a = x6_menu_update(m, mask);
    assert(x6_menu_update(m, 0) == X6_MENU_NONE);
    return a;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    frame[0] = 0x13579bdfu;frame[X6_UI_WIDTH * X6_UI_HEIGHT + 1] = 0x2468ace0u;

    /* Boot animation must remain bounded, including saturated elapsed time. */
    for (uint32_t t = 0; t <= 140; ++t) {
        x6_ui_draw_splash(&surface, t);guard_check();
        if (t == 60) save(out, "startup");
    }
    x6_ui_draw_splash(&surface, 0xffffffffu);guard_check();

    struct x6_game five[5] = {
        game("altbeast", "Altered Beast", "Version 1", "Sega", "1988", 0, 0),
        game("digdug", "Dig Dug", "set 1", "Namco", "1982", 1, 1),
        game("dkong", "Donkey Kong", "US", "Nintendo of America", "1981", 1, 1),
        game("galaga", "Galaga", "Namco", "Namco", "1981", 1, 1),
        game("rygar", "Rygar", "US set 1", "Tecmo", "1986", 0, 1),
    };
    struct x6_menu m;

    /* Buttons held while entering the menu must not launch anything. */
    x6_menu_init(&m, five, 5, 2);
    assert(m.selected == 2);
    assert(x6_menu_update(&m, X6_BTN_A) == X6_MENU_NONE);
    assert(x6_menu_update(&m, X6_BTN_A) == X6_MENU_NONE);
    assert(x6_menu_update(&m, 0) == X6_MENU_NONE);
    settle(&m, 20);
    x6_menu_draw(&m, &surface);guard_check();save(out, "menu-dkong");

    /* Navigation: single steps wrap, held direction repeats after a delay. */
    assert(tap(&m, X6_BTN_UP) == X6_MENU_NONE && m.selected == 1);
    tap(&m, X6_BTN_UP);assert(m.selected == 0);
    tap(&m, X6_BTN_UP);assert(m.selected == 4);
    tap(&m, X6_BTN_DOWN);assert(m.selected == 0);
    for (int i = 0; i < 18; ++i) x6_menu_update(&m, X6_BTN_DOWN);
    assert(m.selected == 1);
    for (int i = 0; i < 10; ++i) x6_menu_update(&m, X6_BTN_DOWN);
    assert(m.selected == 3);                      /* two repeats, clamped not wrapped */
    for (int i = 0; i < 40; ++i) x6_menu_update(&m, X6_BTN_DOWN);
    assert(m.selected == 4);
    x6_menu_update(&m, 0);

    /* Missing ROM: no launch, a notice, and the row nudges. */
    tap(&m, X6_BTN_DOWN);assert(m.selected == 0);
    assert(tap(&m, X6_BTN_A) == X6_MENU_NONE);
    assert(m.notice_frames > 0 && strstr(m.notice, "altbeast.zip") && m.nudge_frames > 0);
    settle(&m, 4);
    x6_menu_draw(&m, &surface);guard_check();save(out, "menu-missing-rom");
    settle(&m, 200);assert(m.notice_frames == 0);

    /* A and Start both launch an available game. */
    tap(&m, X6_BTN_DOWN);
    assert(x6_menu_update(&m, X6_BTN_A) == X6_MENU_LAUNCH && m.selected == 1);
    x6_menu_update(&m, 0);
    assert(x6_menu_update(&m, X6_BTN_START) == X6_MENU_LAUNCH);
    x6_menu_update(&m, 0);
    /* Face buttons other than A do nothing. */
    assert(tap(&m, X6_BTN_B) == X6_MENU_NONE && tap(&m, X6_BTN_X) == X6_MENU_NONE && m.selected == 1);

    /* Long list: paging, scroll window and scroll bar. */
    struct x6_game many[14];
    const char *titles[14] = {"1942", "Bomb Jack", "Centipede", "Dig Dug", "Donkey Kong",
        "Donkey Kong Junior", "Frogger", "Galaga", "Gyruss", "Mappy", "Ms. Pac-Man",
        "Pengo", "Rally-X", "Super Pac-Man with an Extremely Long Title"};
    for (int i = 0; i < 14; ++i) many[i] = game("demo", titles[i], "", "Fixture Maker", "198x", i & 1, i != 9);
    x6_menu_init(&m, many, 14, 0);x6_menu_update(&m, 0);
    tap(&m, X6_BTN_RIGHT);assert(m.selected == 4);
    tap(&m, X6_BTN_RIGHT);tap(&m, X6_BTN_RIGHT);tap(&m, X6_BTN_RIGHT);assert(m.selected == 13);
    assert(m.target_scroll == (14 - 4) * 30);
    tap(&m, X6_BTN_LEFT);assert(m.selected == 9);
    settle(&m, 30);
    assert(m.scroll_q8 == m.target_scroll * 256);
    x6_menu_draw(&m, &surface);guard_check();save(out, "menu-long-list");
    tap(&m, X6_BTN_DOWN);tap(&m, X6_BTN_DOWN);tap(&m, X6_BTN_DOWN);tap(&m, X6_BTN_DOWN);
    assert(m.selected == 13);settle(&m, 30);
    x6_menu_draw(&m, &surface);guard_check();save(out, "menu-long-title");

    /* Mid-animation frame draws without touching memory outside the surface. */
    x6_menu_init(&m, many, 14, 0);x6_menu_update(&m, 0);
    x6_menu_update(&m, X6_BTN_RIGHT);x6_menu_update(&m, 0);
    x6_menu_draw(&m, &surface);guard_check();

    /* Empty catalogue. */
    x6_menu_init(&m, 0, 0, 0);
    assert(tap(&m, X6_BTN_A) == X6_MENU_NONE && tap(&m, X6_BTN_DOWN) == X6_MENU_NONE);
    x6_menu_draw(&m, &surface);guard_check();save(out, "menu-empty");

    x6_ui_draw_loading(&surface, &five[2]);guard_check();save(out, "loading");

    const char *settings[] = {"Music\tTechno", "Menu clicks\tOn", "Brightness\t13 / 16",
                              "Screen blank\tNever", "Auto power off\t5 min"};
    x6_ui_draw_settings(&surface, settings, 5, 2, 0);guard_check();save(out, "settings");
    const char *paused[] = {"Resume", "Restart", "Difficulty: Normal", "Rapid fire: 15 Hz", "Rapid buttons: All", "Back to menu"};
    x6_ui_draw_pause(&surface, "Donkey Kong", paused, 6, 0, 0, 1);guard_check();save(out, "pause");

    /* Game overlay: hint visible early, gone after three seconds, bar while held. */
    for (int i = 0; i < X6_UI_WIDTH * X6_UI_HEIGHT; ++i) surface.pixels[i] = 0xff000000u;
    x6_gfx_fill(&surface, 70, 0, 180, 240, 0xff203050u);
    uint32_t before = surface.pixels[230 * X6_UI_WIDTH + 160];
    x6_ui_draw_game_hint(&surface, 200, 0);
    assert(surface.pixels[230 * X6_UI_WIDTH + 160] == before);
    x6_ui_draw_game_hint(&surface, 10, 0);guard_check();save(out, "game-hint");
    x6_gfx_fill(&surface, 0, 0, 320, 240, 0xff000000u);x6_gfx_fill(&surface, 70, 0, 180, 240, 0xff203050u);
    x6_ui_draw_game_hint(&surface, 400, 40);guard_check();save(out, "game-returning");

    /* Primitive clipping at every edge. */
    x6_gfx_round_rect(&surface, -20, -20, 400, 300, 12, 0x80ffffffu);
    x6_gfx_disc(&surface, 0, 0, 20, 0xffffffffu);x6_gfx_disc(&surface, 319, 239, 20, 0xffffffffu);
    x6_text_draw(&surface, 300, 235, "Clipped text", 2, 0xffffffffu);
    x6_text_draw(&surface, -40, -5, "Clipped text", 2, 0xffffffffu);
    guard_check();

    /* Font metrics. */
    assert(x6_text_width("", 1) == 0 && x6_text_width("A", 1) == 5 && x6_text_width("AA", 1) == 11);
    assert(x6_text_width("AA", 2) == 22 && x6_text_width("\x01", 1) == x6_text_width("?", 1));

    puts("UI host tests passed: navigation, launch rules, missing ROM, scrolling, overlays, clipping.");
    return 0;
}

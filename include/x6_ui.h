#ifndef X6_UI_H
#define X6_UI_H
/* Arcade menu UI. Pure drawing/state code: no OS calls, no libc, no MAME.
 * Draws into the 320x240 ARGB surface the stock launcher scales to the panel. */
#include <stdint.h>

#define X6_UI_WIDTH 320
#define X6_UI_HEIGHT 240

/* Physical player-0 masks measured in device-game-test-002. */
#define X6_BTN_UP 0x001u
#define X6_BTN_DOWN 0x002u
#define X6_BTN_RIGHT 0x004u
#define X6_BTN_LEFT 0x008u
#define X6_BTN_A 0x010u
#define X6_BTN_B 0x020u
#define X6_BTN_X 0x040u
#define X6_BTN_Y 0x080u
#define X6_BTN_SELECT 0x400u
#define X6_BTN_START 0x800u
/* Host-defined: the standalone X6 shell forwards the MENU key as this bit. */
#define X6_BTN_MENU 0x1000u

/* Full-scale value of the in-game return bar; the frontend times the hold. */
#define X6_UI_RETURN_HOLD 60u

struct x6_surface {
    uint32_t *pixels;
    int32_t width, height, pitch;
};

/* Original boot artwork, rendered directly with our pixel font and primitives. */
void x6_ui_draw_splash(struct x6_surface *s, uint32_t elapsed_ticks);

/* Font: printable ASCII 32..126, 7-pixel cap height, 9-row cell. */
#define X6_FONT_ROWS 9
#define X6_FONT_CAP 7
extern const uint8_t x6_font_width[95];
extern const uint8_t x6_font_rows[95][X6_FONT_ROWS];

/* Colors are 0xAARRGGBB; alpha below 0xff blends over existing pixels. */
void x6_gfx_fill(struct x6_surface *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void x6_gfx_vgradient(struct x6_surface *s, int32_t y, int32_t h, uint32_t top, uint32_t bottom);
void x6_gfx_round_rect(struct x6_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint32_t color);
void x6_gfx_disc(struct x6_surface *s, int32_t cx, int32_t cy, int32_t radius, uint32_t color);
/* Scale 1 is the raw font; scale 2 uses edge-smoothed (EPX) doubling. */
int32_t x6_text_width(const char *text, int32_t scale);
int32_t x6_text_draw(struct x6_surface *s, int32_t x, int32_t y, const char *text,
                     int32_t scale, uint32_t color);

/* Preserve the old 0/1 values and structure layout across host versions. READY
 * means eligible to try, not that the ROM contents or gameplay were validated. */
enum x6_game_availability {
    X6_GAME_MISSING = 0, X6_GAME_READY = 1, X6_GAME_UNSUPPORTED,
    X6_GAME_INVALID, X6_GAME_FORMAT, X6_GAME_ENGINE, X6_GAME_CATALOG
};

struct x6_game {
    char id[16];        /* MAME short name; the ROM is <id>.zip */
    char title[40];     /* description without the parenthesised variant */
    char variant[24];   /* text inside the parentheses, may be empty */
    char maker[32];
    char year[8];
    uint8_t vertical;   /* swapped-axis (portrait) monitor */
    uint8_t available;  /* enum x6_game_availability; only READY may launch */
    uint8_t imperfect;  /* driver flags report imperfect/missing features */
    uint8_t folder;     /* browser directory; never passed to an emulator */
    char path[256];     /* discovered SD path; empty for synthetic folders */
};

enum x6_menu_action { X6_MENU_NONE, X6_MENU_LAUNCH, X6_MENU_BACK };

struct x6_menu {
    const struct x6_game *games;
    uint32_t count, selected;
    uint32_t previous_mask, held_frames, frame;
    int32_t scroll_q8, target_scroll, cursor_q8;
    uint32_t nudge_frames, notice_frames;
    char notice[48];
    char location[48];
    uint32_t can_back;
};

void x6_menu_init(struct x6_menu *menu, const struct x6_game *games, uint32_t count,
                  uint32_t selected);
/* Feed one frame of player-0 buttons. LAUNCH means games[selected] was chosen
 * and its ROM is present. */
enum x6_menu_action x6_menu_update(struct x6_menu *menu, uint32_t mask);
void x6_menu_notice(struct x6_menu *menu, const char *text);
void x6_menu_location(struct x6_menu *menu, const char *location, uint32_t can_back);
void x6_menu_draw(const struct x6_menu *menu, struct x6_surface *s);

void x6_ui_draw_loading(struct x6_surface *s, const struct x6_game *game);
/* In-game overlay: a fading return hint, then a fill bar while held. */
void x6_ui_draw_game_hint(struct x6_surface *s, uint32_t game_frames, uint32_t hold_frames);
/* Pause menu over the frozen game picture. dim_first darkens the picture once.
 * note (may be null) is a small line under the items. */
#define X6_PAUSE_MAX 6u
void x6_ui_draw_pause(struct x6_surface *s, const char *title, const char *const *items, uint32_t count,
                      uint32_t selected, const char *note, uint32_t dim_first);

/* Settings screen. Each item is "Name\tValue"; the value is right-aligned. */
void x6_ui_draw_settings(struct x6_surface *s, const char *const *items, uint32_t count, uint32_t selected,
                         const char *note);
#endif

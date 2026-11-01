/* Arcade game list: input handling, easing animation and layout. */
#include "x6_ui.h"

/* Higher-contrast palette (2026-11-01): darker ground, brighter text, solid selection. */
#define BG_TOP 0xff05060du
#define BG_BOTTOM 0xff0d0a1du
#define TEXT 0xffffffffu
#define TEXT_DIM 0xffd6dbeeu
#define TEXT_FAINT 0xff9ca4c4u
#define TEXT_DISABLED 0xffa0a0a0u
#define ACCENT 0xffff4d6au
#define WARN 0xffffbe4cu
#define PANEL 0xf0c42a49u
#define RULE 0x40ffffffu
#define SHADE 0xa0000000u

#define MARGIN 16
#define LIST_TOP 46
#define ROW_H 30
#define VISIBLE_ROWS 4
#define LIST_W (X6_UI_WIDTH - 2 * MARGIN)
#define DETAIL_Y 172
#define FOOTER_Y 214

#define REPEAT_DELAY 18u
#define REPEAT_RATE 5u
#define NUDGE_FRAMES 12u
#define NOTICE_FRAMES 150u
#define DIRECTIONS (X6_BTN_UP | X6_BTN_DOWN | X6_BTN_LEFT | X6_BTN_RIGHT)

static void copy_text(char *out, uint32_t capacity, const char *in)
{
    uint32_t i = 0;
    if (!capacity) return;
    if (in) for (; in[i] && i + 1 < capacity; ++i) out[i] = in[i];
    out[i] = 0;
}

static uint32_t append(char *out, uint32_t used, uint32_t capacity, const char *in)
{
    while (in && *in && used + 1 < capacity) out[used++] = *in++;
    if (capacity) out[used < capacity ? used : capacity - 1] = 0;
    return used;
}

/* Copy text, cutting it with "..." when wider than max_width. */
static void fit_text(char *out, uint32_t capacity, const char *in, int32_t scale, int32_t max_width)
{
    copy_text(out, capacity, in);
    if (x6_text_width(out, scale) <= max_width) return;
    uint32_t length = 0;
    while (out[length]) ++length;
    while (length > 0) {
        --length;
        while (length > 0 && out[length - 1] == ' ') --length;
        if (length + 4 > capacity) continue;
        out[length] = '.';out[length + 1] = '.';out[length + 2] = '.';out[length + 3] = 0;
        if (x6_text_width(out, scale) <= max_width) return;
    }
    copy_text(out, capacity, "...");
}

static int32_t target_scroll(const struct x6_menu *m)
{
    int32_t top = m->target_scroll / ROW_H, selected = (int32_t)m->selected;
    if (selected < top) top = selected;
    if (selected >= top + VISIBLE_ROWS) top = selected - VISIBLE_ROWS + 1;
    int32_t last = (int32_t)m->count - VISIBLE_ROWS;
    if (top > last) top = last;
    if (top < 0) top = 0;
    return top * ROW_H;
}

static int32_t ease(int32_t current_q8, int32_t target_q8)
{
    int32_t delta = target_q8 - current_q8;
    if (delta > -96 && delta < 96) return target_q8;
    return current_q8 + delta / 3;
}

void x6_menu_init(struct x6_menu *m, const struct x6_game *games, uint32_t count, uint32_t selected)
{
    *m = (struct x6_menu){0};
    m->games = games;m->count = games ? count : 0;
    m->selected = selected < m->count ? selected : 0;
    /* Ignore buttons still held from the launcher or from leaving a game. */
    m->previous_mask = 0xffffffffu;
    m->target_scroll = target_scroll(m);
    m->scroll_q8 = m->target_scroll * 256;
    m->cursor_q8 = (int32_t)m->selected * ROW_H * 256;
}

void x6_menu_notice(struct x6_menu *m, const char *text)
{
    copy_text(m->notice, sizeof(m->notice), text);
    m->notice_frames = m->notice[0] ? NOTICE_FRAMES : 0;
}

void x6_menu_location(struct x6_menu *m, const char *location, uint32_t can_back)
{
    copy_text(m->location, sizeof m->location, location);
    m->can_back = can_back != 0;
}

static void move(struct x6_menu *m, int32_t step, uint32_t wrap)
{
    if (!m->count || !step) return;
    int32_t next = (int32_t)m->selected + step, last = (int32_t)m->count - 1;
    if (next < 0) next = wrap && m->selected == 0 ? last : 0;
    if (next > last) next = wrap && (int32_t)m->selected == last ? 0 : last;
    if (m->selected != (uint32_t)next) m->notice_frames = 0;
    m->selected = (uint32_t)next;
    m->target_scroll = target_scroll(m);
}

static const char *disabled_reason(uint8_t state)
{
    switch (state) {
    case X6_GAME_UNSUPPORTED: return "No driver in this MAME build";
    case X6_GAME_INVALID: return "Empty, unreadable or invalid ROM file";
    case X6_GAME_FORMAT: return "Unsupported file format or ROM name";
    default: return "ROM file is missing";
    }
}

enum x6_menu_action x6_menu_update(struct x6_menu *m, uint32_t mask)
{
    enum x6_menu_action action = X6_MENU_NONE;
    uint32_t pressed = mask & ~m->previous_mask;
    m->previous_mask = mask;
    m->held_frames = mask & DIRECTIONS ? m->held_frames + 1 : 0;
    uint32_t repeat = m->held_frames > REPEAT_DELAY && (m->held_frames - REPEAT_DELAY) % REPEAT_RATE == 0;
    uint32_t steer = pressed & DIRECTIONS ? pressed : repeat ? mask : 0;
    if (steer & X6_BTN_UP) move(m, -1, pressed != 0);
    else if (steer & X6_BTN_DOWN) move(m, 1, pressed != 0);
    else if (steer & X6_BTN_LEFT) move(m, -VISIBLE_ROWS, 0);
    else if (steer & X6_BTN_RIGHT) move(m, VISIBLE_ROWS, 0);
    if ((pressed & X6_BTN_B) && m->can_back) action = X6_MENU_BACK;
    else if (pressed & (X6_BTN_A | X6_BTN_START) && m->count) {
        const struct x6_game *game = &m->games[m->selected];
        if (game->available == X6_GAME_READY) {
            action = X6_MENU_LAUNCH;
        } else {
            if (game->available == X6_GAME_MISSING) {
                char text[48];
                uint32_t used = append(text, 0, sizeof(text), "Missing ROM: ");
                used = append(text, used, sizeof(text), game->id);
                append(text, used, sizeof(text), ".zip");
                x6_menu_notice(m, text);
            } else x6_menu_notice(m, disabled_reason(game->available));
            m->nudge_frames = NUDGE_FRAMES;
        }
    }
    m->scroll_q8 = ease(m->scroll_q8, m->target_scroll * 256);
    m->cursor_q8 = ease(m->cursor_q8, (int32_t)m->selected * ROW_H * 256);
    if (m->nudge_frames) --m->nudge_frames;
    if (m->notice_frames) --m->notice_frames;
    ++m->frame;
    return action;
}

static void keycap(struct x6_surface *s, int32_t *x, int32_t y, const char *key, const char *label)
{
    int32_t w = x6_text_width(key, 1) + 8;
    x6_gfx_round_rect(s, *x, y, w, 13, 4, 0x58ffffffu);
    x6_text_draw(s, *x + 4, y + 3, key, 1, TEXT);
    *x = x6_text_draw(s, *x + w + 5, y + 3, label, 1, TEXT_DIM) + 12;
}

static void draw_background(struct x6_surface *s)
{
    x6_gfx_vgradient(s, 0, s->height, BG_TOP, BG_BOTTOM);
    /* Soft glow behind the header. */
    for (int32_t i = 0; i < 6; ++i)
        x6_gfx_round_rect(s, 200 + i * 6, -40 + i * 6, 160 - i * 12, 100 - i * 12, 40, 0x05ff5d73u);
}

static void draw_header(struct x6_surface *s, const struct x6_menu *m)
{
    x6_gfx_disc(s, MARGIN + 3, 20, 3, ACCENT);
    x6_text_draw(s, MARGIN + 12, m->location[0] ? 7 : 13, "X6 Reloaded", 2, TEXT);
    if (m->location[0]) {
        char path[48];
        fit_text(path, sizeof path, m->location, 1, LIST_W);
        x6_text_draw(s, MARGIN, 27, path, 1, TEXT_DIM);
    }
    if (m->count) {
        char count[24];
        uint32_t used = 0, n = m->selected + 1, total = m->count;
        char digits[12];int32_t d = 0;
        do {digits[d++] = (char)('0' + n % 10);n /= 10;} while (n && d < 11);
        while (d) count[used++] = digits[--d];
        used = append(count, used, sizeof(count), " / ");
        do {digits[d++] = (char)('0' + total % 10);total /= 10;} while (total && d < 11);
        while (d && used + 1 < sizeof(count)) count[used++] = digits[--d];
        count[used] = 0;
        x6_text_draw(s, X6_UI_WIDTH - MARGIN - x6_text_width(count, 1), 18, count, 1, TEXT_FAINT);
    }
    x6_gfx_fill(s, MARGIN, 38, LIST_W, 1, RULE);
}

static void draw_row(struct x6_surface *list, const struct x6_game *game, int32_t y, uint32_t selected)
{
    int32_t right = MARGIN + LIST_W - 6;
    int32_t tag_w = 0;
    if (game->folder) {
        tag_w = x6_text_width(game->year, 1) + 14;
        x6_text_draw(list, right - tag_w, y + 12, game->year, 1, selected ? TEXT : TEXT_FAINT);
        x6_text_draw(list, right - 5, y + 12, ">", 1, TEXT);
    } else if (game->available != X6_GAME_READY) {
        const char *tag = game->available == X6_GAME_UNSUPPORTED ? "UNSUPPORTED"
            : game->available == X6_GAME_INVALID ? "INVALID"
            : game->available == X6_GAME_FORMAT ? "FORMAT" : "NO ROM";
        tag_w = x6_text_width(tag, 1) + 10;
        x6_gfx_round_rect(list, right - tag_w, y + 9, tag_w, 13, 4, 0x26808080u);
        x6_text_draw(list, right - tag_w + 5, y + 12, tag, 1, TEXT_DISABLED);
    } else if (game->year[0]) {
        tag_w = x6_text_width(game->year, 1);
        x6_text_draw(list, right - tag_w, y + 12, game->year, 1, selected ? TEXT : TEXT_FAINT);
    }
    char title[40];
    int32_t left = game->folder ? 28 : 6;
    fit_text(title, sizeof(title), game->title, 2, LIST_W - left - tag_w - 12);
    uint32_t color = game->available != X6_GAME_READY ? TEXT_DISABLED : selected ? TEXT : TEXT_DIM;
    if (game->folder) {
        x6_gfx_fill(list, MARGIN + 6, y + 9, 7, 3, WARN);
        x6_gfx_round_rect(list, MARGIN + 6, y + 11, 15, 11, 2, WARN);
        x6_gfx_fill(list, MARGIN + 8, y + 14, 11, 1, 0xff80592cu);
    }
    x6_text_draw(list, MARGIN + left, y + 8, title, 2, color);
}

static void draw_details(struct x6_surface *s, const struct x6_menu *m)
{
    if (m->notice_frames) {
        x6_gfx_disc(s, MARGIN + 3, DETAIL_Y + 11, 3, WARN);
        x6_text_draw(s, MARGIN + 12, DETAIL_Y + 8, m->notice, 1, WARN);
        return;
    }
    const struct x6_game *g = &m->games[m->selected];
    char line[64];
    if (g->available != X6_GAME_READY) {
        fit_text(line, sizeof line, disabled_reason(g->available), 1, LIST_W);
        x6_text_draw(s, MARGIN, DETAIL_Y + 2, line, 1, TEXT_DIM);
        const char *name = g->path[0] ? g->path : g->id;
        for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
        fit_text(line, sizeof line, name, 1, LIST_W);
        x6_text_draw(s, MARGIN, DETAIL_Y + 18, line, 1, TEXT_DISABLED);
        return;
    }
    fit_text(line, sizeof(line), g->maker[0] ? g->maker : "Unknown maker", 1, LIST_W);
    x6_text_draw(s, MARGIN, DETAIL_Y + 2, line, 1, TEXT_DIM);
    if (g->folder) {
        x6_text_draw(s, MARGIN, DETAIL_Y + 18, "A or START opens this folder", 1, TEXT_FAINT);
        return;
    }
    const char *parts[4] = {g->year, g->vertical ? "Vertical" : "Horizontal", g->variant,
                            g->imperfect ? "Imperfect emulation" : ""};
    int32_t x = MARGIN;
    for (uint32_t i = 0; i < 4; ++i) {
        if (!parts[i] || !parts[i][0]) continue;
        if (x != MARGIN) {x6_gfx_fill(s, x + 3, DETAIL_Y + 21, 2, 2, TEXT_FAINT);x += 10;}
        x = x6_text_draw(s, x, DETAIL_Y + 18, parts[i], 1, i == 3 ? WARN : TEXT_FAINT);
    }
}

static void draw_footer(struct x6_surface *s, const struct x6_menu *m)
{
    x6_gfx_fill(s, 0, FOOTER_Y, X6_UI_WIDTH, X6_UI_HEIGHT - FOOTER_Y, SHADE);
    x6_gfx_fill(s, 0, FOOTER_Y, X6_UI_WIDTH, 1, RULE);
    int32_t x = MARGIN, y = FOOTER_Y + 7;
    if (m->count) keycap(s, &x, y, "A", m->games[m->selected].folder ? "Open"
        : m->games[m->selected].available == X6_GAME_READY ? "Play" : "Info");
    if (m->can_back) keycap(s, &x, y, "B", "Back");
    else if (m->count) keycap(s, &x, y, "UP/DOWN", "Browse");
    if (m->count || m->can_back) keycap(s, &x, y, "SELECT", "Settings");
}

static void draw_empty(struct x6_surface *s)
{
    static const char title[] = "No games yet";
    static const char line1[] = "Add supported ZIPs to roms/mame";
    static const char line2[] = "Then reboot to refresh the library";
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(title, 2)) / 2, 92, title, 2, TEXT);
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(line1, 1)) / 2, 122, line1, 1, TEXT_DIM);
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(line2, 1)) / 2, 134, line2, 1, TEXT_DIM);
}

void x6_menu_draw(const struct x6_menu *m, struct x6_surface *s)
{
    draw_background(s);
    draw_header(s, m);
    if (!m->count) {draw_empty(s);draw_footer(s, m);return;}
    struct x6_surface list = {s->pixels + LIST_TOP * s->pitch, s->width, VISIBLE_ROWS * ROW_H, s->pitch};
    int32_t scroll = m->scroll_q8 / 256;
    int32_t shake = m->nudge_frames ? ((m->nudge_frames & 2u) ? 3 : -3) : 0;
    int32_t cy = m->cursor_q8 / 256 - scroll;
    uint32_t disabled = m->games[m->selected].available != X6_GAME_READY;
    x6_gfx_round_rect(&list, MARGIN - 6 + shake, cy + 2, LIST_W + 12, ROW_H - 4, 8,
        disabled ? 0xff282b32u : PANEL);
    x6_gfx_round_rect(&list, MARGIN - 6 + shake, cy + 9, 3, ROW_H - 18, 1,
        disabled ? TEXT_DISABLED : TEXT);
    for (uint32_t i = 0; i < m->count; ++i) {
        int32_t y = (int32_t)i * ROW_H - scroll;
        if (y <= -ROW_H || y >= VISIBLE_ROWS * ROW_H) continue;
        draw_row(&list, &m->games[i], y, i == m->selected);
    }
    if (m->count > VISIBLE_ROWS) {
        int32_t track = VISIBLE_ROWS * ROW_H - 8;
        int32_t thumb = track * VISIBLE_ROWS / (int32_t)m->count;
        int32_t span = ((int32_t)m->count - VISIBLE_ROWS) * ROW_H;
        int32_t top = span ? (track - thumb) * scroll / span : 0;
        x6_gfx_round_rect(s, X6_UI_WIDTH - 6, LIST_TOP + 4, 2, track, 1, 0x14ffffffu);
        x6_gfx_round_rect(s, X6_UI_WIDTH - 6, LIST_TOP + 4 + top, 2, thumb < 8 ? 8 : thumb, 1, 0x60ffffffu);
    }
    x6_gfx_fill(s, MARGIN, DETAIL_Y - 6, LIST_W, 1, RULE);
    draw_details(s, m);
    draw_footer(s, m);
}

void x6_ui_draw_loading(struct x6_surface *s, const struct x6_game *game)
{
    draw_background(s);
    char title[40];
    fit_text(title, sizeof(title), game ? game->title : "Arcade", 2, X6_UI_WIDTH - 2 * MARGIN);
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(title, 2)) / 2, 88, title, 2, TEXT);
    if (game && game->maker[0]) {
        char maker[40];
        fit_text(maker, sizeof(maker), game->maker, 1, X6_UI_WIDTH - 2 * MARGIN);
        x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(maker, 1)) / 2, 114, maker, 1, TEXT_DIM);
    }
    x6_gfx_round_rect(s, X6_UI_WIDTH / 2 - 24, 136, 48, 3, 1, 0x20ffffffu);
    x6_gfx_round_rect(s, X6_UI_WIDTH / 2 - 24, 136, 20, 3, 1, ACCENT);
    static const char hint[] = "In a game: MENU for options, hold START for the list";
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(hint, 1)) / 2, 200, hint, 1, TEXT_FAINT);
}

void x6_ui_draw_game_hint(struct x6_surface *s, uint32_t game_frames, uint32_t hold_frames)
{
    static const char hint[] = "Hold START for menu";
    static const char leaving[] = "Returning to menu";
    const char *text = hold_frames ? leaving : hint;
    uint32_t alpha = 255u;
    if (!hold_frames) {
        if (game_frames >= 180u) return;
        if (game_frames > 150u) alpha = (180u - game_frames) * 255u / 30u;
    }
    int32_t w = x6_text_width(hint, 1) + 24, h = 17;
    int32_t x = (X6_UI_WIDTH - w) / 2, y = X6_UI_HEIGHT - h - 8;
    x6_gfx_round_rect(s, x, y, w, h, 8, (alpha * 0xd0u / 255u) << 24 | 0x0e1024u);
    if (hold_frames) {
        uint32_t fill = hold_frames > X6_UI_RETURN_HOLD ? X6_UI_RETURN_HOLD : hold_frames;
        x6_gfx_round_rect(s, x, y, (int32_t)((uint32_t)w * fill / X6_UI_RETURN_HOLD), h, 8, 0xc0ff5d73u);
    }
    x6_text_draw(s, x + (w - x6_text_width(text, 1)) / 2, y + 5, text, 1, alpha << 24 | 0xf4f5fbu);
}

void x6_ui_draw_pause(struct x6_surface *s, const char *title, const char *const *items, uint32_t count,
                      uint32_t selected, const char *note, uint32_t dim_first)
{
    if (dim_first) x6_gfx_fill(s, 0, 0, X6_UI_WIDTH, X6_UI_HEIGHT, 0xa8000000u);
    if (count > X6_PAUSE_MAX) count = X6_PAUSE_MAX;
    int32_t w = 220, h = 34 + (int32_t)count * 26 + 34;
    int32_t x = (X6_UI_WIDTH - w) / 2, y = (X6_UI_HEIGHT - h) / 2;
    x6_gfx_round_rect(s, x, y, w, h, 10, 0xff10121fu);
    x6_gfx_round_rect(s, x, y, w, 3, 1, ACCENT);
    char t[40];
    fit_text(t, sizeof(t), title ? title : "Paused", 1, w - 24);
    x6_text_draw(s, x + (w - x6_text_width(t, 1)) / 2, y + 12, t, 1, TEXT_DIM);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t iy = y + 30 + (int32_t)i * 26;
        char label[40];
        fit_text(label, sizeof(label), items[i], 2, w - 28);
        if (i == selected) x6_gfx_round_rect(s, x + 10, iy, w - 20, 22, 6, PANEL);
        x6_text_draw(s, x + (w - x6_text_width(label, 2)) / 2, iy + 4, label, 2, i == selected ? TEXT : TEXT_DIM);
    }
    if (note && note[0]) x6_text_draw(s, x + (w - x6_text_width(note, 1)) / 2, y + h - 30, note, 1, WARN);
    static const char foot[] = "A select   B / MENU resume";
    x6_text_draw(s, x + (w - x6_text_width(foot, 1)) / 2, y + h - 16, foot, 1, TEXT_FAINT);
}

void x6_ui_draw_settings(struct x6_surface *s, const char *const *items, uint32_t count, uint32_t selected,
                         const char *note)
{
    draw_background(s);
    x6_gfx_disc(s, MARGIN + 3, 20, 3, ACCENT);
    x6_text_draw(s, MARGIN + 12, 13, "SETTINGS", 2, TEXT);
    x6_gfx_fill(s, MARGIN, 38, LIST_W, 1, RULE);
    for (uint32_t i = 0; i < count && i < 6; ++i) {
        int32_t y = 46 + (int32_t)i * 26;
        char name[32], value[24];uint32_t k = 0;const char *c = items[i];
        while (*c && *c != '\t' && k + 1 < sizeof name) name[k++] = *c++;
        name[k] = 0;k = 0;
        if (*c == '\t') ++c;
        while (*c && k + 1 < sizeof value) value[k++] = *c++;
        value[k] = 0;
        if (i == selected) x6_gfx_round_rect(s, MARGIN - 6, y, LIST_W + 12, 22, 7, PANEL);
        x6_text_draw(s, MARGIN + 4, y + 4, name, 2, i == selected ? TEXT : TEXT_DIM);
        char shown[28];uint32_t n = 0;
        if (i == selected) {shown[n++] = '<';shown[n++] = ' ';}
        for (const char *v = value; *v && n + 3 < sizeof shown; ++v) shown[n++] = *v;
        if (i == selected) {shown[n++] = ' ';shown[n++] = '>';}
        shown[n] = 0;
        x6_text_draw(s, MARGIN + LIST_W - 4 - x6_text_width(shown, 1), y + 8, shown, 1, i == selected ? TEXT : TEXT_FAINT);
    }
    if (note && note[0]) x6_text_draw(s, MARGIN, FOOTER_Y - 14, note, 1, WARN);
    x6_gfx_fill(s, 0, FOOTER_Y, X6_UI_WIDTH, X6_UI_HEIGHT - FOOTER_Y, SHADE);
    x6_gfx_fill(s, 0, FOOTER_Y, X6_UI_WIDTH, 1, RULE);
    int32_t x = MARGIN, y = FOOTER_Y + 7;
    keycap(s, &x, y, "LEFT/RIGHT", "Change");
    keycap(s, &x, y, "B", "Back");
}

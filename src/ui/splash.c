/* Original arcade-cabinet boot card. No game art, logos, or external assets.
 * Time is in the X6's 10 ms ticks. Colour motion, no full-screen flashing. */
#include "x6_ui.h"

static void centered(struct x6_surface *s, int32_t y, const char *text, int32_t scale, uint32_t color)
{
    x6_text_draw(s, (X6_UI_WIDTH - x6_text_width(text, scale)) / 2, y, text, scale, color);
}

void x6_ui_draw_splash(struct x6_surface *s, uint32_t elapsed_ticks)
{
    uint32_t t = elapsed_ticks > 120u ? 120u : elapsed_ticks;
    x6_gfx_vgradient(s, 0, s->height, 0xff05060du, 0xff15102cu);
    /* A perspective floor under a small cabinet marquee. */
    for (int32_t y = 166, gap = 3; y < 240; y += gap, gap += 3)
        x6_gfx_fill(s, 0, y, 320, 1, 0xff352445u);
    for (int32_t i = -5; i <= 5; ++i) {
        for (int32_t y = 167; y < 240; ++y)
            x6_gfx_fill(s, 160 + i * (y - 150) / 2, y, 1, 1, 0xff352445u);
    }
    for (uint32_t i = 0; i < 24; ++i) {
        int32_t x = (int32_t)((i * 73u + 19u) % 304u) + 8;
        int32_t y = (int32_t)((i * 37u + 11u) % 146u) + 8;
        x6_gfx_fill(s, x, y, 1, 1, i % 3 ? 0xff777491u : 0xff43dce6u);
    }
    x6_gfx_round_rect(s, 24, 47, 272, 111, 10, 0xff47213du);
    x6_gfx_round_rect(s, 26, 49, 268, 107, 9, 0xff080b19u);
    x6_gfx_fill(s, 39, 60, (int32_t)(242u * t / 120u), 2, 0xff43dce6u);
    centered(s, 73, "X6", 4, 0xff253759u);
    x6_text_draw(s, (320 - x6_text_width("X6", 4)) / 2 - 2, 71, "X6", 4, 0xff43dce6u);
    centered(s, 113, "RELOADED", 3, 0xffff4d6au);
    centered(s, 177, "SMALL MACHINE. BIG ARCADE.", 1, 0xffd6dbeeu);
    centered(s, 205, "SD BOOT", 1, 0xff9ca4c4u);
    for (uint32_t i = 0; i < 3; ++i)
        x6_gfx_disc(s, 148 + (int32_t)i * 12, 226, 2,
                    (t / 20u) % 3u == i ? 0xff43dce6u : 0xff47334du);
}

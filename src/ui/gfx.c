/* Drawing primitives and pixel-font text for the arcade menu.
 * Integer-only, clipped, and free of libc so it links into the Melis module. */
#include "x6_ui.h"

static uint32_t blend(uint32_t dst, uint32_t src, uint32_t alpha)
{
    if (alpha >= 255u) return src | 0xff000000u;
    if (!alpha) return dst;
    uint32_t inv = 255u - alpha;
    uint32_t rb = ((src & 0xff00ffu) * alpha + (dst & 0xff00ffu) * inv + 0x800080u) >> 8;
    uint32_t g = ((src & 0x00ff00u) * alpha + (dst & 0x00ff00u) * inv + 0x008000u) >> 8;
    return 0xff000000u | (rb & 0xff00ffu) | (g & 0x00ff00u);
}

static void plot(struct x6_surface *s, int32_t x, int32_t y, uint32_t color, uint32_t coverage)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) return;
    uint32_t alpha = ((color >> 24) * coverage + 127u) / 255u;
    uint32_t *p = &s->pixels[y * s->pitch + x];
    *p = blend(*p, color & 0xffffffu, alpha);
}

void x6_gfx_fill(struct x6_surface *s, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > s->width ? s->width : x + w;
    int32_t y1 = y + h > s->height ? s->height : y + h;
    uint32_t alpha = color >> 24, rgb = color & 0xffffffu;
    for (int32_t row = y0; row < y1; ++row) {
        uint32_t *p = &s->pixels[row * s->pitch];
        for (int32_t col = x0; col < x1; ++col) p[col] = blend(p[col], rgb, alpha);
    }
}

static uint32_t lerp_channel(uint32_t a, uint32_t b, int32_t t, int32_t span)
{
    return (uint32_t)(((int32_t)a * (span - t) + (int32_t)b * t) / span);
}

void x6_gfx_vgradient(struct x6_surface *s, int32_t y, int32_t h, uint32_t top, uint32_t bottom)
{
    if (h <= 0) return;
    for (int32_t row = 0; row < h; ++row) {
        int32_t span = h > 1 ? h - 1 : 1;
        uint32_t c = 0xff000000u;
        for (int32_t shift = 0; shift <= 16; shift += 8)
            c |= lerp_channel((top >> shift) & 255u, (bottom >> shift) & 255u, row, span) << shift;
        x6_gfx_fill(s, 0, y + row, s->width, 1, c);
    }
}

/* Coverage (0..255) of a pixel at squared distance d2 (in 1/16 px units squared)
 * from a circle of radius r: a one-pixel linear ramp across the edge. */
static uint32_t edge_coverage(int32_t d2_16, int32_t r)
{
    int32_t r16 = r * 16;
    int32_t inner = (r16 - 8) * (r16 - 8), outer = (r16 + 8) * (r16 + 8);
    if (d2_16 <= inner) return 255u;
    if (d2_16 >= outer) return 0u;
    return (uint32_t)((outer - d2_16) * 255 / (outer - inner)); /* fits int32 for r < 64 */
}

void x6_gfx_round_rect(struct x6_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t radius, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius <= 0) {x6_gfx_fill(s, x, y, w, h, color);return;}
    x6_gfx_fill(s, x, y + radius, w, h - 2 * radius, color);
    x6_gfx_fill(s, x + radius, y, w - 2 * radius, radius, color);
    x6_gfx_fill(s, x + radius, y + h - radius, w - 2 * radius, radius, color);
    for (int32_t dy = 0; dy < radius; ++dy) {
        for (int32_t dx = 0; dx < radius; ++dx) {
            /* Pixel centre relative to the corner circle centre, in 1/16 px. */
            int32_t px = (radius - dx) * 16 - 8, py = (radius - dy) * 16 - 8;
            uint32_t cov = edge_coverage(px * px + py * py, radius);
            if (!cov) continue;
            plot(s, x + dx, y + dy, color, cov);
            plot(s, x + w - 1 - dx, y + dy, color, cov);
            plot(s, x + dx, y + h - 1 - dy, color, cov);
            plot(s, x + w - 1 - dx, y + h - 1 - dy, color, cov);
        }
    }
}

void x6_gfx_disc(struct x6_surface *s, int32_t cx, int32_t cy, int32_t radius, uint32_t color)
{
    for (int32_t dy = -radius - 1; dy <= radius; ++dy) {
        for (int32_t dx = -radius - 1; dx <= radius; ++dx) {
            int32_t px = dx * 16 + 8, py = dy * 16 + 8;
            uint32_t cov = edge_coverage(px * px + py * py, radius);
            if (cov) plot(s, cx + dx, cy + dy, color, cov);
        }
    }
}

static int32_t glyph_index(char c)
{
    unsigned char u = (unsigned char)c;
    return u >= 32u && u <= 126u ? (int32_t)u - 32 : '?' - 32;
}

static uint32_t ink(int32_t index, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || x >= x6_font_width[index] || y >= X6_FONT_ROWS) return 0;
    return (x6_font_rows[index][y] >> x) & 1u;
}

int32_t x6_text_width(const char *text, int32_t scale)
{
    int32_t width = 0;
    if (!text) return 0;
    for (const char *p = text; *p; ++p) width += (x6_font_width[glyph_index(*p)] + 1) * scale;
    return width > 0 ? width - scale : 0;
}

int32_t x6_text_draw(struct x6_surface *s, int32_t x, int32_t y, const char *text,
                     int32_t scale, uint32_t color)
{
    if (!text) return x;
    for (const char *p = text; *p; ++p) {
        int32_t g = glyph_index(*p), w = x6_font_width[g];
        for (int32_t gy = 0; gy < X6_FONT_ROWS; ++gy) {
            for (int32_t gx = 0; gx < w; ++gx) {
                uint32_t P = ink(g, gx, gy);
                if (scale == 2) {
                    /* EPX: round diagonal steps instead of blocky doubling. */
                    uint32_t A = ink(g, gx, gy - 1), B = ink(g, gx + 1, gy);
                    uint32_t C = ink(g, gx - 1, gy), D = ink(g, gx, gy + 1);
                    uint32_t q[4] = {P, P, P, P};
                    if (C == A && C != D && A != B) q[0] = A;
                    if (A == B && A != C && B != D) q[1] = B;
                    if (D == C && D != B && C != A) q[2] = C;
                    if (B == D && B != A && D != C) q[3] = D;
                    for (int32_t i = 0; i < 4; ++i)
                        if (q[i]) plot(s, x + gx * 2 + (i & 1), y + gy * 2 + (i >> 1), color, 255u);
                } else if (P) {
                    for (int32_t sy = 0; sy < scale; ++sy)
                        for (int32_t sx = 0; sx < scale; ++sx)
                            plot(s, x + gx * scale + sx, y + gy * scale + sy, color, 255u);
                }
            }
        }
        x += (w + 1) * scale;
    }
    return x;
}

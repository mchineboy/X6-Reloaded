/* Pure, bounded startup progress renderer. Counts are measured; the moving
 * segment is indeterminate because directory totals are not known in advance. */
#include "x6_library.h"
static void number(char *out, uint32_t value, const char *suffix)
{
    char reverse[12];uint32_t n = 0, at = 0;
    do {reverse[n++] = (char)('0' + value % 10);value /= 10;} while (value);
    while (n) out[at++] = reverse[--n];
    while (*suffix && at + 1 < 48) out[at++] = *suffix++;out[at] = 0;
}
void x6_ui_draw_scan(struct x6_surface *s, const struct x6_scan_info *p)
{
    if (!s || !p) return;
    x6_gfx_fill(s, 0, 0, s->width, s->height, 0xff05060du);
    x6_text_draw(s, 16, 15, "X6 Reloaded", 2, 0xffffffffu);
    x6_gfx_fill(s, 16, 43, 288, 1, 0x40ffffffu);
    const char *phase = "Preparing library";
    switch (p->phase) {
    case X6_SCAN_TITLES: phase = "Reading title indexes";break;
    case X6_SCAN_OPENING: phase = "Creating library index";break;
    case X6_SCAN_SCANNING: phase = "Scanning games";break;
    case X6_SCAN_FINALIZING: phase = "Saving library index";break;
    case X6_SCAN_SORTING: phase = "Sorting game titles";break;
    case X6_SCAN_READY: phase = "Library ready";break;
    case X6_SCAN_MENU: phase = "Opening game menu";break;
    case X6_SCAN_FAILED: phase = "Library scan stopped";break;
    default: break;
    }
    x6_text_draw(s, 16, 65, phase, 2, 0xffffffffu);
    if (p->phase == X6_SCAN_SCANNING)
        x6_text_draw(s, 16, 94, x6_library_name(p->library), 1, 0xffd6dbeeu);
    char line[48];number(line, p->checked, " files checked");
    x6_text_draw(s, 16, 126, line, 1, 0xffd6dbeeu);
    number(line, p->indexed, " games found");x6_text_draw(s, 16, 145, line, 1, 0xffffffffu);
    x6_gfx_round_rect(s, 16, 175, 288, 6, 3, 0xff282b32u);
    uint32_t done = p->phase == X6_SCAN_READY || p->phase == X6_SCAN_MENU;
    uint32_t x = done || p->phase == X6_SCAN_FAILED ? 0 : ((p->checked / 16 + p->phase * 3) % 72) * 216 / 71;
    x6_gfx_round_rect(s, 16 + (int32_t)x, 175, done ? 288 : 72, 6, 3,
        p->phase == X6_SCAN_FAILED ? 0xffffbe4cu : done ? 0xff6bd99bu : 0xffff4d6au);
    x6_text_draw(s, 16, 201, p->phase == X6_SCAN_FAILED ? x6_scan_error_message(p) : "Please wait", 1, 0xffd6dbeeu);
    if (p->phase == X6_SCAN_FAILED) {
        number(line, p->errors, "");
        x6_text_draw(s, 16, 219, "Diagnostic code:", 1, 0xffa0a0a0u);
        x6_text_draw(s, 100, 219, line, 1, 0xffffbe4cu);
    }
}

#include "x6_library.h"
const char *x6_library_name(uint32_t library)
{
    static const char *const names[] = {"MAME Arcade", "Original Arcade", "NES / Famicom",
        "Super Nintendo", "Mega Drive / Genesis", "Game Boy", "Game Boy Color", "Game Boy Advance"};
    return library < X6_LIBRARY_COUNT ? names[library] : "Library";
}
uint32_t x6_library_bucket(const char *title)
{
    uint32_t c = (unsigned char)title[0];
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'F') return 1;
    if (c >= 'G' && c <= 'L') return 2;
    if (c >= 'M' && c <= 'R') return 3;
    if (c >= 'S' && c <= 'Z') return 4;
    return 0;
}
const char *x6_scan_error_message(const struct x6_scan_info *p)
{
    uint32_t e = p ? p->errors : 0;
    if (e & X6_SCAN_OPEN) return "Cannot open library cache";
    if (e & X6_SCAN_WRITE) return "Cannot write library cache";
    if (e & X6_SCAN_CLOSE_FILE) return "Cannot close library cache";
    if (e & X6_SCAN_CLOSE_DIR) return "Cannot close game folder";
    if (e & X6_SCAN_LIMIT) return "Game catalog limit reached";
    if (e & X6_SCAN_DIRECTORY) return "Game folder scan failed";
    if (e & X6_SCAN_PATH) return "ROM path is too long";
    if (e & X6_SCAN_TITLE) return "Cannot read title index";
    return "Library scan did not finish";
}

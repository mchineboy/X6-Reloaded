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

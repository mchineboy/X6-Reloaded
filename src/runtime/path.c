#include "x6_path.h"
uint32_t x6_path_normalize(char out[256], const char *path)
{
    if (!out || !path || (path[0] != 'F' && path[0] != 'f') || path[1] != ':'
        || (path[2] != '/' && path[2] != '\\')) return 0;
    out[0] = 'F';out[1] = ':';uint32_t start = 3;
    for (uint32_t i = 2; i < 256; ++i) {
        unsigned char c = (unsigned char)path[i];
        if ((c && c < 32) || c == ':') return 0;
        out[i] = c == '/' ? '\\' : (char)c;
        if (out[i] == '\\' || !c) {
            if (i >= start && i - start == 2 && path[start] == '.' && path[start + 1] == '.') return 0;
            start = i + 1;
        }
        if (!c) return i > 3;
    }
    return 0;
}

#include "x6_runtime.h"
#include "x6_path.h"
void *x6_fs_open(const char *path, const char *mode)
{
    char normalized[256];
    if (!mode || !x6_path_normalize(normalized, path)) return 0;
    return x6_fs_open_raw(normalized, mode);
}

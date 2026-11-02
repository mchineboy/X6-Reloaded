#ifndef X6_PATH_H
#define X6_PATH_H
#include <stdint.h>
/* Canonical, bounded SD-file paths at the Melis boundary. */
uint32_t x6_path_normalize(char out[256], const char *path);
#endif

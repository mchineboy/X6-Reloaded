#ifndef X6_DIRECTORY_H
#define X6_DIRECTORY_H
#include <stdint.h>

#define X6_DIRECTORY_LIMIT 4096u
struct x6_directory_entry {
    char name[256];
    uint32_t attributes, size_low, size_high;
};
typedef int32_t (*x6_directory_visitor)(const struct x6_directory_entry *, void *);
typedef int32_t (*x6_directory_walker)(const char *, x6_directory_visitor, void *);
/* Read-only, one directory, bounded traversal. Missing folders return -1.
 * Visitor: 0 continues, 1 stops successfully, negative aborts. Always closes. */
int32_t x6_directory_walk(const char *path, x6_directory_visitor visitor, void *context);
uint32_t x6_directory_join(char *out, uint32_t capacity, const char *directory, const char *name);

/* Exact V1.241017 syscall wrappers supplied by the host; tests supply doubles. */
void *x6_directory_open_raw(const char *path);
const uint8_t *x6_directory_read_raw(void *handle);
int32_t x6_directory_close_raw(void *handle);
#endif

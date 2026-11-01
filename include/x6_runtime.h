#ifndef X6_RUNTIME_H
#define X6_RUNTIME_H
#include <stdint.h>

/* Version-locked ARM wrappers inferred from V1.241017 SD fba.mod.
 * Open/read/close and bounded heap/seek calls passed on-device in tests 002/003.
 * x6_fs_write (fwrite, svc 0x2605) is used only for small per-game settings files
 * on F:. No format, raw-disk, or flash service is declared here. */
void *x6_fs_open(const char *path, const char *mode);
uint32_t x6_fs_read(void *ptr, uint32_t size, uint32_t count, void *handle);
uint32_t x6_fs_write(const void *ptr, uint32_t size, uint32_t count, void *handle);
int32_t x6_fs_close(void *handle);
int32_t x6_fs_seek(void *handle, int32_t offset, int32_t origin);
int32_t x6_fs_tell(void *handle);

/* Raw heap-zero imports; callers use the bounded C adapters below. */
void *x6_heap_alloc(uint32_t bytes);
void x6_heap_free(void *ptr);
void *x6_heap_resize(void *ptr, uint32_t bytes);
void *x6_malloc(uint32_t bytes);
void *x6_calloc(uint32_t count, uint32_t bytes);
void *x6_realloc(void *ptr, uint32_t bytes);
void x6_free(void *ptr);

struct x6_runtime_diagnostic {
    uint32_t read_ok;
    uint32_t seek_ok;
    uint32_t heap_ok;
    uint32_t grow_ok;
};

/* Fixed marker only; heap allocations <=8192 bytes, all released before return.
 * Runtime calls can fail; status fields report failure without inventing success. */
struct x6_runtime_diagnostic x6_check_runtime(void);
#endif

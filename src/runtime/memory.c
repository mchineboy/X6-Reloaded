/* Small adapters over the version-locked heap-zero imports. No private arena,
 * SDK code, silent success stubs, or unconditional pointer reuse. */
#include "x6_runtime.h"

void *x6_malloc(uint32_t bytes)
{
    return bytes ? x6_heap_alloc(bytes) : 0;
}

void x6_free(void *ptr)
{
    if (ptr)
        x6_heap_free(ptr);
}

void *x6_calloc(uint32_t count, uint32_t bytes)
{
    uint64_t total = (uint64_t)count * bytes;
    if (!total || total > UINT32_MAX)
        return 0;
    uint8_t *ptr = x6_malloc((uint32_t)total);
    if (ptr)
        for (uint32_t i = 0; i < (uint32_t)total; ++i)
            ptr[i] = 0;
    return ptr;
}

void *x6_realloc(void *ptr, uint32_t bytes)
{
    if (!ptr)
        return x6_malloc(bytes);
    if (!bytes) {
        x6_free(ptr);
        return 0;
    }
    return x6_heap_resize(ptr, bytes);
}

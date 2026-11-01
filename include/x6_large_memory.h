#ifndef X6_LARGE_MEMORY_H
#define X6_LARGE_MEMORY_H
#include <stdint.h>
void *x6_page_alloc(uint32_t pages);
void x6_page_free(void *ptr);
void *x6_libc_alloc(uint32_t bytes);
void *x6_libc_resize(void *ptr,uint32_t bytes);
void x6_libc_free(void *ptr);
uint32_t x6_libc_live_bytes(void);
#endif

/* Hybrid allocator: same 2 MiB split and 1 KiB page units as stock FBA.
 * Every allocation owns an aligned size/family header, permitting safe copying
 * across families during realloc. Finite 16 MiB block / 24 MiB live budgets. */
#include <stdint.h>
#include "x6_runtime.h"
#include "x6_large_memory.h"
#include "x6_guard.h"
#define MAGIC 0x58364d45u
struct header {uint32_t magic,bytes,pages,reserved;};
static uint32_t live;
uint32_t x6_libc_live_bytes(void) {return live;}
void *x6_libc_alloc(uint32_t n) {
 if(!n || n>16u*1024*1024 || n>24u*1024*1024-live) return 0;
 uint32_t total=n+sizeof(struct header),pages=total>=2097152u?(total+1023u)/1024u:0;
 struct header *h=pages?x6_page_alloc(pages):x6_malloc(total);
 if(!h) return 0;
 if((uintptr_t)h&7u) {if(pages)x6_page_free(h);else x6_free(h);return 0;}
 *h=(struct header){MAGIC,n,pages,0};live+=n;return h+1;
}
void x6_libc_free(void *p) {
 if(!p) return;struct header *h=(struct header *)p-1;
 if(h->magic!=MAGIC || h->bytes>live) x6_runtime_abort(9);
 live-=h->bytes;h->magic=0;
 if(h->pages)x6_page_free(h);else x6_free(h);
}
void *x6_libc_resize(void *p,uint32_t n) {
 if(!p)return x6_libc_alloc(n);
 if(!n){x6_libc_free(p);return 0;}
 struct header *h=(struct header *)p-1;
 if(h->magic!=MAGIC)x6_runtime_abort(9);
 void *q=x6_libc_alloc(n);if(!q)return 0;
 uint32_t count=h->bytes<n?h->bytes:n;
 for(uint32_t i=0;i<count;++i)((uint8_t *)q)[i]=((const uint8_t *)p)[i];
 x6_libc_free(p);return q;
}

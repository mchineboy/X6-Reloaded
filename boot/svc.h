/* Melis SVC wrappers for X6 test apps (numbers from stock shell/launcher call sites). */
#include <stdint.h>


#define SVC(num, a0, a1, a2, a3) ({                                   \
    register uint32_t r0 __asm__("r0") = (uint32_t)(a0);              \
    register uint32_t r1 __asm__("r1") = (uint32_t)(a1);              \
    register uint32_t r2 __asm__("r2") = (uint32_t)(a2);              \
    register uint32_t r3 __asm__("r3") = (uint32_t)(a3);              \
    __asm__ volatile("svc %4" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
                     : "i"(num) : "memory", "r12", "lr");             \
    r0; })

#define fopen_(p, m)          SVC(0x2602, p, m, 0, 0)
#define fclose_(f)            SVC(0x2603, f, 0, 0, 0)
#define fwrite_(b, s, n, f)   SVC(0x2605, b, s, n, f)
#define fioctrl_(f, c, a, p)  SVC(0x260a, f, c, a, p)
#define palloc_(np)           SVC(0x2500, np, 0, 0, 0)
#define pfree_(a, np)         SVC(0x2501, a, np, 0, 0)
#define sleep_(t)             SVC(0x241c, t, 0, 0, 0)

#define kgrab_(name, cb)      SVC(0x1c02, name, cb, 0, 0)
#define krelease_(h)          SVC(0x1c03, h, 0, 0, 0)

void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);

struct exf_magic {
    char sig[8];
    uint32_t version, type, heap_addr, heap_size;
    int (*main)(void *);
    uint32_t stack;
    uint8_t prio, pad[3];
};
#define EXF_MAGIC(fn) \
    __attribute__((section("MAGIC"), used)) const struct exf_magic exf_magic = \
    { { 'e','P','D','K','.','e','x','f' }, 0x01000000, 0, 0x01f00000, 0x400, fn, 0x4000, 6, { 0, 0, 0 } }

/* UART1 (debug console, PA3) through the kernel's IO mapping at 0xf1c00000 */
static inline void uputc(int c)
{
    volatile uint32_t *u = (volatile uint32_t *)0xf1c25400u;
    uint32_t n = 200000;
    while (!(u[5] & 0x20) && --n) ;
    if (n) u[0] = (uint32_t)c;
}
static inline void uputs(const char *s) { while (*s) uputc(*s++); }
static inline void uhex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) { uint32_t d = (v >> i) & 15; uputc(d < 10 ? '0' + d : 'a' + d - 10); }
}

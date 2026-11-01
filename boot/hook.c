/* X6 card-boot hook. Runs where stock boot0 would jump to boot1 (boot1 already in
 * DRAM at 0x80400000 and checksum-verified by boot0). Reads a payload from the SD
 * card, applies verified word patches to boot1, copies payload items to DRAM,
 * fixes boot1's eGON checksum and jumps to boot1. Any failure: jump to boot1 as-is.
 *
 * Card layout (512-byte sectors):  sector 64 = header, item data follows.
 * header: magic 'X6PL', version 1, n_items, n_patches, header_sum,
 *         items[8]   {first_sector, bytes, dest, sum32}
 *         patches[16]{addr, expect, value}
 */
#include <stdint.h>

struct sd_state { uint32_t fifo_off, step, err_rint, ocr, rca, hc; };
int sd_init(struct sd_state *s);
int sd_read(struct sd_state *s, uint32_t sector, uint32_t count, uint32_t *dst);

#define R32(a)   (*(volatile uint32_t *)(a))
#define BOOT1    0x80400000u
#define HDR_SECT 64u
#define MAGIC    0x4c503658u    /* 'X6PL' */

struct item  { uint32_t sector, bytes, dest, sum; };
struct patch { uint32_t addr, expect, value; };
struct hdr {
    uint32_t magic, version, n_items, n_patches, hdr_sum;
    struct item items[8];
    struct patch patches[16];
};

static void putc_(int c)
{
    uint32_t n = 100000;
    while (!(R32(0x01c25414) & 0x20) && --n) ;
    if (!n) return;
    R32(0x01c25400) = (uint32_t)c;
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void puthex(uint32_t v)
{
    for (int i = 28; i >= 0; i -= 4) { uint32_t d = (v >> i) & 15; putc_(d < 10 ? '0' + d : 'a' + d - 10); }
}

static uint32_t sum32(const uint32_t *p, uint32_t bytes)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < bytes / 4; i++) s += p[i];
    return s;
}

static void __attribute__((noreturn)) go_boot1(void)
{
    ((void (*)(void))BOOT1)();
    for (;;) ;
}

static uint32_t hdrbuf[128];

void __attribute__((noreturn, section(".text.entry"))) hook(void)
{
    struct sd_state s = { 0x200, 0, 0, 0, 0, 0 };
    struct hdr *h = (struct hdr *)hdrbuf;
    int r;

    puts_("\r\n[X6HOOK] start\r\n");
    r = sd_init(&s);
    if (r == 0) r = sd_read(&s, HDR_SECT, 1, hdrbuf);
    if (r) { puts_("[X6HOOK] sd error "); puthex((uint32_t)r); putc_(' '); puthex(s.err_rint); puts_(" -> stock\r\n"); go_boot1(); }

    {
        uint32_t saved = h->hdr_sum;
        h->hdr_sum = 0;
        if (h->magic != MAGIC || h->version != 1 || h->n_items > 8 || h->n_patches > 16 ||
            sum32(hdrbuf, sizeof hdrbuf) != saved) {
            puts_("[X6HOOK] no valid payload -> stock\r\n");
            go_boot1();
        }
    }
    for (uint32_t i = 0; i < h->n_items; i++) {
        struct item *it = &h->items[i];
        uint32_t nsec = (it->bytes + 511) / 512;
        r = sd_read(&s, HDR_SECT + it->sector, nsec, (uint32_t *)it->dest);
        if (r || sum32((uint32_t *)it->dest, it->bytes) != it->sum) {
            puts_("[X6HOOK] item "); puthex(i); puts_(" bad -> stock\r\n");
            go_boot1();
        }
    }
    /* all items good: apply only patches whose current value is the expected one */
    uint32_t applied = 0;
    for (uint32_t i = 0; i < h->n_patches; i++) {
        struct patch *p = &h->patches[i];
        if (R32(p->addr) == p->expect) { R32(p->addr) = p->value; applied++; }
    }
    /* boot1 eGON checksum: word at +12, length at +16, stamp 0x5f0a6c39 */
    {
        uint32_t len = R32(BOOT1 + 16);
        R32(BOOT1 + 12) = 0x5f0a6c39u;
        R32(BOOT1 + 12) = sum32((uint32_t *)BOOT1, len);
    }
    puts_("[X6HOOK] payload ok, patches "); puthex(applied); puts_("/"); puthex(h->n_patches); puts_("\r\n");
    go_boot1();
}

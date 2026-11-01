/* Minimal F1C200s SDC0 PIO reader for the X6 card boot stage (read-only).
 * SDC0 at 0x01c0f000, pins PF0-PF5 function 2, clock from OSC24M.
 * Card commands used: CMD0, CMD8, CMD55/ACMD41, CMD2, CMD3, CMD7, CMD16, CMD17. No writes. */
#include <stdint.h>

#define R32(a)      (*(volatile uint32_t *)(a))
#define CCU         0x01c20000u
#define PIO         0x01c20800u
#define SDC         0x01c0f000u
#define GCTRL       (SDC + 0x00)
#define CLKCR       (SDC + 0x04)
#define TMOUT       (SDC + 0x08)
#define WIDTH       (SDC + 0x0c)
#define BLKSZ       (SDC + 0x10)
#define BYTECNT     (SDC + 0x14)
#define CMDR        (SDC + 0x18)
#define CMDARG      (SDC + 0x1c)
#define RESP0       (SDC + 0x20)
#define RESP1       (SDC + 0x24)
#define RESP2       (SDC + 0x28)
#define RESP3       (SDC + 0x2c)
#define RINT        (SDC + 0x38)
#define STATUS      (SDC + 0x3c)

#define C_START     (1u << 31)
#define C_UPCLK     (1u << 21)
#define C_INIT      (1u << 15)
#define C_WAITPRE   (1u << 13)
#define C_DATA      (1u << 9)
#define C_CRC       (1u << 8)
#define C_LONG      (1u << 7)
#define C_RESP      (1u << 6)
#define RI_ERR      0xbfc2u
#define RI_CMDDONE  (1u << 2)
#define RI_DATAOVER (1u << 3)
#define ST_FIFOEMPTY (1u << 2)
#define ST_BUSY     (1u << 9)

struct sd_state {
    uint32_t fifo_off;     /* in: 0x100 or 0x200 */
    uint32_t step;         /* out: last step reached */
    uint32_t err_rint;     /* out: RINT on failure */
    uint32_t ocr, rca, hc; /* out */
};

static void delay(volatile uint32_t n) { while (n--) ; }

static int wait_clear(uint32_t reg, uint32_t mask, uint32_t loops)
{
    while (loops--) if (!(R32(reg) & mask)) return 0;
    return -1;
}

static int update_clock(void)
{
    R32(CMDR) = C_START | C_UPCLK | C_WAITPRE;
    return wait_clear(CMDR, C_START, 2000000);
}

static int cmd(struct sd_state *s, uint32_t idx, uint32_t arg, uint32_t flags)
{
    uint32_t loops = 2000000, ri;
    R32(RINT) = 0xffffffff;
    R32(CMDARG) = arg;
    R32(CMDR) = C_START | C_WAITPRE | flags | idx;
    do {
        ri = R32(RINT);
        if (ri & RI_ERR) { s->err_rint = ri; return -1; }
    } while (!(ri & RI_CMDDONE) && --loops);
    if (!loops) { s->err_rint = ri | 0x80000000u; return -2; }
    return 0;
}

static void set_clock(uint32_t ccu_val)
{
    R32(CLKCR) &= ~(1u << 16);           /* card clock off */
    update_clock();
    R32(CCU + 0x88) = ccu_val;
    R32(CLKCR) = (R32(CLKCR) & ~0xffu);   /* divider bypass */
    update_clock();
    R32(CLKCR) |= (1u << 16);            /* card clock on */
    update_clock();
}

int sd_init(struct sd_state *s)
{
    uint32_t i;
    s->step = 1;
    /* bus gate + reset for SDC0 (bit 8) */
    R32(CCU + 0x60)  |= (1u << 8);
    R32(CCU + 0x2c0) |= (1u << 8);
    /* PF0-PF5 -> function 2 (SDC0), pull-up on all but CLK (PF2) */
    R32(PIO + 5 * 0x24 + 0x00) = (R32(PIO + 5 * 0x24 + 0x00) & 0xff000000u) | 0x00222222u;
    R32(PIO + 5 * 0x24 + 0x1c) = (R32(PIO + 5 * 0x24 + 0x1c) & ~0xfffu) | 0x545u;
    /* module clock on (OSC24M / 8 / 8) before touching the controller */
    R32(CCU + 0x88) = 0x80030007u;
    delay(10000);
    /* controller soft reset */
    R32(GCTRL) = 0x7;
    if (wait_clear(GCTRL, 0x7, 1000000)) return -1;
    R32(TMOUT) = 0xffffff40u;
    R32(WIDTH) = 0;                       /* 1-bit bus */
    s->step = 2;
    set_clock(0x80030007u);               /* OSC24M / 8 / 8 = 375 kHz */
    delay(100000);
    s->step = 3;
    if (cmd(s, 0, 0, C_INIT)) return -3;                         /* GO_IDLE */
    s->step = 4;
    s->hc = 0;
    if (cmd(s, 8, 0x1aa, C_RESP | C_CRC) == 0 && (R32(RESP0) & 0xfff) == 0x1aa)
        s->hc = 1;                                               /* v2 card */
    s->step = 5;
    for (i = 0; i < 2000; i++) {
        if (cmd(s, 55, 0, C_RESP | C_CRC)) return -5;
        if (cmd(s, 41, s->hc ? 0x40ff8000u : 0x00ff8000u, C_RESP)) return -6;
        s->ocr = R32(RESP0);
        if (s->ocr & 0x80000000u) break;
        delay(20000);
    }
    if (!(s->ocr & 0x80000000u)) return -7;
    s->hc = (s->ocr >> 30) & 1;
    s->step = 6;
    if (cmd(s, 2, 0, C_RESP | C_LONG | C_CRC)) return -8;        /* ALL_SEND_CID */
    if (cmd(s, 3, 0, C_RESP | C_CRC)) return -9;                 /* SEND_RELATIVE_ADDR */
    s->rca = R32(RESP0) >> 16;
    s->step = 7;
    set_clock(0x80000001u);               /* OSC24M / 2 = 12 MHz */
    if (cmd(s, 7, s->rca << 16, C_RESP | C_CRC)) return -10;     /* SELECT_CARD */
    if (wait_clear(STATUS, ST_BUSY, 2000000)) return -11;
    if (!s->hc && cmd(s, 16, 512, C_RESP | C_CRC)) return -12;   /* SET_BLOCKLEN */
    s->step = 8;
    return 0;
}

int sd_read(struct sd_state *s, uint32_t sector, uint32_t count, uint32_t *dst)
{
    uint32_t fifo = SDC + s->fifo_off;
    while (count--) {
        uint32_t n = 128, loops, ri;
        R32(GCTRL) |= (1u << 31);         /* FIFO accessed by CPU */
        R32(BLKSZ) = 512;
        R32(BYTECNT) = 512;
        R32(RINT) = 0xffffffff;
        R32(CMDARG) = s->hc ? sector : sector * 512;
        R32(CMDR) = C_START | C_WAITPRE | C_DATA | C_RESP | C_CRC | 17;
        while (n) {
            loops = 4000000;
            while ((R32(STATUS) & ST_FIFOEMPTY) && --loops) {
                ri = R32(RINT);
                if (ri & RI_ERR) { s->err_rint = ri; return -20; }
            }
            if (!loops) { s->err_rint = R32(RINT) | 0x80000000u; return -21; }
            *dst++ = R32(fifo);
            n--;
        }
        loops = 4000000;
        do {
            ri = R32(RINT);
            if (ri & RI_ERR) { s->err_rint = ri; return -22; }
        } while ((ri & (RI_CMDDONE | RI_DATAOVER)) != (RI_CMDDONE | RI_DATAOVER) && --loops);
        if (!loops) { s->err_rint = ri | 0x80000000u; return -23; }
        sector++;
    }
    return 0;
}

/* H5: standalone host for the arcade module.
 * Loads f:\apps\mod\fba.mod, opens the X6-ARCADE menu, runs frames, shows the
 * module's 320x240 ARGB buffer on a double-buffered display layer, plays its
 * 22050 Hz stereo audio through b:\AUDIO\PLAY, feeds LKeyBoardDev keys as
 * the module's button mask. Hold Menu (0x1c3) ~1 s to quit back to boot.esh. */
#include "svc.h"
#include "x6_ui.h"

#define mod_install_(p, m)   SVC(0x2805, p, m, 0, 0)
#define mod_open_(id, m)     SVC(0x2800, id, m, 0, 0)
#define mod_close_(h)        SVC(0x2801, h, 0, 0, 0)
#define mod_uninstall_(id)   SVC(0x2806, id, 0, 0, 0)

#define SW 480
#define SH 320
#define GW 320
#define GH 240

struct rect { int32_t x, y; uint32_t w, h; };
struct fb { uint32_t addr[3]; uint32_t w, h; uint8_t format, seq, mode, br_swap; uint32_t cs_mode; };
struct layer_info { uint8_t mode, pipe, prio, alpha_en; uint16_t alpha_val; uint8_t ck_en, pad; struct rect src, scn; struct fb fb; };
struct ev { uint32_t type, code; int32_t value; };
struct pkg { uint32_t hdr[3]; uint32_t count; struct ev ev[64]; };

struct host_cb { int32_t (*submit_audio)(const void *, uint32_t); uint32_t *(*get_output_buffer)(void); char resource_path[256]; };
struct emu {
    int32_t (*load)(const struct host_cb *, const char *);
    int32_t (*unload)(void);
    int32_t (*frame)(uint32_t);
    int32_t (*u0c)(void);
    int32_t (*set_buttons)(uint32_t, uint32_t);
    int32_t (*u14)(void);
    int32_t (*save_state)(const char *);
    int32_t (*load_state)(const char *);
    uint32_t *(*video_info)(uint32_t *, uint32_t *, uint32_t *);
    int32_t (*progress)(void);
};

static volatile uint32_t mask, menu_down;
static uint32_t gbuf, front;                 /* gbuf: module draws here; front: shown by the layer */
static uint32_t afp, aplugged;                         /* b:\AUDIO\PLAY */
static uint8_t astage[40960];
static uint32_t acount, adrop;
static volatile uint32_t volreq, quitreq, menupresses;
/* idle handling: screen blank and auto power-off, configured by the module's settings */
static uint32_t g_disp, blank_ticks, off_ticks, blanked, brightness = 12;
static volatile uint32_t last_input, wake_req;
#define ticks_() SVC(0x241f, 0, 0, 0, 0)                  /* 10 ms system ticks (as the module's timer.S) */
static volatile uint32_t acalls, awritten, alastw, alastq, lastkey, vol = 25;
#define AFRAME 1468u                         /* (22050/60)*4 bytes, as the stock launcher */
static struct host_cb cb;

static void say(const char *k, uint32_t v) { uputs("[X6MENU] "); uputs(k); uputs("="); uhex(v); uputs("\r\n"); }

/* LKeyBoardDev reports the whole set of held matrix buttons as ONE code (bits OR'd together)
 * and re-sends it on every change: e.g. Right 0x04 v0, then Right+A 0x14 v1 (measured 2026-11-01).
 * Like the stock launcher (app_root 0x620ac), press ORs the code into the mask and release clears it.
 * Select/Start/Menu/Volume arrive as their own codes. Value 2 is auto-repeat (still held). */
static uint32_t special_bit(uint32_t c)
{
    if (c == 0x2a1) return 0x400;                       /* Select */
    if (c == 0x1f6) return 0x800;                       /* Start */
    if (c == 0x1c3) return 0x1000;                      /* Menu -> module pause menu (host-defined bit) */
    return 0;
}

static int key_cb(void *p)
{
    struct pkg *k = p;
    if (!k || k->count > 64) return -1;
    for (uint32_t i = 0; i < k->count; i++) {
        if (k->ev[i].type != 1) continue;
        uint32_t c = k->ev[i].code, b;
        last_input = ticks_();
        if (blanked && k->ev[i].value) { wake_req = 1; continue; }   /* first press only wakes the screen */
        b = special_bit(c);
        if (!b && c <= 0x3ff && c != 0x1d4 && c != 0x1b2) b = c;   /* matrix combination code */
        if (k->ev[i].value) lastkey = c;
        /* measured 2026-11-01: Volume Up 0x1d4, Volume Down 0x1b2 */
        if (c == 0x1c3) { menu_down = k->ev[i].value != 0; if (k->ev[i].value) menupresses++; }
        if (k->ev[i].value && (c == 0x1d4) && vol < 30) { vol++; volreq = 1; }
        if (k->ev[i].value && (c == 0x1b2) && vol > 0) { vol--; volreq = 1; }
        if (b) mask = k->ev[i].value ? (mask | b) : (mask & ~b);
    }
    return 0;
}

static int32_t audio_cb(const void *pcm, uint32_t bytes)
{
    uint32_t q;
    if (!afp) return (int32_t)bytes;
    if (acount + bytes > sizeof astage) { acount = 0; adrop++; }   /* overflow: drop staged audio */
    if (bytes <= sizeof astage) { memcpy(astage + acount, pcm, bytes); acount += bytes; }
    acalls++;
    q = fioctrl_(afp, 43, 1, 0);                                    /* bytes queued in the driver */
    alastq = q;
    /* No sleep here: the module paces itself (x6_pacer); the stock launcher's sleep would add a
       second wait. Data stays staged while the driver holds >= 5 frames. */
    if (q < 5 * AFRAME && acount) { alastw = fwrite_(astage, 1, acount, afp); awritten += acount; acount = 0; }
    return (int32_t)bytes;
}


/* Power-manager mode bracket copied from the stock launcher (app_root 0x119a4 / 0x11b80). */
static void pwm_mode(uint32_t want)
{
    uint32_t f = SVC(0x2203, 2, 0, 0, 0);                  /* current clock */
    uint32_t rel = SVC(0x1901, f > 600000000u ? 1 : 3, 0, 0, 0);
    sleep_(1);
    uint32_t req = SVC(0x1900, want, 0, 0, 0);
    say("pwm_clock", f); say("pwm_release", rel); say("pwm_request", req);
}

static void audio_open(void)
{
    struct { uint8_t ch, bits, pad[2]; uint32_t rate; } para = { 2, 16, { 0, 0 }, 22050 };
    uint32_t r;
    afp = fopen_("b:\\AUDIO\\PLAY", "r+");
    if (!afp) {                                                   /* driver not plugged in yet */
        r = SVC(0x1f00, "d:\\drv\\audio.drv", 0, 0, 1);       /* esDEV_Plugin, as init.axf */
        say("audio_plugin_d", r);
        aplugged = 1;
        afp = fopen_("b:\\AUDIO\\PLAY", "r+");
    }
    if (!afp) {
        r = SVC(0x1f00, "\\drv\\audio.drv", 0, 0, 1);
        say("audio_plugin_rel", r);
        aplugged = 2;
        afp = fopen_("b:\\AUDIO\\PLAY", "r+");
    }
    say("audio_fp", afp);
    if (!afp) return;
    /* No CTRL ioctls: 45/1 (+10/0x141) is init.axf's teardown and closes the playback codec
       (audio.drv 0xe0602204 -> 0xe0600cc4). The stock launcher never calls them. */
    say("audio_set", fioctrl_(afp, 23, 0, &para));
    say("audio_mode", fioctrl_(afp, 40, 3, 0));
    say("audio_vol", fioctrl_(afp, 25, 25, 0));                    /* desktop dsk_volume: ioctl 25, 0..30 */
    say("audio_start", fioctrl_(afp, 1, 0, 0));
    say("audio_flush", fioctrl_(afp, 42, 0, 0));
}

static void audio_close(void)
{
    if (!afp) return;
    fioctrl_(afp, 2, 0, 0);
    fclose_(afp);
    afp = 0;
    if (aplugged == 1) say("audio_plugout", SVC(0x1f01, "d:\\drv\\audio.drv", 0, 0, 0));
    if (aplugged == 2) say("audio_plugout", SVC(0x1f01, "\\drv\\audio.drv", 0, 0, 0));
}


/* 3x5 hex digits, bit 2 = left column */
static const uint8_t hexfont[16][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},
    {7,5,7,5,7},{7,5,7,1,7},{7,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,7,4,7},{7,4,7,4,4}};
static void draw_hex(uint32_t *fb, uint32_t x, uint32_t y, uint32_t v)
{
    for (int d = 0; d < 8; d++) {
        const uint8_t *g = hexfont[(v >> (28 - 4 * d)) & 15];
        for (uint32_t r = 0; r < 12; r++)
            for (uint32_t c = 0; c < 8; c++) {
                uint32_t on = (r < 10 && c < 6) ? (g[r / 2] >> (2 - c / 2)) & 1 : 0;
                fb[(y + r) * GW + x + d * 8 + c] = on ? 0xffffff00 : 0xff000000;
            }
    }
}
static void draw_debug(uint32_t *fb)
{
    draw_hex(fb, 0, 0, acalls);
    draw_hex(fb, 0, 12, awritten);
    draw_hex(fb, 0, 24, alastw);
    draw_hex(fb, 0, 36, alastq);
    draw_hex(fb, 0, 48, lastkey);
    draw_hex(fb, 0, 60, vol);
    draw_hex(fb, 0, 72, menupresses);
}

static void copy_frame(uint32_t *d, const uint32_t *s, uint32_t words)
{
    __asm__ volatile(
        "1: ldmia %1!, {r3-r10}\n"
        "   stmia %0!, {r3-r10}\n"
        "   subs %2, %2, #8\n"
        "   bne 1b\n"
        : "+r"(d), "+r"(s), "+r"(words) : : "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "memory", "cc");
}
static uint32_t *getbuf_cb(void) { return (uint32_t *)gbuf; }   /* back buffer */



/* Services offered to our modified fba.mod through resource_path ("X6HOST1" + pointer). */
struct x6_host_services {
    uint32_t version;
    void (*set_brightness)(uint32_t level);
    void (*set_idle)(uint32_t blank_seconds, uint32_t off_seconds);
};
static void lcd_brightness(uint32_t level)
{
    /* Stock LCD levels: 0 brightest .. 10 dimmest still visible (desktop.mod 0xe1f09e20 maps its
       16 settings onto 10..0; levels above 10 go black on this panel). Slider 0..15 -> 10..0. */
    uint32_t lv = level > 15 ? 15 : level;
    uint32_t a[3] = { 10u - lv * 10u / 15u, 0, 0 };
    if (g_disp) fioctrl_(g_disp, 0x142, 0, a);               /* desktop.mod 0xe1f09ec8: LCD set brightness */
}
static void svc_set_brightness(uint32_t level) { brightness = level; if (!blanked) lcd_brightness(level); }
static void svc_set_idle(uint32_t blank_s, uint32_t off_s) { blank_ticks = blank_s * 100u; off_ticks = off_s * 100u; last_input = ticks_(); }
static const struct x6_host_services host_services = { 1, svc_set_brightness, svc_set_idle };

int main(void *arg)
{
    uint32_t disp, h, kh, a[3], mid, mh, r;
    uint32_t np = 1 + ((GW * GH * 4) >> 10);
    struct layer_info li;
    const struct emu *e;
    (void)arg;
    uputs("\r\n[X6MENU] start\r\n");

    disp = fopen_("b:\\DISP\\DISPLAY", "r+");
    g_disp = disp;
    last_input = ticks_();
    gbuf = palloc_(np);
    front = palloc_(np);
    say("disp", disp); say("gbuf", gbuf); say("front", front);
    if (!disp || !gbuf || !front) return 1;
    memset((void *)gbuf, 0, GW * GH * 4);
    memset((void *)front, 0, GW * GH * 4);
    SVC(0x2515, front, GW * GH * 4, 0, 0);

    a[0] = a[1] = a[2] = 0;
    h = fioctrl_(disp, 0x40, 0, a);
    say("layer", h);
    memset(&li, 0, sizeof li);
    li.mode = 4;                                         /* scaler: 320x240 -> 4:3 area of the panel */
    li.alpha_en = 1; li.alpha_val = 0xff;
    li.src = (struct rect){ 0, 0, GW, GH };
    li.scn = (struct rect){ (SW - 427) / 2, 0, 427, SH };
    li.fb.addr[0] = front; li.fb.w = GW; li.fb.h = GH; li.fb.format = 10; li.fb.mode = 1;
    a[0] = 0; fioctrl_(disp, 0x10, 0, a);
    a[0] = h; a[1] = (uint32_t)&li; a[2] = 0; r = fioctrl_(disp, 0x4a, 0, a); say("set_para_scaler", r);
    if (r) {                                             /* no scaler: centre 1:1 */
        li.mode = 0; li.scn = (struct rect){ (SW - GW) / 2, (SH - GH) / 2, GW, GH };
        a[0] = h; a[1] = (uint32_t)&li; r = fioctrl_(disp, 0x4a, 0, a); say("set_para_plain", r);
    }
    a[0] = h; a[1] = (uint32_t)&li.src; fioctrl_(disp, 0x46, 0, a);
    a[0] = 0; fioctrl_(disp, 0x11, 0, a);
    a[0] = h; a[1] = 0; fioctrl_(disp, 0x56, 0, a); r = fioctrl_(disp, 0x42, 0, a); say("layer_open", r);

    /* Show the original boot marquee before loading the emulator. The final
       frame stays visible while module loading finishes. One boot only. */
    {
        struct x6_surface splash = {(uint32_t *)gbuf, GW, GH, GW};
        for (uint32_t t = 0; t <= 120; t += 5) {
            x6_ui_draw_splash(&splash, t);
            copy_frame((uint32_t *)front, (const uint32_t *)gbuf, GW * GH);
            SVC(0x2515, front, GW * GH * 4, 0, 0);
            sleep_(5);
        }
    }

    pwm_mode(1);
    audio_open();
    kh = kgrab_("LKeyBoardDev", key_cb);
    say("keys", kh);

    mid = mod_install_("f:\\apps\\mod\\fba.mod", 0);
    say("mod_id", mid);
    mh = mid ? mod_open_(mid & 0xff, 0) : 0;
    say("mod_handle", mh);
    if (!mh) goto teardown;
    e = *(const struct emu **)(mh + 4);
    say("emu", (uint32_t)e);
    memset(&cb, 0, sizeof cb);
    cb.submit_audio = audio_cb; cb.get_output_buffer = getbuf_cb;
    {
        const struct x6_host_services *hp = &host_services;
        static const char magic[8] = { 'X', '6', 'H', 'O', 'S', 'T', '1', 0 };
        memcpy(cb.resource_path, magic, 8);
        memcpy(cb.resource_path + 8, &hp, sizeof hp);
    }
    r = e->load(&cb, "f:\\game\\cps\\X6-ARCADE.zip");
    say("load", r);
    if (r == 0) {
        uint32_t held = 0, frames = 0, skipleft = 0, lastcalls = 0;
        for (;;) {
            e->set_buttons(0, mask);
            if (volreq && afp) { volreq = 0; fioctrl_(afp, 25, vol, 0); }
            {
                uint32_t now = ticks_(), idle = now - last_input, b3[3] = { 0, 0, 0 };
                if (wake_req) {
                    wake_req = 0;
                    if (blanked) { fioctrl_(disp, 0x140, 0, b3); blanked = 0; lcd_brightness(brightness); say("wake", idle); }
                }
                if (!blanked && blank_ticks && idle > blank_ticks) { fioctrl_(disp, 0x141, 0, b3); blanked = 1; say("blank", idle); }
                if (off_ticks && idle > off_ticks) { say("idle_poweroff", idle); quitreq = 2; break; }
                {   /* status every ~30 s: idle ticks, blank limit, power-off limit */
                    static uint32_t next_status;
                    if ((int32_t)(now - next_status) >= 0) {
                        next_status = now + 3000u;
                        uputs("[X6MENU] idle="); uhex(idle); uputs(" blank="); uhex(blank_ticks);
                        uputs(" off="); uhex(off_ticks); uputs(" now="); uhex(now); uputs("\r\n");
                    }
                }
            }
            uint32_t render = 1;
            if (afp && acalls != lastcalls) {           /* game audio flowing (menu makes none): stock frame skip, 4 at <1 frame queued .. 0 at >=4 */
                uint32_t buffered = fioctrl_(afp, 43, 1, 0) / AFRAME;
                uint32_t skip = buffered >= 4 ? 0 : 4 - buffered;
                if (skipleft) { skipleft--; render = 0; } else skipleft = skip;
            }
            lastcalls = acalls;
            r = e->frame(render);
            if ((int32_t)r < 0) { say("frame_err", r); break; }
            if (render) copy_frame((uint32_t *)front, (const uint32_t *)gbuf, GW * GH);   /* finished frame -> shown buffer */
#ifdef X6_DEBUG_OVERLAY
            draw_debug((uint32_t *)front);
#endif
            if (render) SVC(0x2515, front, GW * GH * 4, 0, 0);   /* clean D-cache over it (as display.drv does) */
            if (++frames == 60) { say("frames", frames); say("audio_queued", afp ? fioctrl_(afp, 43, 1, 0) : 0); say("audio_drops", adrop); }
            held = menu_down ? held + 1 : 0;
            (void)held;                                 /* Menu opens the module's pause menu; power key powers off */
        }
        e->unload();
        say("audio_drops", adrop);
    }
    mod_close_(mh);
    mod_uninstall_(mid);
teardown:
    if (kh) krelease_(kh);
    a[0] = h; a[1] = 0; fioctrl_(disp, 0x43, 0, a); fioctrl_(disp, 0x41, 0, a);
    audio_close();
    pwm_mode(3);
    pfree_(gbuf, np);
    pfree_(front, np);
    fclose_(disp);
    uputs("[X6MENU] end\r\n");
    if (quitreq == 2) {
        /* stock power off: power.drv ioctl 0xff04 on b:\HID\POWER (power.drv 0xe130016c);
           init.axf normally plugs power.drv, so plug it here first */
        uint32_t pfp = fopen_("b:\\HID\\POWER", "r+");
        if (!pfp) { say("power_plugin", SVC(0x1f00, "\\drv\\power.drv", 0, 0, 1)); pfp = fopen_("b:\\HID\\POWER", "r+"); }
        say("power_fp", pfp);
        if (pfp) { say("power_off", fioctrl_(pfp, 0xff04, 0, 0)); sleep_(200); }
        /* matrixkey.drv switch_io_pulldown (0xe0a00768): script [soft_switch] switch_pin -> output 0 */
        uint32_t cfg[14], ph;
        memset(cfg, 0, sizeof cfg);
        say("pin_cfg", SVC(0x1d00, "soft_switch", "switch_pin", cfg, 14));
        ph = SVC(0x2100, cfg, 1, 0, 0);
        say("pin_req", ph);
        if (ph) { SVC(0x2105, ph, 1, 0, 0); sleep_(10); SVC(0x2109, ph, 0, 0, 0); }
        sleep_(200);
    }
    return 0;
}
EXF_MAGIC(main);

void *memcpy(void *d, const void *s, unsigned n) { uint8_t *x = d; const uint8_t *y = s; while (n--) *x++ = *y++; return d; }
void *memset(void *d, int c, unsigned n) { uint8_t *x = d; while (n--) *x++ = (uint8_t)c; return d; }

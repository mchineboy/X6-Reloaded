/* Standalone MAME host with an opt-in, original-FBA compatibility trial.
 * All fixed-address emulator switching takes place outside module callbacks. */
#include "svc.h"
#include "x6_ui.h"
#include "x6_host_services.h"
#include "session.h"
#include "x6_timing.h"
#include "x6_directory.h"
#include "x6_stock_catalog.h"
#include "x6_catalog.h"
#include "x6_runtime.h"
#include "x6_library.h"
#include "x6_titles.h"

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
static struct x6_host_callbacks cb;
static struct x6_session session;
static uint32_t stock_active, stock_ready, stock_requested, stockbuf, blocked_buttons;
static uint32_t restore_library;
static struct x6_game original_game;
static uint32_t original_count, original_selected, legacy_selected;
static uint32_t original_library = X6_LIBRARY_STOCK, library_status;
static uint32_t stock_w, stock_h, stock_pitch, stock_video_error;
#define STOCK_PAGES ((X6_STOCK_MAX_SIDE * X6_STOCK_MAX_SIDE * 4u) / 1024u)

void *x6_directory_open_raw(const char *path) {return (void *)SVC(0x260d, path, 0, 0, 0);}
const uint8_t *x6_directory_read_raw(void *handle) {return (const uint8_t *)SVC(0x260f, handle, 0, 0, 0);}
int32_t x6_directory_close_raw(void *handle) {return (int32_t)SVC(0x260e, handle, 0, 0, 0);}
void *x6_fs_open(const char *path, const char *mode) {return (void *)fopen_(path, mode);}
uint32_t x6_fs_read(void *data, uint32_t size, uint32_t count, void *handle) {return SVC(0x2604, data, size, count, handle);}
uint32_t x6_fs_write(const void *data, uint32_t size, uint32_t count, void *handle) {return SVC(0x2605, data, size, count, handle);}
int32_t x6_fs_seek(void *handle, int32_t offset, int32_t origin) {return (int32_t)SVC(0x2606, handle, offset, origin, 0);}
int32_t x6_fs_close(void *handle) {return (int32_t)fclose_(handle);}

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
    /* MAME paces itself. Original FBA relies on the host's queue backpressure,
       so retain the stock launcher's one-tick wait only for that engine. */
    if (q < 5 * AFRAME && acount) { alastw = fwrite_(astage, 1, acount, afp); awritten += acount; acount = 0; }
    else if (stock_active && q >= 5 * AFRAME) sleep_(1);
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
static uint32_t *getbuf_cb(void)
{
    if (!stock_active) return (uint32_t *)gbuf;
    stock_w = stock_h = stock_pitch = 0;
    if (session.emu) session.emu->video_info(&stock_w, &stock_h, &stock_pitch);
    if (!stockbuf || !x6_stock_geometry(stock_w, stock_h, stock_pitch)) {
        stock_video_error = 1;return 0;
    }
    return (uint32_t *)stockbuf;
}



/* Services offered to our modified fba.mod through resource_path ("X6HOST1" + pointer). */
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
static uint32_t svc_stock_available(void) { return stock_ready; }
static int32_t svc_request_library_game(uint32_t library, uint32_t index)
{
    struct x6_game g;
    if (!library || library >= X6_LIBRARY_COUNT || stock_active || stock_requested
        || x6_libraries_game(library, index, &g) != 0 || g.available != X6_GAME_READY) return -1;
    original_game = g;original_library = library;original_selected = index;stock_requested = 1;return 0;
}
static int32_t svc_request_stock(void)
{
    return stock_ready ? svc_request_library_game(X6_LIBRARY_STOCK, legacy_selected) : -1;
}
static uint32_t svc_restore_library(void) { return restore_library; }
static uint32_t svc_stock_count(void) {return original_count;}
static int32_t svc_stock_metadata(uint32_t index, struct x6_game *out)
{
    return x6_libraries_game(X6_LIBRARY_STOCK, index, out);
}
static int32_t svc_request_stock_game(uint32_t index)
{
    return svc_request_library_game(X6_LIBRARY_STOCK, index);
}
static uint32_t svc_restore_stock_game(void) {return original_selected;}
static uint32_t svc_library_status(void) {return library_status;}
static const struct x6_host_services host_services = {
    6, svc_set_brightness, svc_set_idle, svc_stock_available, svc_request_stock, svc_restore_library,
    svc_stock_count, svc_stock_metadata, svc_request_stock_game, svc_restore_stock_game, x6_directory_walk,
    &x6_libraries_provider, svc_request_library_game, svc_library_status, x6_titles_apply
};

static int32_t install_module(const char *path) { return (int32_t)mod_install_(path, 0); }
static uintptr_t open_module(uint32_t id) { return mod_open_(id, 0); }
static const struct x6_emu_interface *module_interface(uintptr_t handle)
{
    return *(const struct x6_emu_interface **)(handle + 4);
}
static int32_t close_module(uintptr_t handle) { return (int32_t)mod_close_(handle); }
static int32_t uninstall_module(uint32_t id) { return (int32_t)mod_uninstall_(id); }
static const struct x6_module_ops module_ops = {
    install_module, open_module, module_interface, close_module, uninstall_module
};

/* Return -2 if module-manager ownership could not be released. In that case
 * no replacement may be installed at the shared 0xe9100000 address. */
static int32_t start_emulator(uint32_t stock)
{
    if (x6_session_stop(&session) != 0) return -2;
    if (stockbuf) {pfree_(stockbuf, STOCK_PAGES);stockbuf = 0;}
    stock_active = stock;stock_requested = 0;stock_video_error = 0;
    if (stock) restore_library = original_library + 1; /* Reopen the actual stock library. */
    blocked_buttons = mask; /* Ignore every currently held button until release. */
    acount = 0;acalls = 0;
    if (afp) fioctrl_(afp, 42, 0, 0); /* Discard the previous engine's queued music. */
    memset((void *)gbuf, 0, GW * GH * 4);
    memset(&cb, 0, sizeof cb);
    cb.submit_audio = audio_cb;cb.get_output_buffer = getbuf_cb;
    if (stock) {
        if (original_game.available != X6_GAME_READY || !original_library || original_library >= X6_LIBRARY_COUNT) return -1;
        stockbuf = palloc_(STOCK_PAGES);
        if (!stockbuf) return -1;
        memset((void *)stockbuf, 0, STOCK_PAGES * 1024u);
        const char *path = original_game.path;
        uint32_t parent = 0;
        for (uint32_t i = 0; path[i] && i + 1 < sizeof cb.resource_path; ++i) {
            cb.resource_path[i] = path[i];
            if (path[i] == '\\' || path[i] == '/') parent = i;
        }
        cb.resource_path[parent] = 0;
    } else {
        const struct x6_host_services *hp = &host_services;
        static const char magic[8] = {'X','6','H','O','S','T','1',0};
        memcpy(cb.resource_path, magic, 8);
        memcpy(cb.resource_path + 8, &hp, sizeof hp);
    }
    int32_t result = x6_session_start(&session, &module_ops,
        stock ? x6_library_module(original_library) : "f:\\x6\\arcade.mod", &cb,
        stock ? original_game.path : "f:\\game\\cps\\X6-ARCADE.zip");
    say(stock ? "stock_load" : "mame_load", (uint32_t)result);
    if (result != 0) return session.id ? -2 : -1;
    if (stock && !getbuf_cb()) return -1;
    if (stock && afp) {
        static const int16_t silence[2205 * 2] = {0};
        audio_cb(silence, sizeof silence); /* Start with a bounded 100 ms reserve. */
    }
    last_input = ticks_();
    return 0;
}

static void present(void)
{
    copy_frame((uint32_t *)front, (const uint32_t *)gbuf, GW * GH);
    SVC(0x2515, front, GW * GH * 4, 0, 0);
}

static void catalog_progress(uint32_t library, uint32_t count)
{
    struct x6_surface surface = {(uint32_t *)gbuf, GW, GH, GW};
    x6_ui_draw_splash(&surface, 60);
    x6_gfx_fill(&surface, 0, 192, GW, 48, 0xff05060du);
    x6_text_draw(&surface, 16, 198, x6_library_name(library), 1, 0xffffffffu);
    char line[32] = {0}, reverse[12];uint32_t n = 0, at = 0;
    do {reverse[n++] = (char)('0' + count % 10);count /= 10;} while (count);
    while (n) line[at++] = reverse[--n];
    const char *suffix = " files found";while (*suffix && at + 1 < sizeof line) line[at++] = *suffix++;
    x6_text_draw(&surface, 16, 214, line, 1, 0xffa0a0a0u);present();
}

int main(void *arg)
{
    uint32_t disp, h, kh, a[3], r;
    uint32_t np = 1 + ((GW * GH * 4) >> 10);
    struct layer_info li;
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

    x6_libraries_progress(catalog_progress);
    library_status = x6_libraries_build() != 0;
    uint32_t buckets[X6_LIBRARY_BUCKETS];
    original_count = x6_libraries_provider.stats(X6_LIBRARY_STOCK, buckets);
    legacy_selected = original_count;
    for (uint32_t i = 0; i < original_count; ++i) {
        struct x6_game g;
        if (x6_libraries_game(X6_LIBRARY_STOCK, i, &g) == 0 && g.id[0] == '2' && g.id[1] == '0'
            && g.id[2] == '2' && g.id[3] == '0' && g.id[4] == 'b' && g.id[5] == 'b' && !g.id[6]
            && g.available == X6_GAME_READY) {legacy_selected = i;break;}
    }
    stock_ready = legacy_selected < original_count;
    say("original_games", original_count);
    int32_t result = start_emulator(0);
    uint32_t paused = result != 0, failed = paused, fatal = paused;
    uint32_t frames = 0, skipleft = 0, lastcalls = 0, previous = 0;
    uint32_t pause_selected = 0, pause_dim = 1;
    struct x6_pacer stock_pacer;
    x6_pacer_reset(&stock_pacer);
    for (;;) {
        blocked_buttons &= mask;
        uint32_t buttons = mask & ~blocked_buttons;
        uint32_t pressed = buttons & ~previous;
        previous = buttons;
        if (volreq && afp) {volreq = 0;fioctrl_(afp, 25, vol, 0);}
        {
            uint32_t now = ticks_(), idle = now - last_input, b3[3] = {0, 0, 0};
            if (wake_req) {
                wake_req = 0;
                if (blanked) {fioctrl_(disp, 0x140, 0, b3);blanked = 0;lcd_brightness(brightness);}
            }
            if (!blanked && blank_ticks && idle > blank_ticks) {fioctrl_(disp, 0x141, 0, b3);blanked = 1;}
            if (off_ticks && idle > off_ticks) {quitreq = 2;break;}
        }
        if (stock_active && !paused && (pressed & X6_BTN_MENU)) {
            paused = 1;pause_selected = 0;pause_dim = 1;
            acount = 0;
            if (afp) fioctrl_(afp, 42, 0, 0);
            pressed &= ~X6_BTN_MENU; /* Opening Menu must not immediately resume. */
        }
        if (paused) {
            const char *items[2] = {"Resume", "Back to Reloaded"};
            const char *note = "Menu or B resumes";
            uint32_t count = 2;
            if (failed) {
                items[0] = fatal ? "Restart required" : "Back to Reloaded";
                count = 1;
                note = fatal ? "Power off and restart the X6" : "Original game could not be started";
            }
            if (pressed & (X6_BTN_UP | X6_BTN_DOWN)) pause_selected = (pause_selected + 1) % count;
            uint32_t resume = !failed && (pressed & (X6_BTN_B | X6_BTN_MENU));
            uint32_t back = !fatal && (pressed & X6_BTN_A) && (failed || pause_selected == 1);
            if (!failed && (pressed & X6_BTN_A) && pause_selected == 0) resume = 1;
            if (back) {
                result = start_emulator(0);
                failed = paused = result != 0;fatal = failed;
                pause_selected = 0;pause_dim = 1;previous = 0;
                skipleft = 0;lastcalls = 0;x6_pacer_reset(&stock_pacer);
                continue;
            }
            if (resume) {
                paused = 0;blocked_buttons = mask;previous = 0;
                x6_pacer_reset(&stock_pacer);
                continue;
            }
            struct x6_surface surface = {(uint32_t *)gbuf, GW, GH, GW};
            x6_ui_draw_pause(&surface, failed ? x6_library_name(original_library) : original_game.title,
                             items, count, pause_selected, note, pause_dim);
            pause_dim = 0;present();sleep_(2);
            continue;
        }
        const struct x6_emu_interface *e = session.emu;
        e->set_buttons(0, stock_active ? buttons & 0xfffu : buttons);
        if (stock_active && (!afp || acalls == lastcalls)) {
            uint32_t now = ticks_(), wait = x6_pacer_wait(&stock_pacer, now);
            if (wait) sleep_(wait);
            x6_pacer_observe(&stock_pacer, ticks_());
        }
        uint32_t render = 1;
        if (afp && acalls != lastcalls) {
            uint32_t buffered = fioctrl_(afp, 43, 1, 0) / AFRAME;
            uint32_t skip = buffered >= 4 ? 0 : 4 - buffered;
            if (skipleft) {skipleft--;render = 0;} else skipleft = skip;
        }
        lastcalls = acalls;
        int32_t frame_result = e->frame(render);
        if (stock_requested) {
            /* We are back in host code. No module callback is on the stack. */
            result = start_emulator(1);
            paused = failed = result != 0;fatal = result == -2;
            pause_selected = 0;pause_dim = 1;previous = 0;
            skipleft = 0;lastcalls = 0;x6_pacer_reset(&stock_pacer);
            continue;
        }
        if (frame_result < 0 || stock_video_error
            || (stock_active && render && x6_stock_blit((uint32_t *)gbuf,
                (const uint32_t *)stockbuf, stock_w, stock_h, stock_pitch) != 0)) {
            paused = failed = 1;fatal = !stock_active;
            pause_selected = 0;pause_dim = 1;blocked_buttons = mask;previous = 0;
            continue;
        }
        if (render) present();
#ifdef X6_DEBUG_OVERLAY
        draw_debug((uint32_t *)front);
#endif
        if (++frames == 60) {say("frames", frames);say("audio_queued", afp ? fioctrl_(afp, 43, 1, 0) : 0);}
    }
    int32_t stopped = x6_session_stop(&session);
    say("module_stop", (uint32_t)stopped);
    if (!stopped && stockbuf) {pfree_(stockbuf, STOCK_PAGES);stockbuf = 0;}
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

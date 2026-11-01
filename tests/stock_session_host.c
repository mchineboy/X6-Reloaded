/* Only the native test uses these module-manager doubles. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "session.h"

static struct x6_session s;
static char events[512];
static uint32_t n, installed, next_id, fail_install, fail_open, fail_table, fail_load, fail_close, fail_uninstall;
static int inside_frame;
static void event(char c) {assert(n + 1 < sizeof events);events[n++] = c;events[n] = 0;}
static int32_t load(const struct x6_host_callbacks *cb, const char *game)
{
    assert(installed && cb && game);event('L');return fail_load ? -1 : 0;
}
static int32_t unload(void) {assert(installed);event('U');return -1;} /* Stock FBA's normal result. */
static int32_t frame(uint32_t render) {(void)render;assert(installed);inside_frame = 1;return 0;}
static int32_t buttons(uint32_t player, uint32_t mask) {(void)player;(void)mask;return 0;}
static uint32_t *video(uint32_t *w, uint32_t *h, uint32_t *p) {*w = 320;*h = 224;*p = 320;return 0;}
static const struct x6_emu_interface emu = {
    .load = load, .unload = unload, .frame = frame, .set_buttons = buttons, .video_info = video
};
static int32_t install(const char *path)
{
    assert(path && !installed && !inside_frame);event('I');
    if (fail_install) return -1;
    installed = ++next_id;return (int32_t)installed;
}
static uintptr_t open_(uint32_t id) {assert(id == installed);event('O');return fail_open ? 0 : 42;}
static const struct x6_emu_interface *interface_(uintptr_t handle)
{
    assert(handle == 42 && installed);event('T');return fail_table ? 0 : &emu;
}
static int32_t close_(uintptr_t handle)
{
    assert(handle == 42 && installed && !inside_frame);event('C');return fail_close ? -1 : 0;
}
static int32_t uninstall(uint32_t id)
{
    assert(id == installed && !inside_frame);event('D');
    if (fail_uninstall) return -1;
    installed = 0;return 0;
}
static const struct x6_module_ops ops = {install, open_, interface_, close_, uninstall};
static const struct x6_host_callbacks cb = {0};
static int32_t start(void) {return x6_session_start(&s, &ops, "module", &cb, "game");}
static void reset(void)
{
    assert(!s.id && !installed);n = next_id = 0;events[0] = 0;
    fail_install = fail_open = fail_table = fail_load = fail_close = fail_uninstall = 0;
}
static void lifecycle(void)
{
    reset();assert(start() == 0);
    assert(!strcmp(events, "IOTL"));
    assert(start() == -1 && !strcmp(events, "IOTL")); /* No overlapping load. */
    s.emu->frame(1);inside_frame = 0; /* Host regains control before switching. */
    assert(x6_session_stop(&s) == 0 && !s.id && !s.emu && !s.handle);
    assert(start() == 0); /* FBA */
    assert(x6_session_stop(&s) == 0);
    assert(start() == 0); /* Reloaded again */
    assert(x6_session_stop(&s) == 0 && x6_session_stop(&s) == 0);
    assert(!strcmp(events, "IOTLUCDIOTLUCDIOTLUCD"));

    reset();fail_install = 1;assert(start() == -1 && !s.id && !strcmp(events, "I"));
    reset();fail_open = 1;assert(start() == -1 && !s.id && !strcmp(events, "IOD"));
    reset();fail_table = 1;assert(start() == -1 && !s.id && !strcmp(events, "IOTCD"));
    reset();fail_load = 1;assert(start() == -1 && !s.id && !strcmp(events, "IOTLUCD"));

    reset();assert(start() == 0);fail_close = 1;
    assert(x6_session_stop(&s) == -1 && s.id && s.handle && !s.emu);
    uint32_t before = n;assert(start() == -1 && n == before);
    fail_close = 0;assert(x6_session_stop(&s) == 0);
    assert(!strcmp(events, "IOTLUCCD")); /* Unload is called exactly once. */

    reset();assert(start() == 0);fail_uninstall = 1;
    assert(x6_session_stop(&s) == -1 && s.id && !s.handle && !s.emu);
    before = n;assert(start() == -1 && n == before);
    fail_uninstall = 0;assert(x6_session_stop(&s) == 0);
    assert(!strcmp(events, "IOTLUCDD"));
}

static uint32_t source[512 * 512 + 2], target[320 * 240 + 2];
static void geometry(void)
{
    for (uint32_t i = 0; i < sizeof source / sizeof *source; ++i) source[i] = 0x7f123456;
    for (uint32_t i = 0; i < sizeof target / sizeof *target; ++i) target[i] = 0xabcdef12;
    const uint32_t sizes[][3] = {{320,224,320}, {384,224,400}, {224,320,224}, {512,512,512}, {1,1,1}};
    for (uint32_t i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        uint32_t w = sizes[i][0], h = sizes[i][1], p = sizes[i][2];
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < p; ++x) source[1 + y * p + x] = x < w ? y * 1024 + x : 0xdeadbeef;
        assert(x6_stock_blit(target + 1, source + 1, w, h, p) == 0);
        for (uint32_t y = 0; y < 240; ++y)
            for (uint32_t x = 0; x < 320; ++x)
                assert(target[1 + y * 320 + x] == (y * h / 240) * 1024 + x * w / 320);
        assert(target[0] == 0xabcdef12 && target[320 * 240 + 1] == 0xabcdef12);
    }
    const uint32_t bad[][3] = {{0,224,320}, {320,0,320}, {513,224,513}, {320,513,320}, {320,224,319}, {320,224,513}, {0xffffffff,1,0xffffffff}};
    for (uint32_t i = 0; i < sizeof bad / sizeof *bad; ++i) {
        uint32_t snapshot = target[1];
        assert(x6_stock_blit(target + 1, source + 1, bad[i][0], bad[i][1], bad[i][2]) == -1);
        assert(target[1] == snapshot);
    }
    assert(x6_stock_blit(0, source + 1, 320, 224, 320) == -1);
}

int main(void)
{
    lifecycle();geometry();
    puts("PASS: MAME/FBA/MAME lifecycle, partial failures, close/uninstall ownership, native video bounds and padded pitch");
    return 0;
}

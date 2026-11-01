/* Core and filesystem doubles only; never linked into firmware. The real
 * frontend/menu/music code handles every selection and handoff below. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"
#include "driver.h"
#include "x6_arcade.h"
#include "x6_catalog.h"
#include "x6_host_services.h"
#include "x6_runtime.h"

struct RunningMachine *Machine;
static uint32_t pixels[320 * 240], ticks, available, requests, checks, core_loads, inside_frame;
static int32_t request_result;
static uint32_t driver_count = 1, core_runs, load_success;
static char last_path[1024];
static uint32_t *buffer(void) {return pixels;}
static int32_t audio(const void *pcm, uint32_t bytes) {assert(pcm && bytes % 4 == 0);return (int32_t)bytes;}
uint32_t x6_time_ticks(void) {return ticks;}
void x6_time_delay(uint32_t count) {ticks += count;}
uint32_t x6_core_driver_count(void) {return driver_count;}
uint32_t x6_core_driver(uint32_t index, struct x6_driver_info *out)
{
    assert(index < driver_count);
    if (driver_count == 1) *out = (struct x6_driver_info){"dkong", "Donkey Kong", "1981", "Nintendo", 1, 0, 0};
    else {
        static char id[16], title[40];
        snprintf(id, sizeof id, "alpha%u", index);snprintf(title, sizeof title, "Alpha %02u", index);
        *out = (struct x6_driver_info){index == 42 ? "target" : id, index == 42 ? "Z Target" : title, "1981", "Fixture", 0, 0, 0};
    }
    return 1;
}
int32_t x6_file_size(const char *path) {return strstr(path, "F:/game/cps/") == path && strstr(path, ".zip") ? 100 : -1;}
void *x6_fs_open(const char *path, const char *mode) {(void)path;(void)mode;return 0;}
uint32_t x6_fs_read(void *ptr, uint32_t size, uint32_t count, void *f) {(void)ptr;(void)size;(void)count;(void)f;return 0;}
uint32_t x6_fs_write(const void *ptr, uint32_t size, uint32_t count, void *f) {(void)ptr;(void)size;(void)count;(void)f;return 0;}
int32_t x6_fs_close(void *f) {(void)f;return 0;}
void retro_set_environment(retro_environment_t cb) {(void)cb;}
void retro_set_video_refresh(retro_video_refresh_t cb) {(void)cb;}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {(void)cb;}
void retro_set_input_poll(retro_input_poll_t cb) {(void)cb;}
void retro_set_input_state(retro_input_state_t cb) {(void)cb;}
void retro_init(void) {}
void retro_deinit(void) {}
bool retro_load_game(const struct retro_game_info *game) {
    assert(game && game->path);++core_loads;
    snprintf(last_path, sizeof last_path, "%s", game->path);return load_success != 0;
}
void retro_unload_game(void) {}
void retro_run(void) {++core_runs;}
void retro_get_system_av_info(struct retro_system_av_info *av) {
    memset(av, 0, sizeof *av);av->geometry.base_width = 320;av->geometry.base_height = 240;
    av->timing.fps = 60;av->timing.sample_rate = 22050;
}

static uint32_t stock_available(void) {++checks;return available;}
static int32_t request_stock(void) {assert(inside_frame);++requests;return request_result;}
static void brightness(uint32_t level) {(void)level;}
static void idle(uint32_t blank, uint32_t off) {(void)blank;(void)off;}
static uint32_t restore_library(void) {return 2;}
static uint32_t restore_root(void) {return 0;}
static uint32_t requested_stock, metadata_failure, stock_disabled;
static uint32_t stock_count(void) {return 43;}
static int32_t stock_metadata(uint32_t index, struct x6_game *out)
{
    if (metadata_failure && index == 3) return -1;
    assert(index < 43);*out = (struct x6_game){.available = 1};
    if (index == 42 && stock_disabled) out->available = X6_GAME_FORMAT;
    snprintf(out->id, sizeof out->id, "stock%u", index);
    snprintf(out->title, sizeof out->title, index == 42 ? "Z Target" : "Alpha %02u", index);
    snprintf(out->path, sizeof out->path, "F:\\roms\\original\\stock%u.fba", index);
    return 0;
}
static int32_t request_stock_game(uint32_t index) {assert(inside_frame);requested_stock = index;++requests;return 0;}
static uint32_t restore_stock_game(void) {return 42;}
static int32_t walk(const char *path, x6_directory_visitor visitor, void *context)
{
    if (!strcmp(path, "F:/roms/mame")) {
        struct x6_directory_entry e = {.name = "aaa_unknown.zip", .size_low = 100};
        assert(visitor(&e, context) == 0);
    } else assert(!strcmp(path, "F:/game/cps"));
    return 0;
}
static const struct x6_host_services services = {
    .version = 2, .set_brightness = brightness, .set_idle = idle,
    .stock_available = stock_available, .request_stock = request_stock
};
static void load_menu(const struct x6_host_services *svc)
{
    struct x6_host_callbacks cb = {.submit_audio = audio, .get_output_buffer = buffer};
    memcpy(cb.resource_path, "X6HOST1", 8);
    memcpy(cb.resource_path + 8, &svc, sizeof svc);
    assert(x6_arcade_interface.load(&cb, "F:/game/cps/X6-ARCADE.zip") == 0);
}
static void frame(uint32_t mask)
{
    assert(x6_arcade_interface.set_buttons(0, mask) == 0);
    inside_frame = 1;
    assert(x6_arcade_interface.frame(1) == 0);
    inside_frame = 0;
}
static void choose_stock(void)
{
    frame(0);frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0);
    frame(X6_BTN_A);frame(0);
}
static void choose_mame(void)
{
    frame(0);frame(X6_BTN_A);frame(0);frame(X6_BTN_A);frame(0);
}

int main(void)
{
    available = 1;load_menu(&services);
    frame(X6_BTN_A);frame(X6_BTN_A); /* A held during load must not launch. */
    assert(!requests && !core_loads);
    choose_stock();
    assert(requests == 1 && checks == 1 && !core_loads);
    frame(X6_BTN_A);frame(0);frame(X6_BTN_A); /* Handoff stays queued; no second request. */
    assert(requests == 1 && !core_loads);
    assert(x6_arcade_interface.unload() == 0);

    request_result = -1;load_menu(&services);choose_stock();
    assert(requests == 2 && !core_loads);
    frame(0);frame(X6_BTN_B);frame(0);frame(X6_BTN_UP);frame(0);
    frame(X6_BTN_A);frame(0);frame(X6_BTN_A);frame(0);
    assert(core_loads == 1); /* Failed stock request leaves the MAME menu usable. */
    x6_arcade_interface.unload();

    available = 0;request_result = 0;load_menu(&services);choose_mame();
    assert(requests == 2 && core_loads == 2); /* No hidden stock entry when absent. */
    x6_arcade_interface.unload();

    /* Allocate only the old prefix. ASan detects any accidental v2-field read. */
    struct legacy_services {uint32_t version;void (*brightness)(uint32_t);void (*idle)(uint32_t,uint32_t);};
    struct legacy_services *legacy = malloc(sizeof *legacy);
    assert(legacy);*legacy = (struct legacy_services){1, brightness, idle};
    uint32_t before = checks;
    load_menu((const struct x6_host_services *)legacy);choose_mame();
    assert(requests == 2 && core_loads == 3 && checks == before);
    x6_arcade_interface.unload();free(legacy);

    /* Returning from FBA reopens the stock folder; Settings preserves it too. */
    available = 1;
    struct x6_host_services restore = services;restore.version = 3;restore.restore_library = restore_library;
    load_menu(&restore);frame(X6_BTN_A);frame(X6_BTN_A);assert(requests == 2);
    frame(0);frame(X6_BTN_SELECT);frame(0);frame(X6_BTN_B);frame(0);
    frame(X6_BTN_A);frame(0);assert(requests == 3 && core_loads == 3);
    x6_arcade_interface.unload();

    /* A filtered row zero must launch source driver 42, including after return. */
    available = 0;driver_count = 43;load_success = 1;
    load_menu(&services);frame(0);frame(X6_BTN_A);frame(0); /* MAME groups */
    frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0); /* S-Z folder */
    frame(X6_BTN_A);frame(0);
    assert(core_loads == 4 && !strcmp(last_path, "F:/game/cps/target.zip"));
    frame(X6_BTN_START);ticks += 201;frame(X6_BTN_START);frame(X6_BTN_START);frame(0);
    assert(core_runs > 0 && core_loads == 4);
    frame(X6_BTN_A);frame(0);
    assert(core_loads == 5 && !strcmp(last_path, "F:/game/cps/target.zip"));
    x6_arcade_interface.unload();

    /* Version 4 discovers multiple stock games and passes their source index;
       no title/filename is baked into the stock launch request. */
    uint32_t old_requests = requests;
    struct x6_host_services dynamic = services;
    dynamic.version = 4;dynamic.restore_library = restore_root;
    dynamic.stock_count = stock_count;dynamic.stock_metadata = stock_metadata;
    dynamic.request_stock_game = request_stock_game;dynamic.restore_stock_game = restore_stock_game;
    load_menu(&dynamic);frame(0);frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0);
    frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0);frame(X6_BTN_A);frame(0);
    assert(requests == old_requests + 1 && requested_stock == 42);
    x6_arcade_interface.unload();
    dynamic.restore_library = restore_library;
    load_menu(&dynamic);frame(X6_BTN_A);frame(0);frame(X6_BTN_A);frame(0);
    assert(requests == old_requests + 2 && requested_stock == 42);
    x6_arcade_interface.unload();
    metadata_failure = 1;load_menu(&dynamic);frame(0);frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0);
    assert(requests == old_requests + 2);x6_arcade_interface.unload();
    /* v5 enumeration keeps unknown ZIPs visible but cannot load either core.
       Moving past the disabled row still launches the supported MAME game. */
    metadata_failure = 0;driver_count = 1;
    dynamic.version = 5;dynamic.walk_directory = walk;dynamic.restore_library = restore_root;
    uint32_t old_loads = core_loads;old_requests = requests;
    load_menu(&dynamic);choose_mame();frame(X6_BTN_START);frame(0);
    assert(core_loads == old_loads && requests == old_requests);
    frame(X6_BTN_DOWN);frame(0);frame(X6_BTN_A);frame(0);
    assert(core_loads == old_loads + 1 && !strcmp(last_path, "F:/game/cps/dkong.zip"));
    x6_arcade_interface.unload();
    stock_disabled = 1;dynamic.restore_library = restore_library;
    load_menu(&dynamic);frame(0);frame(X6_BTN_A);frame(0);frame(X6_BTN_START);frame(0);
    assert(requests == old_requests && core_loads == old_loads + 1);
    x6_arcade_interface.unload();
    puts("PASS: real frontend folder selection, held buttons, deferred handoff, request failure, legacy hosts, stock-folder restore, settings and filtered MAME game return");
    return 0;
}

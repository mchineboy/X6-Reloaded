/* Arcade libretro frontend for the recovered stock module table.
 *
 * Two entry paths from the stock launcher:
 *  - X6-ARCADE.zip opens the arcade menu. Games start from the menu, and
 *    holding START for X6_RETURN_TICKS timer ticks (2 s) returns to it.
 *    (The stock launcher never reports Select and Start held together.)
 *  - <driver>.zip starts that game directly, with the diagnostic monitor
 *    overlay, as in the original Donkey Kong trial.
 *
 * Output is a 320x240 ARGB surface. Portrait output fills a centred 180x240
 * (3:4) area; landscape output fills the surface (4:3). Platform imports come
 * from the final runtime, not from fake core stubs. D-pad, Start/Select and
 * face masks follow corrected user observations. Shoulders remain unbound. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "libretro.h"
#include "x6_arcade.h"
#include "x6_catalog.h"
#include "x6_host_services.h"
#include "x6_browser.h"
#include "x6_monitor.h"
#include "x6_timing.h"
#include "x6_ui.h"
#include "x6_runtime.h"
#include "x6_music.h"
#ifdef X6_AREA_SCALE
#include "x6_scale.h"
static struct x6_area_scaler area_scaler;
static uint32_t area_ready;
#endif
#ifdef X6_MELIS_RUNTIME
#include "x6_guard.h"
#endif

#define WIDTH 320u
#define HEIGHT 240u
#define PORTRAIT_WIDTH 180u
#define MAX_SOURCE 512u
#define PATH_SIZE 1024u
#ifndef RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE
#define RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE (81 | 0x10000)
#endif

enum mode { MODE_IDLE, MODE_MENU, MODE_LOADING, MODE_GAME, MODE_PAUSE, MODE_SETTINGS, MODE_HANDOFF };

static struct x6_host_callbacks callbacks;
static volatile uint32_t buttons[5];
static uint32_t *last_buffer;
static uint32_t loaded, initialized, render_frame;
static int32_t error;
static char rom_path[PATH_SIZE];
static char resource_path[PATH_SIZE];
static char rom_directory[PATH_SIZE];
static struct x6_pacer pacer;
/* Cores above 22,050 Hz are linearly resampled to the launcher's fixed rate. */
static uint32_t source_rate = 22050, audio_phase;
static int16_t audio_previous[2], converted_audio[2048];

/* Prime the already configured host stream only on the first valid game audio.
 * Keep the estimate across in-menu launches so a quick restart does not add a
 * second full reservoir. The next load() resets it for a new host stream. */
static int16_t lead_silence[735*2];
static uint32_t audio_needs_lead,audio_have_output,audio_last_tick;
static uint32_t audio_ticks(void) {
#if defined(X6_MELIS_RUNTIME) || defined(X6_TIMING_TEST)
    return x6_time_ticks();
#else
    return 0;
#endif
}
static void host_audio(const void *data,uint32_t bytes) {
    callbacks.submit_audio(data,bytes);
    audio_last_tick=audio_ticks();audio_have_output=1;
}
static void prime_audio(void) {
    if(!audio_needs_lead)return;
    audio_needs_lead=0;
    uint32_t elapsed=audio_ticks()-audio_last_tick;
    uint32_t frames=!audio_have_output || elapsed>=10?2205:elapsed*22050/100;
    while(frames) {
        uint32_t chunk=frames>735?735:frames;
        host_audio(lead_silence,chunk*4);frames-=chunk;
    }
}


/* ---- Global settings (f:/x6/settings.bin) and the optional X6 host services ----
 * The standalone X6 shell passes "X6HOST1" + a pointer to x6_host_services in
 * resource_path; the stock launcher passes a plain path, so these stay unused there. */
static const struct x6_host_services *host_services;
struct x6_settings { uint8_t magic, version, music, clicks, brightness, blank, poweroff, reserved; };
static struct x6_settings settings = {0x58, 1, X6_TUNE_TECHNO, 1, 12, 0, 0, 0};
static const uint16_t blank_minutes[5] = {0, 1, 2, 5, 10};
static const uint16_t off_minutes[5] = {0, 5, 10, 15, 30};
#define SETTINGS_PATH "F:\\x6\\settings.bin"
static void settings_apply(void)
{
    if (!host_services) return;
    if (host_services->set_brightness) host_services->set_brightness(settings.brightness);
    if (host_services->set_idle)
        host_services->set_idle(blank_minutes[settings.blank % 5] * 60u, off_minutes[settings.poweroff % 5] * 60u);
}
static void settings_load(void)
{
    struct x6_settings loaded;
    void *f = x6_fs_open(SETTINGS_PATH, "rb");
    if (f) {
        if (x6_fs_read(&loaded, 1, sizeof loaded, f) == sizeof loaded && loaded.magic == 0x58 && loaded.version == 1) {
            if (loaded.music >= X6_TUNE_COUNT) loaded.music = X6_TUNE_TECHNO;
            if (loaded.brightness > 15) loaded.brightness = 12;
            loaded.blank %= 5;loaded.poweroff %= 5;loaded.clicks = loaded.clicks ? 1 : 0;
            settings = loaded;
        }
        x6_fs_close(f);
    }
    settings_apply();
}
static void settings_save(void)
{
    void *f = x6_fs_open(SETTINGS_PATH, "wb");
    if (!f) return;
    x6_fs_write(&settings, 1, sizeof settings, f);
    x6_fs_close(f);
}
static int16_t music_buf[368 * 2];
static uint32_t music_frame_parity;
static void music_frame(uint32_t tune)
{
    if (!callbacks.submit_audio) return;
    uint32_t count = (music_frame_parity++ & 1u) ? 368u : 367u;   /* 22050 / 60 per frame */
    x6_music_render(music_buf, count, tune);
    callbacks.submit_audio(music_buf, count * 4);
}
static void ui_click(void) {if (settings.clicks) x6_music_click();}

static enum mode mode;
static uint32_t from_menu, loading_shown, game_frames, hold_frames;
/* The return hold and in-game hint are timed in Melis ticks (assumed 100 Hz,
 * as in x6_timing.h), not emulated frames: slow games run far below 60 fps. */
#define X6_TICK_HZ 100u
#define X6_RETURN_TICKS (2u * X6_TICK_HZ)
static uint32_t game_start_tick, hold_start_tick, holding;
static uint32_t pause_selected, pause_dim, pause_prev, pause_note;
static uint32_t ticks_as_frames(uint32_t ticks) {return ticks > 0x7fffffffu / 60u ? 0x7fffffffu : ticks * 60u / X6_TICK_HZ;}
static struct x6_game games[X6_CATALOG_MAX];
static struct x6_game stock_games[X6_CATALOG_MAX];
static uint32_t game_count, current_game, portrait;
static struct x6_menu menu;
static struct x6_browser browser;
static uint32_t launch_library, launch_game;
static uint32_t menu_prev_buttons, settings_selected, settings_prev;
static const struct x6_game stock_game = {
    .id = "2020bb", .title = "2020 Super Baseball", .maker = "Stock arcade library", .available = 1
};

static uint32_t copy_path(char *out, uint32_t capacity, const char *in, uint32_t limit)
{
    if (!in || !capacity) return 0;
    for (uint32_t i = 0; i < limit && i < capacity; ++i) {
        out[i] = in[i] == '\\' ? '/' : in[i];
        if (!out[i]) return i != 0;
    }
    out[0] = 0;return 0;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) if (*p == '/') base = p + 1;
    return base;
}

static char lower(char c) {return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;}

static uint32_t same_name(const char *name, const char *expected)
{
    uint32_t i = 0;
    for (; expected[i]; ++i) if (lower(name[i]) != expected[i]) return 0;
    return name[i] == 0;
}

/* Base name equals "<id>.zip", ignoring case. */
static uint32_t names_driver(const char *name, const char *id)
{
    uint32_t i = 0;
    for (; id[i]; ++i) if (lower(name[i]) != id[i]) return 0;
    return same_name(name + i, ".zip");
}

static uint32_t parent_directory(char *out, const char *path)
{
    uint32_t last = 0, i = 0;
    for (; path[i] && i + 1 < PATH_SIZE; ++i) {out[i] = path[i];if (path[i] == '/') last = i;}
    out[last] = 0;
    return last != 0;
}

static bool environment(unsigned command, void *data)
{
    switch (command) {
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if(!data)return false;
        *(int *)data=RETRO_AV_ENABLE_AUDIO | (render_frame?RETRO_AV_ENABLE_VIDEO:0);
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (!data) return false;
        *(unsigned *)data = 0;return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return data && *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data) return false;
        *(const char **)data = resource_path;return true;
    case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE:
        if (!data) return false;
        *(unsigned *)data = 22050;return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *variable = data;
        if (!variable || !variable->key) return false;
        const char *key = variable->key;
        static const char skip[] = "mame2000-skip_disclaimer";
        static const char info[] = "mame2000-show_gameinfo";
        uint32_t a = 0, b = 0;
        while (key[a] && skip[a] && key[a] == skip[a]) ++a;
        while (key[b] && info[b] && key[b] == info[b]) ++b;
        if (!key[a] && !skip[a]) {variable->value = "enabled";return true;}
        if (!key[b] && !info[b]) {variable->value = "disabled";return true;}
        return false;
    }
    default: return false;
    }
}

/* Scaling lookups, rebuilt when the core's output size changes, and a shared
 * RGB565 -> ARGB table: no per-pixel division or channel expansion. */
static uint16_t source_x[WIDTH], source_y[HEIGHT];
static uint32_t rgb565_colors[65536], colors_ready, scaled_width, scaled_height;

static void prepare_video(uint32_t width, uint32_t height, uint32_t content)
{
    for (uint32_t x = 0; x < content; ++x) source_x[x] = (uint16_t)(((x * 2u + 1u) * width) / (content * 2u));
    for (uint32_t y = 0; y < HEIGHT; ++y) source_y[y] = (uint16_t)(((y * 2u + 1u) * height) / (HEIGHT * 2u));
    scaled_width = width;scaled_height = height;
#ifdef X6_AREA_SCALE
    area_ready=(width>content || height>HEIGHT)?x6_area_prepare(&area_scaler,width,height,content,HEIGHT):0;
#endif
    if (!colors_ready) {
        for (uint32_t p = 0; p < 65536u; ++p) {
            uint32_t r = (p >> 11) & 31u, g = (p >> 5) & 63u, b = p & 31u;
            rgb565_colors[p] = 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
        colors_ready = 1;
    }
}

static void video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    /* Intentional host omissions are already H; D is a null requested picture. */
    if(render_frame)x6_monitor_video(data!=0,0);
    if (!render_frame || !data) return; /* Null denotes a duplicate frame. */
    if (!width || !height || width > MAX_SOURCE || height > MAX_SOURCE || pitch < width * 2u
        || (pitch & 1u) || ((uintptr_t)data & 1u)) {
        error = 2;return;
    }
    uint32_t *pixels = callbacks.get_output_buffer();
    if (!pixels) {error = 3;return;}
    last_buffer = pixels;
    x6_monitor_video(1,1);
    uint32_t content = height > width ? PORTRAIT_WIDTH : WIDTH;
    uint32_t left = (WIDTH - content) / 2u;
    if (width != scaled_width || height != scaled_height || !colors_ready) prepare_video(width, height, content);
#ifdef X6_AREA_SCALE
    if(width>content || height>HEIGHT) {
        if(!area_ready) {error=2;return;}
        for(uint32_t y=0;y<HEIGHT;++y) {
            uint32_t *row=pixels+y*WIDTH;
            for(uint32_t x=0;x<left;++x)row[x]=0xff000000u;
            for(uint32_t x=left+content;x<WIDTH;++x)row[x]=0xff000000u;
        }
        if(!x6_area_render(&area_scaler,data,(uint32_t)pitch,pixels+left,WIDTH,rgb565_colors))error=2;
        return;
    }
#endif
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        const uint16_t *source = (const uint16_t *)((const uint8_t *)data + source_y[y] * pitch);
        uint32_t *row = pixels + y * WIDTH;
        for (uint32_t x = 0; x < left; ++x) row[x] = 0xff000000u;
        for (uint32_t x = 0; x < content; ++x) row[left + x] = rgb565_colors[source[source_x[x]]];
        for (uint32_t x = left + content; x < WIDTH; ++x) row[x] = 0xff000000u;
    }
}

static size_t audio(const int16_t *data, size_t frames)
{
    if (!frames) return 0;
    if (!data || ((uintptr_t)data & 1u) || frames > UINT32_MAX / 4u) {error = 4;return 0;}
    if (!callbacks.submit_audio) return frames;
    prime_audio();
    if (source_rate == 22050) {host_audio(data, (uint32_t)frames * 4u);return frames;}
    uint32_t used = 0;
    for (uint32_t i = 0; i < frames; ++i) {
        audio_phase += 22050;
        if (audio_phase >= source_rate) {
            audio_phase -= source_rate;
            uint32_t fraction = 22050 - audio_phase;
            for (uint32_t c = 0; c < 2; ++c) {
                int32_t previous = audio_previous[c], current = data[i * 2 + c];
                converted_audio[used * 2 + c] = (int16_t)(previous + (current - previous) * (int32_t)fraction / 22050);
            }
            if (++used == 1024) {host_audio(converted_audio, used * 4);used = 0;}
        }
        audio_previous[0] = data[i * 2];audio_previous[1] = data[i * 2 + 1];
    }
    if (used) host_audio(converted_audio, used * 4);
    return frames;
}

static void poll(void) {} /* Stock pushes masks through set_buttons. */

/* Rapid fire (pause-menu cheat): a held button pulses with a period of rapid_period[rate] frames
 * (on for the first half). Rates: Off, 30, 20, 15, 10, 7.5 Hz at 60 fps. Buttons: All/A/B/X/Y. */
static const uint8_t rapid_period[6] = {0, 2, 3, 4, 6, 8};
static const char *const rapid_rate_name[6] = {"Off", "30 Hz", "20 Hz", "15 Hz", "10 Hz", "7.5 Hz"};
static const uint32_t rapid_button_mask[5] = {X6_BTN_A | X6_BTN_B | X6_BTN_X | X6_BTN_Y, X6_BTN_A, X6_BTN_B, X6_BTN_X, X6_BTN_Y};
static const char *const rapid_button_name[5] = {"All", "A", "B", "X", "Y"};
static uint32_t rapid_rate, rapid_buttons, rapid_phase;
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port >= 5 || device != RETRO_DEVICE_JOYPAD || index) return 0;
    uint32_t held = buttons[port], mask;
    /* Device-test-002: preserve physical names; MAME button 1 is libretro B. */
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_START: mask = X6_BTN_START;break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: mask = X6_BTN_SELECT;break;
    case RETRO_DEVICE_ID_JOYPAD_UP: mask = X6_BTN_UP;break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN: mask = X6_BTN_DOWN;break;
    case RETRO_DEVICE_ID_JOYPAD_LEFT: mask = X6_BTN_LEFT;break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: mask = X6_BTN_RIGHT;break;
    case RETRO_DEVICE_ID_JOYPAD_B: mask = X6_BTN_B;break;
    case RETRO_DEVICE_ID_JOYPAD_A: mask = X6_BTN_A;break;
    case RETRO_DEVICE_ID_JOYPAD_Y: mask = X6_BTN_Y;break;
    case RETRO_DEVICE_ID_JOYPAD_X: mask = X6_BTN_X;break;
    /* Bits 8/9 and the physical shoulder/stick relationships are unmeasured.
     * Never reuse Start/Select bits 10/11 as libretro shoulder buttons. */
    default: return 0;
    }
    if (rapid_rate && (mask & rapid_button_mask[rapid_buttons])) {
        uint32_t period = rapid_period[rapid_rate];
        if (rapid_phase % period >= (period + 1) / 2) return 0;   /* off part of the cycle */
    }
    return (held & mask) != 0;
}

static void stop_game(void)
{
    uint32_t was_loaded = loaded;
    loaded = 0;render_frame = 0;
    if (was_loaded) retro_unload_game();
    if (initialized) retro_deinit();
    initialized = 0;
}


/* ---- Per-game difficulty: the driver's "Difficulty" DIP switch, chosen in the pause menu,
 * saved as F:/<rom dir>/<id>.x6d (one byte = setting index) and applied after each load. */
#include "driver.h"
#define DIP_MAX 8
static struct InputPort *dip_name_port;
static struct InputPort *dip_settings[DIP_MAX];
static uint32_t dip_count, dip_index;
static uint32_t has_text(const char *s, const char *needle)
{
    for (; s && *s; ++s) {
        uint32_t i = 0;
        while (needle[i] && lower(s[i]) == needle[i]) ++i;
        if (!needle[i]) return 1;
    }
    return 0;
}
static void dip_find(void)
{
    dip_name_port = 0;dip_count = 0;dip_index = 0;
    if (!Machine || !Machine->input_ports) return;
    for (struct InputPort *in = Machine->input_ports; (in->type & ~IPF_MASK) != IPT_END; ++in) {
        if ((in->type & ~IPF_MASK) != IPT_DIPSWITCH_NAME || !has_text(in->name, "difficult")) continue;
        dip_name_port = in;
        for (struct InputPort *set = in + 1; (set->type & ~IPF_MASK) == IPT_DIPSWITCH_SETTING && dip_count < DIP_MAX; ++set)
            dip_settings[dip_count++] = set;
        for (uint32_t i = 0; i < dip_count; ++i)
            if (dip_settings[i]->default_value == (in->default_value & in->mask)) dip_index = i;
        if (dip_count < 2) {dip_name_port = 0;dip_count = 0;}
        return;
    }
}
static void dip_set(uint32_t index)
{
    if (!dip_name_port || index >= dip_count) return;
    dip_index = index;
    dip_name_port->default_value = (uint16_t)((dip_name_port->default_value & ~dip_name_port->mask)
                                              | (dip_settings[index]->default_value & dip_name_port->mask));
}
static uint32_t dip_path(char *out, uint32_t capacity, const char *id)
{
    uint32_t n = 0;
    char directory[PATH_SIZE];
    const char *base = rom_directory;
    if (current_game < game_count && games[current_game].path[0]
        && parent_directory(directory, games[current_game].path)) base = directory;
    for (; base[n] && n + 1 < capacity; ++n) out[n] = base[n];
    if (n + 1 < capacity) out[n++] = '/';
    for (const char *c = id; *c && n + 1 < capacity; ++c) out[n++] = *c;
    for (const char *c = ".x6d"; *c && n + 1 < capacity; ++c) out[n++] = *c;
    out[n] = 0;
    for (uint32_t i = 0; out[i]; ++i) if (out[i] == '/') out[i] = '\\';
    return n;
}
/* per-game settings file: [difficulty index | 0xff][rapid rate][rapid buttons]
 * (older 2-byte files stored rapid 1 = on, which maps to 15 Hz) */
static void dip_load_saved(const char *id)
{
    char path[PATH_SIZE + 24];uint8_t value[3] = {0xff, 0, 0};
    dip_find();rapid_rate = 0;rapid_buttons = 0;rapid_phase = 0;
    dip_path(path, sizeof(path), id);
    void *f = x6_fs_open(path, "rb");
    if (!f) return;
    uint32_t n = x6_fs_read(value, 1, 3, f);
    x6_fs_close(f);
    if (n >= 1 && dip_name_port && value[0] < dip_count) dip_set(value[0]);
    if (n == 2) rapid_rate = value[1] == 1 ? 3 : 0;
    if (n >= 3) {rapid_rate = value[1] < 6 ? value[1] : 0;rapid_buttons = value[2] < 5 ? value[2] : 0;}
}
static void dip_save(const char *id)
{
    char path[PATH_SIZE + 24];
    uint8_t value[3] = {(uint8_t)(dip_name_port ? dip_index : 0xff), (uint8_t)rapid_rate, (uint8_t)rapid_buttons};
    dip_path(path, sizeof(path), id);
    void *f = x6_fs_open(path, "wb");
    if (!f) return;
    x6_fs_write(value, 1, 3, f);
    x6_fs_close(f);
}

/* Start games[index] from rom_path. Returns 0, or -1 with error set. */
static int32_t start_game(uint32_t index)
{
    if (index >= game_count || games[index].available != X6_GAME_READY) {error = 1;return -1;}
    current_game = index;
    retro_set_environment(environment);retro_set_video_refresh(video);
    retro_set_audio_sample_batch(audio);retro_set_input_poll(poll);retro_set_input_state(input);
    scaled_width = scaled_height = 0;
    source_rate = 22050;audio_phase = 0;audio_previous[0] = audio_previous[1] = 0;
    retro_init();initialized = 1;
    struct retro_game_info game = {0};game.path = rom_path;
    if (!retro_load_game(&game)) {error = 5;stop_game();return -1;}
    loaded = 1;
    dip_load_saved(games[index].id);
    struct retro_system_av_info av = {0};retro_get_system_av_info(&av);
    if (!(av.timing.sample_rate >= 22050.0 && av.timing.sample_rate <= 192000.0)
        || av.timing.sample_rate != (double)(uint32_t)av.timing.sample_rate || av.timing.fps < 50.0 || av.timing.fps > 61.0
        || !av.geometry.base_width || !av.geometry.base_height
        || av.geometry.base_width > MAX_SOURCE || av.geometry.base_height > MAX_SOURCE) {
        error = 6;stop_game();return -1;
    }
    source_rate = (uint32_t)av.timing.sample_rate;
    portrait = av.geometry.base_height > av.geometry.base_width;
    game_frames = 0;hold_frames = 0;audio_needs_lead=1;holding = 0;game_start_tick = x6_time_ticks();
    x6_pacer_reset(&pacer);
    /* Near-60 Hz games keep the measured 60 Hz pacing; slower boards run at their own rate. */
    if (av.timing.fps < 59.0) x6_pacer_set_rate(&pacer, (uint32_t)(av.timing.fps + 0.5));
    return 0;
}

static void open_browser(void)
{
    x6_menu_init(&menu, browser.entries, browser.count, browser.selected);
    x6_menu_location(&menu, browser.location, browser.level != X6_BROWSER_ROOT);
    if (browser.read_error) x6_menu_notice(&menu, "Library cache unreadable; reboot");
    menu_prev_buttons = buttons[0];
    mode = MODE_MENU;loading_shown = 0;x6_music_reset();
    x6_pacer_reset(&pacer);
}

static void open_menu(uint32_t game_index)
{
    x6_browser_focus(&browser, X6_LIBRARY_MAME, game_index);
    open_browser();
}

/* Build rom_path as <rom_directory>/<id>.zip for a menu choice. */
static uint32_t menu_rom_path(const struct x6_game *game)
{
    if (game->path[0]) return copy_path(rom_path, sizeof rom_path, game->path, sizeof game->path);
    uint32_t n = 0;
    for (; rom_directory[n] && n < PATH_SIZE - 1; ++n) rom_path[n] = rom_directory[n];
    const char *parts[3] = {"/", game->id, ".zip"};
    for (uint32_t p = 0; p < 3; ++p)
        for (const char *c = parts[p]; *c; ++c) {
            if (n + 1 >= PATH_SIZE) return 0;
            rom_path[n++] = *c;
        }
    rom_path[n] = 0;
    return 1;
}

static int32_t unload_impl(void)
{
    stop_game();
    mode = MODE_IDLE;from_menu = 0;loading_shown = 0;
    last_buffer = 0;game_count = 0;
    callbacks = (struct x6_host_callbacks){0};
    for (uint32_t i = 0; i < 5; ++i) buttons[i] = 0;
    return 0;
}

static int32_t unload_call(void *unused) {(void)unused;return unload_impl();}
static int32_t guarded(int32_t (*function)(void *),void *context)
{
#ifdef X6_MELIS_RUNTIME
    int32_t result=x6_stack_guarded(function,context);
    if (result<=-125) {error=7;return -1;}
    return result;
#else
    return function(context);
#endif
}
static int32_t unload(void) {return guarded(unload_call,0);}

static int32_t load_impl(const struct x6_host_callbacks *host, const char *path)
{
    if (initialized || loaded || mode != MODE_IDLE) return -1;
    error = 0;last_buffer = 0;audio_needs_lead=0;audio_have_output=0;audio_last_tick=0;
    x6_monitor_reset();x6_pacer_reset(&pacer);
    if (!host || !host->get_output_buffer || !copy_path(rom_path, sizeof(rom_path), path, PATH_SIZE)
        || !parent_directory(rom_directory, rom_path)) {error = 1;return -1;}
    host_services = 0;
    if (host->resource_path[0] == 'X' && host->resource_path[1] == '6' && host->resource_path[2] == 'H'
        && host->resource_path[3] == 'O' && host->resource_path[4] == 'S' && host->resource_path[5] == 'T'
        && host->resource_path[6] == '1' && host->resource_path[7] == 0) {
        const struct x6_host_services *svc;
        const uint8_t *src = (const uint8_t *)host->resource_path + 8;uint8_t *dst = (uint8_t *)&svc;
        for (uint32_t i = 0; i < sizeof svc; ++i) dst[i] = src[i];
        if (svc && svc->version >= 1 && svc->version <= 8) host_services = svc;
        copy_path(resource_path, sizeof(resource_path), rom_directory, PATH_SIZE);
    } else if (host->resource_path[0]) {
        if (!copy_path(resource_path, sizeof(resource_path), host->resource_path, sizeof(host->resource_path))) {
            error = 1;return -1;
        }
    } else {
        copy_path(resource_path, sizeof(resource_path), rom_directory, PATH_SIZE);
    }
    const char *name = base_name(rom_path);
    uint32_t menu_entry = same_name(name, "x6-arcade.zip");
    game_count = x6_catalog_scan(games, X6_CATALOG_MAX, rom_directory,
        host_services && host_services->version >= 5 ? host_services->walk_directory : 0);
    if (host_services && host_services->version >= 6 && host_services->resolve_title) {
        for (uint32_t i = 0; i < game_count; ++i) host_services->resolve_title(&games[i], X6_LIBRARY_MAME);
        x6_catalog_sort(games, game_count);
    }
    uint32_t stock_count = 0;
    if (menu_entry && host_services && host_services->version >= 6 && host_services->libraries && host_services->request_library_game) {
        /* New hosts supply only the requested page; no 512-game preload. */
    } else if (menu_entry && host_services && host_services->version >= 4 && host_services->stock_count
        && host_services->stock_metadata && host_services->request_stock_game) {
        stock_count = host_services->stock_count();
        if (stock_count > X6_CATALOG_MAX) stock_count = X6_CATALOG_MAX;
        for (uint32_t i = 0; i < stock_count; ++i)
            if (host_services->stock_metadata(i, &stock_games[i]) != 0) {stock_count = 0;break;}
    } else if (menu_entry && host_services && host_services->version >= 2
        && host_services->stock_available && host_services->request_stock && host_services->stock_available()) {
        stock_count = 1;stock_games[0] = stock_game; /* Compatibility with preceding hosts. */
    }
    uint32_t index = game_count;
    if (!menu_entry)
        for (uint32_t i = 0; i < game_count; ++i) if (names_driver(name, games[i].id)) {index = i;break;}
    if (!menu_entry && (index == game_count || games[index].available != X6_GAME_READY)) {
        error = 1;game_count = 0;return -1;
    }
    callbacks.submit_audio = host->submit_audio;
    callbacks.get_output_buffer = host->get_output_buffer;
    for (uint32_t i = 0; i < 5; ++i) buttons[i] = 0;
    if (menu_entry) {
        from_menu = 1;settings_load();
        if (host_services && host_services->version >= 6 && host_services->libraries && host_services->request_library_game)
            x6_browser_init_provider(&browser, games, game_count, host_services->libraries);
        else x6_browser_init(&browser, games, game_count, stock_games, stock_count);
        if (x6_catalog_status() || (host_services && host_services->version >= 6
            && host_services->library_status && host_services->library_status()))
            x6_browser_incomplete(&browser);
        if (host_services && host_services->version >= 7 && host_services->scan_info) {
            const struct x6_scan_info *scan = host_services->scan_info();
            if (scan && scan->errors)
                x6_browser_error(&browser, x6_scan_error_message(scan), scan->error_path, scan->errors);
            else if (x6_catalog_status())
                x6_browser_error(&browser, "MAME file scan did not finish", "F:/roms/mame", x6_catalog_status());
        }
        if (host_services && host_services->version >= 3 && host_services->restore_library
            && host_services->restore_library() >= 2) {
            uint32_t index = host_services->version >= 4 && host_services->restore_stock_game
                ? host_services->restore_stock_game() : 0;
            uint32_t lib = host_services->version >= 6 ? host_services->restore_library() - 1 : X6_LIBRARY_STOCK;
            x6_browser_focus(&browser, lib, index);
        }
        open_browser();
        return 0;
    }
    from_menu = 0;
    if (start_game(index)) {
        int32_t failure = error;
        unload_impl();error = failure;return -1;
    }
    mode = MODE_GAME;
    return 0;
}

struct load_arguments {const struct x6_host_callbacks *host;const char *path;};
static int32_t load_call(void *context)
{
    struct load_arguments *args=context;return load_impl(args->host,args->path);
}
static int32_t load(const struct x6_host_callbacks *host,const char *path)
{
    struct load_arguments args={host,path};return guarded(load_call,&args);
}

static uint32_t *surface_for_frame(struct x6_surface *s)
{
    uint32_t *pixels = callbacks.get_output_buffer();
    if (!pixels) {error = 3;return 0;}
    last_buffer = pixels;
    *s = (struct x6_surface){pixels, WIDTH, HEIGHT, WIDTH};
    return pixels;
}

static void frame_menu(uint32_t render)
{
    struct x6_surface s;
    uint32_t before = menu.selected;
    uint32_t pressed = buttons[0] & ~menu_prev_buttons;menu_prev_buttons = buttons[0];
    if (pressed & X6_BTN_SELECT) {                         /* Select: settings screen */
        browser.selected = menu.selected;
        mode = MODE_SETTINGS;settings_selected = 0;settings_prev = buttons[0];ui_click();
        music_frame(settings.music);
        return;
    }
    enum x6_menu_action action = x6_menu_update(&menu, buttons[0]);
    if (action == X6_MENU_BACK) {
        if (x6_browser_back(&browser, menu.selected)) {open_browser();ui_click();}
    } else if (action == X6_MENU_LAUNCH) {
        enum x6_browser_action chosen = x6_browser_choose(&browser, menu.selected, &launch_game);
        if (chosen == X6_BROWSER_ENTER) {open_browser();ui_click();}
        else if (chosen == X6_BROWSER_PLAY) {
            launch_library = browser.library;mode = MODE_LOADING;loading_shown = 0;ui_click();
        }
    }
    if (menu.selected != before) ui_click();
    if (render && surface_for_frame(&s)) {
        if (mode == MODE_LOADING) {x6_ui_draw_loading(&s, &browser.entries[menu.selected]);loading_shown = 1;}
        else x6_menu_draw(&menu, &s);
    }
    music_frame(mode == MODE_MENU ? settings.music : X6_TUNE_OFF);   /* clicks still play while loading starts */
}

/* Settings screen: items adjusted with Left/Right (or A), B/Select returns and saves. */
enum { SET_MUSIC, SET_CLICKS, SET_BRIGHTNESS, SET_BLANK, SET_POWEROFF, SET_COUNT };
static void frame_settings(uint32_t render)
{
    static const char *const tunes[X6_TUNE_COUNT] = {"Off", "Chiptune", "Techno", "Rock"};
    static char lines[SET_COUNT][40];
    static const char *const names[SET_COUNT] = {"Music", "Menu clicks", "Brightness", "Screen blank", "Auto power off"};
    uint32_t pressed = buttons[0] & ~settings_prev;settings_prev = buttons[0];
    if (pressed & X6_BTN_UP) {settings_selected = (settings_selected + SET_COUNT - 1) % SET_COUNT;ui_click();}
    if (pressed & X6_BTN_DOWN) {settings_selected = (settings_selected + 1) % SET_COUNT;ui_click();}
    if (pressed & (X6_BTN_LEFT | X6_BTN_RIGHT | X6_BTN_A)) {
        int32_t d = (pressed & X6_BTN_LEFT) ? -1 : 1;
        switch (settings_selected) {
        case SET_MUSIC: settings.music = (uint8_t)((settings.music + X6_TUNE_COUNT + d) % X6_TUNE_COUNT);x6_music_reset();break;
        case SET_CLICKS: settings.clicks = !settings.clicks;break;
        case SET_BRIGHTNESS: {int32_t b = settings.brightness + d;settings.brightness = (uint8_t)(b < 0 ? 0 : b > 15 ? 15 : b);break;}
        case SET_BLANK: settings.blank = (uint8_t)((settings.blank + 5 + d) % 5);break;
        case SET_POWEROFF: settings.poweroff = (uint8_t)((settings.poweroff + 5 + d) % 5);break;
        }
        settings_apply();ui_click();
    }
    if (pressed & (X6_BTN_B | X6_BTN_SELECT)) {
        settings_save();ui_click();open_browser();
        music_frame(settings.music);
        return;
    }
    for (uint32_t i = 0; i < SET_COUNT; ++i) {
        char value[16];uint32_t n = 0;const char *v = value;
        switch (i) {
        case SET_MUSIC: v = tunes[settings.music % X6_TUNE_COUNT];break;
        case SET_CLICKS: v = settings.clicks ? "On" : "Off";break;
        case SET_BRIGHTNESS: value[n++] = (char)('0' + (settings.brightness + 1) / 10 % 10);value[n++] = (char)('0' + (settings.brightness + 1) % 10);
                             if (value[0] == '0') {value[0] = value[1];n = 1;} value[n++] = ' ';value[n++] = '/';value[n++] = ' ';value[n++] = '1';value[n++] = '6';value[n] = 0;break;
        case SET_BLANK: case SET_POWEROFF: {
            uint32_t m = i == SET_BLANK ? blank_minutes[settings.blank % 5] : off_minutes[settings.poweroff % 5];
            if (!m) v = "Never";
            else {if (m >= 10) value[n++] = (char)('0' + m / 10);value[n++] = (char)('0' + m % 10);
                  value[n++] = ' ';value[n++] = 'm';value[n++] = 'i';value[n++] = 'n';value[n] = 0;}
            break;}
        }
        uint32_t k = 0;
        for (const char *c = names[i]; *c && k + 1 < sizeof lines[i]; ++c) lines[i][k++] = *c;
        lines[i][k++] = '\t';
        for (const char *c = v; *c && k + 1 < sizeof lines[i]; ++c) lines[i][k++] = *c;
        lines[i][k] = 0;
    }
    struct x6_surface s;
    if (render && surface_for_frame(&s)) {
        const char *items[SET_COUNT];
        for (uint32_t i = 0; i < SET_COUNT; ++i) items[i] = lines[i];
        x6_ui_draw_settings(&s, items, SET_COUNT, settings_selected,
                            host_services ? 0 : "Brightness and timers need the X6 shell");
    }
    music_frame(settings.music);
}

static void frame_loading(uint32_t render)
{
    struct x6_surface s;
    if (!loading_shown) { /* Show the loading screen before the blocking load. */
        if (render && surface_for_frame(&s)) {x6_ui_draw_loading(&s, &browser.entries[menu.selected]);loading_shown = 1;}
        return;
    }
    if (launch_library != X6_LIBRARY_MAME) {
        int32_t result = host_services->version >= 6 && host_services->request_library_game
            ? host_services->request_library_game(launch_library, launch_game)
            : launch_library != X6_LIBRARY_STOCK ? -1 : host_services->version >= 4
                ? host_services->request_stock_game(launch_game) : host_services->request_stock();
        if (result == 0) mode = MODE_HANDOFF;
        else {open_browser();x6_menu_notice(&menu, "Original game is unavailable");}
        return;
    }
    if (!menu_rom_path(&games[launch_game]) || start_game(launch_game)) {
        error = 0; /* The menu stays usable; the notice explains the failure. */
        open_browser();
        x6_menu_notice(&menu, "That game could not be started");
        return;
    }
    mode = MODE_GAME;
}

static void frame_game(uint32_t render)
{
    const uint32_t chord = X6_BTN_START; /* Start still reaches the game while held */
    if (from_menu && (buttons[0] & X6_BTN_MENU) && !(pause_prev & X6_BTN_MENU)) {
        pause_prev = buttons[0];mode = MODE_PAUSE;pause_selected = 0;pause_dim = 1;pause_note = 0;
        return;
    }
    pause_prev = buttons[0];
    if (from_menu) {
        uint32_t now = x6_time_ticks();
        if ((buttons[0] & chord) == chord) {
            if (!holding) {holding = 1;hold_start_tick = now;}
            uint32_t elapsed = now - hold_start_tick;
            hold_frames = (elapsed >= X6_RETURN_TICKS ? X6_UI_RETURN_HOLD : elapsed * X6_UI_RETURN_HOLD / X6_RETURN_TICKS) + 1;
        } else {
            holding = 0;hold_frames = 0;
        }
        game_frames = ticks_as_frames(now - game_start_tick);
        if (holding && now - hold_start_tick >= X6_RETURN_TICKS) {
            uint32_t selected = current_game;
            stop_game();open_menu(selected);
            struct x6_surface s;
            if (render && surface_for_frame(&s)) x6_menu_draw(&menu, &s);
            return;
        }
    }
    render_frame = render != 0;
    ++rapid_phase;
    retro_run();render_frame = 0;
    if (render && last_buffer) {
        if (from_menu) {
            struct x6_surface s = {last_buffer, WIDTH, HEIGHT, WIDTH};
            x6_monitor_draw_fps(last_buffer, pacer.fps_tenths); /* temporary speed readout */
            /* Select re-shows the hint; holding Start shows the fill bar. */
            uint32_t partial = (buttons[0] & (X6_BTN_SELECT | X6_BTN_START)) != 0;
            x6_ui_draw_game_hint(&s, partial && !hold_frames ? 0 : game_frames, hold_frames);
        } else if (portrait) {
            x6_monitor_draw(last_buffer, pacer.fps_tenths);
        } else { /* sidebars would cover a landscape picture */
            x6_monitor_draw_fps(last_buffer, pacer.fps_tenths);
        }
    }
}


enum pause_item { PAUSE_RESUME, PAUSE_RESTART, PAUSE_DIFFICULTY, PAUSE_RAPID, PAUSE_RAPID_BUTTONS, PAUSE_BACK };
static void frame_pause(uint32_t render)
{
    enum pause_item ids[6];const char *labels[6];uint32_t count = 0;
    static char rapid_label[32], rapid_buttons_label[32];
    static char difficulty_label[48];
    ids[count] = PAUSE_RESUME;labels[count++] = "Resume";
    ids[count] = PAUSE_RESTART;labels[count++] = "Restart game";
    if (dip_name_port) {
        uint32_t n = 0;const char *prefix = "Difficulty: ", *value = dip_settings[dip_index]->name;
        while (*prefix && n + 1 < sizeof(difficulty_label)) difficulty_label[n++] = *prefix++;
        while (value && *value && n + 1 < sizeof(difficulty_label)) difficulty_label[n++] = *value++;
        difficulty_label[n] = 0;
        ids[count] = PAUSE_DIFFICULTY;labels[count++] = difficulty_label;
    }
    {
        uint32_t n = 0;const char *a = "Rapid fire: ", *b = rapid_rate_name[rapid_rate];
        while (*a) rapid_label[n++] = *a++;
        while (*b) rapid_label[n++] = *b++;
        rapid_label[n] = 0;
        ids[count] = PAUSE_RAPID;labels[count++] = rapid_label;
        if (rapid_rate) {
            n = 0;a = "Rapid buttons: ";b = rapid_button_name[rapid_buttons];
            while (*a) rapid_buttons_label[n++] = *a++;
            while (*b) rapid_buttons_label[n++] = *b++;
            rapid_buttons_label[n] = 0;
            ids[count] = PAUSE_RAPID_BUTTONS;labels[count++] = rapid_buttons_label;
        }
    }
    ids[count] = PAUSE_BACK;labels[count++] = "Back to game list";
    if (pause_selected >= count) pause_selected = 0;

    uint32_t now = buttons[0], pressed = now & ~pause_prev;
    pause_prev = now;
    if (pressed & X6_BTN_UP) {pause_selected = (pause_selected + count - 1) % count;ui_click();}
    if (pressed & X6_BTN_DOWN) {pause_selected = (pause_selected + 1) % count;ui_click();}
    if (pressed & (X6_BTN_B | X6_BTN_MENU)) {mode = MODE_GAME;x6_pacer_reset(&pacer);return;}
    if (ids[pause_selected] == PAUSE_DIFFICULTY && (pressed & (X6_BTN_LEFT | X6_BTN_RIGHT | X6_BTN_A))) {
        uint32_t next = (pressed & X6_BTN_LEFT) ? (dip_index + dip_count - 1) % dip_count : (dip_index + 1) % dip_count;
        dip_set(next);dip_save(games[current_game].id);pause_note = 1;
        pressed &= ~X6_BTN_A;
    }
    if (ids[pause_selected] == PAUSE_RAPID && (pressed & (X6_BTN_LEFT | X6_BTN_RIGHT | X6_BTN_A))) {
        rapid_rate = (pressed & X6_BTN_LEFT) ? (rapid_rate + 5) % 6 : (rapid_rate + 1) % 6;
        dip_save(games[current_game].id);ui_click();
        pressed &= ~X6_BTN_A;
    }
    if (ids[pause_selected] == PAUSE_RAPID_BUTTONS && (pressed & (X6_BTN_LEFT | X6_BTN_RIGHT | X6_BTN_A))) {
        rapid_buttons = (pressed & X6_BTN_LEFT) ? (rapid_buttons + 4) % 5 : (rapid_buttons + 1) % 5;
        dip_save(games[current_game].id);ui_click();
        pressed &= ~X6_BTN_A;
    }
    if (pressed & X6_BTN_A) {
        uint32_t selected = current_game;
        enum pause_item id = ids[pause_selected];
        if (id == PAUSE_RESUME) {mode = MODE_GAME;x6_pacer_reset(&pacer);return;}
        stop_game();pause_note = 0;
        if (id == PAUSE_RESTART) {
            if (!menu_rom_path(&games[selected]) || start_game(selected)) {
                error = 0;open_menu(selected);x6_menu_notice(&menu, "That game could not be restarted");
                return;
            }
            mode = MODE_GAME;return;
        }
        open_menu(selected);
        struct x6_surface s;
        if (render && surface_for_frame(&s)) x6_menu_draw(&menu, &s);
        return;
    }
    struct x6_surface s;
    if (surface_for_frame(&s)) {
        x6_ui_draw_pause(&s, games[current_game].title, labels, count, pause_selected,
                         pause_note ? "Restart the game to be sure it applies" : 0, pause_dim);
        pause_dim = 0;
    }
}

static int32_t frame_call(void *context)
{
    uint32_t render=*(const uint32_t *)context;
    if (mode == MODE_IDLE || error) return -1;
#if defined(X6_MELIS_RUNTIME) || defined(X6_TIMING_TEST)
    uint32_t now=x6_time_ticks(),wait=x6_pacer_wait(&pacer,now);
    if (wait) {
        x6_time_delay(wait);
        uint32_t after=x6_time_ticks();
        if ((int32_t)(after-now)<(int32_t)wait) {error=8;return -1;}
        now=after;
    }
    x6_pacer_observe(&pacer,now);
    x6_monitor_frame(now,render!=0);
#endif
    switch (mode) {
    case MODE_MENU: frame_menu(render);break;
    case MODE_LOADING: frame_loading(render);break;
    case MODE_GAME: frame_game(render);break;
    case MODE_PAUSE: frame_pause(render);break;
    case MODE_SETTINGS: frame_settings(render);break;
    case MODE_HANDOFF: break; /* Host owns the queued request after this call. */
    default: return -1;
    }
    return error ? -1 : 0;
}
static int32_t frame(uint32_t render) {return guarded(frame_call,&render);}
static int32_t set_buttons(uint32_t player, uint32_t mask)
{
    x6_monitor_input(player,mask);
    if (player >= 5) return -1;
    buttons[player] = mask;return 0;
}
static int32_t unknown(void) {return 0;}
static int32_t unsupported(const char *path) {(void)path;return -1;}
static uint32_t *video_info(uint32_t *width, uint32_t *height, uint32_t *pitch)
{
    if (width) *width = WIDTH;
    if (height) *height = HEIGHT;
    if (pitch) *pitch = WIDTH;
    return last_buffer;
}
static int32_t progress(void) {return mode != MODE_IDLE ? 100 : 0;}
int32_t x6_arcade_last_error(void) {return error;}
const struct x6_emu_interface x6_arcade_interface = {
    load, unload, frame, unknown, set_buttons, unknown,
    unsupported, unsupported, video_info, progress
};

/* Experimental services diagnostic, building on the device-tested display/input
 * path. SD access is read-only. Audio uses the host callback; no flash writes. */
#include "x6_stock_abi.h"
#include "x6_runtime.h"

#define WIDTH 320u
#define HEIGHT 240u
#define FILE_MARKER "X6-RELOADED-FILE-TEST-V1\n"
#define AUDIO_FRAMES_PER_CHUNK 256u
#define AUDIO_CHUNKS 24u


#if defined(__ELF__)
#define MODULE_ENTRY __attribute__((section(".text.entry")))
#define MODULE_MAGIC __attribute__((section("MAGIC"), used))
#else
#define MODULE_ENTRY
#define MODULE_MAGIC
#endif

static uint32_t *(*output_buffer)(void);
static uint32_t *last_buffer;
static uint32_t buttons[5];
static uint32_t frames;
static uint32_t loaded;
static int32_t (*submit_audio)(const void *, uint32_t);
static int16_t audio_chunk[AUDIO_FRAMES_PER_CHUNK * 2];
static uint32_t tone_chunks_left;
static uint32_t tone_phase;
static struct x6_runtime_diagnostic runtime_status;
static uint32_t audio_submitted;

static void send_tone_chunk(void)
{
    if (!submit_audio || !tone_chunks_left)
        return;
    for (uint32_t i = 0; i < AUDIO_FRAMES_PER_CHUNK; ++i) {
        tone_phase += 440;
        if (tone_phase >= 22050)
            tone_phase -= 22050;
        /* About 3% of full-scale PCM; identical samples in both channels. */
        int16_t sample = tone_phase < 11025 ? 1024 : -1024;
        audio_chunk[i * 2] = sample;
        audio_chunk[i * 2 + 1] = sample;
    }
    /* This records submission, not proof that the speaker played it. */
    submit_audio(audio_chunk, sizeof(audio_chunk));
    audio_submitted = 1;
    --tone_chunks_left;
}

static int32_t probe_load(const struct x6_host_callbacks *host, const char *rom_path)
{
    (void)rom_path;
    loaded = 0;
    output_buffer = 0;
    last_buffer = 0;
    frames = 0;
    submit_audio = 0;
    tone_chunks_left = 0;
    tone_phase = 0;
    runtime_status = (struct x6_runtime_diagnostic){0, 0, 0, 0};
    audio_submitted = 0;
    for (uint32_t i = 0; i < 5; ++i)
        buttons[i] = 0;
    if (!host || !host->get_output_buffer)
        return -1;
    output_buffer = host->get_output_buffer;
    submit_audio = host->submit_audio;
    tone_chunks_left = AUDIO_CHUNKS;
    runtime_status = x6_check_runtime();
    loaded = 1;
    return 0;
}

static int32_t probe_unload(void)
{
    loaded = 0;
    output_buffer = 0;
    last_buffer = 0;
    submit_audio = 0;
    tone_chunks_left = 0;
    return 0;
}

/* Minimal original 5x7 labels for the four diagnostic results. */
static void draw_status_labels(uint32_t *pixels)
{
    static const uint8_t glyphs[11][7] = {
        {30,17,17,30,20,18,17}, {31,16,16,30,16,16,31},
        {14,17,17,31,17,17,17}, {30,17,17,17,17,17,30},
        {15,16,16,14,1,1,30}, {17,18,20,24,20,18,17},
        {17,17,17,31,17,17,17}, {30,17,17,30,16,16,16},
        {14,17,16,23,17,17,14}, {14,17,17,17,17,17,14},
        {17,17,17,21,21,21,10}
    };
    static const uint8_t words[4][4] = {{0,1,2,3}, {4,1,1,5}, {6,1,2,7}, {8,0,9,10}};
    for (uint32_t cell = 0; cell < 4; ++cell) {
        for (uint32_t y = 144; y < 160; ++y) pixels[y * WIDTH + cell * 80] = 0xff000000u;
        for (uint32_t letter = 0; letter < 4; ++letter)
            for (uint32_t row = 0; row < 7; ++row)
                for (uint32_t column = 0; column < 5; ++column)
                    if (glyphs[words[cell][letter]][row] & (1u << (4 - column)))
                        for (uint32_t dy = 0; dy < 2; ++dy)
                            for (uint32_t dx = 0; dx < 2; ++dx)
                                pixels[(145 + row * 2 + dy) * WIDTH + cell * 80 + 16 + letter * 12 + column * 2 + dx] = 0xff000000u;
    }
}

static int32_t probe_frame(uint32_t render)
{
    static const uint32_t colors[8] = {
        0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u,
        0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u
    };
    if (!loaded)
        return -1;
    ++frames;
    send_tone_chunk();
    if (!render)
        return 0;
    uint32_t *pixels = output_buffer();
    if (!pixels)
        return -1;
    last_buffer = pixels;
    for (uint32_t y = 0; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            uint32_t color;
            if (y < 136)
                color = colors[x / 40];
            else if (y < 144)
                color = audio_submitted ? 0xff0080ffu : 0xff202020u;
            else if (y < 160)
                color = (x < 80 ? runtime_status.read_ok :
                         x < 160 ? runtime_status.seek_ok :
                         x < 240 ? runtime_status.heap_ok : runtime_status.grow_ok)
                      ? 0xff00ff00u : 0xffff0000u;
            else if (y < 216) {
                uint32_t bit = x / 24;
                color = bit < 12 && (buttons[0] & (1u << bit))
                      ? 0xff00ff00u : 0xff202020u;
            } else {
                uint32_t bit = x / 12;
                color = bit < 24 && (frames & (1u << bit))
                      ? 0xffffffffu : 0xff000000u;
            }
            pixels[y * WIDTH + x] = color;
        }
    }
    draw_status_labels(pixels);
    return 0;
}

static int32_t probe_unknown(void) { return 0; }

static int32_t probe_buttons(uint32_t player, uint32_t mask)
{
    if (player >= 5)
        return -1;
    buttons[player] = mask;
    return 0;
}

static int32_t probe_state_unsupported(const char *path)
{
    (void)path;
    return -1;
}

static uint32_t *probe_video(uint32_t *width, uint32_t *height, uint32_t *pitch)
{
    if (width) *width = WIDTH;
    if (height) *height = HEIGHT;
    if (pitch) *pitch = WIDTH;
    return last_buffer;
}

static int32_t probe_progress(void) { return loaded ? 100 : 0; }

const struct x6_emu_interface x6_probe_interface = {
    probe_load, probe_unload, probe_frame, probe_unknown, probe_buttons,
    probe_unknown, probe_state_unsupported, probe_state_unsupported,
    probe_video, probe_progress
};

static struct x6_module_handle module_handle = {0, 0, 0, &x6_probe_interface};

MODULE_ENTRY
struct x6_module_handle *x6_module_open(uint32_t module_id, uint32_t mode)
{
    module_handle.module_id = (uint8_t)module_id;
    module_handle.mode = (uint8_t)mode;
    return &module_handle;
}

static int32_t module_init(void) { return 0; }
static int32_t module_exit(void) { return probe_unload(); }
static int32_t module_close(struct x6_module_handle *handle)
{
    (void)handle;
    return 0;
}
static uint32_t module_read(void *p, uint32_t size, uint32_t count, void *handle)
{
    (void)p; (void)handle;
    return size * count;
}
static uint32_t module_write(const void *p, uint32_t size, uint32_t count, void *handle)
{
    (void)p; (void)handle;
    return size * count;
}
static int32_t module_ioctl(void *handle, uint32_t command, uint32_t arg, void *buffer)
{
    (void)handle; (void)command; (void)arg; (void)buffer;
    return 0;
}

MODULE_MAGIC
const struct x6_module_magic x6_probe_magic = {
    {'e','P','D','K','.','m','o','d'},
    {0x01000000u, 0x91u, 0x004f0000u, 0x400u},
    module_init, module_exit, x6_module_open, module_close,
    module_read, module_write, module_ioctl
};

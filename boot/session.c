#include "session.h"

int32_t x6_session_stop(struct x6_session *s)
{
    if (!s || !s->id) return 0;
    if (s->load_called) {
        s->load_called = 0;
        s->emu->unload();
    }
    s->emu = 0; /* Never call a table after close or uninstall. */
    if (s->handle) {
        if (s->ops->close(s->handle) != 0) return -1;
        s->handle = 0;
    }
    if (s->ops->uninstall(s->id) != 0) return -1;
    *s = (struct x6_session){0};
    return 0;
}

int32_t x6_session_start(struct x6_session *s, const struct x6_module_ops *ops,
                         const char *module, const struct x6_host_callbacks *host,
                         const char *game)
{
    if (!s || s->id || !ops || !ops->install || !ops->open || !ops->interface
        || !ops->close || !ops->uninstall || !module || !game || !host) return -1;
    int32_t id = ops->install(module);
    if (id <= 0 || id > 255) return -1;
    s->ops = ops;s->id = (uint32_t)id;
    s->handle = ops->open(s->id);
    if (!s->handle) goto fail;
    s->emu = ops->interface(s->handle);
    if (!s->emu || !s->emu->load || !s->emu->unload || !s->emu->frame
        || !s->emu->set_buttons || !s->emu->video_info) goto fail;
    s->load_called = 1;
    if (s->emu->load(host, game) != 0) goto fail;
    return 0;
fail:
    x6_session_stop(s);
    return -1;
}

uint32_t x6_stock_geometry(uint32_t w, uint32_t h, uint32_t pitch)
{
    return w && h && w <= X6_STOCK_MAX_SIDE && h <= X6_STOCK_MAX_SIDE
        && pitch >= w && pitch <= X6_STOCK_MAX_SIDE;
}

int32_t x6_stock_blit(uint32_t *out, const uint32_t *in,
                       uint32_t w, uint32_t h, uint32_t pitch)
{
    if (!out || !in || !x6_stock_geometry(w, h, pitch)) return -1;
    /* Build the horizontal source-index map once per picture instead of
     * recomputing the quotient for every output pixel. */
    uint16_t columns[320];
    for (uint32_t x = 0; x < 320; ++x) columns[x] = (uint16_t)(x * w / 320);
    for (uint32_t y = 0; y < 240; ++y) {
        const uint32_t *row = in + (y * h / 240) * pitch;
        for (uint32_t x = 0; x < 320; ++x) out[y * 320 + x] = row[columns[x]];
    }
    return 0;
}

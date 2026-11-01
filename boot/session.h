#ifndef X6_SESSION_H
#define X6_SESSION_H
#include "x6_stock_abi.h"

/* One fixed-address emulator may be installed at a time. OS operations are
 * supplied by x6menu; native tests exercise this same lifecycle code. */
struct x6_module_ops {
    int32_t (*install)(const char *path);
    uintptr_t (*open)(uint32_t id);
    const struct x6_emu_interface *(*interface)(uintptr_t handle);
    int32_t (*close)(uintptr_t handle);
    int32_t (*uninstall)(uint32_t id);
};
struct x6_session {
    const struct x6_module_ops *ops;
    const struct x6_emu_interface *emu;
    uintptr_t handle;
    uint32_t id, load_called;
};
int32_t x6_session_start(struct x6_session *s, const struct x6_module_ops *ops,
                         const char *module, const struct x6_host_callbacks *host,
                         const char *game);
/* A failed close/uninstall keeps ownership recorded and prevents replacement.
 * The stock FBA unload returns -1 on its normal path; that result is not a
 * module-manager close/uninstall failure. */
int32_t x6_session_stop(struct x6_session *s);

/* Stock FBA draws its native dimensions, not MAME's 320x240 menu surface. */
#define X6_STOCK_MAX_SIDE 512u
uint32_t x6_stock_geometry(uint32_t width, uint32_t height, uint32_t pitch);
int32_t x6_stock_blit(uint32_t *out, const uint32_t *in,
                       uint32_t width, uint32_t height, uint32_t pitch);
#endif

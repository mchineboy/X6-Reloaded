#ifndef X6_STOCK_ABI_H
#define X6_STOCK_ABI_H

#include <stdint.h>
#include <stddef.h>

/* Provisional ABI recovered from the exact V1.241017 launcher + SD fba.mod.
 * These types are hypotheses supported by static call sites, not an SDK.
 * No OS imports, direct device access, or libc dependency is defined here. */
struct x6_host_callbacks {
    int32_t (*submit_audio)(const void *pcm, uint32_t byte_count);
    uint32_t *(*get_output_buffer)(void);
    char resource_path[256];
};

struct x6_emu_interface {
    int32_t (*load)(const struct x6_host_callbacks *host, const char *rom_path);
    int32_t (*unload)(void);
    int32_t (*frame)(uint32_t render);
    int32_t (*unknown_0c)(void);
    int32_t (*set_buttons)(uint32_t player, uint32_t mask);
    int32_t (*unknown_14)(void);
    int32_t (*save_state)(const char *path);
    int32_t (*load_state)(const char *path);
    uint32_t *(*video_info)(uint32_t *width, uint32_t *height, uint32_t *pitch_pixels);
    int32_t (*load_progress)(void);
};

struct x6_module_handle {
    uint8_t module_id;
    uint8_t mode;
    uint16_t reserved;
    const struct x6_emu_interface *emulator;
};

struct x6_module_magic {
    char signature[8];
    uint32_t opaque_header[4];
    int32_t (*init)(void);
    int32_t (*exit)(void);
    struct x6_module_handle *(*open)(uint32_t module_id, uint32_t mode);
    int32_t (*close)(struct x6_module_handle *handle);
    uint32_t (*read)(void *ptr, uint32_t size, uint32_t count, void *handle);
    uint32_t (*write)(const void *ptr, uint32_t size, uint32_t count, void *handle);
    int32_t (*ioctl)(void *handle, uint32_t command, uint32_t argument, void *buffer);
};

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(struct x6_host_callbacks) == 264, "host argument layout");
_Static_assert(sizeof(struct x6_emu_interface) == 40, "ten interface words");
_Static_assert(offsetof(struct x6_module_handle, emulator) == 4, "handle bridge");
_Static_assert(sizeof(struct x6_module_handle) == 8, "handle layout");
_Static_assert(sizeof(struct x6_module_magic) == 52, "Melis MAGIC layout");
#endif

extern const struct x6_emu_interface x6_probe_interface;
struct x6_module_handle *x6_module_open(uint32_t module_id, uint32_t mode);

#endif

/* Stock-compatible module envelope; module read/write slots mirror the stock
 * template. They are not the file I/O implementation. */
#include "x6_arcade.h"
#if defined(__ELF__)
#define MODULE_ENTRY __attribute__((section(".text.entry")))
#define MODULE_MAGIC __attribute__((section("MAGIC"), used))
#else
#define MODULE_ENTRY
#define MODULE_MAGIC
#endif
static struct x6_module_handle module_handle = {0, 0, 0, &x6_arcade_interface};

MODULE_ENTRY
struct x6_module_handle *x6_module_open(uint32_t module_id, uint32_t mode)
{
    module_handle.module_id = (uint8_t)module_id;
    module_handle.mode = (uint8_t)mode;
    return &module_handle;
}

static int32_t module_init(void) { return 0; }
static int32_t module_exit(void) { return x6_arcade_interface.unload(); }
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
const struct x6_module_magic x6_arcade_magic = {
    {'e','P','D','K','.','m','o','d'},
    {0x01000000u, 0x91u, 0x004f0000u, 0x400u},
    module_init, module_exit, x6_module_open, module_close,
    module_read, module_write, module_ioctl
};

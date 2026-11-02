#ifndef X6_HOST_SERVICES_H
#define X6_HOST_SERVICES_H
#include <stdint.h>
#include "x6_directory.h"
#include "x6_library.h"
struct x6_game;

/* X6HOST1 resource_path extension. Versions 2/3 preserve the older prefixes.
 * Requests only queue work: the host switches modules AFTER frame() returns. */
struct x6_host_services {
    uint32_t version;
    void (*set_brightness)(uint32_t level);
    void (*set_idle)(uint32_t blank_seconds, uint32_t off_seconds);
    uint32_t (*stock_available)(void);                  /* version >= 2 */
    int32_t (*request_stock)(void);                    /* version >= 2 */
    uint32_t (*restore_library)(void);                 /* version >= 3: 0=root, 2=stock */
    uint32_t (*stock_count)(void);                     /* version >= 4 */
    int32_t (*stock_metadata)(uint32_t index, struct x6_game *out);
    int32_t (*request_stock_game)(uint32_t index);
    uint32_t (*restore_stock_game)(void);
    x6_directory_walker walk_directory;               /* version >= 5; read-only */
    const struct x6_library_provider *libraries;       /* version >= 6; paged */
    int32_t (*request_library_game)(uint32_t library, uint32_t index);
    uint32_t (*library_status)(void);                 /* nonzero: incomplete scan */
    void (*resolve_title)(struct x6_game *, uint32_t library);
};
#endif

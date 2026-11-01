/* Exposes MAME 2000's compiled driver table to the catalogue.
 * Compiled with the MAME include paths; only the module build links it. */
#include "driver.h"
#include "x6_catalog.h"

#define X6_IMPERFECT_FLAGS (GAME_NOT_WORKING | GAME_WRONG_COLORS | GAME_IMPERFECT_COLORS \
    | GAME_NO_SOUND | GAME_IMPERFECT_SOUND | GAME_UNEMULATED_PROTECTION)

uint32_t x6_core_driver_count(void)
{
    uint32_t n = 0;
    while (drivers[n]) ++n;
    return n;
}

uint32_t x6_core_driver(uint32_t index, struct x6_driver_info *out)
{
    if (!out || index >= x6_core_driver_count()) return 0;
    const struct GameDriver *d = drivers[index];
    if (d->flags & NOT_A_DRIVER) return 0;
    out->name = d->name;out->description = d->description;
    out->year = d->year;out->maker = d->manufacturer;
    out->vertical = (d->flags & ORIENTATION_SWAP_XY) != 0;
    out->imperfect = (d->flags & X6_IMPERFECT_FLAGS) != 0;
    out->clone = d->clone_of && !(d->clone_of->flags & NOT_A_DRIVER);
    return 1;
}

#ifndef X6_STOCK_CATALOG_H
#define X6_STOCK_CATALOG_H
#include "x6_ui.h"

/* Primary drop folder plus existing stock-card locations. No writes. */
uint32_t x6_stock_catalog_build(struct x6_game *out, uint32_t capacity);
uint32_t x6_stock_catalog_legacy(const struct x6_game *games, uint32_t count);
#endif

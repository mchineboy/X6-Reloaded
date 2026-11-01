#ifndef X6_ARCADE_H
#define X6_ARCADE_H
#include "x6_stock_abi.h"
extern const struct x6_emu_interface x6_arcade_interface;
/* Diagnostic code, not a user-facing menu/API. Zero means no observed error. */
int32_t x6_arcade_last_error(void);
#endif

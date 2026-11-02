#ifndef X6_TITLES_H
#define X6_TITLES_H
#include "x6_library.h"
/* Optional, editable TSVs; no ROM bytes or vendor title data in the executable. */
uint32_t x6_titles_load(void);
void x6_titles_apply(struct x6_game *game, uint32_t library);
void x6_titles_filename(struct x6_game *game, const char *name);
uint32_t x6_titles_console(struct x6_game *game, uint32_t library, uint32_t size);
#endif

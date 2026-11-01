#ifndef X6_MUSIC_H
#define X6_MUSIC_H
#include <stdint.h>
/* Original menu tunes and UI click, synthesised at 22050 Hz stereo s16. */
enum x6_tune { X6_TUNE_OFF, X6_TUNE_CHIP, X6_TUNE_TECHNO, X6_TUNE_ROCK, X6_TUNE_COUNT };
void x6_music_reset(void);
void x6_music_click(void);
/* Render interleaved stereo frames; tune OFF still renders pending clicks. */
void x6_music_render(int16_t *out, uint32_t frames, uint32_t tune);
#endif

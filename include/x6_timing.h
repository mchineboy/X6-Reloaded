#ifndef X6_TIMING_H
#define X6_TIMING_H
#include <stdint.h>
/* This baseline uses a 100 Hz Melis tick. Hardware calibration is pending. */
struct x6_pacer {
    uint32_t active, deadline, fraction, epoch, window_frames, fps_tenths;
    uint32_t hz; /* target calls per second; 0 means 60 */
};
void x6_pacer_reset(struct x6_pacer *pacer);
/* Games whose hardware refresh is below 59 Hz (Irem M62 55 Hz, Burger Time 57 Hz). */
void x6_pacer_set_rate(struct x6_pacer *pacer, uint32_t hz);
uint32_t x6_pacer_wait(struct x6_pacer *pacer, uint32_t now);
void x6_pacer_observe(struct x6_pacer *pacer, uint32_t now);
uint32_t x6_time_ticks(void);
void x6_time_delay(uint32_t ticks);
#endif

#ifndef X6_MONITOR_H
#define X6_MONITOR_H
#include <stdint.h>
struct x6_input_monitor {
    uint32_t masks[5], events, last_player, last_nonzero;
};
struct x6_frame_delivery {
    uint32_t epoch,active,last_tick,window_calls,window_pictures,window_host_skips,window_duplicates,window_gap;
    uint32_t pictures_tenths,host_skips_tenths,duplicates_tenths,max_gap_ticks;
#ifdef X6_TIMING_CALIBRATION
    uint32_t start_tick,elapsed_ticks;
#endif
};
void x6_monitor_frame(uint32_t ticks,uint32_t requested);
void x6_monitor_video(uint32_t available,uint32_t copied);
const struct x6_frame_delivery *x6_monitor_delivery(void);
void x6_monitor_reset(void);
void x6_monitor_input(uint32_t player,uint32_t mask);
const volatile struct x6_input_monitor *x6_monitor_status(void);
void x6_monitor_draw_fps(uint32_t *pixels,uint32_t fps_tenths);
void x6_monitor_draw(uint32_t *pixels,uint32_t fps_tenths);
#endif

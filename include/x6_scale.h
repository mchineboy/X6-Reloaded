#ifndef X6_SCALE_H
#define X6_SCALE_H
#include <stdint.h>
#define X6_SCALE_TAPS 4u
struct x6_scale_span {uint16_t first,count,weight[X6_SCALE_TAPS];};
struct x6_area_scaler {
    uint32_t width,height,out_width,out_height;
    struct x6_scale_span x[320],y[240];
    uint32_t rows[X6_SCALE_TAPS][320];
};
/* RGB565 source, byte pitch; ARGB destination, pixel stride. No allocation.
 * At most four source pixels overlap each destination pixel on either axis. */
uint32_t x6_area_prepare(struct x6_area_scaler *,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t x6_area_render(struct x6_area_scaler *,const uint16_t *,uint32_t,
                        uint32_t *,uint32_t,const uint32_t *);
#endif

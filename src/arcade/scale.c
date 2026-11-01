/* Separable pixel-area downsampling. Axis plans normalize to exactly 256;
 * the render loop uses bounded integer weights and contains no division. */
#include <stdint.h>
#include <stddef.h>
#include "x6_scale.h"
static uint32_t axis(struct x6_scale_span *plan,uint32_t source,uint32_t target) {
    for(uint32_t out=0;out<target;++out) {
        uint32_t start=out*source,end=start+source;
        uint32_t first=start/target,last=(end-1u)/target;
        if(last-first>=X6_SCALE_TAPS)return 0;
        struct x6_scale_span *p=plan+out;
        p->first=(uint16_t)first;p->count=(uint16_t)(last-first+1u);
        uint32_t used=0,previous=0;
        for(uint32_t tap=0;tap<p->count;++tap) {
            uint32_t left=(first+tap)*target,right=left+target;
            if(left<start)left=start;if(right>end)right=end;
            used+=right-left;
            uint32_t cumulative=(used*256u+source/2u)/source;
            p->weight[tap]=(uint16_t)(cumulative-previous);previous=cumulative;
        }
    }
    return 1;
}
uint32_t x6_area_prepare(struct x6_area_scaler *s,uint32_t w,uint32_t h,uint32_t ow,uint32_t oh) {
    if(!s)return 0;s->out_width=s->out_height=0;
    if(!w || !h || w>512 || h>512 || !ow || ow>320 || !oh || oh>240)return 0;
    if(!axis(s->x,w,ow) || !axis(s->y,h,oh))return 0;
    s->width=w;s->height=h;s->out_width=ow;s->out_height=oh;return 1;
}
static uint32_t color(uint32_t rb,uint32_t g) {
    /* Each packed lane sums to <=255*256; rounding cannot cross lanes. */
    return 0xff000000u | (((rb+0x00800080u)>>8)&0x00ff00ffu) | ((g+128u)&0x0000ff00u);
}
uint32_t x6_area_render(struct x6_area_scaler *s,const uint16_t *source,uint32_t pitch,
                        uint32_t *destination,uint32_t stride,const uint32_t *colors) {
    if(!s || !source || !destination || !colors || !s->out_width || !s->out_height
        || pitch<s->width*2u || (pitch&1u) || ((uintptr_t)source&1u) || stride<s->out_width)return 0;
    uint32_t cached[X6_SCALE_TAPS]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    for(uint32_t y=0;y<s->out_height;++y) {
        const struct x6_scale_span *yp=s->y+y;
        for(uint32_t tap=0;tap<yp->count;++tap) {
            uint32_t sy=yp->first+tap,slot=sy%X6_SCALE_TAPS;
            if(cached[slot]==sy)continue;
            const uint16_t *row=(const uint16_t *)((const uint8_t *)source+sy*pitch);
            for(uint32_t x=0;x<s->out_width;++x) {
                const struct x6_scale_span *xp=s->x+x;uint32_t rb=0,g=0;
                for(uint32_t xt=0;xt<xp->count;++xt) {
                    uint32_t c=colors[row[xp->first+xt]],weight=xp->weight[xt];
                    rb+=(c&0x00ff00ffu)*weight;g+=((c>>8)&255u)*weight;
                }
                s->rows[slot][x]=color(rb,g);
            }
            cached[slot]=sy;
        }
        uint32_t *out=destination+y*stride;
        for(uint32_t x=0;x<s->out_width;++x) {
            uint32_t rb=0,g=0;
            for(uint32_t tap=0;tap<yp->count;++tap) {
                uint32_t c=s->rows[(yp->first+tap)%X6_SCALE_TAPS][x],weight=yp->weight[tap];
                rb+=(c&0x00ff00ffu)*weight;g+=((c>>8)&255u)*weight;
            }
            out[x]=color(rb,g);
        }
    }
    return 1;
}

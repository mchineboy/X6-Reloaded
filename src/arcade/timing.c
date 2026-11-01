/* Rational deadlines, including time already spent in core/audio/display.
 * No fixed per-frame sleep, busy wait, or unbounded catch-up loop. */
#include "x6_timing.h"
void x6_pacer_reset(struct x6_pacer *p) {*p=(struct x6_pacer){0};}
void x6_pacer_set_rate(struct x6_pacer *p,uint32_t hz) {p->hz=hz>=50 && hz<=60?hz:60;p->fraction=0;}
uint32_t x6_pacer_wait(struct x6_pacer *p,uint32_t now)
{
    if (!p->active) {
        p->active=1;p->deadline=now;p->epoch=now;p->window_frames=0;
        return 0;
    }
    uint32_t hz=p->hz?p->hz:60;
    p->fraction+=100;
    p->deadline+=p->fraction/hz;p->fraction%=hz;
    int32_t ahead=(int32_t)(p->deadline-now);
    if (ahead>2 || ahead < -10) {
        p->deadline=now;p->fraction=0;return 0;
    }
    return ahead>0 ? (uint32_t)ahead : 0;
}
void x6_pacer_observe(struct x6_pacer *p,uint32_t now)
{
    uint32_t elapsed=now-p->epoch;
    if (elapsed>=100) {
        p->fps_tenths=elapsed<=10000 && p->window_frames<=100000
                     ? p->window_frames*1000/elapsed : 0;
        p->epoch=now;p->window_frames=0;
    }
    ++p->window_frames;
}

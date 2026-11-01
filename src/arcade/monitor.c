/* Read-only input observer; it does not invent or remap key events. */
#include "x6_monitor.h"
/* Stock can push input outside the frame call; display reads are snapshots. */
static volatile struct x6_input_monitor state;
static struct x6_frame_delivery delivery;
void x6_monitor_reset(void) {state=(struct x6_input_monitor){0};delivery=(struct x6_frame_delivery){0};}
const struct x6_frame_delivery *x6_monitor_delivery(void) {return &delivery;}
void x6_monitor_frame(uint32_t ticks,uint32_t requested) {
    if(!delivery.active) {
        delivery.active=1;delivery.epoch=ticks;delivery.last_tick=ticks;
#ifdef X6_TIMING_CALIBRATION
        delivery.start_tick=ticks;
#endif
    }
#ifdef X6_TIMING_CALIBRATION
    delivery.elapsed_ticks=ticks-delivery.start_tick;
#endif
    uint32_t gap=ticks-delivery.last_tick;delivery.last_tick=ticks;
    if(gap>delivery.window_gap)delivery.window_gap=gap;
    uint32_t elapsed=ticks-delivery.epoch;
    if(elapsed>=100) {
        delivery.pictures_tenths=elapsed<=10000?delivery.window_pictures*1000/elapsed:0;
        delivery.host_skips_tenths=elapsed<=10000?delivery.window_host_skips*1000/elapsed:0;
        delivery.duplicates_tenths=elapsed<=10000?delivery.window_duplicates*1000/elapsed:0;
        delivery.max_gap_ticks=delivery.window_gap;
        delivery.epoch=ticks;delivery.window_calls=delivery.window_pictures=delivery.window_host_skips=delivery.window_duplicates=delivery.window_gap=0;
    }
    ++delivery.window_calls;if(!requested)++delivery.window_host_skips;
}
void x6_monitor_video(uint32_t available,uint32_t copied) {
    if(!available)++delivery.window_duplicates;
    if(copied)++delivery.window_pictures;
}
void x6_monitor_input(uint32_t player,uint32_t mask)
{
    ++state.events;
    if (player<5) state.masks[player]=mask;
    if (mask) {state.last_player=player;state.last_nonzero=mask;}
}
const volatile struct x6_input_monitor *x6_monitor_status(void) {return &state;}
/* Original 5x7 uppercase/digit glyphs, packed one row per byte. */
static const uint8_t glyphs[26][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
 {31,16,16,30,16,16,16},{30,17,17,30,16,16,16},{15,16,16,14,1,1,30},
 {16,16,16,16,16,16,31},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{0,0,0,0,0,12,12},{17,25,25,21,19,19,17},
 {17,17,17,31,17,17,17},{14,17,16,23,17,17,15}
};
static const char alphabet[]="0123456789ABCDEFPSLTUV.NHG";
static void text(uint32_t *p,uint32_t x,uint32_t y,const char *s)
{
    for (;*s && x+5<=320;++s,x+=6) {
        uint32_t g=0;while (alphabet[g] && alphabet[g]!=*s) ++g;
        if (!alphabet[g]) continue;
        for (uint32_t row=0;row<7 && y+row<240;++row)
            for (uint32_t col=0;col<5;++col)
                if (glyphs[g][row]&(1u<<(4-col))) p[(y+row)*320+x+col]=0xff00ff80u;
    }
}
static void hex(uint32_t *p,uint32_t x,uint32_t y,uint32_t n)
{
    char s[9];for (uint32_t i=0;i<8;++i) s[i]="0123456789ABCDEF"[(n>>((7-i)*4))&15];
    s[8]=0;text(p,x,y,s);
}
static void delivery_text(uint32_t *,uint32_t,uint32_t,uint32_t);
void x6_monitor_draw(uint32_t *p,uint32_t fps)
{
    if (!p) return;
    /* Clear only sidebars; the centered 180x240 game image is preserved. */
    for (uint32_t y=0;y<240;++y) for (uint32_t x=0;x<70;++x) {
        p[y*320+x]=0xff000000u;p[y*320+250+x]=0xff000000u;
    }
    for (uint32_t i=0;i<5;++i) {
        char label[3]={'P',(char)('0'+i),0};
        text(p,8,12+i*32,label);hex(p,8,23+i*32,state.masks[i]);
    }
    text(p,258,12,"LAST P");hex(p,258,23,state.last_player);
    text(p,258,44,"LAST");hex(p,258,55,state.last_nonzero);
    text(p,258,76,"EVENTS");hex(p,258,87,state.events);
    delivery_text(p,258,108,fps);
}

static void rate(uint32_t *p,uint32_t x,uint32_t y,char label,uint32_t value) {
    char s[7]={label,' ',(char)('0'+((value/100)%10)),(char)('0'+((value/10)%10)),'.',(char)('0'+value%10),0};
    text(p,x,y,s);
}
/* E emulated calls; V copied pictures; H host omissions; D core duplicates;
 * G maximum call gap in assumed milliseconds. No LCD-present API is known. */
static void delivery_text(uint32_t *p,uint32_t x,uint32_t y,uint32_t fps) {
    rate(p,x,y,'E',fps);rate(p,x,y+9,'V',delivery.pictures_tenths);
    rate(p,x,y+18,'H',delivery.host_skips_tenths);rate(p,x,y+27,'D',delivery.duplicates_tenths);
    uint32_t ms=delivery.max_gap_ticks>99?999:delivery.max_gap_ticks*10;
    char gap[6]={'G',' ',(char)('0'+ms/100),(char)('0'+ms/10%10),(char)('0'+ms%10),0};text(p,x,y+36,gap);
#ifdef X6_TIMING_CALIBRATION
    /* Nominal seconds, deliberately using the unverified 100 Hz conversion.
     * Compare this independent tick total with an external stopwatch. */
    uint32_t t=delivery.elapsed_ticks/10u;if(t>99999u)t=99999u;
    char elapsed[9]={'T',' ',(char)('0'+t/10000u),(char)('0'+t/1000u%10u),
        (char)('0'+t/100u%10u),(char)('0'+t/10u%10u),'.',(char)('0'+t%10u),0};
    text(p,x,y+45,elapsed);
#endif
}
/* Temporary landscape diagnostic: top-right 50x47, or 50x56 with timer. */
#ifdef X6_TIMING_CALIBRATION
#define MONITOR_HEIGHT 56u
#else
#define MONITOR_HEIGHT 47u
#endif
void x6_monitor_draw_fps(uint32_t *p,uint32_t fps)
{
    if (!p) return;
    for(uint32_t y=0;y<MONITOR_HEIGHT;++y)for(uint32_t x=270;x<320;++x)p[y*320+x]=0xff000000u;
    delivery_text(p,272,2,fps);
}

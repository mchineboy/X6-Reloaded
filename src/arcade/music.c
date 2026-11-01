/* Menu music and UI clicks for the X6 arcade frontend. All tunes are original and
 * synthesised in integer arithmetic: 22050 Hz stereo s16, peaks around -26 dBFS.
 *   1 Chiptune: square lead + triangle bass, Am-F-C-G, 132 BPM
 *   2 Techno:   16-bar cold electro: syncopated pulse bass, layered arps/lead/drums, Dm Dm Bb C, 128 BPM
 *   3 Rock:     16-bar anthem: square fanfare intro, doubled-lead chorus, pitch-bend solo, 132 BPM */
#include <stdint.h>
#include "x6_music.h"

#define RATE 22050u

static uint32_t note_inc(int32_t midi)
{
    /* phase increment = f * 2^32 / rate, f = 440 * 2^((midi-69)/12) */
    static const uint32_t semitone_q16[12] = {65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715};
    int32_t n = midi - 69, octave = 0;
    while (n < 0) {n += 12;--octave;}
    while (n >= 12) {n -= 12;++octave;}
    uint64_t f_q16 = (uint64_t)440u * semitone_q16[n];
    if (octave >= 0) f_q16 <<= octave; else f_q16 >>= -octave;
    return (uint32_t)((f_q16 << 16) / RATE);
}

/* shared voice state */
static uint32_t pos, step, fade;
static uint32_t lead_phase, lead_inc, lead_env;
static uint32_t bass_phase, bass_inc, bass_env;
static uint32_t arp_phase, arp_inc, arp_env;
static uint32_t gtr_phase[3], gtr_inc[3], gtr_env;
static uint32_t kick_phase, kick_inc, kick_env;
static uint32_t snare_env, hat_env, hat_open, hat_prev, noise = 0xace1u;
static uint32_t click_phase, click_env;
static uint32_t lead_target, lead_hold, vib_phase, low_phase;

void x6_music_reset(void)
{
    pos = step = fade = 0;
    lead_phase = lead_inc = lead_env = bass_phase = bass_inc = bass_env = 0;
    arp_phase = arp_inc = arp_env = gtr_env = 0;
    lead_target = lead_hold = vib_phase = low_phase = 0;
    for (int i = 0; i < 3; ++i) gtr_phase[i] = gtr_inc[i] = 0;
    kick_phase = kick_inc = kick_env = snare_env = hat_env = hat_open = 0;
}

void x6_music_click(void) {click_env = 4096;click_phase = 0;}

static int32_t tri(uint32_t phase)
{
    int32_t t = (int32_t)(phase >> 16);
    return t < 32768 ? t - 16384 : 49151 - t;          /* -16384..16383 */
}

static int32_t noise_next(void)
{
    noise = (noise >> 1) ^ (-(int32_t)(noise & 1u) & 0xb400u);
    return (int32_t)(noise & 0xffffu) - 32768;
}

static int32_t drums(void)
{
    int32_t v = 0, n = noise_next();
    if (kick_env) {
        kick_phase += kick_inc;if (kick_inc > 10000000u) kick_inc -= kick_inc >> 9;   /* ~165 Hz -> ~50 Hz drop */
        v += tri(kick_phase) * (int32_t)kick_env / 4096 * 1400 / 16384;
        kick_env = kick_env > 3 ? kick_env - 3 : 0;
    }
    if (snare_env) {v += n * (int32_t)snare_env / 4096 * 500 / 32768;snare_env = snare_env > 2 ? snare_env - 2 : 0;}
    if (hat_env) {
        int32_t hp = n - (int32_t)hat_prev;hat_prev = (uint32_t)n;
        v += hp * (int32_t)hat_env / 4096 * 160 / 65536;
        uint32_t d = hat_open ? 2u : 6u;hat_env = hat_env > d ? hat_env - d : 0;
    }
    return v;
}
static void kick(void) {kick_env = 4096;kick_inc = note_inc(52);kick_phase = 0;}

/* ---- 1: chiptune ---- */
#define CHIP_STEP 2506u
static const int8_t chip_lead[64] = {
    69,0,72,0,76,0,72,0, 81,-1,79,-1,76,0,72,0,
    65,0,69,0,72,0,69,0, 77,-1,76,-1,72,0,69,0,
    67,0,72,0,76,0,72,0, 79,-1,77,-1,76,-1,74,0,
    71,0,74,0,79,0,74,0, 83,-1,81,-1,79,-1,-1,0};
static const int8_t chip_bass[4][2] = {{45,57},{41,53},{48,60},{43,55}};
static int32_t chip_sample(void)
{
    if (pos == 0) {
        int32_t lead = chip_lead[step];
        if (lead > 0) {lead_inc = note_inc(lead);lead_env = 4096;}
        else if (lead == 0) lead_env = 0;
        bass_inc = note_inc(chip_bass[step / 16][(step / 2) & 1]);
    }
    if (++pos >= CHIP_STEP) {pos = 0;step = (step + 1) & 63u;}
    lead_phase += lead_inc;bass_phase += bass_inc;
    int32_t sq = (lead_phase >> 30) == 0 ? 1 : -1;     /* 25% duty */
    if (lead_env > 1600) lead_env -= 1;
    return sq * (int32_t)(lead_env * 1050 / 4096) + tri(bass_phase) * 1350 / 16384;   /* level-matched to the other tunes */
}

/* ---- 2: techno ---- original 16-bar loop at 128 BPM (~30 s), cold and robotic.
 * Hypnotic syncopated pulse-wave bass on every bar (Dm Dm Bb C), layers enter and drop out:
 *   bars 0-3   kick + short hats, bass from bar 2
 *   bars 4-7   + crisp noise snare + fast arpeggios
 *   bars 8-11  + sparse minor-key lead phrases
 *   bars 12-13 drop: kick and snare out, bass + arps + lead hold the tension
 *   bars 14-15 everything back, snare fill into the loop */
#define TECHNO_STEP 2584u
static const int8_t techno_root[4] = {38, 38, 34, 36};                 /* D2 D2 Bb1 C2 */
static const uint8_t techno_minor[4] = {1, 1, 0, 0};
/* bass per 16th: semitone offset from root + 1, 0 = rest (syncopated, same every bar) */
static const int8_t techno_bass[16] = {1,0,13,1, 0,1,13,0, 1,0,1,13, 0,13,1,11};
#define R 0
#define H -1
static const int8_t techno_lead[64] = {                                   /* bars 8-11 (and 12-13 repeat 8-9) */
    74,H,H,H, R,R,R,R, 77,H,81,H, H,H,R,R,
    79,H,H,H, H,H,R,R, 77,H,76,H, 74,H,R,R,
    74,H,H,H, R,R,70,H, H,H,R,R, 72,H,H,H,
    76,H,H,H, 79,H,H,H, 76,H,H,H, H,H,R,R};
#undef R
#undef H
static int32_t techno_sample(void)
{
    if (pos == 0) {
        uint32_t bar = step / 16, s16 = step & 15u, chord = bar & 3u;
        int32_t root = techno_root[chord], third = techno_minor[chord] ? 3 : 4;
        uint32_t drop = bar == 12 || bar == 13;
        if (!drop && (s16 & 3u) == 0) kick();
        if (bar >= 4 && !drop && (s16 == 4 || s16 == 12 || (bar == 15 && s16 >= 12))) snare_env = 3000;
        if ((s16 & 3u) == 2 || (bar >= 4 && (s16 == 3 || s16 == 11))) {hat_env = 2400;hat_open = 0;}
        if (bar >= 2) {
            int8_t b = techno_bass[s16];
            if (b) {bass_inc = note_inc(root + b - 1);bass_env = 4096;} else bass_env = 0;
        }
        if (bar >= 4) {
            static const uint8_t arp[4] = {0, 1, 2, 3};
            uint32_t a = arp[s16 & 3u];
            arp_inc = note_inc(root + 36 + (a == 0 ? 0 : a == 1 ? third : a == 2 ? 7 : 12));arp_env = 4096;
        }
        if (bar >= 8 && bar <= 13) {
            int32_t n = techno_lead[((bar - 8) & 3u) * 16 + s16];
            if (n > 0) {lead_inc = note_inc(n);lead_env = 4096;}
            else if (n == 0) lead_env = 0;
        } else lead_env = 0;
    }
    if (++pos >= TECHNO_STEP) {pos = 0;step = (step + 1) & 255u;}
    int32_t v = drums();
    /* bass: 25% pulse, gated per 16th for a tight robotic feel */
    bass_phase += bass_inc;
    v += ((bass_phase >> 30) == 0 ? 1 : -1) * (int32_t)bass_env * 650 / 4096;
    bass_env = bass_env > 6 ? bass_env - 6 : 0;
    /* arps: thin 12.5% pulse, short */
    arp_phase += arp_inc;
    v += ((arp_phase >> 29) == 0 ? 1 : -1) * (int32_t)arp_env * 170 / 4096;
    arp_env = arp_env > 4 ? arp_env - 4 : 0;
    /* lead: cold 50% square with a slightly detuned second voice */
    lead_phase += lead_inc;low_phase += lead_inc + (lead_inc >> 9);
    v += (((lead_phase >> 31) ? 1 : -1) + ((low_phase >> 31) ? 1 : -1)) * (int32_t)lead_env * 190 / 4096;
    if (lead_env > 2400) lead_env -= 1;
    return v;
}

/* ---- 3: rock anthem ---- original 16-bar loop, 16th grid at 132 BPM (~29 s):
 *   bars 0-3   intro: brass-style square fanfare with wide leaps over Em C D Em
 *   bars 4-11  chorus: big doubled square lead (octave below), Em C D B Em C Am-D B
 *   bars 12-15 solo: fast pentatonic "guitar" runs with pitch bends and vibrato
 * Backing throughout: octave-pump bass, rapid 16th chord arpeggios, kick + noise snare. */
#define ANTHEM_STEP 2506u              /* samples per sixteenth at 132 BPM */
#define H -1
static const int8_t anthem_root[16] = {40,36,38,40, 40,36,38,47, 40,36,45,47, 40,36,38,47};
static const uint8_t anthem_minor[16] = {1,0,0,1, 1,0,0,0, 1,0,1,0, 1,0,0,0};
static const int8_t anthem_lead[256] = {
    /* intro */
    64,H,H,H, 71,H,76,H, H,H,H,H, 74,H,71,H,
    72,H,H,H, 67,H,72,H, 76,H,H,H, 74,H,72,H,
    74,H,H,H, 69,H,74,H, 78,H,H,H, 81,H,78,H,
    79,H,H,H, 76,H,H,H, 71,H,74,H, 75,H,H,H,
    /* chorus */
    76,H,H,H, 71,H,76,H, 79,H,H,H, 78,H,76,H,
    79,H,H,H, 76,H,72,H, 76,H,H,H, 79,H,H,H,
    78,H,H,H, 74,H,69,H, 74,H,H,H, 78,H,81,H,
    83,H,H,H, H,H,81,H, 79,H,78,H, 75,H,H,H,
    76,H,88,H, 83,H,H,H, 79,H,H,H, 76,H,H,H,
    84,H,H,H, 83,H,81,H, 79,H,H,H, 76,H,H,H,
    81,H,H,H, 79,H,78,H, 74,H,H,H, 78,H,H,H,
    83,H,H,H, H,H,H,H, 87,H,H,H, H,H,H,H,
    /* solo */
    76,79,81,83, 86,83,81,79, 81,83,86,88, 86,83,81,79,
    84,H,84,H, 83,81,79,81, 83,H,H,H, 79,81,83,84,
    86,85,83,81, 78,81,83,86, 90,H,H,H, 88,86,83,81,
    83,H,H,H, 81,83,87,H, H,H,86,H, 87,H,H,H};
#undef H
/* bend up into the note by N semitones (solo) */
static const uint8_t anthem_bend[256] = {[208] = 2, [210] = 1, [216] = 2, [232] = 2, [240] = 1, [252] = 1};
static int32_t rock_sample(void)
{
    if (pos == 0) {
        uint32_t bar = step / 16, s16 = step & 15u, section = bar < 4 ? 0 : bar < 12 ? 1 : 2;
        int32_t root = anthem_root[bar], third = anthem_minor[bar] ? 3 : 4;
        /* drums */
        if (s16 == 0 || s16 == 8 || (section && (s16 == 6 || s16 == 14)) || (section == 2 && (s16 & 3u) == 0)) kick();
        if (s16 == 4 || s16 == 12 || ((bar == 7 || bar == 11 || bar == 15) && s16 >= 13)) snare_env = 4096;  /* fills */
        if ((s16 & 1u) == 0) {hat_env = section ? 2600 : 1800;hat_open = 0;}
        /* bass: octave pump on 16ths */
        bass_inc = note_inc(root + ((s16 & 1u) ? 12 : 0));bass_env = 4096;
        /* rapid chord arpeggio, two octaves up */
        static const uint8_t arp[8] = {0, 1, 2, 3, 2, 1, 0, 2};
        uint32_t a = arp[s16 & 7u];
        int32_t iv = a == 0 ? 0 : a == 1 ? third : a == 2 ? 7 : 12;
        arp_inc = note_inc(root + 24 + iv);arp_env = 4096;
        /* lead */
        int32_t n = anthem_lead[step];
        if (n > 0) {
            lead_target = note_inc(n);
            lead_inc = anthem_bend[step] ? note_inc(n - anthem_bend[step]) : lead_target;
            lead_env = 4096;lead_hold = 0;
        } else if (n < 0) ++lead_hold;
        else lead_env = 0;
    }
    uint32_t bar = step / 16, section = bar < 4 ? 0 : bar < 12 ? 1 : 2;
    if (++pos >= ANTHEM_STEP) {pos = 0;step = (step + 1) & 255u;}
    int32_t v = drums();
    /* bass: square, punchy */
    bass_phase += bass_inc;
    v += ((bass_phase >> 31) ? 1 : -1) * (int32_t)bass_env * 560 / 4096;
    bass_env = bass_env > 1500 ? bass_env - 2 : 1500;
    /* arpeggio: thin 12.5% square */
    arp_phase += arp_inc;
    v += ((arp_phase >> 29) == 0 ? 1 : -1) * (int32_t)arp_env * 200 / 4096;
    arp_env = arp_env > 3 ? arp_env - 3 : 0;
    /* lead: glide for bends, vibrato on held notes, bold square (25%) + octave-down square in the chorus */
    if (lead_inc < lead_target) {uint32_t d = (lead_target - lead_inc) >> 8;lead_inc += d ? d : 1;}
    else if (lead_inc > lead_target) lead_inc = lead_target;
    uint32_t inc = lead_inc;
    if (lead_hold > 2) {
        vib_phase += 1168000u;                                     /* ~6 Hz */
        inc = (uint32_t)((int64_t)inc + (int64_t)inc * tri(vib_phase) / 16384 / 45);
    }
    lead_phase += inc;low_phase += inc >> 1;
    int32_t l = ((lead_phase >> 30) == 0 ? 1 : -1) * 520;
    if (section == 1) l += ((low_phase >> 31) ? 1 : -1) * 230;
    if (section == 2) l = l * 3 / 2 > 700 ? 700 : l * 3 / 2;     /* solo slightly hotter */
    v += l * (int32_t)lead_env / 4096;
    if (lead_env > 2800) lead_env -= 1;
    return v;
}

void x6_music_render(int16_t *out, uint32_t frames, uint32_t tune)
{
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t v = 0;
        if (tune == X6_TUNE_CHIP) v = chip_sample();
        else if (tune == X6_TUNE_TECHNO) v = techno_sample();
        else if (tune == X6_TUNE_ROCK) v = rock_sample();
        if (fade < 65536) fade += 4;                     /* ~0.7 s fade-in */
        v = v * (int32_t)(fade >> 4) / 4096 * 9 / 16;
        if (click_env) {                                  /* 2.2 kHz blip, ~25 ms */
            click_phase += 428000000u;
            v += ((click_phase >> 31) ? 1 : -1) * (int32_t)click_env * 900 / 4096;
            click_env = click_env > 8 ? click_env - 8 : 0;
        }
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        out[2 * i] = (int16_t)v;out[2 * i + 1] = (int16_t)v;
    }
}

#ifndef GFX_SPACE_H
#define GFX_SPACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_SPACE_SAMPLE_RATE 48000u
#define GFX_SPACE_BLOCK_FRAMES 8u

/*
 * Bipolar Space effect:
 *   macro < 0: intimate room/plate voicing
 *   macro = 0: bit-exact dry output; both networks continue processing
 *   macro > 0: expansive hall voicing
 *
 * Caller owns this state. Initialize once before processing.
 * Fixed-memory, allocation-free, portable C11 implementation.
 */
typedef struct {
    /* Room AP storage: 206 samples; Hall AP storage: 1466 samples. */
    float ap_buffer[1672];
    unsigned ap_pos[2][4];

    /* Room FDN storage: 2670 samples; Hall FDN storage: 14570 samples. */
    float fdn_buffer[17240];
    unsigned fdn_pos[2][8];

    float damping_state[2][8];
    float feedback_gain[2][8];

    float smoothed_macro;
    float wet_mix;
    float room_decay_seconds;
    float hall_decay_seconds;
    float room_damping_alpha;
    float hall_damping_alpha;

    float target_wet_mix;
    float target_room_decay_seconds;
    float target_hall_decay_seconds;
    float target_room_damping_alpha;
    float target_hall_damping_alpha;
    unsigned parameter_counter;
    int initialized;
} gfx_space_state;

void gfx_space_init(gfx_space_state *state);
void gfx_space_process_sample(gfx_space_state *state, float *left, float *right, float macro);
void gfx_space_process(gfx_space_state *state, float *left, float *right,
                       size_t frame_count, float macro);

#ifdef __cplusplus
}
#endif
#endif

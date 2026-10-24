#ifndef GFX_SPACE_H
#define GFX_SPACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_SPACE_SAMPLE_RATE 48000u

typedef struct {
    float ap_buffer[1466];
    unsigned ap_pos[4];
    float fdn_buffer[14570];
    unsigned fdn_pos[8];
    float damping_state[8];
    float wet_mix;
    float decay_seconds;
    float damping_alpha;
    float feedback_gain[8];
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

#include "gfx_space.h"
#include <math.h>
#include <string.h>

#define GFX_PI 3.14159265358979323846f
#define GFX_INV_SQRT8 0.35355339059327376220f
#define GFX_SMOOTH_COEFF 0.0025f
#define GFX_T60_LN1000 6.907755278982137f

enum { ROOM = 0, HALL = 1, NETWORK_COUNT = 2 };

static const unsigned k_ap_delay[NETWORK_COUNT][4] = {
    {31u, 43u, 59u, 73u}, {173u, 233u, 449u, 611u}
};
static const unsigned k_ap_offset[NETWORK_COUNT][4] = {
    {0u, 31u, 74u, 133u}, {206u, 379u, 612u, 1061u}
};
static const float k_ap_coeff[NETWORK_COUNT][4] = {
    {0.72f, 0.72f, 0.75f, 0.75f}, {0.65f, 0.65f, 0.70f, 0.70f}
};
static const unsigned k_fdn_delay[NETWORK_COUNT][8] = {
    {149u, 211u, 263u, 293u, 337u, 397u, 463u, 557u},
    {953u, 1103u, 1297u, 1531u, 1823u, 2153u, 2591u, 3119u}
};
static const unsigned k_fdn_offset[NETWORK_COUNT][8] = {
    {0u, 149u, 360u, 623u, 916u, 1253u, 1650u, 2113u},
    {2670u, 3623u, 4726u, 6023u, 7554u, 9377u, 11530u, 14121u}
};
static const float k_hadamard[8][8] = {
    { 1, 1, 1, 1, 1, 1, 1, 1},
    { 1,-1, 1,-1, 1,-1, 1,-1},
    { 1, 1,-1,-1, 1, 1,-1,-1},
    { 1,-1,-1, 1, 1,-1,-1, 1},
    { 1, 1, 1, 1,-1,-1,-1,-1},
    { 1,-1, 1,-1,-1, 1,-1, 1},
    { 1, 1,-1,-1,-1,-1, 1, 1},
    { 1,-1,-1, 1,-1, 1, 1,-1}
};

static float clampf_local(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float log_lerp(float a, float b, float t)
{
    return expf(lerpf(logf(a), logf(b), t));
}
static float damping_alpha(float cutoff_hz)
{
    float a = 1.0f - expf((-2.0f * GFX_PI * cutoff_hz) /
                          (float)GFX_SPACE_SAMPLE_RATE);
    return clampf_local(a, 0.0f, 1.0f);
}

static void targets_from_macro(float macro, gfx_space_state *s)
{
    const float u = clampf_local(fabsf(macro), 0.0f, 1.0f);
    float hall_t60, hall_cutoff;
    if (u <= 0.7f) {
        const float t = u / 0.7f;
        s->target_wet_mix = lerpf(0.0f, 0.40f, t);
        hall_t60 = log_lerp(1.5f, 3.5f, t);
        hall_cutoff = log_lerp(12000.0f, 7000.0f, t);
    } else {
        const float t = (u - 0.7f) / 0.3f;
        s->target_wet_mix = lerpf(0.40f, 0.60f, t);
        hall_t60 = log_lerp(3.5f, 5.0f, t);
        hall_cutoff = log_lerp(7000.0f, 4500.0f, t);
    }
    s->target_room_decay_seconds = log_lerp(0.30f, 1.50f, u);
    s->target_room_damping_alpha = damping_alpha(log_lerp(12000.0f, 8500.0f, u));
    s->target_hall_decay_seconds = hall_t60;
    s->target_hall_damping_alpha = damping_alpha(hall_cutoff);
}

/*
 * Host impulse fits show the damping loop shortens broadband T60 by about 9%.
 * Apply 1.10 compensation; remeasure if delay lengths or damping curves change.
 */
static void update_feedback_gains(gfx_space_state *s, unsigned network, float t60)
{
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        const float delay = (float)k_fdn_delay[network][i];
        s->feedback_gain[network][i] =
            expf((-GFX_T60_LN1000 * delay) /
                 ((float)GFX_SPACE_SAMPLE_RATE * (t60 * 1.10f)));
    }
}

static void run_network(gfx_space_state *s, unsigned network, float input,
                        float *wet_left, float *wet_right)
{
    float x = input;
    float d[8], q[8], feedback[8], write_value[8];
    const float alpha = (network == ROOM) ? s->room_damping_alpha : s->hall_damping_alpha;
    unsigned i, j;

    for (i = 0; i < 4u; ++i) {
        const unsigned pos = s->ap_pos[network][i];
        const unsigned offset = k_ap_offset[network][i];
        const float delayed_v = s->ap_buffer[offset + pos];
        const float v = x + k_ap_coeff[network][i] * delayed_v;
        const float y = delayed_v - k_ap_coeff[network][i] * v;
        s->ap_buffer[offset + pos] = v;
        s->ap_pos[network][i] = (pos + 1u == k_ap_delay[network][i]) ? 0u : pos + 1u;
        x = y;
    }

    for (i = 0; i < 8u; ++i) {
        const unsigned offset = k_fdn_offset[network][i];
        const unsigned pos = s->fdn_pos[network][i];
        d[i] = s->fdn_buffer[offset + pos];
        q[i] = s->damping_state[network][i] +
               alpha * (d[i] - s->damping_state[network][i]);
        s->damping_state[network][i] = q[i];
    }
    for (i = 0; i < 8u; ++i) {
        float sum = 0.0f;
        for (j = 0; j < 8u; ++j) sum += k_hadamard[i][j] * q[j];
        feedback[i] = GFX_INV_SQRT8 * sum;
    }
    for (i = 0; i < 8u; ++i)
        write_value[i] = x * GFX_INV_SQRT8 +
                         s->feedback_gain[network][i] * feedback[i];

    {
        float l = 0.0f, r = 0.0f;
        for (i = 0; i < 8u; ++i) {
            l += d[i];
            r += ((i & 1u) == 0u) ? d[i] : -d[i];
        }
        *wet_left = GFX_INV_SQRT8 * l;
        *wet_right = GFX_INV_SQRT8 * r;
    }
    for (i = 0; i < 8u; ++i) {
        const unsigned offset = k_fdn_offset[network][i];
        const unsigned pos = s->fdn_pos[network][i];
        s->fdn_buffer[offset + pos] = write_value[i];
        s->fdn_pos[network][i] =
            (pos + 1u == k_fdn_delay[network][i]) ? 0u : pos + 1u;
    }
}

void gfx_space_init(gfx_space_state *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->room_decay_seconds = 0.30f;
    state->hall_decay_seconds = 1.50f;
    state->room_damping_alpha = damping_alpha(12000.0f);
    state->hall_damping_alpha = damping_alpha(12000.0f);
    state->target_room_decay_seconds = state->room_decay_seconds;
    state->target_hall_decay_seconds = state->hall_decay_seconds;
    state->target_room_damping_alpha = state->room_damping_alpha;
    state->target_hall_damping_alpha = state->hall_damping_alpha;
    update_feedback_gains(state, ROOM, state->room_decay_seconds);
    update_feedback_gains(state, HALL, state->hall_decay_seconds);
    state->initialized = 1;
}

void gfx_space_process_sample(gfx_space_state *state, float *left, float *right, float macro)
{
    float dry_l, dry_r, mono, room_l, room_r, hall_l, hall_r, wet_l, wet_r;
    if (state == NULL || left == NULL || right == NULL) return;
    if (!state->initialized) gfx_space_init(state);
    if (!isfinite(macro)) macro = 0.0f;
    macro = clampf_local(macro, -1.0f, 1.0f);
    dry_l = *left;
    dry_r = *right;
    state->smoothed_macro += GFX_SMOOTH_COEFF * (macro - state->smoothed_macro);

    /* Parameter curves and exp/log work run at the 8-frame block rate. */
    if (state->parameter_counter == 0u) {
        targets_from_macro(state->smoothed_macro, state);
        update_feedback_gains(state, ROOM, state->room_decay_seconds);
        update_feedback_gains(state, HALL, state->hall_decay_seconds);
    }
    state->wet_mix += GFX_SMOOTH_COEFF * (state->target_wet_mix - state->wet_mix);
    state->room_decay_seconds += GFX_SMOOTH_COEFF *
        (state->target_room_decay_seconds - state->room_decay_seconds);
    state->hall_decay_seconds += GFX_SMOOTH_COEFF *
        (state->target_hall_decay_seconds - state->hall_decay_seconds);
    state->room_damping_alpha += GFX_SMOOTH_COEFF *
        (state->target_room_damping_alpha - state->room_damping_alpha);
    state->hall_damping_alpha += GFX_SMOOTH_COEFF *
        (state->target_hall_damping_alpha - state->hall_damping_alpha);

    mono = 0.5f * (dry_l + dry_r);
    run_network(state, ROOM, mono, &room_l, &room_r);
    run_network(state, HALL, mono, &hall_l, &hall_r);

    if (macro == 0.0f) {
        *left = dry_l;
        *right = dry_r;
    } else {
        const float m = state->wet_mix;
        float side = clampf_local((state->smoothed_macro + 0.25f) / 0.50f, 0.0f, 1.0f);
        /* Smoothly crossfade room and hall voicings through the dry center. */
        side = side * side * (3.0f - 2.0f * side);
        wet_l = lerpf(room_l, hall_l, side);
        wet_r = lerpf(room_r, hall_r, side);
        *left = (1.0f - m) * dry_l + m * wet_l;
        *right = (1.0f - m) * dry_r + m * wet_r;
    }
    state->parameter_counter = (state->parameter_counter + 1u) % GFX_SPACE_BLOCK_FRAMES;
}

void gfx_space_process(gfx_space_state *state, float *left, float *right,
                       size_t frame_count, float macro)
{
    size_t n;
    if (state == NULL || left == NULL || right == NULL) return;
    for (n = 0; n < frame_count; ++n)
        gfx_space_process_sample(state, &left[n], &right[n], macro);
}

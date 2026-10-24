#include "gfx_space.h"
#include <math.h>
#include <string.h>

#define GFX_PI 3.14159265358979323846f
#define GFX_INV_SQRT8 0.35355339059327376220f
#define GFX_SMOOTH_COEFF 0.0025f

static const unsigned k_ap_delay[4] = {173u, 233u, 449u, 611u};
static const float k_ap_coeff[4] = {0.65f, 0.65f, 0.70f, 0.70f};
static const unsigned k_ap_offset[4] = {0u, 173u, 406u, 855u};
static const unsigned k_fdn_delay[8] = {953u, 1103u, 1297u, 1531u, 1823u, 2153u, 2591u, 3119u};
static const unsigned k_fdn_offset[8] = {0u, 953u, 2056u, 3353u, 4884u, 6707u, 8860u, 11451u};

/* Unnormalized Walsh-ordered Hadamard signs; transform applies 1/sqrt(8). */
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

static float clampf_local(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float log_lerp(float a, float b, float t) { return expf(lerpf(logf(a), logf(b), t)); }

static void targets_from_macro(float macro, float *wet, float *t60, float *cutoff) {
    const float u = clampf_local(fabsf(macro), 0.0f, 1.0f);
    if (u <= 0.7f) {
        const float t = u / 0.7f;
        *wet = lerpf(0.0f, 0.40f, t);
        *t60 = log_lerp(1.5f, 3.5f, t);
        *cutoff = log_lerp(12000.0f, 7000.0f, t);
    } else {
        const float t = (u - 0.7f) / 0.3f;
        *wet = lerpf(0.40f, 0.60f, t);
        *t60 = log_lerp(3.5f, 5.0f, t);
        *cutoff = log_lerp(7000.0f, 4500.0f, t);
    }
}

static float one_pole_alpha(float cutoff_hz) {
    const float a = 1.0f - expf((-2.0f * GFX_PI * cutoff_hz) / (float)GFX_SPACE_SAMPLE_RATE);
    return clampf_local(a, 0.0f, 1.0f);
}

static void update_feedback_targets(gfx_space_state *s, float t60) {
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        s->feedback_gain[i] = expf((-6.907755278982137f * (float)k_fdn_delay[i]) /
                                   ((float)GFX_SPACE_SAMPLE_RATE * t60));
    }
}

void gfx_space_init(gfx_space_state *state) {
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->wet_mix = 0.0f;
    state->decay_seconds = 1.5f;
    state->damping_alpha = one_pole_alpha(12000.0f);
    update_feedback_targets(state, state->decay_seconds);
    state->initialized = 1;
}

void gfx_space_process_sample(gfx_space_state *state, float *left, float *right, float macro) {
    float dry_l, dry_r, mono, x, wet_l, wet_r;
    float target_wet, target_t60, target_cutoff, target_alpha;
    float d[8], q[8], f[8], write_value[8];
    unsigned i, j;

    if (state == NULL || left == NULL || right == NULL) return;
    if (!state->initialized) gfx_space_init(state);
    dry_l = *left;
    dry_r = *right;

    targets_from_macro(macro, &target_wet, &target_t60, &target_cutoff);
    target_alpha = one_pole_alpha(target_cutoff);
    state->wet_mix += GFX_SMOOTH_COEFF * (target_wet - state->wet_mix);
    state->decay_seconds += GFX_SMOOTH_COEFF * (target_t60 - state->decay_seconds);
    state->damping_alpha += GFX_SMOOTH_COEFF * (target_alpha - state->damping_alpha);
    update_feedback_targets(state, state->decay_seconds);

    /* Mono excitation; dry stereo is preserved separately. */
    mono = 0.5f * (dry_l + dry_r);
    x = mono;
    for (i = 0; i < 4u; ++i) {
        const unsigned pos = state->ap_pos[i];
        const float delayed_v = state->ap_buffer[k_ap_offset[i] + pos];
        const float v = x + k_ap_coeff[i] * delayed_v;
        const float y = delayed_v - k_ap_coeff[i] * v;
        state->ap_buffer[k_ap_offset[i] + pos] = v;
        state->ap_pos[i] = (pos + 1u == k_ap_delay[i]) ? 0u : pos + 1u;
        x = y;
    }

    /* Read every FDN line before any line is written. */
    for (i = 0; i < 8u; ++i) {
        d[i] = state->fdn_buffer[k_fdn_offset[i] + state->fdn_pos[i]];
        q[i] = state->damping_state[i] + state->damping_alpha * (d[i] - state->damping_state[i]);
        state->damping_state[i] = q[i];
    }
    for (i = 0; i < 8u; ++i) {
        float sum = 0.0f;
        for (j = 0; j < 8u; ++j) sum += k_hadamard[i][j] * q[j];
        f[i] = GFX_INV_SQRT8 * sum;
    }
    for (i = 0; i < 8u; ++i)
        write_value[i] = x * GFX_INV_SQRT8 + state->feedback_gain[i] * f[i];

    /* Distinct orthogonal projections for left/right wet outputs. */
    {
        float sum_l = 0.0f, sum_r = 0.0f;
        for (i = 0; i < 8u; ++i) {
            sum_l += d[i];
            sum_r += ((i & 1u) == 0u) ? d[i] : -d[i];
        }
        wet_l = GFX_INV_SQRT8 * sum_l;
        wet_r = GFX_INV_SQRT8 * sum_r;
    }
    for (i = 0; i < 8u; ++i) {
        const unsigned pos = state->fdn_pos[i];
        state->fdn_buffer[k_fdn_offset[i] + pos] = write_value[i];
        state->fdn_pos[i] = (pos + 1u == k_fdn_delay[i]) ? 0u : pos + 1u;
    }

    /* Exact dry output at macro zero; internal reverb state still advances. */
    if (macro == 0.0f) {
        *left = dry_l;
        *right = dry_r;
    } else {
        const float m = state->wet_mix;
        *left = (1.0f - m) * dry_l + m * wet_l;
        *right = (1.0f - m) * dry_r + m * wet_r;
    }
}

void gfx_space_process(gfx_space_state *state, float *left, float *right,
                       size_t frame_count, float macro) {
    size_t n;
    if (state == NULL || left == NULL || right == NULL) return;
    for (n = 0; n < frame_count; ++n)
        gfx_space_process_sample(state, &left[n], &right[n], macro);
}

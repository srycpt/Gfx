#include "gfx_space.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SECONDS 8u
#define N ((size_t)GFX_SPACE_SAMPLE_RATE * TEST_SECONDS)
#define PRE_ROLL ((size_t)GFX_SPACE_SAMPLE_RATE / 10u)
#define RMS_WINDOW ((size_t)GFX_SPACE_SAMPLE_RATE / 10u)

static float left[N];
static float right[N];

static int all_finite(const float *x, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) if (!isfinite(x[i])) return 0;
    return 1;
}

static float rms_range(const float *l, const float *r, size_t begin, size_t end)
{
    double sum = 0.0;
    size_t i, count = 0;
    for (i = begin; i < end; ++i) {
        sum += (double)l[i] * l[i] + (double)r[i] * r[i];
        count += 2;
    }
    return count ? (float)sqrt(sum / (double)count) : 0.0f;
}

static float correlation(const float *l, const float *r, size_t begin, size_t end)
{
    double lr = 0.0, ll = 0.0, rr = 0.0;
    size_t i;
    for (i = begin; i < end; ++i) {
        lr += (double)l[i] * r[i];
        ll += (double)l[i] * l[i];
        rr += (double)r[i] * r[i];
    }
    return (ll > 0.0 && rr > 0.0) ? (float)(lr / sqrt(ll * rr)) : 0.0f;
}

static float estimate_t60(const float *l, const float *r, float start_s, float end_s)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    size_t count = 0;
    size_t start = (size_t)(start_s * (float)GFX_SPACE_SAMPLE_RATE);
    size_t end = (size_t)(end_s * (float)GFX_SPACE_SAMPLE_RATE);
    size_t a;

    if (end > N) end = N;
    for (a = start; a + RMS_WINDOW <= end; a += RMS_WINDOW) {
        const float rms = rms_range(l, r, a, a + RMS_WINDOW);
        const double x = ((double)a + (double)RMS_WINDOW * 0.5) /
                         (double)GFX_SPACE_SAMPLE_RATE;
        const double y = rms > 1.0e-8f ? 20.0 * log10((double)rms) : -160.0;
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++count;
    }
    if (count < 3u) return 0.0f;
    {
        const double den = (double)count * sxx - sx * sx;
        const double slope = den != 0.0 ?
            ((double)count * sxy - sx * sy) / den : 0.0;
        return slope < -1.0 ? (float)(-60.0 / slope) : 0.0f;
    }
}

static void report_bands(const float *l, const float *r, const char *name)
{
    const float edges[] = {0.0f, 0.1f, 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f};
    size_t i;
    printf("\n%s impulse response RMS by time band (dBFS):\n", name);
    for (i = 0; i + 1u < sizeof(edges) / sizeof(edges[0]); ++i) {
        size_t a = (size_t)(edges[i] * (float)GFX_SPACE_SAMPLE_RATE);
        size_t b = (size_t)(edges[i + 1u] * (float)GFX_SPACE_SAMPLE_RATE);
        const float rms = rms_range(l, r, a, b);
        const float db = rms > 0.0f ? 20.0f * log10f(rms) : -INFINITY;
        printf("%4.1f–%4.1f s: %8.2f dBFS\n", edges[i], edges[i + 1u], db);
    }
}

static int write_csv(const char *path, const float *l, const float *r)
{
    FILE *fp = fopen(path, "w");
    size_t i;
    if (!fp) { perror(path); return 0; }
    fputs("sample,time_s,left,right\n", fp);
    for (i = 0; i < N; ++i)
        fprintf(fp, "%zu,%.9f,%.9g,%.9g\n",
                i, (double)i / GFX_SPACE_SAMPLE_RATE, l[i], r[i]);
    fclose(fp);
    return 1;
}

static int run_impulse(float macro, const char *name, const char *csv,
                       float decay_start, float decay_end)
{
    gfx_space_state state;
    float pre_l[8] = {0}, pre_r[8] = {0};
    size_t remaining = PRE_ROLL;
    float peak = 0.0f;
    size_t i;
    float t60, corr, mono_rms;
    int failed = 0;

    gfx_space_init(&state);
    while (remaining > 0u) {
        const size_t n = remaining < 8u ? remaining : 8u;
        memset(pre_l, 0, sizeof(pre_l));
        memset(pre_r, 0, sizeof(pre_r));
        gfx_space_process(&state, pre_l, pre_r, n, macro);
        remaining -= n;
    }

    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    left[0] = right[0] = 0.5f;
    gfx_space_process(&state, left, right, N, macro);

    if (!all_finite(left, N) || !all_finite(right, N)) {
        printf("%s finite output: FAIL\n", name);
        return 1;
    }
    puts("finite output: PASS");
    for (i = 0; i < N; ++i) {
        const float pl = fabsf(left[i]), pr = fabsf(right[i]);
        if (pl > peak) peak = pl;
        if (pr > peak) peak = pr;
    }
    corr = correlation(left, right,
        (size_t)(0.1f * (float)GFX_SPACE_SAMPLE_RATE), N);
    {
        double sum = 0.0;
        for (i = 0; i < N; ++i) {
            const double m = 0.5 * ((double)left[i] + right[i]);
            sum += m * m;
        }
        mono_rms = (float)sqrt(sum / (double)N);
    }
    t60 = estimate_t60(left, right, decay_start, decay_end);
    if ((!strcmp(name, "ROOM") && (t60 < 1.15f || t60 > 1.85f)) ||
        (!strcmp(name, "HALL") && (t60 < 4.0f || t60 > 6.0f))) {
        printf("%s decay target: FAIL\n", name);
        failed = 1;
    } else {
        printf("%s decay target: PASS\n", name);
    }
    printf("%s peak: %.6f (%.2f dBFS)\n", name, peak,
           peak > 0.0f ? 20.0f * log10f(peak) : -INFINITY);
    printf("%s estimated T60: %.2f s (fit %.2f–%.2f s)\n",
           name, t60, decay_start, decay_end);
    printf("%s stereo correlation: %.3f; mono-fold RMS: %.6f\n",
           name, corr, mono_rms);

    if (peak > 1.0f) { printf("%s peak safety: FAIL\n", name); failed = 1; }
    if (mono_rms < 1.0e-6f) { printf("%s mono fold-down: FAIL\n", name); failed = 1; }
    if (corr > 0.995f) { printf("%s stereo distinction: WARN (high correlation)\n", name); }

    report_bands(left, right, name);
    if (!write_csv(csv, left, right)) return 1;
    printf("wrote %s\n", csv);
    return failed;
}

static int test_antiphase_input(void)
{
    gfx_space_state state;
    float pre_l[8], pre_r[8];
    size_t remaining = PRE_ROLL;
    float tail_rms;
    gfx_space_init(&state);
    while (remaining > 0u) {
        const size_t n = remaining < 8u ? remaining : 8u;
        memset(pre_l, 0, sizeof(pre_l));
        memset(pre_r, 0, sizeof(pre_r));
        gfx_space_process(&state, pre_l, pre_r, n, 1.0f);
        remaining -= n;
    }
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    left[0] = 0.5f;
    right[0] = -0.5f;
    gfx_space_process(&state, left, right, N, 1.0f);
    tail_rms = rms_range(left, right,
        (size_t)(0.05f * (float)GFX_SPACE_SAMPLE_RATE),
        (size_t)(1.50f * (float)GFX_SPACE_SAMPLE_RATE));
    printf("antiphase stereo excitation RMS: %.8f (%s)\n",
           tail_rms, tail_rms > 1.0e-5f ? "PASS" : "FAIL");
    return tail_rms > 1.0e-5f ? 0 : 1;
}

int main(void)
{
    gfx_space_state state;
    float dry_l[64], dry_r[64];
    size_t i;
    int failed = 0;

    gfx_space_init(&state);
    for (i = 0; i < 64u; ++i) {
        dry_l[i] = left[i] = (float)((int)(i % 17u) - 8) / 9.0f;
        dry_r[i] = right[i] = (float)((int)(i % 13u) - 6) / 7.0f;
    }
    gfx_space_process(&state, left, right, 64u, 0.0f);
    for (i = 0; i < 64u; ++i)
        if (left[i] != dry_l[i] || right[i] != dry_r[i]) failed = 1;
    if (state.ap_buffer[0] == 0.0f) failed = 1;
    printf("exact dry bypass with live state: %s\n", failed ? "FAIL" : "PASS");

    failed |= run_impulse(-1.0f, "ROOM", "space_room_impulse.csv", 0.1f, 1.2f);
    failed |= run_impulse(1.0f, "HALL", "space_hall_impulse.csv", 0.3f, 4.5f);
    failed |= test_antiphase_input();
    return failed ? 1 : 0;
}

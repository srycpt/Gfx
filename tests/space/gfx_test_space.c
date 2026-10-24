#include "gfx_space.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SECONDS 8
#define PREFIX (GFX_SPACE_SAMPLE_RATE / 10u)
#define N (GFX_SPACE_SAMPLE_RATE * TEST_SECONDS)

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

static void report_bands(const float *l, const float *r)
{
    const unsigned edges[] = {0, 5, 10, 15, 20, 30, 40, 50, 60, 70, 80};
    size_t i;
    puts("Impulse response RMS by time band (seconds), dBFS:");
    for (i = 0; i + 1 < sizeof(edges) / sizeof(edges[0]); ++i) {
        size_t a = PREFIX + (size_t)edges[i] * GFX_SPACE_SAMPLE_RATE / 10u;
        size_t b = PREFIX + (size_t)edges[i + 1] * GFX_SPACE_SAMPLE_RATE / 10u;
        float rms, db;
        if (a > N) a = N;
        if (b > N) b = N;
        rms = rms_range(l, r, a, b);
        db = rms > 0.0f ? 20.0f * log10f(rms) : -INFINITY;
        printf("%4.1f–%4.1f s: %8.2f dBFS\n",
               edges[i] / 10.0, edges[i + 1] / 10.0, db);
    }
}

int main(void)
{
    static float left[N];
    static float right[N];
    static float dry_l[64], dry_r[64];
    gfx_space_state state;
    size_t i;
    int failed = 0;

    /* Test 1: exact dry output at macro zero. */
    gfx_space_init(&state);
    for (i = 0; i < 64; ++i) {
        dry_l[i] = left[i] = (float)((int)(i % 17) - 8) / 9.0f;
        dry_r[i] = right[i] = (float)((int)(i % 13) - 6) / 7.0f;
    }
    gfx_space_process(&state, left, right, 64, 0.0f);
    for (i = 0; i < 64; ++i) {
        if (left[i] != dry_l[i] || right[i] != dry_r[i]) failed = 1;
    }
    printf("exact dry bypass: %s\n", failed ? "FAIL" : "PASS");

    /* Test 2: impulse response and finite/stability check. */
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    gfx_space_init(&state);
    left[PREFIX] = right[PREFIX] = 0.5f;
    gfx_space_process(&state, left, right, N, 0.7f);
    if (!all_finite(left, N) || !all_finite(right, N)) {
        puts("finite output: FAIL");
        return 2;
    }
    puts("finite output: PASS");
    {
        float peak = 0.0f;
        for (i = 0; i < N; ++i) {
            float a = fabsf(left[i]), b = fabsf(right[i]);
            if (a > peak) peak = a;
            if (b > peak) peak = b;
        }
        printf("peak output: %.6f (%.2f dBFS)\n",
               peak, peak > 0.0f ? 20.0f * log10f(peak) : -INFINITY);
    }
    report_bands(left, right);

    /* CSV enables plotting/correlation checks in Python/R. */
    {
        FILE *fp = fopen("space_impulse.csv", "w");
        if (!fp) {
            perror("space_impulse.csv");
            return 1;
        }
        fputs("sample,time_s,left,right\n", fp);
        for (i = 0; i < N; ++i)
            fprintf(fp, "%zu,%.9f,%.9g,%.9g\n",
                    i, (double)i / GFX_SPACE_SAMPLE_RATE, left[i], right[i]);
        fclose(fp);
        puts("wrote space_impulse.csv");
    }

    return failed ? 1 : 0;
}

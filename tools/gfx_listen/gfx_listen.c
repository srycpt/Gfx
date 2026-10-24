#define _POSIX_C_SOURCE 200809L
#include "gfx_space.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK_FRAMES 256
#define MACRO_LINE 128

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s --input test.wav --fx gfx_space --m VALUE\n"
        "Reads a WAV (mono or stereo, 48 kHz) and writes interleaved "
        "float32 stereo PCM to stdout.\n"
        "Enter a macro value on stdin followed by Enter to change it live.\n"
        "Pipe stdout to SoX play, e.g.:\n"
        "  %s --input test.wav --fx gfx_space --m -0.6 | "
        "play -q -t raw -r 48000 -e floating-point -b 32 -c 2 -\n",
        argv0, argv0);
}

static int parse_float(const char *s, float *out)
{
    char *end = NULL;
    float v;
    errno = 0;
    v = strtof(s, &end);
    if (errno || end == s || *end != '\0' || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

static int read_macro_updates(float *macro, char *pending, size_t *pending_len)
{
    struct pollfd pfd;
    char buf[128];
    ssize_t n;
    size_t i;

    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & (POLLIN | POLLHUP)))
        return 0;

    n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return 0;

    for (i = 0; i < (size_t)n; ++i) {
        char ch = buf[i];
        if (ch == '\n' || ch == '\r') {
            if (*pending_len > 0) {
                float v;
                pending[*pending_len] = '\0';
                if (parse_float(pending, &v)) {
                    *macro = v;
                    fprintf(stderr, "\nmacro=%.4f\n", (double)v);
                } else {
                    fprintf(stderr, "\nignored invalid macro: %s\n", pending);
                }
                *pending_len = 0;
            }
        } else if (*pending_len + 1 < MACRO_LINE) {
            pending[(*pending_len)++] = ch;
        } else {
            *pending_len = 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *fx_name = "gfx_space";
    float macro = 0.0f;
    int have_macro = 0;
    int i;
    SNDFILE *infile;
    SF_INFO info;
    gfx_space_state fx;
    float left[BLOCK_FRAMES], right[BLOCK_FRAMES];
    float interleaved[BLOCK_FRAMES * 2];
    char macro_pending[MACRO_LINE];
    size_t macro_pending_len = 0;
    sf_count_t frames_read;

    memset(&info, 0, sizeof(info));
    memset(macro_pending, 0, sizeof(macro_pending));

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--input") && i + 1 < argc) {
            input_path = argv[++i];
        } else if (!strcmp(argv[i], "--fx") && i + 1 < argc) {
            fx_name = argv[++i];
        } else if (!strcmp(argv[i], "--m") && i + 1 < argc) {
            if (!parse_float(argv[++i], &macro)) {
                fprintf(stderr, "Invalid --m value\n");
                return 2;
            }
            have_macro = 1;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!input_path || !have_macro) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(fx_name, "gfx_space") != 0) {
        fprintf(stderr, "Unsupported FX: %s (currently gfx_space only)\n", fx_name);
        return 2;
    }

    infile = sf_open(input_path, SFM_READ, &info);
    if (!infile) {
        fprintf(stderr, "Cannot open %s: %s\n", input_path, sf_strerror(NULL));
        return 1;
    }
    if (info.samplerate != (int)GFX_SPACE_SAMPLE_RATE ||
        (info.channels != 1 && info.channels != 2)) {
        fprintf(stderr,
            "Input must be 48 kHz mono or stereo; got %d Hz, %d channels.\n",
            info.samplerate, info.channels);
        sf_close(infile);
        return 2;
    }

    gfx_space_init(&fx);
    fprintf(stderr, "gfx_listen: %s, %d Hz, %d channel(s), macro %.4f\n",
            input_path, info.samplerate, info.channels, (double)macro);
    fprintf(stderr, "Live macro: type a number and press Enter on stdin.\n");

    while ((frames_read = sf_readf_float(infile, interleaved, BLOCK_FRAMES)) > 0) {
        sf_count_t n;
        read_macro_updates(&macro, macro_pending, &macro_pending_len);

        for (n = 0; n < frames_read; ++n) {
            if (info.channels == 1) {
                left[n] = interleaved[n];
                right[n] = interleaved[n];
            } else {
                left[n] = interleaved[2 * n];
                right[n] = interleaved[2 * n + 1];
            }
        }

        gfx_space_process(&fx, left, right, (size_t)frames_read, macro);

        for (n = 0; n < frames_read; ++n) {
            interleaved[2 * n] = left[n];
            interleaved[2 * n + 1] = right[n];
        }

        if (fwrite(interleaved, sizeof(float) * 2, (size_t)frames_read, stdout)
                != (size_t)frames_read) {
            fprintf(stderr, "stdout write failed\n");
            sf_close(infile);
            return 1;
        }
    }

    sf_close(infile);
    fflush(stdout);
    return 0;
}

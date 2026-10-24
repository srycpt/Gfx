# GfX

Embedded digital guitar-effects firmware for ESP32-S31-WROOM-1U. DSP runs at 48 kHz; codec I/O runs at 192 kHz.

## Layout

- `docs/architecture.md` — canonical architecture contract.
- `effects/space/hall/` — portable Hall reverb implementation.
- `tests/space/` — automated DSP checks and impulse-response report.
- `tools/gfx_listen/` — WAV listening harness with live macro updates.
- Root `Makefile` — host-side build and test targets.

## Build and test

Dependencies: C11 compiler, Make, libsndfile, and SoX for playback.

Debian/Ubuntu: `sudo apt install build-essential make libsndfile1-dev sox`

Termux: `pkg install clang make libsndfile sox`

Run `make` then `make test`. The test emits `space_impulse.csv`; generated files and build artifacts are not committed.

## Listen

Use a 48 kHz mono or stereo WAV:

```sh
./build/gfx_listen --input test.wav --fx gfx_space --m -0.6 \
  | play -q -t raw -r 48000 -e floating-point -b 32 -c 2 -
```

Enter a macro value followed by Enter to change it during playback. Audio is stdout; diagnostics are stderr. In this Hall-only build, macro sign is ignored and magnitude controls intensity.

## Status

Hall is an experimental baseline. Decay-time accuracy, stereo correlation, and target-device WCET still require validation.

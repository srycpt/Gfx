# GfX

Embedded digital guitar-effects firmware for ESP32-S31-WROOM-1U. DSP runs at 48 kHz; codec I/O runs at 192 kHz.

## Layout

- `docs/architecture.md` — canonical architecture contract.
- `effects/space/` — portable bipolar Space effect.
- `tests/space/` — endpoint impulse tests, T60 estimate, stereo correlation and mono-fold-down checks.
- `tools/gfx_listen/` — WAV listening harness with live macro updates.
- Root `Makefile` — host-side build/test targets.

## Space macro

- Negative: intimate room/plate voicing, short delays and shorter decay.
- Zero: exact bit-exact dry passthrough; both reverb states continue updating.
- Positive: expansive Hall, with T60 increasing toward 5 seconds.
- Wet mix increases with macro magnitude. The macro sign selects the voicing.

## Build and test

Dependencies: C11 compiler, Make, libsndfile, and SoX for playback.

Debian/Ubuntu: `sudo apt install build-essential make libsndfile1-dev sox`

Termux: `pkg install clang make libsndfile sox`

Run `make` then `make test`. The test writes `space_room_impulse.csv` and `space_hall_impulse.csv` under `build/`; generated files and build artifacts are not committed.

## Listen

Use a 48 kHz mono or stereo WAV:

```sh
./build/gfx_listen --input test.wav --fx gfx_space --m -0.6 \
  | play -q -t raw -r 48000 -e floating-point -b 32 -c 2 -
```

Enter a macro value followed by Enter to change it during playback. Audio is stdout; diagnostics are stderr.

## Validation status

The portable implementation is a development baseline. The host test measures decay and checks stereo/mono behavior; listening tests and target-device WCET measurement remain required before firmware integration.

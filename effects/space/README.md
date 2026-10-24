# Space

Bipolar room-to-hall insert effect, portable C11.

## Control

| Macro | Character | Nominal decay |
|---|---|---:|
| -1.0 | Intimate room/plate voicing | 1.5 s |
| 0.0 | Exact dry, unity | — |
| +1.0 | Expansive hall | 5.0 s |

The room/plate endpoint is an algorithmic small-room voicing, not a physical plate model. It uses shorter input diffusion and FDN delays, a brighter damping curve, and a shorter decay. Hall uses longer delay lengths and darker high-frequency damping. Wet mix rises from 0 to 0.60 with absolute macro.

Macro magnitude and polarity are smoothed. The room/hall wet character crossfades through a small region around the center, while raw macro exactly equal to zero always outputs the unmodified dry samples. Both networks continue processing in bypass.

## DSP topology

- Separate stereo input all-pass chains, with distinct L/R delays.
- Shared 8-line FDN per voicing, with normalized Hadamard feedback.
- Independent room and hall state.
- Frequency-dependent one-pole damping inside each feedback loop.
- Orthogonal stereo output projections.
- Per-line feedback gain calibrated against measured impulse T60.
- No allocation, locks, waits, or buffer copies in the processing path.

Separate stereo diffusion and alternating-sign FDN injection preserve excitation for antiphase L/R inputs; the host test asserts nonzero reverb tail for that case.

## API

```c
void gfx_space_init(gfx_space_state *state);
void gfx_space_process_sample(gfx_space_state *state,
                              float *left, float *right, float macro);
void gfx_space_process(gfx_space_state *state, float *left, float *right,
                       size_t frame_count, float macro);
void gfx_space_process_interleaved(gfx_space_state *state, float *stereo,
                                   size_t frame_count, float macro);
```

The processing functions are in-place. The interleaved variant expects `[L0,R0,L1,R1,...]`. The caller owns the state and should give it static storage on the MCU; the state consumes approximately 83 KiB.

The state-based API is suitable for host testing and adaptation to the engine's final working-buffer symbol/layout. No guessed global engine buffer is embedded here.

## Validate

```sh
make
make test
```

The test checks exact dry bypass while state updates, finite output, peak, estimated endpoint T60, stereo correlation, mono-fold-down energy, and antiphase input. CI uses strict C11 warnings. Listening and target-device WCET measurements are still required.

# GfX Effect Development — Context Seed

Read this seed and `docs/architecture.md` before designing an effect. The architecture document is authoritative for the product, audio schedule, control philosophy, and plugin contract. This seed records the proven development workflow from Space.

## Fixed contract

- Target MCU: ESP32-S31-WROOM-1U. Internal DSP 48 kHz; codec I/O 192 kHz.
- DSP processes 8 stereo frames in place per call. One block is 166.7 µs.
- Plugin API: `void effect_init(void);` and `void effect_process(float macro);`.
- Macro range [-1,+1]. Center is exact bit-exact dry passthrough at unity; effect state continues updating.
- Each axis has two distinct endpoint effects. Smooth parameters internally; do not make hard switches or zipper noise.
- No allocation, locks, waits, buffer copies, or parameter handoffs in the real-time process function. Persistent state is plugin-owned and initialized outside the hot path.
- WCET is the primary performance metric. Bound the longest feasible path, then validate on the target. Do not trade audible quality for premature optimization while comfortably inside budget.
- Mono compatibility is mandatory. Stereo must earn its complexity. Keep levels controlled and compare loudness fairly.

## Workflow for each effect thread

1. **Read the architecture contract and existing effect sources.** Reuse conventions and the test/listening harness where appropriate; do not blindly copy another effect's algorithm.
2. **Define the sound before code.** Specify endpoint identities, the normal macro region (0–0.7), the extreme region (0.7–1.0), response curves, dry/center behavior, level target, and mono behavior. List unresolved decisions rather than guessing.
3. **Choose the simplest credible algorithm.** Compare alternatives only where the sonic or WCET tradeoff matters. Avoid FFT, lookahead, extra buffering, or complex control machinery without a concrete reason.
4. **Build portable C first.** Use an explicit state struct and testable sample/block API, fixed memory, no heap. Keep target-specific engine integration separate until the algorithm is validated. The final adapter must conform to the global in-place `effect_init/effect_process` contract.
5. **Create two host harnesses:**
   - Automated test harness: impulse/step, silence, tones, noise, representative audio, finite/stability checks, peak and level behavior, parameter extremes/transitions, center bit-exact bypass with live state, and mono fold-down. Add effect-specific assertions (e.g. T60 for reverb, delay timing, modulation depth/rate, distortion transfer).
   - Listening harness: play a 48 kHz WAV through the effect, write raw float32 stereo audio to stdout, diagnostics to stderr, and accept live newline-separated macro updates on stdin. Keep it simple enough for rapid A/B listening.
6. **Use measurements, not intuition, to tune.** Generate CSV/metrics from the automated harness. Compare candidate curves/algorithms at matched perceived loudness. Inspect impulse response and numerical behavior before tuning by ear.
7. **Measure cost.** Compile strict C11 with warnings as errors, profile host behavior for regressions, estimate operations and worst-case branches, then measure target-device WCET. Host timing is not a deterministic WCET proof.
8. **Commit source, tests, listening tool updates, and a short effect README to `srycpt/Gfx`.** Add/update CI so the portable build and tests run on each push. Report what passed and what still requires listening or target validation.

## Space is the reference workflow

See:
- `effects/space/gfx_space.c` and `.h`
- `effects/space/README.md`
- `tests/space/gfx_test_space.c`
- `tools/gfx_listen/gfx_listen.c`
- `.github/workflows/space-test.yml`

Space demonstrates: portable fixed-state DSP; a bipolar room-to-hall axis; smoothed parameters; exact center bypass while state continues; independent stereo diffusion; explicit mono/antiphase tests; impulse-derived decay measurements; strict-build CI.

Space is a **workflow and harness reference, not a universal algorithm template**. Use the relevant test signals and metrics for each effect. Its current state-based API is a host-development API; do not assume it is already the final firmware adapter.

## Required output for each effect

- Brief sonic specification and macro curve.
- Algorithm choice and meaningful alternatives rejected.
- Portable C implementation with fixed state.
- Automated tests plus listening harness support.
- State-memory estimate and operation/WCET analysis.
- CI result, measured metrics, listening issues, and remaining target-specific work.
- Minimal adapter plan for the final plugin API.

Keep the implementation lean and the artifacts reproducible. Ask for user input only when a sonic decision or unresolved requirement genuinely blocks progress.

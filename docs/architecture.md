### Digital Guitar FX — Architecture Contract v1.0

Canonical project seed · October 2026

### 1. Product

A digital guitar effects pedal with analog-style immediacy: seven macro controls, three buttons (Memory 1, Memory 2, Live), no display, no menus, no amp simulation.

Target users range from home players to rehearsal and occasional-gig musicians. Prioritize recognizable, fundamental guitar sounds over effect-catalog breadth.

- Internal DSP: 48 kHz.
- Codec: 192 kHz.
- Target end-to-end latency: preferably near 0.5 ms, hard objective below 1 ms.
- Stereo-capable output remains under consideration; mono compatibility is mandatory.
- Target MCU: ESP32-S31-WROOM-1U.

### 2. Audio architecture

Core 0 — Harness
- Handles I²S DMA.
- Input DMA ring: 32 samples at 192 kHz.
- Decimates 192 → 48 kHz.
- Dispatches each 8-frame block to the DSP core.
- Handles output DMA.

Core 1 — DSP engine
- Dedicated DSP execution, raw-metal style.
- Processes the fixed 8-frame working window.
- Calls plugins sequentially, then interpolates 48 → 192 kHz.
- Writes output into DMA memory.

Working buffer
- Fixed address, owned by the engine.
- Eight stereo frames, float32: 64 bytes.
- No ring buffer needed for the working window.
- Plugins process samples 0 through 7 in place.

Existing sample-rate conversion code: `convert_192and48.h`. Treat as the initial implementation; revise only when testing demonstrates a need.

### 3. Scheduling and WCET

At 48 kHz, eight frames represent 166.7 µs. At 192 kHz, 32 samples also represent 166.7 µs.

The scheduling constraints are:

- Decimation on Core 0 must complete within its 166.7 µs period.
- DSP processing plus interpolation must complete within 166.7 µs on Core 1.

Decimation on Core 0 can overlap with DSP processing on Core 1. The DSP core must finish processing and interpolation before the next block requires it.

WCET methodology:
- Trace compiled machine code, not just source-level operations.
- Identify the longest feasible execution path.
- Account for instruction costs, branches, loops, and worst-case memory behavior.
- Sum plugin costs plus engine overhead.
- Use measurements to validate the analytical model, not as the deterministic bound.

### 4. Plugin API

Each effect exports:

```c
void effect_init(void);
void effect_process(float macro);
```

- `macro` is in [-1.0, +1.0].
- Each plugin processes the fixed eight-frame stereo working window in place.
- Persistent state belongs to the plugin.
- Macro smoothing/stepping is internal to the plugin.
- No return value, buffer copying, dynamic allocation, locking, waiting, or parameter exchange during processing.

### 5. Macro behavior

- Center (0): exact bit-exact dry passthrough at unity.
- Effect state continues updating while bypassed.
- Each axis has two endpoints; center is exact dry bypass.
- Left and right endpoints may map to independent character choices.
- Normal control region: |macro| = 0.0–0.7.
- Extreme/caricature region: |macro| = 0.7–1.0.
- Output gain calibration is deferred until algorithms are implemented and tested. At |macro| = 1, gain must not exceed +3 dB relative to center; derive the curve from test harnesses rather than guessing.

### 6. Engineering priorities

- WCET is the primary timing metric.
- Sound quality comes first while staying within the WCET budget.
- Avoid premature optimization; measure and optimize when evidence indicates it is necessary.
- Keep effects modular and independently testable.


### 7. Space effect contract

The Space axis is bipolar:

- Negative macro: intimate room/plate voicing, short diffusion delays, nominal T60 from 0.30 to 1.50 s.
- Zero: bit-exact dry output at unity; both room and hall states continue updating.
- Positive macro: expansive hall, with T60 extending toward 5.00 s.
- Wet mix increases with absolute macro, reaching 0.60 at either endpoint.
- Room and hall use independent eight-line FDNs with normalized Hadamard feedback, independent stereo input diffusers, and distinct stereo output projections.
- The room/hall character blend is smoothed through the center region to prevent a polarity-switch discontinuity.
- Feedback gains are calibrated against host impulse-response T60 fits; rerun those fits after changing delay lengths or damping.

The host test checks exact bypass while state advances, finite/stable output, peak level, approximate endpoint T60, stereo correlation, mono fold-down energy, and antiphase stereo excitation. These checks do not replace listening tests or target-device WCET measurement.

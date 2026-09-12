# Optional real-canvas recording source review

Reviewed `scripts/record_cove_visual.mjs` and the `VOXY_SMOKE_VISUAL_CAPTURE` hook in `scripts/smoke_integrated_wasm.mjs` without running a browser, game, test or image viewer.

The hook is opt-in and attached to the actual effects-control journey. The recording reads the real canvas, game observation and dimensions. It sends no fabricated game input or state mutation. It requests a 12-second, 30 fps WebM recording at 6 Mbps and writes one initial actual browser PNG. The main smoke timeout remains in force. Recording is correctly described as visual evidence, not a performance benchmark.

Two bounded findings were sent to root:

- The 20 MB output cap is checked only after chunks have already accumulated. Check the running chunk total too. Ensure track/timer cleanup also occurs on unsupported codecs, constructor/start errors and recorder errors; currently only `onstop` reliably performs cleanup.
- The smoke hook calls the finish function only after the journey passes. Finalize the bounded recording in `finally`, retaining the original journey failure, so an actual failed-control attempt can still preserve its recording.

The source alone cannot establish that this Chrome/WebGPU canvas supplies useful recorded frames or valid decoded video. That must be checked from the one real recording attempt. No product, visual or runtime pass is claimed by this review.

## Follow-up source verification

Root corrected both findings before execution. The recorder now caps retained chunks incrementally and cleans up tracks/timer on the reviewed codec/constructor/start/error paths. The promise has an early rejection handler. Smoke finalizes the recording in `finally` and preserves the original journey failure if capture also fails. No remaining concrete source finding was identified in this bounded follow-up. The helper still requests real canvas frames only; actual decode/support, measured duration and visual content remain runtime checks.

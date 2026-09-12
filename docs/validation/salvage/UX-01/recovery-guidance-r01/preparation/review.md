# D41 first-recovery guidance — independent semantic review

Reviewer: `/root/presentation_review`, 2026-09-12. Bounded read-only source review; no compilation, tests, GPU/application/browser runs or images. Root owns all runtime evidence and publication.

## Result

No remaining actionable finding in the reviewed D41 formatter/Application changes. Two concrete defects found in the initial paused-HUD draft were corrected before this review concluded:

1. While a requested harbor installation was pausing/uploading/admitting, `installationPending` could be true while `checkpointPending` was false. Historical `deliveryDurable` then advertised Resume even though the actual action gate refused it. `CovePauseFacts` now carries harbor/rescue/checkpoint state directly; `applyCovePauseGuidance` presents pending work before historical success and removes Resume until that work completes.
2. After a successful delivery, historical durable success replaced the current status of a later manual save, including recoverable save failure. The pause formatter now prioritizes current `Save failed.` and `Saving ` statuses before historical delivery/power success. Storage revocation remains the highest-priority recovery state. Actual harbor parking refusal remains visible with Resume available after installation cancellation.

`PausedPowerAndManualSaveOverrideHistoricalDeliverySuccess` is authored to exercise pre-checkpoint installation, blocked Resume, checkpoint failure, a separate later manual-save failure, manual progress, powered completion, rescue, harbor refusal and storage revocation, while preserving the economy line. Its source matches the corrected behavior; this reviewer did not run it.

## Authority and integration checked

- First-job J/H availability is sampled from actual outer/action guards. J remains available on foot. H calls the existing physical `eligible` callback with cargo found by the exact current cargo ID; geometric comparisons in the formatter explain refusal and never grant delivery permission.
- Delivery metrics refer to the observed cargo root in the installed fixture frame: horizontal distance plus the existing 1.1 m allowance, root height, full translational speed and angular-speed norm. Current installed limits are distance at most 3.9 m, height at least -1 m, speed at most 0.8 m/s and angular speed at most 1 rad/s. Being at the helm, having a cable attached, boat speed or clearing the entire load above water are not added as delivery rules.
- Pending hand-off, physical banked state and durable storage acknowledgment remain distinct. The formatter never reports a newly completed delivery/power save from `cargoBanked` or `installed` alone.
- K availability uses the real absent-stage/profile condition, saved delivery, storage readiness, primary body and action guards, plus off-boat three-dimensional feet distance at most 4 m from the dock controls. Later parking/space checks remain real runtime checks; the HUD cannot promise they will pass.
- Pause returns before running/workshop guidance; workshop input/content retains its own priority and action mappings. Running recovery guidance preserves the nearby E interaction and movement/build/pause/rescue hints.
- The added native HUD observation reads renderer-owned sampled content and escapes control characters, quotes and backslashes. It does not submit intents, recalculate readiness or advance gameplay. Existing fixed HUD GPU allocation and quad limits are unchanged.

## Review limits

The six focused formatter/layout cases and pre-existing HUD checks are source-reviewed only. Actual layout fit, native/browser integration, the real first-job/harbor journey, mandatory suite and any gate/commit status must come from root's subsequent results. This review does not add a screenshot requirement or authorize an unchanged gameplay matrix.

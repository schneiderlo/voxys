# Drum command acknowledgement baseline review

Read-only source review of the final `Application::render` rope-sample guard. No remaining actionable finding. No builds, tests, GPU runs or images were performed by this reviewer.

## Observed failure and cause

`native-journey-r04/summary.json` remains a failed journey. It reports reel-in rest-length change `-0.1333332061767578` m and phase discrepancy `-0.2380950110299246` radians. The expected full drum change is `0.4761900220598493` radians at the authored 0.28 m wrap radius; only half was retained.

Previously the render-time sample guard required `towObservedTick >= towChangedTick`. A successful motor command advances the command frontier before its new observation arrives. During that gap, rendering passed no rope sample even though the attachment and its last accepted observation remained valid. `CoveMechanisms::prepare` correctly interprets an absent sample as a stopped/detached observation route: it clears the baseline and advances `ropeFloor_`. The next accepted cable length then establishes a fresh baseline, discarding its initial real displacement.

## Reviewed correction

`src/app/application.cpp:2291` retains the last accepted observation while a motor command awaits acknowledgement. It still requires mechanisms to be running, a valid current attachment, no locally confirmed break, an alive and unbroken observation, exact current attachment-handle equality, and a positive observed tick. Only the render-time command-frontier comparison was removed.

`src/game/expedition/cove_mechanisms.hpp:54` integrates only strictly newer same-handle observations. Repeating the accepted baseline during acknowledgement contributes zero rotation; the next accepted length contributes the whole length delta once. Older observations remain ignored, and conflicting lengths at the same tick remain rejected.

The authoritative snapshot acceptance in `src/app/application.cpp:8677` still rejects observations before `towChangedTick`, checks exact attachment generation and body endpoints, and validates finite cable length/motor speed and allowed length range. No snapshot, command, physics, save or scanner guard was relaxed. Pause/workshop/checkpoint transitions still disable mechanisms and clear the integration baseline. Detach/new attachment handles retain their existing reset semantics.

## Validation limit

This review establishes the source-level cause and bounded correction. Root owns the rebuilt native/browser evidence and must verify the full `-deltaLength / 0.28` phase relation; this document does not claim that validation passed.

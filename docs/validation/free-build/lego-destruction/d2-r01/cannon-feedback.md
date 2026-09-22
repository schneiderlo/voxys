# Cannon feedback correction — 2026-09-18

Owner report: “nothing works.” A global input freeze was not reproduced: the
existing browser accepted cannon, motorbike, walking-mode and pause actions.
An out-of-range cannon refusal only displayed once; repeating the action after
the three-second toast expired appeared unresponsive.

The runtime now increments an interaction feedback serial for these actions.
The UI reopens identical refusal messages for a new action, without extending
them on ordinary frames. The distant cannon button displays distance in studs;
requesting it turns the view toward the cannon and explains walking/running.
The player is not teleported.

Successful manual release/removal selects a collision-swept close-up of the upper
facade. Aim inputs, a successful shot, rebuilding, or leaving restore the normal
view. Refused shots retain inspection. Camera observation now reports the actual
cannon camera rather than the inactive walking orbit.

The controls explicitly state that shots do not break walls yet. This is still
the bounded D2 manual experiment, not D3 impact-driven destruction.

Validation:

- 10/10 UI tests: repeated identical feedback after expiry, distance, inspection
  hints, honest shot limitation and settling message.
- Both actual-terrain/source-house integration variants pass, including close-up
  target/distance and rebuild assertions; see `cannon-feedback-world.log/xml`.
- Final WASM build succeeds. No physics implementation changed in this correction.
- Independent agent reviewed feedback and camera transitions; its failed-shot
  inspection finding was fixed before the final native/WASM runs.
- Updated browser preview `?experience=build&preview=cannon-feedback-r03&telemetry=0`
  visibly reports “Cannon · 17 studs”. Clicking shows the new walk/run instruction;
  after it disappears, clicking again displays it again. No console errors or
  warnings were captured in that preview. The older preview had to be closed
  to release its saved-world lock before loading the new one. This check does
  not establish the cause of the owner's report of all controls failing.

Full browser collapse acceptance, walkable opening, D3 impact activation and the
normal gate hook remain open. No gate commit or publication is claimed.

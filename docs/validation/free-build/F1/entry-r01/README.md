# Free building entry — F1, r01

The owner cancelled the adventure roadmap and requested free LEGO-style building
that feels good. This candidate implements the initial creative profile. F1 is
not a completed gate; ordinary play and save/reopen checks remain pending. A local browser candidate is available.

## Implemented

- Explicit `free_build.cfg`, `GameMode::FreeBuild` and `?experience=build`.
- New browser packages default to creative. Explicit adventure/Cove routes remain.
- Separate installed content identity and browser/native save namespaces. Creative
  never consumes/refunds building materials. Old worlds retain their own rules.
- Start in Build with Brick 2 × 4 selected. Ground-supported bricks can start a
  creation. Disconnected grounded builds share one ownership record initially.
- Remove adventure scenery, residents, enemies, resource chores and their HUD.
- Keep full fixed terrain, minifigure, pictured parts menu, rotate/remove/last-place
  undo, camera/comfort controls and explicit confirmed saves.
- Furniture remains available as decoration; hinged doors can open/close. Other
  furniture adventure interactions are not offered in the creative profile.

## Evidence and limits

`checks/core.log` and the four XML files record 163 passing CPU component cases:
76 authority/geometry cases, 14 door cases, 8 native storage cases, 65 config cases.
These include unlimited creative placement/removal without generating inventory,
terrain support vs floating/overlap refusal, creative configuration and isolated
native save/reopen while old saved bytes are preserved.

`checks/browser-ui.log`: 46 passing DOM-fixture checks, including hiding adventure
HUD and routing Continue/New to creative. `checks/browser-saves.log`: 17 passing
transport checks, including separate database and remembered-world metadata.
These are not browser rendering or real IndexedDB gameplay checks.

Native executable and runtime-test compilation pass. `checks/runtime-r02.xml`
records a passing creative integration case (5.107 s): actual full terrain, real
headless RADV WebGPU resources and synthetic library input place/rotate a brick,
undo it, and restore the exact accepted snapshot. No material changes, residents,
active trail sites or combat clock. This is not ordinary GUI play or a physical
controller check.

That run also records an old door test failure: its player at 2 m south of the
hinge could open the door, but the opened handle moved beyond the 3.2 m use reach.
The fixture now starts 1.5 m south, within reach of both poses. Gameplay reach was
not weakened. `checks/legacy-runtime-r03.xml` records the passing rerun (6.544 s).

No screenshots, owner enjoyment approval, final lighting/audio, multi-step undo,
paint, ordinary native play, performance result or completed F1/F2 gate claimed.

## Browser candidate and ordinary interaction

Final local candidate: `build-free-build/web-r02`, build
`free-build-31c84dbe8d5ac6ea`, 48 manifest files verified.
Open <http://127.0.0.1:42763/index.html?experience=build&telemetry=0>.
`checks/wasm.log` records the completed WASM build. One existing compiler warning
about the defaulted WorldPosition comparison remains; it is unrelated to this mode.

Official browser controls on r01 confirmed startup in Build with Brick 2 × 4,
Unlimited pieces and no quest/health/material HUD. A ground click too close to the
player was refused. A farther ground click placed a brick; Done showed “Placed.”
The next preview could show overlap immediately after acceptance, obscuring the
success. The r02 browser interface therefore shows the live count/1,024 and a
separate last-action message. Its 46 DOM component tests pass, including that case.

The final package opened in a separate tab, but both preview tabs then became
unavailable in the browser session. The ordinary save/reopen flow and final r02
visual inspection were not completed. No automated claim of user enjoyment,
complete control usability or successful F1 gate is made. No gate commit yet.
The first successful placement view was inspected once; no repeated screenshots.

# Adventure comfort and combat controls

This checkpoint adds remembered comfort settings and configurable Attack/Dodge controls to the existing warm minifigure adventure. It is partial ACCESS-A01 implementation; the overall accessibility and G-B/G-C gameplay gates remain open.

## Player behavior

Open **Menu → Comfort and controls**. Choose text at 100%, 125% or 150%, high contrast, reduced motion, mouse/controller look speed, look inversion, hold/toggle mouse look, and movement/look deadzones. **Attack and dodge controls** changes a keyboard key, mouse button or supported controller button. Conflicts are marked and refused; menu, movement and building controls retain their existing bindings. Reset and Save settings again are explicit actions.

Visible Attack/Dodge prompts follow accepted bindings. Held input must be released after a menu, focus loss, rebind or controller reconnection before it can fire again. Completed short key/mouse taps still work. Mouse inversion affects held and completed right-button drags; ordinary mouse movement does not rotate the camera in hold mode.

Settings are separate from world saves. Linux uses `adventure-preferences-v1.json` under the resolved Adventure save root (optional `VOXY_ADVENTURE_PREFERENCES` override). Browser settings use `voxys.adventure.preferences.v1` on that browser origin. Browser storage failure keeps the current visit usable and reports that settings could not be remembered. A fresh load does not overwrite a damaged profile. A later explicit change/reset/retry can replace it. Existing Cove preferences remain separate.

## Implementation contract

- `adventure_preferences.*` owns the strict, bounded 4,096-byte version-1 codec and semantic combat input router. Unknown/duplicate/missing fields, invalid values, foreign profiles and conflicting bindings refuse atomically. This new Adventure profile does not migrate or reinterpret Cove's profile.
- `adventure_runtime.*` applies settings to shared input/camera handling, publishes immutable menu intents and a decimal-string preference revision, and exposes the bounded host action API. Setting changes while paused do not change the world revision or serialized archive.
- `adventure_preferences_store.*` implements Linux file publication with a restrictive temporary file, file flush, rename and directory flush. Invalid or unavailable reads retain current values; uncertain durability is reported separately. Other native platforms report unsupported persistence.
- `web/adventure_preferences.js` transports only canonical core settings and persists once per explicit revision. It never grants gameplay actions or polls storage repeatedly. The existing accepted UI snapshot may omit top-level `ready`; an explicit false/failed state remains unavailable.
- Both build systems register the code/tests. The actual WASM host exports `adventure_preferences_action`, and preview packaging checks that export.

## Evidence

The retained logs in `checks/` record nine core settings/input cases, six Linux storage cases and twelve existing Cove-input cases passing. They cover strict parsing, reserved/conflicting controls, release-to-rearm, short clicks, pad reconnection, file replacement and refusal paths. Store reopening uses independent objects and closed descriptors; it is not an operating-system process-restart test.

The native and WASM executable builds pass. The expanded real-terrain headless runtime integration passes in 5.347 seconds. It uses six published preference-menu choices, compares the entire paused AdventureState and archive bytes, independently parses the resulting native settings file, refuses malformed/conflicting changes, and checks four held/completed mouse gestures with normal and inverted yaw. It reuses the existing terrain/quest/recipe integration instead of starting a second GPU harness. This is library-input evidence, not a physical-controller or windowed player journey.

Eleven browser preferences cases and forty UI cases pass after the readiness fix. The added integration case uses the actual UI and preference modules with the production-shaped snapshot (no top-level `ready`, and `quest.ready: false`), activates an encoded menu row, verifies the saved status and requires only one write through steady polling. Its core/storage are still test doubles; the C++ codec and real browser check provide separate coverage.

The corrected package is [available here](http://127.0.0.1:42759/index.html?experience=adventure&telemetry=0): `build-adventure-g-c/web-r05`, ID `adventure-8dda99b275e01b17`. All 47 manifest file hashes match. Its native companion is the unchanged `build-adventure-g-c/native-r04/voxy_native`, SHA-256 `a99c6db70194e67871950e78f45242cfe9e5bb1681f2552fb3232d02dde92b6a`. JavaScript-only corrections required packaging, not another executable build.

Ordinary CUA browser interaction on isolated port 42759 changed Text size from 100% to 125%, opened Attack and dodge controls, and rebound Dodge from Q to F. The UI reported **Settings saved on this device**. A separate fresh game tab on the same origin started with **Dodge · F** and showed **Text size: 125%** and the saved status in its settings menu. No preference state was injected through developer code. The first tab's reload command left its current menu unchanged, so that command alone is not counted as a completed reload. The passing persistence evidence is the new game instance reading the stored preferences. No gameplay world was saved, loaded or overwritten for this check. No screenshots were taken.

The final evidence, source hashes and exact scopes are in [results.json](results.json).

## Retained failures and limits

The first focused compile stopped at an extra brace in a test initializer; that was corrected before the passing run. Review found and corrected dropped between-frame mouse clicks and completed right-button drags. The first real browser settings check then exposed a readiness mismatch: the preference bridge required `ready === true`, while the production snapshot omits that field. Earlier fixtures supplied the field and therefore missed the integration defect. The correction passes the UI-plus-transport regression and fresh browser-instance checks described above. The initial local preview server launch was refused by the filesystem/network sandbox; its authorized local-only escalation succeeded. No approval-review rejection occurred.

No physical-controller accessibility session, full native quest journey, owner visual approval, Windows settings persistence or completed gameplay gate is claimed. World schema remains 5 and content identity remains `b989f53fd82527a42f3e60582f9bb0cdcafced9cfea94b53074cccf37eca290c`.

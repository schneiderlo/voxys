# Cove menus, controls and learning

This record covers the owner's UX-02–04 request, decision D49. The baseline is
`c5300d05e0f4a2fb3bfd7466925fd0790ceb7e2c`. The implementation is for the
existing native Linux and browser Cove. Whole-game prerequisites and human
accessibility acceptance are separate from these implemented components.

## Player behavior

- Open the expedition menu with **P**, controller **Menu**, or **Escape** while
  walking or sailing. Native offers job board, nearby map, inventory, camera,
  controls/accessibility, help, saved expeditions and an explicit quit choice.
  Browser provides these pages through **More controls**, plus the landing
  page's New/Continue/saved-world links.
- Pause waits for the final neutral movement tick and its completion. Menus,
  settings and save status remain usable. **Resume** returns control without
  replaying held buttons. A paused workshop after Test also has a Resume path.
- The paused Job page offers an explicit **Resume** while keeping the job
  visible. Resume does not accept a job or deliver cargo; the separate action
  still waits for the real eligibility checks.
- **F2** opens camera options during play, or the workshop tools while building.
  Menu navigation, Confirm and Back remain fixed inside menus. F2 and controller
  Menu retain an entry path; F9 remains the engine's display pacing shortcut.
- Settings provide complete action rebinding, alternate keyboard chords,
  controller buttons, mouse/stick speed, independent stick deadzones, look
  inversion, press-to-toggle reeling and mouse orbit, 100/125/150% text, high
  contrast, optional guide cards and event captions. Reduced motion remains an
  actual camera setting. Conflicting bindings refuse as one complete change.
- The map lists actual dock, delivery harbor, observed boat and generator
  positions. It is a nearby navigation aid, not a generated world map.
- Short guide cards follow walking, opening the workshop, keeping an edit,
  testing/launching, boarding, sailing and a confirmed save. Help can be repeated
  and the guide restarted or hidden. Guide progress is presentation history for
  the current run; it never awards resources. English is the installed language.

## Test sail and save safety

**Test sail** uses the kept connected draft, excluding the next unused brick
preview. It works without buying the draft. The original complete Cove archive,
editor/history, accepted design, player and camera are retained in RAM. The
actual physics replacement uses the existing bounded preparation, admission,
completion and retirement path. It does not send an economic refit command.

During Test, the original canonical GameSession remains frozen. Interaction,
sailing, towing, camera and pause work. Jobs, delivery rewards, harbor purchases,
cutting, blueprint mutation and expedition saving refuse. Return verifies the
canonical checkpoint bytes, restores the original physical initial conditions
and editor, and ends in the paused workshop. One joined maintenance tick can
integrate the restored physical state; exact post-integration float equality is
not claimed. Original ownership, resources and editor history are protected.

Quit/Leave discards the RAM test. Restart loads the last confirmed normal
expedition save; an unsaved draft still needs the explicit design-library save.
Normal **Launch** continues to use authoritative resources and durable owned
parts. UI confirmations explain unsaved-work loss; they do not silently save a
different draft or alter the saved slot.

Native New/Load holds the selected slot's exclusive storage owner and checks the
entire candidate against trusted installed content, catalog, terrain, player and
physical restore rules before retiring the current Cove. The old world drains
before its window/GPU are destroyed and the staged application initializes.
A missing, locked or incompatible selection keeps the current paused world.
Device failure during a later new application initialization is still a runtime
failure; the prior normal disk save remains the recovery point.

Browser Continue/history stores at most 32 confirmed world IDs in optional local
metadata. IDs enter history only after real save/load acknowledgment. Existing
bookmarks and the strict world loader remain valid. History metadata is not a
save or an ownership grant.

## Implementation contract

- `CoveInputPreferences` has a strict version-1 JSON schema, bounded to 32 KiB.
  Duplicate/unknown/missing fields, invalid ranges and context-overlapping
  bindings refuse without partial mutation. All 71 action IDs have labels and
  keyboard/pad defaults. Fixed menu escape routes cannot be stolen.
- `CoveInputRouter` samples once per display-input frame. World, workshop and
  modal contexts prevent controls leaking between screens. Physical held keys
  survive logical reset until release; repeats, focus loss, reconnect and
  preference changes cannot create a new gameplay edge. Deadzones are applied
  once by the platform gamepad filter. Continuous/toggled winch control is
  neutralized across focus/menu/pause transitions.
- Native preferences are an optional bounded sidecar beside the configured
  design directory (`cove-preferences.json`), outside the expedition ledger.
  Explicit Apply uses atomic replacement/flush semantics on Linux. Browser uses
  optional localStorage after the core validates the whole candidate. A storage
  error keeps the applied settings for the visit and never revokes a world save.
- `CoveOnboarding` accepts finite, monotonic observed facts, with stable text
  keys and an English fallback catalog. Captions come from actual state changes
  such as boarding, helm, swimming, cable state, launch, test return and durable
  delivery. They expire on accepted ticks and pause with the world. This is
  event text, not an implemented audio/subtitle backend.
- Browser `cove_menu.js` and `cove_preferences.js` use the existing controller
  bridge, not a second gamepad poll. Native `NativeWorkshopMenu` owns navigation,
  settings and confirmation screens. At large text sizes the native naming
  keyboard scrolls its existing letter grid without shrinking text or hiding
  its action buttons.
- `voxy_cove_preferences_action` reads/applies/resets preferences;
  `voxy_cove_save_completed` observes an actual host acknowledgment. Practice
  actions 340/341 begin/return, 350 requests the normal save, 351 opens the job
  page, and 352/353 restart/toggle tutorials. Native host-only actions 354, 360
  and 4000+ select quit, new world and an enumerated saved world.

## Validation

The [native runtime record](native-runtime.md) composes seven retained menu/input
records with seven final world-switch/restart records and five targeted
Job/Return handoffs. The final two native runs pass on r06. This preserves the
earlier failed attempts as failures and does not replay the completed prefix.
Both application builds pass; the final browser package is `web-r04`.

The required suite passes on the final source: **2,243 game tests passed,
3 skipped and 4 disabled**; unchanged terrain import **10 passed, 1 skipped**
from valid cache. Bazel completed in 1,126.206 seconds. The skips are window
creation, dense-grid overflow benchmark and LEGO-horizon benchmark; none is
counted as a pass. [Exact result, logs and XML](checks/mandatory-r02-summary.json).

Core cases cover atomic preference validation/storage, input context and
reconnect neutralization, actual-fact tutorial progression, and six practice
rollback/economy cases. Browser UI cases cover menus, controller ownership,
optional persistence, Save permission and design-library restrictions.
All **138 focused browser UI cases** pass.
[Browser controls and retained failure history](browserchecks/browser-runtime.md)
record six completed main stages, the actual saved-picker reload and the final
two-stage Resume/exact original-blueprint continuation. The final continuation
passes in 2.213 seconds with no browser/GPU errors and normal browser/controller
exits. The saved world, original eleven-part boat, resources and settings match.

The first required suite was stopped after one existing input-flood test failed.
Its duplicate held-key Down calls are intentionally filtered by the new physical
key tracking, so that sequence no longer fills the keyboard queue. The test now
sends real Down/Up pairs, preserves all fail-closed assertions and verifies real
release/repress recovery. Production input is unchanged. All 33 input and 12
preference cases then pass in the standard configuration. The accidental earlier
opt-config attempt failed unrelated dependency compilation and ran no tests.
[Interrupted suite record](checks/mandatory-r01-interrupted.json),
[focused correction records](checks/input-flood/index.json).

The final required command is:

```sh
nix-shell --run 'bazel test //tests:voxy_tests //tools:terrain_diffusion_import_test --test_output=errors'
```

[Source/build inputs](checks/source-inputs.json),
[initial build and focused-check records](checks/initial-checks.json),
[final build/delta records](checks/final-delta-checks.json) and the
[independent source review](independent-source-review.md) retain provenance.
The containing normal-hook commit is the verified component checkpoint. Its
parent is the baseline above; the user-authorized publication targets `main`.
The exact locally served package is `build-cove-ux-r01/web-r04`, with its
[27-file manifest](browserchecks/checks/web-r04-package.json). Local publication
metadata records the actual commit, remote identity and preview address after
push. The existing browser tab is not automatically reloaded.

Remaining parent work includes full UX-01/PLAY-03, production SAVE-04, the second
job/PLAY-06, translations, human dyslexia/accessibility sessions, physical-device
usability review, co-op pause rules and Windows durable preference/storage work.
No unchanged screenshot matrix is required for this component.

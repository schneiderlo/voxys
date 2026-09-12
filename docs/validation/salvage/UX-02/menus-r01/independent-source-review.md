# Independent source review — Cove menus and practice

This review covers the UX-02–04 candidate's input ownership, menus, settings, save permissions and practice integration. It is source review, not runtime or gate acceptance. No remaining actionable finding was identified in this bounded scope after the corrections below.

## Scope

- Application input routing, preference application, paused/menu/focus handoffs, save requests and the practice action whitelist in `src/app/application.cpp`.
- Native menu opening, controller/keyboard capture, paused workshop Return and dynamic binding presentation in `src/engine/platform/native/workshop_menu.*` and `Application::nativeCoveHudContent()`.
- Browser `web/cove_menu.js`, `cove_preferences.js`, `controller_menu.js`, `cove_saves.js`, `salvage_preview.js`, their HTML/CSS integration, and the design-library practice guard.
- Full preference updates preserve the core's atomic validation. Optional local preference/history metadata does not publish expedition state. Saved-world links retain the current origin/path and use validated world IDs; confirmed history is recorded only after the existing successful publication/acknowledgment paths.
- Menu ownership and cleanup, remapped Pause/Save inside the general browser menu, controller section navigation, stale-action permission checks, and settings/text-entry exclusions.
- The native saved-slot transition in `src/engine/platform/native/cove_saves.cpp` and `entry.cpp`, plus the optional `cove_preferences_store.*` boundary. The target worker retains its own store lock and fully preflighted archive before requesting Leave. Transfer requires completed old-world drain, and old GPU/window shutdown precedes new initialization. Failed target preparation preserves the current world. Preference reads preserve their output on refusal; a post-rename directory-flush failure reports Uncertain rather than Saved and does not revoke expedition authority.

## Findings corrected before freeze

1. **Paused Return could trap the workshop.** Practice Return intentionally ends paused with the original editor open. The World-only Pause action and native outside-workshop menu guard initially prevented the expected Resume route. Application now uses World input context while paused. The native menu permits paused-workshop P/Menu/Escape to open the Game page; Return to building resumes without issuing another workshop-open command. The focused `PausedWorkshopReturnCanOpenGameAndResumeWithoutOpeningTools` case also retains ordinary running F2 tools behavior and a refused Resume.
2. **Save was offered for a state the core refuses.** Browser Save now disables for an open workshop and explicitly closed session admission. Its global request bridge re-reads these permissions and `status().canSave` agrees. The new save case checks the returned paused workshop, closed admission, no publication, and explanatory status.
3. **Practice displayed normal recovery instructions.** The browser objective now gives the active practice phase/message priority over pause/job/reward guidance and promotes Return only when its existing control is permitted. Its focused case checks the paused Return transition against an otherwise available job.
4. **Ignored required input result.** The preference/input integration initially ignored the `[[nodiscard]]` dead-zone setter result. Root corrected the compile issue before the final application build.

Focus loss still reaches the input router while paused, so it clears held/toggled winch intent before the paused early return. Resume resets input ownership; the reviewed route does not carry a pre-menu held command back into gameplay. Native HUD binding hints now use the configured action labels. Optional event captions do not replace blocked/waiting refusal text; they are state-event captions, with no audio-caption claim.

## Later bounded UI follow-up

The browser author added an owner-scoped resize handler after an actual small-window journey found the focused Apply control outside the viewport. Source review confirms it scrolls the same available focused control, or repairs unavailable focus, only while that menu owns input. It never clicks; cleanup removes the listener. Its focused source case checks retained focus, scrolling, no activation, and no focus theft after release. Whether it resolves the measured runtime failure remains an actual-journey check.

The browser's explicit paused Resume button forwards the existing Pause control and retains a non-Overview page such as Job after Resume. Its source regression checks that Job stays open. The reviewer found no actionable issue in these two browser deltas and did not rerun their author-reported Node checks.

The presentation/core agent then **authored**, rather than independently reviewed, the matching native Job-page change in `workshop_menu.cpp`: Resume expedition appears while paused, uses public action 91, and keeps Job open on success or refusal. Accept/Deliver still require fresh authoritative permissions; Resume never dispatches a job action implicitly. Root owns independent approval of that delta. The agent's new `MenuFixture.JobResumeKeepsPageAndRequiresFreshPermissionBeforeAcceptOrDeliver` regression passed **1/1, zero skips** in the CPU-only `//tests:native_workshop_menu` target. It covers disabled Resume, an authoritative Resume refusal, accepted Resume without premature Accept, and delivery remaining disabled until the actual eligibility fact changes. Evidence: `build-cove-ux-r01/checks/native-job-resume-r02.log`, `native-job-resume-r02-test.log`, and `native-job-resume-r02.xml`. The earlier r01 log is a sandbox denial of the Nix daemon socket before compilation; r02 used the authorized existing Nix environment.

## Native driver oracle correction

Root independently reviewed the native Job Resume dispatch and regression after
the agent's implementation. The explicit action preserves the page, handles a
refusal, and leaves acceptance/delivery gated by fresh host facts. No further
actionable source finding was identified in that delta.

An earlier reviewer request incorrectly treated `assetFixture.sceneSunShadows` as current workshop visibility and required it to be false after Return. Actual native evidence contradicted that assumption. The field exposes `BlitPath::didUseSceneSunShadows()`, a last-use status; the workshop bypasses that composition and can retain its earlier true value. This was a **review/driver oracle error, not a rendering defect**. Workshop captures must instead use the explicit workshop mode and zero submitted scenery/dock draws, without constraining that stale Blit status. Normal scene captures can still require the real shadow-use flag. The author preserves the failed attempt and labels any reused completed Return snapshot explicitly; it is not a new runtime or frame-completion pass.

## Browser runner follow-up

The resize acceptance driver now observes a matching real resize event and its following browser animation frame before measuring the focused control. Source review confirms it does not scroll or repair focus itself and retains the exact viewport bounds and obstruction checks. A retained-settings continuation refuses unless current core preferences exactly match a previous report with a passed controller Apply stage; absent preferences require a fresh Apply, not an assumed continuation.

The retained-profile runner previously used unconditional SIGKILL, so a successful Apply in one process was not sufficient evidence that optional profile metadata survived its shutdown. The replacement requests Browser.close, then uses bounded termination fallbacks and records the actual exit. Close is only a flushing opportunity; persistence still needs a later read. Review also required a bound on the forced-kill wait and an explicit distinction between process exit and completed pipe closure. These are harness changes and source-only review; the already executed runs retain their exact earlier runner identity.

## Practice review attribution

The presentation/core agent implemented `CoveTestSession` and its Application adapter, so this document does **not** label that agent's own implementation reading an independent approval. Root separately reviewed that adapter and reported no new data-safety issue: retained editor/scene lifetimes survive the swaps, original physical root/cargo restoration commands are joined, and the frozen canonical `SessionSaveCodec` proof is checked before publication. Practice cannot pay, award, allocate canonical identities, or publish a practice save.

Return restores the original authored poses and velocities as initial conditions and then observes the required maintenance physics tick. It does not claim bit-identical post-step floating-point poses. The original workshop object, including kept draft/history and unused brush preview, is retained. Quit discards RAM practice and keeps the last normal durable save; an unsaved local draft is not a durable library record.

## Evidence and limits

- The reviewer ran the six `CoveTestSession` CPU cases once after the initial test-only bounded-index conversion compile correction: **6 passed, 0 skipped**. Evidence is retained in `build-cove-practice-r01/checks/cpu-r02.log`, `cpu-r02-tests.log`, `cpu-r02.xml`, with exact core/test hashes in `build-cove-practice-r01/core-inputs-r02.json`. Cases use the installed catalogue, actual accepted delivery economics and authoritative refit logic.
- The browser/native authors reported their focused Node/CPU checks separately. This reviewer inspected the final Save/objective/paused-workshop regression source without rerunning it. Author reports are not substituted for a new independent execution or for the final combined build.
- No application launch, browser journey, GPU test, image, layout measurement or full repository suite was run for this source review. Real Test/Return, remapped input, menu navigation, durable reload and clean retirement remain the scheduled runtime acceptance. Broader UX completion, mandatory-suite status and publication are tracked by root separately.

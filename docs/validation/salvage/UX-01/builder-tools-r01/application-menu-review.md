# Application and native menu integration review

Bounded read-only review of the new group/controller/menu integration in `src/app/application.{hpp,cpp}`, `src/engine/platform/native/workshop_menu.{hpp,cpp}`, its six focused test cases, the new menu portion of `src/render/cove_hud.{hpp,cpp}`, and the narrow F10 owner guard in native Cove saves. No app, build, GPU or screenshot was run for this review.

Two actionable integration findings were sent to root and the native owner before final builds:

1. `Application::updateNativeWorkshopMenu` supplies logical window dimensions to the menu hit layout, while `renderNativeCoveHud` supplies framebuffer/swapchain dimensions to the actual HUD. GLFW cursor coordinates are logical. Because `uiScale(height)` is clamped and nonlinear, the two layouts are not proportional on a high-DPI window. A click can miss or activate another row. Use one explicit logical-to-framebuffer pointer conversion and the rendered viewport dimensions, or one shared logical layout. Add a 2× viewport/pointer CPU case.
2. `renderNativeCoveHud` still refreshes content at 10 Hz while menu state and its hit layout update every frame. Immediately after a page change, a click can be resolved against a different menu than the one displayed. Refresh modal/opening/closing menu content immediately, retaining the existing bounded observational HUD sampling outside menus.

No additional actionable defect was found in the reviewed group action dispatch, selection highlight predicate, exact kept-design quote/refit path, direct continuous-controller placement stock lookup, modal opening/closing frame ownership, native F10 guard, pure camera actions, library callback routing or quote invalidation on workshop reopening. Group actions dispatch to the approved core API and retain the existing authority guards. Controller Place uses `placeBrickTool` and next-stock lookup against the preview; explicit Keep remains one-shot. Final Launch recomputes the exact quote and refit request independently of the menu cache.

## Reviewed corrections

Root and the native owner fixed both findings. The Application now passes exact swapchain dimensions and an explicit framebuffer/logical pointer scale to the menu; the menu multiplies its input pointer by that scale. The added `HighDpiPointerHitsTheExactFramebufferRow` CPU case chooses a nonfirst rendered row through 2× logical coordinates and requires the correct page. Immediate HUD refresh now applies whenever the native menu is active or the previously rendered content still contains a menu, covering opening, navigation and closing. The menu content branch precedes the unnecessary world snapshot.

The corrected source has no remaining actionable findings in this bounded review. The new high-DPI case was read but not run by this reviewer; root owns its verification. Limits: this is source review, not evidence of actual device/DPI behavior, browser controller operation, integrated native naming/file exchange, publication or a mandatory gate. The prior core CPU checks and independent library review are recorded separately in `HANDOFF.md` and `native-library-review.md`.

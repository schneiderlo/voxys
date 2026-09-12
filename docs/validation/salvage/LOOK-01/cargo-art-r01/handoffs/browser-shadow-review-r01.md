# Browser continuation r01: rendered shadow observation

The actual first D44 browser continuation failed in 1.749 seconds, after its
first paused-restoration stage passed. The raw failed summary remains unchanged
at `browser-continuation-r01/summary.json`; the original outer report is
`browser-startup-r01.json`. The outer run failed with `browserErrors=[]` and
`sample.errors=[]`. The paused captured state showed nine presentations, an
owned fixture of 9,731,960 bytes, normal world shadows, and later actual GPU and
physics completion. The failure state taken moments later was running outside
Workshop with shadows true again. This is not a completed runtime pass.

The source contract explains the failed assertion. Application's
`assetFixture.sceneSunShadows` reads `BlitPath::didUseSceneSunShadows()`.
`BlitPath::render` resets `usedSceneSunShadows_` to false at entry and sets it
when encoding the geometry-water pass from the current scene background.
It describes rendering activity, not a persistent logical world-mode setting.
The original driver demanded the flag equal the expected mode during every
poll, across render, discard and transition updates. The raw failure does not identify
the exact boundary at which its false sample was taken.

The bounded driver correction removes only that per-render flag from the
intermediate invariant. Each capture now waits for its explicit requested
workshop/pause mode and the expected shadow flag, then requires a strictly
later submitted serial under that same mode and owner. It freezes that later
serial and requires its actual GPU completion plus physics completion of that
later observation's mechanism tick. Both the matched sample and subsequent
submission/completion points are recorded. The expected logical mode stays
fixed during those waits. The flag is not atomically tagged to a submission,
so this is a matched status sample followed by later same-mode submitted and
completed work; it does not pretend to be a per-submission shadow counter. All identity, exact ownership,
presentation count, 16 MiB cap, environment, draw-work, pending/game-state and
blueprint checks remain intact. The proof records the captured shadow value,
mode and submission serial explicitly.

This changes the driver to match the existing observation contract; it is not
a product, shader, compatibility or performance fix. The r01 failure is retained
and no subsequent runtime pass is claimed. The exact corrective source is in
`runtime-shadow-r03/driver-capture.patch`, with before/after hashes alongside it.
Root owns integration and one actual corrected continuation, followed by the
single completed final required suite for the coalesced D39–D44 checkpoint.

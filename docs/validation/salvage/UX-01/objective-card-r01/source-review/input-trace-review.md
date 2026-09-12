# Actual D42 event trace: scoped interpretation

The second actual browser objective journey passed all five stages in 3.033
seconds on the unchanged frozen web-r01 package. The only changed driver adds
passive document capture/bubble event records and read-only geometry snapshots
around the same single physical click. It replaces no handler, adds no retry,
and calls no gameplay setter or substitute action.

`browser/r02/summary.json` records one trusted pointerdown/mousedown and
pointerup/mouseup/click at (143.74609375, 315.5390625), all targeting the enabled
`salvage-objective-action`. Its bounds stay x=57, y=297.1171875,
w=173.4921875, h=36.84375 before, during and after dispatch. Focus moves from the
canvas to that button after the press. No recorded input event is prevented.

During the trusted objective click, exactly one untrusted DOM click targets
`salvage-job-accept`, as intended by the product controller's existing-handler
forwarding. It appears once in document capture and once in bubbling; these are
two observations of one forwarded event, not two actions. Its zero pointer
coordinates and resulting canvas hit-test are normal for this programmatic DOM
click, not evidence that the user clicked the canvas. The actual job changes
to accepted with unchanged ownership. Physical B then hides the card in the
workshop and restores the Board guidance afterward.

This proves routing in r02. It does **not** explain r01. The first run retained
final game/DOM states but no event boundaries, so a missed physical click cannot
be distinguished from an early action refusal. The added read-only evaluations
change timing; the successful trace has about 661 ms between before-move and
after-move, but no matching first-run measurement exists. No UI, application,
permission or CSS fix was made, and no such fix or causal timing diagnosis is
claimed. The failed r01 evidence remains intact.

Executed traced driver SHA-256:
`cc722aadc5ef8a08dc816b45c59e7506990df311b337e97e3109803788cc9295`.
The exact before/after hashes and patch are retained alongside this review.
Each of the five successful states has later GPU/physics completion proof with
unchanged world/body and exact starter stock. The combined outer browser run
and following mechanism journey are still pending at this review's preparation.

Later outer completion: root confirmed process exit zero and outer status passed.
The same session's following mechanism journey passed 28 stages in 84.663 seconds;
browserErrors and sample.errors are empty, with no console errors. The README
links the original shared reports. This does not change the unresolved cause
of the first missed click or claim a product fix.

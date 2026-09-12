# D42 independent semantic review

Reviewer: `presentation_review`, 2026-09-12. Read-only source/test inspection;
no Node repeat, browser, game, GPU, image or screenshot run by the reviewer.

Two findings were corrected before final handoff:

1. Initial refresh briefly hid/disabled the promoted button before restoring
   the same action. That could steal keyboard focus. Final refresh applies only
   the resulting state. The focused regression observes no visibility/disabled
   transition and no live-region mutation for unchanged state; a real pending
   transition clears the action. Title/detail/label writes are also idempotent.
2. Storage revocation can leave older original controls enabled while
   `session.admissionOpen` is explicitly false. The card now gives unavailable
   expedition guidance with no action, including accepted tow, durable power,
   paused and stale-click paths. An already-pending Leave keeps its own waiting
   text. Missing optional admission observations preserve compatibility.

The reviewer verified both final fixes and their regressions, then reported no
remaining actionable finding in original-handler dispatch, exact step/target
revalidation, optional tow/lift handling, drawer-only focus without motor input,
Workshop/inspection hiding, cleanup or source-level CSS wrapping.

The implementation agent ran the final focused Node check once after those
review-driven changes: all ten new and 26 existing assertion groups passed.
Raw stdout and the exact command/result are in `checks/node-ui-r02.*`.
No actual browser action or measured viewport/visual acceptance is claimed.
Root owns integration and the short real-browser Accept→Workshop check.

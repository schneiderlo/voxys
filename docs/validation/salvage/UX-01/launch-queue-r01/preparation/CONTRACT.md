# D43 launch upload queue retry

Scratch-only candidate. No builds, tests or application runs yet.

The accepted launch candidate owns all compiled roots and upload payloads before GPU admission. Queue Busy and initialization NotReady preserve that candidate and its remaining payloads. The existing resource poll runs before session preparation polling; only missing shape handles are uploaded on each poll. Every accepted handle remains on its matching root, even if a terminal error follows. None means every actual shape is Ready, never merely submitted.

No new state, allocation during preparation polling, queue limits, schema, input, readiness or joined-stage rule. Terminal errors cancel through existing partial-handle retirement. Publication still waits for old-body death, new-body observation, joined physics/event frontiers and existing player/space/ownership checks. Leave/session closure retains the existing cancellation/drain behavior.

Root applies application.patch separately against evolving Application; helper.patch covers only roots header/source, their direct physics build dependency and the existing Cove CPU tests. D41 must preserve the final App change on later rebase.

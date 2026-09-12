# D43 launch retry under resource backpressure

Source-only candidate; no builds, CPU/GPU tests or application runs performed. Root owns integration, verification, the required final suite and publication. Live files are unchanged by this worker.

## Apply

Apply `helper.patch` and the separately localized `application.patch`. The latter changes only `configureCoveLaunch`'s prepare/poll callbacks; do not copy the whole Application overlay over later D41/D42 edits. `base-sha256.json` records the exact copied baseline and `candidate-sha256.json` pins all five overlay files. Existing CMake sources already contain the changed units. The sole build change is a direct `//src/physics:physics` dependency for the root helper's public resource interface.

Changed files are `src/game/expedition/cove_rigid_roots.hpp`, its `.cpp`, `src/game/BUILD`, `tests/test_cove_player.cpp` and the separate Application hunk. No assets, schemas, inputs, GPU limits or additional runtime fields.

## Cause and behavior

A fresh Launch compiled every root, then attempted all uploads immediately in its preparation callback. Any refused handle canceled the whole candidate. The real GPU store has eight pending-operation slots, while uncapped browser rendering can have more outstanding frames; `Busy` is temporary admission backpressure. Its public `upload(AuthoredShape&&, error)` contract preserves the supplied prepared value on refusal.

`prepareBuild` now completes every fallible CPU preparation, retains the candidate and returns Pending. `pollBuild` runs after `updateCovePhysics` calls the existing resource owner's `poll`. It invokes `CoveRigidRoots::prepareShapes` on the retained payloads. That helper:

- Checks payload/root count and existing bindings before taking another GPU handle.
- Uploads only roots lacking a shape handle; earlier accepted roots are never uploaded again.
- Preserves Busy/NotReady as pending, with the untouched remaining prepared values.
- Stores every returned handle before classifying the error. In particular, the real store can return an inserted valid handle with GpuFailure; cancellation still owns it.
- Requires actual resource state Ready for every shape before returning None. Uploading remains NotReady.
- Accepts an empty payload span after every shape handle exists. Application may free moved payloads while waiting for the uploads to become Ready.
- Does not poll, copy shapes, allocate, admit bodies, advance ticks or retire anything itself.

Terminal errors cancel through the existing launch retirement loop: every valid partial handle stays on its exact root, is retired and remains owned until Missing. Existing stageBuild checks remain unchanged, including joined scheduled/encoded/completed frontiers, player/space/harbor guards and atomic body replacement. Existing observedBuild still requires new-root observation and every parent-death event before publication. GameSession retains Pending preparation and reserved ownership; session closure still marks an unstaged candidate canceled. No debit/design is published merely because upload succeeded.

## Focused checks authored, not run

Two cases in the existing `//tests:cove_player` target use a clearly labeled scripted resource implementation and an actual compiled two-root Cove boat:

- `CoveMovement.LaunchShapesRetryBusyWithoutLosingPayloadOrDuplicatingAcceptedRoots`: initialization and upload NotReady; first accepted root then Busy on the second; exact untouched remaining payload/storage; retry only the second root; empty payload after both handles; Uploading cannot return Ready; an explicit fake resource poll permits readiness; keys remain exact and no bodies/joined tick exist.
- `CoveMovement.LaunchShapeTerminalErrorsRetainEveryPartialHandleForCancellation`: malformed count refuses before any upload; Capacity preserves the unsubmitted second shape; GpuFailure with a valid second handle retains it; caller cancellation retains all accepted payloads through Retiring and removes them only after the explicit fake poll.

Suggested focused filter: `CoveMovement.LaunchShapes*:*LaunchShapeTerminalErrors*` in `//tests:cove_player`. These are CPU ownership/protocol checks, not actual GPU completion proof.

The existing real case `//tests:gpu_authored_shapes --test_filter=GpuAuthoredShapes.BoundedPendingOperationsRefuseWithoutConsumingPreparedStorage` fills all eight operation slots, confirms Busy leaves the ninth prepared payload intact, drains, then successfully uploads it. Root can include this one affected GPU case without repeating the unchanged motion/camera matrix.

The rebuilt uncapped browser journey must complete real free Reverse/Disable Launch controls without retry injection or relaxed acceptance. Root's planned final native recovery journey checks the current combined Application as well. Passing helper tests alone do not establish application acceptance.

## Source audit

Baseline hashes verified; separate diffs generated; whitespace check produced no diagnostics. Conditional GoogleTest assertions use braces to avoid the previously encountered dangling-else warning. Independent review is requested and belongs in `review.md`. No compile or runtime success is claimed here.

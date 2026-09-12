# Environment admission boundary correction

The native r04 log ends immediately after boat/cargo spawn, with no GPU error. Its retained early observer packet has tick0 and no observed environment body. The old new-environment branch returned false for any dead slot, including a completed packet preceding queued body creation. The failed run did not log the precise rejected packet, so it alone does not prove that branch was taken.

The backend queues spawn and ordinary destruction at `nextMutationTick() = encodedTick_ + pendingTicks_ + 1`; the public frontier reports `scheduled = encodedTick_ + pendingTicks_`. Successful spawn therefore records `environmentAdmissionTick = scheduled + 1`. Earlier packets cannot describe the new lifetime and are skipped. At or after admission, exact live body generation and authored shape are required; unexplained death refuses and logs its tick. This matches the existing boat-root admission boundary.

Leave records the same exact removal boundary. A dead packet retires the shape only at or after that boundary. This also covers cancellation of an unexecuted spawn: the backend removes its pending lifetime, and the application still waits for post-boundary completed evidence. A live body at or after removal is refused. Shape retirement remains owned by the existing resource manager and its completion/lifetime rules. No legacy scenery, boat, cargo, save or physics backend behavior changed.

Only the new environment fields, spawn, Leave and snapshot branch in `src/app/application.cpp` changed. Source context and whitespace checks passed. No compiler, test, GPU, app or image run was performed for this correction. Root owns the next integrated native verification.

Independent presentation review subsequently confirmed the exact scheduler boundary, strict post-admission identity checks, and post-removal retirement, with no actionable finding and no reviewer test/runtime. The next actual native r02 journey successfully admitted and rendered the environment and restored the old world; its later independent stair traversal failure is preserved under build-cove-visual-r01/native-environment-r02.

# Native design library review

Read-only review of `src/game/expedition/design_library.{hpp,cpp}`, `src/engine/platform/native/design_library.{hpp,cpp}`, `tests/test_design_library.cpp` and `tests/test_native_design_library.cpp`.

No actionable correctness findings in the reviewed implementation. Collection publication retains current and backup together until the existing generation store acknowledges durability. A failed or uncertain publication does not report success or allow a stale generation retry. The worker owns storage descriptors and drains accepted writes on close; application validation remains on the calling thread. Revision checks, exact name uniqueness, bounded envelopes, independent library identities, import filename restrictions, no-follow file admission and no-overwrite flushed exports are consistent with the stated contract.

The focused build invocation in `checks/focused-r02.log` passed all six model and eight native host cases. Exact XML is preserved in `checks/design-library-r02.xml` and `checks/native-design-library-r02.xml`. The sibling author subsequently corrected three recovery strings to say restart the game, matching the terminal close API; no protocol change was reported.

Limits: this review does not establish Application menu integration, browser/native exchange through a real installed-content validator, non-Linux behavior, GPU behavior or a mandatory-suite gate. The transport tests deliberately use checksum-valid transport fixtures; the host callback still must perform the existing full blueprint/content validation. Recovery is at the paired complete-generation level plus the stored previous design version; it does not repair a deliberately re-encoded malformed inner record.

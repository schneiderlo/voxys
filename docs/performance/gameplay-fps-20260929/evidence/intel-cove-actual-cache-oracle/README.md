# Actual Cove cache shader oracle: software-only complete

All 33 paired whole-entry cases (66 streams) are byte-identical for final live RGBA16Float/R32Float on llvmpipe Vulkan. Source, fixture, blob and raw output identities have been independently reverified. The exercised-path control shows the new actual-Cove cache differs from the legacy cache, while unshadowed cached and live colors match exactly. Nonfinite output components fail the replay.

Cases cover unshadowed, shadowed and mixed visibility; LEGO, smooth and study materials; supplied and invalid normals; wet shoreline; sky, zero, negative, mixed and tiny positive depths; changed environment; moon, zero and tiny direct lighting; live time/exposure; invalid cache; and underwater caustics. Only the explicitly declared private cache flag differs in uploads.

This is supplemental software correctness only. Intel hardware execution is blocked by the current WSL interop permission restriction. The CPU cache patch and lifecycle test are prepared but uncompiled/unrun; callback ordering, content invalidation, full 16-texture layout, submission rollback and real engine frame equivalence remain acceptance gates. No game timing was run for this prototype and it is not promoted.

Final source/fixture/report identities and outstanding scope are in final-status.json. The CPU/test patch and risk report are in ../cove-actual-cache-prototype/. All owned replay/export/probe processes are closed.

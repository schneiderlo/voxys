# Independent visual review — imported Blacksmith

Reviewed 2026-09-17 by `village_visual_gap_review`, independently of implementation.

**Verdict: genuine LEGO part detail is preserved in the optimized studio preview. This is not yet an in-game visual acceptance.**

## Evidence inspected

- Original studio image: `/tmp/voxys-blacksmith-proof.png`.
- Optimized, lower-light image: `/tmp/voxys-blacksmith-release/preview.png`.
- Optimized image SHA-256: `79ef89aa69c55ef9081aa31c55d3137f3d1361717bfa11d23762f4e79337bfff`.
- Import log, material conversion script, and the pinned source description.

The current direction is to use actual downloaded LDraw assemblies. Matching the former AI village image or replacing the current player character is not the acceptance criterion for this asset.

## Findings

- Roof tiles, studs, masonry joints, lattice windows, foliage pieces and cart-wheel spokes remain recognisable individual LEGO elements. The lower-light image retains these details after the reported 18% triangle reduction. No obvious widespread warping or newly collapsed parts is visible from this angle.
- Lower lighting substantially corrects the previous washed-out appearance. Brown framing, multiple roof blues and green foliage now have useful colour separation. The surface does not appear excessively glossy in this studio view.
- Black openings above and left of the front dormer, and the blue plate projecting outside the upper-right roof trim, remain conspicuous. They also appear in the original pre-optimization image, so this comparison does **not** implicate mesh reduction. The implementing agent subsequently reports matching these features against the original OMR render; this source comparison was not independently repeated by this reviewer.
- Import logs show no missing-part warning; the separately reported dependency audit resolves all 859 source definitions. Complete dependencies do not independently prove correct part placement, transforms or face orientation.
- Metal/rubber material classes are reported preserved in this derivative. Transparent source parts remain opaque because the current cooker supports opaque materials only. Record this approximation; do not claim full source material fidelity.

## Open acceptance checks

- [x] Compare the dormer openings and upper-right roof plate with a source assembly render: reported completed by the implementing agent against the original OMR image, with matching seams/extensions. Not independently repeated by this reviewer.
- [ ] Inspect the optimized asset from the opposite side and close to the roof; this single view cannot establish complete geometric fidelity.
- [ ] Inspect the cooked asset in the game under its actual warm lighting, including shaded walls and roof detail. Studio appearance does not validate engine normals, culling or materials.
- [ ] Confirm terrain contact, stud/minifigure scale, collision alignment and a usable walking approach in game.
- [ ] Measure runtime cost at the intended placement density. This visual review does not approve performance or a whole village composed of repeated high-detail assemblies.

The studio model is a credible basis for integration. The pending checks above remain open and must not be represented as completed visual acceptance.

## Runtime evidence follow-up

Independently inspected `in-game.png` in this directory. It shows the game running at the bay overlook, with the current character, motorcycle, terrain, trees and interface. **The Blacksmith is outside the frame.** This image supports startup evidence only; it cannot establish the imported model's in-game readability, face culling, shading, scale or terrain contact.

The implementing agent reports seeing the actual model in a village-facing browser view without an obvious artifact before restoring the camera. That frame was not preserved here and has not been independently reviewed. Startup and focused-test success are also reported separately; neither substitutes for a visible asset inspection.

Independent runtime visual acceptance therefore remains open. Even a distant village-facing frame would only support a limited silhouette/culling check: close geometry, opposite-side faces, ground contact and collision alignment still require suitable evidence. Existing smaller procedural cottages remain pending replacement; this asset integration does not complete the village conversion.

# Cove building controls

Open the workshop at the dock with **B** on the keyboard or **View / Back** on a standard controller. Leave the boat first.

On native, **F2 / Menu** opens the tools and saved-design menu. The browser has the same tools in its workshop panel; **Menu** gives the controller focus over those buttons. Directions choose an item, **A / Confirm** uses it, and **B / Back** closes a drawer or returns to building. The native menu also accepts mouse clicks and wheel scrolling.

## Build and edit

For a first build, select the **Cargo cradle**, Remove it and Keep. That frees the starter boat’s central studs for bricks. Choose a brick, Snap it into place, then Keep or place it.

| Action | Keyboard / mouse | Controller on the build surface |
|---|---|---|
| Choose a brick | Palette or 1 / 2 / 3 | Menu → Choose parts / brick palette |
| Select a part | Click, or Tab | LB / RB |
| Select a group | Shift-click; Ctrl+A selects all | Menu → Select and group |
| Move the selection | Arrows; Q / Z raise / lower | D-pad or left stick; RT / LT raise / lower |
| Rotate | R around Y; Shift+R around X; Alt+R around Z | X around Y; other axes in the menu |
| Snap to a compatible connection | T | Y |
| Keep / place | E; click a valid pointer preview | A |
| Cancel the preview | Backspace; Esc stops the spare brick preview | B |
| Undo / redo a kept draft edit | U or Ctrl+Z; Ctrl+Y | Menu → Undo / Redo draft |
| Copy the selection | Ctrl+D or Copy selection | Menu → Duplicate group |
| Replace selected parts | Choose a catalog type, then Ctrl+R / Replace selection | Choose a type, then Menu → Replace |
| Paint | Y cycles colors; named swatches in browser | Menu → Paint bricks |
| Configure modules | X enable; L output limit; N reverse | Menu → Module settings |
| Launch the kept design | Enter / Launch | Menu → Launch boat |

Selection changes require a kept or cancelled preview. Moving, rotating, removing, painting and supported configuration apply to the entire selection. Keep stores one undoable edit. Undo/redo restores its selection too. A new kept edit clears redo history.

Copy starts the new group slightly to the right. Move or Snap it until it fits, then Keep. Mirror makes a copy across the boat's X=0 or Z=0 build plane. Only the three symmetric brick definitions currently support reflection; asymmetric machinery receives an explicit refusal. Mirrored copies must still connect and avoid overlap. No material is spent by a preview, copy or saved blueprint.

Replacement removes the selected design members and introduces replacements in distinct free slots. Existing paid identities are never changed into another part type. Compatible stored parts and normal prices/refunds are resolved by Launch. Mixed unsupported paint/settings and capacity violations refuse the whole operation.

## View the build

- **Right-drag:** orbit. **Middle-drag** or **Shift+right-drag:** pan. **Wheel:** zoom.
- **A / D:** orbit. **W / S:** zoom. **G:** frame selection. **M:** frame boat.
- **Controller right stick:** orbit. Hold **R3** while moving the right stick to pan. Hold **L3** while moving the right stick vertically to zoom. Focus/Frame boat are also in the menu.

Opening or closing a menu, losing focus, or reconnecting a controller clears held actions. Release the controller to neutral before using it again. Confirm does not repeat. Directional navigation repeats deliberately.

## Saved designs

Open **Saved designs**. Name a design by typing or using the on-screen keyboard. Native names use a four-row letter grid: directions select a character; A adds it; LB/RB or Tab switches between letters and Done/Backspace/Cancel. Names are limited to 96 UTF-8 bytes. Unsupported native font glyphs display as explicit Unicode codes while stored names remain exact.

Save new, update, copy, rename, restore the previous version, delete with confirmation, and export/import are available. The library holds up to 32 named designs and one previous version for each. Wait for the saved acknowledgment. A failed or uncertain write is not reported as saved.

Native Linux designs normally live in `~/.local/share/voxys/Designs` (or the corresponding XDG data directory). An explicit `--expedition-root` keeps its isolated library under `<root>/Designs`. Export writes a new `.voxy-design.json` file to **Exports** without overwriting an existing file. Put files in **Imports**, then refresh and select one in the menu. Paths are shown in the menu. Browser import/export uses the browser file controls. Both use the same exchange format.

Stop the unused brick preview, or Keep/Cancel an ordinary preview, before loading a design. Loading changes the draft only. Check the price, then Launch. A design never grants physical parts, stock or expedition progress.

To preserve the physical boat and mission progress, close the workshop, Pause, and use **Save expedition** in the browser or **F10** on Linux. Blueprint saving is separate from expedition saving.

## Current scope

These controls operate in the existing Cove and preserve its current budgets: 96 scene slots, with a tested 64-brick design. General 256-part assemblies and the broader game gates remain separate requirements. Native durable storage currently supports Linux; the existing Windows storage backend remains unfinished. COM/flotation/stress overlays are optional and are not added by this controls work.

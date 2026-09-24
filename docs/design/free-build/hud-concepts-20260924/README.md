# LEGO-style MMO HUD concepts

Four generated design options over a real in-game screenshot. These are proposals for selection, not implemented HUDs.

Selected direction: **Clear adventure**, with the **Social playground** radial piece selector while building. See the [corrected shared native/browser HUD](../shared-hud-correction-20260924/README.md) for current rendered previews and their capture provenance.

The game was started in Windows Chrome with WebGPU. All DOM overlays were hidden and explore mode enabled before capturing [the clean source image](game-no-ui.png). No placeholder backdrop or image-based UI removal was used. Capture details are in [capture.json](capture.json).

Each option was generated separately with the built-in image generator using the same screenshot as its edit target. The tool does not expose a version selector, so a specific “2.5” model is not claimed. The prompt requested preservation of the scene; AI outputs can still subtly re-render the background. The minimap, quests, health and social widgets are concept artwork, not evidence of implemented gameplay.

1. [Clear adventure](01-clear-adventure.png) — minimal framing, slim bars, clear world view.
2. [Builder's expedition](02-builders-expedition.png) — graphite instruments, mechanical details and compact toy tools.
3. [Storybook guild](03-storybook-guild.png) — green and gold fantasy framing, illustrated portraits.
4. [Social playground](04-social-playground.png) — colourful social presence and a radial build selector.

The exact four prompts are saved in [prompts.md](prompts.md). No application UI code was changed during this concept-generation task.

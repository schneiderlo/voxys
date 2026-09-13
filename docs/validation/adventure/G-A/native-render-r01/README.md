# Native saved world after the render fix

**Passed.** The corrected native binary opened the genuine saved world from
the earlier ordinary-controls journey and ran its raycast frame loop for eight
seconds after startup. The complete check took 11.312 seconds. It restored the
19-part house, registered bed 21, equipped hammer and balances of 566 wood,
304 stone and 70 scrap. There were no GPU, renderer or fatal error lines.

All stored file bytes remained unchanged. The owned process stopped after
requested SIGTERM without force killing. No gameplay action, save request,
image capture or repeat construction journey was performed.

The original [native home journey](../native-home-r01/README.md) verified controls,
geometry, items and save/restart behavior before the visual review exposed two
render wiring errors. Adventure now enables the opaque scene and samples the
terrain depth cache; the raycast-only and resize guards preserve that path.
This short native check covers startup and continued rendering after those
changes. The browser's separately recorded image provides visual acceptance.

Four debug log intervals reported the active raycast frame loop. These are
ordinary frame-loop timing logs, **not** independent GPU draw-count or performance
budget evidence. Read-only observations remained fresh throughout the check.

[Results and hashes](results.json) identify the tested binary, exact command,
observation count, original save hashes and final unchanged save hashes.
The [complete record](summary.json.gz), [process log](process.log.gz),
[driver](run.py.gz) and [diagnostic configuration](observed-adventure.cfg.gz)
are retained. The configuration differs from `adventure.cfg` only by enabling
debug FPS logging; the command also requests debug-level logs and 1280×800.

This check does not replace the ordinary-controls journey or claim a second
native visual review. No further GPU work was performed.

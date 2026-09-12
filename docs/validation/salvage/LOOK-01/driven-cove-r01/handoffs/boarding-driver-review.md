# Actual interaction ends the approach

The first native journey stopped after30.167seconds with the player at `[4.68133,1.285,-52.3956]` and the real `board` interaction available. The driver unnecessarily insisted on reaching the exact authored boarding-point vicinity before pressing E, and never used the available prompt. The application remained healthy, with no process GPU errors.

Approach now follows the admitted marked route through `(6,-49.5)` and `(4.5,-49.5)`, then ends when the actual board interaction is available. Helm/dock approaches likewise end on their respective real prompt. Each action still uses physical E/click input and requires its confirmed on-boat/helm/dock transition. No state, camera, collision, physics or application code changed. Native and browser drivers share this correction; preserve the first failure.

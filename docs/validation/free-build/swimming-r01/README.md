# Swimming: surface movement and free diving

Hold Space to rise and X to descend. WASD moves relative to the camera.
While holding mouse-look, forward/backward swimming follows camera pitch;
otherwise it stays level. Controller triggers rise/dive. Releasing movement
settles to a stop and holds depth. Rising stops at the surface.

Control reference: the [swimming guide on Blizzard's forum](https://us.forums.blizzard.com/en/wow/t/welcome-to-warcraft-frequent-questions/48331).
This implements the requested movement feel, not WoW's breath/damage systems.

Swimming accelerates smoothly to two thirds of walking speed. Three-dimensional
input shares one speed limit; Shift does not boost water movement. Surface
entry cancels falling speed, and shore exits use the walking step/headroom
sweeps. Underwater movement retains collision with walls, floors and ceilings.
Saved underwater positions load at their saved depth.

The builder leans into travel, follows dive/rise pitch, paddles with alternating
arms and kicks at the molded hip joints. Stopping blends into a slower tread.
The root bob and roll are cosmetic and respect reduced-motion settings; the
camera and collision position are not animated. Menus pause the animation.
The HUD shows swimming hints only in water, including with the palette open.

## Validation

- Movement/input/animation target: passes.
- Player and collision cases in the world target: 21 pass.
- Full-terrain runtime integration: both tests pass, including held Space/X,
  surface movement, explore/build modes, underwater save/reload and menu rearming.
- Adventure control preferences: passes.
- Browser UI: all seven tests pass.
- Native executable and browser executable builds succeed.

The native test run used `Adventure*:FreeBuildRuntimeIntegration.*` to isolate
the relevant cases from concurrent forest work. That forest test file needed
`-Wno-error=dangling-else` for an unrelated in-progress warning. No movement
warnings were suppressed.

Native screenshots were captured in an isolated saved world with software
Vulkan rendering. Both the [surface tread](surface-idle.png) and
[forward stroke](surface-swim.png) were visually checked: the figure stays at
the waterline, leans into travel, and keeps the limb joints connected.
These captures predate the final desktop control-hint text update. They verify
the pose and placement; the software renderer is not a performance benchmark.

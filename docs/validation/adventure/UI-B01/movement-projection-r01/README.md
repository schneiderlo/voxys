# Camera-relative movement correction

Adventure's sideways movement used a right-handed basis while the renderer uses
left-handed camera matrices. At orbit yaw zero, pressing D or pushing the left
stick right moved toward world +X, which projects to the left of the screen.
`adventureCameraRelativeMovement` now uses the renderer's matching right vector.
Forward movement, analog magnitude and the player's existing diagonal speed cap
are unchanged.

Two focused CPU tests passed in **10 ms**. The production input router feeds the
helper for keyboard and controller samples. The real orbit camera and `Camera`
then project the resulting world displacement at eight yaw angles, three
elevations and two aspect ratios. All **768** cardinal/diagonal direction cases
move to the expected side of the image and forward/backward in camera depth.
The second case checks analog magnitude, diagonal magnitude and neutral input.

Reproduce from the repository root:

```sh
nix-shell --run 'bazel test //tests:adventure_movement --jobs=4 --test_output=errors'
```

[Results and source hashes](results.json) identify the checked code. Compressed
build/test logs and XML are in `checks/`. The first attempt could not reach the
Nix daemon from the sandbox. The next stopped on duplicate Bazel registration;
the focused target was moved outside the generated target list, then passed.

These are CPU integration checks against the actual renderer matrices. They do
not claim a native or browser player journey, OS controller access or a new GPU
capture. The parent task integrates the helper into `AdventureRuntime::update`
and owns subsequent gameplay acceptance.

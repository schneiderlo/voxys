# Town residents on the installed full terrain

The CPU preflight passed on the unmodified 8192×8192 terrain, height scale 600,
cell scale 1, water height −200. The decoded raw SHA-256 matched the installed
world identity. Both production marker solids were included. No terrain crop,
flattening, game launch, screenshot or saved-world mutation was used.

All three residents were admitted at their canonical anchors. Starting at the
real town spawn, the production player controller reached their admitted
approach points in 31, 31 and 43 ordinary 1/60-second steps. Every endpoint stayed
in Walking mode, had clear capsule space and passed the real range/line-of-sight
interaction query.

Three additional cases put a legacy player at each authored resident anchor.
Admission preserved each exact player pose, kept its collision capsule clear,
kept the exact town recovery spawn clear and admitted the displaced resident at
a safe bounded alternative. These are derived actor-admission checks; save codec
migration is covered separately by the storage and adventure authority tests.

[Results](results.json), [the production-controller probe](full-terrain-town.cpp)
and [successful output](full-terrain-town-r02.log) retain the exact numbers.
The first standalone link omitted `construction_types.cpp`; that setup failure
is retained in `full-terrain-town-r01.log`. Adding that real identity dependency
allowed the unchanged production geometry source to compile and pass, exit 0.
The preserved build script expects to run from the repository root with the
source copied to `build-adventure-g-b/world-checks/` and the verified raw terrain
at `/tmp/voxys-adventure-world.r16`.

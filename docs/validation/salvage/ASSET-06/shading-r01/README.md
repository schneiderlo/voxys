# Rejected first surface-normal candidate

The r03 winch/helm candidate replaces flat face normals with weighted normals
and analytic torus normals. Both Blender exports and strict cooks succeeded.
Four actual native Near views are retained in `native-lod1/`.

Root inspected the winch-metal-key and helm-grip-key images against the flat
r02 baseline. Rings improved, but flat panel normals acquired unwanted
rounding. The independent exported-GLB check in `export-rejection.json` rejects
the first winch Near panel: 0.790669 degrees from its plane, above the 0.03-degree
quantization allowance. This is an expected rejection, not a passed checkpoint.

The assets and exact recipes remain in
`data/salvage/material-calibration/r03/`. They are not selected by the normal
browser/native inspection configuration. The corrected candidate and full
handoff are in [shading-r02](../shading-r02/README.md).

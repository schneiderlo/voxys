# Water Pro Vendoring

The web build has an integration point for the licensed `threejs-water-pro`
package, but the package is not committed here.

Copy the purchased browser ESM build into:

```text
web/vendor/threejs-water-pro/threejs-water-pro.module.js
web/vendor/threejs-water-pro/textures/waternormals.jpg
```

If the module imports `three` as a bare package name, also vendor a browser ESM
copy of Three.js at:

```text
web/vendor/three/three.module.js
```

The import map in `index.html` already points `three` and `threejs-water-pro`
at those paths. Start the WASM build normally, then add `?waterpro=1` to the URL
to make `water-pro-adapter.js` try the licensed package. Without that query
flag, Voxys uses the built-in WGSL water surface in `shaders/ray_blit.wgsl`.

Useful query overrides:

```text
?waterpro=1
?waterpro=1&waterpro-module=./vendor/threejs-water-pro/custom-entry.js
?waterpro=1&waterpro-normals=./vendor/threejs-water-pro/textures/custom.jpg
```

The adapter also exposes a clean Three.js shader fallback through
`window.VoxyWaterPro.createSurface({ THREE, scene, options })` for diagnostics
or side demos. The main Voxys terrain canvas remains owned by the C++/WebGPU
renderer, so the production water visible in the app is the WGSL implementation.

/* Keep this list exhaustive. The parity runner checks it against shaders/. */
const VOXY_SHADER_PARITY_FILES = Object.freeze([
    "cull_terrain.wgsl",
    "debug_depth.wgsl",
    "mip_generate.wgsl",
    "physics_ballistic.wgsl",
    "physics_broad_phase.wgsl",
    "physics_ccd.wgsl",
    "physics_deterministic_primitives.wgsl",
    "physics_dynamic_solver.wgsl",
    "physics_event_readback.wgsl",
    "physics_islands.wgsl",
    "physics_lockstep.wgsl",
    "physics_narrow_phase.wgsl",
    "physics_primitive_cull.wgsl",
    "physics_primitives.wgsl",
    "physics_primitives_compact.wgsl",
    "physics_queries.wgsl",
    "ray_blit.wgsl",
    "sky_lut.wgsl",
    "sky_lut_mip.wgsl",
    "terrain.wgsl",
    "terrain_raycast.wgsl",
    "underwater_particles.wgsl",
    "water_clipmap.wgsl",
    "water_composite.wgsl",
    "water_fft.wgsl",
    "water_finalize.wgsl",
    "water_foam.wgsl",
]);

if (typeof module !== "undefined") {
    module.exports = VOXY_SHADER_PARITY_FILES;
}

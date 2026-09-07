#!/usr/bin/env python3
"""LEGO raycast regression: compare GPU depth/normals with analytic solids.

Run with Python packages wgpu and numpy and a WebGPU-capable Vulkan adapter.
VOXY_LEGO_SHADER may point at a baseline shader for before/after comparisons.
This deliberately tests rendered output, including the complete mip traversal.
"""
import os
from pathlib import Path
import time
import numpy as np
import wgpu

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT, SIZE = 128, 80, 16
SCALE = 10.0
MODE = int(os.environ.get("VOXY_LEGO_MODE", "2" if os.environ.get("VOXY_LEGO_STUDY") == "1" else "1"))
STUDY = MODE >= 2
STUD_HEIGHT = 0.18 if STUDY else 0.2
STUD_RADIUS = 0.30 if STUDY else 0.35


def unit(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def reference(origin, rays, heights):
    """Independent exhaustive box/capped-cylinder intersections, in float64."""
    best = np.full(rays.shape[0], np.inf)
    normals = np.zeros_like(rays)

    def accept(t, n, valid):
        mask = valid & (t > 0) & (t < best)
        best[mask] = t[mask]
        normals[mask] = np.broadcast_to(n, rays.shape)[mask]

    for z in range(0 if STUDY else 1, SIZE - 1):
        for x in range(0 if STUDY else 1, SIZE - 1):
            lo = np.array([x - 7.5, -SCALE, z - 7.5])
            hi = lo + [1, heights[z, x] + SCALE, 1]
            with np.errstate(divide='ignore', invalid='ignore'):
                slabs = np.stack([(lo - origin) / rays, (hi - origin) / rays])
            enter = np.min(slabs, axis=0)
            leave = np.max(slabs, axis=0)
            t = np.max(enter, axis=1)
            face = np.argmax(enter, axis=1)
            n = np.zeros_like(rays)
            n[np.arange(len(rays)), face] = -np.sign(rays[np.arange(len(rays)), face])
            accept(t, n, t <= np.min(leave, axis=1))
            center = np.array([x - 7, heights[z, x], z - 7])
            oc = origin - center
            t = (STUD_HEIGHT - oc[1]) / rays[:, 1]
            p = oc + rays * t[:, None]
            accept(t, [0, 1, 0], np.sum(p[:, [0, 2]] ** 2, axis=1) <= STUD_RADIUS ** 2)
            a = np.sum(rays[:, [0, 2]] ** 2, axis=1)
            b = 2 * (rays[:, 0] * oc[0] + rays[:, 2] * oc[2])
            c = oc[0] ** 2 + oc[2] ** 2 - STUD_RADIUS ** 2
            disc = b * b - 4 * a * c
            for sign in [-1, 1]:
                t = (-b + sign * np.sqrt(np.maximum(disc, 0))) / (2 * a)
                p = oc + rays * t[:, None]
                radial = p.copy()
                radial[:, 1] = 0
                n = radial / np.maximum(np.linalg.norm(radial, axis=1, keepdims=True), 1e-20)
                accept(t, n, (disc >= 0) & (p[:, 1] >= 0) & (p[:, 1] <= STUD_HEIGHT))
    return best, normals


def check_bevel_shading(device):
    """Execute the production bevel function on edge/rim/filtering cases."""
    source = (ROOT / 'shaders/ray_blit.wgsl').read_text()
    start = source.index('fn legoBevelNormal(')
    end = source.index('// One filtered color sample', start)
    helper = source[start:end]
    cases = [
        # local XZ, distance below top, footprint, geometric normal XYZ, pad
        [0.5, 0.5, 0, 0, 0, 1, 0, 0],  # cap center
        [1, 0.8, 0, 0, 0, 1, 0, 0],    # brick top edge
        [0.85, 0.5, 0, 0, 0, 1, 0, 0], # stud cap rim
        [0.85, 0.5, 0, 0, 1, 0, 0, 0], # stud wall rim
        [1, 0.5, 1, 0, 1, 0, 0, 0],    # deep flat wall
        [1, 1, 0, 0, 0, 1, 0, 0],      # three-way corner
        [1, 0.8, 0, 0.06, 0, 1, 0, 0], # subpixel: exact flat normal
        [0, 0.8, 0, 0, 0, 1, 0, 0],    # opposite edge symmetry
    ]
    cases += [[1, 0.8, 0, f, 0, 1, 0, 0] for f in np.linspace(0, 0.1, 64)]
    values = np.array(cases, dtype=np.float32)
    shader = device.create_shader_module(code=helper + """
struct Sample { position: vec4<f32>, normal: vec4<f32> };
@group(0) @binding(0) var<storage, read> samples: array<Sample>;
@group(0) @binding(1) var<storage, read_write> result: array<vec4<f32>>;
@compute @workgroup_size(1)
fn test(@builtin(global_invocation_id) id: vec3<u32>) {
    let s = samples[id.x];
    result[id.x] = vec4<f32>(legoBevelNormal(s.position.xy,
        s.normal.xyz, s.position.z, s.position.w), 0.0);
}
""")
    pipeline = device.create_compute_pipeline(layout='auto', compute={'module': shader, 'entry_point': 'test'})
    inputs = device.create_buffer_with_data(data=values, usage=wgpu.BufferUsage.STORAGE)
    outputs = device.create_buffer(size=len(cases)*16, usage=wgpu.BufferUsage.STORAGE | wgpu.BufferUsage.COPY_SRC)
    group = device.create_bind_group(layout=pipeline.get_bind_group_layout(0), entries=[
        {'binding': 0, 'resource': {'buffer': inputs}},
        {'binding': 1, 'resource': {'buffer': outputs}}])
    encoder = device.create_command_encoder()
    compute = encoder.begin_compute_pass()
    compute.set_pipeline(pipeline)
    compute.set_bind_group(0, group)
    compute.dispatch_workgroups(len(cases))
    compute.end()
    device.queue.submit([encoder.finish()])
    normals = np.frombuffer(device.queue.read_buffer(outputs), dtype=np.float32).reshape(-1, 4)[:, :3]
    assert np.all(np.isfinite(normals))
    np.testing.assert_allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-5)
    for i in [0, 4, 6]:
        np.testing.assert_allclose(normals[i], values[i, 4:7], atol=1e-5)
    for i in [1, 2, 3]:
        np.testing.assert_allclose(normals[i], unit([1, 1, 0]), atol=1e-5)
    np.testing.assert_allclose(normals[5], [0.5, 2**-0.5, 0.5], atol=1e-5)
    np.testing.assert_allclose(normals[7], normals[1]*[-1, 1, 1], atol=1e-5)
    assert np.all(np.diff(normals[8:, 0]) <= 1e-6), 'Bevel must fade monotonically'
    assert np.all(np.sum(normals*values[:, 4:7], axis=1) >= 2**-0.5-1e-5)
    print('PASS: bevel edges, stud rims, corners, symmetry, finite unit normals, and subpixel fade')


def main():
    adapter = wgpu.gpu.request_adapter_sync(power_preference='low-power')
    device = adapter.request_device_sync()
    print('Adapter:', adapter.info['device'], 'shared terrain' if STUDY else 'legacy K')
    check_bevel_shading(device)
    source = Path(os.environ.get('VOXY_LEGO_SHADER', ROOT / 'shaders/terrain_raycast.wgsl')).read_text()
    shader = device.create_shader_module(code=source)
    device.create_shader_module(code=(ROOT / 'shaders/ray_blit.wgsl').read_text())
    pipeline = device.create_compute_pipeline(layout='auto', compute={'module': shader, 'entry_point': 'main'})
    sampled = wgpu.TextureUsage.TEXTURE_BINDING | wgpu.TextureUsage.COPY_DST
    height_tex = device.create_texture(size=(SIZE, SIZE, 1), mip_level_count=5, format='r32uint', usage=sampled)
    shadow = device.create_texture(size=(SIZE, SIZE, 1), format='r32uint', usage=sampled)
    water = device.create_texture(size=(1, 1, 4), format='rgba16float', usage=sampled)
    uniform = device.create_buffer(size=544, usage=wgpu.BufferUsage.UNIFORM | wgpu.BufferUsage.COPY_DST)
    output_usage = wgpu.TextureUsage.STORAGE_BINDING | wgpu.TextureUsage.COPY_SRC
    depth = device.create_texture(size=(WIDTH, HEIGHT, 1), format='r32float', usage=output_usage)
    shades = device.create_texture(size=(WIDTH, HEIGHT, 1), format='r32float', usage=output_usage)
    material = device.create_texture(size=(WIDTH, HEIGHT, 1), format='rgba16float', usage=output_usage)
    resources = [ {'buffer': uniform}, height_tex.create_view(), depth.create_view(),
                  shades.create_view(), material.create_view(), shadow.create_view(),
                  water.create_view(dimension='2d-array'), device.create_sampler() ]
    group = device.create_bind_group(layout=pipeline.get_bind_group_layout(0),
        entries=[{'binding': i, 'resource': value} for i, value in enumerate(resources)])
    cases = [('flat', [0, 5, -13], [0, 0, 0]),
             ('terraces', [4, 6, -13], [0, 0, 0]),
             ('grazing', [0, 0.15, -13], [0, 0.15, 0]),
             ('distant', [0, 400, -4000], [0, 0, 0]),
             ('ceiling', [0, 12, -13], [0, 10, 0])]
    failures = []
    for label, camera, target in cases:
        raw = np.full((SIZE, SIZE), 32768, dtype=np.uint32)
        if label == 'terraces':
            raw[:, 8:] += 3277
            raw[10:, :] += 1638
        if label == 'ceiling':
            raw.fill(65535)
        heights = (raw.astype(np.float64) / 65535 * 2 - 1) * SCALE
        if STUDY:
            count = int(np.floor(2*SCALE/.32+.5))
            levels = (raw.astype(np.uint64)*count+32767)//65535
            heights = -SCALE + levels.astype(np.float64)*(2*SCALE/count)
        for mip in range(5):
            size, step = SIZE >> mip, 1 << mip
            values = np.empty((size, size), dtype=np.uint32)
            for z in range(size):
                for x in range(size):
                    values[z, x] = raw[z*step:min(SIZE, (z+1)*step+1), x*step:min(SIZE, (x+1)*step+1)].max() if mip else raw[z, x]
            device.queue.write_texture({'texture': height_tex, 'mip_level': mip}, values,
                {'bytes_per_row': size * 4}, (size, size, 1))
        origin = np.array(camera, dtype=np.float64)
        forward = unit(np.array(target) - origin)
        right = unit(np.cross([0, 1, 0], forward))
        up = np.cross(forward, right)
        sy = np.tan(np.radians(55) / 2)
        sx = sy * WIDTH / HEIGHT
        if label == 'distant':
            sx *= 0.004
            sy *= 0.004
        u = np.zeros(136, dtype=np.float32)
        u[32:48] = np.array([list(right)+[0], list(up)+[0], list(forward)+[0], [0,0,0,1]]).flatten()
        u[48:52] = [SIZE, SIZE, 1/SIZE, 1/SIZE]
        u[52:56] = [SCALE, 1, 1, 0]
        u[56:60] = list(origin) + [1]
        u[60:64] = [sx, sy, MODE, 0]
        u[92:96] = list(unit([1, 2, -1])) + [0]
        device.queue.write_buffer(uniform, 0, u)
        xx, yy = np.meshgrid((np.arange(WIDTH)+0.5)/WIDTH*2-1, 1-(np.arange(HEIGHT)+0.5)/HEIGHT*2)
        rays = unit((xx[..., None]*sx*right + yy[..., None]*sy*up + forward).reshape(-1, 3))
        expected_depth, expected_normal = reference(origin, rays, heights)
        timings = []
        for frame in range(8):
            start = time.perf_counter()
            encoder = device.create_command_encoder()
            compute = encoder.begin_compute_pass()
            compute.set_pipeline(pipeline)
            compute.set_bind_group(0, group)
            compute.dispatch_workgroups(WIDTH//8, HEIGHT//8)
            compute.end()
            device.queue.submit([encoder.finish()])
            result = device.queue.read_texture({'texture': depth}, {'bytes_per_row': WIDTH*4}, (WIDTH, HEIGHT, 1))
            if frame >= 3:
                timings.append((time.perf_counter()-start)*1000)
        actual = np.frombuffer(result, dtype=np.float32)
        material_data = device.queue.read_texture({'texture': material}, {'bytes_per_row': WIDTH*8}, (WIDTH, HEIGHT, 1))
        material_values = np.frombuffer(material_data, dtype=np.float16).reshape(-1, 4).astype(np.float64)
        normals = material_values[:, :3]
        shadow_data = device.queue.read_texture({'texture': shades}, {'bytes_per_row': WIDTH*4}, (WIDTH, HEIGHT, 1))
        shadows = np.frombuffer(shadow_data, dtype=np.float32)
        hit = np.isfinite(expected_depth)
        top_distance = material_values[hit, 3]
        assert np.all(np.isfinite(top_distance)) and np.all(top_distance >= 0)
        assert np.all(top_distance[expected_normal[hit, 1] > 0.5] < 0.002)
        assert np.all(np.isfinite(shadows[hit]))
        assert np.all((shadows[hit] >= 0) & (shadows[hit] <= 1))
        mismatch = hit != (actual > 0)
        tolerance = 0.002 if label != 'distant' else 0.08
        error = np.abs(actual[hit] - expected_depth[hit])
        alignment = np.sum(normals[hit] * expected_normal[hit], axis=1)
        bad = int(np.count_nonzero(mismatch)) + int(np.count_nonzero(error > tolerance))
        wrong_normal = int(np.count_nonzero(alignment < (0.99 if label != 'distant' else 0.95)))
        print(f'{label}: hits={hit.sum()} depth_errors={bad} normal_errors={wrong_normal} median_ms={np.median(timings):.3f}')
        # Distant subpixel silhouettes permit a handful of f32 boundary differences.
        allowed = 12 if label == 'distant' else 2
        failures.extend([(label, 'depth', bad)] if bad > allowed else [])
        failures.extend([(label, 'normals', wrong_normal)] if wrong_normal > allowed else [])
    assert not failures, failures
    print('PASS: LEGO depth, face/stud normals, grazing rays, distance, and maximum-height bounds')


if __name__ == '__main__':
    main()

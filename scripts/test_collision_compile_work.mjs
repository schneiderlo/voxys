import assert from 'node:assert/strict';
import fs from 'node:fs';
import {test} from 'node:test';

// A source-level compiler-work guard, not a runtime instruction estimate.
// Confirm its latency relevance with compare_pipeline_startup.mjs on hardware.
const shader = fs.readFileSync(process.env.VOXY_NARROW_SHADER
    || new URL('../shaders/physics_narrow_phase.wgsl', import.meta.url), 'utf8');

test('authored body ordering exposes only one large contact traversal to inlining', () => {
    const source = shader.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');
    const start = source.indexOf('fn collide_authored_polyhedra(');
    assert(start >= 0, 'missing authored contact entry');
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    for (; end < source.length && depth; ++end) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
    }
    assert.equal(depth, 0, 'unterminated authored contact entry');
    const body = source.slice(open + 1, end - 1);
    const calls = [...body.matchAll(/\bcollide_authored_polyhedra_ordered\s*\(/g)].length;
    assert.equal(calls, 1, 'duplicated BVH/contact call sites increase cold shader compilation');
});

test('terrain shape dispatch exposes one large primitive traversal to inlining', () => {
    const source = fs.readFileSync(new URL('../shaders/physics_ballistic.wgsl', import.meta.url), 'utf8');
    const body = source.slice(source.indexOf('fn generate_terrain_candidates('), source.indexOf('fn candidate_less('));
    assert.equal([...body.matchAll(/\bgenerate_primitive_terrain_candidates\s*\(/g)].length, 1);
});

test('primitive capsule sampling excludes the unreachable authored traversal', () => {
    const source = fs.readFileSync(new URL('../shaders/physics_queries.wgsl', import.meta.url), 'utf8');
    const body = source.slice(source.indexOf('fn closest_segment_surface('), source.indexOf('fn sphere_separation('));
    assert.equal([...body.matchAll(/\bshape_surface\s*\(/g)].length, 0);
    assert.equal([...body.matchAll(/\bprimitive_surface\s*\(/g)].length, 5);
});

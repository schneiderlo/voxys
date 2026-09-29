import {test} from 'node:test';
import assert from 'node:assert/strict';
import {summarizeTerrainCacheWork} from './terrain_cache_work.mjs';

function sample() {
    return {before: {acquisitions: 100, telemetry: {render: {surface_acquired_frames: 100,
        static_cache_frames: 100, terrain_cache_refreshes: 10}}},
    after: {acquisitions: 200, telemetry: {render: {surface_acquired_frames: 200,
        static_cache_frames: 200, terrain_cache_refreshes: 15}}},
    workBefore: {'renderPass:blit_static_background_pass': 20, 'draw:blit_background_pipeline': 20,
        'renderPass:blit_water_clipmap_pass': 100, 'draw:blit_cached_opaque_pipeline': 100},
    workAfter: {'renderPass:blit_static_background_pass': 23, 'draw:blit_background_pipeline': 23,
        'renderPass:blit_water_clipmap_pass': 200, 'draw:blit_cached_opaque_pipeline': 160,
        'draw:blit_cached_opaque_color_pipeline': 40}};
}
test('depth and background color cache work stay distinct across both composition pipelines', () => {
    const result = summarizeTerrainCacheWork(sample());
    assert.equal(result.valid, true);
    assert.equal(result.terrainDepthAndShadow.reusedFrames, 95);
    assert.equal(result.backgroundColor.encodedReuseFrames, 97);
    assert.equal(result.backgroundColor.bakePassesPerSurfaceFrame, .03);
    assert.equal(result.fpsClaim, false);
});
test('a settled zero-bake interval requires positive startup instrumentation', () => {
    const row = sample();
    for (const key of ['renderPass:blit_static_background_pass', 'draw:blit_background_pipeline']) row.workAfter[key] = row.workBefore[key];
    assert.equal(summarizeTerrainCacheWork(row).backgroundColor.encodedReuseFrames, 100);
    delete row.workBefore['draw:blit_background_pipeline']; delete row.workAfter['draw:blit_background_pipeline'];
    assert.equal(summarizeTerrainCacheWork(row).valid, false);
});
test('actual Cove bake pipeline requires its own startup evidence and preserves pass parity', () => {
    const row = sample();
    for (const endpoint of ['workBefore', 'workAfter']) {
        row[endpoint]['draw:cove_background_pipeline'] = row[endpoint]['draw:blit_background_pipeline'];
        delete row[endpoint]['draw:blit_background_pipeline'];
    }
    const result = summarizeTerrainCacheWork(row);
    assert.equal(result.valid, true); assert.equal(result.backgroundColor.encodedBakeDraws, 3);
    assert.deepEqual(result.backgroundColor.encodedBakeDrawsByPipeline,
        [{label:'blit_background_pipeline',count:0}, {label:'cove_background_pipeline',count:3}]);
    ++row.workAfter['draw:cove_background_pipeline'];
    assert.equal(summarizeTerrainCacheWork(row).valid, false);
});
test('legacy to actual Cove pipeline transitions sum explicit bake draws without hiding missing work', () => {
    const row = sample(); row.workAfter['draw:blit_background_pipeline'] = 21;
    row.workAfter['draw:cove_background_pipeline'] = 2;
    const result = summarizeTerrainCacheWork(row);
    assert.equal(result.valid, true); assert.equal(result.backgroundColor.encodedBakeDraws, 3);
    assert.deepEqual(result.backgroundColor.encodedBakeDrawsByPipeline,
        [{label:'blit_background_pipeline',count:1}, {label:'cove_background_pipeline',count:2}]);
    delete row.workAfter['draw:cove_background_pipeline'];
    assert.equal(summarizeTerrainCacheWork(row).valid, false);
});
test('missing, backward, fractional and oversized engine counts reject the diagnostic', () => {
    for (const mutate of [row => {delete row.before.telemetry.render.static_cache_frames;},
        row => {row.after.telemetry.render.terrain_cache_refreshes = 9;},
        row => {row.after.telemetry.render.terrain_cache_refreshes = 15.5;},
        row => {row.after.telemetry.render.static_cache_frames = 201;},
        row => {row.after.telemetry.render.terrain_cache_refreshes = 111;},
        row => {row.after.acquisitions = 199;}]) {
        const row = sample(); mutate(row); assert.equal(summarizeTerrainCacheWork(row).valid, false);
    }
});
test('encoded passes require matching pipeline draws and cached frame scope', () => {
    for (const mutate of [row => {++row.workAfter['draw:blit_background_pipeline'];},
        row => {--row.workAfter['renderPass:blit_water_clipmap_pass'];},
        row => {row.workAfter['renderPass:blit_static_background_pass'] = 121; row.workAfter['draw:blit_background_pipeline'] = 121;},
        row => {row.workAfter['draw:blit_cached_opaque_pipeline'] = 99;}]) {
        const row = sample(); mutate(row); assert.equal(summarizeTerrainCacheWork(row).valid, false);
    }
});
test('clean captures cannot masquerade as instrumented zero-work diagnostics', () => {
    const row = sample(); delete row.workBefore; delete row.workAfter;
    const result = summarizeTerrainCacheWork(row);
    assert.equal(result.valid, false); assert.equal(result.terrainDepthAndShadow.bakes, 5);
    assert.equal(result.backgroundColor, undefined);
});

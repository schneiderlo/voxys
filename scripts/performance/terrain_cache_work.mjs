#!/usr/bin/env node
// Offline analysis of existing engine telemetry and --counts=1 observations.
// This module adds no work to the production render loop or clean FPS capture.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export function summarizeTerrainCacheWork(sample) {
    const failures = [];
    const delta = (before, after, key, description) => {
        const first = before?.[key], last = after?.[key];
        if (!Number.isSafeInteger(first) || first < 0 || !Number.isSafeInteger(last) || last < first) {
            failures.push(`Missing or nonmonotonic ${description}`); return null;
        }
        return last - first;
    };
    const before = sample.before?.telemetry?.render, after = sample.after?.telemetry?.render;
    const surfaceFrames = delta(before, after, 'surface_acquired_frames', 'engine surface counter');
    const acquisitions = delta(sample.before, sample.after, 'acquisitions', 'canvas acquisition counter');
    if (!(surfaceFrames > 0) || surfaceFrames !== acquisitions) failures.push('Surface and canvas counts must agree and advance');
    const cachedDepthFrames = delta(before, after, 'static_cache_frames', 'terrain cache frame counter');
    const depthBakes = delta(before, after, 'terrain_cache_refreshes', 'terrain cache refresh counter');
    if (cachedDepthFrames > surfaceFrames || depthBakes > cachedDepthFrames) failures.push('Terrain cache counts exceed their frame scope');
    const result = {surfaceFrames, terrainDepthAndShadow: {
        cachedFrames: cachedDepthFrames, bakes: depthBakes,
        reusedFrames: cachedDepthFrames === null || depthBakes === null ? null : cachedDepthFrames - depthBakes,
    }};

    if (!sample.workBefore || !sample.workAfter) {
        failures.push('Background color analysis requires a separate --counts=1 diagnostic');
    } else {
        const workDelta = (key, required = false) => {
            if (required && !(sample.workBefore[key] > 0)) failures.push(`Missing startup instrumentation: ${key}`);
            return delta({...sample.workBefore, [key]: sample.workBefore[key] ?? 0},
                {...sample.workAfter, [key]: sample.workAfter[key] ?? 0}, key, key);
        };
        const bakePasses = workDelta('renderPass:blit_static_background_pass', true);
        const bakeDrawLabels = ['blit_background_pipeline', 'cove_background_pipeline'];
        const bakeDrawsByPipeline = bakeDrawLabels.map(label => ({label, count: workDelta(`draw:${label}`)}));
        const bakeDraws = bakeDrawsByPipeline.some(row => row.count === null)
            ? null : bakeDrawsByPipeline.reduce((sum, row) => sum + row.count, 0);
        if (!bakeDrawLabels.some(label => sample.workBefore[`draw:${label}`] > 0)) {
            failures.push('Missing startup background color bake pipeline instrumentation');
        }
        const compositionPasses = workDelta('renderPass:blit_water_clipmap_pass', true);
        const depthCompositions = workDelta('draw:blit_cached_opaque_pipeline');
        const colorCompositions = workDelta('draw:blit_cached_opaque_color_pipeline');
        const compositionDraws = depthCompositions === null || colorCompositions === null
            ? null : depthCompositions + colorCompositions;
        if (!['draw:blit_cached_opaque_pipeline', 'draw:blit_cached_opaque_color_pipeline'].some(key => sample.workBefore[key] > 0)) {
            failures.push('Missing startup cached composition pipeline instrumentation');
        }
        if (bakePasses !== bakeDraws || compositionPasses !== compositionDraws) failures.push('Background render-pass and pipeline-draw counters disagree');
        if (compositionPasses !== cachedDepthFrames || bakePasses > compositionPasses) failures.push('Background color counts exceed or differ from the cached frame scope');
        result.backgroundColor = {encodedBakePasses: bakePasses, encodedBakeDraws: bakeDraws,
            encodedBakeDrawsByPipeline: bakeDrawsByPipeline,
            encodedCompositionPasses: compositionPasses, encodedCompositionDraws: compositionDraws,
            encodedReuseFrames: bakePasses === null || compositionPasses === null ? null : compositionPasses - bakePasses,
            bakePassesPerSurfaceFrame: surfaceFrames > 0 && bakePasses !== null ? bakePasses / surfaceFrames : null,
            countMeaning: 'Encoded render passes/draws observed at the WebGPU API; not completed GPU execution or visible display frames'};
    }
    return {...result, valid: failures.length === 0, failures: [...new Set(failures)],
        fpsClaim: false, source: 'Existing engine telemetry and optional WebGPU work wrappers; no production instrumentation added'};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const options = Object.fromEntries(process.argv.slice(2).map(argument => {
        const split = argument.indexOf('=');
        if (!argument.startsWith('--') || split < 3) throw Error('Use --report=FILE --output=FILE');
        return [argument.slice(2, split), argument.slice(split + 1)];
    }));
    if (!options.report || !options.output) throw Error('--report and --output are required');
    const report = JSON.parse(fs.readFileSync(options.report, 'utf8'));
    const rows = report.rows.map(sample => ({trial: sample.trial, variant: sample.variant,
        experience: sample.experience, scenario: sample.scenario, cacheWork: summarizeTerrainCacheWork(sample)}));
    fs.writeFileSync(options.output, JSON.stringify({schema: 'voxys.terrain-cache-work.v1',
        sourceReport: path.resolve(options.report), sourceHarnessIdentity: report.harnessIdentity,
        capturePurpose: report.capturePurpose, rows, fpsClaim: false}, null, 2));
    if (rows.some(row => !row.cacheWork.valid)) process.exitCode = 2;
}

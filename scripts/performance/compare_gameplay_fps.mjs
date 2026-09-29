#!/usr/bin/env node
// Compare clean repeated captures after separate build phases. Prefer the
// runner's in-process ABBA mode when both staged variants are available.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {compareGameplayFps} from './gameplay_fps_metrics.mjs';
const options = Object.fromEntries(process.argv.slice(2).map(arg => {
    const split = arg.indexOf('=');
    if (!arg.startsWith('--') || split < 3) throw Error('Use --name=value');
    return [arg.slice(2, split), arg.slice(split + 1)];
}));
if (!options.baseline || !options.candidate || !options.output) throw Error('--baseline, --candidate and --output are required');
const files = [options.baseline, options.candidate].map(file => fs.readFileSync(file));
const [baseline, candidate] = files.map(bytes => JSON.parse(bytes));
const failures = [];
for (const [label, report] of [['baseline', baseline], ['candidate', candidate]]) {
    if (!report.complete || report.error) failures.push(`${label} capture is incomplete`);
    if (report.configuration.instrumented || report.configuration.profiling) failures.push(`${label} contains diagnostics; clean timing required`);
    if (report.rows.some(row => row.travel.playerTickMethod !== 'readonly-scalar')) failures.push(`${label} lacks the clean scalar player clock`);
}
for (const key of ['width', 'height', 'repeats', 'ticks', 'warmupTicks', 'driverPollMs', 'playerStallTimeoutMs', 'recordPowerLineStatus', 'travelClock', 'experiences', 'scenarios', 'uncapped', 'clockHour', 'dayNight', 'requireZeroSun']) {
    if (JSON.stringify(baseline.configuration[key]) !== JSON.stringify(candidate.configuration[key])) failures.push(`Mismatched ${key}`);
}
if (JSON.stringify(baseline.browser) !== JSON.stringify(candidate.browser)) failures.push('Mismatched Chrome version');
if (JSON.stringify(baseline.chromeArguments) !== JSON.stringify(candidate.chromeArguments)) failures.push('Mismatched Chrome flags');
if (JSON.stringify(baseline.harnessIdentity) !== JSON.stringify(candidate.harnessIdentity)) failures.push('Mismatched measurement harness');
if (JSON.stringify(baseline.system.gpu.devices) !== JSON.stringify(candidate.system.gpu.devices)) failures.push('Mismatched physical GPU/driver');
const rows = [...baseline.rows.map(row => ({...row, variant: 'baseline'})),
    ...candidate.rows.map(row => ({...row, variant: 'candidate'}))];
const comparison = compareGameplayFps(rows, Number(options['max-regression-percent'] ?? 7.5));
for (const group of comparison) failures.push(...group.failures.map(value => `${group.workload}: ${value}`));
const output = {schema: 'voxys.gameplay-fps-comparison.v1',
    captures: files.map((bytes, index) => ({file: path.resolve(index ? options.candidate : options.baseline),
        sha256: createHash('sha256').update(bytes).digest('hex')})),
    identity: {browser: baseline.browser, gpu: baseline.system.gpu.devices, configuration: baseline.configuration},
    comparison, failures};
fs.mkdirSync(path.dirname(path.resolve(options.output)), {recursive: true});
fs.writeFileSync(options.output, JSON.stringify(output, null, 2));
console.log(JSON.stringify({comparison, failures}, null, 2));
process.exitCode = failures.length ? 2 : 0;

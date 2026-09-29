import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

export function scriptedOrbitMouseX(step, width) {
    // Keep the final fifty player ticks still. Every accepted drag returns to
    // the same coordinate before the endpoint is observed, even if a slow
    // render skips some intermediate mouse samples.
    return width / 2 + (step < 110 ? 100 * Math.sin(step * Math.PI * 2 / 110) : 0);
}

export function hashGameplayInputFile(filename) {
    const bytes = fs.readFileSync(filename);
    return {bytes: bytes.length, sha256: sha256(bytes)};
}

export function inspectGameplaySite(directory) {
    const index = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
    const stems = ['voxy_wasm', 'voxy_wasm_cc'].filter(stem =>
        ['.js', '.wasm'].every(suffix => fs.existsSync(path.join(directory, stem + suffix))));
    if (stems.length !== 1) throw Error('Expected one complete voxy_wasm or voxy_wasm_cc executable');
    const stem = stems[0], glue = fs.readFileSync(path.join(directory, stem + '.js'), 'utf8');
    const dynamicPacks = !glue.includes(stem + '.data') && /voxyReleaseFiles\s*=\s*['"]\{/.test(index);
    if (!dynamicPacks && !fs.existsSync(path.join(directory, stem + '.data'))) throw Error(`Missing required staged data: ${stem}.data`);
    const names = new Set(['index.html', stem + '.js', stem + '.wasm']);
    for (const filename of fs.readdirSync(directory)) {
        if (/^voxy_[a-z0-9_]+\.(data|wasm)$/.test(filename)
            || ['voxy_graphics.json', 'benchmark-package.json', 'package.json'].includes(filename)) names.add(filename);
    }
    const files = {};
    for (const filename of names) {
        const bytes = fs.readFileSync(path.join(directory, filename));
        files[filename] = {bytes: bytes.length, sha256: sha256(bytes)};
    }
    return {stem, dynamicPacks, files};
}

export function readGameplayShaderOverrides(manifestPath) {
    const overrides = {}, identities = {};
    if (!manifestPath) return {overrides, identities};
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    for (const [label, entry] of Object.entries(manifest)) {
        if (!entry || typeof entry !== 'object' || !entry.source || !entry.expectedSource) {
            throw Error(`Shader ${label} requires source and expectedSource files`);
        }
        const filename = path.resolve(path.dirname(manifestPath), entry.source);
        const expectedFilename = path.resolve(path.dirname(manifestPath), entry.expectedSource);
        const code = fs.readFileSync(filename, 'utf8'), expectedCode = fs.readFileSync(expectedFilename, 'utf8');
        const expectedSha256 = sha256(expectedCode);
        if (entry.expectedSha256 !== undefined && entry.expectedSha256 !== expectedSha256) {
            throw Error(`Expected shader hash differs for ${label}`);
        }
        overrides[label] = {code, expectedCode, expectedSha256};
        identities[label] = {filename, sha256: sha256(code), bytes: Buffer.byteLength(code),
            expectedFilename, expectedSha256};
    }
    return {overrides, identities};
}

export function readGameplaySymbolMap(filename, expectedWasmSha256, actualWasmSha256) {
    if (!filename) return {matched: false, reason: 'No symbol map supplied; WASM names remain unverified'};
    if (expectedWasmSha256 !== actualWasmSha256) throw Error('Symbol map requires the matching captured WASM SHA-256');
    const bytes = fs.readFileSync(filename);
    return {matched: true, filename: path.resolve(filename), sha256: sha256(bytes),
        bytes: bytes.length, wasmSha256: actualWasmSha256};
}

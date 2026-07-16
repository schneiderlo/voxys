import fs from "node:fs";
import path from "node:path";

const options = {
    candidate: "",
    baseline: "",
    html: "",
    json: "",
    minimumSamples: 10,
    failRegressionPercent: 0,
};

const usage = () => {
    console.error(
        "usage: node scripts/analyze_wasm_profile.mjs PROFILE.json "
        + "[--baseline BASELINE.json] [--html REPORT.html] "
        + "[--json REPORT.json] [--minimum-samples N] "
        + "[--fail-regression-percent N]",
    );
};

for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const value = () => process.argv[++index] ?? "";
    if (argument === "--baseline") options.baseline = value();
    else if (argument === "--html") options.html = value();
    else if (argument === "--json") options.json = value();
    else if (argument === "--minimum-samples") {
        options.minimumSamples = Number.parseInt(value(), 10);
    } else if (argument === "--fail-regression-percent") {
        options.failRegressionPercent = Number.parseFloat(value());
    } else if (argument === "--help" || argument === "-h") {
        usage();
        process.exit(0);
    } else if (argument.startsWith("-") || options.candidate) {
        usage();
        throw new Error(`unknown argument: ${argument}`);
    } else {
        options.candidate = argument;
    }
}

if (!options.candidate || !(options.minimumSamples > 0)
    || !(options.failRegressionPercent >= 0)) {
    usage();
    process.exit(2);
}

const readProfile = (filename) => {
    const profile = JSON.parse(fs.readFileSync(filename, "utf8"));
    if (profile.schema !== "voxys.wasm_click_profile.v1") {
        throw new Error(`${filename}: unsupported profile schema`);
    }
    if (!profile.raw?.frameMs || !profile.raw?.frameSamples
        || !profile.raw?.gpuStageSamples
        || !profile.raw?.renderGpuStageSamples) {
        throw new Error(`${filename}: raw samples are required`);
    }
    return profile;
};

const percentile = (values, fraction) => {
    if (values.length === 0) return 0;
    const sorted = [...values].sort((left, right) => left - right);
    const rank = Math.max(1, Math.ceil(sorted.length * fraction));
    return sorted[Math.min(rank - 1, sorted.length - 1)];
};

const median = (values) => percentile(values, 0.5);
const statistics = (values) => {
    const finite = values.filter(Number.isFinite);
    const p50 = median(finite);
    const deviations = finite.map((value) => Math.abs(value - p50));
    return {
        count: finite.length,
        mean: finite.length === 0 ? 0
            : finite.reduce((sum, value) => sum + value, 0) / finite.length,
        p50,
        p95: percentile(finite, 0.95),
        p99: percentile(finite, 0.99),
        maximum: finite.length === 0 ? 0 : Math.max(...finite),
        mad: median(deviations),
    };
};

const stageSeries = (profile, domain) => {
    const isRender = domain === "render";
    const names = Object.keys(isRender
        ? profile.gpu.render.stages : profile.gpu.stages);
    const samples = isRender
        ? profile.raw.renderGpuStageSamples : profile.raw.gpuStageSamples;
    return names.map((name, stage) => ({
        domain,
        name,
        values: samples.map((sample) => sample.stages[stage]),
    }));
};

const summarizeProfile = (profile) => {
    const stages = [
        ...stageSeries(profile, "render"),
        ...stageSeries(profile, "physics"),
    ].map((stage) => ({
        ...stage,
        statistics: statistics(stage.values),
    }));
    const measuredGpuP50 = stages.reduce(
        (sum, stage) => sum + stage.statistics.p50, 0,
    );
    for (const stage of stages) {
        stage.sharePercent = measuredGpuP50 > 0
            ? stage.statistics.p50 * 100 / measuredGpuP50 : 0;
    }
    const elapsedSeconds = profile.frame.elapsedMs / 1000;
    const pacingSkips = Math.max(
        0, profile.gpu.pacingSkipsEnd - profile.gpu.pacingSkipsStart,
    );
    return {
        frame: statistics(profile.raw.frameMs),
        cpu: statistics(profile.raw.frameSamples.map(
            (sample) => sample.cpuMs)),
        queue: statistics(profile.raw.frameSamples.map(
            (sample) => sample.queue)),
        physicsGpu: statistics(profile.raw.gpuStageSamples.map(
            (sample) => sample.totalMs)),
        renderGpu: statistics(profile.raw.renderGpuStageSamples.map(
            (sample) => sample.totalMs)),
        throughputFps: profile.frame.throughputFps,
        pacingSkips,
        pacingSkipsPerSecond: elapsedSeconds > 0
            ? pacingSkips / elapsedSeconds : 0,
        peakJsHeapBytes: profile.memory.peakJsHeapBytes,
        wasmBytes: profile.memory.wasmBytes,
        measuredGpuP50,
        budget85FpsMs: 1000 / 85,
        stages,
    };
};

const worldCamera = (profile) => {
    const camera = profile.workload.benchmarkCamera ?? profile.browser.camera;
    return camera.position.map(
        (value, axis) => value + 256 * (camera.sector?.[axis] ?? 0),
    );
};

const sameNumber = (left, right, tolerance = 1e-5) =>
    Number.isFinite(left) && Number.isFinite(right)
    && Math.abs(left - right) <= tolerance;

const equivalenceOracle = (baseline, candidate) => {
    if (!baseline) return { passed: true, mismatches: [] };
    const mismatches = [];
    const exact = (label, left, right) => {
        if (left !== right) mismatches.push(`${label}: ${left} != ${right}`);
    };
    exact("canvas width", baseline.browser.canvasWidth,
          candidate.browser.canvasWidth);
    exact("canvas height", baseline.browser.canvasHeight,
          candidate.browser.canvasHeight);
    exact("device vendor", baseline.browser.device?.adapter?.vendor,
          candidate.browser.device?.adapter?.vendor);
    exact("device architecture", baseline.browser.device?.adapter?.architecture,
          candidate.browser.device?.adapter?.architecture);
    exact("browser profile session", baseline.browser.profileSession,
          candidate.browser.profileSession);
    exact("body count", baseline.workload.observedBodies,
          candidate.workload.observedBodies);
    exact("workload mode", baseline.workload.mode,
          candidate.workload.mode);
    exact("expected body count", baseline.workload.expectedBodies,
          candidate.workload.expectedBodies);
    exact("measurement start tick", baseline.workload.measurementStartTick,
          candidate.workload.measurementStartTick);
    exact("measurement end tick", baseline.workload.measurementEndTick,
          candidate.workload.measurementEndTick);
    exact("telemetry tick", baseline.workload.tick,
          candidate.workload.tick);
    exact("candidate pairs", baseline.workload.candidates,
          candidate.workload.candidates);
    exact("contacts", baseline.workload.contacts,
          candidate.workload.contacts);
    exact("solver mode", baseline.workload.solverMode,
          candidate.workload.solverMode);
    exact("scheduled substeps", baseline.workload.substeps,
          candidate.workload.substeps);
    exact("physics backend", baseline.invariants.backend,
          candidate.invariants.backend);
    exact("arithmetic", baseline.invariants.arithmetic,
          candidate.invariants.arithmetic);
    const baselineCamera = worldCamera(baseline);
    const candidateCamera = worldCamera(candidate);
    for (let axis = 0; axis < 3; ++axis) {
        if (!sameNumber(baselineCamera[axis], candidateCamera[axis], 1e-3)) {
            mismatches.push(`camera axis ${axis}: ${baselineCamera[axis]} != `
                + candidateCamera[axis]);
        }
    }
    for (const key of ["yaw", "pitch", "fov_y", "aspect_ratio"]) {
        const left = baseline.workload.benchmarkCamera?.[key];
        const right = candidate.workload.benchmarkCamera?.[key];
        if (!sameNumber(left, right, 1e-4)) {
            mismatches.push(`camera ${key}: ${left} != ${right}`);
        }
    }
    const baselineInputs = baseline.workload.clickInputs ?? [];
    const candidateInputs = candidate.workload.clickInputs ?? [];
    exact("click input count", baselineInputs.length, candidateInputs.length);
    for (let index = 0;
         index < Math.min(baselineInputs.length, candidateInputs.length);
         ++index) {
        exact(`click ${index} encoded tick`, baselineInputs[index].encodedTick,
              candidateInputs[index].encodedTick);
    }
    return { passed: mismatches.length === 0, mismatches };
};

const createRandom = (seed) => {
    let state = seed >>> 0 || 0x9e3779b9;
    return () => {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        return (state >>> 0) / 0x1_0000_0000;
    };
};

const hash = (text) => {
    let result = 2166136261;
    for (const character of text) {
        result ^= character.codePointAt(0);
        result = Math.imul(result, 16777619);
    }
    return result >>> 0;
};

const bootstrapMedianDelta = (label, baseline, candidate) => {
    if (baseline.length < options.minimumSamples
        || candidate.length < options.minimumSamples
        || median(baseline) === 0) return null;
    const random = createRandom(hash(label));
    const deltas = [];
    for (let repeat = 0; repeat < 2000; ++repeat) {
        const left = Array.from({ length: baseline.length }, () =>
            baseline[Math.floor(random() * baseline.length)]);
        const right = Array.from({ length: candidate.length }, () =>
            candidate[Math.floor(random() * candidate.length)]);
        deltas.push((median(right) / median(left) - 1) * 100);
    }
    return {
        low: percentile(deltas, 0.025),
        high: percentile(deltas, 0.975),
    };
};

const probabilityLower = (baseline, candidate) => {
    if (baseline.length === 0 || candidate.length === 0) return 0;
    let score = 0;
    for (const right of candidate) {
        for (const left of baseline) {
            if (right < left) score += 1;
            else if (right === left) score += 0.5;
        }
    }
    return score / (baseline.length * candidate.length);
};

const compareLatency = (label, baseline, candidate) => {
    const baselineStats = statistics(baseline);
    const candidateStats = statistics(candidate);
    const deltaPercent = baselineStats.p50 === 0 ? 0
        : (candidateStats.p50 / baselineStats.p50 - 1) * 100;
    const confidenceInterval = bootstrapMedianDelta(
        label, baseline, candidate,
    );
    let verdict = "insufficient samples";
    if (confidenceInterval) {
        if (confidenceInterval.high < 0) verdict = "win";
        else if (confidenceInterval.low > 0) verdict = "regression";
        else verdict = "noisy";
    }
    return {
        label,
        baseline: baselineStats.p50,
        candidate: candidateStats.p50,
        deltaPercent,
        confidenceInterval,
        candidateLowerProbability: probabilityLower(baseline, candidate),
        verdict,
    };
};

const candidate = readProfile(options.candidate);
const baseline = options.baseline ? readProfile(options.baseline) : null;
const candidateSummary = summarizeProfile(candidate);
const baselineSummary = baseline ? summarizeProfile(baseline) : null;
const oracle = equivalenceOracle(baseline, candidate);

const comparisons = [];
if (baseline) {
    comparisons.push(
        compareLatency("frame", baseline.raw.frameMs, candidate.raw.frameMs),
        compareLatency(
            "CPU frame",
            baseline.raw.frameSamples.map((sample) => sample.cpuMs),
            candidate.raw.frameSamples.map((sample) => sample.cpuMs),
        ),
        compareLatency(
            "physics GPU",
            baseline.raw.gpuStageSamples.map((sample) => sample.totalMs),
            candidate.raw.gpuStageSamples.map((sample) => sample.totalMs),
        ),
        compareLatency(
            "render GPU",
            baseline.raw.renderGpuStageSamples.map((sample) => sample.totalMs),
            candidate.raw.renderGpuStageSamples.map((sample) => sample.totalMs),
        ),
    );
    const baselineStages = new Map(baselineSummary.stages.map(
        (stage) => [`${stage.domain}:${stage.name}`, stage],
    ));
    for (const stage of candidateSummary.stages) {
        const previous = baselineStages.get(`${stage.domain}:${stage.name}`);
        if (previous) comparisons.push(compareLatency(
            `${stage.domain}: ${stage.name}`, previous.values, stage.values,
        ));
    }
}

const topHotspots = [...candidateSummary.stages]
    .sort((left, right) => right.statistics.p50 - left.statistics.p50)
    .slice(0, 5);

const number = (value, digits = 3) => Number.isFinite(value)
    ? value.toFixed(digits) : "n/a";
const percent = (value) => `${value >= 0 ? "+" : ""}${number(value, 1)}%`;
const bytes = (value) => `${number(value / (1024 * 1024), 1)} MiB`;

const lines = [];
lines.push("# Voxy WebGPU profile analysis", "");
lines.push(
    `Profile: \`${path.basename(options.candidate)}\``,
    `Workload: ${candidate.workload.observedBodies} bodies, `
        + `${candidate.browser.canvasWidth}×${candidate.browser.canvasHeight}, `
        + `${candidate.browser.device?.adapter?.vendor ?? "unknown"} `
        + `${candidate.browser.device?.adapter?.architecture ?? "GPU"}`,
    `Equivalence oracle: ${oracle.passed ? "PASS" : "FAIL"}`,
    `Runtime invariants: ${candidate.invariants.overallPassed ? "PASS" : "FAIL"}`,
    "",
);
if (!oracle.passed) {
    lines.push(...oracle.mismatches.map((mismatch) => `- ${mismatch}`), "");
}
lines.push(
    "| Metric | p50 | p95 | p99 |",
    "|---|---:|---:|---:|",
    `| Frame latency | ${number(candidateSummary.frame.p50)} ms | `
        + `${number(candidateSummary.frame.p95)} ms | `
        + `${number(candidateSummary.frame.p99)} ms |`,
    `| CPU submit | ${number(candidateSummary.cpu.p50)} ms | `
        + `${number(candidateSummary.cpu.p95)} ms | `
        + `${number(candidateSummary.cpu.p99)} ms |`,
    `| Physics GPU | ${number(candidateSummary.physicsGpu.p50)} ms | `
        + `${number(candidateSummary.physicsGpu.p95)} ms | `
        + `${number(candidateSummary.physicsGpu.p99)} ms |`,
    `| Render GPU | ${number(candidateSummary.renderGpu.p50)} ms | `
        + `${number(candidateSummary.renderGpu.p95)} ms | `
        + `${number(candidateSummary.renderGpu.p99)} ms |`,
    "",
    `Throughput: **${number(candidateSummary.throughputFps, 2)} FPS**`,
    `85 FPS budget: **${number(candidateSummary.budget85FpsMs)} ms**`,
    `Measured median GPU work: **${number(candidateSummary.measuredGpuP50)} ms**`,
    `Queue p95: **${number(candidateSummary.queue.p95, 1)} frames**; `
        + `pacing skips: **${number(candidateSummary.pacingSkipsPerSecond, 2)}/s**`,
    `Peak JS heap: **${bytes(candidateSummary.peakJsHeapBytes)}**; `
        + `WASM memory: **${bytes(candidateSummary.wasmBytes)}**`,
    "",
    "## Top GPU hotspots",
    "",
    "| Rank | Domain | Stage | p50 | p95 | Share | Tail p95/p50 |",
    "|---:|---|---|---:|---:|---:|---:|",
);
for (const [index, stage] of topHotspots.entries()) {
    const tailRatio = stage.statistics.p50 > 0
        ? stage.statistics.p95 / stage.statistics.p50 : 0;
    lines.push(
        `| ${index + 1} | ${stage.domain} | ${stage.name} | `
        + `${number(stage.statistics.p50)} ms | `
        + `${number(stage.statistics.p95)} ms | `
        + `${number(stage.sharePercent, 1)}% | ${number(tailRatio, 2)}× |`,
    );
}

if (baseline) {
    const throughputDelta = baselineSummary.throughputFps === 0 ? 0
        : (candidateSummary.throughputFps / baselineSummary.throughputFps - 1)
            * 100;
    lines.push(
        "",
        "## Baseline comparison",
        "",
        `Baseline: \`${path.basename(options.baseline)}\``,
        `Throughput: ${number(baselineSummary.throughputFps, 2)} → `
            + `${number(candidateSummary.throughputFps, 2)} FPS `
            + `(${percent(throughputDelta)})`,
        "",
        "| Metric | Baseline p50 | Candidate p50 | Delta | 95% bootstrap CI | Verdict |",
        "|---|---:|---:|---:|---:|---|",
    );
    for (const comparison of comparisons) {
        const interval = comparison.confidenceInterval
            ? `${percent(comparison.confidenceInterval.low)} to `
                + percent(comparison.confidenceInterval.high)
            : "n/a";
        lines.push(
            `| ${comparison.label} | ${number(comparison.baseline)} ms | `
            + `${number(comparison.candidate)} ms | `
            + `${percent(comparison.deltaPercent)} | ${interval} | `
            + `${comparison.verdict} |`,
        );
    }
}

lines.push(
    "",
    "Profiler note: GPU timestamp packets are collected asynchronously. "
        + "They measure stage work without inserting CPU waits; sample `atMs` "
        + "is collection time, not a synchronized CPU/GPU clock.",
);

const report = lines.join("\n");
console.log(report);

const machineReport = {
    schema: "voxys.wasm_profile_analysis.v1",
    candidate: options.candidate,
    baseline: options.baseline || null,
    oracle,
    invariantsPassed: candidate.invariants.overallPassed,
    summary: candidateSummary,
    comparisons,
    topHotspots,
};

if (options.json) {
    fs.writeFileSync(options.json, `${JSON.stringify(machineReport, null, 2)}\n`);
}

const escapeHtml = (text) => text.replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;").replaceAll(">", "&gt;");
const palette = [
    "#61afef", "#98c379", "#e5c07b", "#c678dd", "#e06c75",
    "#56b6c2", "#d19a66", "#7fdbca", "#c5a5c5", "#abb2bf",
];
const stackSvg = (summaries) => {
    const width = 1100;
    const labelWidth = 120;
    const chartWidth = width - labelWidth - 20;
    const maxTotal = Math.max(...summaries.map((entry) =>
        entry.summary.stages.reduce(
            (sum, stage) => sum + stage.statistics.p50, 0,
        )), 0.001);
    const height = 45 + summaries.length * 54;
    const chunks = [`<svg viewBox="0 0 ${width} ${height}" role="img">`];
    summaries.forEach((entry, row) => {
        const y = 25 + row * 54;
        chunks.push(`<text x="0" y="${y + 19}" fill="#ddd">`
            + `${escapeHtml(entry.label)}</text>`);
        let x = labelWidth;
        entry.summary.stages.forEach((stage, index) => {
            const segment = stage.statistics.p50 / maxTotal * chartWidth;
            if (segment <= 0) return;
            chunks.push(`<rect x="${x}" y="${y}" width="${segment}" `
                + `height="28" fill="${palette[index % palette.length]}">`
                + `<title>${escapeHtml(stage.domain)}: `
                + `${escapeHtml(stage.name)} ${number(stage.statistics.p50)} ms`
                + `</title></rect>`);
            x += segment;
        });
        chunks.push(`<text x="${Math.min(x + 8, width - 70)}" y="${y + 19}" `
            + `fill="#ddd">${number(entry.summary.measuredGpuP50)} ms</text>`);
    });
    chunks.push("</svg>");
    return chunks.join("");
};

if (options.html) {
    const lanes = baselineSummary
        ? [{ label: "baseline", summary: baselineSummary },
           { label: "candidate", summary: candidateSummary }]
        : [{ label: "profile", summary: candidateSummary }];
    const html = `<!doctype html>
<meta charset="utf-8">
<title>Voxy WebGPU profile</title>
<style>
body{background:#17191d;color:#e5e7eb;font:15px/1.5 system-ui;margin:2rem;max-width:1200px}
h1{font-size:1.6rem} .panel{background:#20232a;border:1px solid #343943;border-radius:8px;padding:1rem;margin:1rem 0}
pre{white-space:pre-wrap;font:13px/1.55 ui-monospace,monospace} svg{width:100%;height:auto}
</style>
<h1>Voxy WebGPU profile</h1>
<div class="panel"><h2>Median GPU stage lanes</h2>${stackSvg(lanes)}</div>
<div class="panel"><pre>${escapeHtml(report)}</pre></div>`;
    fs.writeFileSync(options.html, html);
}

let failed = !candidate.invariants.overallPassed || !oracle.passed;
if (baseline && options.failRegressionPercent > 0) {
    const important = new Set(["frame", "physics GPU", "render GPU"]);
    failed ||= comparisons.some((comparison) =>
        important.has(comparison.label)
        && comparison.verdict === "regression"
        && comparison.deltaPercent >= options.failRegressionPercent);
}
if (failed) process.exitCode = 2;

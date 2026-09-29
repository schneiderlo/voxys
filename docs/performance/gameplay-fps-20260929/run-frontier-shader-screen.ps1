# Windows PowerShell. Default: verify readonly inputs. -Run starts one ABBA.
# The agent has not executed this script. It writes only its own new TEMP folder.
param([switch]$Run)
$ErrorActionPreference = 'Stop'
$sourceRoot = '\\wsl.localhost\Ubuntu\tmp\voxys-fps-20260929'
$nodeExe = 'C:\Program Files\nodejs\node.exe'
$chromeExe = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
$harness = Join-Path $sourceRoot 'fps-harness-v6'
$site = Join-Path $sourceRoot 'final-cpu-site'
$manifest = Join-Path $sourceRoot 'shader-candidates\combined-daytime\manifest.json'
foreach ($file in @($nodeExe, $chromeExe)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing program: $file" }
}
$pinned = @{
    'final-wasm-interface-proof.json' = '17a9d0765eb4ce71d15d0214a71593a6b1d28a74113cd3286ecf0b4b2814bd21'
    'final-native-cpu\proof.json' = '37e576b2b1058286f266450b47a1ea6a4a0e1c4f844e880c8227d542140b58c5'
    'intel-combined-daytime-oracle\hardware-result.json' = 'e935f1b7686b7d15b32f03e5c5a0dc65092b4bacedd3172d3f44dbdeb233295a'
    'final-cpu-site\index.html' = 'a5b8993b1475afe67bd6f466a8f00a1a2d82825fc76a90e20d4cdbcc6905ea14'
    'final-cpu-site\voxy_wasm_cc.wasm' = 'a04daf5d95497f97737838cc64cec9e2122280ea1bbd6f0f3e0162b3722f574b'
    'final-cpu-site\voxy_wasm_cc.js' = '396db0497c0e802d1baf26ff4d7f96124d2e6d22adb1e77cf9b608934be6fbb7'
    'final-cpu-site\voxy_wasm_cc.data' = '3c75b59c7fbe829facbbf7e70d164d6c53a7afb882acfe9e01cafdd0314edaa6'
    'fps-harness-v6\benchmark_gameplay_fps.mjs' = 'e2bec22a9206e192b672cec84dd04e6d4f2a5bd60d25615ee263caf51ae18ca6'
    'fps-harness-v6\canvas_fps_probe.mjs' = 'cd49892432d89d4eb6b13631b981fc460834578001b2770bc5631081d156dc70'
    'fps-harness-v6\gameplay_fps_inputs.mjs' = '235d75dbc5007e527ea994365289448d3e3f1e2bc74e3bab7cbd88087ccbb590'
    'fps-harness-v6\gameplay_fps_metrics.mjs' = 'd482b2a09f771e18620391abf84b9d2384cb4483cacf3087c8a188f9cd97710e'
    'fps-harness-v6\gameplay_fps_work.mjs' = 'c956c3be4d6560d052e2056f8219d68dcf5ed261fa16d42f131f9a4910eaea26'
    'fps-harness-v6\gameplay_counters.mjs' = 'e7d919a3486721032dc6601baf3a29235040949d028eaecbe2b6ed2f811f06a9'
    'shader-candidates\combined-daytime\manifest.json' = 'f21c353e215d9fc7e6d25311805c1df350318708e06822d6ef797e64e4689113'
    'shader-candidates\mesh-shadow-gating\mesh_path.wgsl' = '6faf8b0152425a8d01f68e35892d1404040cf17e49e7ba86b445518fb49d0836'
    'shader-candidates\water-deferred-shadow\water_clipmap.wgsl' = 'd92c968dec06454e2729434a2d732e0d9217498446f443da25db4cade1ba8e43'
    'shader-candidates\cove-lazy-background-load\ray_blit.wgsl' = '75366ef320c798db98c298c539b89b37f3993c6f8d6955ec32dc53d0b1eae84a'
    'shader-candidates\baseline\mesh_path.wgsl' = '75c974f8062e38a836d5e83852f5892a90cdc81af312e7fb62fb376a67e6f67b'
    'shader-candidates\baseline\water_clipmap.wgsl' = 'f38d46349279b39076de523732623f1a4cb16d949c7fbbd8d9b63c4fbabe9abb'
    'shader-candidates\baseline\ray_blit.wgsl' = 'c2a2b432bd99b29af1f8baca92c690e92f147857004e9477aa5531069df3c97d'
}
foreach ($relative in $pinned.Keys) {
    $file = Join-Path $sourceRoot $relative
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pinned[$relative]) {
        throw "Input changed: $relative"
    }
}
$cpuProof = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'final-wasm-interface-proof.json') | ConvertFrom-Json
$shaderProof = Get-Content -Raw -LiteralPath (Join-Path $sourceRoot 'intel-combined-daytime-oracle\hardware-result.json') | ConvertFrom-Json
if (-not $cpuProof.accepted -or -not $cpuProof.actualWasmValidAndCompiled -or -not $shaderProof.result.ok) {
    throw 'Reviewed CPU or shipping-format Intel shader correctness proof is missing.'
}
Write-Host 'Inputs verified. Same CPU build; three guarded shader overrides; fixed actual daylight.'
if (-not $Run) { Write-Host 'No capture started. Add -Run after the clean timing window is ready.'; return }
& $nodeExe --version
if ($LASTEXITCODE -ne 0) { throw 'Windows Node cannot start.' }
$runName = 'voxys-frontier-shader-screen-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$output = Join-Path $env:TEMP $runName
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
New-Item -ItemType Directory -Path $output | Out-Null
$runnerArguments = @(
    (Join-Path $harness 'benchmark_gameplay_fps.mjs'), "--chrome=$chromeExe",
    "--site=$site", "--candidate-site=$site", "--candidate-shaders=$manifest",
    "--output=$output", '--repeats=1', '--experiences=frontier', '--scenarios=idle,orbit,walk',
    '--warmup-ticks=600', '--ticks=600', '--width=1280', '--height=720', '--hour=16', '--day-night=0',
    '--capture-purpose=reviewed-combined-daytime-frontier-clean-abba'
)
$runnerArguments | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'launch-argv.json') -Encoding UTF8
Write-Host "Four fresh worlds. Idle, orbit and straight walk. Output: $output"
Write-Host 'Keep this window open. The runner closes only its own isolated browser.'
& $nodeExe @runnerArguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'capture.log')
$runnerExit = $LASTEXITCODE
Write-Host "Capture exit: $runnerExit. Results: $output\report.json"
exit $runnerExit

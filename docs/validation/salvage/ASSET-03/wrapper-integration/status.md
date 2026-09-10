# ASSET-03 cooker integration — preceding precheck record

This records the earlier precheck while integration was in progress. Final acceptance now lives in [report.md](report.md), with exact source/tool identities in [summary.json](summary.json). The main plan now marks ASSET-03 complete. The preceding
completed ASSET-02 wrapper, test and guide are retained in
`../preceding-asset02-source/`; their source hashes and validation remain in
the ASSET-02 integrated report.

The wrapper now always selects `--profile salvage-rigid-v1`, with no legacy
retry. Its manifest binds that profile/CLI contract and the Python cooker source
digest in addition to existing converter, validator, source and output hashes.
An executable or cooker-source change detected during cooking rejects publication.

The Python suite now contains 23 cases. New actual-converter cases require
rejection of an unsupported required extension and an optional clearcoat
material extension. A separate failure fixture records invocation count and
arguments, proving a rejected profile is never retried in legacy mode.
Existing malformed-output fixtures now write the CLI's fourth argument rather
than the second argument, which is the profile name.

Three narrow wrapper regressions executed successfully before the new converter
was ready: no profile fallback, partial converter failure and successful-exit
corrupt VMESH rejection. These use deliberately failing converter scripts and
the actual integrated C++ validator; they do not exercise strict glTF conversion.
The [precheck log](precheck.log) records all three passes. Actual command:

```sh
SALVAGE_CONVERTER=build-salvage-native/bin/gltf_vmesh_tool \
SALVAGE_VALIDATOR=build-salvage-native/bin/gameplay_sidecar_tool \
python3 tools/salvage_assets/test_cook_gameplay_asset.py \
  CookTest.test_rejected_profile_never_retries_without_the_profile \
  CookTest.test_partial_converter_failure_does_not_publish \
  CookTest.test_zero_exit_corrupt_vmesh_is_rejected_by_actual_cpp_reader
```

The remaining work at that preceding stage was to compile the finished strict converter, run all 23 cases through both
normal build systems, inspect actual profile-rejection diagnostics, cook the
probe twice into new immutable directories, retain source/binary/bundle hashes,
and complete independent review before updating the main checkbox. No current
native/browser material or game-appearance acceptance is implied.

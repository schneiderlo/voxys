# Final Bazel native and browser builds

**Passed.** These artifacts include both preview retirement fixes and the
explicit Winch catalog pointer check. They do not include the unfinished
DATA-03 or ASSET-02 implementation. Base revision is `7563f61`; the tested
working sources are identified in [source hashes](source-hashes.json), with
exact binaries and package sizes in [artifact hashes](artifacts.json).

Executed from the repository root:

```sh
nix-shell --run 'bazel build -c opt --jobs=4 //:voxy_native && bazel build --config=wasm --jobs=4 //:voxy_wasm //tools:serve_wasm'
```

Both builds exited **0**. [Build output](bazel-build.log) is retained. The
[earlier failed native attempt](../final-native-build-first-failure/README.md)
remains separate. Compiler warnings and repository hooks remain enabled.

The real built server launcher was then exercised on loopback:

```sh
nix-shell --run 'VOXY_HOST=127.0.0.1 VOXY_PORT=18743 bazel-bin/tools/serve_wasm'
```

Use the temporary package directory printed by that launcher in another
terminal; the recorded directory is disposable, not a required permanent path:

```sh
python3 docs/validation/salvage/BOOT-05/check_package.py <printed-directory> --url http://127.0.0.1:18743
python3 docs/validation/salvage/BOOT-02/test_package.py <printed-directory>
```

Both checks exited **0**. The first verified 58 source-matching preload entries
(57,674,353 bytes), all five configs, 17 static references, 27 raw shaders,
five real WASM control exports and 48 HTTP-delivered files. Six negative controls
also passed without modifying the original package. See
[package check](package-1.log), [negative controls](package-2.log),
[actual invocation/environment](commands.json) and [server output](bazel-server.log).
The harness stopped and reaped its test server afterward.

These are build and package-integrity results. Real native/browser controls,
visual limitations and source-to-capture identity are recorded under BOOT-05.
The complete test suite is a separate required G00 check.

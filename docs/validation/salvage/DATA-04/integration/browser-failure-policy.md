# Browser exception and allocation policy

The GameSession transaction boundary catches `std::bad_alloc` to reject a
preparation before publication, and discards adapter reservations before
propagating an unexpected C++ exception. Those semantics must exist in the
actual browser build, not only native tests.

## Selected configuration

Both build systems use Emscripten JavaScript-based C++ exceptions:
`-fexceptions` during compilation and linking. Bazel's SDK `exceptions` feature
supplies only the compile flag, so `.bazelrc` also supplies the link flag.
CMake applies both before dependency targets are declared. The existing
`ASYNCIFY` setting is retained for the current WebGPU integration.

Both also link `-sABORTING_MALLOC=0` with the existing fixed WASM heap. This
permits a failed allocation to return null and C++ `operator new` to throw
`std::bad_alloc`, instead of terminating JavaScript before the catch boundary.
It does not enable memory growth, guarantee recovery from arbitrary host or GPU
failure, or make unchecked C allocation users recoverable. The host must still
stop or recover the session after an unexpected exception.

## Evidence and rejected configuration

The first Bazel WASM compile failed because its default is `-fno-exceptions`;
that failure is retained in `first-wasm-failure/`. The preceding CMake build
compiled without explicitly enabling exception catching and was not accepted.

Native WASM EH (`-fwasm-exceptions`) subsequently built in both systems, but the
SDK emitted an incompatibility warning for its combination with Asyncify.
Those logs and artifact identities are preserved in `native-wasm-eh-attempt/`.
A successful link alone did not establish a supported error-recovery path.
The separate 20-case native-WASM-EH Node test is retained as preceding evidence;
it is not a test of the selected final application configuration.

The isolated runner in `../wasm-exceptions/` records actual compilation and
execution, including exception cleanup and a bounded fixed-heap allocation
probe. Final integration evidence must additionally include native/JS-EH WASM
app builds and the visible browser lifecycle journey. At the time this policy
was written those final runs were in progress; their reports control acceptance.

## Sources and limits

The [official Emscripten exception documentation](https://emscripten.org/docs/porting/exceptions.html)
requires compile and link flags and describes the higher overhead of JS-based
EH. The [allocation setting reference](https://emscripten.org/docs/tools_reference/settings_reference.html#aborting-malloc)
documents fixed-heap abort versus null behavior. Local SDK source inspection
confirmed the flags in `emscripten_toolchain/toolchain.bzl`, `operator_new_impl`
in libc++ `src/new.cpp`, and the abort branch in `src/lib/libcore.js`.

This changes browser code generation globally. Earlier cached-render throughput
figures are historical, and no performance equivalence is claimed. The later
visible frame-time and memory gates must measure the final configuration.
Replacing Asyncify or narrowing exception coverage requires a separately
verified architectural change; silently dropping transaction cleanup is not
an optimization.

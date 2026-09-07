# Native optimized integration build — retained failure

The final `bazel build -c opt --jobs=4 //:voxy_native` attempt failed with
GCC 15.2's `-Werror=null-dereference` in the Winch catalog validation branch.
The [original source](part_catalog.cpp) is the previously tested DATA-02
`12ed95f3e466da436144e8e63137065c6a25279c4771228b44e0031f673ef942` version.
[Actual build output](bazel.log) is preserved; this failure was not suppressed.

The branch first checked the socket through `requireSocket`, then performed a
second lookup and dereferenced it. The final implementation retains a single
pointer, explicitly handles missing/incompatible sockets, then checks its force
limit. Error precedence and physical validation are preserved. DATA-02 includes
the independent follow-up review and refreshed rejection tests. The subsequent
native and WASM Bazel builds both passed; see [final builds](../final-build/README.md).

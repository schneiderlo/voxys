# DATA-02 checked Winch socket follow-up — independent review

**Result: approved within this bounded correction. All 16 strict standalone
sanitizer cases pass, including four added Winch-specific assertions.** No warning
suppression, API change or authoring-limit change was introduced.

Reviewer: `simulation_production`; date: 2026-09-07. Root made the source fix.
The reviewer inspected it and added test assertions inside the existing
`ModuleParametersAndRequiredSocketRolesCannotBeIgnored` case. No shared build,
GPU, task-checkbox or Git changes were made by this worker.

## Correction and behavior

Root's later optimized native integration failed on the unchanged prior
`part_catalog.cpp` hash `12ed95f3e466da436144e8e63137065c6a25279c4771228b44e0031f673ef942`.
GCC 15.2 reported a possible null dereference on a repeated `findSocket` lookup.
[Original failure](../G00/final-native-build-first-failure/bazel.log) is preserved.

`src/game/construction/part_catalog.cpp:197` now obtains the line socket once.
The null branch reports `MissingModuleSocket`; wrong family/role reports
`IncompatibleModuleSocket`; the non-null compatible branch compares Winch force
against that same socket's tension ceiling. Existing invalid numerical module
parameters still report `InvalidModule` before the final socket-error return.
This preserves intended validation precedence and avoids a second unchecked
dereference. The pointer refers to the immutable input definition throughout.

The expanded existing case checks a missing Winch socket ID, a structural socket
where a TowLine socket is needed, a TowLine with the wrong role, and a line socket
rated at 11,999 N with the 12,000 N Winch. Expected results are respectively
missing, incompatible, incompatible and invalid module. The valid twelve-part
catalog and all prior cases still pass.

## Tested identity and command

| File/artifact | SHA-256 |
|---|---|
| `part_catalog.hpp` | `8c107f72da01b065f811db557b38de9a3a014e5717b3348951cfe21a07bc8c13` |
| `part_catalog.cpp` | `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3` |
| `tests/test_part_catalog.cpp` | `b0cd3d3dd28f579d9e47c85705244ecb704d56cde205b5547b438c7d3f64b5bb` |
| `/tmp/salvage-part-catalog-review-winch-expanded` | `8858db5eb4bdb6c0602b52d692e3c92c1f497142c8f7ff7bfc062fdba9359959` |

Actual command from the repository root:

```sh
/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  -std=c++20 -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast \
  -Wcast-align -Wunused -Woverloaded-virtual -Wpedantic -Wconversion \
  -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2 \
  -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches \
  -Wlogical-op -Wuseless-cast -Werror -O1 -g \
  -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all \
  -Isrc -isystem third_party/googletest/googletest/include \
  src/game/construction/construction_types.cpp \
  src/game/construction/part_catalog.cpp tests/test_part_catalog.cpp \
  build-lego-native/lib/libgtest_main.a build-lego-native/lib/libgtest.a \
  -pthread -o /tmp/salvage-part-catalog-review-winch-expanded
/tmp/salvage-part-catalog-review-winch-expanded
```

[Compile log](winch-review-expanded-compile.txt) is empty: exit 0, no diagnostics.
[Test log](winch-review-tests.txt): 16/16 pass, no sanitizer diagnostics.
The preceding run without the four extra assertions is separately retained in
[winch-before-extra-cases-tests.txt](winch-before-extra-cases-tests.txt).
Historical source/test hashes and optimized/CMake evidence are unchanged in
[README-before-winch-pointer-fix.md](README-before-winch-pointer-fix.md).

This worker did not rerun CMake or Bazel during root's exclusive full-suite run.
Root owns the optimized native/full-suite integration result for this correction.

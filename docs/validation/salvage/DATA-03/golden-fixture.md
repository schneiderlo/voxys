# DATA-03 independent full wire fixture

**Result: the complete 509-byte schema-1 fixture matches the production encoder,
and its independently authored bytes decode to the expected semantic fields.
All 23 strict standalone tests pass, with no compiler or sanitizer diagnostics.**

Executor: `simulation_production`, 2026-09-07. This implements the non-blocking
wire-compatibility recommendation in [the independent review](review.md).
Production model/catalog sources and shared build files were not changed.
The previous 22-test [author output](tests-ubsan.log) and independent
[review output](review-tests.log) remain untouched and retain their prior hashes.

## Independent origin and reproduction

[generate-wire-fixture.py](generate-wire-fixture.py) assembles the documented
SVBM wire layout with Python's standard `struct.pack` and explicit little-endian
formats. It imports no production codec, parser, headers or compiled executable.
The enum values, content namespace, fields and record widths are explicit
constants. The expected bytes were generated **before** running the C++ test;
they were not recorded from `encodeBuild`.

Retained artifacts:

- [Binary fixture](svbm-v1-golden.bin), [readable hex](svbm-v1-golden.hex).
- [Complete field offsets, widths, formats and values](svbm-v1-golden-offsets.json).
  Large values are decimal strings in this manifest to avoid JSON Number loss.
- [Recipe check output](golden-recipe-check.log).

The C++ test `BuildModel.SchemaOneMatchesIndependentFullWireFixture` embeds the
literal hex so it needs no runtime file paths or Bazel data files. It compares
the full production encoding against those bytes, including unused fields and
canonical ordering. It separately decodes the golden literal, **not** the
encoder output, and checks build identity/revision/owner/lease, both complete
part records and every connection field.

To check the retained artifacts and the embedded C++ literal without changing
anything:

```sh
python3 docs/validation/salvage/DATA-03/generate-wire-fixture.py
```

Initial authorship used `--write` to create the binary/hex/offset artifacts.
That option does not replace the C++ test literal. Do not regenerate/rebaseline
a schema-1 fixture merely to make a changed encoder pass; an intended wire
change needs an explicit compatibility/schema decision.

## Fixture semantics

World namespace is the 16 ASCII bytes `build-test-world`; content namespace is
`voxys-salvage-v1`. Build ID 1 belongs to owner 2. Revision is
`0x0123456789abcdef`. Lease holder 3 uses authority epoch `9007199254740993` and
expiry tick `18446744073709551613`, exercising lossless large integers.

| Field | Winch instance 10 | Tow-eye instance 11 |
|---|---|---|
| Exact definition | ID 7, version 1 | ID 8, version 1 |
| Placement ticks | −137, 29, 211 | 503, −47, −89 |
| Proper rotation ID | 4 | 18 |
| Health / 10000 | 4321 | 9876 |
| sRGB RGBA bytes | 13, 21, 89, 200 | 240, 155, 8, 255 |
| Settings | Winch, disabled, channel 9, default payout 1234 mm | Passive, enabled, unused values zero |
| Provenance | StarterLoan, entitlement 900 | Paid, zero entitlement bytes |

Connection 100 is an enabled Rope from instance 10/socket 10 to instance
11/socket 10. Damage is 1234. Tension/shear/bending/torsion strengths are
1000.25 / 2000.5 / 3000.75 / 4000.125 in their documented units. Their distinct
binary-exact fractions detect field swaps and floating-point-width mistakes.
Minimum/maximum/rest lengths are 750 / 35000 / 5678 mm. The default unattached
winch payout is deliberately different from the existing connection's rest
length. These are authored design records, not evidence of live rope dynamics.

The input fixture reverses both part order and connection endpoints before
encoding; the expected bytes retain the documented ascending canonical order.

## Schema offsets

Offsets are zero-based; widths are bytes. The linked manifest lists every field.

| Record or selected field | Offset | Width |
|---|---:|---:|
| Header | 0 | 113 |
| Revision | 32 | 8 |
| Lease-present flag | 64 | 1 |
| Lease holder / epoch / expiry | 65 / 89 / 97 | 24 / 8 / 8 |
| Part / connection counts | 105 / 109 | 4 / 4 |
| Winch part | 113 | 130 |
| Winch settings / origin / entitlement | 208 / 218 / 219 | 10 / 1 / 24 |
| Tow-eye part | 243 | 130 |
| Rope connection | 373 | 136 |
| Rope strengths / lengths | 465 / 497 | 32 / 12 |
| End of fixture | 509 | 0 |

Settings use `u8 kind, u8 enabled, u8 channel, u16 limit, u8 reversed,
u32 default length`. Fixture enum values are Passive=0, Winch=3, Paid=0,
StarterLoan=1, Rope=1; rotation IDs use DATA-01's frozen order. IDs contain 16
namespace bytes and a little-endian u64 counter. All integers are little-endian;
strengths are IEEE-754 binary64. There is no native struct padding in the fixture.

## Executed checks and identity

Actual standalone commands from the repository root:

```sh
SALVAGE_CXX=/nix/store/788mx070y81zjlg5ipcl0cra3afviw9k-gcc-wrapper-15.2.0/bin/g++ \
  SALVAGE_TEST_BINARY=/tmp/salvage-build-model-golden-tests \
  bash docs/validation/salvage/DATA-03/build-standalone.sh \
  > docs/validation/salvage/DATA-03/golden-compile.log 2>&1
/tmp/salvage-build-model-golden-tests \
  > docs/validation/salvage/DATA-03/golden-tests.log 2>&1
```

The existing script applies full repository GCC warnings, `-Werror`, `-O1`,
undefined-behavior/float-cast-overflow sanitizers and immediate sanitizer failure.
[Compile output](golden-compile.log): empty, exit 0.
[Test output](golden-tests.log): **23/23 pass**, no sanitizer diagnostics.

| Source/artifact | SHA-256 |
|---|---|
| `build_model.hpp` | `e99cf8d8b6a409276195a49d1887d6ee4518683b0cc4fa5550af08a7d3746f19` |
| `build_model.cpp` | `5307ee045a24f3974ec109f914eb5f8d5a4c65755a32f7286ff6308be9cd508f` |
| `tests/test_build_model.cpp` | `36f69f12d5ee451013cc7d9a45795d9f87ad10568d016c68b32003ed00da29bf` |
| `part_catalog.cpp` dependency | `24869c7c346d49421b2fe20123181e4e5738d6e6ddc366339292d3f12044bcf3` |
| Fixture binary | `6afbf529535b91973b640b2b1b1ab6205ed863218ee5242ce171b948048b8ea3` |
| Independent recipe | `65b12e47709265f9675d6e08ec332f84c173d3ee044d19601a4686f097d75c78` |
| Standalone test executable | `355b4bbc0f635506d08ce8e1bc19422d205447e14fa195c93e954e320d109d0f` |

The test was authored while G00's hook ran; G00 committed separately as
`7f28fab`. These DATA-03 changes remain outside that checkpoint. No GPU,
CMake/Bazel, network, runtime save-journal, performance or platform-parity check
was run for this addition. Root owns subsequent integrated build validation.

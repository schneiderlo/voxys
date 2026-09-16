# Proposed second change — creative structure JSON reuse

Recorded after the first candidate profiles, before implementation. Candidate 1
post-profile: JSON 56.24% inclusive CPU, numeric float formatting 43.34%; editing
allocations 202.78 MiB/945,590 calls. The first full commit hook subsequently passed (`5381b18a`).

Updated ranking: unchanged structure serialization impact5/confidence4/effort2 =10;
part lookup impact2/confidence3/effort3 =2; scratch reuse impact2/confidence3/effort4
=1.5. No replay file/network I/O exists. Choose serialization next.

Proof sketch: the structures fragment depends only on the accepted structure
sequence and number formatting. In creative mode, structure edits publish a new
geometry revision; player motion does not alter structures. World+epoch+geometry
revision identifies that sequence between restores. Initialization and successful
restore must clear the fragment, since restore can replace same-revision contents.
Copy the original formatting loop unchanged into a local writer; cache its exact
bytes for the classic locale and default FE_TONEAREST rounding only. Preserve the
original direct formatting path for other locales/rounding and legacy adventure.
This avoids assuming arbitrary user facets are pure or that floating conversion
ignores rounding mode. A cache miss inherits the current output stream format.
A hit appends the exact bytes the original loop would produce. Preserve field
ordering, escaping, precision17, and metre conversion; do not change polling,
parse format, fields, or the remaining dynamic JSON. Store one bounded fragment
(max1,024 parts), not whole frames or accepted save bytes. Actual saves remain
completely independent.

Test same-revision restore of different structure contents (e.g. brick kind at
unchanged position and identity), not just revision changes. Also test a temporary
non-classic numeric locale after warming the classic fragment; original stream
formatting must still take effect and returning to classic restores exact bytes.
Also preserve directed rounding when the process temporarily leaves FE_TONEAREST.
Use the existing real-terrain integration environment and unchanged 24-run
benchmark/golden harness. Measure candidate against both baseline and candidate1.

Rejected shortcut: replacing every construction_policy partFor scan with lower_bound
without a precomputed index is not provably equivalent for that API. The standalone
policy accepts unsorted vectors even though normal session state is sorted. Keep
that boundary unchanged rather than assuming all callers are validated sessions.

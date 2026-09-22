# Missing cannon hit event regression

The runtime observed a cannonball physically colliding but no `ContactHit`.
Event packing scanned raw manifold history using a count of nonempty records.
With eight slots per body pair, raw history is sparse: two hits can occupy slots
0 and 8 while the count is only 2. Scanning raw slots 0 and 1 loses the second.

`GpuAuthoredShapes.CannonCcdEmitsEveryDenseHitWithEightSparsePatchSlots` uses the
real backend, eight normal slots and sixteen solver substeps. Two CCD spheres
strike one static authored shape in the same tick. Before the fix, both collide
but the second event is missing. After the fix, both events contain the correct
body lifetimes, authored face feature, positive normal impulse and measured
closing speed above 200 game units/second. Pass: 8.7 seconds.

The backend now binds dense solved `activeManifolds()` for event packing, with
active count telemetry word24 and the dense contact capacity. Existing compact
stream callers retain their word11 convention through an explicit source flag.
Raw ordinal/source identity remains unchanged inside each dense record. No CCD
trajectory, restitution, physics tuning or solver behavior changed.

Evidence: `contact-dense-before.log`, `contact-dense-after.log`,
`contact-dense-after.xml`. Actual world routing/visual proof remains separate.

Follow-up regression gate also passed:

- Existing event readback suite, 2/2 cases (priority/stable keys and batched tick
  parameters), 0.4 seconds.
- Authored event lifecycle, 4/4 cases: full clock across counter wrap; owned
  frontier GPU completion proof; confirmed winch break and unread-event
  backpressure; force-limited winch event correctness. 91.8 seconds.

Logs/XML: `contact-event-ring.*`, `contact-event-lifecycle.*`.

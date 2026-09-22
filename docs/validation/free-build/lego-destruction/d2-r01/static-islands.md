# Fixed supports and sleeping islands

A fixed floor previously joined every touching dynamic body into a single island.
That coupled otherwise independent piles: one moving pile could prevent quiet
piles elsewhere on the same floor from sleeping.

Both the general and compact GPU union passes now ignore contact edges with a
zero-inverse-mass endpoint. Fixed bodies retain singleton island/sleeping-grid
records so broad-phase collision can still find them. Contacts still solve
against the fixed body normally.

The compact pass retains the existing eight-storage-buffer limit. It constructs
body-sort records directly from roots instead of writing and rereading an
otherwise redundant intermediate body-record buffer, freeing that binding for
poses/inverse mass. No device limits or sleeping thresholds were raised.

All seven island tests pass. The new regression runs both execution paths:
two dynamic bodies touch one fixed support; the quiet body sleeps while the
moving body remains awake, and the fixed support stays in the sleeping grid.
The old long-chain test now explicitly initializes positive inverse masses;
it still requires all 255 dynamic bodies to join one island.

Evidence: [islands-tests.log](islands-tests.log). This does not on its own prove
the imported hollow-brick stack is stable; its contact regression is separate.

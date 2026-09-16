# Adventure C: meadow, signal terrace and beacon loop

**Implemented CPU slice; wider route remains a proposal.** Four scenery groups,
a paid terrace solution and its walking detour pass production component tests.
The complete loop, camera/visual presentation, ordinary native/browser play and
fun are **unverified**. This note does not complete EXPLORE-A01 or G-C.

## Fixed landscape and route scale

Keep town **(−63,−895)** and the existing beacon **(−63,−975)** unchanged.
Their direct 80 m connection is gentle meadow: column tops decline from
−145.92 to −149.12 m, with adjacent changes no greater than one 0.32 m plate.
There is no natural ravine or required barrier across that direct route.

Use Signal Terrace and Survey Overlook for a purposeful eastern loop. The
waypoint polyline below is **406.97 m**; its complete collision-safe walking
length remains unmeasured. The earlier 398.70 m / 484 m terrain-grid estimates
used the abandoned X22 discovery and are retained only as planning history.
A 407 m walk at 3.6 m/s is under two minutes before activities; distance alone
is not evidence of a 20–30 minute adventure.

| Order / purpose | X, Z metres | Column top Y | Nominal stud-centre feet Y |
| --- | --- | ---: | ---: |
| Town start / final return | −63, −895 | −145.92 | −145.735 |
| East town exit around sign | −61, −895 | −145.92 | −145.735 |
| Meadow fork | −61, −917 | −145.60 | −145.415 |
| Low side of the real terrace | 29, −923 | −129.60 | −129.415 |
| Signal Terrace discovery | 30, −923 | −128.64 | −128.455 |
| Survey Overlook discovery | 22, −1031 | −134.72 | −134.535 |
| Descending return toward beacon | −61, −978 | −149.12 | −148.935 |
| Beacon approach east of marker | −61, −975 | −149.12 | −148.935 |
| Return via fork, east exit, town | As above | — | — |

The eastern discoveries remain optional; direct beacon travel stays open.
Visible waystones, a ruined signal frame and a survey worklog give the longer
route a purpose. Do not describe exploration distance as proven route traversal.

## A real obstacle with two verified approaches

At **X=29.5, Z=−923**, an existing **0.96 m terrace face** blocks the actual
walking controller. The original 0.64 m face at X=21.5 was tested and is already
walkable through rounded capsule edge support. Its failed proposal is retained;
no controller or terrain rule was weakened to manufacture a challenge.

Three existing **Pier** pieces create a 1.44 m-wide step for **6 stone**.
Bottom-centre positions are **(29.10,−129.92,−923.48)**,
**(29.10,−129.92,−923.00)** and **(29.10,−129.92,−922.52)**, yaw zero.
Each exact box is 0.48×0.48×0.96 m; its top is **−128.96 m**. The .02 m lattice
positions are (1455,−6496,−46174/−46150/−46126). The paid candidate remains a
preview/helper, never automatically installed or granted to the player.

Actual `prepareBlueprint`, terrain/support/approach validation and commit charge
six stone. `AdventurePlayer` then walks up and down without jumping or entering
Airborne mode. The alternative contour route is **37 m** each way:
**(29,−923) → (29,−941) → (30,−941) → (30,−923)**. It also passes actual
bidirectional walking with installed scenery. Jumping remains a creative option.
The flat-bottom Stair cannot embed in rising terrain; keep that rule intact.

## Installed discoveries, scenery and useful supplies

- Discovery 1 at **(30,−923)** offers a separately claimed **4 scrap** reward.
  Its open signal frame at **(30,−920.5)** uses six Piers and one Beam.
- Discovery 2 at **(22,−1031)** offers a separately claimed **12 stone** reward.
  Its three-Pier survey cairn is at **(22,−1033.5)**.
- Node 19: **12 stone**, (20,−924), nominal support Y≈−134.54.
  Node 20: **16 wood**, (21,−936), support Y≈−134.22.
  Node 21: **4 scrap**, (22,−1031), support Y≈−134.54.
  Resources 1–18 retain their exact identities, poses, yields and depletion.
- The three-Pier meadow waystone is at **(−59,−917)**. A broken watch arch at
  **(−40,−1095)** uses nine Piers and one Beam, marking a later destination
  about 122 m beyond the beacon. Its rendered visibility remains unverified.
- Current fresh inventory is 640 wood / 320 stone / 80 scrap. These small caches
  are not yet meaningful scarcity rewards; never subtract old saved inventory.

The four groups contain **23 catalog pieces and 23 matching solids**, bounded
by 24 pieces/32 solids. They admit on the full terrain with both original
markers, all eight village groups and all three residents. Scenery is not usable
player furniture. The dormant/lit beacon uses the existing marker and a receipt.

## Compatibility and remaining acceptance

A whole authored group defers to old player/home/recovery positions or legacy
reserved IDs. New construction preserves its walking approach; unchanged saved
houses keep priority. There is no larger blanket build exclusion or terrain edit.
Discovery availability follows admitted scenery and reachable standing space.
Only unpublished schema4 discovery 1 and its new frame moved from X22 to X30;
all older anchors remain exact. Content identity and schema migration are handled
by the separate authority module and require their own round-trip evidence.

A field home must have a currently usable owned bed, chest and bench in the same
structure, within 25 m of the beacon and more than 40 m from town. Town recovery
registration remains unchanged. Physical furniture access has a separate check.
Remaining work includes a second materially different construction solution,
whole-loop traversal, camera clearance/visibility, ordinary native/browser play,
reward/reload integration, and an enjoyable quest pacing pass.

## Measurement and retained proof

The immutable source is **8192² little-endian uint16**, SHA-256
`2a0ae88395e6d6e59d8853540c875d834f0f04015ee953d76faf68abae610965`.
It uses height scale 600 m, cell scale 1 m, centred origin 4095.5 m and water −200 m.
Plate level is `(raw*3750+32767)/65535` with integer division; nominal columns are
`−600 + level*0.32`. Stud-centre feet add 0.18+0.005 m. The tests use production
floating-point support and sweeps, rather than treating these rounded values as
collision certificates. No samples were modified and no GPU/UI was used.

[Final five-case CPU evidence](../validation/adventure/EXPLORE-A01/trail-sites-r01/README.md)
includes the full-terrain run, exact source hashes and rejected first proposal.
[Earlier terrain-grid calculation](adventure-route-r01/terrain-sampling.json)
remains planning history, not proof of the revised complete loop.

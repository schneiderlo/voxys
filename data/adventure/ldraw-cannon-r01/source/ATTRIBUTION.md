# Cannon source attribution

This package uses the official LDraw **2527c01.dat — Minifig Cannon Non-Shooting
with Base (Shortcut)** by **Andy Westrate [westrate]**.
Its base, **2527.dat**, and cannon barrel, **518.dat**, are by
**Paul Easter [pneaster]**. All original source geometry, headers, and contributor
history are retained byte-for-byte.

- [LDraw part listing](https://library.ldraw.org/parts/list?tableSearch=2527.dat)
- [LDraw parts library](https://library.ldraw.org/)
- [Original official library archive](https://library.ldraw.org/library/updates/complete.zip)

All **27** recursively used external files declare **CC BY 4.0**. Their individual
credits, history, and license notices remain in the vendored files. The complete
legal text is included unchanged in [CAlicense4.txt](ldraw/CAlicense4.txt):
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
The archive's general [CAreadme.txt](ldraw/CAreadme.txt) and historical
[CAlicense.txt](ldraw/CAlicense.txt) are also preserved. The latter's inclusion
does not change these 27 files' declared CC BY 4.0 license.

## Contributing file authors

These seven `Author:` credits occur in the recursive dependency closure.
Additional modification credits are preserved in the source `!HISTORY` lines.
The manifest maps each file to its exact hash, author, and license header.

- Andy Westrate [westrate]
- James Jessiman
- Mark Kennedy [mkennedy]
- Niels Karsdorp [nielsk]
- Paul Easter [pneaster]
- Tore Eriksson [Tore_Eriksson]
- Willy Tschager [Holly-Wood]

## Project wrapper and modifications

`cannon.mpd` is a **Voxys-authored placement wrapper**, containing a single red
instance of the existing LDraw shortcut. It is not an official LEGO set
reconstruction and is not represented as an original model by Andy Westrate.
The source shortcut's colour inheritance makes the base red (LDraw colour 4);
its barrel keeps the source's dark-grey colour (8) and original fixed tilt.
No referenced part geometry has been edited or substituted in this directory.

The wrapper is new project content; it is not an unchanged third-party download.
Any later mesh conversion, material changes, or game placement must be recorded
separately as derived output and retain the relevant source attribution.

LEGO is a trademark of the LEGO Group. These are community-authored LDraw files;
the package does not indicate LEGO Group sponsorship or endorsement.

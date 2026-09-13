# G-A browser continuation and visible home, r05

**Saved-world continuation, default entry, canvas input and representative render presence passed.** This checks the first useful home; it is not final art approval or a sustained performance benchmark.

Executed package `adventure-c7e884ef1ae075e8` from `build-adventure-g-a/web-r03`, served on the same isolated port 42752. This reused only [r04's confirmed saved checkpoint](../browser-r04/README.md) and its test-only browser profile. It did not replay the passed gathering/building/furniture sequence or fabricate a new world. The short continuation took 34.05 seconds.

## Verified results

- Actual Save and ordinary Continue restored the same world, all house parts, ten wood in the chest, remaining supplies, equipped hammer and registered bed exactly.
- Navigating to the bare `/index.html` URL selected adventure by default and restored that same confirmed home.
- One ordinary nonbuilding canvas click left the pointer unlocked, kept the actual input state uncaptured, and changed no parts or inventory.
- The character walked out through the existing doorway. The previously gathered wood pile remained depleted after reload.
- The single [exterior capture](home-exterior.png) now visibly contains the house, open doorway, robot, supply piles and the main LEGO landscape. Root independently reviewed this image and accepted render presence. No further captures were taken.
- Chrome 152.0.0.0 selected AMD RDNA-3 with `isFallbackAdapter=false`. WASM heap was 536,870,912 bytes before and after. The final adaptive canvas backing size was 1297 × 811; the browser viewport/capture is 1280 × 800. No page exceptions or uncaptured/validation GPU errors were reported. The owned browser closed normally and the GPU lane was handed to the native checker.

## Evidence and limits

[summary.json](summary.json) contains exact read-only observations and ordinary control traces. Its earlier construction stages are retained from r04's confirmed checkpoint; the new continuation starts after those stages. [executed-driver.mjs](executed-driver.mjs) is the exact r05 script. [package-sha256.json](package-sha256.json) pins every executed package file. No game actions or state setters were called directly by the driver, no archives were edited, and no input events or counters were injected.

The earlier [r04 image failure](../browser-r04/README.md) remains preserved. Root corrected opaque-scene initialization, terrain/object depth composition and associated render guards before this new image. This is a justified verification of an actual rendering change, not a repeated camera search.

Frame-rate text in the image is incidental UI output, not a percentile benchmark. This record does not certify 1024-part performance, interiors' final lighting, environment art polish, quests, enemies or co-op. The implementation gate and commit remain root's responsibility.

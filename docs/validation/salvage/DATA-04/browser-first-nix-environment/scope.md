# Browser startup failed before game initialization

The hardware Chrome GPU process exited with signal11 (exit139) repeatedly when
launched inside the Nix development shell. WebGPU returned no suitable adapter;
no game session or journey ran. This attempt is retained, not a pass.

The prior successful browser recipe uses the normal host environment (Nix is
required for native build/runtime libraries, not the installed Chrome). A fresh
run outside Nix will distinguish environment contamination from a game defect.
The initial observation alone does not identify the exact crashing library.

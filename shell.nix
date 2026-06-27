{ pkgs ? import <nixpkgs> {} }:

let
  runtimeLibs = with pkgs; [
    libGL
    libx11
    libxcursor
    libxext
    libxfixes
    libxi
    libxinerama
    libxrandr
    libxrender
    libxkbcommon
    libdrm
    mesa
    vulkan-loader
    wayland
  ];

  runtimeLibPath = pkgs.lib.makeLibraryPath runtimeLibs;

  nixBazelrc = pkgs.writeText "voxys-nix.bazelrc" (
    pkgs.lib.concatMapStringsSep "\n"
      (dep: "build --linkopt=-L${pkgs.lib.getLib dep}/lib")
      runtimeLibs
  );

  bazel = pkgs.writeShellScriptBin "bazel" ''
    exec ${pkgs.bazelisk}/bin/bazelisk --bazelrc=${nixBazelrc} "$@"
  '';
in
pkgs.mkShell {
  name = "voxys-dev";

  nativeBuildInputs = with pkgs; [
    bazel
    binutils
    cmake
    curl
    gcc
    git
    gnumake
    ninja
    pkg-config
    python3
    unzip
    uv
    vulkan-tools
    wget
    which
    zstd
  ];

  buildInputs = runtimeLibs ++ (with pkgs; [
    cargo
    rustc
    xorgproto
  ]);

  shellHook = ''
    export PROJECT_ROOT="$PWD"
    export CC="${pkgs.gcc}/bin/gcc"
    export CXX="${pkgs.gcc}/bin/g++"
    export LD_LIBRARY_PATH="${runtimeLibPath}:$PROJECT_ROOT/third_party/wgpu-native/dist/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export WGPU_BACKEND="''${WGPU_BACKEND:-vulkan}"

    if [ -z "''${VK_ICD_FILENAMES:-}" ] && [ -d "${pkgs.mesa}/share/vulkan/icd.d" ]; then
      export VK_ICD_FILENAMES="$(printf "%s:" ${pkgs.mesa}/share/vulkan/icd.d/*.json | sed 's/:$//')"
    fi
    export VK_DRIVER_FILES="''${VK_DRIVER_FILES:-$VK_ICD_FILENAMES}"

    echo "voxys Nix shell"
    echo "  setup deps: ./scripts/fetch_deps.sh"
    echo "  build:      bazel build //:voxy_native"
    echo "  benchmark:  bazel run //:voxy_native -- --benchmark --no-validation"
  '';
}

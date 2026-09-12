package(default_visibility = ["//visibility:public"])

filegroup(
    name = "cove_hud_font_notice",
    srcs = ["fonts/cove-hud/LICENSE.debian", "fonts/cove-hud/README.md"],
)

filegroup(
    name = "salvage_material_metric",
    srcs = [
        "salvage/fixture-material-metric-r01.json",
        "salvage/fixture-material-shading-r02.json",
        "salvage/fixture-cove-r01.json",
        "salvage/cove-bricks-r02.json",
        "salvage/cove-workshop-r03.json",
        "salvage/cove-workshop-r04.json",
        "salvage/cove-workshop-r05.json",
        "salvage/cove-workshop-r06.json",
    ] + glob([
        "salvage/bricks/r02/*/cooked/*",
        "salvage/toy-art/r01/*/cooked/*",
        "salvage/toy-art/r02/*/cooked/*",
        "salvage/toy-art/r03/*/cooked/*",
        "salvage/toy-art/r04/*/cooked/*",
        "salvage/toy-art/r05/*/cooked/*",
        "salvage/mechanisms/r01/*/cooked/*",
        "salvage/material-calibration/r02/pontoon-r02/cooked/*",
        "salvage/material-calibration/r02/kit/winch/cooked/*",
        "salvage/material-calibration/r02/kit/helm/cooked/*",
        "salvage/material-calibration/r04/kit/winch/cooked/*",
        "salvage/material-calibration/r04/kit/helm/cooked/*",
    ]),
)

filegroup(
    name = "salvage_material_calibration",
    srcs = ["salvage/fixture-material-calibration-r01.json"] + glob([
        "salvage/material-calibration/r01/pontoon/cooked/*",
        "salvage/material-calibration/r01/kit/winch/cooked/*",
    ]),
)

filegroup(
    name = "salvage_functional_kit",
    srcs = [
        "salvage/fixture-kit-narrow.json",
        "salvage/fixture-kit-broad.json",
        "salvage/fixture-kit-cargo.json",
    ] + glob(["salvage/functional-kit/r09/*/cooked/*"]),
)

filegroup(
    name = "salvage_hierarchy_probe",
    srcs = [
        "salvage/fixture-hierarchy-v1.json",
        "salvage/hierarchy_probe/candidates/v1-r02/cooked/cook-manifest.json",
        "salvage/hierarchy_probe/candidates/v1-r02/cooked/gameplay.json",
        "salvage/hierarchy_probe/candidates/v1-r02/cooked/lod-1.vmesh",
        "salvage/hierarchy_probe/candidates/v1-r02/source/golden.json",
    ],
)

filegroup(
    name = "salvage_asset_fixture",
    srcs = [
        "salvage/fixture-pontoon-v2.json",
        "salvage/fixture-pontoon-assembly.json",
        "salvage/fixture-pontoon-rotations-a.json",
        "salvage/fixture-pontoon-rotations-b.json",
        "salvage/pontoon/release-candidates/v2-rc01/cooked/cook-manifest.json",
        "salvage/pontoon/release-candidates/v2-rc01/cooked/gameplay.json",
        "salvage/pontoon/release-candidates/v2-rc01/cooked/lod-1.vmesh",
        "salvage/pontoon/release-candidates/v2-rc01/cooked/lod-2.vmesh",
        "salvage/pontoon/release-candidates/v2-rc01/cooked/lod-3.vmesh",
    ],
)

# Offline authoring fixture; this does not add it to the shipped scene assets.
filegroup(
    name = "salvage_runtime_probe",
    srcs = [
        "salvage/runtime_probe_v1/cook-manifest.json",
        "salvage/runtime_probe_v1/gameplay.json",
        "salvage/runtime_probe_v1/lod-9007199254740997.vmesh",
    ],
)

filegroup(
    name = "salvage_authoring_probe",
    srcs = ["salvage/authoring_probe/probe.glb"],
)

filegroup(
    name = "data",
    srcs = [
        "moto/bike.glb",
        "moto/bike.vmesh",
        "moto/rider.glb",
        "moto/rider.vmesh",
        "moto/track.glb",
        "moto/track.vmesh",
        "generated/ocean_environment.png",
        "generated/td_seed_1234_2048_albedo.jpg",
        "generated/td_seed_1234_8192.ldh",
        "lego_shore.ldh",
        "materials/Grass001_1K-JPG_Color.jpg",
        "materials/Grass001_1K-JPG_NormalGL.jpg",
        "materials/Grass001_1K-JPG_Roughness.jpg",
        "materials/Ground037_1K-JPG_Color.jpg",
        "materials/Ground037_1K-JPG_NormalGL.jpg",
        "materials/Ground037_1K-JPG_Roughness.jpg",
        "materials/Ground054_1K-JPG_Color.jpg",
        "materials/Ground054_1K-JPG_NormalGL.jpg",
        "materials/Ground054_1K-JPG_Roughness.jpg",
        "materials/Rock050_1K-JPG_Color.jpg",
        "materials/Rock050_1K-JPG_NormalGL.jpg",
        "materials/Rock050_1K-JPG_Roughness.jpg",
    ],
)

filegroup(
    name = "performance_golden",
    srcs = ["canyon_8k.ldh"],
)

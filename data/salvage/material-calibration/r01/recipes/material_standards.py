"""Shared material calibration constants; no Blender or runtime dependencies.

Opaque calibration deliberately omits microdetail until metric UVs are authored.
It is not the finished wet/glass material implementation. See MATERIALS.md.
"""

LEGACY = 'legacy'
OPAQUE_CALIBRATION = 'opaque-calibration-v1'
PROFILES = (LEGACY, OPAQUE_CALIBRATION)

# Perceptual roughness (the shader squares it for GGX alpha), not alpha itself.
OPAQUE_RESPONSE = {
    'cream': {'family': 'molded_plastic', 'roughness': .34, 'metallic': 0.},
    'teal': {'family': 'molded_plastic', 'roughness': .40, 'metallic': 0.},
    'coral': {'family': 'molded_plastic', 'roughness': .36, 'metallic': 0.},
    'slate': {'family': 'dark_polymer', 'roughness': .73, 'metallic': 0.},
    'steel': {'family': 'bare_metal', 'roughness': .30, 'metallic': 1.},
}


def unorm8(value):
    """Round a declared normalized calibration value to its actual stored byte."""
    return int(value * 255 + .5)


def region_bytes(name, srgb_hex):
    response = OPAQUE_RESPONSE[name]
    if len(srgb_hex) != 6 or any(c not in '0123456789ABCDEF' for c in srgb_hex):
        raise ValueError('calibration color must be six uppercase hex digits')
    return (tuple(int(srgb_hex[i:i+2], 16) for i in (0, 2, 4)) + (255,),
            (255, unorm8(response['roughness']), unorm8(response['metallic']), 255))

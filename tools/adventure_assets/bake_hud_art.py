#!/usr/bin/env python3
"""Bake the installed game meshes into a shared, transparent HUD art atlas.

Uses Blender 4.1 CPU Cycles, never a GPU. The installed triangle positions,
split normals, node transforms and runtime material palette are authoritative.
The baked pieces use no substitute geometry, extra bevels or recolored defaults.
Three separately generated action icons are packed from checked-in originals.

python3 tools/adventure_assets/bake_hud_art.py --blender <blender executable>
python3 tools/adventure_assets/bake_hud_art.py --check
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "data/art/adventure-hud-r01"
HEADER = ROOT / "src/render/generated/adventure_hud_art.hpp"
FONT = ROOT / "data/fonts/adventure-hud/DMSans.ttf"
SOURCES = {
    "building": "data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh",
    "door": "data/adventure/door-r01/cooked/door-leaf-lod0.vmesh",
    "builder": "data/adventure/builder-r01/human.vmesh",
}
RECIPE = "installed-mesh-cpu-studio-hud-r01"
PIECES = [(i % 8 * 128, i // 8 * 128, 128, 128) for i in range(15)]
PORTRAIT = (0, 288, 192, 192)
MINIMAP = (256, 256, 256, 256)
CONTROL_REGIONS = [(896, 128, 64, 64), (960, 128, 64, 64), (896, 192, 64, 64)]
CONTROL_NAMES = ["motorbike", "cannon", "save-building"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def runtime_palette():
    text = (ROOT / "src/game/adventure/adventure_runtime.cpp").read_text()
    pairs = re.findall(r'\{"(adventure_(?:cream|teal|wood|coral|slate))",0x([0-9a-f]{6})\}', text)
    assert len(pairs) == 5, "Expected the five explicit runtime building colors"
    assert "material.roughnessFactor=std::max(.6f,material.roughnessFactor);material.metallicFactor=0;" in text, \
        "Runtime material response changed; update the bake deliberately"
    return dict(pairs)


def read_mesh(path):
    raw = Path(path).read_bytes()
    assert raw[:8] == b"VOXYMESH"
    h = struct.unpack_from("<14I13Q", raw, 8)
    assert h[0] == 1 and h[3] == 72 and h[5] in (2, 4) and not h[10]
    vo, io, so, mo, _, no = h[14:20]
    strings = h[24]
    def name(offset):
        return raw[strings + offset:raw.index(0, strings + offset)].decode()
    materials = []
    for i in range(h[7]):
        at = mo + i * 128
        assert not any(raw[at + 100:at + 104]), "Textured material needs explicit texture handling"
        values = struct.unpack_from("<12f", raw, at)
        materials.append(dict(name=name(struct.unpack_from("<I", raw, at + 124)[0]),
                              base=values[:4], metallic=values[4], roughness=values[5]))
    nodes = []
    for i in range(h[8]):
        n = struct.unpack_from("<10fiIiI", raw, no + i * 64)
        assert n[12] == -1
        nodes.append(dict(translation=n[:3], rotation=n[3:7], scale=n[7:10],
                          parent=n[10], mesh=n[11], name=name(n[13])))
    return dict(vertices=[struct.unpack_from("<3f", raw, vo + i * h[3]) for i in range(h[2])],
                normals=[struct.unpack_from("<3f", raw, vo + i * h[3] + 12) for i in range(h[2])],
                indices=struct.unpack_from("<" + ("H" if h[5] == 2 else "I") * h[4], raw, io),
                index_stride=h[5], submeshes=[struct.unpack_from("<4I", raw, so + i * 16) for i in range(h[6])],
                materials=materials, nodes=nodes)


def blender_worker(config_path):
    import bpy
    from mathutils import Matrix, Quaternion, Vector
    config = json.loads(Path(config_path).read_text())
    out = Path(config["output"])
    out.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = config["samples"]
    scene.cycles.seed = 19
    scene.cycles.use_animated_seed = False
    scene.cycles.use_denoising = True
    scene.cycles.denoiser = "OPENIMAGEDENOISE"
    if hasattr(scene.cycles, "denoising_use_gpu"):
        scene.cycles.denoising_use_gpu = False
    scene.cycles.max_bounces = 6
    scene.cycles.diffuse_bounces = 3
    scene.cycles.glossy_bounces = 3
    scene.render.threads_mode = "FIXED"
    scene.render.threads = config["threads"]
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.render.film_transparent = True
    scene.render.image_settings.compression = 75
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "Medium High Contrast"
    scene.view_settings.exposure = -.85
    scene.view_settings.gamma = 1
    scene.world = bpy.data.worlds.new("neutral studio fill")
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background")
    background.inputs["Color"].default_value = (.72, .78, .85, 1)
    background.inputs["Strength"].default_value = .25
    scene.render.use_file_extension = True
    # Canonical +Y up/-Z forward -> Blender +Z up/+Y forward.
    canonical = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    builder_basis = Matrix.Diagonal((-1., 1., -1., 1.))
    def linear(byte):
        x = byte / 255
        return x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4
    def import_asset(key):
        data = read_mesh(config["sources"][key])
        mats = []
        for m in data["materials"]:
            mat = bpy.data.materials.new(m["name"])
            mat.use_nodes = True
            bsdf = mat.node_tree.nodes.get("Principled BSDF")
            base = m["base"]
            roughness, metallic = m["roughness"], m["metallic"]
            if key == "building":
                color = config["palette"][m["name"]]
                base = tuple(linear(int(color[i:i+2], 16)) for i in (0, 2, 4)) + (1.,)
                roughness, metallic = max(.6, roughness), 0
            bsdf.inputs["Base Color"].default_value = base
            bsdf.inputs["Roughness"].default_value = roughness
            bsdf.inputs["Metallic"].default_value = metallic
            mats.append(mat)
        transforms = {}
        def world(index):
            if index in transforms:
                return transforms[index]
            n = data["nodes"][index]
            q = n["rotation"]
            local = Matrix.LocRotScale(Vector(n["translation"]), Quaternion((q[3], *q[:3])), Vector(n["scale"]))
            transforms[index] = world(n["parent"]) @ local if n["parent"] >= 0 else local
            return transforms[index]
        objects = []
        for index, node in enumerate(data["nodes"]):
            if node["mesh"] == 0xffffffff:
                continue
            faces, face_materials = [], []
            for start, count, material, mesh in data["submeshes"]:
                if mesh != node["mesh"]:
                    continue
                for offset in range(start // data["index_stride"], start // data["index_stride"] + count, 3):
                    faces.append(data["indices"][offset:offset+3])
                    face_materials.append(material)
            used = sorted({i for face in faces for i in face})
            remap = {old: new for new, old in enumerate(used)}
            mesh = bpy.data.meshes.new(node["name"])
            mesh.from_pydata([data["vertices"][i] for i in used], [], [tuple(remap[i] for i in f) for f in faces])
            mesh.update()
            for mat in mats:
                mesh.materials.append(mat)
            for polygon, material in zip(mesh.polygons, face_materials):
                polygon.material_index = material
                polygon.use_smooth = True
            # VMESH has already split vertices at hard edges. Preserve its
            # manufactured corner normals; never recompute or smooth the shape.
            mesh.normals_split_custom_set_from_vertices([data["normals"][i] for i in used])
            obj = bpy.data.objects.new(node["name"], mesh)
            scene.collection.objects.link(obj)
            obj.matrix_world = canonical @ (builder_basis if key == "builder" else Matrix.Identity(4)) @ world(index)
            obj.hide_render = True
            objects.append((node["mesh"], obj))
        return objects
    assets = {key: import_asset(key) for key in config["sources"]}
    def aim(obj, target):
        obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()
    lights = []
    for label, position, power, size in [
        ("broad key", (-3.5, 4.0, 6.0), 480, 3.5),
        ("cool fill", (4., 1.0, 2.5), 90, 4.0),
        ("plastic rim", (-1.0, -3.0, 4.0), 450, 2.0),
    ]:
        bpy.ops.object.light_add(type="AREA", location=position)
        light = bpy.context.object
        light.name = label
        light.data.energy = power
        light.data.shape = "DISK"
        light.data.size = size
        lights.append((light, Vector(position), power, size))
    bpy.ops.object.camera_add()
    camera = bpy.context.object
    camera.data.type = "ORTHO"
    camera.data.lens = 70
    scene.camera = camera
    all_objects = [o for group in assets.values() for _, o in group]
    def render(name, objects, portrait=False):
        for obj in all_objects:
            obj.hide_render = obj not in objects
        points = [obj.matrix_world @ v.co for obj in objects for v in obj.data.vertices]
        low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
        high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
        center = (low + high) * .5
        if portrait:
            center = Vector((0, 0, 1.375))
            extent = .82
            direction = Vector((-.16, 1., .04)).normalized()
        else:
            direction = Vector((6., 7., 5.6)).normalized()
            # Measure true projected extents: tall door and long beam retain
            # honest proportions and identical breathing room in every tile.
            rotation = (-direction).to_track_quat("-Z", "Y").to_matrix()
            projected = [rotation.transposed() @ (p - center) for p in points]
            extent = max(max(p[a] for p in projected) - min(p[a] for p in projected) for a in (0, 1)) / .84
        camera.location = center + direction * max(10, extent * 4)
        aim(camera, center)
        camera.data.ortho_scale = extent
        scene.view_settings.exposure = -.2 if portrait else -.85
        for light, relative, power, size in lights:
            # Keep identical photographic exposure across the real-size pieces.
            light.location = center + relative * extent
            light.data.energy = power * extent * extent
            light.data.size = size * extent
            aim(light, center)
        resolution = 576 if portrait else 384
        scene.render.resolution_x = resolution
        scene.render.resolution_y = resolution
        scene.render.filepath = str(out / (name + ".png"))
        bpy.ops.render.render(write_still=True)
        print("HUD_ART_RENDERED", name, flush=True)
    for kind in config.get("only", list(range(1, 16))):
        if kind == 0:
            continue
        selected = [obj for mesh, obj in assets["building"] if mesh == (kind - 1 if kind < 15 else 3)]
        if kind == 15:
            selected += [obj for _, obj in assets["door"]]
        render(f"piece-{kind:02d}", selected)
    if not config.get("only") or 0 in config["only"]:
        render("builder-portrait", [obj for _, obj in assets["builder"]], portrait=True)
    (out / "renderer.json").write_text(json.dumps(dict(blender=bpy.app.version_string,
        engine="Cycles", device="CPU", samples=scene.cycles.samples, seed=19,
        splitNormals="installed VMESH normals", builderBasis="render-to-canonical12; flip X/Z"), indent=2) + "\n")


def font_art():
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(str(FONT), 32)
    # DM Sans axes are optical size, then weight. Use real medium weight;
    # never synthesize bold by repeated or offset glyph draws.
    font.set_variation_by_axes([32, 550])
    coverage = Image.new("L", (512, 256))
    draw = ImageDraw.Draw(coverage)
    glyphs = []
    x, y, row = 2, 2, 0
    for code in range(32, 127):
        char = chr(code)
        left, top, right, bottom = font.getbbox(char)
        width, height = right-left, bottom-top
        if x+width+2 > 512:
            x, y, row = 2, y+row+4, 0
        assert y+height+2 <= 256, "Font must fit its dedicated atlas region"
        draw.text((x-left, y-top), char, font=font, fill=255)
        glyphs.append((x+512, y+256, width, height, left, top, round(font.getlength(char)*64)))
        x += width+4
        row = max(row, height)
    rgba = Image.new("RGBA", coverage.size, (255, 255, 255, 0))
    rgba.putalpha(coverage)
    return rgba, glyphs


def make_header(png, glyphs):
    text = """// Generated by tools/adventure_assets/bake_hud_art.py. Do not edit.
// Baked installed meshes, generated action icons and DM Sans; PNG top-left coordinates.
#pragma once
#include <array>
#include <cstdint>
namespace voxy::render::adventure_hud_art {
inline constexpr uint32_t width=1024,height=512;
struct Region {uint16_t x,y,width,height;};
inline constexpr std::array<Region,15> pieces{{
"""
    text += "".join("    {" + ",".join(map(str, region)) + "},\n" for region in PIECES) + "}};\n"
    text += "inline constexpr Region portrait{0,288,192,192};\ninline constexpr Region minimap{256,256,256,256};\n"
    text += "// Motorbike, cannon, save: generated semantic action art; never building geometry.\n"
    text += "inline constexpr std::array<Region,3> controlRegions{{\n"
    text += "".join("    {" + ",".join(map(str, region)) + "},\n" for region in CONTROL_REGIONS) + "}};\n"
    text += "inline constexpr uint32_t fontPixels=32,fontWeight=550;\n"
    text += "struct Glyph {uint16_t x,y,width,height;int16_t left,top;uint16_t advance64;};\n"
    text += "inline constexpr std::array<Glyph,95> glyphs{{\n"
    text += "".join("    {" + ",".join(map(str, glyph)) + "},\n" for glyph in glyphs) + "}};\n"
    text += f"inline constexpr std::array<uint8_t,{len(png)}> png{{{{\n"
    text += "".join("    " + ",".join(f"0x{v:02x}" for v in png[i:i+24]) + ",\n" for i in range(0, len(png), 24))
    return text + "}};\n} // namespace voxy::render::adventure_hud_art\n"


def package(rendered):
    from PIL import Image, ImageDraw, __version__ as pillow_version
    OUTPUT.mkdir(parents=True, exist_ok=True)
    sprites = OUTPUT / "sprites"
    sprites.mkdir(exist_ok=True)
    atlas = Image.new("RGBA", (1024, 512))
    entries = []
    for name, region in [(f"piece-{i+1:02d}", r) for i, r in enumerate(PIECES)] + [("builder-portrait", PORTRAIT)]:
        original = Image.open(rendered / (name + ".png")).convert("RGBA")
        sprite = original.resize(region[2:], Image.Resampling.LANCZOS)
        sprite.save(sprites / (name + ".png"), optimize=True)
        atlas.paste(sprite, region[:2])
        entries.append(dict(name=name, rect=region, sha256=sha(sprites / (name + ".png"))))
    font_image, glyphs = font_art()
    atlas.paste(font_image, (512, 256))
    controls = []
    for name, region in zip(CONTROL_NAMES, CONTROL_REGIONS):
        source = OUTPUT / "generated-controls" / (name + "-original.png")
        original = Image.open(source).convert("RGBA")
        assert original.getchannel("A").getextrema() == (0, 255), "Generated control needs genuine transparency"
        # Preserve all generated alpha and artwork. Only proportional sizing
        # and atlas placement happen here; no repainting or background removal.
        sprite = original.resize(region[2:], Image.Resampling.LANCZOS)
        sprite.save(sprites / ("control-" + name + ".png"), optimize=True)
        atlas.paste(sprite, region[:2])
        controls.append(dict(name=name, rect=region, source=str(source.relative_to(ROOT)),
                             sourceSha256=sha(source), file="sprites/control-"+name+".png",
                             sha256=sha(sprites / ("control-" + name + ".png"))))
    atlas.save(OUTPUT / "atlas.png", optimize=True)
    png = (OUTPUT / "atlas.png").read_bytes()
    HEADER.write_text(make_header(png, glyphs))
    # Review only. The runtime atlas itself is fully transparent around art.
    review = Image.new("RGBA", (1024, 512), "#172c37")
    review.alpha_composite(atlas)
    draw = ImageDraw.Draw(review)
    for i, region in enumerate(PIECES):
        draw.text((region[0]+7, region[1]+5), str(i+1), fill="#dbe9ee")
    draw.text((256, 272), "Reserved for live minimap", fill="#91a6ad")
    review.convert("RGB").save(OUTPUT / "review.png", optimize=True)
    manifest = dict(schema=1, recipe=RECIPE, atlas=dict(file="atlas.png", width=1024, height=512,
        sha256=sha(OUTPUT / "atlas.png")), renderer=json.loads((rendered / "renderer.json").read_text()),
        sources={path: sha(ROOT / path) for path in SOURCES.values()},
        generator=dict(file=str(Path(__file__).relative_to(ROOT)), sha256=sha(__file__)),
        font=dict(file=str(FONT.relative_to(ROOT)), sha256=sha(FONT), pixels=32, weight=550,
                  opticalSize=32, glyphs="ASCII32..126", region=[512,256,512,256], license="data/fonts/adventure-hud/OFL.txt"),
        paletteSrgb=runtime_palette(), materialPolicy="Exact installed materials; building runtime warm palette/roughness override. No mesh changes.",
        filtering=dict(renderPixels=384, portraitRenderPixels=576, filter="Pillow LANCZOS premultiplied RGBA", pillow=pillow_version),
        pieces=entries[:15], portrait=entries[15], reservedMinimap=MINIMAP,
        controls=controls, controlPromptsSha256=sha(OUTPUT / "generated-controls/prompts.json"),
        header=dict(file=str(HEADER.relative_to(ROOT)), sha256=sha(HEADER)), reviewSha256=sha(OUTPUT / "review.png"),
        reproduction="python3 tools/adventure_assets/bake_hud_art.py --blender <Blender4.1 executable> --samples 64 --threads 3")
    (OUTPUT / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Packed HUD art: {len(png)} PNG bytes, 15 actual pieces, one actual portrait and three generated action icons")


def check():
    from PIL import Image
    m = json.loads((OUTPUT / "provenance.json").read_text())
    assert m["recipe"] == RECIPE and m["paletteSrgb"] == runtime_palette()
    assert m["generator"]["sha256"] == sha(__file__), "Generator changed; rebake assets"
    for path, digest in m["sources"].items():
        assert sha(ROOT / path) == digest, path
    for item in m["pieces"] + [m["portrait"]]:
        path = OUTPUT / "sprites" / (item["name"] + ".png")
        assert sha(path) == item["sha256"] and Image.open(path).getbbox()
    assert m["controlPromptsSha256"] == sha(OUTPUT / "generated-controls/prompts.json")
    for item in m["controls"]:
        assert sha(ROOT / item["source"]) == item["sourceSha256"]
        assert sha(OUTPUT / item["file"]) == item["sha256"]
    assert sha(OUTPUT / "atlas.png") == m["atlas"]["sha256"]
    assert sha(OUTPUT / "review.png") == m["reviewSha256"]
    assert sha(HEADER) == m["header"]["sha256"]
    assert sha(FONT) == m["font"]["sha256"]
    font_image, glyphs = font_art()
    assert HEADER.read_text() == make_header((OUTPUT / "atlas.png").read_bytes(), glyphs)
    atlas = Image.open(OUTPUT / "atlas.png")
    assert atlas.mode == "RGBA" and atlas.size == (1024, 512)
    assert atlas.crop((256, 256, 512, 512)).getbbox() is None
    assert atlas.crop((960, 192, 1024, 256)).getbbox() is None
    for item in m["controls"]:
        x,y,w,h = item["rect"]
        assert atlas.crop((x,y,x+w,y+h)).tobytes() == Image.open(OUTPUT / item["file"]).tobytes()
    assert atlas.crop((512, 256, 1024, 512)).tobytes() == font_image.tobytes()
    print("HUD art source/output hashes, embedded PNG, atlas geometry and reserved regions verified")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--blender", default="/mnt/c/Program Files/Blender Foundation/Blender 4.1/blender.exe")
    p.add_argument("--samples", type=int, default=64)
    p.add_argument("--threads", type=int, default=3)
    p.add_argument("--check", action="store_true")
    p.add_argument("--worker", type=Path)
    p.add_argument("--package", type=Path, help=argparse.SUPPRESS)
    p.add_argument("--only", type=int, nargs="+", help=argparse.SUPPRESS)
    args = p.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else None)
    if args.worker:
        blender_worker(args.worker)
        return
    if args.check:
        check()
        return
    if args.package:
        package(args.package)
        return
    windows = str(args.blender).endswith(".exe") and sys.platform != "win32"
    temp_root = Path("/mnt/c/Users/m3ndag/AppData/Local/Temp") if windows else None
    staging = Path(tempfile.mkdtemp(prefix="voxys-hud-art-", dir=temp_root))
    def external(path):
        return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip() if windows else str(path)
    sources = {}
    for key, source in SOURCES.items():
        destination = staging / (key + ".vmesh")
        shutil.copyfile(ROOT / source, destination)
        sources[key] = external(destination)
    shutil.copyfile(__file__, staging / "bake_hud_art.py")
    rendered = staging / "rendered"
    config = dict(sources=sources, output=external(rendered), samples=args.samples,
                  threads=args.threads, palette=runtime_palette())
    if args.only:
        config["only"] = args.only
    (staging / "config.json").write_text(json.dumps(config))
    print("CPU render staging:", staging, flush=True)
    subprocess.run([args.blender, "--background", "--factory-startup", "--threads", str(args.threads),
        "--python-exit-code", "1", "--python", external(staging / "bake_hud_art.py"), "--", "--worker",
        external(staging / "config.json")], check=True)
    if not args.only:
        package(rendered)


if __name__ == "__main__":
    main()

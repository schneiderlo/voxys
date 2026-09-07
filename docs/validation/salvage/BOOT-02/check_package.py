#!/usr/bin/env python3
"""Check a staged browser package without starting a browser or GPU.

Checks local HTML/CSS/module references, the standalone shader-check manifest,
all preload ranges and bytes, route configs and their terrain references, and
the actual WASM export symbols behind the KEEPALIVE browser controls.
This is a packaging gate, not evidence of successful GPU startup or gameplay.
"""

import argparse
import configparser
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parents[4]
ENTRY = re.compile(
    r'''["']?filename["']?\s*:\s*["']([^"']+)["']\s*,\s*'''
    r'''["']?start["']?\s*:\s*(\d+)\s*,\s*'''
    r'''["']?end["']?\s*:\s*(\d+)'''
)
MODULE_IMPORT = re.compile(
    r'''(?:\bfrom\s*|\bimport\s*\(?\s*)["']([./][^"']+)["']'''
)
CSS_URL = re.compile(r'''url\(\s*["']?([^)'"\s]+)["']?\s*\)''')
CONTROLS = ("voxy_lego_action", "voxy_get_lego_hud_json", "voxy_get_heap_used_bytes",
            "voxy_salvage_preview_action", "voxy_get_salvage_preview_json")


class References(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []

    def handle_starttag(self, tag, attrs):
        self.urls.extend(value for key, value in attrs if key in ("href", "src") and value)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def local_file(package, origin, url):
    parsed = urlsplit(url)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return None
    path = unquote(parsed.path)
    result = (package / path.lstrip("/") if path.startswith("/") else origin.parent / path).resolve()
    require(result.is_relative_to(package), f"Reference escapes package: {origin.name}: {url}")
    require(result.is_file(), f"Missing served asset: {origin.name}: {url}")
    require(result.stat().st_size > 0, f"Empty served asset: {url}")
    return result


def check(package, source, node, base_url=None):
    package, source = package.resolve(), source.resolve()
    require((package / "index.html").is_file(), "Missing package index.html")
    referenced = set()
    http_files = set()
    for path in package.rglob("*"):
        if not path.is_file() or path.suffix not in (".html", ".css", ".js", ".mjs"):
            continue
        # Build directories may contain CMake internals. Only shipped web files
        # are inputs here; the generated WASM glue is checked separately below.
        relative = path.relative_to(package)
        if not (source / "web" / relative).is_file():
            continue
        text = path.read_text()
        urls = []
        if path.suffix == ".html":
            parser = References()
            parser.feed(text)
            urls.extend(parser.urls)
        if path.suffix in (".js", ".mjs", ".html"):
            urls.extend(MODULE_IMPORT.findall(text))
        if path.suffix in (".css", ".html"):
            urls.extend(CSS_URL.findall(text))
        for url in urls:
            target = local_file(package, path, url)
            if target:
                referenced.add(target)

    # Make's old partial copy silently omitted whole routes. Require each source
    # web asset, including pages not linked from the main menu.
    for path in (source / "web").rglob("*"):
        if path.is_file() and path.name != "BUILD":
            staged = package / path.relative_to(source / "web")
            require(staged.is_file(), f"Unpackaged web asset: {path.name}")
            http_files.add(staged)
            # index.html may legitimately rewrite build IDs and the Bazel name.
            if path.name != "index.html":
                require(staged.read_bytes() == path.read_bytes(), f"Stale web asset: {path.name}")

    parity = package / "shader_parity_manifest.js"
    shader_names = re.findall(r'''["']([^"']+\.wgsl)["']''', parity.read_text())
    require(bool(shader_names), "Empty shader parity manifest")
    for name in shader_names:
        staged = local_file(package, parity, f"/shaders/{name}")
        http_files.add(staged)
        require(staged.read_bytes() == (source / "shaders" / name).read_bytes(),
                f"Stale served shader: {name}")

    glue_files = [path for path in referenced if re.fullmatch(r"voxy_wasm(?:_cc)?\.js", path.name)]
    require(len(glue_files) == 1, "Expected one WASM loader referenced by index.html")
    glue_path = glue_files[0]
    glue = glue_path.read_text()
    data_path, wasm_path = glue_path.with_suffix(".data"), glue_path.with_suffix(".wasm")
    require(data_path.is_file() and wasm_path.is_file(), "Missing WASM binary or preload package")
    http_files.update((glue_path, data_path, wasm_path))
    package_bytes = data_path.read_bytes()
    entries = {}
    previous_end = 0
    for name, start, end in ENTRY.findall(glue):
        start, end = int(start), int(end)
        require(name not in entries, f"Duplicate preload entry: {name}")
        require(0 <= start <= end <= len(package_bytes), f"Preload range out of bounds: {name}")
        require(start == previous_end, f"Gap or overlap before preload entry: {name}")
        previous_end = end
        original = (source / name.lstrip("/")).resolve()
        require(original.is_relative_to(source) and original.is_file(), f"Unknown preload source: {name}")
        data = package_bytes[start:end]
        require(data == original.read_bytes(), f"Stale/corrupt preload bytes: {name}")
        entries[name] = data
    require(bool(entries) and previous_end == len(package_bytes), "Incomplete preload manifest")

    configs = {"voxy.cfg"}
    configs.update(re.findall(r'''["']([^"']+\.cfg)["']''', (source / "web/index.html").read_text()))
    for name in sorted(configs):
        require(f"/{name}" in entries, f"Missing route config in WASM preload: {name}")
        config = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=("#", ";"))
        config.read_string(entries[f"/{name}"].decode())
        for key in ("heightmap", "albedo", "lightmap"):
            asset = config.get("terrain", key, fallback="").strip().strip('"')
            if asset:
                require("/" + asset.lstrip("/") in entries,
                        f"Missing configured terrain asset: {name}: {key}={asset}")

    # Compile the real binary to discover exports. Release/Closure builds rename
    # WASM symbols, so follow each public Module property to its binary symbol.
    subprocess.run([node, "--check", str(glue_path)], check=True, text=True, capture_output=True)
    result = subprocess.run([node, "-e", """
const fs = require('node:fs');
const module = new WebAssembly.Module(fs.readFileSync(process.argv[1]));
console.log(JSON.stringify(WebAssembly.Module.exports(module).filter(x => x.kind === 'function').map(x => x.name)));
""", str(wasm_path)], check=True, text=True, capture_output=True)
    exports = set(json.loads(result.stdout))
    bindings = {}
    for name in CONTROLS:
        prop = rf'''Module(?:\._{name}|\[["']_{name}["']\])'''
        match = re.search(prop + r'''\s*=\s*[\w$]+(?:\.([\w$]+)|\[["']([^"']+)["']\])''', glue)
        require(match is not None, f"Missing JS control binding: {name}")
        symbol = match.group(1) or match.group(2)
        require(symbol in exports, f"JS control has no function export: {name} -> {symbol}")
        bindings[name] = symbol

    if base_url:
        for path in sorted(http_files):
            relative = path.relative_to(package).as_posix()
            with urlopen(base_url.rstrip("/") + "/" + quote(relative), timeout=15) as response:
                require(response.status == 200, f"HTTP {response.status}: {relative}")
                if path.suffix == ".wasm":
                    require(response.headers.get_content_type() == "application/wasm",
                            f"Wrong WASM MIME type: {relative}")
                received = hashlib.file_digest(response, "sha256").digest()
            with path.open("rb") as original:
                expected = hashlib.file_digest(original, "sha256").digest()
            require(received == expected, f"HTTP content differs from package: {relative}")

    return {"package": str(package), "referenced_static_assets": len(referenced),
            "served_shaders": len(shader_names), "preload_files": len(entries),
            "preload_bytes": len(package_bytes), "route_configs": sorted(configs),
            "keepalive_exports": bindings,
            "http_files_verified": len(http_files) if base_url else 0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--node", default="node")
    parser.add_argument("--url", help="Also fetch and compare every web asset, parity shader, and WASM artifact")
    args = parser.parse_args()
    try:
        result = check(args.package, args.source, args.node, args.url)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print(json.dumps(result, indent=2))
    print("PASS: served assets, source-matching preloads, route configs, and KEEPALIVE exports")


if __name__ == "__main__":
    main()

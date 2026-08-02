#!/usr/bin/env python3
"""Procedurally author the RIDGEBREAK bike and rigid rider as binary glTF 2.0."""

import argparse
import json
import math
import os
import struct
import zlib
from dataclasses import dataclass, field


Vec2 = tuple[float, float]
Vec3 = tuple[float, float, float]


def add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a: Vec3, s: float) -> Vec3:
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def length(a: Vec3) -> float:
    return math.sqrt(dot(a, a))


def norm(a: Vec3) -> Vec3:
    size = length(a)
    if size <= 1.0e-12:
        raise ValueError("cannot normalize a zero vector")
    return mul(a, 1.0 / size)


def basis(axis: Vec3, ring_handedness: bool = True) -> tuple[Vec3, Vec3, Vec3]:
    d = norm(axis)
    ref = (1.0, 0.0, 0.0) if abs(d[0]) < 0.85 else (0.0, 1.0, 0.0)
    u = norm(sub(ref, mul(d, dot(ref, d))))
    v = cross(d, u) if ring_handedness else cross(u, d)
    return d, u, v


@dataclass
class Mesh:
    positions: list[Vec3] = field(default_factory=list)
    normals: list[Vec3] = field(default_factory=list)
    uvs: list[Vec2] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)

    def vertex(self, p: Vec3, n: Vec3, uv: Vec2) -> int:
        self.positions.append(p)
        self.normals.append(norm(n))
        self.uvs.append(uv)
        return len(self.positions) - 1

    def merge(self, other: "Mesh") -> None:
        base = len(self.positions)
        self.positions.extend(other.positions)
        self.normals.extend(other.normals)
        self.uvs.extend(other.uvs)
        self.indices.extend(base + i for i in other.indices)


def cylinder(a: Vec3, b: Vec3, radius_a: float, radius_b: float | None = None,
             segments: int = 16) -> Mesh:
    """Closed smooth cylinder/frustum between arbitrary endpoints."""
    radius_b = radius_a if radius_b is None else radius_b
    axis = sub(b, a)
    axis_len = length(axis)
    d, u, v = basis(axis)
    mesh = Mesh()
    slope = (radius_b - radius_a) / axis_len
    rings: list[list[int]] = []
    for end, (center, radius) in enumerate(((a, radius_a), (b, radius_b))):
        ring = []
        for i in range(segments + 1):
            theta = 2.0 * math.pi * i / segments
            radial = add(mul(u, math.cos(theta)), mul(v, math.sin(theta)))
            p = add(center, mul(radial, radius))
            n = sub(radial, mul(d, slope))
            ring.append(mesh.vertex(p, n, (i / segments, float(end))))
        rings.append(ring)
    for i in range(segments):
        p0, p1 = rings[0][i], rings[0][i + 1]
        p2, p3 = rings[1][i + 1], rings[1][i]
        mesh.indices.extend((p0, p1, p2, p0, p2, p3))

    for center, normal, ring, reverse in (
        (a, mul(d, -1.0), rings[0], True),
        (b, d, rings[1], False),
    ):
        ci = mesh.vertex(center, normal, (0.5, 0.5))
        cap = []
        for i in range(segments + 1):
            theta = 2.0 * math.pi * i / segments
            radius = radius_a if center == a else radius_b
            radial = add(mul(u, math.cos(theta)), mul(v, math.sin(theta)))
            cap.append(mesh.vertex(add(center, mul(radial, radius)), normal,
                                   (0.5 + 0.5 * math.cos(theta),
                                    0.5 + 0.5 * math.sin(theta))))
        for i in range(segments):
            if reverse:
                mesh.indices.extend((ci, cap[i + 1], cap[i]))
            else:
                mesh.indices.extend((ci, cap[i], cap[i + 1]))
    return mesh


def torus(center: Vec3, axis: Vec3, major: float, minor: float,
          major_segments: int = 48, minor_segments: int = 16) -> Mesh:
    """Closed torus around an arbitrary axis."""
    d, u, v = basis(axis)
    mesh = Mesh()
    rows: list[list[int]] = []
    for i in range(major_segments + 1):
        theta = 2.0 * math.pi * i / major_segments
        radial = add(mul(u, math.cos(theta)), mul(v, math.sin(theta)))
        row = []
        for j in range(minor_segments + 1):
            phi = 2.0 * math.pi * j / minor_segments
            n = add(mul(radial, math.cos(phi)), mul(d, math.sin(phi)))
            p = add(center, add(mul(radial, major + minor * math.cos(phi)),
                                mul(d, minor * math.sin(phi))))
            row.append(mesh.vertex(p, n, (i / major_segments, j / minor_segments)))
        rows.append(row)
    for i in range(major_segments):
        for j in range(minor_segments):
            a, b = rows[i][j], rows[i + 1][j]
            c, d0 = rows[i + 1][j + 1], rows[i][j + 1]
            mesh.indices.extend((a, b, c, a, c, d0))
    return mesh


def ellipsoid(center: Vec3, axis: Vec3, half_length: float, radius_u: float,
              radius_v: float, longitude: int = 32, latitude: int = 16) -> Mesh:
    """Closed ellipsoid whose long local axis is `axis`."""
    d, u, v = basis(axis, ring_handedness=False)
    mesh = Mesh()
    north = mesh.vertex(add(center, mul(d, half_length)), d, (0.5, 0.0))
    rows: list[list[int]] = []
    for lat in range(1, latitude):
        phi = math.pi * lat / latitude
        cp, sp = math.cos(phi), math.sin(phi)
        row = []
        for lon in range(longitude + 1):
            theta = 2.0 * math.pi * lon / longitude
            ct, st = math.cos(theta), math.sin(theta)
            p = add(center, add(mul(d, half_length * cp),
                                add(mul(u, radius_u * sp * ct),
                                    mul(v, radius_v * sp * st))))
            n = add(mul(d, cp / half_length),
                    add(mul(u, sp * ct / radius_u), mul(v, sp * st / radius_v)))
            row.append(mesh.vertex(p, n, (lon / longitude, lat / latitude)))
        rows.append(row)
    south = mesh.vertex(sub(center, mul(d, half_length)), mul(d, -1.0), (0.5, 1.0))
    for lon in range(longitude):
        mesh.indices.extend((north, rows[0][lon + 1], rows[0][lon]))
    for row in range(len(rows) - 1):
        for lon in range(longitude):
            a, b = rows[row][lon], rows[row][lon + 1]
            c, d0 = rows[row + 1][lon + 1], rows[row + 1][lon]
            mesh.indices.extend((a, b, c, a, c, d0))
    for lon in range(longitude):
        mesh.indices.extend((south, rows[-1][lon], rows[-1][lon + 1]))
    return mesh


def ellipsoid_between(a: Vec3, b: Vec3, radius_u: float, radius_v: float,
                      longitude: int = 32, latitude: int = 16) -> Mesh:
    return ellipsoid(mul(add(a, b), 0.5), sub(b, a), length(sub(b, a)) * 0.5,
                     radius_u, radius_v, longitude, latitude)


def append_quad(mesh: Mesh, points: tuple[Vec3, Vec3, Vec3, Vec3],
                normals: tuple[Vec3, Vec3, Vec3, Vec3], uvs: tuple[Vec2, Vec2, Vec2, Vec2]) -> None:
    order = [0, 1, 2, 3]
    face = cross(sub(points[1], points[0]), sub(points[2], points[0]))
    if dot(face, add(add(normals[0], normals[1]), add(normals[2], normals[3]))) < 0.0:
        order = [0, 3, 2, 1]
    ids = [mesh.vertex(points[i], normals[i], uvs[i]) for i in order]
    mesh.indices.extend((ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]))


def box(center: Vec3, size: Vec3) -> Mesh:
    """Closed hard-edged box."""
    mesh = Mesh()
    hx, hy, hz = size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    x0, x1 = center[0] - hx, center[0] + hx
    y0, y1 = center[1] - hy, center[1] + hy
    z0, z1 = center[2] - hz, center[2] + hz
    faces = (
        (((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)), (1.0, 0.0, 0.0)),
        (((x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (x0, y0, z0)), (-1.0, 0.0, 0.0)),
        (((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)), (0.0, 1.0, 0.0)),
        (((x0, y0, z1), (x0, y0, z0), (x1, y0, z0), (x1, y0, z1)), (0.0, -1.0, 0.0)),
        (((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)), (0.0, 0.0, 1.0)),
        (((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)), (0.0, 0.0, -1.0)),
    )
    for points, normal in faces:
        append_quad(mesh, points, (normal,) * 4, ((0, 0), (1, 0), (1, 1), (0, 1)))
    return mesh


def bank_wedge(length: float, width: float, height: float) -> Mesh:
    """Low open-bottom berm bank; both visible faces terminate at grade."""
    mesh = Mesh()
    half_length = length * 0.5
    half_width = width * 0.5
    inner = -half_width
    outer = half_width
    z0, z1 = -half_length, half_length
    slope_normal = norm((-height, width, 0.0))
    append_quad(
        mesh,
        ((inner, 0.0, z0), (inner, 0.0, z1),
         (outer, height, z1), (outer, height, z0)),
        (slope_normal,) * 4,
        ((0, 0), (0, 1), (1, 1), (1, 0)))
    append_quad(
        mesh,
        ((outer, 0.0, z1), (outer, 0.0, z0),
         (outer, height, z0), (outer, height, z1)),
        ((1.0, 0.0, 0.0),) * 4,
        ((0, 0), (1, 0), (1, 1), (0, 1)))
    return mesh


def fender(center: Vec3, radius_inner: float, radius_outer: float, width: float,
           angle0: float, angle1: float, segments: int = 36) -> Mesh:
    """Closed radial arc shell around the X-axis."""
    mesh = Mesh()
    half = width * 0.5
    for i in range(segments):
        t0 = angle0 + (angle1 - angle0) * i / segments
        t1 = angle0 + (angle1 - angle0) * (i + 1) / segments
        r0, r1 = (0.0, math.cos(t0), math.sin(t0)), (0.0, math.cos(t1), math.sin(t1))
        for radius, sign in ((radius_outer, 1.0), (radius_inner, -1.0)):
            points = tuple(add(center, (x, radius * r[1], radius * r[2]))
                           for x, r in ((-half, r0), (half, r0), (half, r1), (-half, r1)))
            normals = tuple(mul(r, sign) for r in (r0, r0, r1, r1))
            append_quad(mesh, points, normals, ((0, 0), (1, 0), (1, 1), (0, 1)))
        for x, normal in ((-half, (-1.0, 0.0, 0.0)), (half, (1.0, 0.0, 0.0))):
            points = tuple(add(center, (x, radius * r[1], radius * r[2]))
                           for radius, r in ((radius_inner, r0), (radius_outer, r0),
                                             (radius_outer, r1), (radius_inner, r1)))
            append_quad(mesh, points, (normal,) * 4, ((0, 0), (1, 0), (1, 1), (0, 1)))
    for theta, sign in ((angle0, -1.0), (angle1, 1.0)):
        radial = (0.0, math.cos(theta), math.sin(theta))
        tangent = (0.0, -math.sin(theta) * sign, math.cos(theta) * sign)
        points = tuple(add(center, (x, radius * radial[1], radius * radial[2]))
                       for x, radius in ((-half, radius_inner), (half, radius_inner),
                                         (half, radius_outer), (-half, radius_outer)))
        append_quad(mesh, points, (tangent,) * 4, ((0, 0), (1, 0), (1, 1), (0, 1)))
    return mesh


@dataclass(frozen=True)
class Finish:
    name: str
    color: tuple[float, float, float, float]
    metallic: float
    roughness: float
    alpha_mode: str = "OPAQUE"
    double_sided: bool = False
    texture_kind: str = ""
    unlit: bool = False


RUBBER = Finish("rubber", (0.018, 0.021, 0.024, 1.0), 0.0, 0.72)
CHROME = Finish("chrome", (0.72, 0.78, 0.82, 1.0), 1.0, 0.16)
ALUMINUM = Finish("aluminum", (0.38, 0.43, 0.47, 1.0), 0.9, 0.28)
DARK_METAL = Finish("dark_metal", (0.055, 0.065, 0.072, 1.0), 0.75, 0.38)
ORANGE = Finish("ridge_orange", (0.82, 0.065, 0.008, 1.0), 0.28, 0.30)
CYAN = Finish("ridge_cyan", (0.0, 0.48, 0.58, 1.0), 0.16, 0.34)
WHITE = Finish("number_white", (0.92, 0.95, 0.92, 1.0), 0.05, 0.32)
SEAT = Finish("seat", (0.035, 0.045, 0.055, 1.0), 0.0, 0.58)
GOLD = Finish("shock_gold", (0.84, 0.47, 0.06, 1.0), 0.85, 0.22)
SUIT_NAVY = Finish("suit_navy", (0.025, 0.07, 0.16, 1.0), 0.02, 0.72)
SUIT_ORANGE = Finish("suit_orange", (0.82, 0.055, 0.008, 1.0), 0.02, 0.68)
SUIT_CYAN = Finish("suit_cyan", (0.0, 0.48, 0.58, 1.0), 0.02, 0.65)
HELMET = Finish("helmet", (0.72, 0.025, 0.008, 1.0), 0.10, 0.42)
VISOR = Finish("visor", (0.025, 0.07, 0.09, 1.0), 0.65, 0.12)
SHADOW_SOFT = Finish("shadow_soft", (0.08, 0.065, 0.045, 0.10), 0.0, 1.0,
                     "BLEND", True, "shadow")
CONTACT = Finish("tire_contact", (0.08, 0.065, 0.045, 0.12), 0.0, 0.95,
                 "BLEND", True, "contact")
DUST = Finish("dust", (0.66, 0.49, 0.29, 0.13), 0.0, 1.0,
              "BLEND", True, "dust", False)
TRACK_ORANGE = Finish("track_orange", (1.0, 1.0, 1.0, 1.0), 0.08, 0.48,
                      "OPAQUE", False, "track_paint", False)
TRACK_DARK = Finish("track_dark", (0.028, 0.032, 0.034, 1.0), 0.05, 0.76)
TRACK_WHITE = Finish("track_white", (1.0, 1.0, 1.0, 1.0), 0.72, 0.48,
                     "OPAQUE", False, "fence")
BALE = Finish("bale", (1.0, 1.0, 1.0, 1.0), 0.0, 0.92,
              "OPAQUE", False, "bale")
ROCK = Finish("track_rock", (1.0, 1.0, 1.0, 1.0), 0.0, 0.88,
              "OPAQUE", False, "rock")
SCRUB = Finish("track_scrub", (1.0, 1.0, 1.0, 1.0), 0.0, 0.86,
               "OPAQUE", False, "scrub")
ARCH_BANNER = Finish("ridgebreak_banner", (1.0, 1.0, 1.0, 1.0), 0.0, 0.62,
                     "OPAQUE", False, "banner")
RUT = Finish("rut_soil", (0.62, 0.48, 0.31, 0.20), 0.0, 0.98,
             "BLEND", True, "rut")
COMPACTION = Finish("landing_compaction", (0.48, 0.35, 0.21, 0.46), 0.0, 1.0,
                    "BLEND", True, "compaction")
BERM = Finish("hero_berm", (1.0, 1.0, 1.0, 1.0), 0.0, 0.96,
              "OPAQUE", False, "berm")
FOOTING = Finish("arch_footing", (1.0, 1.0, 1.0, 1.0), 0.0, 0.91,
                 "OPAQUE", False, "rock")


@dataclass
class Part:
    name: str
    mesh: Mesh
    finish: Finish


def joined(*meshes: Mesh) -> Mesh:
    result = Mesh()
    for mesh in meshes:
        result.merge(mesh)
    return result


def decal_quad(half_x: float, half_z: float) -> Mesh:
    mesh = Mesh()
    append_quad(mesh,
                ((-half_x, 0, -half_z), (-half_x, 0, half_z),
                 (half_x, 0, half_z), (half_x, 0, -half_z)),
                ((0, 1, 0),) * 4,
                ((0, 0), (0, 1), (1, 1), (1, 0)))
    return mesh


def particle_cross() -> Mesh:
    mesh = Mesh()
    append_quad(mesh,
                ((-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0)),
                ((0, 0, 1),) * 4,
                ((0, 0), (1, 0), (1, 1), (0, 1)))
    append_quad(mesh,
                ((0, -1, -1), (0, 1, -1), (0, 1, 1), (0, -1, 1)),
                ((1, 0, 0),) * 4,
                ((0, 0), (0, 1), (1, 1), (1, 0)))
    diagonal = 0.7071067811865476
    append_quad(mesh,
                ((-diagonal, -1, diagonal), (diagonal, -1, -diagonal),
                 (diagonal, 1, -diagonal), (-diagonal, 1, diagonal)),
                ((diagonal, 0, diagonal),) * 4,
                ((0, 0), (1, 0), (1, 1), (0, 1)))
    return mesh


def build_track() -> list[Part]:
    """Thirteen origin-rooted modular meshes. Their order is a runtime ABI."""
    arch = joined(
        box((-3.4, 2.3, 0.0), (0.30, 4.6, 0.30)),
        box((3.4, 2.3, 0.0), (0.30, 4.6, 0.30)),
        box((0.0, 4.38, 0.0), (7.10, 0.44, 0.34)),
        cylinder((-3.28, 3.64, 0.0), (-2.66, 4.16, 0.0),
                 0.09, segments=10),
        cylinder((3.28, 3.64, 0.0), (2.66, 4.16, 0.0),
                 0.09, segments=10),
    )
    stake = joined(
        cylinder((0.0, 0.0, 0.0), (0.0, 2.2, 0.0), 0.075, 0.055, 12),
        box((0.28, 1.82, 0.0), (0.55, 0.54, 0.08)),
    )
    chevron = joined(
        cylinder((-0.74, 1.65, 0.0), (0.08, 1.02, 0.0), 0.14, segments=10),
        cylinder((0.08, 1.02, 0.0), (-0.74, 0.39, 0.0), 0.14, segments=10),
        cylinder((0.12, 1.65, 0.0), (0.94, 1.02, 0.0), 0.14, segments=10),
        cylinder((0.94, 1.02, 0.0), (0.12, 0.39, 0.0), 0.14, segments=10),
    )
    hay_bale = joined(
        box((0.0, 0.42, 0.0), (1.5, 0.84, 0.72)),
        cylinder((-0.43, 0.42, -0.39), (-0.43, 0.42, 0.39), 0.035, segments=8),
        cylinder((0.43, 0.42, -0.39), (0.43, 0.42, 0.39), 0.035, segments=8),
    )
    tire_stack = joined(
        *(torus((0.0, 0.17 + layer * 0.25, 0.0), (0.0, 1.0, 0.0),
                0.34, 0.12, 24, 10) for layer in range(3))
    )
    fence = joined(
        cylinder((-2.9, 0.0, 0.0), (-2.9, 2.0, 0.0), 0.09, segments=10),
        cylinder((2.9, 0.0, 0.0), (2.9, 2.0, 0.0), 0.09, segments=10),
        box((0.0, 1.55, 0.0), (5.9, 0.12, 0.14)),
        box((0.0, 0.62, 0.0), (5.9, 0.12, 0.14)),
    )
    rock = ellipsoid((0.0, 0.48, 0.0), (0.18, 1.0, 0.11),
                     0.55, 0.82, 0.62, 12, 7)
    scrub = joined(
        ellipsoid((0.0, 0.56, 0.0), (0.12, 1.0, 0.05),
                  0.62, 0.28, 0.22, 10, 6),
        ellipsoid((-0.28, 0.42, 0.04), (-0.28, 1.0, 0.16),
                  0.48, 0.22, 0.18, 10, 6),
        ellipsoid((0.31, 0.39, -0.06), (0.34, 1.0, -0.12),
                  0.44, 0.21, 0.17, 10, 6),
    )
    hero_berm = bank_wedge(7.2, 1.6, 0.48)
    arch_footing = joined(
        box((-3.4, 0.11, 0.0), (0.68, 0.22, 0.68)),
        box((3.4, 0.11, 0.0), (0.68, 0.22, 0.68)),
    )
    return [
        Part("start_arch", arch, TRACK_ORANGE),
        Part("marker_stake", stake, TRACK_ORANGE),
        Part("chevron", chevron, TRACK_ORANGE),
        Part("hay_bale", hay_bale, BALE),
        Part("tire_stack", tire_stack, TRACK_DARK),
        Part("fence_panel", fence, TRACK_WHITE),
        Part("rock", rock, ROCK),
        Part("scrub", scrub, SCRUB),
        Part("arch_banner", box((0.0, 3.82, -0.19), (4.2, 0.58, 0.07)),
             ARCH_BANNER),
        Part("rut_strip", decal_quad(0.18, 4.0), RUT),
        Part("landing_patch", decal_quad(2.2, 3.0), COMPACTION),
        Part("hero_berm", hero_berm, BERM),
        Part("arch_footing", arch_footing, FOOTING),
    ]


def wheel_rim(center: Vec3) -> Mesh:
    pieces = [torus(center, (1, 0, 0), 0.245, 0.016, 48, 12)]
    for i in range(20):
        angle = 2.0 * math.pi * i / 20
        outer = (0.026 if i & 1 else -0.026,
                 center[1] + 0.232 * math.cos(angle),
                 center[2] + 0.232 * math.sin(angle))
        inner_angle = angle + (0.18 if i & 1 else -0.18)
        inner = (-outer[0], center[1] + 0.043 * math.cos(inner_angle),
                 center[2] + 0.043 * math.sin(inner_angle))
        pieces.append(cylinder(inner, outer, 0.0042, segments=10))
    return joined(*pieces)


def knobby_tire(center: Vec3) -> Mesh:
    pieces = [torus(center, (1, 0, 0), 0.255, 0.055, 48, 18)]
    # Staggered shoulder knobs read as tread from every angle without the
    # full-width crossbars that turned the rear tyre into a ladder silhouette.
    for i in range(12):
        angle = 2.0 * math.pi * (i + 0.5) / 12
        radial = (0.0, math.cos(angle), math.sin(angle))
        y = center[1] + 0.304 * radial[1]
        z = center[2] + 0.304 * radial[2]
        for side in (-1.0, 1.0):
            phase = 0.012 * side * math.sin(angle)
            pieces.append(ellipsoid(
                (side * 0.048, y + phase * radial[1],
                 z + phase * radial[2]), radial,
                0.012, 0.019, 0.024, 8, 5))
    return joined(*pieces)


def build_bike() -> list[Part]:
    rear, front = (0.0, 0.31, -0.71), (0.0, 0.31, 0.71)
    parts = [
        Part("wheel_f", knobby_tire(front), RUBBER),
        Part("wheel_r", knobby_tire(rear), RUBBER),
        Part("rim_f", wheel_rim(front), CHROME),
        Part("rim_r", wheel_rim(rear), CHROME),
        Part("hub_f", cylinder((-0.075, 0.31, 0.71), (0.075, 0.31, 0.71), 0.042, segments=24), ALUMINUM),
        Part("hub_r", cylinder((-0.085, 0.31, -0.71), (0.085, 0.31, -0.71), 0.048, segments=24), ALUMINUM),
        Part("disc_f", cylinder((-0.057, 0.31, 0.71), (-0.047, 0.31, 0.71), 0.115, segments=36), CHROME),
        Part("disc_r", cylinder((0.052, 0.31, -0.71), (0.061, 0.31, -0.71), 0.095, segments=32), CHROME),
    ]

    frame_tubes = (
        ((0, 0.73, -0.34), (0, 0.82, 0.30), 0.036),
        ((0, 0.82, 0.30), (0, 0.48, 0.18), 0.040),
        ((0, 0.48, 0.18), (0, 0.54, -0.35), 0.040),
        ((0, 0.54, -0.35), (0, 0.73, -0.34), 0.034),
        ((-0.13, 0.73, -0.35), (-0.13, 0.54, 0.02), 0.026),
        ((0.13, 0.73, -0.35), (0.13, 0.54, 0.02), 0.026),
        ((-0.13, 0.54, 0.02), (0, 0.82, 0.30), 0.024),
        ((0.13, 0.54, 0.02), (0, 0.82, 0.30), 0.024),
    )
    parts.append(Part("frame", joined(*(cylinder(a, b, r, segments=18)
                                        for a, b, r in frame_tubes)), ORANGE))

    engine = joined(
        ellipsoid((0, 0.55, -0.02), (0, 1, 0), 0.19, 0.22, 0.18, 32, 16),
        ellipsoid((0, 0.62, 0.06), (0, 0, 1), 0.17, 0.17, 0.16, 28, 14),
        cylinder((-0.20, 0.54, -0.02), (0.20, 0.54, -0.02), 0.105, segments=24),
    )
    parts.extend((
        Part("engine", engine, DARK_METAL),
        Part("tank", ellipsoid_between((0, 0.81, -0.06), (0, 0.83, 0.38),
                                        0.205, 0.17, 36, 18), ORANGE),
        Part("seat", ellipsoid_between((0, 0.78, -0.52), (0, 0.81, -0.08),
                                        0.165, 0.075, 32, 14), SEAT),
        Part("fender_f", fender(front, 0.325, 0.355, 0.19, -1.02, 1.12, 40), ORANGE),
        Part("fender_r", fender(rear, 0.325, 0.355, 0.20, -0.88, 0.92, 36), ORANGE),
        Part("fork_l", joined(
            cylinder((-0.105, 0.30, 0.71), (-0.105, 0.88, 0.43), 0.026, segments=20),
            cylinder((-0.105, 0.66, 0.54), (-0.105, 0.96, 0.39), 0.034, segments=20)), CHROME),
        Part("fork_r", joined(
            cylinder((0.105, 0.30, 0.71), (0.105, 0.88, 0.43), 0.026, segments=20),
            cylinder((0.105, 0.66, 0.54), (0.105, 0.96, 0.39), 0.034, segments=20)), CHROME),
    ))

    swing = joined(
        cylinder((-0.13, 0.52, -0.23), (-0.10, 0.31, -0.71), 0.030, 0.024, 18),
        cylinder((0.13, 0.52, -0.23), (0.10, 0.31, -0.71), 0.030, 0.024, 18),
        cylinder((-0.13, 0.52, -0.23), (0.13, 0.52, -0.23), 0.034, segments=18),
        cylinder((-0.10, 0.31, -0.71), (0.10, 0.31, -0.71), 0.025, segments=18),
    )
    spring = [cylinder((0, 0.55, -0.35), (0, 0.77, -0.22), 0.025, segments=18)]
    spring.extend(torus((0, 0.59 + i * 0.027, -0.326 + i * 0.016), (0, 0.86, 0.51),
                        0.050, 0.007, 24, 8) for i in range(7))
    bar = joined(
        cylinder((-0.43, 1.01, 0.43), (0.43, 1.01, 0.43), 0.014, segments=18),
        cylinder((-0.24, 0.96, 0.39), (-0.32, 1.01, 0.43), 0.014, segments=18),
        cylinder((0.24, 0.96, 0.39), (0.32, 1.01, 0.43), 0.014, segments=18),
    )
    exhaust = joined(
        cylinder((0.14, 0.58, 0.12), (0.22, 0.68, -0.10), 0.030, segments=18),
        cylinder((0.22, 0.68, -0.10), (0.23, 0.68, -0.48), 0.034, 0.045, 18),
        ellipsoid_between((0.23, 0.68, -0.46), (0.24, 0.71, -0.75),
                          0.068, 0.058, 28, 14),
        cylinder((0.24, 0.71, -0.75), (0.24, 0.71, -0.85), 0.040, 0.032, 18),
    )
    parts.extend((
        Part("swingarm", swing, ALUMINUM),
        Part("shock", joined(*spring), GOLD),
        Part("bar", bar, CHROME),
        Part("grip_l", cylinder((-0.48, 1.01, 0.43), (-0.38, 1.01, 0.43), 0.026, segments=20), RUBBER),
        Part("grip_r", cylinder((0.38, 1.01, 0.43), (0.48, 1.01, 0.43), 0.026, segments=20), RUBBER),
        Part("exhaust", exhaust, CHROME),
        Part("peg_l", cylinder((-0.34, 0.47, -0.13), (-0.16, 0.47, -0.13), 0.026, segments=16), DARK_METAL),
        Part("peg_r", cylinder((0.16, 0.47, -0.13), (0.34, 0.47, -0.13), 0.026, segments=16), DARK_METAL),
        Part("numberplate_f", ellipsoid((0, 0.92, 0.46), (0, 1, 0), 0.15,
                                         0.18, 0.025, 28, 12), WHITE),
        Part("numberplate_l", ellipsoid((-0.18, 0.74, -0.34), (0, 0, 1), 0.19,
                                         0.12, 0.022, 28, 12), CYAN),
        Part("numberplate_r", ellipsoid((0.18, 0.74, -0.34), (0, 0, 1), 0.19,
                                         0.12, 0.022, 28, 12), CYAN),
        Part("radiator_l", box((-0.18, 0.68, 0.16), (0.045, 0.25, 0.22)), DARK_METAL),
        Part("radiator_r", box((0.18, 0.68, 0.16), (0.045, 0.25, 0.22)), DARK_METAL),
        Part("triple_clamp", joined(
            cylinder((-0.17, 0.91, 0.42), (0.17, 0.91, 0.42), 0.028, segments=18),
            cylinder((-0.15, 0.98, 0.39), (0.15, 0.98, 0.39), 0.025, segments=18)), ALUMINUM),
        Part("drive_detail", joined(
            torus((0.105, 0.31, -0.71), (1, 0, 0), 0.105, 0.010, 28, 8),
            cylinder((0.105, 0.31, -0.71), (0.125, 0.31, -0.71),
                     0.080, segments=24),
            cylinder((0.115, 0.395, -0.68), (0.115, 0.55, -0.22),
                     0.009, segments=10),
            cylinder((0.115, 0.225, -0.68), (0.115, 0.48, -0.20),
                     0.009, segments=10)), GOLD),
        Part("shroud_l", ellipsoid_between((-0.205, 0.70, -0.02),
                                             (-0.215, 0.80, 0.30),
                                             0.115, 0.025, 28, 12), ORANGE),
        Part("shroud_r", ellipsoid_between((0.205, 0.70, -0.02),
                                             (0.215, 0.80, 0.30),
                                             0.115, 0.025, 28, 12), ORANGE),
        Part("shadow_soft", decal_quad(0.58, 0.92), SHADOW_SOFT),
        Part("contact_patch_f", decal_quad(0.075, 0.23), CONTACT),
        Part("contact_patch_r", decal_quad(0.085, 0.25), CONTACT),
    ))
    dust_mesh = particle_cross()
    for index in range(16):
        parts.append(Part(f"fx_dust_{index}", dust_mesh, DUST))
    return parts


def build_rider() -> list[Part]:
    hip_l, hip_r = (-0.14, 0.91, -0.15), (0.14, 0.91, -0.15)
    knee_l, knee_r = (-0.24, 0.66, 0.17), (0.24, 0.66, 0.17)
    ankle_l, ankle_r = (-0.29, 0.47, -0.10), (0.29, 0.47, -0.10)
    shoulder_l, shoulder_r = (-0.23, 1.36, 0.04), (0.23, 1.36, 0.04)
    elbow_l, elbow_r = (-0.38, 1.18, 0.24), (0.38, 1.18, 0.24)
    hand_l, hand_r = (-0.43, 1.04, 0.43), (0.43, 1.04, 0.43)
    torso = joined(
        ellipsoid_between((0, 0.94, -0.11), (0, 1.24, -0.01),
                          0.175, 0.115, 32, 16),
        ellipsoid_between((0, 1.17, -0.02), (0, 1.45, 0.10),
                          0.235, 0.135, 36, 18))
    helmet = joined(
        ellipsoid((0, 1.62, 0.19), (0, 1, 0), 0.19,
                  0.155, 0.175, 36, 18),
        ellipsoid_between((-0.12, 1.58, 0.26), (-0.10, 1.48, 0.37),
                          0.040, 0.032, 20, 10),
        ellipsoid_between((0.12, 1.58, 0.26), (0.10, 1.48, 0.37),
                          0.040, 0.032, 20, 10))
    return [
        Part("torso", torso, SUIT_NAVY),
        Part("head", helmet, HELMET),
        Part("upperarm_l", ellipsoid_between(shoulder_l, elbow_l, 0.075, 0.064, 28, 14), SUIT_ORANGE),
        Part("upperarm_r", ellipsoid_between(shoulder_r, elbow_r, 0.075, 0.064, 28, 14), SUIT_ORANGE),
        Part("forearm_l", ellipsoid_between(elbow_l, hand_l, 0.070, 0.062, 28, 14), SUIT_CYAN),
        Part("forearm_r", ellipsoid_between(elbow_r, hand_r, 0.070, 0.062, 28, 14), SUIT_CYAN),
        Part("thigh_l", ellipsoid_between(hip_l, knee_l, 0.115, 0.105, 32, 16), SUIT_ORANGE),
        Part("thigh_r", ellipsoid_between(hip_r, knee_r, 0.115, 0.105, 32, 16), SUIT_ORANGE),
        Part("shin_l", ellipsoid_between(knee_l, ankle_l, 0.090, 0.082, 30, 15), SUIT_NAVY),
        Part("shin_r", ellipsoid_between(knee_r, ankle_r, 0.090, 0.082, 30, 15), SUIT_NAVY),
        Part("glove_l", ellipsoid_between((-0.47, 1.04, 0.43), (-0.39, 1.04, 0.43),
                                           0.045, 0.040, 24, 12), SUIT_ORANGE),
        Part("glove_r", ellipsoid_between((0.39, 1.04, 0.43), (0.47, 1.04, 0.43),
                                           0.045, 0.040, 24, 12), SUIT_ORANGE),
        Part("boot_l", ellipsoid_between(ankle_l, (-0.29, 0.46, -0.14),
                                          0.075, 0.065, 28, 14), DARK_METAL),
        Part("boot_r", ellipsoid_between(ankle_r, (0.29, 0.46, -0.14),
                                          0.075, 0.065, 28, 14), DARK_METAL),
        Part("visor", ellipsoid((0, 1.65, 0.345), (1, 0, 0), 0.145,
                                 0.072, 0.025, 28, 12), VISOR),
        Part("helmet_peak", ellipsoid_between(
            (0, 1.67, 0.29), (0, 1.67, 0.48),
            0.11, 0.018, 24, 8), HELMET),
        Part("chest_plate", ellipsoid((0, 1.31, 0.145), (0, 1, 0), 0.20,
                                       0.205, 0.035, 28, 12), WHITE),
        Part("collar", torus((0, 1.48, 0.08), (0, 1, 0),
                              0.145, 0.028, 28, 10), DARK_METAL),
        Part("waist_belt", torus((0, 1.02, -0.10), (0, 1, 0),
                                  0.155, 0.030, 28, 10), SUIT_CYAN),
        Part("knee_guard_l", ellipsoid(knee_l, sub(ankle_l, knee_l),
                                        0.105, 0.105, 0.045, 24, 12), WHITE),
        Part("knee_guard_r", ellipsoid(knee_r, sub(ankle_r, knee_r),
                                        0.105, 0.105, 0.045, 24, 12), WHITE),
        Part("goggle_strap", torus((0, 1.65, 0.19), (0, 1, 0),
                                    0.158, 0.018, 30, 8), SUIT_CYAN),
        Part("boot_buckle_l", cylinder((-0.34, 0.47, -0.10),
                                         (-0.25, 0.47, -0.10),
                                         0.018, segments=12), ALUMINUM),
        Part("boot_buckle_r", cylinder((0.25, 0.47, -0.10),
                                         (0.34, 0.47, -0.10),
                                         0.018, segments=12), ALUMINUM),
    ]


COMPONENT_INFO = {
    5123: ("H", 2),
    5125: ("I", 4),
    5126: ("f", 4),
}
TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}


def validate_mesh(part: Part) -> None:
    mesh = part.mesh
    if not mesh.positions or len(mesh.positions) != len(mesh.normals) or len(mesh.positions) != len(mesh.uvs):
        raise ValueError(f"{part.name}: mismatched or empty vertex streams")
    if not mesh.indices or len(mesh.indices) % 3:
        raise ValueError(f"{part.name}: index stream is not triangles")
    for i, n in enumerate(mesh.normals):
        if not all(math.isfinite(x) for x in mesh.positions[i] + n + mesh.uvs[i]):
            raise ValueError(f"{part.name}: non-finite vertex")
        if not 0.999 <= length(n) <= 1.001:
            raise ValueError(f"{part.name}: non-unit normal")
    for tri in range(0, len(mesh.indices), 3):
        ids = mesh.indices[tri:tri + 3]
        if min(ids) < 0 or max(ids) >= len(mesh.positions):
            raise ValueError(f"{part.name}: triangle index out of range")
        p0, p1, p2 = (mesh.positions[i] for i in ids)
        face = cross(sub(p1, p0), sub(p2, p0))
        if length(face) <= 1.0e-11:
            raise ValueError(f"{part.name}: degenerate triangle {tri // 3}")
        average_normal = add(add(mesh.normals[ids[0]], mesh.normals[ids[1]]), mesh.normals[ids[2]])
        if dot(face, average_normal) <= 0.0:
            raise ValueError(f"{part.name}: clockwise triangle {tri // 3}")


def procedural_rgba_png(kind: str, size: int = 64) -> bytes:
    glyphs = {
        "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
        "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
        "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
        "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
        "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01110"),
        "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
        "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
        "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    }
    text = "RIDGEBREAK"

    def banner_pixel(x: int, y: int) -> tuple[int, int, int, int]:
        # Ten compact 5x7 glyphs fit without touching the sampler-safe border.
        start_x, start_y, glyph_pitch, glyph_scale = 2, 21, 6, 3
        letter_index = (x - start_x) // glyph_pitch
        local_x = (x - start_x) % glyph_pitch
        local_y = (y - start_y) // glyph_scale
        ink = (0 <= letter_index < len(text) and local_x < 5
               and 0 <= y - start_y < 7 * glyph_scale
               and glyphs[text[letter_index]][local_y][local_x] == "1")
        if ink:
            return ((255, 112, 38, 255) if letter_index < 5
                    else (255, 252, 235, 255))
        # Worn navy event fabric with restrained orange pinstripes.
        stripe = y in (17, 45) and 3 <= x < size - 3
        grain = round(5.0 * math.sin(x * 0.79 + y * 1.31))
        return ((190, 55, 18, 255) if stripe
                else (max(24, 35 + grain), max(28, 40 + grain),
                      max(32, 47 + grain), 255))

    opaque_base = {
        "track_paint": (220, 70, 18, 255),
        "fence": (150, 158, 158, 255),
        "bale": (174, 132, 58, 255),
        "rock": (178, 166, 145, 255),
        "scrub": (126, 150, 74, 255),
        "berm": (168, 116, 68, 255),
        "banner": (35, 40, 47, 255),
    }
    pixels = bytearray()
    for y in range(size):
        pixels.append(0)  # PNG filter type: None
        for x in range(size):
            nx = (2.0 * (x + 0.5) / size) - 1.0
            ny = (2.0 * (y + 0.5) / size) - 1.0
            radius = math.sqrt(nx * nx + ny * ny)
            falloff = max(0.0, min(1.0, 1.0 - radius))
            if kind == "shadow":
                alpha = falloff * falloff * (3.0 - 2.0 * falloff)
            elif kind == "contact":
                alpha = falloff ** 1.35
            elif kind == "dust":
                grain = 0.60 + 0.26 * math.sin(x * 1.71 + y * 2.37)
                grain += 0.16 * math.sin(x * 4.13 - y * 1.19)
                clump = 1.0 if ((x * 17 + y * 29 + x * y * 3) % 41) < 5 else 0.0
                alpha = falloff ** 1.55 * max(0.28, min(1.0, grain + clump * 0.34))
            elif kind == "banner":
                rgba = banner_pixel(x, y)
                alpha = None
            elif kind in opaque_base:
                u = 2.0 * math.pi * x / size
                v = 2.0 * math.pi * y / size
                wave = math.sin(u * 5.0 + v * 3.0) + 0.55 * math.sin(u * 13.0 - v * 7.0)
                if kind == "track_paint":
                    chip = ((x * 19 + y * 31 + x * y * 7) % 97) < 9
                    rgba = (67, 62, 52, 255) if chip else (
                        220 + round(10 * wave), 70 + round(5 * wave), 18, 255)
                elif kind == "fence":
                    wire = ((x + 2 * y) % 13 == 0) or ((x - 2 * y) % 13 == 0)
                    value = 176 if wire else 116 + round(9 * wave)
                    rgba = (value, value + 5, value + 6, 255)
                elif kind == "bale":
                    strand = 18 if ((3 * x + y) % 11) < 2 else 0
                    rgba = (145 + strand + round(8 * wave),
                            104 + strand + round(6 * wave), 40, 255)
                elif kind == "rock":
                    value = round(9 * wave)
                    rgba = (178 + value, 166 + value, 145 + value, 255)
                elif kind == "scrub":
                    leaf = 18 if ((x * 11 + y * 7) % 23) < 7 else 0
                    rgba = (112 + leaf, 136 + leaf + round(5 * wave), 64, 255)
                else:  # berm
                    ridge = 13 if ((2 * x + y) % 17) < 4 else 0
                    rgba = (150 + ridge + round(7 * wave),
                            98 + ridge + round(5 * wave), 55, 255)
                alpha = None
            elif kind == "rut":
                across = max(0.0, 1.0 - abs(nx) ** 2.6)
                tread = 0.72 + 0.20 * math.sin(ny * math.pi * 12.0 + nx * 2.0)
                alpha = across ** 2.2 * tread
            elif kind == "compaction":
                ellipse = max(0.0, 1.0 - math.sqrt(nx * nx + (ny * 0.72) ** 2))
                pebble = 0.78 + 0.18 * math.sin(x * 1.83 + y * 2.51)
                alpha = ellipse ** 1.4 * pebble
            else:
                raise ValueError(f"unknown procedural texture kind: {kind}")
            if alpha is not None:
                rgba = (255, 255, 255,
                        round(max(0.0, min(1.0, alpha)) * 255.0))

            # Exact equal borders make every procedural texture safe for the
            # runtime's repeating sampler and its full mip chain.
            if x in (0, size - 1) or y in (0, size - 1):
                rgba = opaque_base.get(kind, (255, 255, 255, 0))
            pixels.extend(rgba)

    # Every opposite edge must match exactly before this image can be used by
    # repeating runtime samplers. The radial field reaches transparent black
    # equally on all four sides, avoiding a dust/shadow seam at any mip.
    stride = size * 4 + 1
    top = pixels[1:1 + size * 4]
    bottom = pixels[(size - 1) * stride + 1:(size - 1) * stride + 1 + size * 4]
    left = b"".join(pixels[row * stride + 1:row * stride + 5]
                    for row in range(size))
    right = b"".join(pixels[row * stride + 1 + (size - 1) * 4:
                            row * stride + 1 + size * 4]
                     for row in range(size))
    if top != bottom or left != right:
        raise ValueError(f"{kind}: generated texture is not edge-identical")

    def chunk(name: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + name + payload
                + struct.pack(">I", zlib.crc32(name + payload) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(pixels), level=9))
            + chunk(b"IEND", b""))


def write_glb(path: str, parts: list[Part]) -> dict[str, int]:
    if len({part.name for part in parts}) != len(parts):
        raise ValueError(f"{path}: duplicate part name")
    blob = bytearray()
    views: list[dict] = []
    accessors: list[dict] = []
    materials: list[dict] = []
    meshes: list[dict] = []
    nodes: list[dict] = []
    images: list[dict] = []
    textures: list[dict] = []
    texture_cache: dict[str, int] = {}

    def align4() -> None:
        blob.extend(b"\0" * ((-len(blob)) & 3))

    def stream(values: list[tuple] | list[int], component_type: int, value_type: str,
               target: int) -> int:
        align4()
        offset = len(blob)
        code, component_size = COMPONENT_INFO[component_type]
        count = TYPE_COUNT[value_type]
        flat = [component for value in values for component in value] if count > 1 else values
        blob.extend(struct.pack("<" + code * len(flat), *flat))
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(flat) * component_size,
                      "target": target})
        columns = [[float(value[c]) for value in values] for c in range(count)] if count > 1 else [[float(v) for v in values]]
        accessor = {"bufferView": len(views) - 1, "byteOffset": 0, "componentType": component_type,
                    "count": len(values), "type": value_type,
                    "min": [min(column) for column in columns], "max": [max(column) for column in columns]}
        accessors.append(accessor)
        return len(accessors) - 1

    def texture_index(kind: str) -> int:
        cached = texture_cache.get(kind)
        if cached is not None:
            return cached
        encoded = procedural_rgba_png(kind)
        align4()
        offset = len(blob)
        blob.extend(encoded)
        views.append({"buffer": 0, "byteOffset": offset,
                      "byteLength": len(encoded)})
        images.append({"name": f"ridgebreak_{kind}",
                       "bufferView": len(views) - 1,
                       "mimeType": "image/png"})
        textures.append({"source": len(images) - 1})
        result = len(textures) - 1
        texture_cache[kind] = result
        return result

    for part in parts:
        validate_mesh(part)
        material_index = len(materials)
        material = {
            "name": f"{part.name}_{part.finish.name}",
            "pbrMetallicRoughness": {
                "baseColorFactor": list(part.finish.color),
                "metallicFactor": part.finish.metallic,
                "roughnessFactor": part.finish.roughness,
            },
        }
        if part.finish.alpha_mode != "OPAQUE":
            material["alphaMode"] = part.finish.alpha_mode
        if part.finish.double_sided:
            material["doubleSided"] = True
        if part.finish.texture_kind:
            material["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": texture_index(part.finish.texture_kind)
            }
        if part.finish.unlit:
            material["extensions"] = {"KHR_materials_unlit": {}}
        materials.append(material)
        position = stream(part.mesh.positions, 5126, "VEC3", 34962)
        normal = stream(part.mesh.normals, 5126, "VEC3", 34962)
        uv = stream(part.mesh.uvs, 5126, "VEC2", 34962)
        index_type = 5123 if len(part.mesh.positions) <= 65535 else 5125
        indices = stream(part.mesh.indices, index_type, "SCALAR", 34963)
        meshes.append({"name": part.name, "primitives": [{
            "attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv},
            "indices": indices, "material": material_index, "mode": 4,
        }]})
        nodes.append({"name": part.name, "mesh": len(meshes) - 1})

    align4()
    document = {
        "asset": {"version": "2.0", "generator": "RIDGEBREAK stdlib procedural author"},
        "scene": 0,
        "scenes": [{"name": "RIDGEBREAK", "nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    if images:
        document["images"] = images
        document["textures"] = textures
    if any(part.finish.unlit for part in parts):
        document["extensionsUsed"] = ["KHR_materials_unlit"]
    json_bytes = json.dumps(document, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    json_bytes += b" " * ((-len(json_bytes)) & 3)
    bin_bytes = bytes(blob)
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    glb = (struct.pack("<4sII", b"glTF", 2, total) +
           struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes +
           struct.pack("<II", len(bin_bytes), 0x004E4942) + bin_bytes)
    with open(path, "wb") as output:
        output.write(glb)
    return {
        "parts": len(parts),
        "triangles": sum(len(p.mesh.indices) // 3 for p in parts),
        "vertices": sum(len(p.mesh.positions) for p in parts),
        "bytes": len(glb),
    }


def validate_glb(path: str) -> dict[str, int]:
    with open(path, "rb") as source:
        data = source.read()
    if len(data) < 28:
        raise ValueError(f"{path}: truncated GLB")
    magic, version, declared_length = struct.unpack_from("<4sII", data)
    if magic != b"glTF" or version != 2 or declared_length != len(data):
        raise ValueError(f"{path}: invalid GLB header")
    chunks = []
    offset = 12
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"{path}: truncated chunk header")
        size, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if size % 4 or offset + size > len(data):
            raise ValueError(f"{path}: invalid chunk range/alignment")
        chunks.append((kind, data[offset:offset + size]))
        offset += size
    if len(chunks) != 2 or chunks[0][0] != 0x4E4F534A or chunks[1][0] != 0x004E4942:
        raise ValueError(f"{path}: expected JSON and BIN chunks")
    document = json.loads(chunks[0][1].decode("utf-8"))
    binary = chunks[1][1]
    if document.get("asset", {}).get("version") != "2.0":
        raise ValueError(f"{path}: not glTF 2.0")
    if document.get("scene") != 0 or len(document.get("scenes", [])) != 1:
        raise ValueError(f"{path}: expected exactly one default scene")
    nodes = document.get("nodes", [])
    roots = document["scenes"][0].get("nodes", [])
    if roots != list(range(len(nodes))) or any("children" in node for node in nodes):
        raise ValueError(f"{path}: nodes are not independent scene roots")
    if document.get("buffers") != [{"byteLength": len(binary)}]:
        raise ValueError(f"{path}: BIN buffer length mismatch")

    views = document.get("bufferViews", [])
    for view in views:
        start = view.get("byteOffset", 0)
        size = view.get("byteLength", 0)
        if view.get("buffer") != 0 or start % 4 or size <= 0 or start + size > len(binary):
            raise ValueError(f"{path}: bufferView out of range or unaligned")

    decoded: list[list[tuple | int | float]] = []
    for accessor in document.get("accessors", []):
        view_index = accessor.get("bufferView", -1)
        if not 0 <= view_index < len(views):
            raise ValueError(f"{path}: accessor bufferView out of range")
        component_type = accessor.get("componentType")
        value_type = accessor.get("type")
        if component_type not in COMPONENT_INFO or value_type not in TYPE_COUNT:
            raise ValueError(f"{path}: unsupported accessor encoding")
        code, component_size = COMPONENT_INFO[component_type]
        width = TYPE_COUNT[value_type]
        count = accessor.get("count", 0)
        byte_offset = accessor.get("byteOffset", 0)
        required = byte_offset + count * width * component_size
        view = views[view_index]
        if count <= 0 or byte_offset % component_size or required > view["byteLength"]:
            raise ValueError(f"{path}: accessor range invalid")
        start = view.get("byteOffset", 0) + byte_offset
        flat = struct.unpack_from("<" + code * count * width, binary, start)
        values = list(flat) if width == 1 else [tuple(flat[i:i + width]) for i in range(0, len(flat), width)]
        columns = [[float(value[c]) for value in values] for c in range(width)] if width > 1 else [[float(v) for v in values]]
        actual_min = [min(column) for column in columns]
        actual_max = [max(column) for column in columns]
        if any(not math.isfinite(v) for column in columns for v in column):
            raise ValueError(f"{path}: non-finite accessor value")
        for key, actual in (("min", actual_min), ("max", actual_max)):
            declared = accessor.get(key)
            if declared is None or len(declared) != width or any(abs(a - float(b)) > 1.0e-6 for a, b in zip(actual, declared)):
                raise ValueError(f"{path}: incorrect accessor {key}")
        decoded.append(values)

    meshes = document.get("meshes", [])
    materials = document.get("materials", [])
    if len(nodes) != len(meshes) or len(nodes) != len(materials):
        raise ValueError(f"{path}: parts do not have one mesh and material each")
    triangles = vertices = 0
    for node_index, node in enumerate(nodes):
        if node.get("mesh") != node_index or not node.get("name"):
            raise ValueError(f"{path}: node/mesh mapping invalid")
        primitives = meshes[node_index].get("primitives", [])
        if len(primitives) != 1:
            raise ValueError(f"{path}: expected one primitive per part")
        primitive = primitives[0]
        attrs = primitive.get("attributes", {})
        if set(attrs) != {"POSITION", "NORMAL", "TEXCOORD_0"} or primitive.get("mode") != 4:
            raise ValueError(f"{path}: primitive attribute set/mode invalid")
        if primitive.get("material") != node_index:
            raise ValueError(f"{path}: primitive material mapping invalid")
        position_accessor = attrs["POSITION"]
        normal_accessor = attrs["NORMAL"]
        uv_accessor = attrs["TEXCOORD_0"]
        if not all(0 <= i < len(decoded) for i in (position_accessor, normal_accessor, uv_accessor)):
            raise ValueError(f"{path}: attribute accessor out of range")
        vertex_count = len(decoded[position_accessor])
        accessor_specs = document["accessors"]
        if (accessor_specs[position_accessor]["componentType"], accessor_specs[position_accessor]["type"]) != (5126, "VEC3"):
            raise ValueError(f"{path}: POSITION is not float VEC3")
        if (accessor_specs[normal_accessor]["componentType"], accessor_specs[normal_accessor]["type"]) != (5126, "VEC3"):
            raise ValueError(f"{path}: NORMAL is not float VEC3")
        if (accessor_specs[uv_accessor]["componentType"], accessor_specs[uv_accessor]["type"]) != (5126, "VEC2"):
            raise ValueError(f"{path}: TEXCOORD_0 is not float VEC2")
        if len(decoded[normal_accessor]) != vertex_count or len(decoded[uv_accessor]) != vertex_count:
            raise ValueError(f"{path}: attribute counts differ")
        index_accessor = primitive.get("indices", -1)
        if not 0 <= index_accessor < len(decoded):
            raise ValueError(f"{path}: missing index accessor")
        if (accessor_specs[index_accessor]["componentType"] not in (5123, 5125) or
                accessor_specs[index_accessor]["type"] != "SCALAR"):
            raise ValueError(f"{path}: indices are not unsigned integer SCALAR")
        index_values = decoded[index_accessor]
        if len(index_values) % 3 or min(index_values) < 0 or max(index_values) >= vertex_count:
            raise ValueError(f"{path}: triangle indices out of range")
        triangles += len(index_values) // 3
        vertices += vertex_count
    return {"parts": len(nodes), "triangles": triangles, "vertices": vertices, "bytes": len(data)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outdir", default="data/moto", help="output directory (default: data/moto)")
    args = parser.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    dust_sheet_alpha = DUST.color[3] * 0.86
    dust_cross_alpha = 1.0 - (1.0 - dust_sheet_alpha) ** 3
    if dust_cross_alpha > 0.35:
        raise ValueError("dust cross exceeds the bounded 0.35 composite alpha")
    if SHADOW_SOFT.color[3] > 0.18 or CONTACT.color[3] > 0.12:
        raise ValueError("grounding cards exceed their bounded AO alpha")
    assets = (("bike.glb", build_bike()), ("rider.glb", build_rider()),
              ("track.glb", build_track()))
    if any(part.name.startswith("fx_chunk_") for part in assets[0][1]):
        raise ValueError("bike.glb: detached dirt chunks are forbidden")
    if any(part.name == "back_plate" for part in assets[1][1]):
        raise ValueError("rider.glb: opaque rectangular back plate is forbidden")
    required = {
        "bike.glb": {"frame", "engine", "tank", "seat", "fender_f", "fender_r", "fork_l",
                     "fork_r", "bar", "wheel_f", "wheel_r", "swingarm", "shock", "exhaust",
                     "peg_l", "peg_r"},
        "rider.glb": {"torso", "head", "upperarm_l", "upperarm_r", "forearm_l", "forearm_r",
                      "thigh_l", "thigh_r", "shin_l", "shin_r"},
        "track.glb": {"start_arch", "marker_stake", "chevron", "hay_bale",
                      "tire_stack", "fence_panel", "rock", "scrub",
                      "arch_banner", "rut_strip", "landing_patch",
                      "hero_berm", "arch_footing"},
    }
    budgets = {"bike.glb": (15000, 60000), "rider.glb": (8000, 30000),
               "track.glb": (500, 12000)}
    for filename, parts in assets:
        missing = required[filename] - {part.name for part in parts}
        if missing:
            raise ValueError(f"{filename}: missing required parts: {', '.join(sorted(missing))}")
        path = os.path.join(args.outdir, filename)
        authored = write_glb(path, parts)
        verified = validate_glb(path)
        if authored != verified:
            raise ValueError(f"{path}: authored and verified counts differ")
        if not budgets[filename][0] <= verified["triangles"] <= budgets[filename][1]:
            raise ValueError(f"{path}: triangle count is outside its budget")
        print(f"{path}: {verified['parts']} parts, {verified['triangles']} triangles, "
              f"{verified['vertices']} vertices, {verified['bytes']} bytes [verified]")


if __name__ == "__main__":
    main()

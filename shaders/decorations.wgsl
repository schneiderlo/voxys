// ═══════════════════════════════════════════════════════════════════════════════
// decorations.wgsl - Instanced Terrain Decoration Shader
// ═══════════════════════════════════════════════════════════════════════════════

struct CameraUniforms {
    viewProj : mat4x4<f32>,
    invViewProj : mat4x4<f32>,
    invView : mat4x4<f32>,
    terrainSize : vec2<f32>,
    invTerrainSize : vec2<f32>,
    metrics : vec4<f32>,
    cameraPos : vec4<f32>,
    invProjParams : vec4<f32>,
    lightDirVS : vec4<f32>,
    frustumPlanes : array<vec4<f32>, 6>,
    lightDirWS : vec4<f32>,
};

struct DecorationInstance {
    baseRadius : vec4<f32>,
    colorHeight : vec4<f32>,
    variant : vec4<f32>,
};

struct DecorationParams {
    useRayDepth : u32,
    rayDepthBias : f32,
    fadeStart : f32,
    fadeEnd : f32,
};

const KIND_BROADLEAF : f32 = 0.0;
const KIND_PINE : f32 = 1.0;
const KIND_GRASS : f32 = 2.0;
const KIND_FLOWER : f32 = 3.0;
const KIND_REED : f32 = 4.0;
const KIND_ROCK : f32 = 5.0;
const KIND_DRY_SHRUB : f32 = 6.0;
const KIND_JUNGLE : f32 = 7.0;
const KIND_ACACIA : f32 = 8.0;
const KIND_CYPRESS : f32 = 9.0;
const KIND_MUSHROOM : f32 = 10.0;
const KIND_CACTUS : f32 = 11.0;
const DECORATION_QUADS : u32 = 8u;
const TREE_LEAF_QUADS : u32 = 6u;
const GROUND_DECORATION_VERTICES : u32 = 48u;
const TREE_CUBOID_VERTICES : u32 = 18u;
const TREE_CUBOID_COUNT : u32 = 7u;
const TREE_VOLUME_VERTICES : u32 = TREE_CUBOID_VERTICES * TREE_CUBOID_COUNT;

@group(0) @binding(0) var<uniform> camera : CameraUniforms;
@group(0) @binding(1) var<storage, read> decorations : array<DecorationInstance>;
@group(0) @binding(2) var rayDepthTex : texture_2d<f32>;
@group(0) @binding(3) var<uniform> params : DecorationParams;

struct VSOut {
    @builtin(position) clipPos : vec4<f32>,
    @location(0) worldPos : vec3<f32>,
    @location(1) uv : vec2<f32>,
    @location(2) color : vec3<f32>,
    @location(3) normal : vec3<f32>,
    @location(4) kind : f32,
    @location(5) segment : f32,
    @location(6) seed : f32,
};

struct TreeCuboid {
    center : vec3<f32>,
    halfSize : vec3<f32>,
    material : f32,
};

fn hash2D(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn isKind(kind: f32, expected: f32) -> bool {
    return abs(kind - expected) < 0.5;
}

fn isTreeKind(kind: f32) -> bool {
    return kind < 1.5 || (kind > 6.5 && kind < 9.5);
}

fn quadCorner(localIndex : u32) -> vec2<f32> {
    var corner = vec2<f32>(-1.0, 0.0);
    if (localIndex == 1u || localIndex == 4u) {
        corner = vec2<f32>(1.0, 0.0);
    }
    if (localIndex == 2u || localIndex == 3u) {
        corner = vec2<f32>(-1.0, 1.0);
    }
    if (localIndex == 5u) {
        corner = vec2<f32>(1.0, 1.0);
    }
    return corner;
}

fn makeVSOut(worldPos : vec3<f32>,
             uv : vec2<f32>,
             color : vec3<f32>,
             normal : vec3<f32>,
             kind : f32,
             segment : f32,
             seed : f32) -> VSOut {
    var out : VSOut;
    out.clipPos = camera.viewProj * vec4<f32>(worldPos, 1.0);
    out.worldPos = worldPos;
    out.uv = uv;
    out.color = color;
    out.normal = normal;
    out.kind = kind;
    out.segment = segment;
    out.seed = seed;
    return out;
}

fn treeCuboid(cuboidIndex : u32,
              base : vec3<f32>,
              radius : f32,
              height : f32,
              kind : f32,
              seed : f32) -> TreeCuboid {
    let isPine = isKind(kind, KIND_PINE);
    let isJungle = isKind(kind, KIND_JUNGLE);
    let isAcacia = isKind(kind, KIND_ACACIA);
    let isCypress = isKind(kind, KIND_CYPRESS);

    var trunkHeight = height * 0.58;
    if (isAcacia) {
        trunkHeight = height * 0.72;
    } else if (isJungle) {
        trunkHeight = height * 0.62;
    } else if (isCypress) {
        trunkHeight = height * 0.84;
    } else if (isPine) {
        trunkHeight = height * 0.54;
    }

    if (cuboidIndex == 0u) {
        let trunkRadius = max(radius * select(0.13, 0.09, isCypress), 0.10);
        return TreeCuboid(
            base + vec3<f32>(0.0, trunkHeight * 0.5, 0.0),
            vec3<f32>(trunkRadius, trunkHeight * 0.5, trunkRadius),
            0.0
        );
    }

    let i = cuboidIndex - 1u;
    let jitter = hash2D(vec2<f32>(f32(cuboidIndex) * 2.17, seed * 19.31)) - 0.5;
    var center = base + vec3<f32>(0.0, height * 0.66, 0.0);
    var halfSize = vec3<f32>(radius * 0.58, height * 0.16, radius * 0.58);

    if (isPine) {
        let tier = f32(i);
        let y = height * (0.32 + tier * 0.095);
        let scale = max(0.18, 0.78 - tier * 0.095);
        center = base + vec3<f32>(0.0, y, 0.0);
        halfSize = vec3<f32>(radius * scale, height * 0.075, radius * scale);
    } else if (isCypress) {
        let tier = f32(i);
        let y = height * (0.22 + tier * 0.105);
        let scale = max(0.22, 0.48 - tier * 0.035);
        center = base + vec3<f32>(0.0, y, 0.0);
        halfSize = vec3<f32>(radius * scale, height * 0.095, radius * scale);
    } else if (isAcacia) {
        let angle = f32(i) * 1.2566371 + seed * 6.2831853;
        let offset = vec3<f32>(cos(angle), 0.0, sin(angle)) * radius * select(0.00, 0.38, i > 0u);
        let wide = select(0.72, 0.46, i > 1u);
        center = base + offset + vec3<f32>(0.0, height * (0.76 + jitter * 0.035), 0.0);
        halfSize = vec3<f32>(radius * wide, height * 0.070, radius * 0.38);
        if ((i % 2u) == 0u) {
            halfSize = vec3<f32>(radius * 0.38, height * 0.070, radius * wide);
        }
    } else {
        let angle = f32(i) * 1.04719755 + seed * 6.2831853;
        let offsetAmount = radius * select(0.0, 0.34, i > 0u);
        let offset = vec3<f32>(cos(angle), 0.0, sin(angle)) * offsetAmount;
        let upper = i >= 4u;
        center = base + offset + vec3<f32>(0.0, height * select(0.62, 0.78, upper) + jitter * radius * 0.06, 0.0);
        halfSize = vec3<f32>(
            radius * select(0.68, 0.46, upper),
            height * select(0.16, 0.12, upper),
            radius * select(0.68, 0.46, upper)
        );
        if (isJungle) {
            halfSize *= vec3<f32>(1.16, 1.10, 1.16);
            center.y -= height * 0.035;
        }
    }

    return TreeCuboid(center, halfSize, 1.0);
}

fn treeVolumeVertex(vertexIndex : u32,
                    base : vec3<f32>,
                    radius : f32,
                    height : f32,
                    color : vec3<f32>,
                    kind : f32,
                    seed : f32) -> VSOut {
    let cuboidIndex = vertexIndex / TREE_CUBOID_VERTICES;
    let faceVertex = vertexIndex % TREE_CUBOID_VERTICES;
    let faceIndex = faceVertex / 6u;
    let localIndex = faceVertex % 6u;
    let corner = quadCorner(localIndex);
    let shape = treeCuboid(cuboidIndex, base, radius, height, kind, seed);

    let toCamera = camera.cameraPos.xyz - shape.center;
    let sx = select(-1.0, 1.0, toCamera.x >= 0.0);
    let sz = select(-1.0, 1.0, toCamera.z >= 0.0);

    var worldPos = shape.center;
    var normal = vec3<f32>(0.0, 1.0, 0.0);
    if (faceIndex == 0u) {
        worldPos = shape.center + vec3<f32>(
            corner.x * shape.halfSize.x,
            shape.halfSize.y,
            (corner.y * 2.0 - 1.0) * shape.halfSize.z
        );
        normal = vec3<f32>(0.0, 1.0, 0.0);
    } else if (faceIndex == 1u) {
        worldPos = shape.center + vec3<f32>(
            sx * shape.halfSize.x,
            (corner.y * 2.0 - 1.0) * shape.halfSize.y,
            corner.x * shape.halfSize.z
        );
        normal = vec3<f32>(sx, 0.0, 0.0);
    } else {
        worldPos = shape.center + vec3<f32>(
            corner.x * shape.halfSize.x,
            (corner.y * 2.0 - 1.0) * shape.halfSize.y,
            sz * shape.halfSize.z
        );
        normal = vec3<f32>(0.0, 0.0, sz);
    }

    if (shape.material > 0.5) {
        let windPhase = dot(base.xz, vec2<f32>(0.013, 0.017)) + seed * 6.2831853 + camera.invProjParams.w * 0.72;
        let windWave = sin(windPhase) * 0.64 + sin(windPhase * 1.73 + 1.1) * 0.36;
        worldPos += vec3<f32>(windWave, 0.0, windWave * 0.37) * radius * 0.030;
    }

    var treeColor = color;
    if (shape.material < 0.5) {
        treeColor = vec3<f32>(0.14, 0.078, 0.036) *
                    (0.84 + 0.22 * hash2D(floor(base.xz * 0.13 + vec2<f32>(seed * 17.0, 5.0))));
        if (isKind(kind, KIND_ACACIA)) {
            treeColor = vec3<f32>(0.22, 0.13, 0.055);
        } else if (isKind(kind, KIND_CYPRESS)) {
            treeColor = vec3<f32>(0.12, 0.065, 0.040);
        }
    } else {
        let leafShade = 0.82 + 0.22 * hash2D(floor(shape.center.xz * 0.21 + vec2<f32>(f32(cuboidIndex) * 11.0, seed * 13.0)));
        treeColor *= leafShade;
        if (isKind(kind, KIND_PINE)) {
            treeColor *= vec3<f32>(0.72, 0.92, 0.78);
        } else if (isKind(kind, KIND_JUNGLE)) {
            treeColor *= vec3<f32>(0.62, 1.02, 0.58);
        } else if (isKind(kind, KIND_ACACIA)) {
            treeColor *= vec3<f32>(1.12, 0.98, 0.56);
        } else if (isKind(kind, KIND_CYPRESS)) {
            treeColor *= vec3<f32>(0.66, 0.94, 0.72);
        }
    }

    return makeVSOut(worldPos, corner, treeColor, normal, kind, 100.0 + f32(cuboidIndex), seed);
}

@vertex
fn vs(@builtin(vertex_index) vertexIndex : u32,
      @builtin(instance_index) instanceIndex : u32) -> VSOut {
    let decoration = decorations[instanceIndex];
    let base = decoration.baseRadius.xyz;
    let radius = decoration.baseRadius.w;
    let height = decoration.colorHeight.w;
    let kind = decoration.variant.x;
    let seed = decoration.variant.y;
    let isTree = isTreeKind(kind);

    if (isTree) {
        return treeVolumeVertex(vertexIndex, base, radius, height, decoration.colorHeight.rgb, kind, seed);
    }

    if (vertexIndex >= GROUND_DECORATION_VERTICES) {
        return makeVSOut(base, vec2<f32>(0.0, 0.0), decoration.colorHeight.rgb,
                         vec3<f32>(0.0, 1.0, 0.0), kind, -1.0, seed);
    }

    let quadIndex = vertexIndex / 6u;
    let localIndex = vertexIndex % 6u;
    let corner = quadCorner(localIndex);
    let isPine = isKind(kind, KIND_PINE);
    let isJungle = isKind(kind, KIND_JUNGLE);
    let isAcacia = isKind(kind, KIND_ACACIA);
    let isCypress = isKind(kind, KIND_CYPRESS);
    let isGrass = isKind(kind, KIND_GRASS);
    let isRock = isKind(kind, KIND_ROCK);
    let isReed = isKind(kind, KIND_REED);
    let isFlower = isKind(kind, KIND_FLOWER);
    let isDryShrub = isKind(kind, KIND_DRY_SHRUB);
    let isMushroom = isKind(kind, KIND_MUSHROOM);
    let isCactus = isKind(kind, KIND_CACTUS);
    let treeTrunkQuad = isTree && quadIndex >= TREE_LEAF_QUADS;
    let leafQuadIndex = min(quadIndex, TREE_LEAF_QUADS - 1u);

    var angle = f32(quadIndex) * 0.78539816 + seed * 0.91;
    if (isTree) {
        angle = f32(leafQuadIndex) * 1.04719755 + seed * 0.73;
    }
    if (treeTrunkQuad) {
        angle = f32(quadIndex - TREE_LEAF_QUADS) * 1.5707963 + seed * 0.29;
    }
    let axis = normalize(vec3<f32>(cos(angle), 0.0, sin(angle)));

    var quadRadius = radius;
    var quadBottom = height * 0.12;
    var quadHeight = height * 0.88;
    if (!isTree) {
        let segmentScale = f32(quadIndex) * 0.07;
        quadRadius = radius * (0.58 + segmentScale + seed * 0.14);
        quadBottom = 0.0;
        quadHeight = height * (0.86 + seed * 0.14);
        if (isRock) {
            quadRadius = radius * (0.74 + 0.10 * f32(quadIndex % 2u));
            quadHeight = height * (0.56 + 0.09 * f32(quadIndex));
        } else if (isReed) {
            quadRadius = radius * (0.34 + 0.07 * f32(quadIndex));
            quadHeight = height * (0.92 + seed * 0.10);
        } else if (isFlower) {
            quadRadius = radius * (0.46 + 0.10 * f32(quadIndex % 2u));
            quadHeight = height * (0.78 + seed * 0.18);
        } else if (isDryShrub) {
            quadRadius = radius * (0.62 + 0.10 * f32(quadIndex));
            quadHeight = height * (0.70 + seed * 0.18);
        } else if (isMushroom) {
            quadRadius = radius * (0.40 + 0.11 * f32(quadIndex % 3u));
            quadHeight = height * (0.86 + seed * 0.16);
        } else if (isCactus) {
            quadRadius = radius * (0.52 + 0.06 * f32(quadIndex % 2u));
            quadHeight = height;
        }
    } else if (treeTrunkQuad) {
        quadRadius = max(radius * 0.16, 0.11);
        quadBottom = 0.0;
        quadHeight = select(height * 0.56, height * 0.76, isAcacia);
        quadHeight = select(quadHeight, height * 0.64, isJungle);
        quadHeight = select(quadHeight, height * 0.82, isCypress);
    } else if (isPine) {
        quadRadius = radius * (0.88 + seed * 0.12);
        quadBottom = height * 0.08;
        quadHeight = height * 0.92;
    } else if (isJungle) {
        quadRadius = radius * (1.02 + seed * 0.16);
        quadBottom = height * 0.12;
        quadHeight = height * 0.86;
    } else if (isAcacia) {
        quadRadius = radius * (1.05 + seed * 0.18);
        quadBottom = height * 0.46;
        quadHeight = height * 0.42;
    } else if (isCypress) {
        quadRadius = radius * (0.72 + seed * 0.08);
        quadBottom = height * 0.04;
        quadHeight = height * 0.94;
    } else if (leafQuadIndex == 1u || leafQuadIndex == 4u) {
        quadRadius = radius * 0.86;
        quadBottom = height * 0.24;
        quadHeight = height * 0.70;
    } else if (leafQuadIndex == 2u || leafQuadIndex == 5u) {
        quadRadius = radius * 0.72;
        quadBottom = height * 0.34;
        quadHeight = height * 0.58;
    }

    var worldPos = base + axis * (corner.x * quadRadius) + vec3<f32>(0.0, quadBottom + corner.y * quadHeight, 0.0);
    if (isTree && !treeTrunkQuad) {
        let clusterPhase = f32(leafQuadIndex) * 2.3999632 + seed * 6.2831853;
        let clusterOffset = vec3<f32>(cos(clusterPhase), 0.0, sin(clusterPhase)) *
                            radius * (0.10 + 0.08 * hash2D(vec2<f32>(f32(leafQuadIndex), seed)));
        worldPos += clusterOffset * (0.35 + 0.65 * smoothstep(0.12, 0.92, corner.y));
        worldPos.y += radius * 0.10 *
                      (hash2D(vec2<f32>(f32(leafQuadIndex) * 3.1, seed * 11.7)) - 0.5) *
                      smoothstep(0.10, 0.85, corner.y);
    }
    let windPhase = dot(base.xz, vec2<f32>(0.013, 0.017)) + seed * 6.2831853 + camera.invProjParams.w * 0.72;
    let windWave = sin(windPhase) * 0.64 + sin(windPhase * 1.73 + 1.1) * 0.36;
    let windBend = smoothstep(0.10, 1.0, corner.y);
    var windScale = 0.0;
    if (!treeTrunkQuad) {
        if (isTree) {
            windScale = radius * 0.055;
        } else if (isGrass || isFlower || isReed) {
            windScale = radius * 0.16;
        } else if (isDryShrub) {
            windScale = radius * 0.045;
        }
    }
    worldPos += vec3<f32>(windWave, 0.0, windWave * 0.37) * windScale * windBend;

    var normal = normalize(vec3<f32>(axis.z, select(0.28, 0.08, treeTrunkQuad), -axis.x));
    if (!isTree) {
        normal = normalize(vec3<f32>(axis.z, select(0.42, 0.16, isRock), -axis.x));
    }

    return makeVSOut(worldPos, corner, decoration.colorHeight.rgb, normal, kind, f32(quadIndex), seed);
}

@fragment
fn fs(input : VSOut) -> @location(0) vec4<f32> {
    let dist = length(input.worldPos - camera.cameraPos.xyz);

    if (params.useRayDepth != 0u) {
        let dims = textureDimensions(rayDepthTex, 0);
        let maxCoord = vec2<i32>(i32(dims.x) - 1, i32(dims.y) - 1);
        let pixel = clamp(vec2<i32>(floor(input.clipPos.xy)), vec2<i32>(0, 0), maxCoord);
        let terrainDepth = textureLoad(rayDepthTex, pixel, 0).x;
        if (terrainDepth > 0.0 && dist > terrainDepth + params.rayDepthBias) {
            discard;
        }
    }

    let centered = abs(input.uv.x);
    let y = input.uv.y;
    let isTree = isTreeKind(input.kind);
    let isPine = isKind(input.kind, KIND_PINE);
    let isJungle = isKind(input.kind, KIND_JUNGLE);
    let isAcacia = isKind(input.kind, KIND_ACACIA);
    let isCypress = isKind(input.kind, KIND_CYPRESS);
    let isGrass = isKind(input.kind, KIND_GRASS);
    let isFlower = isKind(input.kind, KIND_FLOWER);
    let isReed = isKind(input.kind, KIND_REED);
    let isRock = isKind(input.kind, KIND_ROCK);
    let isDryShrub = isKind(input.kind, KIND_DRY_SHRUB);
    let isMushroom = isKind(input.kind, KIND_MUSHROOM);
    let isCactus = isKind(input.kind, KIND_CACTUS);
    let isTrunk = isTree && input.segment >= 5.5;
    let treeVolume = isTree && input.segment >= 99.5;
    let treeVolumeTrunk = treeVolume && input.segment < 100.5;
    let treeVolumeLeaves = treeVolume && !treeVolumeTrunk;
    let leafNoise = hash2D(floor(input.worldPos.xz * 1.7 + vec2<f32>(y * 37.0, y * 19.0)));

    var coverage = 0.0;
    var color = input.color * (0.74 + 0.30 * leafNoise);
    if (isTree) {
        if (input.segment >= 99.5) {
            coverage = 1.0;
            let lobeNoise = hash2D(floor(input.worldPos.xz * 0.58 +
                                         vec2<f32>(input.segment * 7.0, input.seed * 31.0)));
            color = input.color * (0.94 + 0.16 * leafNoise);
            if (treeVolumeLeaves) {
                color = input.color * (1.02 + 0.18 * leafNoise);
                color = mix(color, color * vec3<f32>(0.82, 1.16, 0.72), 0.10 + 0.12 * lobeNoise);
                color = mix(color, color * vec3<f32>(1.12, 0.94, 0.68), 0.07 * step(0.78, lobeNoise));
                color *= 1.0 + 0.10 * max(input.normal.y, 0.0);
            }
        } else {
        var leafAlpha = 0.0;
        if (isPine) {
            let coneWidth = mix(0.78, 0.16, y);
            let cone = 1.0 - smoothstep(coneWidth, coneWidth + 0.12, centered);
            let tiers = 0.74 + 0.26 * smoothstep(0.18, 0.50, abs(fract(y * 4.25 + input.seed * 0.37) - 0.5));
            leafAlpha = cone * tiers * smoothstep(0.04, 0.16, y) * (1.0 - smoothstep(0.98, 1.06, y));
        } else if (isJungle) {
            let crown = 1.0 - smoothstep(0.48, 1.10, centered + abs(y - 0.56) * 0.56);
            let canopySkirt = 1.0 - smoothstep(0.62, 1.04, centered + max(0.30 - y, 0.0) * 1.30);
            let topTuft = 1.0 - smoothstep(0.32, 0.78, centered + max(y - 0.84, 0.0) * 1.20);
            let blockyLobes = 0.76 + 0.24 * step(0.22, hash2D(floor(input.worldPos.xz * 1.16 + vec2<f32>(input.seed * 41.0, y * 12.0))));
            leafAlpha = max(max(crown, canopySkirt), topTuft) *
                        smoothstep(0.06, 0.20, y) *
                        (1.0 - smoothstep(0.98, 1.06, y)) *
                        blockyLobes;
        } else if (isAcacia) {
            let umbrella = 1.0 - smoothstep(0.58, 1.08, centered + abs(y - 0.70) * 1.72);
            let shelf = smoothstep(0.34, 0.54, y) * (1.0 - smoothstep(0.92, 1.02, y));
            let raggedEdge = 0.78 + 0.22 * step(0.34, hash2D(floor(input.worldPos.xz * 1.08 + vec2<f32>(input.seed * 23.0, y * 15.0))));
            leafAlpha = umbrella * shelf * raggedEdge;
        } else if (isCypress) {
            let columnWidth = mix(0.46, 0.18, y);
            let column = 1.0 - smoothstep(columnWidth, columnWidth + 0.13, centered);
            let tiers = 0.82 + 0.18 * smoothstep(0.18, 0.48, abs(fract(y * 5.3 + input.seed * 0.21) - 0.5));
            leafAlpha = column * tiers * smoothstep(0.02, 0.12, y) * (1.0 - smoothstep(0.96, 1.04, y));
        } else {
            let leafOval = 1.0 - smoothstep(0.52, 1.02, centered + abs(y - 0.60) * 0.72);
            let leafCrown = 1.0 - smoothstep(0.42, 0.92, centered + max(y - 0.78, 0.0) * 1.35);
            let leafLower = smoothstep(0.10, 0.24, y) * (1.0 - smoothstep(0.95, 1.03, y));
            let blockyLobes = 0.82 + 0.18 * step(0.28, hash2D(floor(input.worldPos.xz * 0.92 + vec2<f32>(input.seed * 31.0, y * 9.0))));
            leafAlpha = max(leafOval, leafCrown) * leafLower * blockyLobes;
        }
        let raggedEdge = hash2D(floor(input.worldPos.xz * 2.9 +
                                      vec2<f32>(input.segment * 17.0, y * 31.0)));
        let innerHole = hash2D(floor(input.worldPos.xz * 4.7 +
                                     vec2<f32>(input.segment * 29.0, y * 43.0)));
        let edgeZone = smoothstep(0.50, 0.96, centered + abs(y - 0.58) * 0.38);
        let edgeCut = mix(0.78, 1.0, step(edgeZone * 0.52, raggedEdge));
        let holeCut = mix(0.62, 1.0, step(0.16, innerHole));
        leafAlpha *= edgeCut * holeCut * (0.86 + 0.14 * smoothstep(0.12, 0.88, y));
        let trunkAlpha = (1.0 - smoothstep(0.12, 0.34, centered)) *
                         (1.0 - smoothstep(0.88, 1.02, y));
        coverage = select(leafAlpha, trunkAlpha, isTrunk);
        if (isPine && !isTrunk) {
            color *= vec3<f32>(0.74, 0.94, 0.78);
        } else if (isJungle && !isTrunk) {
            color *= vec3<f32>(0.64, 1.04, 0.58);
        } else if (isAcacia && !isTrunk) {
            color *= vec3<f32>(1.12, 0.98, 0.56);
        } else if (isCypress && !isTrunk) {
            color *= vec3<f32>(0.66, 0.94, 0.72);
        } else if (!isTrunk) {
            let autumnTint = smoothstep(0.76, 0.98, hash2D(floor(input.worldPos.xz * 0.045 + vec2<f32>(input.seed * 13.0, 7.0))));
            let youngLeafTint = smoothstep(0.58, 0.86, hash2D(floor(input.worldPos.xz * 0.073 + vec2<f32>(-5.0, input.seed * 19.0))));
            color = mix(color, color * vec3<f32>(1.28, 0.82, 0.38), autumnTint * 0.42);
            color = mix(color, color * vec3<f32>(0.82, 1.16, 0.58), youngLeafTint * 0.18);
        }
        var trunkColor = vec3<f32>(0.14, 0.078, 0.036) * (0.82 + 0.26 * leafNoise);
        if (isAcacia) {
            trunkColor = vec3<f32>(0.22, 0.13, 0.055) * (0.84 + 0.22 * leafNoise);
        } else if (isCypress) {
            trunkColor = vec3<f32>(0.12, 0.065, 0.040) * (0.82 + 0.20 * leafNoise);
        }
        color = select(color, trunkColor, isTrunk);
        }
    } else if (isGrass) {
        let bladeWidth = mix(0.26, 0.055, y) + 0.035 * hash2D(input.worldPos.xz * 2.3 + vec2<f32>(input.seed));
        let bladeAlpha = (1.0 - smoothstep(bladeWidth, bladeWidth + 0.13, centered)) *
                         smoothstep(0.02, 0.14, y) *
                         (1.0 - smoothstep(0.88, 1.03, y));
        let sideNotch = 0.84 + 0.16 * step(0.42, hash2D(vec2<f32>(input.segment, input.seed * 17.0)));
        coverage = bladeAlpha * sideNotch;
        color = input.color * vec3<f32>(0.82, 1.08, 0.80) * (0.78 + 0.28 * leafNoise);
    } else if (isFlower) {
        let stemWidth = mix(0.16, 0.045, y);
        let stemAlpha = (1.0 - smoothstep(stemWidth, stemWidth + 0.08, centered)) *
                        smoothstep(0.02, 0.16, y) *
                        (1.0 - smoothstep(0.82, 0.98, y));
        let petalCore = length(vec2<f32>(input.uv.x * 1.25, y - 0.74));
        let petalAlpha = (1.0 - smoothstep(0.16, 0.36, petalCore)) *
                         smoothstep(0.52, 0.66, y) *
                         (1.0 - smoothstep(0.90, 1.02, y));
        coverage = max(stemAlpha, petalAlpha);
        var petalColor = vec3<f32>(0.96, 0.76, 0.22);
        let palette = fract(input.seed * 4.0);
        if (palette > 0.66) {
            petalColor = vec3<f32>(0.92, 0.92, 0.96);
        } else if (palette > 0.33) {
            petalColor = vec3<f32>(0.84, 0.32, 0.62);
        }
        color = mix(input.color * vec3<f32>(0.82, 1.05, 0.78), petalColor, smoothstep(0.05, 0.55, petalAlpha));
    } else if (isReed) {
        let stalkWidth = mix(0.18, 0.045, y);
        let stalkAlpha = (1.0 - smoothstep(stalkWidth, stalkWidth + 0.075, centered)) *
                         smoothstep(0.01, 0.10, y) *
                         (1.0 - smoothstep(0.96, 1.04, y));
        let seedHead = (1.0 - smoothstep(0.10, 0.24, centered)) *
                       smoothstep(0.66, 0.80, y) *
                       (1.0 - smoothstep(0.96, 1.04, y));
        coverage = max(stalkAlpha, seedHead * 0.92);
        let headColor = vec3<f32>(0.46, 0.34, 0.14) * (0.84 + 0.24 * leafNoise);
        color = mix(input.color * vec3<f32>(0.92, 1.00, 0.70), headColor, smoothstep(0.10, 0.65, seedHead));
    } else if (isRock) {
        let profile = 1.0 - smoothstep(mix(0.82, 0.22, y), mix(0.96, 0.34, y), centered + max(y - 0.80, 0.0) * 0.70);
        let base = smoothstep(0.00, 0.08, y) * (1.0 - smoothstep(0.94, 1.04, y));
        coverage = profile * base;
        let facet = 0.70 + 0.38 * hash2D(floor(input.worldPos.xz * 1.9 + vec2<f32>(input.segment * 7.0, y * 11.0)));
        color = input.color * facet;
    } else if (isDryShrub) {
        let branchWidth = mix(0.28, 0.060, y);
        let branchAlpha = (1.0 - smoothstep(branchWidth, branchWidth + 0.12, centered)) *
                          smoothstep(0.02, 0.14, y) *
                          (1.0 - smoothstep(0.82, 1.02, y));
        let twig = step(0.46, hash2D(floor(input.worldPos.xz * 2.7 + vec2<f32>(input.segment * 13.0, y * 17.0))));
        coverage = branchAlpha * (0.78 + 0.22 * twig);
        color = input.color * vec3<f32>(1.08, 0.92, 0.62) * (0.78 + 0.26 * leafNoise);
    } else if (isMushroom) {
        let stemWidth = mix(0.13, 0.055, y);
        let stemAlpha = (1.0 - smoothstep(stemWidth, stemWidth + 0.055, centered)) *
                        smoothstep(0.02, 0.12, y) *
                        (1.0 - smoothstep(0.56, 0.72, y));
        let capCenter = vec2<f32>(input.uv.x * 1.30, (y - 0.62) * 2.20);
        let capAlpha = (1.0 - smoothstep(0.34, 0.62, length(capCenter))) *
                       smoothstep(0.42, 0.54, y) *
                       (1.0 - smoothstep(0.82, 0.96, y));
        let spotMask = step(0.76, hash2D(floor(input.worldPos.xz * 5.4 + vec2<f32>(input.segment * 17.0, y * 23.0))));
        let palette = fract(input.seed * 5.0);
        var capColor = vec3<f32>(0.78, 0.20, 0.11);
        if (palette > 0.66) {
            capColor = vec3<f32>(0.72, 0.58, 0.34);
        } else if (palette > 0.33) {
            capColor = vec3<f32>(0.42, 0.24, 0.14);
        }
        let stemColor = vec3<f32>(0.78, 0.70, 0.56) * (0.84 + 0.18 * leafNoise);
        let dottedCap = mix(capColor, vec3<f32>(0.94, 0.88, 0.70), spotMask * 0.38);
        coverage = max(stemAlpha, capAlpha);
        color = mix(stemColor, dottedCap, smoothstep(0.08, 0.52, capAlpha));
    } else if (isCactus) {
        let column = (1.0 - smoothstep(0.26, 0.36, centered)) *
                     smoothstep(0.01, 0.08, y) *
                     (1.0 - smoothstep(0.96, 1.04, y));
        let armY = 0.44 + input.seed * 0.18;
        let armBand = (1.0 - smoothstep(0.10, 0.20, abs(y - armY))) *
                      smoothstep(0.26, 0.42, centered) *
                      (1.0 - smoothstep(0.80, 0.96, centered));
        let armCap = (1.0 - smoothstep(0.08, 0.18, abs(y - (armY + 0.14)))) *
                     smoothstep(0.48, 0.62, centered) *
                     (1.0 - smoothstep(0.84, 0.98, centered));
        let rib = 0.78 + 0.22 * smoothstep(0.04, 0.18, abs(fract(centered * 4.5 + input.seed) - 0.5));
        let thorn = step(0.965, hash2D(floor(input.worldPos.xz * 8.0 + vec2<f32>(input.segment * 11.0, y * 31.0))));
        coverage = max(column, max(armBand, armCap));
        color = input.color * vec3<f32>(0.78, 1.08, 0.84) * rib +
                vec3<f32>(0.20, 0.22, 0.14) * thorn * coverage * 0.12;
    }

    if (coverage < 0.09) {
        discard;
    }

    let lightDir = normalize(camera.lightDirWS.xyz);
    let ambient = max(camera.lightDirVS.w, 0.08);
    let normalWS = normalize(input.normal);
    let diffuse = max(dot(normalWS, lightDir), 0.0);
    let wrappedDiffuse = mix(0.30 + 0.70 * diffuse, 0.48 + 0.52 * diffuse, select(0.0, 1.0, treeVolume));
    var lighting = ambient + wrappedDiffuse * select(0.62, 0.78, treeVolume);
    if (treeVolumeLeaves) {
        lighting += 0.12 + 0.10 * max(normalWS.y, 0.0);
        lighting += 0.08 * max(dot(-normalWS, lightDir), 0.0);
    } else if (treeVolumeTrunk) {
        lighting *= 0.92;
    }
    color *= lighting;

    let fade = 1.0 - smoothstep(params.fadeStart, params.fadeEnd, dist);
    if (fade < 0.05) {
        discard;
    }
    color *= 0.72 + 0.28 * fade;

    let fogDensity = max(camera.metrics.w, 0.0);
    let fogColor = vec3<f32>(0.6, 0.68, 0.76);
    let fogFactor = clamp(1.0 - exp(-fogDensity * dist), 0.0, 0.72);
    let finalColor = mix(color, fogColor, fogFactor);

    return vec4<f32>(finalColor, 1.0);
}

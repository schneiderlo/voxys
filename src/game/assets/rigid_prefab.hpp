#pragma once

#include "game/construction/construction_types.hpp"
#include "moto/vmesh.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxy::game::assets {

// Defaults are also hard runtime admission ceilings: callers may only lower
// them. Not a driver/staging working-set claim. The authored part policy passes
// tighter limits for each LOD/complete set. Rigid normalScale is limited to [0,16].
struct RigidPrefabLimits {
    uint32_t maximumNodes = 256;
    uint32_t maximumMeshes = 256;
    uint32_t maximumSubmeshes = 512;
    uint32_t maximumMaterials = 64;
    uint32_t maximumVertices = 65'536;
    uint32_t maximumIndices = 196'608;
    uint32_t maximumTextureDimension = 2'048;
    uint32_t maximumMeshInstances = 256;
    uint32_t maximumExpandedDraws = 512;
    uint64_t maximumDecodedBytes = 32ull * 1024ull * 1024ull;
    uint64_t maximumGpuBytes = 32ull * 1024ull * 1024ull;
    double maximumAbsoluteCoordinate = 100'000.0;
    double maximumTransformCondition = 1.0e6;
};

struct PrefabBounds {
    glm::dvec3 minimum{0.0};
    glm::dvec3 maximum{0.0};
    bool valid = false;
};

struct RigidMeshNode {
    uint32_t nodeIndex = 0;
    uint32_t meshIndex = 0;
    glm::dmat4 nodeToAsset{1.0};
};

struct RigidPrefabCounts {
    uint64_t decodedBytes = 0; // All owned VMESH vectors/string, no allocator overhead.
    uint64_t vertexBytes = 0;
    uint64_t indexBytes = 0; // MeshPath uploads u32, including u16 source indices.
    uint64_t materialBytes = 0; // 64 bytes per GPU material.
    uint64_t textureBaseBytes = 0; // One copy per uploaded material slot.
    uint64_t textureMipBytes = 0; // Complete RGBA8 chains, includes base levels.
    uint64_t gpuBytes = 0; // Geometry + materials + texture mip chains.
    uint32_t textureCount = 0;
    uint32_t meshInstances = 0;
    uint32_t expandedDraws = 0;
};

// CPU-only owned record. Store as immutable alongside its exact admitted VMESH;
// neither this type nor transient GPU array indices are durable content IDs.
struct RigidPrefab {
    construction::CubeRotation renderToCanonical{};
    std::vector<RigidMeshNode> meshNodes{};
    std::vector<PrefabBounds> meshBounds{};
    PrefabBounds canonicalBounds{};
    RigidPrefabCounts counts{};
    RigidPrefabLimits limits{};
};

// Validates rigid drawable VMESH, evaluates the complete node forest in double
// precision, and accounts resources before GPU allocation. The caller already
// binds file/profile/content identities. No sidecar parsing, filesystem or GPU.
// Failure leaves output unchanged. std::bad_alloc follows normal C++ behavior.
[[nodiscard]] bool prepareRigidPrefab(
    const moto::VmeshData& data, construction::CubeRotation renderToCanonical,
    const RigidPrefabLimits& limits, RigidPrefab& output, std::string& error);

struct RigidPrefabDraw {
    uint32_t nodeIndex = 0;
    uint32_t meshIndex = 0;
    glm::mat4 modelMatrix{1.0f};
};

// cameraRelativeRoot already has the camera sector removed before float
// conversion. Composition is root * gridPart * perLOD basis * full node matrix.
// Socket/collision/anchor transforms MUST NOT use the render basis/node matrix.
// Replaces output transactionally; does not append or mutate the prefab.
[[nodiscard]] bool placeRigidPrefab(
    const RigidPrefab& prefab, const glm::dmat4& cameraRelativeRoot,
    construction::GridTransform gridPart, size_t maximumDrawRecords,
    std::vector<RigidPrefabDraw>& output, std::string& error);

} // namespace voxy::game::assets

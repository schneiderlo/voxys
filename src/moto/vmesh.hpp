// ═══════════════════════════════════════════════════════════════════════════════
// vmesh.hpp - VMESH engine mesh format (RIDGEBREAK mesh pipeline)
// ═══════════════════════════════════════════════════════════════════════════════
// The runtime mesh format for Voxys. The offline tool converts glTF 2.0 to
// .vmesh; the runtime loads only .vmesh. The layout is fixed-width and
// offset-based so the same loader works on native and WASM with no pointer
// fixups and no host-endian dependence beyond the little-endian WebGPU target.
//
// File layout (all little-endian):
//   [0 .. 255]   VmeshHeader
//   [vertices]   vertexCount * vertexStride bytes
//   [indices]    indexCount * indexStride bytes
//   [submeshes]  submeshCount * VmeshSubmesh
//   [materials]  materialCount * VmeshMaterial
//   [images]     concatenated RGBA8 image data, referenced by
//                VmeshMaterial::textureOffset/Size (byte offsets into this
//                section); images are 4-byte aligned
//   [nodes]      nodeCount * VmeshNode
//   [skins]      skinCount * VmeshSkin followed by jointCount * VmeshJoint
//   [anims]      animCount * VmeshAnim
//   [channels]   animChannelCount * VmeshAnimChannel
//   [channelData] animation key times and values
//   [stringBlob] NUL-terminated names, referenced by nameOffset fields
//   [fileSize]   end of file
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace voxy::moto {

constexpr char kVmeshMagic[8] = {'V', 'O', 'X', 'Y', 'M', 'E', 'S', 'H'};
constexpr uint32_t kVmeshVersion = 1u;

// Vertex layout flags.
constexpr uint32_t kVmeshHasTangent = 1u << 0u;
constexpr uint32_t kVmeshHasSkin = 1u << 1u;
constexpr uint32_t kVmeshHasAnimations = 1u << 2u;

// Vertex attribute semantics. Every vertex uses the same interleaved layout so
// the renderer builds one pipeline. Vertex stride is fixed.
struct VmeshVertex {
    float position[3];    //  0   local space, metres
    float normal[3];      // 12
    float tangent[4];     // 24   xyz + handedness sign in w
    float texCoord[2];    // 40   source UVs; import profile defines image row orientation
    uint16_t joint[4];    // 48   joint indices (into owning skin)
    float weight[4];      // 56   skin weights, sum ~1
};                        // 72 bytes total

inline constexpr uint32_t kVmeshVertexStride = static_cast<uint32_t>(sizeof(VmeshVertex));

struct VmeshSubmesh {
    uint32_t indexOffset = 0;  // byte offset into index data
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    uint32_t meshIndex = 0;    // owning mesh (part) index
};

// Material texture slots.
enum VmeshTextureSlot : uint8_t {
    VmeshTextureBaseColor = 0,
    VmeshTextureNormal = 1,
    VmeshTextureMetallicRoughness = 2,
    VmeshTextureEmissive = 3,
    VmeshTextureCount = 4,
};

enum VmeshAlphaMode : uint8_t {
    VmeshAlphaOpaque = 0,
    VmeshAlphaMask = 1,
    VmeshAlphaBlend = 2,
};

struct VmeshMaterial {
    float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
    float alphaCutoff = 0.5f;
    uint8_t alphaMode = VmeshAlphaOpaque;
    uint8_t doubleSided = 0;
    uint8_t unlit = 0;
    uint8_t reserved = 0;
    // Texture slots. Offsets/sizes address the image section; width/height are
    // the decoded RGBA8 dimensions; hasTexture gates sampling.
    uint32_t textureOffset[VmeshTextureCount] = {};
    uint32_t textureSize[VmeshTextureCount] = {};
    uint16_t textureWidth[VmeshTextureCount] = {};
    uint16_t textureHeight[VmeshTextureCount] = {};
    uint8_t hasTexture[VmeshTextureCount] = {};
    uint8_t textureIsSrgb[VmeshTextureCount] = {};
    uint16_t reserved2 = 0;
    uint32_t reserved3[3] = {};  // keeps the material at a 128-byte stride
    uint32_t nameOffset = 0;     // into string blob, 0 if unnamed
};

struct VmeshNode {
    float translation[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // (x, y, z, w)
    float scale[3] = {1.0f, 1.0f, 1.0f};
    int32_t parent = -1;
    uint32_t meshIndex = 0xFFFFFFFFu;
    int32_t skinIndex = -1;
    uint32_t nameOffset = 0;
    uint32_t reserved2[2] = {};  // keeps the node at a 64-byte stride
};

struct VmeshJoint {
    uint32_t nodeIndex = 0;
    float inverseBindMatrix[16] = {};  // column-major, inverse of the joint
                                       // world matrix at bind time
};

struct VmeshSkin {
    int32_t skeletonRoot = -1;
    uint32_t jointCount = 0;
    uint64_t jointsOffset = 0;       // byte offset into the joint array
};

enum VmeshAnimPath : uint8_t {
    VmeshAnimPathTranslation = 0,
    VmeshAnimPathRotation = 1,
    VmeshAnimPathScale = 2,
};

enum VmeshAnimInterpolation : uint8_t {
    VmeshAnimInterpolationLinear = 0,
    VmeshAnimInterpolationStep = 1,
};

struct VmeshAnimChannel {
    // Key data is interleaved as [time f32] * keyCount followed by
    // [value] * keyCount where value is 3 f32 (translation/scale) or
    // 4 f32 (rotation, xyz w).
    uint64_t keysOffset = 0;  // byte offset into channel data area
    uint32_t nodeIndex = 0;
    uint8_t path = VmeshAnimPathTranslation;
    uint8_t interpolation = VmeshAnimInterpolationLinear;
    uint8_t reserved[2] = {};
    uint32_t keyCount = 0;
    uint32_t reserved2 = 0;   // keeps the channel at a 24-byte stride
};

struct VmeshAnim {
    float duration = 0.0f;
    uint32_t channelCount = 0;
    uint64_t channelsOffset = 0;  // byte offset into animChannels section
    uint32_t nameOffset = 0;
    uint32_t reserved2 = 0;       // keeps the anim at a 24-byte stride
};

struct VmeshHeader {
    char magic[8] = {'V', 'O', 'X', 'Y', 'M', 'E', 'S', 'H'};
    uint32_t version = kVmeshVersion;
    uint32_t flags = 0;
    uint32_t vertexCount = 0;
    uint32_t vertexStride = kVmeshVertexStride;
    uint32_t indexCount = 0;
    uint32_t indexStride = 2;  // 2 or 4
    uint32_t submeshCount = 0;
    uint32_t materialCount = 0;
    uint32_t nodeCount = 0;
    uint32_t meshCount = 0;
    uint32_t skinCount = 0;
    uint32_t jointCount = 0;
    uint32_t animCount = 0;
    uint32_t animChannelCount = 0;
    uint64_t verticesOffset = 0;
    uint64_t indicesOffset = 0;
    uint64_t submeshesOffset = 0;
    uint64_t materialsOffset = 0;
    uint64_t imagesOffset = 0;
    uint64_t nodesOffset = 0;
    uint64_t skinsOffset = 0;
    uint64_t animsOffset = 0;
    uint64_t animChannelsOffset = 0;
    uint64_t channelDataOffset = 0;
    uint64_t stringBlobOffset = 0;
    uint64_t stringBlobSize = 0;
    uint64_t fileSize = 0;
    uint64_t reservedTail[11] = {};  // keeps the header at 256 bytes
};

static_assert(sizeof(VmeshVertex) == 72u);
static_assert(sizeof(VmeshMaterial) == 128u);
static_assert(sizeof(VmeshNode) == 64u);
static_assert(sizeof(VmeshJoint) == 68u);
static_assert(sizeof(VmeshSkin) == 16u);
static_assert(sizeof(VmeshAnimChannel) == 24u);
static_assert(sizeof(VmeshAnim) == 24u);
static_assert(sizeof(VmeshHeader) == 256u);

/// In-memory form of a .vmesh file.
struct VmeshData {
    VmeshHeader header{};
    std::vector<uint8_t> vertices;
    std::vector<uint8_t> indices;
    std::vector<VmeshSubmesh> submeshes;
    std::vector<VmeshMaterial> materials;
    std::vector<uint8_t> images;
    std::vector<VmeshNode> nodes;
    std::vector<VmeshSkin> skins;
    std::vector<VmeshJoint> joints;
    std::vector<VmeshAnim> anims;
    std::vector<VmeshAnimChannel> animChannels;
    // Each channel's keysOffset is relative to this buffer's start.
    std::vector<uint8_t> channelData;
    std::string stringBlob;

    [[nodiscard]] const char* name(uint32_t offset) const noexcept {
        if (offset == 0u || offset >= stringBlob.size()) return "";
        return stringBlob.c_str() + offset;
    }
};

// Errors are a simple string; the callers log and bail. Values are returned
// from writer/reader so a single failure never leaves partial files live.

/// Serialize a complete VmeshData to a byte buffer. Returns false and a reason
/// if any invariant is violated. The output buffer is cleared on failure.
[[nodiscard]] bool writeVmesh(const VmeshData& data, std::vector<uint8_t>* out,
                              std::string* error);

/// Parse a byte buffer into VmeshData. Performs exhaustive bounds and
/// consistency checks before any allocation. The destination is left untouched
/// on failure.
[[nodiscard]] bool readVmesh(const uint8_t* bytes, size_t byteCount,
                             VmeshData* out, std::string* error);

}  // namespace voxy::moto

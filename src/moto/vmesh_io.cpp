// ═══════════════════════════════════════════════════════════════════════════════
// vmesh_io.cpp - VMESH serialization and parsing
// ═══════════════════════════════════════════════════════════════════════════════

#include "moto/vmesh.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace voxy::moto {

namespace {

bool appendBytes(std::vector<uint8_t>* out, const void* data, size_t count) {
    if (count == 0u) return true;
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + count);
    return true;
}

template <typename T>
void appendAligned(std::vector<uint8_t>* out, const T& value) {
    constexpr size_t alignment = 4u;
    while ((out->size() % alignment) != 0u) {
        out->push_back(0);
    }
    appendBytes(out, &value, sizeof(T));
}

bool alignWriter(std::vector<uint8_t>* out) {
    constexpr size_t alignment = 4u;
    while ((out->size() % alignment) != 0u) {
        out->push_back(0);
    }
    return true;
}

bool fail(std::string* error, const char* reason) {
    if (error != nullptr) *error = reason;
    return false;
}

size_t toSize(uint64_t value) noexcept {
#if SIZE_MAX < UINT64_MAX
    return static_cast<size_t>(value);
#else
    return value;
#endif
}

}  // namespace

bool writeVmesh(const VmeshData& data, std::vector<uint8_t>* out,
                std::string* error) {
    if (out == nullptr) return fail(error, "null output buffer");

    const VmeshHeader& header = data.header;

    // Validate the in-memory form before serializing anything.
    if (header.version != kVmeshVersion) return fail(error, "unsupported version");
    if (header.vertexStride != kVmeshVertexStride) return fail(error, "unexpected vertex stride");
    if (header.indexStride != 2u && header.indexStride != 4u) return fail(error, "invalid index stride");
    if (header.vertexCount != data.vertices.size() / header.vertexStride) return fail(error, "vertex count mismatch");
    if (header.indexCount != data.indices.size() / header.indexStride) return fail(error, "index count mismatch");
    if (header.submeshCount != data.submeshes.size()) return fail(error, "submesh count mismatch");
    if (header.materialCount != data.materials.size()) return fail(error, "material count mismatch");
    if (header.nodeCount != data.nodes.size()) return fail(error, "node count mismatch");
    if (header.skinCount != data.skins.size()) return fail(error, "skin count mismatch");
    if (header.animCount != data.anims.size()) return fail(error, "anim count mismatch");
    if (header.animChannelCount != data.animChannels.size()) return fail(error, "anim channel count mismatch");
    if (data.joints.size() != header.jointCount) return fail(error, "joint count mismatch");
    if (header.vertexCount > 0u && data.vertices.empty()) return fail(error, "missing vertices");

    // Derived size guards. Widths widen implicitly so the checks hold on both
    // 32-bit WASM and 64-bit native without casting warnings.
    const uint64_t submeshBytes = data.submeshes.size() * sizeof(VmeshSubmesh);
    const uint64_t materialBytes = data.materials.size() * sizeof(VmeshMaterial);
    const uint64_t imageBytes = data.images.size();
    const uint64_t nodeBytes = data.nodes.size() * sizeof(VmeshNode);
    const uint64_t jointBytes = data.joints.size() * sizeof(VmeshJoint);
    const uint64_t skinBytes = data.skins.size() * sizeof(VmeshSkin);
    const uint64_t animBytes = data.anims.size() * sizeof(VmeshAnim);
    const uint64_t animChannelBytes = data.animChannels.size() * sizeof(VmeshAnimChannel);
    const uint64_t channelBytes = data.channelData.size();
    const uint64_t stringBytes = data.stringBlob.size();
    const uint64_t limit = std::numeric_limits<uint64_t>::max();

    auto checkedSum = [&](uint64_t a, uint64_t b) -> uint64_t {
        if (a > limit - b) return limit;
        return a + b;
    };

    // Image offsets must address the image section.
    for (const VmeshMaterial& material : data.materials) {
        for (int slot = 0; slot < VmeshTextureCount; ++slot) {
            const uint32_t offset = material.textureOffset[slot];
            const uint32_t size = material.textureSize[slot];
            const bool has = material.hasTexture[slot] != 0u;
            if (!has) {
                if (offset != 0u || size != 0u) return fail(error, "texture data without flag");
                continue;
            }
            if (offset >= data.images.size() || size > data.images.size() - offset) {
                return fail(error, "texture data out of range");
            }
            const uint32_t width = material.textureWidth[slot];
            const uint32_t height = material.textureHeight[slot];
            if (width == 0u || height == 0u) return fail(error, "texture has zero extent");
            const uint64_t expected = static_cast<uint64_t>(width) * height * 4u;
            if (expected != size) return fail(error, "texture size does not match extent");
        }
    }

    // Submesh references.
    for (const VmeshSubmesh& submesh : data.submeshes) {
        if (submesh.materialIndex >= data.materials.size()) return fail(error, "submesh material out of range");
        if (submesh.meshIndex >= header.meshCount) return fail(error, "submesh mesh out of range");
        const uint64_t start = submesh.indexOffset;
        const uint64_t end = checkedSum(start, static_cast<uint64_t>(submesh.indexCount) * header.indexStride);
        if (end > data.indices.size()) return fail(error, "submesh index range out of bounds");
    }

    // Node references.
    for (const VmeshNode& node : data.nodes) {
        if (node.parent >= static_cast<int32_t>(data.nodes.size())) return fail(error, "node parent out of range");
        if (node.meshIndex != 0xFFFFFFFFu && node.meshIndex >= header.meshCount) return fail(error, "node mesh out of range");
        if (node.skinIndex >= static_cast<int32_t>(data.skins.size())) return fail(error, "node skin out of range");
    }

    // Skin references.
    for (const VmeshSkin& skin : data.skins) {
        if (skin.jointsOffset >= jointBytes) return fail(error, "skin joints out of range");
        const uint64_t end = checkedSum(skin.jointsOffset, static_cast<uint64_t>(skin.jointCount) * sizeof(VmeshJoint));
        if (end > jointBytes) return fail(error, "skin joint range out of bounds");
    }

    // Animation references.
    for (const VmeshAnim& anim : data.anims) {
        if ((anim.channelsOffset % sizeof(VmeshAnimChannel)) != 0u) {
            return fail(error, "unaligned anim channel offset");
        }
        const uint64_t end = checkedSum(
            anim.channelsOffset,
            static_cast<uint64_t>(anim.channelCount) * sizeof(VmeshAnimChannel));
        if (end > animChannelBytes) {
            return fail(error, "anim channel range out of bounds");
        }
    }
    for (const VmeshAnimChannel& channel : data.animChannels) {
        if (channel.nodeIndex >= data.nodes.size()) return fail(error, "anim node out of range");
        if (channel.path > VmeshAnimPathScale) return fail(error, "invalid anim path");
        if (channel.interpolation > VmeshAnimInterpolationStep) return fail(error, "invalid interpolation");
        const uint64_t valueComponents = channel.path == VmeshAnimPathRotation ? 4u : 3u;
        const uint64_t keyBytes = static_cast<uint64_t>(channel.keyCount) *
                                  (1u + valueComponents) * sizeof(float);
        if (checkedSum(channel.keysOffset, keyBytes) > channelBytes) {
            return fail(error, "anim keys out of range");
        }
    }
    for (const VmeshSkin& skin : data.skins) {
        for (uint32_t j = 0; j < skin.jointCount; ++j) {
            const VmeshJoint& joint =
                data.joints.data()[skin.jointsOffset / sizeof(VmeshJoint) + j];
            if (joint.nodeIndex >= data.nodes.size()) return fail(error, "joint node out of range");
        }
    }

    out->clear();
    const uint64_t reserveBytes = sizeof(VmeshHeader) + data.vertices.size() +
                                  data.indices.size() + submeshBytes +
                                  materialBytes + imageBytes + nodeBytes +
                                  skinBytes + jointBytes + animBytes +
                                  animChannelBytes + channelBytes + stringBytes;
    if (reserveBytes > SIZE_MAX) return fail(error, "vmesh exceeds address space");
    out->reserve(toSize(reserveBytes));

    // Header placeholder; fill after offsets are known.
    appendBytes(out, &header, sizeof(VmeshHeader));

    auto sectionStart = [&](uint64_t& offsetOut) {
        alignWriter(out);
        offsetOut = out->size();
    };

    VmeshHeader outHeader = header;
    sectionStart(outHeader.verticesOffset);
    appendBytes(out, data.vertices.data(), data.vertices.size());
    sectionStart(outHeader.indicesOffset);
    appendBytes(out, data.indices.data(), data.indices.size());
    sectionStart(outHeader.submeshesOffset);
    for (const VmeshSubmesh& s : data.submeshes) appendAligned(out, s);
    sectionStart(outHeader.materialsOffset);
    for (const VmeshMaterial& m : data.materials) appendAligned(out, m);
    sectionStart(outHeader.imagesOffset);
    appendBytes(out, data.images.data(), data.images.size());
    sectionStart(outHeader.nodesOffset);
    for (const VmeshNode& n : data.nodes) appendAligned(out, n);
    sectionStart(outHeader.skinsOffset);
    for (const VmeshSkin& s : data.skins) appendAligned(out, s);
    appendBytes(out, data.joints.data(), data.joints.size() * sizeof(VmeshJoint));
    sectionStart(outHeader.animsOffset);
    for (const VmeshAnim& a : data.anims) appendAligned(out, a);
    sectionStart(outHeader.animChannelsOffset);
    for (const VmeshAnimChannel& channel : data.animChannels) appendAligned(out, channel);
    sectionStart(outHeader.channelDataOffset);
    appendBytes(out, data.channelData.data(), data.channelData.size());
    sectionStart(outHeader.stringBlobOffset);
    appendBytes(out, data.stringBlob.data(), data.stringBlob.size());

    outHeader.stringBlobSize = data.stringBlob.size();
    outHeader.fileSize = out->size();
    std::memcpy(out->data(), &outHeader, sizeof(VmeshHeader));
    return true;
}

bool readVmesh(const uint8_t* bytes, size_t byteCount, VmeshData* out,
               std::string* error) {
    if (bytes == nullptr || out == nullptr) return fail(error, "null input");
    if (byteCount < sizeof(VmeshHeader)) return fail(error, "file too small");

    VmeshHeader header;
    std::memcpy(&header, bytes, sizeof(VmeshHeader));
    if (std::memcmp(header.magic, kVmeshMagic, sizeof(kVmeshMagic)) != 0) {
        return fail(error, "bad magic");
    }
    if (header.version != kVmeshVersion) return fail(error, "unsupported version");
    if (header.fileSize != byteCount) return fail(error, "size mismatch");
    if (header.vertexStride != kVmeshVertexStride) return fail(error, "unexpected vertex stride");
    if (header.indexStride != 2u && header.indexStride != 4u) return fail(error, "invalid index stride");

    const uint64_t fileSize = byteCount;
    const uint64_t headerSize = sizeof(VmeshHeader);
    if (header.verticesOffset < headerSize) return fail(error, "bad vertices offset");

    // Sections must be ordered and in-bounds.
    const uint64_t order[] = {
        header.verticesOffset,
        header.indicesOffset,
        header.submeshesOffset,
        header.materialsOffset,
        header.imagesOffset,
        header.nodesOffset,
        header.skinsOffset,
        header.animsOffset,
        header.animChannelsOffset,
        header.channelDataOffset,
        header.stringBlobOffset,
        fileSize,
    };
    uint64_t previous = headerSize;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if (order[i] < previous || order[i] > fileSize) {
            return fail(error, "section offsets out of order");
        }
        previous = order[i];
    }

    const uint64_t vertexBytes = static_cast<uint64_t>(header.vertexCount) * header.vertexStride;
    const uint64_t indexBytes = static_cast<uint64_t>(header.indexCount) * header.indexStride;
    const uint64_t submeshBytes = static_cast<uint64_t>(header.submeshCount) * sizeof(VmeshSubmesh);
    const uint64_t materialBytes = static_cast<uint64_t>(header.materialCount) * sizeof(VmeshMaterial);
    const uint64_t nodeBytes = static_cast<uint64_t>(header.nodeCount) * sizeof(VmeshNode);
    const uint64_t jointBytes = static_cast<uint64_t>(header.jointCount) * sizeof(VmeshJoint);
    const uint64_t skinBytes = static_cast<uint64_t>(header.skinCount) * sizeof(VmeshSkin);
    const uint64_t animBytes = static_cast<uint64_t>(header.animCount) * sizeof(VmeshAnim);
    const uint64_t animChannelBytes = static_cast<uint64_t>(header.animChannelCount) * sizeof(VmeshAnimChannel);

    if (header.verticesOffset + vertexBytes > header.indicesOffset) return fail(error, "vertices overflow");
    if (header.indicesOffset + indexBytes > header.submeshesOffset) return fail(error, "indices overflow");
    if (header.submeshesOffset + submeshBytes > header.materialsOffset) return fail(error, "submeshes overflow");
    if (header.materialsOffset + materialBytes > header.imagesOffset) return fail(error, "materials overflow");
    if (header.imagesOffset > header.nodesOffset) return fail(error, "images overflow");
    if (header.nodesOffset + nodeBytes > header.skinsOffset) return fail(error, "nodes overflow");
    if (header.skinsOffset + skinBytes + jointBytes > header.animsOffset) return fail(error, "skins overflow");
    if (header.animsOffset + animBytes > header.animChannelsOffset) return fail(error, "anims overflow");
    if (header.animChannelsOffset + animChannelBytes > header.channelDataOffset) return fail(error, "anim channels overflow");
    if (header.channelDataOffset > header.stringBlobOffset) return fail(error, "channel data overflow");
    if (header.stringBlobSize > fileSize - header.stringBlobOffset) return fail(error, "string blob overflow");

    VmeshData result;
    result.header = header;

    // Allocate after every size is proven against the file bounds. Each vector
    // is bounded by the file size, so a hostile header cannot request an
    // address-space-sized allocation.
    result.vertices.assign(bytes + header.verticesOffset,
                           bytes + header.verticesOffset + vertexBytes);
    result.indices.assign(bytes + header.indicesOffset,
                          bytes + header.indicesOffset + indexBytes);
    result.submeshes.resize(header.submeshCount);
    result.materials.resize(header.materialCount);
    result.nodes.resize(header.nodeCount);
    result.skins.resize(header.skinCount);
    result.joints.resize(header.jointCount);
    result.anims.resize(header.animCount);
    result.animChannels.resize(header.animChannelCount);

    // Optional sections can be empty. Their vectors may have null data(),
    // which is not a valid memcpy destination even for a zero-byte copy.
    const auto copySection = [bytes](void* destination, uint64_t offset,
                                    uint64_t size) {
        if (size != 0u) std::memcpy(destination, bytes + offset, toSize(size));
    };
    copySection(result.submeshes.data(), header.submeshesOffset, submeshBytes);
    copySection(result.materials.data(), header.materialsOffset, materialBytes);
    copySection(result.nodes.data(), header.nodesOffset, nodeBytes);
    copySection(result.skins.data(), header.skinsOffset, skinBytes);
    copySection(result.joints.data(), header.skinsOffset + skinBytes, jointBytes);
    copySection(result.anims.data(), header.animsOffset, animBytes);
    copySection(result.animChannels.data(), header.animChannelsOffset, animChannelBytes);

    result.images.assign(bytes + header.imagesOffset,
                         bytes + header.nodesOffset);
    result.stringBlob.assign(bytes + header.stringBlobOffset,
                             bytes + header.stringBlobOffset + header.stringBlobSize);

    result.channelData.assign(bytes + header.channelDataOffset,
                              bytes + header.stringBlobOffset);

    // Semantic validation of references.
    for (const VmeshSubmesh& submesh : result.submeshes) {
        if (submesh.materialIndex >= result.materials.size()) return fail(error, "submesh material out of range");
        if (submesh.meshIndex >= header.meshCount) return fail(error, "submesh mesh out of range");
        const uint64_t end = static_cast<uint64_t>(submesh.indexOffset) +
                             static_cast<uint64_t>(submesh.indexCount) * header.indexStride;
        if (end > result.indices.size()) return fail(error, "submesh index range out of bounds");
    }
    for (const VmeshNode& node : result.nodes) {
        if (node.parent >= static_cast<int32_t>(result.nodes.size())) return fail(error, "node parent out of range");
        if (node.meshIndex != 0xFFFFFFFFu && node.meshIndex >= header.meshCount) return fail(error, "node mesh out of range");
        if (node.skinIndex >= static_cast<int32_t>(result.skins.size())) return fail(error, "node skin out of range");
    }
    for (const VmeshSkin& skin : result.skins) {
        if (skin.jointsOffset >= jointBytes) return fail(error, "skin joints out of range");
        const uint64_t end = skin.jointsOffset +
                             static_cast<uint64_t>(skin.jointCount) * sizeof(VmeshJoint);
        if (end > jointBytes) return fail(error, "skin joint range out of bounds");
    }
    for (const VmeshAnim& anim : result.anims) {
        if ((anim.channelsOffset % sizeof(VmeshAnimChannel)) != 0u) {
            return fail(error, "unaligned anim channel offset");
        }
        const uint64_t bytesNeeded =
            static_cast<uint64_t>(anim.channelCount) * sizeof(VmeshAnimChannel);
        if (anim.channelsOffset > animChannelBytes ||
            bytesNeeded > animChannelBytes - anim.channelsOffset) {
            return fail(error, "anim channel range out of bounds");
        }
    }
    for (const VmeshSkin& skin : result.skins) {
        for (uint32_t j = 0; j < skin.jointCount; ++j) {
            const uint64_t jointIndex = skin.jointsOffset / sizeof(VmeshJoint) + j;
            const VmeshJoint& joint = result.joints[toSize(jointIndex)];
            if (joint.nodeIndex >= result.nodes.size()) return fail(error, "joint node out of range");
        }
    }
    for (const VmeshAnimChannel& channel : result.animChannels) {
        if (channel.nodeIndex >= result.nodes.size()) return fail(error, "anim node out of range");
        if (channel.path > VmeshAnimPathScale) return fail(error, "invalid anim path");
        if (channel.interpolation > VmeshAnimInterpolationStep) return fail(error, "invalid interpolation");
        const uint64_t valueComponents = channel.path == VmeshAnimPathRotation ? 4u : 3u;
        const uint64_t keyBytes = static_cast<uint64_t>(channel.keyCount) *
                                  (1u + valueComponents) * sizeof(float);
        if (channel.keysOffset > result.channelData.size() ||
            keyBytes > result.channelData.size() - channel.keysOffset) {
            return fail(error, "anim keys out of range");
        }
    }

    // Validate every index references a live vertex.
    const uint8_t* indexData = result.indices.data();
    for (uint32_t i = 0; i < header.indexCount; ++i) {
        uint32_t vertexIndex = 0;
        if (header.indexStride == 2u) {
            vertexIndex = *reinterpret_cast<const uint16_t*>(indexData + i * 2u);
        } else {
            vertexIndex = *reinterpret_cast<const uint32_t*>(indexData + i * 4u);
        }
        if (vertexIndex >= header.vertexCount) return fail(error, "index out of range");
    }

    // Material texture references.
    for (const VmeshMaterial& material : result.materials) {
        for (int slot = 0; slot < VmeshTextureCount; ++slot) {
            const bool has = material.hasTexture[slot] != 0u;
            const uint32_t offset = material.textureOffset[slot];
            const uint32_t size = material.textureSize[slot];
            if (!has) {
                if (offset != 0u || size != 0u) return fail(error, "texture data without flag");
                continue;
            }
            if (offset >= result.images.size() || size > result.images.size() - offset) {
                return fail(error, "texture data out of range");
            }
            const uint32_t width = material.textureWidth[slot];
            const uint32_t height = material.textureHeight[slot];
            if (width == 0u || height == 0u) return fail(error, "texture has zero extent");
            if (static_cast<uint64_t>(width) * height * 4u != size) {
                return fail(error, "texture size does not match extent");
            }
        }
    }

    *out = std::move(result);
    return true;
}

}  // namespace voxy::moto

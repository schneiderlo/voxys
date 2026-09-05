// ═══════════════════════════════════════════════════════════════════════════════
// test_gltf_vmesh_tool.cpp - glTF 2.0 -> .vmesh converter tests
// ═══════════════════════════════════════════════════════════════════════════════
// Drives the in-memory converter with ASCII glTF documents (data-URI buffers)
// and hostile inputs, then round-trips the result through writeVmesh/readVmesh.
// ═══════════════════════════════════════════════════════════════════════════════

#include "gltf_vmesh_tool.hpp"
#include "moto/vmesh.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

// 2x1 RGBA PNG, 72 bytes, pixels (255,0,0) and (0,255,0).
constexpr const char* kTexturePngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAAD0lEQVR4nGP4z8DA8J8BAAf/"
    "Af8Bf4mnAAAAAElFTkSuQmCC";

std::string base64Encode(const std::vector<uint8_t>& bytes) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2u) / 3u) * 4u);
    for (size_t i = 0; i < bytes.size(); i += 3u) {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = i + 1u < bytes.size() ? bytes[i + 1u] : 0u;
        const uint32_t b2 = i + 2u < bytes.size() ? bytes[i + 2u] : 0u;
        const uint32_t triple = (b0 << 16u) | (b1 << 8u) | b2;
        out.push_back(kTable[(triple >> 18u) & 0x3Fu]);
        out.push_back(kTable[(triple >> 12u) & 0x3Fu]);
        out.push_back(i + 1u < bytes.size() ? kTable[(triple >> 6u) & 0x3Fu] : '=');
        out.push_back(i + 2u < bytes.size() ? kTable[triple & 0x3Fu] : '=');
    }
    return out;
}

struct Options {
    bool withNormals = true;
    bool withUvs = true;
    bool withIndices = true;
    bool withTexture = false;
    bool withSkin = false;
    bool withAnimation = false;
    bool withHierarchy = false;
    bool withMaterials = true;
    bool withUnlit = false;
    bool withDoubleSided = false;
    int mode = -1;              // -1 = omit (defaults to TRIANGLES)
    int positionAccessor = 0;   // override the POSITION attribute target
    int indexAccessor = 3;      // override the indices attribute target
    float firstPositionX = 0.0f;
    uint16_t lastIndex = 2;
};

std::vector<uint8_t> buildBuffer(const Options& opt) {
    std::vector<uint8_t> out;
    const auto pushF = [&out](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu));
        }
    };
    const auto pushU16 = [&out](uint16_t value) {
        out.push_back(static_cast<uint8_t>(value & 0xFFu));
        out.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    };

    const float positions[9] = {
        opt.firstPositionX, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    for (const float p : positions) pushF(p);
    for (int i = 0; i < 9; ++i) pushF((i % 3 == 2) ? 1.0f : 0.0f);
    const float uvs[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    for (const float u : uvs) pushF(u);
    pushU16(0);
    pushU16(1);
    pushU16(opt.lastIndex);
    for (int v = 0; v < 3; ++v) {
        pushU16(0);
        pushU16(1);
        pushU16(0);
        pushU16(0);
    }
    for (int v = 0; v < 3; ++v) {
        pushF(1.0f);
        pushF(0.0f);
        pushF(0.0f);
        pushF(0.0f);
    }
    for (int j = 0; j < 2; ++j) {
        for (int c = 0; c < 16; ++c) pushF(c % 5 == 0 ? 1.0f : 0.0f);
    }
    // Animation key times.
    pushF(0.0f);
    pushF(1.0f);
    // Two translation keys.
    pushF(0.0f);
    pushF(0.0f);
    pushF(0.0f);
    pushF(1.0f);
    pushF(0.0f);
    pushF(0.0f);
    // Two identity quaternion keys.
    pushF(0.0f);
    pushF(0.0f);
    pushF(0.0f);
    pushF(1.0f);
    pushF(0.0f);
    pushF(0.0f);
    pushF(0.0f);
    pushF(1.0f);
    return out;
}

std::string buildGltf(const Options& opt) {
    std::vector<uint8_t> buffer = buildBuffer(opt);

    std::string accessors = R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]})";
    accessors += R"(,{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"})";
    accessors += R"(,{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"})";
    accessors += R"(,{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"})";
    accessors += R"(,{"bufferView":4,"componentType":5123,"count":3,"type":"VEC4"})";
    accessors += R"(,{"bufferView":5,"componentType":5126,"count":3,"type":"VEC4"})";
    accessors += R"(,{"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"})";
    accessors += R"(,{"bufferView":7,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]})";
    accessors += R"(,{"bufferView":8,"componentType":5126,"count":2,"type":"VEC3"})";
    accessors += R"(,{"bufferView":9,"componentType":5126,"count":2,"type":"VEC4"})";

    std::string views = R"({"buffer":0,"byteOffset":0,"byteLength":36})";
    views += R"(,{"buffer":0,"byteOffset":36,"byteLength":36})";
    views += R"(,{"buffer":0,"byteOffset":72,"byteLength":24})";
    views += R"(,{"buffer":0,"byteOffset":96,"byteLength":6})";
    views += R"(,{"buffer":0,"byteOffset":102,"byteLength":24})";
    views += R"(,{"buffer":0,"byteOffset":126,"byteLength":48})";
    views += R"(,{"buffer":0,"byteOffset":174,"byteLength":128})";
    views += R"(,{"buffer":0,"byteOffset":302,"byteLength":8})";
    views += R"(,{"buffer":0,"byteOffset":310,"byteLength":24})";
    views += R"(,{"buffer":0,"byteOffset":334,"byteLength":32})";

    std::string attributes = "{\"POSITION\":" + std::to_string(opt.positionAccessor);
    if (opt.withNormals) attributes += ",\"NORMAL\":1";
    if (opt.withUvs) attributes += ",\"TEXCOORD_0\":2";
    if (opt.withSkin) attributes += ",\"JOINTS_0\":4,\"WEIGHTS_0\":5";
    attributes += "}";

    std::string primitive = "{\"attributes\":" + attributes;
    if (opt.withIndices) primitive += ",\"indices\":" + std::to_string(opt.indexAccessor);
    if (opt.withMaterials) primitive += ",\"material\":0";
    if (opt.mode >= 0) primitive += ",\"mode\":" + std::to_string(opt.mode);
    primitive += "}";

    std::string meshes = "{\"name\":\"tri\",\"primitives\":[" + primitive + "]}";

    std::string nodes;
    if (opt.withHierarchy) {
        nodes = R"({"name":"root","children":[1]})";
        nodes += R"(,{"name":"child","mesh":0,"translation":[1,2,3]})";
    } else if (opt.withSkin) {
        nodes = R"({"name":"joint0"})";
        nodes += R"(,{"name":"joint1"})";
        nodes += R"(,{"name":"skinned","mesh":0,"skin":0})";
    } else {
        nodes = R"({"name":"node","mesh":0})";
    }

    std::string scenes = opt.withSkin ? "{\"nodes\":[0,1,2]}" : "{\"nodes\":[0]}";

    std::string materials;
    if (opt.withMaterials) {
        materials = "{";
        materials += "\"name\":\"mat\",";
        materials += R"("pbrMetallicRoughness":{"baseColorFactor":[0.5,0.25,0.125,1],"metallicFactor":0.1,"roughnessFactor":0.7)";
        if (opt.withTexture) materials += R"(,"baseColorTexture":{"index":0})";
        materials += "}";
        if (opt.withDoubleSided) materials += R"(,"doubleSided":true)";
        if (opt.withUnlit) materials += R"(,"extensions":{"KHR_materials_unlit":{}})";
        materials += "}";
    }

    std::string textures;
    std::string images;
    if (opt.withTexture) {
        textures = R"({"source":0})";
        images = R"({"uri":"data:image/png;base64,)" + std::string(kTexturePngBase64) + "\"}";
    }

    std::string animations;
    if (opt.withAnimation) {
        animations = R"({"name":"anim")";
        animations += R"(,"samplers":[{"input":7,"output":8,"interpolation":"LINEAR"},{"input":7,"output":9,"interpolation":"STEP"}])";
        animations += R"(,"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}},{"sampler":1,"target":{"node":1,"path":"rotation"}}])";
        animations += "}";
    }

    std::string skins;
    if (opt.withSkin) {
        skins = R"({"joints":[0,1],"inverseBindMatrices":6})";
    }

    std::string json = "{\"asset\":{\"version\":\"2.0\"}";
    json += ",\"scenes\":[" + scenes + "],\"scene\":0";
    json += ",\"nodes\":[" + nodes + "]";
    json += ",\"meshes\":[" + meshes + "]";
    if (!skins.empty()) json += ",\"skins\":[" + skins + "]";
    if (!animations.empty()) json += ",\"animations\":[" + animations + "]";
    if (!materials.empty()) json += ",\"materials\":[" + materials + "]";
    if (!textures.empty()) json += ",\"textures\":[" + textures + "]";
    if (!images.empty()) json += ",\"images\":[" + images + "]";
    json += ",\"accessors\":[" + accessors + "]";
    json += ",\"bufferViews\":[" + views + "]";
    json += ",\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size());
    json += ",\"uri\":\"data:application/octet-stream;base64," + base64Encode(buffer) + "\"}]}";
    return json;
}

bool convert(const std::string& json, voxy::moto::VmeshData* out, std::string* error) {
    const std::vector<uint8_t> bytes(json.begin(), json.end());
    return voxy::tools::convertGltfBytesToVmesh(bytes, /*isGlb=*/false, out, error);
}

bool roundTrip(const voxy::moto::VmeshData& mesh, voxy::moto::VmeshData* out,
               std::string* error) {
    std::vector<uint8_t> bytes;
    if (!voxy::moto::writeVmesh(mesh, &bytes, error)) return false;
    return voxy::moto::readVmesh(bytes.data(), bytes.size(), out, error);
}

TEST(GltfVmeshTool, ConvertsTriangle) {
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(Options{}), &mesh, &error)) << error;

    EXPECT_EQ(mesh.header.vertexCount, 3u);
    EXPECT_EQ(mesh.header.indexCount, 3u);
    EXPECT_EQ(mesh.header.indexStride, 2u);
    EXPECT_EQ(mesh.header.submeshCount, 1u);
    EXPECT_EQ(mesh.header.materialCount, 1u);
    EXPECT_EQ(mesh.header.nodeCount, 1u);
    EXPECT_EQ(mesh.header.flags, voxy::moto::kVmeshHasTangent);

    const auto* vertex = reinterpret_cast<const voxy::moto::VmeshVertex*>(mesh.vertices.data());
    EXPECT_NEAR(vertex[2].position[0], 0.0f, 1e-6f);
    EXPECT_NEAR(vertex[2].position[1], 1.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].normal[2], 1.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].tangent[0], 1.0f, 1e-5f);
    EXPECT_NEAR(vertex[0].tangent[1], 0.0f, 1e-5f);
    EXPECT_NEAR(vertex[0].tangent[2], 0.0f, 1e-5f);
    EXPECT_NEAR(vertex[0].tangent[3], 1.0f, 1e-5f);

    ASSERT_EQ(mesh.submeshes.size(), 1u);
    EXPECT_EQ(mesh.submeshes[0].indexCount, 3u);
    EXPECT_EQ(mesh.submeshes[0].materialIndex, 0u);
    EXPECT_EQ(mesh.submeshes[0].meshIndex, 0u);

    ASSERT_EQ(mesh.materials.size(), 1u);
    EXPECT_NEAR(mesh.materials[0].metallicFactor, 0.1f, 1e-6f);
    EXPECT_NEAR(mesh.materials[0].roughnessFactor, 0.7f, 1e-6f);
    EXPECT_NEAR(mesh.materials[0].baseColorFactor[0], 0.5f, 1e-6f);

    voxy::moto::VmeshData reloaded;
    ASSERT_TRUE(roundTrip(mesh, &reloaded, &error)) << error;
    EXPECT_EQ(reloaded.header.vertexCount, mesh.header.vertexCount);
    EXPECT_EQ(reloaded.header.indexCount, mesh.header.indexCount);
    EXPECT_EQ(reloaded.materials.size(), 1u);
}

TEST(GltfVmeshTool, NonIndexedSynthesizesIndices) {
    Options opt;
    opt.withIndices = false;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;
    EXPECT_EQ(mesh.header.vertexCount, 3u);
    EXPECT_EQ(mesh.header.indexCount, 3u);
    EXPECT_EQ(mesh.submeshes[0].indexCount, 3u);
}

TEST(GltfVmeshTool, MissingNormalDefaultsUp) {
    Options opt;
    opt.withNormals = false;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;
    const auto* vertex = reinterpret_cast<const voxy::moto::VmeshVertex*>(mesh.vertices.data());
    EXPECT_NEAR(vertex[0].normal[0], 0.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].normal[1], 1.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].normal[2], 0.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].tangent[0], 1.0f, 1e-6f);
    EXPECT_NEAR(vertex[0].tangent[1], 0.0f, 1e-6f);
}

TEST(GltfVmeshTool, DecodesTextureAsSrgbRgba8) {
    Options opt;
    opt.withTexture = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;

    ASSERT_EQ(mesh.materials.size(), 1u);
    const voxy::moto::VmeshMaterial& material = mesh.materials[0];
    EXPECT_EQ(material.hasTexture[voxy::moto::VmeshTextureBaseColor], 1u);
    EXPECT_EQ(material.textureIsSrgb[voxy::moto::VmeshTextureBaseColor], 1u);
    EXPECT_EQ(material.textureWidth[voxy::moto::VmeshTextureBaseColor], 2u);
    EXPECT_EQ(material.textureHeight[voxy::moto::VmeshTextureBaseColor], 1u);
    EXPECT_EQ(material.textureSize[voxy::moto::VmeshTextureBaseColor], 8u);
    EXPECT_EQ(mesh.images.size(), 8u);
    EXPECT_EQ(mesh.images[0], 255u);
    EXPECT_EQ(mesh.images[1], 0u);
    EXPECT_EQ(mesh.images[3], 255u);
    EXPECT_EQ(mesh.images[4], 0u);
    EXPECT_EQ(mesh.images[5], 255u);
}

TEST(GltfVmeshTool, NodeHierarchyPreservesParents) {
    Options opt;
    opt.withHierarchy = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;

    ASSERT_EQ(mesh.nodes.size(), 2u);
    EXPECT_EQ(mesh.nodes[0].parent, -1);
    EXPECT_EQ(mesh.nodes[1].parent, 0);
    EXPECT_NEAR(mesh.nodes[1].translation[0], 1.0f, 1e-6f);
    EXPECT_NEAR(mesh.nodes[1].translation[1], 2.0f, 1e-6f);
    EXPECT_NEAR(mesh.nodes[1].translation[2], 3.0f, 1e-6f);
    EXPECT_EQ(mesh.nodes[1].meshIndex, 0u);
}

TEST(GltfVmeshTool, AnimationsConvertAndRoundTrip) {
    Options opt;
    opt.withHierarchy = true;
    opt.withAnimation = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;

    ASSERT_EQ(mesh.header.animCount, 1u);
    ASSERT_EQ(mesh.header.animChannelCount, 2u);
    EXPECT_TRUE((mesh.header.flags & voxy::moto::kVmeshHasAnimations) != 0u);

    const voxy::moto::VmeshAnim& anim = mesh.anims[0];
    EXPECT_EQ(anim.channelCount, 2u);
    EXPECT_EQ(anim.channelsOffset, 0u);
    EXPECT_NEAR(anim.duration, 1.0f, 1e-6f);

    const auto* channel0 = &mesh.animChannels[0];
    const auto* channel1 = &mesh.animChannels[1];
    EXPECT_EQ(channel0->nodeIndex, 1u);
    EXPECT_EQ(channel0->path, voxy::moto::VmeshAnimPathTranslation);
    EXPECT_EQ(channel0->interpolation, voxy::moto::VmeshAnimInterpolationLinear);
    EXPECT_EQ(channel0->keyCount, 2u);
    EXPECT_EQ(channel0->keysOffset, 0u);
    EXPECT_EQ(channel1->nodeIndex, 1u);
    EXPECT_EQ(channel1->path, voxy::moto::VmeshAnimPathRotation);
    EXPECT_EQ(channel1->interpolation, voxy::moto::VmeshAnimInterpolationStep);
    EXPECT_EQ(channel1->keyCount, 2u);
    EXPECT_EQ(channel1->keysOffset, 8u + 24u);

    EXPECT_EQ(mesh.channelData.size(), (8u + 24u) + (8u + 32u));

    voxy::moto::VmeshData reloaded;
    ASSERT_TRUE(roundTrip(mesh, &reloaded, &error)) << error;
    ASSERT_EQ(reloaded.anims.size(), 1u);
    ASSERT_EQ(reloaded.animChannels.size(), 2u);
    EXPECT_EQ(reloaded.anims[0].channelsOffset, 0u);
    EXPECT_EQ(reloaded.channelData.size(), mesh.channelData.size());
}

TEST(GltfVmeshTool, SkinsPreserveJoints) {
    Options opt;
    opt.withSkin = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;

    ASSERT_EQ(mesh.header.skinCount, 1u);
    ASSERT_EQ(mesh.header.jointCount, 2u);
    EXPECT_TRUE((mesh.header.flags & voxy::moto::kVmeshHasSkin) != 0u);
    EXPECT_EQ(mesh.skins[0].jointCount, 2u);
    EXPECT_EQ(mesh.skins[0].jointsOffset, 0u);
    EXPECT_EQ(mesh.joints[0].nodeIndex, 0u);
    EXPECT_EQ(mesh.joints[1].nodeIndex, 1u);
    EXPECT_NEAR(mesh.joints[0].inverseBindMatrix[0], 1.0f, 1e-6f);

    const auto* vertex = reinterpret_cast<const voxy::moto::VmeshVertex*>(mesh.vertices.data());
    EXPECT_EQ(vertex[0].joint[0], 0u);
    EXPECT_EQ(vertex[0].joint[1], 1u);
    EXPECT_NEAR(vertex[0].weight[0], 1.0f, 1e-6f);

    voxy::moto::VmeshData reloaded;
    ASSERT_TRUE(roundTrip(mesh, &reloaded, &error)) << error;
    EXPECT_EQ(reloaded.header.jointCount, 2u);
}

TEST(GltfVmeshTool, DefaultMaterialSynthesized) {
    Options opt;
    opt.withMaterials = false;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;
    ASSERT_EQ(mesh.materials.size(), 1u);
    EXPECT_NEAR(mesh.materials[0].metallicFactor, 1.0f, 1e-6f);
}

TEST(GltfVmeshTool, MaterialFlagsUnlitAndDoubleSided) {
    Options opt;
    opt.withUnlit = true;
    opt.withDoubleSided = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;
    ASSERT_EQ(mesh.materials.size(), 1u);
    EXPECT_EQ(mesh.materials[0].unlit, 1u);
    EXPECT_EQ(mesh.materials[0].doubleSided, 1u);
}

TEST(GltfVmeshTool, EmptyInputFails) {
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(voxy::tools::convertGltfBytesToVmesh({}, /*isGlb=*/false, &mesh, &error));
    EXPECT_FALSE(error.empty());
}

TEST(GltfVmeshTool, GarbageInputFails) {
    const std::string garbage = "this is not gltf at all";
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(garbage, &mesh, &error));
    EXPECT_FALSE(error.empty());
}

TEST(GltfVmeshTool, TruncatedGlbFails) {
    const std::string truncated = "glTFbroken";
    const std::vector<uint8_t> bytes(truncated.begin(), truncated.end());
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(voxy::tools::convertGltfBytesToVmesh(bytes, /*isGlb=*/true, &mesh, &error));
    EXPECT_FALSE(error.empty());
}

TEST(GltfVmeshTool, NonFinitePositionFails) {
    Options opt;
    opt.firstPositionX = std::numeric_limits<float>::infinity();
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(buildGltf(opt), &mesh, &error));
    EXPECT_NE(error.find("non-finite POSITION"), std::string::npos);
}

TEST(GltfVmeshTool, NonTriangleModeFails) {
    Options opt;
    opt.mode = 1;  // LINES
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(buildGltf(opt), &mesh, &error));
    EXPECT_NE(error.find("only triangle"), std::string::npos);
}

TEST(GltfVmeshTool, OutOfRangeIndexFails) {
    Options opt;
    opt.lastIndex = 5;
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(buildGltf(opt), &mesh, &error));
    EXPECT_NE(error.find("index out of range"), std::string::npos);
}

TEST(GltfVmeshTool, MissingAccessorFails) {
    Options opt;
    opt.positionAccessor = 99;
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(buildGltf(opt), &mesh, &error));
    EXPECT_NE(error.find("accessor index out of range"), std::string::npos);
}

TEST(GltfVmeshTool, SparseAccessorFails) {
    // POSITION is a sparse accessor; the converter must reject it.
    const std::vector<uint8_t> zeroBuffer(56, 0);
    std::string json = R"({
        "asset":{"version":"2.0"},
        "scenes":[{"nodes":[0]}],
        "scene":0,
        "nodes":[{"mesh":0}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
             "sparse":{"count":1,
                       "indices":{"bufferView":2,"componentType":5123},
                       "values":{"bufferView":3}}},
            {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"},
            {"bufferView":2,"componentType":5123,"count":1,"type":"SCALAR"},
            {"bufferView":3,"componentType":5126,"count":1,"type":"VEC3"}
        ],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":6},
            {"buffer":0,"byteOffset":42,"byteLength":2},
            {"buffer":0,"byteOffset":44,"byteLength":12}
        ],
        "buffers":[{"byteLength":56,"uri":"data:application/octet-stream;base64,")";
    json += base64Encode(zeroBuffer);
    json += "\"}]}";
    voxy::moto::VmeshData mesh;
    std::string error;
    EXPECT_FALSE(convert(json, &mesh, &error));
    EXPECT_FALSE(error.empty());
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// test_gltf_vmesh_tool.cpp - glTF 2.0 -> .vmesh converter tests
// ═══════════════════════════════════════════════════════════════════════════════
// Drives the in-memory converter with ASCII glTF documents (data-URI buffers)
// and hostile inputs, then round-trips the result through writeVmesh/readVmesh.
// ═══════════════════════════════════════════════════════════════════════════════

#include "gltf_vmesh_tool.hpp"
#include "moto/vmesh.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <stb_image_write.h>

#include <cmath>
#include <array>
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

using Json = nlohmann::json;

std::vector<uint8_t> jsonGlb(const Json& document) {
    auto text = document.dump();
    while (text.size() % 4u != 0u) text += ' ';
    std::vector<uint8_t> bytes;
    const auto u32 = [&bytes](uint32_t value) {
        for (uint32_t shift = 0; shift < 32u; shift += 8u) bytes.push_back(static_cast<uint8_t>(value >> shift));
    };
    u32(0x46546c67u); u32(2u); u32(static_cast<uint32_t>(20u + text.size()));
    u32(static_cast<uint32_t>(text.size())); u32(0x4e4f534au);
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

bool rigid(const Json& document, voxy::moto::VmeshData* out, std::string* error) {
    return voxy::tools::convertGltfBytesToVmesh(jsonGlb(document), true,
        voxy::tools::GltfImportProfile::SalvageRigidV1, out, error);
}

void rejectsRigid(const Json& document, const char* reason = "") {
    voxy::moto::VmeshData result;
    result.stringBlob = "unchanged sentinel";
    std::string error;
    EXPECT_FALSE(rigid(document, &result, &error)) << document.dump();
    EXPECT_EQ(result.stringBlob, "unchanged sentinel");
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find(reason), std::string::npos) << error;
}

void addTangent(Json& document) {
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 3; ++i) for (float value : {1.0f, 0.0f, 0.0f, 1.0f}) {
        uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits));
        for (uint32_t shift = 0; shift < 32u; shift += 8u) bytes.push_back(static_cast<uint8_t>(bits >> shift));
    }
    const size_t buffer = document["buffers"].size();
    const size_t view = document["bufferViews"].size();
    const size_t accessor = document["accessors"].size();
    document["buffers"].push_back({{"byteLength", bytes.size()}, {"uri", "data:application/octet-stream;base64," + base64Encode(bytes)}});
    document["bufferViews"].push_back({{"buffer", buffer}, {"byteLength", bytes.size()}});
    document["accessors"].push_back({{"bufferView", view}, {"componentType", 5126}, {"count", 3}, {"type", "VEC4"}});
    document["meshes"][0]["primitives"][0]["attributes"]["TANGENT"] = accessor;
}

std::string solidPngUri(int width, int height) {
    const std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 127u);
    std::vector<uint8_t> png;
    const auto write = [](void* target, void* data, int size) {
        auto& output = *static_cast<std::vector<uint8_t>*>(target);
        const auto* bytes = static_cast<const uint8_t*>(data);
        output.insert(output.end(), bytes, bytes + size);
    };
    if (stbi_write_png_to_func(write, &png, width, height, 4, pixels.data(), width * 4) == 0) return {};
    return "data:image/png;base64," + base64Encode(png);
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

TEST(GltfVmeshTool, RigidProfileAcceptsGoldenAndPreservesSharedMeshHierarchy) {
    auto document = Json::parse(buildGltf(Options{}));
    document["nodes"] = Json::array({{{"children", {1, 2}}, {"translation", {2, 3, 4}}},
        {{"mesh", 0}, {"translation", {1, 0, 0}}}, {{"mesh", 0}, {"translation", {0, 0, 2}}}});
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(rigid(document, &mesh, &error)) << error;
    ASSERT_EQ(mesh.nodes.size(), 3u);
    EXPECT_EQ(mesh.nodes[1].parent, 0); EXPECT_EQ(mesh.nodes[2].parent, 0);
    EXPECT_EQ(mesh.nodes[1].meshIndex, mesh.nodes[2].meshIndex);
    EXPECT_EQ(mesh.nodes[1].translation[0], 1.0f);
    EXPECT_EQ(mesh.nodes[0].translation[1], 3.0f);
    voxy::moto::VmeshData copy;
    ASSERT_TRUE(roundTrip(mesh, &copy, &error)) << error;
    EXPECT_EQ(copy.vertices, mesh.vertices);
    EXPECT_EQ(copy.nodes[2].translation[2], 2.0f);
}

TEST(GltfVmeshTool, AllAttributeCountsAreValidatedBeforeReadsInLegacyAndRigidPaths) {
    Options options; options.withSkin = true;
    const auto original = Json::parse(buildGltf(options));
    for (const char* name : {"NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0", "TANGENT"}) {
        for (int count : {2, 4}) {
            auto document = original;
            addTangent(document);
            const size_t index = document["meshes"][0]["primitives"][0]["attributes"][name].get<size_t>();
            auto& accessor = document["accessors"][index];
            accessor["count"] = count;
            // Longer valid byte spans still have invalid cross-attribute counts.
            if (count == 4) {
                const size_t view = accessor["bufferView"].get<size_t>();
                document["bufferViews"][view]["byteLength"] = 64;
                if (std::string_view(name) == "TANGENT") {
                    std::vector<uint8_t> padding(64u, 0u);
                    const size_t buffer = document["bufferViews"][view]["buffer"].get<size_t>();
                    document["buffers"][buffer] = {{"byteLength", 64}, {"uri", "data:application/octet-stream;base64," + base64Encode(padding)}};
                }
            }
            voxy::moto::VmeshData mesh; mesh.stringBlob = "sentinel";
            std::string error;
            EXPECT_FALSE(convert(document.dump(), &mesh, &error)) << name << " " << count;
            EXPECT_EQ(mesh.stringBlob, "sentinel");
            EXPECT_NE(error.find("count"), std::string::npos) << error;
            document.erase("skins"); document["nodes"] = Json::array({{{"mesh", 0}}}); document["scenes"][0]["nodes"] = {0};
            document["meshes"][0]["primitives"][0]["attributes"].erase("JOINTS_0");
            document["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_0");
            if (std::string_view(name) != "JOINTS_0" && std::string_view(name) != "WEIGHTS_0") rejectsRigid(document, "count");
        }
    }
}

TEST(GltfVmeshTool, AccessorOffsetsCannotWrapAndStrictAlignmentIsChecked) {
    const auto original = Json::parse(buildGltf(Options{}));
    for (const char* where : {"accessor", "view"}) {
        auto document = original;
        document[std::string_view(where) == "view" ? "bufferViews" : "accessors"][0]["byteOffset"] = UINT64_MAX - 15u;
        voxy::moto::VmeshData mesh;
        std::string error;
        EXPECT_FALSE(convert(document.dump(), &mesh, &error));
        rejectsRigid(document);
    }
    auto document = original;
    document["bufferViews"][0]["byteOffset"] = 1;
    rejectsRigid(document, "alignment");
    document = original; document["bufferViews"][0]["byteStride"] = 13;
    document["bufferViews"][0]["byteLength"] = 39;
    rejectsRigid(document, "byteStride");
}

TEST(GltfVmeshTool, MatrixAndTrsAgreeAcrossQuaternionBranchesAndAsymmetricRotation) {
    const double a = std::sqrt(0.5);
    const std::vector<std::array<double, 4>> rotations = {{{0, 0, a, a}}, {{1, 0, 0, 0}},
        {{0, 1, 0, 0}}, {{0, 0, 1, 0}}, {{0.2, -0.3, 0.4, std::sqrt(0.71)}}};
    for (const auto& q : rotations) {
        const double x = q[0], y = q[1], z = q[2], w = q[3];
        // Independent column-major T*R*S fixture with asymmetric scale/translation.
        const Json matrix = {2*(1-2*(y*y+z*z)), 2*(2*(x*y+z*w)), 2*(2*(x*z-y*w)), 0,
            3*(2*(x*y-z*w)), 3*(1-2*(x*x+z*z)), 3*(2*(y*z+x*w)), 0,
            4*(2*(x*z+y*w)), 4*(2*(y*z-x*w)), 4*(1-2*(x*x+y*y)), 0, 5, -7, 11, 1};
        auto trs = Json::parse(buildGltf(Options{}));
        trs["nodes"][0]["translation"] = {5, -7, 11};
        trs["nodes"][0]["scale"] = {2, 3, 4};
        trs["nodes"][0]["rotation"] = q;
        auto mat = trs; mat["nodes"][0] = {{"mesh", 0}, {"matrix", matrix}};
        voxy::moto::VmeshData t, m;
        std::string error;
        ASSERT_TRUE(rigid(trs, &t, &error)) << error;
        ASSERT_TRUE(rigid(mat, &m, &error)) << error;
        for (size_t c = 0; c < 4u; ++c) EXPECT_NEAR(t.nodes[0].rotation[c], m.nodes[0].rotation[c], 1.0e-6f);
        for (size_t c = 0; c < 3u; ++c) { EXPECT_NEAR(t.nodes[0].scale[c], m.nodes[0].scale[c], 1.0e-6f); EXPECT_EQ(t.nodes[0].translation[c], m.nodes[0].translation[c]); }
        EXPECT_EQ(t.vertices, m.vertices);  // No basis/transform baked into geometry.
        for (size_t c = 0; c < 4u; ++c) trs["nodes"][0]["rotation"][c] = -q[c];
        voxy::moto::VmeshData negative;
        ASSERT_TRUE(rigid(trs, &negative, &error)) << error;
        for (size_t c = 0; c < 4u; ++c) EXPECT_EQ(negative.nodes[0].rotation[c], t.nodes[0].rotation[c]);
    }
}

TEST(GltfVmeshTool, RigidRejectsInvalidTransformsAndNonTrsMatrices) {
    const auto original = Json::parse(buildGltf(Options{}));
    for (const auto& transform : std::vector<Json>{
        {{"translation", {1, 2}}}, {{"rotation", {0, 0, 0, 0}}}, {{"rotation", {0, 0, 0, 2}}},
        {{"scale", {1, 0, 1}}}, {{"scale", {-1, 1, 1}}}, {{"scale", {1e300, 1, 1}}},
        {{"matrix", {1,0,0,0, 0.5,1,0,0, 0,0,1,0, 0,0,0,1}}},
        {{"matrix", {1,0,0,0.1, 0,1,0,0, 0,0,1,0, 0,0,0,1}}},
        {{"matrix", {-1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}},
        {{"matrix", {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}, {"scale", {1,1,1}}}}) {
        auto document = original;
        document["nodes"][0].update(transform);
        rejectsRigid(document);
    }
}

TEST(GltfVmeshTool, RigidRejectsInvalidSceneTopology) {
    const auto original = Json::parse(buildGltf(Options{}));
    for (const auto& nodes : std::vector<Json>{
        Json::array({{{"mesh",0}}, {{"mesh",0}}}),
        Json::array({{{"mesh",0},{"children",{0}}}}),
        Json::array({{{"children",{1,1}}},{{"mesh",0}}}),
        Json::array({{{"children",{1,2}}},{{"children",{2}}},{{"mesh",0}}}),
        Json::array({{{"mesh",0}},{{"children",{2}}},{{"children",{1}}}})}) {
        auto document = original; document["nodes"] = nodes; rejectsRigid(document);
    }
    auto document = original; document["scenes"][0]["nodes"] = {0,0}; rejectsRigid(document);
    document = original; document["scene"] = 1; rejectsRigid(document);
    document = original; document["scenes"].push_back(document["scenes"][0]); rejectsRigid(document);
}

TEST(GltfVmeshTool, RigidRejectsUnsupportedAppearanceAndFunctionRatherThanDroppingIt) {
    const auto original = Json::parse(buildGltf(Options{}));
    for (const char* attribute : {"COLOR_0", "TEXCOORD_1", "JOINTS_0", "WEIGHTS_1", "_CUSTOM"}) {
        auto document = original; document["meshes"][0]["primitives"][0]["attributes"][attribute] = 0;
        rejectsRigid(document, "unsupported rigid attribute");
    }
    for (const char* extension : {"KHR_materials_clearcoat", "KHR_texture_transform", "EXT_mesh_gpu_instancing"}) {
        auto document = original; document["extensionsRequired"] = {extension}; rejectsRigid(document, "extension");
        document = original; document["materials"][0]["extensions"][extension] = Json::object(); rejectsRigid(document, "extension");
    }
    auto document = original; document["materials"][0]["normalTexture"] = {{"index",0},{"extensions",{{"KHR_texture_transform",Json::object()}}}}; rejectsRigid(document, "extension");
    document = original; document["meshes"][0]["primitives"][0]["targets"] = Json::array(); rejectsRigid(document, "morph");
    document = original; document["meshes"][0]["weights"] = {1}; rejectsRigid(document, "morph");
    for (const char* key : {"skins", "animations", "cameras"}) { document = original; document[key] = Json::array({Json::object()}); rejectsRigid(document); }
    for (const char* mode : {"MASK", "BLEND"}) { document = original; document["materials"][0]["alphaMode"] = mode; rejectsRigid(document, "opaque"); }
    document = original; document["materials"][0]["occlusionTexture"] = {{"index",0}}; rejectsRigid(document, "AO");
}

TEST(GltfVmeshTool, RigidChecksMaterialUvTangentAndSamplerContracts) {
    Options options; options.withTexture = true;
    const auto original = Json::parse(buildGltf(options));
    auto document = original; document["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0"); rejectsRigid(document, "TEXCOORD_0");
    document = original; document["materials"][0]["normalTexture"] = {{"index",0}}; rejectsRigid(document, "authored TANGENT");
    addTangent(document); voxy::moto::VmeshData mesh; std::string error;
    ASSERT_TRUE(rigid(document, &mesh, &error)) << error;
    EXPECT_EQ(mesh.materials[0].textureIsSrgb[voxy::moto::VmeshTextureNormal], 0u);
    EXPECT_EQ(mesh.materials[0].textureIsSrgb[voxy::moto::VmeshTextureBaseColor], 1u);
    for (const auto& sampler : std::vector<Json>{{{"wrapS",33071}},{{"magFilter",9728}},{{"minFilter",9729}}}) {
        document = original; document["samplers"] = Json::array({sampler}); document["textures"][0]["sampler"] = 0; rejectsRigid(document, "sampler");
    }
    document = original; document["samplers"] = Json::array({{{"wrapS",10497},{"wrapT",10497},{"magFilter",9729},{"minFilter",9987}}}); document["textures"][0]["sampler"] = 0;
    ASSERT_TRUE(rigid(document, &mesh, &error)) << error;
    document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = -1; rejectsRigid(document, "factor");
}

TEST(GltfVmeshTool, RigidTextureRowsMatchGltfWithoutChangingLegacyRows) {
    constexpr const char* quadrants = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVR4nGP4z8DwHwyBNBAw/AcAR8oI+ItOQ4UAAAAASUVORK5CYII=";
    Options options; options.withTexture = true;
    auto document = Json::parse(buildGltf(options));
    document["images"][0]["uri"] = std::string("data:image/png;base64,") + quadrants;
    voxy::moto::VmeshData strict, legacy; std::string error;
    ASSERT_TRUE(rigid(document, &strict, &error)) << error;
    ASSERT_TRUE(convert(document.dump(), &legacy, &error)) << error;
    EXPECT_EQ(strict.images, (std::vector<uint8_t>{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}));
    EXPECT_EQ(legacy.images, (std::vector<uint8_t>{0,0,255,255, 255,255,0,255, 255,0,0,255, 0,255,0,255}));
    EXPECT_EQ(strict.vertices, legacy.vertices); // UVs and tangent handedness unchanged.
}

TEST(GltfVmeshTool, RigidInputDependenciesAndJsonLimitsRejectBeforeLoading) {
    const auto original = Json::parse(buildGltf(Options{}));
    for (const char* uri : {"other.bin", "https://example.invalid/a", "data:relative.bin", "data:image/png;base64,%%%!", "data:image/png;base64,"}) {
        auto document = original; document["buffers"][0]["uri"] = uri; rejectsRigid(document, "URI");
    }
    auto document = original; document["nodes"] = std::vector<Json>(257u, Json::object()); rejectsRigid(document, "limit");
    document = original; document["images"] = std::vector<Json>(65u, Json::object()); rejectsRigid(document, "limit");
    document = original; document["accessors"][0]["count"] = UINT64_MAX; rejectsRigid(document, "limit");
    document = original; document["extras"] = std::string(1024u*1024u, 'x'); rejectsRigid(document, "JSON byte");
    voxy::moto::VmeshData mesh; std::string error;
    const auto ascii = original.dump();
    EXPECT_FALSE(voxy::tools::convertGltfBytesToVmesh({ascii.begin(),ascii.end()}, false, voxy::tools::GltfImportProfile::SalvageRigidV1, &mesh, &error));
    EXPECT_FALSE(voxy::tools::convertGltfBytesToVmesh(jsonGlb(original), true, static_cast<voxy::tools::GltfImportProfile>(255), &mesh, &error));
}

TEST(GltfVmeshTool, RigidBoundsImageDecodeBeforeAllocationAndRepeatedOutputCopies) {
    Options options; options.withTexture = true;
    const auto original = Json::parse(buildGltf(options));
    auto document = original;
    document["images"][0]["uri"] = solidPngUri(2049, 1);
    rejectsRigid(document, "dimension limit");
    const auto bigImage = Json{{"uri", solidPngUri(2048, 2048)}};
    ASSERT_GT(bigImage["uri"].get<std::string>().size(), 32u);
    document = original;
    // Reuse one compressed payload through bufferView references: repeating
    // its base64 string nine times would hit the JSON budget before decoding.
    const auto pngUri = bigImage["uri"].get<std::string>();
    const auto payload = pngUri.substr(pngUri.find(',') + 1u);
    const size_t pngBytes = payload.size() / 4u * 3u
        - (payload.ends_with("==") ? 2u : (payload.ends_with('=') ? 1u : 0u));
    const size_t buffer = document["buffers"].size();
    const size_t view = document["bufferViews"].size();
    document["buffers"].push_back({{"byteLength", pngBytes}, {"uri", "data:application/octet-stream;base64," + payload}});
    document["bufferViews"].push_back({{"buffer", buffer}, {"byteLength", pngBytes}});
    document["images"] = std::vector<Json>(9u, Json{{"bufferView", view}, {"mimeType", "image/png"}});
    rejectsRigid(document, "decoded image byte limit");
    document = original;
    document["images"][0] = bigImage;
    document["materials"] = std::vector<Json>(9u, original["materials"][0]);
    rejectsRigid(document, "copied image byte limit");
}

TEST(GltfVmeshTool, RigidChecksReferenceSignsAndSynthesizesAnActualDefaultMaterial) {
    const auto original = Json::parse(buildGltf(Options{}));
    auto document = original;
    document["meshes"][0]["primitives"][0].erase("material");
    voxy::moto::VmeshData mesh; std::string error;
    ASSERT_TRUE(rigid(document, &mesh, &error)) << error;
    ASSERT_EQ(mesh.materials.size(), 2u);
    EXPECT_EQ(mesh.submeshes[0].materialIndex, 1u);
    EXPECT_EQ(mesh.materials[1].metallicFactor, 1.0f);
    EXPECT_EQ(mesh.materials[1].baseColorFactor[0], 1.0f);
    for (const char* key : {"material", "indices"}) {
        document = original; document["meshes"][0]["primitives"][0][key] = -2; rejectsRigid(document);
    }
    document = original; document["buffers"][0]["byteLength"] = 0; rejectsRigid(document, "buffers");
    document = original; document["buffers"] = Json::array({{{"byteLength", 64u*1024u*1024u}}, {{"byteLength",1}}}); rejectsRigid(document, "buffers");
}

TEST(GltfVmeshTool, RigidRejectsNarrowingAndMalformedValuesBeforeParserDefaults) {
    Options options; options.withTexture = true;
    const auto original = Json::parse(buildGltf(options));
    auto document = original; document["bufferViews"][0]["buffer"] = uint64_t{1} << 32u; rejectsRigid(document);
    document = original; document["textures"][0]["source"] = uint64_t{1} << 32u; rejectsRigid(document);
    document = original; document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"] = (uint64_t{1} << 32u); rejectsRigid(document);
    document = original; document["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"] = -1; rejectsRigid(document);
    document = original; document["materials"][0]["pbrMetallicRoughness"]["roughnessFactor"] = "invalid"; rejectsRigid(document, "factor");
    document = original; document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"] = {1,1}; rejectsRigid(document, "factor");
    document = original; document["materials"][0]["alphaMode"] = 0; rejectsRigid(document, "opaque");
    document = original; document["materials"][0]["doubleSided"] = 1; rejectsRigid(document, "boolean");
    document = original; document["images"][0]["mimeType"] = "image/jpeg"; rejectsRigid(document, "MIME mismatch");
}

TEST(GltfVmeshTool, FiniteInputsCannotPublishOverflowingSynthesizedTangents) {
    const Options options;
    auto document = Json::parse(buildGltf(options));
    auto buffer = buildBuffer(options);
    const auto put = [&buffer](size_t offset, float value) {
        uint32_t bits = 0u; std::memcpy(&bits, &value, sizeof(bits));
        for (size_t i = 0; i < 4u; ++i) buffer[offset + i] = static_cast<uint8_t>(bits >> (i * 8u));
    };
    put(12u, 1.0e20f); put(28u, 1.0e20f);  // Finite triangle coordinates.
    for (float span : {1.0e-20f, 1.0f}) {
        // Tiny UVs overflow direction arithmetic; ordinary UVs with huge
        // geometry overflow squared-length normalization and formerly emit 0.
        put(80u, span); put(92u, span);
        document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64Encode(buffer);
        voxy::moto::VmeshData output; output.stringBlob = "sentinel";
        std::string error;
        EXPECT_FALSE(convert(document.dump(), &output, &error));
        EXPECT_NE(error.find("generated TANGENT"), std::string::npos) << error;
        EXPECT_EQ(output.stringBlob, "sentinel");
        rejectsRigid(document, "generated TANGENT");
    }
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

TEST(GltfVmeshTool, RejectsOverflowingStringBlobSize) {
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(Options{}), &mesh, &error)) << error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(voxy::moto::writeVmesh(mesh, &bytes, &error)) << error;
    voxy::moto::VmeshHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    header.stringBlobSize = std::numeric_limits<uint64_t>::max();
    std::memcpy(bytes.data(), &header, sizeof(header));

    voxy::moto::VmeshData reloaded;
    EXPECT_FALSE(voxy::moto::readVmesh(bytes.data(), bytes.size(), &reloaded, &error));
    EXPECT_EQ(error, "string blob overflow");
}

TEST(GltfVmeshTool, RejectsOverflowingAnimationRanges) {
    Options opt;
    opt.withHierarchy = true;
    opt.withAnimation = true;
    voxy::moto::VmeshData mesh;
    std::string error;
    ASSERT_TRUE(convert(buildGltf(opt), &mesh, &error)) << error;
    std::vector<uint8_t> original;
    ASSERT_TRUE(voxy::moto::writeVmesh(mesh, &original, &error)) << error;
    voxy::moto::VmeshHeader header;
    std::memcpy(&header, original.data(), sizeof(header));

    // Adding a channel's key bytes to this offset wraps back into the buffer.
    auto bytes = original;
    auto channel = mesh.animChannels[0];
    channel.keysOffset = std::numeric_limits<uint64_t>::max() - 15u;
    std::memcpy(bytes.data() + header.animChannelsOffset, &channel, sizeof(channel));
    voxy::moto::VmeshData reloaded;
    EXPECT_FALSE(voxy::moto::readVmesh(bytes.data(), bytes.size(), &reloaded, &error));
    EXPECT_EQ(error, "anim keys out of range");

    // Keep the channel offset aligned while making its end wrap around.
    bytes = original;
    auto anim = mesh.anims[0];
    const uint64_t limit = std::numeric_limits<uint64_t>::max();
    anim.channelsOffset = limit - limit % sizeof(voxy::moto::VmeshAnimChannel);
    std::memcpy(bytes.data() + header.animsOffset, &anim, sizeof(anim));
    EXPECT_FALSE(voxy::moto::readVmesh(bytes.data(), bytes.size(), &reloaded, &error));
    EXPECT_EQ(error, "anim channel range out of bounds");
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

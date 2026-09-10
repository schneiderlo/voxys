#include "game/assets/cooked_part_bundle.hpp"
#include "core/sha256.hpp"

#include <gtest/gtest.h>
#include <json.hpp>

#include <cstring>
#include <fstream>
#include <map>

namespace voxy::game::assets {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr uint64_t kLod = 9007199254740997ull;

Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }
std::string digest(const Bytes& value) { return core::sha256Hex(core::sha256(std::as_bytes(std::span(value)))); }

Bytes triangle() {
    moto::VmeshData mesh;
    mesh.header.meshCount = 1;
    mesh.header.vertexCount = 3;
    mesh.header.indexCount = 3;
    mesh.header.submeshCount = 1;
    mesh.header.materialCount = 1;
    mesh.header.nodeCount = 1;
    mesh.header.indexStride = 4;
    mesh.header.flags = moto::kVmeshHasTangent;
    std::array<moto::VmeshVertex, 3> vertices{};
    for (auto& vertex : vertices) { vertex.normal[2] = 1; vertex.tangent[0] = 1; vertex.tangent[3] = 1; }
    vertices[1].position[0] = 2;
    vertices[2].position[1] = 1;
    mesh.vertices.resize(sizeof(vertices));
    std::memcpy(mesh.vertices.data(), vertices.data(), sizeof(vertices));
    const std::array<uint32_t, 3> indices{0, 1, 2};
    mesh.indices.resize(sizeof(indices));
    std::memcpy(mesh.indices.data(), indices.data(), sizeof(indices));
    mesh.submeshes.push_back({0, 3, 0, 0});
    mesh.materials.emplace_back();
    mesh.nodes.emplace_back();
    mesh.nodes[0].meshIndex = 0;
    Bytes result;
    std::string error;
    if (!moto::writeVmesh(mesh, &result, &error)) throw std::runtime_error(error);
    return result;
}

struct Fixture {
    Json metadata;
    Json manifest;
    CookedPartSelection selection;
    std::map<std::string, Bytes, std::less<>> files;
    std::vector<std::string> reads;

    Fixture() {
        std::ifstream file("tools/salvage_assets/fixtures/probe.gameplay.json");
        file >> metadata;
        std::string error;
        const auto sidecar = parseGameplaySidecar(metadata.dump(), [](const auto&) { return true; }, error);
        if (!sidecar) throw std::runtime_error(error);
        selection.part = sidecar->part.key;
        selection.lodRules.push_back({kLod, {}});
        files["gameplay.json"] = bytes(sidecar->normalizedJson);
        files[cookedLodFilename(kLod)] = triangle();
        const auto& source = metadata["lods"][0]["source"];
        const std::string hash(64, 'a');
        manifest = {
            {"schema", 1}, {"scope", "offline gameplay sidecar and unchanged exported-glTF-frame VMESH"},
            {"input_sidecar_sha256", hash},
            {"normalized_sidecar", {{"file", "gameplay.json"}, {"sha256", ""}, {"bytes", 1}}},
            {"cooker", {{"sha256", hash}, {"source", "cook_gameplay_asset.py"}}},
            {"converter", {{"sha256", hash}, {"profile", "salvage-rigid-v1"},
                {"interface", "--profile salvage-rigid-v1 input.glb output.vmesh"}}},
            {"validator", {{"sha256", hash}, {"interface", "sidecar.json cooked-directory"}}},
            {"lods", Json::array({{{"id", construction::u64ToDecimal(kLod)},
                {"file", cookedLodFilename(kLod)}, {"sha256", ""}, {"bytes", 1},
                {"source_bytes", source["bytes"]}, {"source_sha256", source["sha256"]}}})}};
        rehashPayloads();
    }

    void trustManifest() {
        files["cook-manifest.json"] = bytes(manifest.dump(2) + "\n");
        selection.manifestSha256 = digest(files["cook-manifest.json"]);
    }
    void rehashPayloads() {
        auto& sidecar = manifest["normalized_sidecar"];
        sidecar["sha256"] = digest(files["gameplay.json"]);
        sidecar["bytes"] = files["gameplay.json"].size();
        for (auto& lod : manifest["lods"]) {
            auto& payload = files[lod["file"].get<std::string>()];
            lod["sha256"] = digest(payload);
            lod["bytes"] = payload.size();
        }
        trustManifest();
    }
    CookedPartByteProvider provider() {
        return [this](std::string_view name, size_t maximum, std::string& error) -> std::optional<Bytes> {
            reads.emplace_back(name);
            const auto found = files.find(name);
            if (found == files.end() || found->second.size() > maximum) { error = "missing/over cap"; return std::nullopt; }
            return found->second;
        };
    }
    std::unique_ptr<const CookedPartBundle> admit(std::string& error) {
        return admitCookedPartBundle(selection, provider(), error);
    }
    void rejected(std::string_view reason) {
        std::string error;
        EXPECT_FALSE(admit(error));
        EXPECT_NE(error.find(reason), std::string::npos) << error;
    }
    void replaceMetadata(Json value, bool normalize = true) {
        std::string error;
        if (normalize) {
            const auto parsed = parseGameplaySidecar(value.dump(), [](const auto&) { return true; }, error);
            if (!parsed) throw std::runtime_error(error);
            files["gameplay.json"] = bytes(parsed->normalizedJson);
        } else files["gameplay.json"] = bytes(value.dump());
        rehashPayloads();
    }
};

TEST(CookedPartBundle, AcceptsOwnedCanonicalAssetWithLosslessKeysAndRecordedBasis) {
    Fixture fixture;
    std::string error;
    auto bundle = fixture.admit(error);
    ASSERT_TRUE(bundle) << error;
    ASSERT_EQ(bundle->lods().size(), 1u);
    EXPECT_EQ(bundle->sidecar().part.key, fixture.selection.part);
    EXPECT_EQ(bundle->lods()[0].id, kLod);
    EXPECT_EQ(bundle->lods()[0].prefab.renderToCanonical.value, 12u);
    EXPECT_DOUBLE_EQ(bundle->lods()[0].prefab.canonicalBounds.minimum.x, -2);
    EXPECT_DOUBLE_EQ(bundle->sidecar().part.sockets[0].frame.translation.y, 32);
    EXPECT_EQ(bundle->requestedGpuBytes(), bundle->lods()[0].prefab.counts.gpuBytes);
    EXPECT_GT(bundle->inputBytes(), bundle->decodedBytes());
    EXPECT_EQ(fixture.reads, (std::vector<std::string>{"cook-manifest.json", "gameplay.json", cookedLodFilename(kLod)}));
    fixture.files.clear();
    EXPECT_EQ(bundle->lods()[0].mesh.header.vertexCount, 3u);
    EXPECT_FALSE(bundle->manifestJson().empty());
}

TEST(CookedPartBundle, AdmitsPinnedActualConverterOutputWithoutShippingSourceOrTools) {
    Fixture fixture;
    fixture.files.clear();
    for (const auto& name : {std::string("cook-manifest.json"), std::string("gameplay.json"), cookedLodFilename(kLod)}) {
        std::ifstream file("data/salvage/runtime_probe_v1/" + name, std::ios::binary);
        ASSERT_TRUE(file) << name;
        fixture.files[name] = Bytes(std::istreambuf_iterator<char>(file), {});
    }
    // Pinned independently from the files, with actual source/cooker/converter
    // provenance retained in ASSET-03. Never recompute this expected digest here.
    fixture.selection.manifestSha256 = "f595f6b490fbbcc6f947eb1ac31f7328cc4dedf9d84896268a8aed45dea80898";
    std::string error;
    const auto bundle = fixture.admit(error);
    ASSERT_TRUE(bundle) << error;
    ASSERT_EQ(bundle->lods().size(), 1u);
    EXPECT_EQ(bundle->lods()[0].vmeshSha256, "4444fd14cfc967b46f3dcbe1079c04f1d0329ed890754ce98f367e71c1c36689");
    EXPECT_EQ(bundle->lods()[0].mesh.header.meshCount, 4u);
    EXPECT_EQ(bundle->lods()[0].mesh.header.indexCount / 3u, 752u);
    EXPECT_EQ(bundle->lods()[0].prefab.counts.meshInstances, 4u);
    EXPECT_EQ(fixture.reads.size(), 3u); // No GLB, executable or Python source requested.
}

TEST(CookedPartBundle, TamperedManifestCannotAuthorizeItsOwnReplacement) {
    Fixture fixture;
    std::string error;
    const auto accepted = fixture.admit(error);
    ASSERT_TRUE(accepted);
    const auto selectedHash = fixture.selection.manifestSha256;
    fixture.files[cookedLodFilename(kLod)].back() ^= 1;
    fixture.rehashPayloads(); // Attacker can replace all neighboring bundle files.
    fixture.selection.manifestSha256 = selectedHash; // Installed registry stays trusted.
    fixture.rejected("SHA256 mismatch: cook-manifest.json");
    EXPECT_EQ(accepted->sidecar().part.key, fixture.selection.part);
    EXPECT_EQ(accepted->lods()[0].mesh.header.vertexCount, 3u);
}

TEST(CookedPartBundle, ExactPayloadHashesAndSizesRejectBeforeDecode) {
    for (const auto& name : {std::string("gameplay.json"), cookedLodFilename(kLod)}) {
        Fixture fixture;
        fixture.files[name][0] ^= 1;
        fixture.rejected("SHA256 mismatch: " + name);
    }
    Fixture fixture;
    fixture.files[cookedLodFilename(kLod)].pop_back();
    fixture.rejected("snapshot size");
}

TEST(CookedPartBundle, MissingFilesAndProviderCapViolationsFailClosed) {
    Fixture fixture;
    fixture.files.erase(cookedLodFilename(kLod));
    fixture.rejected("bundle snapshot");
    const CookedPartByteProvider badProvider = [](std::string_view, size_t maximum, std::string&) {
        return std::optional<Bytes>(Bytes(maximum + 1));
    };
    std::string error;
    EXPECT_FALSE(admitCookedPartBundle(fixture.selection, badProvider, error));
    EXPECT_NE(error.find("snapshot size"), std::string::npos);
}

TEST(CookedPartBundle, StrictManifestRejectsUnknownDuplicateAndDeepObjects) {
    Fixture fixture;
    fixture.manifest["ignored"] = 1;
    fixture.trustManifest(); fixture.rejected("manifest object fields");
    fixture.manifest.erase("ignored");
    auto text = fixture.manifest.dump();
    text.insert(1, "\"schema\":1,");
    fixture.files["cook-manifest.json"] = bytes(text);
    fixture.selection.manifestSha256 = digest(fixture.files["cook-manifest.json"]);
    fixture.rejected("duplicate");
    text = std::string(10, '[') + "0" + std::string(10, ']');
    fixture.files["cook-manifest.json"] = bytes(text);
    fixture.selection.manifestSha256 = digest(fixture.files["cook-manifest.json"]);
    fixture.rejected("complexity");
}

TEST(CookedPartBundle, WrongToolProfileInterfacesOrProvenanceReject) {
    for (const auto& field : {"profile", "interface", "sha256"}) {
        Fixture fixture;
        fixture.manifest["converter"][field] = "legacy";
        fixture.trustManifest(); fixture.rejected("rejected");
    }
    Fixture fixture;
    fixture.manifest["validator"]["interface"] = "anything";
    fixture.trustManifest(); fixture.rejected("validator interface");
}

TEST(CookedPartBundle, FilenamesAreDerivedAndNeverReadAsPaths) {
    for (const auto& name : {"../outside.vmesh", "/tmp/input.vmesh", "https://example.test/a", "lod-1.vmesh"}) {
        Fixture fixture;
        fixture.manifest["lods"][0]["file"] = name;
        fixture.trustManifest(); fixture.rejected("derived LOD filename");
        ASSERT_EQ(fixture.reads.size(), 1u);
    }
    Fixture fixture;
    fixture.manifest["normalized_sidecar"]["file"] = "../gameplay.json";
    fixture.trustManifest(); fixture.rejected("sidecar filename");
}

TEST(CookedPartBundle, NoncanonicalIdsAndSizesCannotRoundOrCoerce) {
    for (const Json& value : {Json("09007199254740997"), Json(kLod), Json("18446744073709551616"), Json("0")}) {
        Fixture fixture;
        fixture.manifest["lods"][0]["id"] = value;
        fixture.trustManifest(); fixture.rejected("canonical LOD ID");
    }
    for (const Json& value : {Json(-1), Json(3.5), Json(true), Json(0), Json(UINT64_MAX)}) {
        Fixture fixture;
        fixture.manifest["lods"][0]["bytes"] = value;
        fixture.trustManifest(); fixture.rejected("manifest");
    }
}

TEST(CookedPartBundle, DifferentSelectedVersionOrLodSetRejects) {
    Fixture fixture;
    ++fixture.selection.part.version;
    fixture.rejected("selected part/version");
    --fixture.selection.part.version;
    ++fixture.selection.lodRules[0].id;
    fixture.rejected("surplus LOD");
    fixture.selection.lodRules.push_back(fixture.selection.lodRules[0]);
    fixture.rejected("duplicate/zero LOD rule");
}

TEST(CookedPartBundle, SidecarAndManifestSourceBindingsMustAgree) {
    Fixture fixture;
    fixture.manifest["lods"][0]["source_sha256"] = std::string(64, 'b');
    fixture.trustManifest(); fixture.rejected("source LOD binding");
    fixture.manifest["lods"][0]["source_sha256"] = fixture.metadata["lods"][0]["source"]["sha256"];
    fixture.manifest["lods"][0]["source_bytes"] = 999;
    fixture.trustManifest(); fixture.rejected("source LOD binding");
}

TEST(CookedPartBundle, MetadataRequiresSharedPhysicalValidationAndExactNormalization) {
    Fixture fixture;
    fixture.replaceMetadata(fixture.metadata, false);
    fixture.rejected("canonical normalized");
    fixture.metadata["part"]["mass"]["dry_mass_kg"] = -1;
    fixture.replaceMetadata(fixture.metadata, false);
    fixture.rejected("gameplay sidecar");
}

TEST(CookedPartBundle, BoundManifestDoesNotReplaceRigidMeshValidation) {
    Fixture fixture;
    auto& payload = fixture.files[cookedLodFilename(kLod)];
    moto::VmeshHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    moto::VmeshNode node{};
    std::memcpy(&node, payload.data() + header.nodesOffset, sizeof(node));
    node.parent = -2;
    std::memcpy(payload.data() + header.nodesOffset, &node, sizeof(node));
    fixture.rehashPayloads(); fixture.rejected("rigid prefab");
    header.vertexCount = UINT32_MAX;
    std::memcpy(payload.data(), &header, sizeof(header));
    fixture.rehashPayloads(); fixture.rejected("header capacity");
}

TEST(CookedPartBundle, InputDecodedAndGpuBudgetsRejectBeforePublication) {
    Fixture fixture;
    std::string error;
    auto accepted = fixture.admit(error);
    ASSERT_TRUE(accepted) << error;
    fixture.selection.maximumInputBytes = accepted->inputBytes() - 1;
    fixture.rejected("cumulative input capacity");
    fixture.selection.maximumInputBytes = 16 * 1024 * 1024;
    fixture.selection.maximumDecodedBytes = accepted->decodedBytes() - 1;
    fixture.rejected("decode input capacity");
    fixture.selection.maximumDecodedBytes = 16 * 1024 * 1024;
    fixture.selection.maximumGpuBytes = accepted->requestedGpuBytes() - 1;
    fixture.rejected("cumulative GPU capacity");
    EXPECT_EQ(accepted->lods().size(), 1u);
}

TEST(CookedPartBundle, InvalidSelectionRejectsBeforeAnyFileRead) {
    Fixture fixture;
    fixture.selection.manifestSha256 = std::string(64, 'A');
    fixture.rejected("selection identity");
    EXPECT_TRUE(fixture.reads.empty());
    fixture.trustManifest();
    fixture.selection.maximumInputBytes = UINT64_MAX;
    fixture.rejected("configured capacity");
    EXPECT_TRUE(fixture.reads.empty());
}

TEST(CookedPartBundle, MultipleLodsShareOnePartButKeepStableIdsAndAssetVersions) {
    Fixture fixture;
    auto lod = fixture.metadata["lods"][0];
    lod["id"] = "2";
    lod["asset"]["counter"] = "1234";
    lod["minimum_screen_height_pixels"] = 200;
    fixture.metadata["lods"].push_back(lod);
    auto entry = fixture.manifest["lods"][0];
    entry["id"] = "2"; entry["file"] = "lod-2.vmesh";
    fixture.manifest["lods"].push_back(entry);
    fixture.files["lod-2.vmesh"] = triangle();
    fixture.selection.lodRules.push_back({2, {}});
    fixture.replaceMetadata(fixture.metadata);
    std::string error;
    auto accepted = fixture.admit(error);
    ASSERT_TRUE(accepted) << error;
    ASSERT_EQ(accepted->lods().size(), 2u);
    EXPECT_EQ(accepted->lods()[0].id, 2u);
    EXPECT_EQ(accepted->lods()[0].asset.id.counter, 1234u);
    EXPECT_EQ(accepted->lods()[1].id, kLod);
    fixture.manifest["lods"][1] = fixture.manifest["lods"][0];
    fixture.trustManifest(); fixture.rejected("duplicate");
}
} // namespace
} // namespace voxy::game::assets

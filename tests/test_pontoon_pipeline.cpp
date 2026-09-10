#include "game/assets/fixture_registry.hpp"
#include "game/construction/build_model.hpp"
#include "core/sha256.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <fstream>
#include <map>

namespace {
using namespace voxy::game::construction;
using namespace voxy::game::assets;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr WorldNamespace world{{'p','o','n','t','o','o','n','-','f','i','t','-','t','e','s','t'}};
DurableId id(uint64_t counter) { return {world, counter}; }
Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }
std::string digest(const Bytes& value) { return voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(value)))); }
Bytes readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing pontoon runfile");
    const auto size = input.tellg();
    if (size <= 0 || size > 16 * 1024 * 1024) throw std::runtime_error("invalid pontoon runfile size");
    Bytes result(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("short pontoon runfile read");
    return result;
}

struct Package {
    CookedPartSelection selection;
    std::map<std::string, Bytes, std::less<>> files;
    Package() {
        const auto input = readFile("data/salvage/fixture-pontoon-v2.json");
        const std::string text(input.begin(), input.end());
        std::string error;
        const auto registry = parseAssetFixtureRegistry(text, error);
        if (!registry) throw std::runtime_error(error);
        const auto& spec = registry->bundles.front(); selection = spec.selection;
        // Declared immutable test runfiles, which Bazel may expose as links.
        // No-follow runtime admission is exercised separately by directory tests.
        for (const auto* name : {"cook-manifest.json", "gameplay.json", "lod-1.vmesh", "lod-2.vmesh", "lod-3.vmesh"}) {
            files[name] = readFile(std::filesystem::path("data/salvage") / spec.directory / name);
        }
    }
    std::unique_ptr<const CookedPartBundle> admit() const {
        std::string error;
        auto result = admitCookedPartBundle(selection,
            [&](std::string_view name, size_t maximum, std::string& reason) -> std::optional<Bytes> {
                const auto file = files.find(name);
                if (file == files.end() || file->second.size() > maximum) { reason = "missing or over bound"; return {}; }
                return file->second;
            }, error);
        if (!result) throw std::runtime_error(error);
        return result;
    }
};

PartCatalog catalog(const CookedPartBundle& bundle) {
    auto draft = makeStarterCatalogDraft();
    draft.definitions.push_back(bundle.sidecar().part);
    CatalogIssue issue;
    auto result = PartCatalog::create(draft, issue, [&](const CookedMeshVisual& visual) {
        for (const auto& lod : bundle.lods()) if (lod.asset == visual.asset) return true;
        return false;
    });
    if (!result) throw std::runtime_error(std::string(issue.field));
    return std::move(*result);
}
PartInstance part(uint64_t counter, ContentKey definition, GridPosition position, const PartCatalog& definitions) {
    PartInstance result;
    result.id = id(counter); result.owningBuild = id(1); result.definition = definition;
    result.placement.translation = position;
    result.settings = defaultModuleSettings(*definitions.lookup(definition).definition);
    return result;
}
Connection weld(uint64_t counter, uint64_t a, uint64_t socketA, uint64_t b, uint64_t socketB) {
    Connection result;
    result.id = id(counter); result.a = {id(a), SocketId{socketA}}; result.b = {id(b), SocketId{socketB}};
    result.strength = {1000,1000,1000,1000};
    return result;
}

TEST(PontoonPipeline, ActualMetadataFitsCrossbeamsAndStackInEveryProperOrientation) {
    const auto bundle = Package{}.admit(); const auto definitions = catalog(*bundle);
    const auto pontoon = bundle->sidecar().part.key;
    ASSERT_EQ(definitions.definitions().size(), 13u);
    ASSERT_TRUE(definitions.lookup(starterPartKey(StarterPart::Pontoon)));
    EXPECT_EQ(definitions.lookup(starterPartKey(StarterPart::Pontoon)).definition->key.version, 1u);
    EXPECT_EQ(pontoon.version, 2u);
    BuildSnapshot skiff; skiff.id = id(1); skiff.owner = id(2);
    skiff.parts = {part(10, pontoon, {-75,0,0}, definitions), part(11, pontoon, {75,0,0}, definitions),
        part(12, starterPartKey(StarterPart::Beam), {0,48,-50}, definitions),
        part(13, starterPartKey(StarterPart::Beam), {0,48,50}, definitions)};
    skiff.connections = {weld(100,10,101,12,101), weld(101,11,101,12,107),
        weld(102,10,103,13,101), weld(103,11,103,13,107)};
    BuildSnapshot stack; stack.id = id(1); stack.owner = id(2);
    stack.parts = {part(10,pontoon,{},definitions),part(11,pontoon,{0,48,0},definitions)};
    stack.connections = {weld(100,10,1,11,2)};
    for (const auto& source : {skiff, stack}) for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        SCOPED_TRACE(static_cast<int>(rotation));
        auto draft = source;
        const GridTransform root{{-12801,-29,12799},{rotation}};
        for (auto& instance : draft.parts) instance.placement = *compose(root, instance.placement);
        BuildIssue issue;
        ASSERT_TRUE(BuildModel::create(draft, definitions, issue)) << static_cast<int>(issue.error) << ' ' << issue.field;
        draft.parts.back().placement.translation.x += 1;
        EXPECT_FALSE(BuildModel::create(draft, definitions, issue));
    }
    stack.parts[1].placement.translation.y = 57; // Loose height is not engaged height.
    BuildIssue issue;
    EXPECT_FALSE(BuildModel::create(stack, definitions, issue));
    EXPECT_EQ(issue.error, BuildError::MisalignedWeld);
    stack.parts[1].placement.translation.y = 48;
    stack.parts[1].placement.rotation = {21}; // Upright, wrong key/long axis.
    EXPECT_FALSE(BuildModel::create(stack, definitions, issue));
    EXPECT_EQ(issue.error, BuildError::MisalignedWeld);
}

TEST(PontoonPipeline, EveryActualLodUsesOneBasisAndSurvivesAdjacentSectorRebasing) {
    const auto bundle = Package{}.admit();
    for (const auto& lod : bundle->lods()) {
        ASSERT_EQ(lod.prefab.meshNodes.size(), 1u);
        ASSERT_EQ(lod.prefab.renderToCanonical.value, 12);
        EXPECT_NEAR(lod.prefab.canonicalBounds.minimum.z, -2, .001);
        EXPECT_NEAR(lod.prefab.canonicalBounds.maximum.z, 2, .001);
        EXPECT_NEAR(lod.prefab.canonicalBounds.maximum.y, .66, .001);
        EXPECT_NEAR(lod.prefab.canonicalBounds.minimum.y, -.48, .001);
        for (uint8_t rotation = 0; rotation < 24; ++rotation) {
            const GridTransform placement{{-75,48,-50},{rotation}};
            const auto axes = *rotationMatrix(placement.rotation);
            std::vector<RigidPrefabDraw> before, after;
            std::string error;
            // The application subtracts sectors in double precision before
            // float matrix conversion, even far from the original region.
            const glm::dvec3 sector(1000000000.0,-1000000000.0,-1.0);
            const glm::dvec3 rootWorld = sector * 256.0 + glm::dvec3(255.875,10,-2);
            const auto rootA = glm::translate(glm::dmat4(1), rootWorld - sector * 256.0);
            const auto rootB = glm::translate(glm::dmat4(1), rootWorld - (sector + glm::dvec3(1,0,0)) * 256.0);
            ASSERT_TRUE(placeRigidPrefab(lod.prefab, rootA, placement, 1, before, error)) << error;
            ASSERT_TRUE(placeRigidPrefab(lod.prefab, rootB, placement, 1, after, error)) << error;
            ASSERT_EQ(before.size(), 1u); ASSERT_EQ(after.size(), 1u);
            for (uint32_t index = 0; index < lod.mesh.header.vertexCount; ++index) {
                voxy::moto::VmeshVertex vertex;
                std::memcpy(&vertex, lod.mesh.vertices.data() + static_cast<size_t>(index) * sizeof(vertex), sizeof(vertex));
                const glm::vec4 source(vertex.position[0],vertex.position[1],vertex.position[2],1);
                // The authoring/export contract's explicit 180-degree Y bridge.
                const std::array<double,3> canonical{-static_cast<double>(source.x),static_cast<double>(source.y),-static_cast<double>(source.z)};
                glm::dvec3 expected = glm::dvec3(255.875,10,-2) + glm::dvec3(-1.5,.96,-1);
                for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 3; ++c)
                    expected[static_cast<glm::length_t>(r)] += static_cast<double>(axes.elements[r*3+c]) * canonical[c];
                const glm::dvec3 actual(before[0].modelMatrix * source);
                const glm::dvec3 rebased(after[0].modelMatrix * source);
                EXPECT_LT(glm::length(actual - expected), .0001);
                EXPECT_LT(glm::length(actual - (rebased + glm::dvec3(256,0,0))), .0001);
            }
        }
    }
}

TEST(PontoonPipeline, ScreenThresholdOrderDoesNotDependOnStableIdSortOrder) {
    Package fixture;
    const auto json = [&](const char* name) {
        const auto& file = fixture.files.at(name);
        return Json::parse(std::string(file.begin(), file.end()));
    };
    auto metadata = json("gameplay.json");
    auto manifest = json("cook-manifest.json");
    const std::array<uint64_t,3> ids{91,7,42};
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto oldName = cookedLodFilename(i + 1);
        const auto newName = cookedLodFilename(ids[i]);
        fixture.files[newName] = std::move(fixture.files.at(oldName)); fixture.files.erase(oldName);
        metadata["lods"][i]["id"] = u64ToDecimal(ids[i]);
        manifest["lods"][i]["id"] = u64ToDecimal(ids[i]); manifest["lods"][i]["file"] = newName;
        fixture.selection.lodRules[i].id = ids[i];
    }
    std::string error;
    const auto normalized = parseGameplaySidecar(metadata.dump(), [](const auto&) {return true;}, error);
    ASSERT_TRUE(normalized) << error;
    fixture.files["gameplay.json"] = bytes(normalized->normalizedJson);
    manifest["normalized_sidecar"]["bytes"] = fixture.files.at("gameplay.json").size();
    manifest["normalized_sidecar"]["sha256"] = digest(fixture.files.at("gameplay.json"));
    fixture.files["cook-manifest.json"] = bytes(manifest.dump(2) + '\n');
    fixture.selection.manifestSha256 = digest(fixture.files.at("cook-manifest.json"));
    const auto bundle = fixture.admit();
    ASSERT_EQ(bundle->lods().front().id, 7u);
    const auto projection = glm::perspective(glm::radians(60.0),1.0,.1,1000.0);
    const auto view = [](double distance) {return glm::lookAt(glm::dvec3(0,.1,-distance),glm::dvec3(0,.1,0),glm::dvec3(0,1,0));};
    EXPECT_EQ(selectFixtureLod(*bundle,glm::dmat4(1),{},projection * view(3),1080),91u);
    EXPECT_EQ(selectFixtureLod(*bundle,glm::dmat4(1),{},projection * view(10),1080),7u);
    EXPECT_EQ(selectFixtureLod(*bundle,glm::dmat4(1),{},projection * view(10),360),42u);
    EXPECT_EQ(selectFixtureLod(*bundle,glm::dmat4(1),{},projection * view(1),1080),91u);
}
} // namespace

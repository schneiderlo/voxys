#include "salvage_assets/gameplay_sidecar.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>
#include <sstream>

namespace {
using namespace voxy::tools::salvage;
using namespace voxy::game::construction;
using Json = nlohmann::json;
Json fixture() {
    std::ifstream file("tools/salvage_assets/fixtures/probe.gameplay.json");
    Json result; file >> result; return result;
}
std::optional<GameplaySidecar> parse(const Json& value, std::string& error) {
    return parseGameplaySidecar(value.dump(), [](const CookedMeshVisual& visual) {
        return visual.path.starts_with("lod-") && visual.path.ends_with(".vmesh");
    }, error);
}
void rejected(const Json& value) { std::string error; EXPECT_FALSE(parse(value, error)); EXPECT_FALSE(error.empty()); }

TEST(GameplaySidecar, SharedCatalogAcceptsTypedAsymmetricPartAndLosslessIds) {
    std::string error;
    auto result = parse(fixture(), error);
    ASSERT_TRUE(result) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(result->part.key.id.counter, 9007199254740993ull);
    EXPECT_EQ(result->part.sockets.front().id.value(), 9007199254740995ull);
    EXPECT_EQ(result->lods.front().id, 9007199254740997ull);
    EXPECT_EQ(result->anchors.front().id, 9007199254741001ull);
    EXPECT_EQ(result->part.collision.front().id.value(), 73u);
    EXPECT_EQ(result->part.buoyancy.front().box.id.value(), 103u);
    EXPECT_DOUBLE_EQ(result->part.mass.dryMassKg, 10.0);
    EXPECT_DOUBLE_EQ(result->part.mass.localCenterOfMass.y, .32);
    EXPECT_TRUE(std::holds_alternative<StructureModule>(result->part.module));
    EXPECT_EQ(result->part.sockets.front().frame.translation.y, 32);
}

TEST(GameplaySidecar, NormalizedMetadataIsIdempotentAndRetainsEveryField) {
    auto original = fixture();
    std::string error;
    auto first = parse(original, error); ASSERT_TRUE(first) << error;
    auto normalized = Json::parse(first->normalizedJson);
    // JSON number representation can canonicalize; every field/value survives.
    EXPECT_EQ(normalized, original);
    auto second = parse(normalized, error); ASSERT_TRUE(second) << error;
    EXPECT_EQ(first->normalizedJson, second->normalizedJson);
    EXPECT_EQ(first->part.key, second->part.key);
}

TEST(GameplaySidecar, IdSortingIsNumericAndDoesNotChangeLodMeaning) {
    auto data = fixture();
    auto socket = data["part"]["sockets"][0]; socket["id"] = "9";
    socket["frame"]["translation_ticks"] = Json::array({10, 32, 0});
    data["part"]["sockets"].push_back(socket);
    auto anchor = data["tool_anchors"][0]; anchor["id"] = "10"; anchor["name_key"] = "probe.other";
    data["tool_anchors"].push_back(anchor);
    auto lod = data["lods"][0]; lod["id"] = "2"; lod["asset"]["counter"] = "2";
    lod["minimum_screen_height_pixels"] = 80;
    data["lods"].push_back(lod);
    std::string error;
    auto first = parse(data, error); ASSERT_TRUE(first) << error;
    std::reverse(data["lods"].begin(), data["lods"].end());
    std::reverse(data["tool_anchors"].begin(), data["tool_anchors"].end());
    std::reverse(data["part"]["sockets"].begin(), data["part"]["sockets"].end());
    auto second = parse(data, error); ASSERT_TRUE(second) << error;
    EXPECT_EQ(first->normalizedJson, second->normalizedJson);
    EXPECT_EQ(first->lods[0].id, 2u);
    EXPECT_DOUBLE_EQ(first->part.visuals[0].minimumScreenHeightPixels, 80.0);
    EXPECT_EQ(first->part.sockets[0].id.value(), 9u);
}

TEST(GameplaySidecar, ExportedGltfBasisAppliesExactlyOnceAndLeavesMetadataCanonical) {
    std::string error; auto result = parse(fixture(), error); ASSERT_TRUE(result);
    const auto forward = renderPointToCanonical(result->lods[0], {0.0, .64, 1.8});
    ASSERT_TRUE(forward); EXPECT_DOUBLE_EQ(forward->z, -1.8); EXPECT_DOUBLE_EQ(forward->y, .64);
    const auto right = renderPointToCanonical(result->lods[0], {-.8, .2, .3});
    ASSERT_TRUE(right); EXPECT_DOUBLE_EQ(right->x, .8); EXPECT_DOUBLE_EQ(right->z, -.3);
    const auto anchor = toMetres(result->anchors[0].frame.translation);
    ASSERT_TRUE(anchor); EXPECT_DOUBLE_EQ(anchor->z, -1.8);
    auto bad = result->lods[0]; bad.renderToCanonical = CubeRotation{24};
    EXPECT_FALSE(renderPointToCanonical(bad, {1, 2, 3}));
}

TEST(GameplaySidecar, SharedPhysicalRulesRejectMassInertiaVolumesAndModuleErrors) {
    auto data = fixture(); data["part"]["mass"]["dry_mass_kg"] = -1; rejected(data);
    data = fixture(); data["part"]["mass"]["inertia_kg_metres_squared"][0] = 1000000; rejected(data);
    data = fixture(); data["part"]["mass"]["center_of_mass_metres"][0] = 500; rejected(data);
    data = fixture(); auto region = data["part"]["buoyancy"][0]; region["box"]["id"] = "104";
    data["part"]["buoyancy"].push_back(region); rejected(data);
    data = fixture(); data["part"]["collision"][0]["half_extents_ticks"][0] = 1000; rejected(data);
    data = fixture(); data["part"]["module"] = {{"type", "engine"}, {"shaft", "42"},
        {"maximum_power_watts", 1000}, {"maximum_torque_newton_metres", 100}}; rejected(data);
    data = fixture(); data["part"]["salvage_yield"]["salvage_material"] = "21"; rejected(data);
}

TEST(GameplaySidecar, UnknownFieldsAndMalformedUnitsCannotDisappearDuringCook) {
    auto data = fixture(); data["socket_names"] = Json::array(); rejected(data);
    data = fixture(); data["part"]["mass"]["density"] = 12; rejected(data);
    data = fixture(); data["schema"] = true; rejected(data);
    data = fixture(); data["schema"] = 2; rejected(data);
    data = fixture(); data["units"]["length"] = "centimetres"; rejected(data);
    data = fixture(); data["metadata_frame"] = "raw_blender"; rejected(data);
    data = fixture(); data["placement_lattice_metres"] = .32; rejected(data);
    data = fixture(); data["part"]["module"]["type"] = "hinge"; rejected(data);
}

TEST(GameplaySidecar, NoncanonicalIdsFramesAndUnsafeSourceReferencesReject) {
    for (const Json& id : {Json("01"), Json("18446744073709551616"), Json(9007199254740993ull), Json("0")}) {
        auto data = fixture(); data["lods"][0]["id"] = id; rejected(data);
    }
    auto data = fixture(); data["part"]["sockets"][0]["frame"]["rotation"] = 24; rejected(data);
    data = fixture(); data["part"]["footprint"]["minimum_ticks"][0] = -2147483648ll; rejected(data);
    data = fixture(); data["lods"][0]["source"]["file"] = "../probe.glb"; rejected(data);
    data = fixture(); data["lods"][0]["source"]["sha256"] = std::string(64, 'X'); rejected(data);
    data = fixture(); data["lods"][0]["source"]["frame"] = "canonical"; rejected(data);
    data = fixture(); data["lods"][0]["source"]["bytes"] = kMaximumSourceGlbBytes + 1; rejected(data);
    data = fixture(); data["tool_anchors"][0]["frame"]["translation_ticks"][0] = 1000; rejected(data);
}

TEST(GameplaySidecar, DuplicatedDurableIdsAnchorsAndLodThresholdsReject) {
    auto data = fixture(); data["part"]["sockets"].push_back(data["part"]["sockets"][0]); rejected(data);
    data = fixture(); data["tool_anchors"].push_back(data["tool_anchors"][0]); rejected(data);
    data = fixture(); auto lod = data["lods"][0]; lod["id"] = "8"; lod["asset"]["counter"] = "8";
    data["lods"].push_back(lod); rejected(data); // Equal thresholds.
    data = fixture(); data["lods"][0]["minimum_screen_height_pixels"] = 1; rejected(data); // No final zero LOD.
}

TEST(GameplaySidecar, CountsBytesDepthDuplicateKeysAndUnavailableAssetsAreBounded) {
    auto data = fixture(); const auto socket = data["part"]["sockets"][0];
    for (size_t i = 0; i < kMaximumPartSockets; ++i) data["part"]["sockets"].push_back(socket);
    rejected(data);
    data = fixture(); const auto anchor = data["tool_anchors"][0];
    for (size_t i = 0; i < kMaximumSidecarAnchors; ++i) data["tool_anchors"].push_back(anchor);
    rejected(data);
    std::string error;
    EXPECT_FALSE(parseGameplaySidecar(std::string(kMaximumSidecarBytes + 1, ' '), {}, error));
    const auto duplicate = std::string("{\"schema\":1,") + fixture().dump().substr(1);
    EXPECT_FALSE(parseGameplaySidecar(duplicate, {}, error)); EXPECT_NE(error.find("duplicate"), std::string::npos);
    const auto nested = std::string(40, '[') + "0" + std::string(40, ']');
    EXPECT_FALSE(parseGameplaySidecar(nested, {}, error));
    EXPECT_FALSE(parseGameplaySidecar(fixture().dump(), [](const CookedMeshVisual&) { return false; }, error));
    EXPECT_NE(error.find("PartCatalog"), std::string::npos);
}
} // namespace

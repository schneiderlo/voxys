#include "game/construction/part_catalog.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace voxy::game::construction {
namespace {

static_assert(std::is_const_v<decltype(std::declval<const PartCatalog&>().definitions())::element_type>);
static_assert(!std::is_convertible_v<SocketId, ProxyId>);
static_assert(!std::is_default_constructible_v<PartCatalog>);

PartDefinition& part(PartCatalogDraft& draft, StarterPart id) {
    const auto key = starterPartKey(id);
    return *std::find_if(draft.definitions.begin(), draft.definitions.end(), [key](const auto& value) { return value.key == key; });
}

CatalogIssue rejected(const PartCatalogDraft& draft) {
    CatalogIssue issue{};
    EXPECT_FALSE(PartCatalog::create(draft, issue));
    return issue;
}

void expectRejected(const PartCatalogDraft& draft, CatalogError error, std::string_view field) {
    const auto issue = rejected(draft);
    EXPECT_EQ(issue.error, error);
    EXPECT_EQ(issue.field, field);
}

TEST(PartCatalog, TwelveStarterDefinitionsLoadWithoutRendererOrAssetCallback) {
    const auto draft = makeStarterCatalogDraft();
    CatalogIssue issue{};
    const auto catalog = PartCatalog::create(draft, issue);
    ASSERT_TRUE(catalog);
    EXPECT_EQ(issue.error, CatalogError::None);
    ASSERT_EQ(catalog->definitions().size(), 12u);
    for (uint8_t id = 1; id <= 12; ++id) {
        const auto found = catalog->lookup(starterPartKey(static_cast<StarterPart>(id)));
        ASSERT_TRUE(found);
        const auto& definition = *found.definition;
        EXPECT_EQ(definition.key.version, 1u);
        EXPECT_GT(definition.mass.dryMassKg, 0.0);
        EXPECT_TRUE(physicallyValidInertia(definition.mass.inertia));
        EXPECT_FALSE(definition.collision.empty());
        EXPECT_FALSE(definition.solidOccupancy.empty());
        EXPECT_FALSE(definition.buoyancy.empty());
        ASSERT_EQ(definition.visuals.size(), 1u);
        const auto* visual = std::get_if<PrototypeBoxVisual>(&definition.visuals[0].asset);
        ASSERT_NE(visual, nullptr);
        EXPECT_TRUE(prototypeAssetExists(*visual));
        EXPECT_EQ(visual->bounds, definition.footprint);
    }
    EXPECT_FALSE(isValid(starterPartKey(static_cast<StarterPart>(13)).id));
    EXPECT_TRUE(std::holds_alternative<FlotationModule>(catalog->lookup(starterPartKey(StarterPart::Pontoon)).definition->module));
    EXPECT_TRUE(std::holds_alternative<EngineModule>(catalog->lookup(starterPartKey(StarterPart::Engine)).definition->module));
    EXPECT_TRUE(std::holds_alternative<RepairModule>(catalog->lookup(starterPartKey(StarterPart::RepairModule)).definition->module));
    const auto& pontoon = *catalog->lookup(starterPartKey(StarterPart::Pontoon)).definition;
    EXPECT_NEAR(*boxVolumeCubicMetres(pontoon.buoyancy[0].box), 3.84, 1.0e-12);
}

TEST(PartCatalog, CatalogOwnsItsDataAndSortsStableIdentifiers) {
    auto draft = makeStarterCatalogDraft();
    std::reverse(draft.definitions.begin(), draft.definitions.end());
    for (auto& definition : draft.definitions) {
        std::reverse(definition.sockets.begin(), definition.sockets.end());
    }
    CatalogIssue issue{};
    const auto catalog = PartCatalog::create(draft, issue);
    ASSERT_TRUE(catalog);
    const auto key = starterPartKey(StarterPart::Beam);
    const auto* beam = catalog->lookup(key).definition;
    ASSERT_NE(beam, nullptr);
    const auto name = beam->nameKey;
    const auto mass = beam->mass.dryMassKg;
    part(draft, StarterPart::Beam).mass.dryMassKg = 10000.0;
    part(draft, StarterPart::Beam).nameKey = "changed";
    part(draft, StarterPart::Beam).sockets.clear();
    EXPECT_EQ(beam->nameKey, name);
    EXPECT_EQ(beam->mass.dryMassKg, mass);
    EXPECT_FALSE(beam->sockets.empty());
    EXPECT_TRUE(std::is_sorted(beam->sockets.begin(), beam->sockets.end(), [](const auto& a, const auto& b) { return a.id < b.id; }));
    EXPECT_TRUE(std::is_sorted(catalog->definitions().begin(), catalog->definitions().end(),
                               [](const auto& a, const auto& b) { return a.key < b.key; }));
}

TEST(PartCatalog, SchemaIdentityDuplicatesAndVersionedLookupAreExplicit) {
    auto draft = makeStarterCatalogDraft();
    draft.schemaVersion = 2;
    expectRejected(draft, CatalogError::UnsupportedSchema, "schemaVersion");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].key.id.counter = 0;
    expectRejected(draft, CatalogError::InvalidDefinitionId, "key.id");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].key.version = 0;
    expectRejected(draft, CatalogError::InvalidVersion, "key.version");
    draft = makeStarterCatalogDraft();
    draft.definitions.push_back(draft.definitions[0]);
    auto issue = rejected(draft);
    EXPECT_EQ(issue.error, CatalogError::DuplicateDefinition);
    EXPECT_EQ(issue.definitionIndex, 12u);
    draft.definitions.back().key.version = 2;
    const auto catalog = PartCatalog::create(draft, issue);
    ASSERT_TRUE(catalog);
    auto key = starterPartKey(StarterPart::Beam);
    EXPECT_TRUE(catalog->lookup(key));
    key.version = 2;
    EXPECT_TRUE(catalog->lookup(key));
    key.version = 3;
    EXPECT_EQ(catalog->lookup(key).error, CatalogError::UnknownVersion);
    key.id.counter = 99;
    EXPECT_EQ(catalog->lookup(key).error, CatalogError::UnknownDefinition);
}

TEST(PartCatalog, MassAndCenterOfMassRejectNonfiniteAndUnsupportedValues) {
    for (double mass : {0.0, -1.0, 1.0e-100, 1.0e7, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        auto draft = makeStarterCatalogDraft();
        draft.definitions[0].mass.dryMassKg = mass;
        expectRejected(draft, CatalogError::InvalidMass, "mass.dryMassKg");
    }
    for (double y : {-100.0, 100.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto draft = makeStarterCatalogDraft();
        draft.definitions[0].mass.localCenterOfMass.y = y;
        expectRejected(draft, CatalogError::InvalidCenterOfMass, "mass.localCenterOfMass");
    }
}

TEST(PartCatalog, FullTensorValidationEnforcesPhysicalPrincipalMoments) {
    // I = trace(S) Identity - S for a positive second-moment matrix S.
    const InertiaTensor physical{{7.0, -0.2, -0.3, -0.2, 6.0, -0.4, -0.3, -0.4, 5.0}};
    EXPECT_TRUE(physicallyValidInertia(physical));
    EXPECT_TRUE(physicallyValidInertia({{5.0, -1.0, 0.0, -1.0, 5.0, 0.0, 0.0, 0.0, 2.0}}));
    // SPD alone is insufficient: 4 > 1 + 1 violates an inertia triangle.
    EXPECT_FALSE(physicallyValidInertia({{4.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}}));
    EXPECT_FALSE(physicallyValidInertia({{1.0, 0.99, 0.0, 0.99, 1.0, 0.0, 0.0, 0.0, 1.5}}));
    EXPECT_FALSE(physicallyValidInertia({}));
    auto asymmetric = physical;
    asymmetric.elements[1] = 0.1;
    EXPECT_FALSE(physicallyValidInertia(asymmetric));
    for (double bad : {-1.0, 0.0, 1.0e-30, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto draft = makeStarterCatalogDraft();
        draft.definitions[0].mass.inertia = {{bad, 0.0, 0.0, 0.0, bad, 0.0, 0.0, 0.0, bad}};
        expectRejected(draft, CatalogError::InvalidInertia, "mass.inertia");
    }
    auto draft = makeStarterCatalogDraft();
    draft.definitions[0].mass.inertia = physical;
    CatalogIssue issue{};
    EXPECT_TRUE(PartCatalog::create(draft, issue));
    draft = makeStarterCatalogDraft();
    draft.definitions[0].mass.dryMassKg = 1.0;
    expectRejected(draft, CatalogError::InvalidInertia, "mass.inertia");
}

TEST(PartCatalog, AnalyticalBoxesMatchIndependentCornerBoundsForAllRotations) {
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        const PartBox box{ProxyId{1}, {{17, -23, 5}, {rotation}}, {7, 11, 13}};
        auto low = GridPosition{5000, 5000, 5000};
        auto high = GridPosition{-5000, -5000, -5000};
        for (int32_t x : {-7, 7}) {
            for (int32_t y : {-11, 11}) {
                for (int32_t z : {-13, 13}) {
                    const auto corner = *transformPosition(box.frame, {x, y, z});
                    low = {std::min(low.x, corner.x), std::min(low.y, corner.y), std::min(low.z, corner.z)};
                    high = {std::max(high.x, corner.x), std::max(high.y, corner.y), std::max(high.z, corner.z)};
                }
            }
        }
        EXPECT_EQ(boxBounds(box), (GridBox{low, high}));
        EXPECT_NEAR(*boxVolumeCubicMetres(box), 0.28 * 0.44 * 0.52, 1.0e-14);
    }
    EXPECT_FALSE(boxBounds({ProxyId{1}, {}, {0, 1, 1}}));
    EXPECT_FALSE(boxBounds({ProxyId{1}, {}, {-1, 1, 1}}));
    EXPECT_FALSE(boxBounds({ProxyId{1}, {{kMaximumGridCoordinate, 0, 0}, {}}, {1, 1, 1}}));
    EXPECT_FALSE(boxBounds({ProxyId{1}, {{}, {24}}, {1, 1, 1}}));
}

TEST(PartCatalog, CollisionOccupancyAndRotationFailuresIdentifyTheirProperty) {
    auto draft = makeStarterCatalogDraft();
    draft.definitions[0].collision.clear();
    expectRejected(draft, CatalogError::MissingCollision, "collision");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].solidOccupancy.clear();
    expectRejected(draft, CatalogError::InvalidBox, "solidOccupancy");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].collision.push_back(draft.definitions[0].collision[0]);
    const auto issue = rejected(draft);
    EXPECT_EQ(issue.error, CatalogError::DuplicateProxyId);
    EXPECT_EQ(issue.elementId, 1u);
    draft = makeStarterCatalogDraft();
    draft.definitions[0].collision[0].frame.rotation = CubeRotation{4};
    expectRejected(draft, CatalogError::InvalidBox, "collision");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].footprint.maximum.x = draft.definitions[0].footprint.minimum.x;
    expectRejected(draft, CatalogError::InvalidFootprint, "footprint");
    for (uint32_t mask : {0u, 0x01000000u, 0xffffffffu}) {
        draft = makeStarterCatalogDraft();
        draft.definitions[0].permittedRotationMask = mask;
        expectRejected(draft, CatalogError::InvalidRotationMask, "permittedRotationMask");
    }
}

TEST(PartCatalog, BuoyancyRejectsDoubleVolumeButAllowsTouchingCompartments) {
    auto draft = makeStarterCatalogDraft();
    auto& pontoon = part(draft, StarterPart::Pontoon);
    auto duplicate = pontoon.buoyancy[0];
    duplicate.box.id = ProxyId{2};
    pontoon.buoyancy.push_back(duplicate);
    expectRejected(draft, CatalogError::OverlappingBuoyancy, "buoyancy");
    pontoon.buoyancy[0].box.halfExtents.x = 50;
    pontoon.buoyancy[0].box.frame.translation.x = -50;
    pontoon.buoyancy[1].box.halfExtents.x = 50;
    pontoon.buoyancy[1].box.frame.translation.x = 50;
    CatalogIssue issue{};
    EXPECT_TRUE(PartCatalog::create(draft, issue));
    EXPECT_NEAR(*boxVolumeCubicMetres(pontoon.buoyancy[0].box) + *boxVolumeCubicMetres(pontoon.buoyancy[1].box), 3.84, 1.0e-12);
    pontoon.buoyancy[1].box.id = ProxyId{1};
    expectRejected(draft, CatalogError::DuplicateProxyId, "buoyancy");
    pontoon.buoyancy.clear();
    expectRejected(draft, CatalogError::InvalidModule, "module");
}

TEST(PartCatalog, TypedSocketCompatibilitySeparatesWeldRopeAndCargoLatch) {
    auto draft = makeStarterCatalogDraft();
    const auto& engine = *findSocket(part(draft, StarterPart::Engine), SocketId{10});
    const auto& propeller = *findSocket(part(draft, StarterPart::Propeller), SocketId{10});
    EXPECT_EQ(matchSockets(engine, propeller, ConnectionKind::Weld), SocketMatchError::None);
    EXPECT_EQ(matchSockets(propeller, engine, ConnectionKind::Weld), SocketMatchError::None);
    EXPECT_EQ(matchSockets(engine, propeller, ConnectionKind::Rope), SocketMatchError::ConnectionKindMismatch);
    const auto& winch = *findSocket(part(draft, StarterPart::Winch), SocketId{10});
    const auto& eye = *findSocket(part(draft, StarterPart::TowEye), SocketId{10});
    EXPECT_EQ(matchSockets(winch, eye, ConnectionKind::Rope), SocketMatchError::None);
    EXPECT_EQ(matchSockets(engine, eye, ConnectionKind::Weld), SocketMatchError::FamilyMismatch);
    auto wrongProfile = eye;
    wrongProfile.profile = 2;
    EXPECT_EQ(matchSockets(winch, wrongProfile, ConnectionKind::Rope), SocketMatchError::ProfileMismatch);
    EXPECT_EQ(matchSockets(winch, winch, ConnectionKind::Rope), SocketMatchError::RoleMismatch);
    const auto& cradle = *findSocket(part(draft, StarterPart::CargoCradle), SocketId{10});
    auto cargoAnchor = cradle;
    cargoAnchor.role = SocketRole::Plug;
    EXPECT_EQ(matchSockets(cradle, cargoAnchor, ConnectionKind::Latch), SocketMatchError::None);
    cargoAnchor.profile = 0;
    EXPECT_EQ(matchSockets(cradle, cargoAnchor, ConnectionKind::Latch), SocketMatchError::InvalidSocket);
}

TEST(PartCatalog, EngagedSocketPitchAndNormalsSurviveSidewaysAndUpsideDownPlacements) {
    auto draft = makeStarterCatalogDraft();
    for (const auto id : {StarterPart::Beam, StarterPart::Plate}) {
        const auto& definition = part(draft, id);
        const auto& top = *findSocket(definition, SocketId{1});
        const auto& bottom = *findSocket(definition, SocketId{2});
        EXPECT_EQ(matchSockets(top, bottom, ConnectionKind::Weld), SocketMatchError::None);
        const int32_t pitch = id == StarterPart::Beam ? kBrickBodyTicks : kPlateTicks;
        EXPECT_EQ(top.frame.translation.y - bottom.frame.translation.y, pitch);
        for (uint8_t r = 0; r < 24; ++r) {
            const GridTransform lower{{-50, 75, 16}, {r}};
            const auto upper = *compose(lower, GridTransform{{0, pitch, 0}, {}});
            const auto topWorld = *compose(lower, top.frame);
            const auto bottomWorld = *compose(upper, bottom.frame);
            EXPECT_EQ(topWorld.translation, bottomWorld.translation);
            const auto topNormal = *rotate(topWorld.rotation, {0, 1, 0});
            const auto bottomNormal = *rotate(bottomWorld.rotation, {0, 1, 0});
            EXPECT_EQ(topNormal.x * bottomNormal.x + topNormal.y * bottomNormal.y + topNormal.z * bottomNormal.z, -1);
            // The mating insertion volume is shared, not a 0.18 m stack gap.
            EXPECT_EQ(transformPosition(topWorld, {0, 9, 0}), transformPosition(bottomWorld, {0, -9, 0}));
        }
    }
}

TEST(PartCatalog, SocketAuthoringErrorsArePrecise) {
    auto draft = makeStarterCatalogDraft();
    draft.definitions[0].sockets.push_back(draft.definitions[0].sockets[0]);
    expectRejected(draft, CatalogError::DuplicateSocketId, "sockets");
    const auto mutate = [](auto change) {
        auto candidate = makeStarterCatalogDraft();
        change(candidate.definitions[0].sockets[0]);
        expectRejected(candidate, CatalogError::InvalidSocket, "sockets");
    };
    mutate([](auto& socket) { socket.id = SocketId{}; });
    mutate([](auto& socket) { socket.frame.rotation = CubeRotation{24}; });
    mutate([](auto& socket) { socket.frame.translation.x = 1000; });
    mutate([](auto& socket) { socket.connectionCapacity = 0; });
    mutate([](auto& socket) { socket.clearance.maximum = socket.clearance.minimum; });
    mutate([](auto& socket) { socket.strength.tensionNewtons = std::numeric_limits<double>::quiet_NaN(); });
    mutate([](auto& socket) { socket.family = static_cast<SocketFamily>(255); });
}

TEST(PartCatalog, ModuleParametersAndRequiredSocketRolesCannotBeIgnored) {
    auto draft = makeStarterCatalogDraft();
    std::get<EngineModule>(part(draft, StarterPart::Engine).module).shaft = SocketId{999};
    expectRejected(draft, CatalogError::MissingModuleSocket, "module");
    std::get<EngineModule>(part(draft, StarterPart::Engine).module).shaft = SocketId{1};
    expectRejected(draft, CatalogError::IncompatibleModuleSocket, "module");
    draft = makeStarterCatalogDraft();
    std::get<WinchModule>(part(draft, StarterPart::Winch).module).line = SocketId{999};
    expectRejected(draft, CatalogError::MissingModuleSocket, "module");
    std::get<WinchModule>(part(draft, StarterPart::Winch).module).line = SocketId{1};
    expectRejected(draft, CatalogError::IncompatibleModuleSocket, "module");
    draft = makeStarterCatalogDraft();
    bool changedLine = false;
    for (auto& socket : part(draft, StarterPart::Winch).sockets) {
        if (socket.id == SocketId{10}) {
            socket.role = SocketRole::Receptacle;
            changedLine = true;
        }
    }
    ASSERT_TRUE(changedLine);
    expectRejected(draft, CatalogError::IncompatibleModuleSocket, "module");
    draft = makeStarterCatalogDraft();
    changedLine = false;
    for (auto& socket : part(draft, StarterPart::Winch).sockets) {
        if (socket.id == SocketId{10}) {
            socket.strength.tensionNewtons = 11999.0; // Winch is rated for 12000 N.
            changedLine = true;
        }
    }
    ASSERT_TRUE(changedLine);
    expectRejected(draft, CatalogError::InvalidModule, "module");
    const auto mutate = [](auto change) {
        auto candidate = makeStarterCatalogDraft();
        change(candidate);
        expectRejected(candidate, CatalogError::InvalidModule, "module");
    };
    mutate([](auto& d) { std::get<EngineModule>(part(d, StarterPart::Engine).module).maximumPowerWatts = -1; });
    mutate([](auto& d) { std::get<PropellerModule>(part(d, StarterPart::Propeller).module).forceFrame.rotation = CubeRotation{24}; });
    mutate([](auto& d) { std::get<HelmModule>(part(d, StarterPart::Helm).module).maximumSteeringRadians = 100; });
    mutate([](auto& d) { std::get<WinchModule>(part(d, StarterPart::Winch).module).reelSpeedMetresPerSecond = std::numeric_limits<double>::quiet_NaN(); });
    mutate([](auto& d) { std::get<WinchModule>(part(d, StarterPart::Winch).module).maximumForceNewtons = 30001; });
    mutate([](auto& d) { std::get<FlotationModule>(part(d, StarterPart::Pontoon).module).dragCoefficients[0] = -1; });
    mutate([](auto& d) { std::get<CargoCradleModule>(part(d, StarterPart::CargoCradle).module).captureDistanceMetres = 1; });
    mutate([](auto& d) { std::get<BraceModule>(part(d, StarterPart::Brace).module).loadTransferFactor = 5; });
    mutate([](auto& d) { std::get<RepairModule>(part(d, StarterPart::RepairModule).module).materialUnitsPerFullHealth = 0; });
}

TEST(PartCatalog, ResourcesMaterialsAndBoundsRejectMalformedAuthoring) {
    auto draft = makeStarterCatalogDraft();
    draft.definitions[0].cost = {};
    expectRejected(draft, CatalogError::InvalidCost, "cost");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].cost.salvageMaterial = std::numeric_limits<uint64_t>::max();
    expectRejected(draft, CatalogError::InvalidCost, "cost");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].salvageYield.salvageMaterial = draft.definitions[0].cost.salvageMaterial + 1;
    expectRejected(draft, CatalogError::InvalidSalvageYield, "salvageYield");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].material.linearBaseColor[1] = std::numeric_limits<double>::quiet_NaN();
    expectRejected(draft, CatalogError::InvalidMaterial, "material.linearBaseColor");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].material.roughness = -0.1;
    expectRejected(draft, CatalogError::InvalidMaterial, "material");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].collision.resize(kMaximumPartBoxes + 1);
    expectRejected(draft, CatalogError::Capacity, "collision");
    draft = makeStarterCatalogDraft();
    draft.definitions.resize(kMaximumCatalogDefinitions + 1);
    expectRejected(draft, CatalogError::Capacity, "definitions");
}

TEST(PartCatalog, PrototypeRegistryRequiresExactIdentityDimensionsAndOneLod) {
    auto draft = makeStarterCatalogDraft();
    auto& visual = std::get<PrototypeBoxVisual>(draft.definitions[0].visuals[0].asset);
    visual.asset.id.counter = 2;
    expectRejected(draft, CatalogError::InvalidAssetReference, "visuals.asset");
    visual.asset = prototypeBoxAsset();
    visual.asset.version = 2;
    expectRejected(draft, CatalogError::InvalidAssetReference, "visuals.asset");
    visual.asset = prototypeBoxAsset();
    visual.bounds.maximum.x -= 1;
    expectRejected(draft, CatalogError::InvalidAssetReference, "visuals.asset");
    draft = makeStarterCatalogDraft();
    draft.definitions[0].visuals[0].minimumScreenHeightPixels = 64;
    draft.definitions[0].visuals.push_back(draft.definitions[0].visuals[0]);
    draft.definitions[0].visuals.back().minimumScreenHeightPixels = 0;
    expectRejected(draft, CatalogError::InvalidAssetReference, "visuals.asset");
    draft = makeStarterCatalogDraft();
    CatalogIssue issue{};
    EXPECT_FALSE(PartCatalog::create(draft, issue, {}, CatalogPolicy{false}));
    EXPECT_EQ(issue.error, CatalogError::PrototypeNotAllowed);
}

class TemporaryAsset {
public:
    TemporaryAsset() {
        std::random_device random;
        for (size_t i = 0; i < 100; ++i) {
            const auto candidate = std::filesystem::temp_directory_path()
                / ("voxys-part-catalog-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory_ = candidate;
                write("catalog asset inventory fixture");
                return;
            }
        }
        throw std::runtime_error("could not reserve unique catalog fixture directory");
    }
    ~TemporaryAsset() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }
    TemporaryAsset(const TemporaryAsset&) = delete;
    TemporaryAsset& operator=(const TemporaryAsset&) = delete;
    void write(std::string_view bytes) const {
        std::ofstream stream(directory_ / "part.vmesh", std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("could not write catalog fixture");
    }
    [[nodiscard]] bool matches() const {
        constexpr std::string_view expected = "catalog asset inventory fixture";
        std::ifstream stream(directory_ / "part.vmesh", std::ios::binary);
        if (!stream) {
            return false;
        }
        std::array<char, expected.size() + 1> bytes{};
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return stream.eof() && stream.gcount() == static_cast<std::streamsize>(expected.size())
            && std::equal(expected.begin(), expected.end(), bytes.begin());
    }
    void remove() const { std::filesystem::remove(directory_ / "part.vmesh"); }

private:
    std::filesystem::path directory_;
};

TEST(PartCatalog, CookedAssetsRequireExactResolverAndActualBackingEntry) {
    TemporaryAsset fixture;
    auto draft = makeStarterCatalogDraft();
    draft.definitions.resize(1);
    ContentKey asset = prototypeBoxAsset();
    asset.id.counter = 42;
    asset.version = 7;
    const CookedMeshVisual expected{asset, "parts/test.vmesh"};
    draft.definitions[0].visuals = {{expected, 0.0}};
    CatalogIssue issue{};
    EXPECT_FALSE(PartCatalog::create(draft, issue));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    const CookedAssetResolver resolver = [&](const CookedMeshVisual& reference) {
        return reference.asset == expected.asset && reference.path == expected.path && fixture.matches();
    };
    EXPECT_TRUE(PartCatalog::create(draft, issue, resolver, CatalogPolicy{false}));
    auto& reference = std::get<CookedMeshVisual>(draft.definitions[0].visuals[0].asset);
    reference.asset = prototypeBoxAsset();
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::InvalidAssetReference);
    reference = expected;
    reference.asset.version = 8;
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    reference = expected;
    reference.path = "parts/missing.vmesh";
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    reference = expected;
    fixture.write("wrong backing content");
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    fixture.write("catalog asset inventory fixturE");
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    fixture.write("catalog asset inventory fixture!");
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
    fixture.remove();
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::MissingAsset);
}

TEST(PartCatalog, InvalidAssetPathsAndLodsFailBeforeAssetLookup) {
    auto draft = makeStarterCatalogDraft();
    draft.definitions.resize(1);
    auto cookedKey = prototypeBoxAsset();
    cookedKey.id.counter = 42;
    for (const auto path : {"../escape.vmesh", "/absolute.vmesh", "parts\\file.vmesh", "parts//file.vmesh",
                            "parts/./file.vmesh", "file://part.vmesh", "part.glb"}) {
        draft.definitions[0].visuals = {{CookedMeshVisual{cookedKey, path}, 0.0}};
        size_t calls = 0;
        CatalogIssue issue{};
        const auto catalog = PartCatalog::create(draft, issue, [&](const auto&) { ++calls; return false; });
        EXPECT_FALSE(catalog);
        EXPECT_EQ(issue.error, CatalogError::InvalidAssetReference) << path;
        EXPECT_EQ(calls, 0u);
    }
    const CookedMeshVisual mesh{cookedKey, "parts/test.vmesh"};
    // Exact test inventory for metadata-only LOD validation; no renderer claim.
    const CookedAssetResolver resolver = [mesh](const auto& value) { return value.asset == mesh.asset && value.path == mesh.path; };
    draft.definitions[0].visuals = {{mesh, 64.0}, {mesh, 16.0}, {mesh, 0.0}};
    CatalogIssue issue{};
    EXPECT_TRUE(PartCatalog::create(draft, issue, resolver, CatalogPolicy{false}));
    for (double threshold : {64.0, 65.0, std::numeric_limits<double>::quiet_NaN()}) {
        draft.definitions[0].visuals[1].minimumScreenHeightPixels = threshold;
        EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
        EXPECT_EQ(issue.error, CatalogError::InvalidVisualLod);
    }
    draft.definitions[0].visuals = {{mesh, 1.0}};
    EXPECT_FALSE(PartCatalog::create(draft, issue, resolver));
    EXPECT_EQ(issue.error, CatalogError::InvalidVisualLod);
}

} // namespace
} // namespace voxy::game::construction

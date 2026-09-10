#include "game/construction/build_model.hpp"

#include <gtest/gtest.h>
#include "core/sha256.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace voxy::game::construction;

constexpr WorldNamespace kWorld{{'b', 'u', 'i', 'l', 'd', '-', 't', 'e', 's', 't', '-', 'w', 'o', 'r', 'l', 'd'}};
DurableId id(uint64_t counter) { return {kWorld, counter}; }

PartCatalog validatedCatalog(PartCatalogDraft draft) {
    CatalogIssue issue;
    auto result = PartCatalog::create(draft, issue);
    if (!result) throw std::runtime_error("invalid catalog fixture: " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}

const PartCatalog& catalog() {
    static const auto value = validatedCatalog(makeStarterCatalogDraft());
    return value;
}

BuildSnapshot emptyBuild() {
    BuildSnapshot result;
    result.id = id(1); result.owner = id(2);
    return result;
}

PartInstance part(uint64_t counter, StarterPart kind = StarterPart::Beam, GridPosition position = {}) {
    PartInstance result;
    result.id = id(counter); result.definition = starterPartKey(kind);
    result.placement.translation = position; result.owningBuild = id(1);
    result.settings = defaultModuleSettings(*catalog().lookup(result.definition).definition);
    return result;
}

Connection weld(uint64_t counter, uint64_t a, uint64_t b, uint64_t socketA = 1, uint64_t socketB = 2) {
    Connection result;
    result.id = id(counter); result.a = {id(a), SocketId{socketA}}; result.b = {id(b), SocketId{socketB}};
    result.strength = {1000.0, 1000.0, 1000.0, 1000.0};
    return result;
}

BuildSnapshot stack() {
    auto result = emptyBuild();
    result.parts = {part(10), part(11, StarterPart::Beam, {0, kBrickBodyTicks, 0})};
    result.connections = {weld(100, 10, 11)};
    return result;
}

BuildError error(const BuildSnapshot& draft, const PartCatalog& definitions = catalog()) {
    BuildIssue issue;
    const auto model = BuildModel::create(draft, definitions, issue);
    EXPECT_EQ(model.has_value(), issue.error == BuildError::None);
    return issue.error;
}

std::vector<std::byte> encoded(const BuildSnapshot& build, const PartCatalog& definitions = catalog()) {
    std::vector<std::byte> result;
    const auto issue = encodeBuild(build, definitions, result);
    EXPECT_EQ(issue.error, BuildError::None) << issue.field;
    return result;
}

void put(std::vector<std::byte>& bytes, size_t offset, uint64_t value, size_t count) {
    ASSERT_LE(offset + count, bytes.size());
    for (size_t i = 0; i < count; ++i) bytes[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 255u);
}

PartDefinition tinyDefinition() {
    auto definition = makeStarterCatalogDraft().definitions.front();
    definition.key.id.counter = 500;
    definition.nameKey = "salvage.part.test_tiny";
    definition.footprint = {{-1, -1, -1}, {1, 1, 1}};
    definition.solidOccupancy = {{ProxyId{1}, {}, {1, 1, 1}}};
    definition.collision = definition.solidOccupancy;
    definition.buoyancy.clear();
    definition.mass = {1.0, {}, {{2.0 / 7500.0, 0.0, 0.0, 0.0, 2.0 / 7500.0, 0.0, 0.0, 0.0, 2.0 / 7500.0}}};
    definition.sockets.resize(2);
    definition.sockets[0].frame.translation.y = 1;
    definition.sockets[1].frame.translation.y = -1;
    definition.visuals = {{PrototypeBoxVisual{prototypeBoxAsset(), definition.footprint}, 0.0}};
    return definition;
}

static_assert(!std::is_default_constructible_v<BuildModel>);
static_assert(!std::is_constructible_v<BuildSnapshot, BuildBlueprint>);
template <class T> concept HasProvenance = requires(T value) { value.provenance; };
template <class T> concept HasHealth = requires(T value) { value.health; };
template <class T> concept HasOwner = requires(T value) { value.owner; };
static_assert(!HasProvenance<DesignPart> && !HasHealth<DesignPart> && !HasOwner<BuildBlueprint>);

TEST(BuildModel, EngagedBrickSpacingAndAllProperOrientationsAreExact) {
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        auto draft = stack();
        const GridTransform whole{{137, -29, 211}, {rotation}};
        for (auto& instance : draft.parts) instance.placement = *compose(whole, instance.placement);
        EXPECT_EQ(error(draft), BuildError::None) << static_cast<int>(rotation);
    }
    auto loose = stack();
    loose.parts[1].placement.translation.y += kStudInsertionTicks;
    EXPECT_EQ(error(loose), BuildError::MisalignedWeld); // .18 m loose studs are not engaged spacing.
    auto penetrating = stack();
    penetrating.connections.clear();
    penetrating.parts[1].placement.translation.y -= 1;
    EXPECT_EQ(error(penetrating), BuildError::SolidOverlap);
}

TEST(BuildModel, AsymmetricSocketPlacementAndSidewaysPlateNeedFineLattice) {
    auto draft = emptyBuild();
    draft.parts = {part(10), part(11, StarterPart::Plate, {75, 32, 0})};
    draft.connections = {weld(100, 10, 11, 106, 2)};
    EXPECT_EQ(error(draft), BuildError::None);
    for (auto& instance : draft.parts) instance.placement = *compose(GridTransform{{}, {4}}, instance.placement);
    EXPECT_EQ(error(draft), BuildError::None);
    const auto position = draft.parts[1].placement.translation;
    EXPECT_TRUE(position.x == 32 || position.x == -32 || position.z == 32 || position.z == -32);
    draft.parts[1].placement.translation.x += 1;
    EXPECT_EQ(error(draft), BuildError::MisalignedWeld);
}

TEST(BuildModel, SocketNormalsAndKeysMustBothMatch) {
    auto draft = stack();
    auto definitions = makeStarterCatalogDraft();
    definitions.definitions[0].sockets[1].frame.rotation = CubeRotation{};
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::MisalignedWeld);
    definitions.definitions[0].sockets[1].frame.rotation = CubeRotation{18};
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::MisalignedWeld);
}

TEST(BuildModel, HalfOpenBoundsAndIntegerOverflowAreRejectedExactly) {
    EXPECT_FALSE(solidBoxesOverlap({{-5, -5, -5}, {5, 5, 5}}, {{5, -5, -5}, {15, 5, 5}}));
    EXPECT_TRUE(solidBoxesOverlap({{-5, -5, -5}, {5, 5, 5}}, {{4, -5, -5}, {15, 5, 5}}));
    const GridBox box{{-1, -2, -3}, {7, 5, 4}};
    for (uint8_t r = 0; r < 24; ++r) {
        const GridTransform transform{{-11, 37, -103}, {r}};
        const auto transformed = transformBounds(transform, box);
        ASSERT_TRUE(transformed);
        EXPECT_EQ(transformBounds(*inverse(transform), *transformed), box);
    }
    auto draft = emptyBuild(); draft.parts = {part(10)};
    draft.parts[0].placement.translation.x = std::numeric_limits<int32_t>::max();
    EXPECT_EQ(error(draft), BuildError::InvalidPlacement);
    draft.parts[0].placement.translation.x = std::numeric_limits<int32_t>::min();
    EXPECT_EQ(error(draft), BuildError::InvalidPlacement);
    draft.parts[0].placement = {{}, {24}};
    EXPECT_EQ(error(draft), BuildError::InvalidPlacement);
}

TEST(BuildModel, MatingClearanceIsBoundedAndCannotPermitThirdPartyIntrusion) {
    auto definitions = makeStarterCatalogDraft();
    definitions.definitions[0].sockets[0].frame.translation.x = 100;
    definitions.definitions[0].sockets[1].frame.translation.x = 100;
    definitions.definitions.push_back(tinyDefinition());
    const auto custom = validatedCatalog(definitions);
    auto draft = stack();
    EXPECT_EQ(error(draft, custom), BuildError::None);
    auto third = part(12); third.definition = definitions.definitions.back().key;
    third.placement.translation = {110, 28, 0};
    draft.parts.push_back(third);
    EXPECT_EQ(error(draft, custom), BuildError::ClearanceBlocked);
    draft.connections.clear(); // Unconnected sockets are query regions, not global keep-empty solids.
    EXPECT_EQ(error(draft, custom), BuildError::None);
    draft = stack();
    definitions.definitions[0].sockets[1].clearance.minimum.x = -5;
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::ClearanceBlocked);
}

TEST(BuildModel, WeldCannotUseClearanceToHideSolidInterpenetration) {
    auto definitions = makeStarterCatalogDraft();
    // Align the sockets at a penetrating position, so frame validation passes.
    definitions.definitions[0].sockets[0].frame.translation.y = 23;
    auto draft = stack(); draft.parts[1].placement.translation.y = 47;
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::SolidOverlap);
}

TEST(BuildModel, RopeAndLatchStoreIntentWithoutInventingRigidCapture) {
    auto draft = emptyBuild();
    draft.parts = {part(10, StarterPart::Winch), part(11, StarterPart::TowEye, {400, 60, 50})};
    auto link = weld(100, 10, 11, 10, 10);
    link.kind = ConnectionKind::Rope;
    link.minimumLengthMillimetres = 500; link.maximumLengthMillimetres = 40000; link.restLengthMillimetres = 5000;
    draft.connections = {link};
    EXPECT_EQ(error(draft), BuildError::None); // No coincident-frame test for a flexible rope.
    std::optional<BuildModel> roundtrip;
    EXPECT_EQ(decodeBuild(encoded(draft), catalog(), roundtrip).error, BuildError::None);
    ASSERT_TRUE(roundtrip);
    EXPECT_EQ(roundtrip->snapshot().parts[0].settings.defaultLineLengthMillimetres, 500u);
    EXPECT_EQ(roundtrip->snapshot().connections[0].restLengthMillimetres, 5000u);
    draft.connections[0].kind = ConnectionKind::Weld;
    EXPECT_EQ(error(draft), BuildError::IncompatibleSocket);
    draft.connections[0] = link; draft.connections[0].maximumLengthMillimetres = 40001;
    EXPECT_EQ(error(draft), BuildError::InvalidConnection);
    draft.connections[0] = link; draft.connections[0].strength.tensionNewtons = 12001.0;
    EXPECT_EQ(error(draft), BuildError::InvalidConnection);
    auto definitions = makeStarterCatalogDraft();
    auto cargo = tinyDefinition();
    cargo.sockets[0].family = SocketFamily::CargoLatch;
    definitions.definitions.push_back(cargo);
    auto custom = validatedCatalog(definitions);
    draft = emptyBuild(); draft.parts = {part(10, StarterPart::CargoCradle), part(11)};
    draft.parts[1].definition = cargo.key; draft.parts[1].placement.translation = {1000, 0, 0};
    draft.connections = {weld(100, 10, 11, 10, 1)};
    draft.connections[0].kind = ConnectionKind::Latch;
    EXPECT_EQ(error(draft, custom), BuildError::None); // Authored latch intent; not a captured body.
}

TEST(BuildModel, SocketSlotsCoverFlexibleAndDisabledLinks) {
    auto draft = emptyBuild();
    draft.parts = {part(10, StarterPart::Winch), part(11, StarterPart::TowEye, {400, 0, 0}),
        part(12, StarterPart::TowEye, {-400, 0, 0})};
    auto a = weld(100, 10, 11, 10, 10);
    a.kind = ConnectionKind::Rope; a.minimumLengthMillimetres = 500;
    a.maximumLengthMillimetres = 10000; a.restLengthMillimetres = 5000;
    auto b = a; b.id = id(101); b.b.part = id(12);
    draft.connections = {a, b};
    EXPECT_EQ(error(draft), BuildError::SocketCapacity);
    draft.connections[1].enabled = false;
    EXPECT_EQ(error(draft), BuildError::SocketCapacity);
    draft.connections.resize(1);
    BuildIssue issue; auto model = BuildModel::create(draft, catalog(), issue);
    ASSERT_TRUE(model);
    const auto occupied = std::find_if(model->sockets().begin(), model->sockets().end(), [](const auto& value) {
        return value.endpoint == SocketEndpoint{id(10), SocketId{10}};
    });
    ASSERT_NE(occupied, model->sockets().end()); EXPECT_EQ(occupied->usedSlots, 1);
}

TEST(BuildModel, ConnectionValidationRejectsDanglingDuplicateUnknownAndOverpoweredLinks) {
    auto draft = stack(); draft.connections[0].b.part = id(999);
    EXPECT_EQ(error(draft), BuildError::UnknownPart);
    draft = stack(); draft.connections[0].b.socket = SocketId{999};
    EXPECT_EQ(error(draft), BuildError::UnknownSocket);
    draft = stack(); draft.connections[0].b.part = id(10);
    EXPECT_EQ(error(draft), BuildError::SamePartConnection);
    draft = stack(); draft.connections[0].kind = static_cast<ConnectionKind>(3);
    EXPECT_EQ(error(draft), BuildError::IncompatibleSocket);
    draft = stack(); auto second = draft.connections[0]; second.id = id(101); std::swap(second.a, second.b);
    draft.connections.push_back(second);
    EXPECT_EQ(error(draft), BuildError::DuplicateConnection);
    for (double strength : {0.0, -0.0, -1.0, 30001.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
        draft = stack(); draft.connections[0].strength.tensionNewtons = strength;
        EXPECT_EQ(error(draft), BuildError::InvalidConnection);
    }
    draft = stack(); draft.connections[0].damage = kFullHealth + 1;
    EXPECT_EQ(error(draft), BuildError::InvalidConnection);
    draft = stack(); draft.connections[0].restLengthMillimetres = 1;
    EXPECT_EQ(error(draft), BuildError::InvalidConnection);
}

TEST(BuildModel, ExactContentOwnershipProvenanceAndModuleSettingsAreValidated) {
    auto draft = stack(); draft.parts[0].definition.version = 2;
    EXPECT_EQ(error(draft), BuildError::UnknownDefinitionVersion);
    draft = stack(); draft.parts[0].definition.id.counter = 999;
    EXPECT_EQ(error(draft), BuildError::UnknownDefinition);
    draft = stack(); draft.parts[0].owningBuild = id(9);
    EXPECT_EQ(error(draft), BuildError::WrongBuild);
    draft = stack(); draft.parts[0].id.world.bytes[0] ^= 1;
    EXPECT_EQ(error(draft), BuildError::WrongWorld);
    draft = stack(); draft.parts[0].health = kFullHealth + 1;
    EXPECT_EQ(error(draft), BuildError::InvalidHealth);
    draft = stack(); draft.parts[0].provenance.origin = PartOrigin::StarterLoan;
    EXPECT_EQ(error(draft), BuildError::InvalidProvenance);
    draft.parts[0].provenance.starterEntitlement = id(900);
    EXPECT_EQ(error(draft), BuildError::None);
    draft.parts[0].provenance.starterEntitlement = id(11);
    EXPECT_EQ(error(draft), BuildError::InvalidProvenance);
    draft = stack(); draft.parts[0].provenance.starterEntitlement = id(900);
    EXPECT_EQ(error(draft), BuildError::InvalidProvenance);
    draft = stack(); draft.editLease = EditLease{id(3), AuthorityEpoch{0}, SimulationTick{200}};
    EXPECT_EQ(error(draft), BuildError::InvalidLease);
    draft = stack(); draft.parts[0].settings.limitPermille = 1000;
    EXPECT_EQ(error(draft), BuildError::InvalidSettings);
    draft = emptyBuild(); draft.parts = {part(10, StarterPart::Engine)};
    EXPECT_EQ(error(draft), BuildError::None);
    draft.parts[0].settings.limitPermille = 1001;
    EXPECT_EQ(error(draft), BuildError::InvalidSettings);
    draft = emptyBuild(); draft.parts = {part(10, StarterPart::Winch)};
    draft.parts[0].settings.defaultLineLengthMillimetres = 499;
    EXPECT_EQ(error(draft), BuildError::InvalidSettings);
    auto definitions = makeStarterCatalogDraft(); definitions.definitions[0].permittedRotationMask = 1;
    draft = stack(); draft.parts[0].placement.rotation = CubeRotation{1};
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::InvalidPlacement);
}

TEST(BuildModel, RevisionsAndFailedEditsLeaveCanonicalAndDerivedStateUntouched) {
    BuildIssue issue; auto model = BuildModel::create(stack(), catalog(), issue);
    ASSERT_TRUE(model);
    auto candidate = model->snapshot();
    const auto before = encoded(model->snapshot());
    const auto firstBounds = model->solids()[0].bounds;
    candidate.parts[0].placement.translation.y += 1;
    EXPECT_EQ(model->replace(TopologyRevision{0}, candidate, catalog()).error, BuildError::MisalignedWeld);
    EXPECT_EQ(encoded(model->snapshot()), before); EXPECT_EQ(model->solids()[0].bounds, firstBounds);
    candidate = model->snapshot(); candidate.parts[0].paint = {33, 200, 19, 255};
    EXPECT_EQ(model->replace(TopologyRevision{9}, candidate, catalog()).error, BuildError::StaleRevision);
    EXPECT_EQ(encoded(model->snapshot()), before);
    EXPECT_EQ(model->replace(TopologyRevision{0}, candidate, catalog()).error, BuildError::None);
    EXPECT_EQ(model->snapshot().revision.value(), 1u);
    EXPECT_EQ(model->snapshot().parts[0].paint, candidate.parts[0].paint);
    EXPECT_EQ(model->replace(TopologyRevision{0}, candidate, catalog()).error, BuildError::StaleRevision);
    auto exhausted = stack(); exhausted.revision = TopologyRevision{std::numeric_limits<uint64_t>::max()};
    auto maximum = BuildModel::create(exhausted, catalog(), issue); ASSERT_TRUE(maximum);
    EXPECT_EQ(maximum->replace(exhausted.revision, exhausted, catalog()).error, BuildError::RevisionExhausted);
    EXPECT_EQ(encoded(maximum->snapshot()), encoded(exhausted));
}

TEST(BuildModel, ExistingPhysicalIdentityCannotConvertLoanToPaidOrReplaceDefinition) {
    auto draft = stack(); draft.parts[0].provenance = {PartOrigin::StarterLoan, id(900)};
    BuildIssue issue; auto model = BuildModel::create(draft, catalog(), issue); ASSERT_TRUE(model);
    const auto before = encoded(model->snapshot());
    auto candidate = model->snapshot(); candidate.parts[0].provenance = {};
    EXPECT_EQ(model->replace({}, candidate, catalog()).error, BuildError::ImmutableIdentity);
    candidate = model->snapshot(); candidate.parts[0].definition = starterPartKey(StarterPart::Pontoon);
    candidate.parts[0].settings = defaultModuleSettings(*catalog().lookup(candidate.parts[0].definition).definition);
    EXPECT_EQ(model->replace({}, candidate, catalog()).error, BuildError::ImmutableIdentity);
    EXPECT_EQ(encoded(model->snapshot()), before);
}

TEST(BuildModel, ReplacementCannotReuseAnExistingPhysicalIdAcrossRecordKinds) {
    BuildIssue issue; auto model = BuildModel::create(stack(), catalog(), issue); ASSERT_TRUE(model);
    const auto before = encoded(model->snapshot());
    auto candidate = model->snapshot(); candidate.connections.clear();
    candidate.parts.push_back(part(100, StarterPart::Beam, {300, 0, 0}));
    ASSERT_EQ(error(candidate), BuildError::None); // Valid alone; illegal relative to its predecessor.
    EXPECT_EQ(model->replace({}, candidate, catalog()).error, BuildError::ImmutableIdentity);
    EXPECT_EQ(encoded(model->snapshot()), before);
    candidate = model->snapshot(); candidate.parts[1].id = id(12);
    candidate.connections[0].id = id(11); candidate.connections[0].b.part = id(12);
    ASSERT_EQ(error(candidate), BuildError::None);
    EXPECT_EQ(model->replace({}, candidate, catalog()).error, BuildError::ImmutableIdentity);
    EXPECT_EQ(encoded(model->snapshot()), before);
}

TEST(BuildModel, InvalidAndDuplicateIdsFailBeforePhysicalPublication) {
    auto draft = stack(); draft.id = {};
    EXPECT_EQ(error(draft), BuildError::InvalidId);
    draft = stack(); draft.owner = {};
    EXPECT_EQ(error(draft), BuildError::InvalidId);
    draft = stack(); draft.parts[0].id.counter = 0;
    EXPECT_EQ(error(draft), BuildError::InvalidId);
    draft = stack(); draft.parts[1].id = draft.parts[0].id;
    EXPECT_EQ(error(draft), BuildError::DuplicateId);
    draft = stack(); draft.connections[0].id = draft.parts[0].id;
    EXPECT_EQ(error(draft), BuildError::DuplicateId);
}

TEST(BuildModel, ModelOwnsItsDraftAndRecordsAreCanonical) {
    auto draft = stack(); std::reverse(draft.parts.begin(), draft.parts.end());
    std::swap(draft.connections[0].a, draft.connections[0].b);
    BuildIssue issue; auto model = BuildModel::create(draft, catalog(), issue); ASSERT_TRUE(model);
    EXPECT_EQ(model->snapshot().parts[0].id, id(10));
    EXPECT_LT(model->snapshot().connections[0].a, model->snapshot().connections[0].b);
    draft.parts.clear(); draft.connections.clear();
    EXPECT_EQ(model->snapshot().parts.size(), 2u); EXPECT_EQ(model->solids().size(), 2u);
    EXPECT_EQ(encoded(model->snapshot()), encoded(stack()));
}

TEST(BuildModel, BothPartAndConnectionInsertionOrderHaveOneEncoding) {
    auto draft = stack();
    draft.parts.push_back(part(12, StarterPart::Beam, {0, 96, 0}));
    draft.connections.push_back(weld(101, 11, 12));
    const auto before = encoded(draft);
    std::reverse(draft.parts.begin(), draft.parts.end());
    std::reverse(draft.connections.begin(), draft.connections.end());
    for (auto& link : draft.connections) std::swap(link.a, link.b);
    EXPECT_EQ(encoded(draft), before);
}

TEST(BuildModel, CanonicalCodecPreservesLosslessIdsConditionLeaseAndConfiguration) {
    auto draft = stack();
    draft.parts[0].id = id(std::numeric_limits<uint64_t>::max() - 1);
    draft.parts[1].id = id(std::numeric_limits<uint64_t>::max());
    draft.connections[0].a.part = draft.parts[0].id; draft.connections[0].b.part = draft.parts[1].id;
    draft.parts[0].health = 4321; draft.parts[0].paint = {1, 17, 253, 75};
    draft.parts[1].provenance = {PartOrigin::StarterLoan, id(900)};
    draft.editLease = EditLease{id(3), AuthorityEpoch{9007199254740993ull}, SimulationTick{std::numeric_limits<uint64_t>::max()}};
    draft.revision = TopologyRevision{9007199254740999ull};
    draft.connections[0].damage = 321;
    const auto bytes = encoded(draft);
    ASSERT_EQ(bytes.size(), kBuildHeaderBytes + 2 * kBuildPartBytes + kBuildConnectionBytes);
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[kBuildHeaderBytes + 16]), 254u);
    EXPECT_EQ(std::to_integer<uint8_t>(bytes[kBuildHeaderBytes + 23]), 255u);
    std::optional<BuildModel> decoded;
    EXPECT_EQ(decodeBuild(bytes, catalog(), decoded).error, BuildError::None); ASSERT_TRUE(decoded);
    EXPECT_EQ(encoded(decoded->snapshot()), bytes);
    EXPECT_EQ(decoded->snapshot().parts[1].id.counter, std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(decoded->snapshot().parts[0].health, 4321);
    EXPECT_EQ(decoded->snapshot().editLease, draft.editLease);
    std::reverse(draft.parts.begin(), draft.parts.end()); std::swap(draft.connections[0].a, draft.connections[0].b);
    EXPECT_EQ(encoded(draft), bytes);
}

TEST(BuildModel, SchemaOneMatchesIndependentFullWireFixture) {
    // Authored from the documented wire contract using Python struct.pack,
    // never captured from encodeBuild. Recipe/field offsets and retained binary:
    // docs/validation/salvage/DATA-03/generate-wire-fixture.py
    // SHA-256: 6afbf529535b91973b640b2b1b1ab6205ed863218ee5242ce171b948048b8ea3
    // SVBM_GOLDEN_HEX_BEGIN
    constexpr std::string_view hex =
        "5356424d010000006275696c642d746573742d776f726c640100000000000000"
        "efcdab89674523016275696c642d746573742d776f726c640200000000000000"
        "016275696c642d746573742d776f726c64030000000000000001000000000020"
        "00fdffffffffffffff02000000010000006275696c642d746573742d776f726c"
        "640a00000000000000766f7879732d73616c766167652d763107000000000000"
        "000100000077ffffff1d000000d3000000046275696c642d746573742d776f72"
        "6c640100000000000000e1100d1559c8030009000000d2040000016275696c64"
        "2d746573742d776f726c6484030000000000006275696c642d746573742d776f"
        "726c640b00000000000000766f7879732d73616c766167652d76310800000000"
        "00000001000000f7010000d1ffffffa7ffffff126275696c642d746573742d77"
        "6f726c6401000000000000009426f09b08ff0001000000000000000000000000"
        "0000000000000000000000000000000000000000006275696c642d746573742d"
        "776f726c6464000000000000006275696c642d746573742d776f726c640a0000"
        "00000000000a000000000000006275696c642d746573742d776f726c640b0000"
        "00000000000a000000000000000101d2040000000000428f400000000000429f"
        "40000000008071a740000000004040af40ee020000b88800002e160000";
    // SVBM_GOLDEN_HEX_END
    static_assert(hex.size() == 509 * 2);
    constexpr std::string_view digits = "0123456789abcdef";
    std::vector<std::byte> golden;
    golden.reserve(509);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const auto high = digits.find(hex[i]);
        const auto low = digits.find(hex[i + 1]);
        ASSERT_NE(high, std::string_view::npos);
        ASSERT_NE(low, std::string_view::npos);
        golden.push_back(static_cast<std::byte>(high * 16 + low));
    }

    auto draft = emptyBuild();
    draft.revision = TopologyRevision{0x0123456789abcdefull};
    draft.editLease = EditLease{id(3), AuthorityEpoch{9007199254740993ull},
        SimulationTick{18446744073709551613ull}};
    auto winch = part(10, StarterPart::Winch, {-137, 29, 211});
    winch.placement.rotation = CubeRotation{4};
    winch.health = 4321; winch.paint = {13, 21, 89, 200};
    winch.settings = {SettingsKind::Winch, false, 9, 0, false, 1234};
    winch.provenance = {PartOrigin::StarterLoan, id(900)};
    auto eye = part(11, StarterPart::TowEye, {503, -47, -89});
    eye.placement.rotation = CubeRotation{18};
    eye.health = 9876; eye.paint = {240, 155, 8, 255};
    eye.settings = {SettingsKind::Passive, true, 0, 0, false, 0};
    auto rope = weld(100, 10, 11, 10, 10);
    rope.kind = ConnectionKind::Rope; rope.damage = 1234;
    rope.strength = {1000.25, 2000.5, 3000.75, 4000.125};
    rope.minimumLengthMillimetres = 750; rope.maximumLengthMillimetres = 35000;
    rope.restLengthMillimetres = 5678;
    draft.parts = {eye, winch}; // Deliberately noncanonical input order.
    draft.connections = {rope};
    std::swap(draft.connections[0].a, draft.connections[0].b);
    std::vector<std::byte> output{std::byte{0xac}};
    const auto encodedIssue = encodeBuild(draft, catalog(), output);
    ASSERT_EQ(encodedIssue.error, BuildError::None) << encodedIssue.field;
    EXPECT_EQ(output, golden); // Whole fixture, including all reserved/unused bytes.

    // Decode the independent bytes, not the result just emitted by the encoder.
    std::optional<BuildModel> decoded;
    const auto decodedIssue = decodeBuild(golden, catalog(), decoded);
    ASSERT_EQ(decodedIssue.error, BuildError::None) << decodedIssue.field;
    ASSERT_TRUE(decoded);
    const auto& actual = decoded->snapshot();
    EXPECT_EQ(actual.id, draft.id); EXPECT_EQ(actual.owner, draft.owner);
    EXPECT_EQ(actual.revision, draft.revision); EXPECT_EQ(actual.editLease, draft.editLease);
    ASSERT_EQ(actual.parts.size(), 2u);
    EXPECT_EQ(actual.parts[0], winch); EXPECT_EQ(actual.parts[1], eye);
    ASSERT_EQ(actual.connections.size(), 1u);
    const auto& link = actual.connections[0];
    EXPECT_EQ(link.id, rope.id); EXPECT_EQ(link.a, rope.a); EXPECT_EQ(link.b, rope.b);
    EXPECT_EQ(link.kind, ConnectionKind::Rope); EXPECT_TRUE(link.enabled);
    EXPECT_EQ(link.damage, 1234);
    EXPECT_DOUBLE_EQ(link.strength.tensionNewtons, 1000.25);
    EXPECT_DOUBLE_EQ(link.strength.shearNewtons, 2000.5);
    EXPECT_DOUBLE_EQ(link.strength.bendingNewtonMetres, 3000.75);
    EXPECT_DOUBLE_EQ(link.strength.torsionNewtonMetres, 4000.125);
    EXPECT_EQ(link.minimumLengthMillimetres, 750u);
    EXPECT_EQ(link.maximumLengthMillimetres, 35000u);
    EXPECT_EQ(link.restLengthMillimetres, 5678u);
}

TEST(BuildModel, DecoderRejectsMalformedOrderValuesAndContentWithoutPublishing) {
    const auto bytes = encoded(stack());
    BuildIssue issue; auto output = BuildModel::create(emptyBuild(), catalog(), issue); ASSERT_TRUE(output);
    const auto original = encoded(output->snapshot());
    const auto rejects = [&](std::vector<std::byte> malformed, BuildError expected) {
        EXPECT_EQ(decodeBuild(malformed, catalog(), output).error, expected);
        ASSERT_TRUE(output); EXPECT_EQ(encoded(output->snapshot()), original);
    };
    auto bad = bytes; put(bad, 4, 2, 4); rejects(bad, BuildError::UnsupportedSchema);
    bad = bytes; bad.push_back(std::byte{}); rejects(bad, BuildError::InvalidEncoding);
    bad = bytes; put(bad, 105, kMaximumBuildParts + 1, 4); rejects(bad, BuildError::Capacity);
    bad = bytes; put(bad, 109, kMaximumBuildConnections + 1, 4); rejects(bad, BuildError::Capacity);
    bad = bytes; put(bad, 64, 2, 1); rejects(bad, BuildError::InvalidEncoding);
    bad = bytes; put(bad, 65 + 16, 1, 8); rejects(bad, BuildError::InvalidEncoding);
    bad = bytes; put(bad, kBuildHeaderBytes + 48, 2, 4); rejects(bad, BuildError::UnknownDefinitionVersion);
    bad = bytes; put(bad, kBuildHeaderBytes + 95, 255, 1); rejects(bad, BuildError::InvalidSettings);
    bad = bytes; put(bad, kBuildHeaderBytes + 96, 2, 1); rejects(bad, BuildError::InvalidEncoding);
    bad = bytes; put(bad, kBuildHeaderBytes + 105, 99, 1); rejects(bad, BuildError::InvalidProvenance);
    bad = bytes; put(bad, kBuildHeaderBytes + 2 * kBuildPartBytes + 88, 99, 1); rejects(bad, BuildError::IncompatibleSocket);
    bad = bytes;
    std::swap_ranges(bad.begin() + static_cast<std::ptrdiff_t>(kBuildHeaderBytes),
        bad.begin() + static_cast<std::ptrdiff_t>(kBuildHeaderBytes + kBuildPartBytes),
        bad.begin() + static_cast<std::ptrdiff_t>(kBuildHeaderBytes + kBuildPartBytes));
    rejects(bad, BuildError::NonCanonicalOrder);
    bad = bytes;
    const auto endpointStart = static_cast<std::ptrdiff_t>(kBuildHeaderBytes + 2 * kBuildPartBytes + 24);
    std::swap_ranges(bad.begin() + endpointStart, bad.begin() + endpointStart + 32, bad.begin() + endpointStart + 32);
    rejects(bad, BuildError::NonCanonicalOrder);
    rejects(std::vector<std::byte>(kMaximumBuildBytes + 1), BuildError::Capacity);
    for (size_t length = 0; length < bytes.size(); ++length) {
        EXPECT_NE(decodeBuild(std::span{bytes}.first(length), catalog(), output).error, BuildError::None) << length;
        ASSERT_TRUE(output); EXPECT_EQ(encoded(output->snapshot()), original);
    }
}

TEST(BuildModel, EncodingFailureAndContentMismatchLeaveOutputUnchanged) {
    auto draft = stack(); std::vector<std::byte> output{std::byte{123}, std::byte{234}};
    const auto before = output;
    draft.parts[0].health = kFullHealth + 1;
    EXPECT_EQ(encodeBuild(draft, catalog(), output).error, BuildError::InvalidHealth);
    EXPECT_EQ(output, before);
    auto definitions = makeStarterCatalogDraft(); definitions.definitions.erase(definitions.definitions.begin());
    std::optional<BuildModel> decoded;
    EXPECT_EQ(decodeBuild(encoded(stack()), validatedCatalog(definitions), decoded).error, BuildError::UnknownDefinition);
    EXPECT_FALSE(decoded);
}

TEST(BuildModel, IndependentPartConnectionProxySocketAndPairWorkCaps) {
    auto draft = emptyBuild();
    draft.parts.resize(kMaximumBuildParts + 1);
    EXPECT_EQ(error(draft), BuildError::Capacity);
    draft = emptyBuild(); draft.connections.resize(kMaximumBuildConnections + 1);
    EXPECT_EQ(error(draft), BuildError::Capacity);
    auto definitions = makeStarterCatalogDraft(); auto& beam = definitions.definitions[0];
    for (uint64_t i = 2; i <= 16; ++i) { auto box = beam.solidOccupancy[0]; box.id = ProxyId{i}; beam.solidOccupancy.push_back(box); }
    draft = emptyBuild();
    for (uint64_t i = 0; i < 129; ++i) draft.parts.push_back(part(10 + i, StarterPart::Beam, {0, 0, static_cast<int32_t>(i) * 100}));
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::Capacity);
    definitions = makeStarterCatalogDraft();
    for (uint64_t i = 2; i <= 8; ++i) {
        auto box = definitions.definitions[0].solidOccupancy[0]; box.id = ProxyId{i};
        definitions.definitions[0].solidOccupancy.push_back(box);
    }
    BuildIssue issue; const auto pairCapped = BuildModel::create(draft, validatedCatalog(definitions), issue);
    EXPECT_FALSE(pairCapped); EXPECT_EQ(issue.error, BuildError::Capacity); EXPECT_EQ(issue.field, "candidatePairs");
    definitions = makeStarterCatalogDraft();
    auto socket = definitions.definitions[0].sockets[0]; definitions.definitions[0].sockets.clear();
    for (uint64_t i = 1; i <= 128; ++i) { socket.id = SocketId{i}; definitions.definitions[0].sockets.push_back(socket); }
    draft.parts.resize(65);
    EXPECT_EQ(error(draft, validatedCatalog(definitions)), BuildError::Capacity);
}

TEST(BuildModel, BlueprintCodecIsCanonicalAndOmitsOwnershipConditionAndProvenance) {
    BuildIssue issue;auto source=stack();source.parts.at(0).paint={17,24,203,255};
    auto model=BuildModel::create(source,catalog(),issue);ASSERT_TRUE(model);
    const auto design=duplicateDesign(*model);std::vector<std::byte> bytes;
    ASSERT_FALSE(encodeBlueprint(design,catalog(),bytes));EXPECT_EQ(bytes.size(),16+2*59+70+32);
    auto reordered=design;std::reverse(reordered.parts.begin(),reordered.parts.end());
    std::swap(reordered.connections.at(0).a,reordered.connections.at(0).b);
    std::vector<std::byte> same;ASSERT_FALSE(encodeBlueprint(reordered,catalog(),same));EXPECT_EQ(bytes,same);
    for(auto& p:source.parts){p.health=4200;p.provenance={PartOrigin::StarterLoan,id(77)};}
    source.owner=id(89);source.revision=TopologyRevision{9007199254740999ull};
    model=BuildModel::create(source,catalog(),issue);ASSERT_TRUE(model);
    ASSERT_FALSE(encodeBlueprint(duplicateDesign(*model),catalog(),same));EXPECT_EQ(same,bytes);
    std::optional<BuildBlueprint> decoded;ASSERT_FALSE(decodeBlueprint(bytes,catalog(),decoded));ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->parts,design.parts);ASSERT_FALSE(encodeBlueprint(*decoded,catalog(),same));EXPECT_EQ(same,bytes);
    auto powered=emptyBuild();powered.parts={part(10,StarterPart::Propeller)};
    powered.parts.at(0).settings.limitPermille=750;powered.parts.at(0).settings.reversed=true;
    model=BuildModel::create(powered,catalog(),issue);ASSERT_TRUE(model);
    ASSERT_FALSE(encodeBlueprint(duplicateDesign(*model),catalog(),bytes));ASSERT_FALSE(decodeBlueprint(bytes,catalog(),decoded));
    EXPECT_EQ(decoded->parts.at(0).settings,powered.parts.at(0).settings);
}
TEST(BuildModel, BlueprintCodecRejectsDamageVersionsOrdinalsAndForgedSettingsWithoutPublishing) {
    BuildIssue issue;const auto model=BuildModel::create(stack(),catalog(),issue);ASSERT_TRUE(model);
    std::vector<std::byte> bytes;ASSERT_FALSE(encodeBlueprint(duplicateDesign(*model),catalog(),bytes));
    std::optional<BuildBlueprint> decoded;ASSERT_FALSE(decodeBlueprint(bytes,catalog(),decoded));const auto expected=decoded->parts;
    for(size_t size=0;size<bytes.size();++size){ASSERT_TRUE(decodeBlueprint(std::span(bytes).first(size),catalog(),decoded));EXPECT_EQ(decoded->parts,expected);}
    auto changed=bytes;changed.at(18)^=std::byte{1};EXPECT_TRUE(decodeBlueprint(changed,catalog(),decoded));
    const auto rehash=[](std::vector<std::byte>& data){const auto hash=voxy::core::sha256(std::span(data).first(data.size()-32));std::copy(hash.bytes.begin(),hash.bytes.end(),data.end()-32);};
    changed=bytes;put(changed,4,999,4);rehash(changed);EXPECT_EQ(decodeBlueprint(changed,catalog(),decoded).error,BuildError::UnsupportedSchema);
    changed=bytes;put(changed,16+59,1,4);rehash(changed);EXPECT_TRUE(decodeBlueprint(changed,catalog(),decoded));
    changed=bytes;put(changed,16+49,255,1);rehash(changed);EXPECT_EQ(decodeBlueprint(changed,catalog(),decoded).error,BuildError::InvalidSettings);
    changed=bytes;put(changed,8,257,4);rehash(changed);EXPECT_EQ(decodeBlueprint(changed,catalog(),decoded).error,BuildError::Capacity);
    changed=bytes;changed.push_back(std::byte{0});EXPECT_TRUE(decodeBlueprint(changed,catalog(),decoded));EXPECT_EQ(decoded->parts,expected);
    auto invalid=duplicateDesign(*model);invalid.parts.at(0).definition.version=999;auto sentinel=bytes;
    EXPECT_TRUE(encodeBlueprint(invalid,catalog(),sentinel));EXPECT_EQ(sentinel,bytes);
}

TEST(BuildModel, BlueprintDuplicationCannotCarryPhysicalAuthorityOrAllocateIds) {
    auto draft = stack(); draft.parts[0].provenance = {PartOrigin::StarterLoan, id(900)};
    draft.parts[0].health = 0; draft.connections[0].damage = 5000;
    draft.editLease = EditLease{id(3), AuthorityEpoch{8}, SimulationTick{600}};
    BuildIssue issue; auto model = BuildModel::create(draft, catalog(), issue); ASSERT_TRUE(model);
    IdAllocator allocator(kWorld, 1000); const auto before = encoded(model->snapshot());
    for (size_t copy = 0; copy < 5; ++copy) {
        const auto blueprint = duplicateDesign(*model);
        ASSERT_EQ(blueprint.parts.size(), 2u); ASSERT_EQ(blueprint.connections.size(), 1u);
        EXPECT_EQ(blueprint.parts[0].ordinal, 1u); EXPECT_EQ(blueprint.parts[1].ordinal, 2u);
        EXPECT_EQ(blueprint.parts[0].definition, draft.parts[0].definition);
        EXPECT_EQ(blueprint.parts[1].placement, draft.parts[1].placement);
        EXPECT_EQ(blueprint.connections[0].a.partOrdinal, 1u); EXPECT_EQ(blueprint.connections[0].b.partOrdinal, 2u);
        EXPECT_EQ(encoded(model->snapshot()), before); EXPECT_EQ(allocator.lastIssued(), 1000u);
    }
}

TEST(BuildModel, CoarseUiStepsRotateStudAndPlateAxesWithoutChangingCanonicalOffsets) {
    EXPECT_EQ(coarsePlacementOffset({-2, 3, 1}), (GridPosition{-100, 48, 50}));
    const auto sideways = coarsePlacementOffset({0, 1, 0}, CubeRotation{4});
    ASSERT_TRUE(sideways); EXPECT_EQ(*sideways, *rotate(CubeRotation{4}, {0, 16, 0}));
    EXPECT_EQ(sideways->y, 0);
    EXPECT_FALSE(coarsePlacementOffset({std::numeric_limits<int32_t>::max(), 0, 0}));
    EXPECT_FALSE(coarsePlacementOffset({0, 0, 0}, CubeRotation{24}));
}

} // namespace

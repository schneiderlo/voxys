#include "game/construction/assembly_functions.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <stdexcept>

namespace voxy::game::construction {
namespace {
constexpr WorldNamespace kWorld{{'f','u','n','c','t','i','o','n','-','t','e','s','t','-','0','1'}};
DurableId id(uint64_t n) { return {kWorld, n}; }
PartCatalog validated(PartCatalogDraft draft) {
    CatalogIssue issue; auto result = PartCatalog::create(draft, issue);
    if (!result) throw std::runtime_error("catalog fixture " + std::to_string(static_cast<int>(issue.error)));
    return std::move(*result);
}
const PartCatalog& catalog() { static const auto result = validated(makeStarterCatalogDraft()); return result; }
BuildSnapshot column(const std::vector<StarterPart>& kinds, const PartCatalog& definitions = catalog()) {
    BuildSnapshot build; build.id = id(1); build.owner = id(2);
    int32_t top = 0;
    for (size_t i = 0; i < kinds.size(); ++i) {
        const auto& definition = *definitions.lookup(starterPartKey(kinds[i])).definition;
        PartInstance part; part.id = id(10 + i); part.owningBuild = build.id; part.definition = definition.key;
        part.settings = defaultModuleSettings(definition);
        part.placement.translation.y = i == 0 ? 0 : top - definition.footprint.minimum.y;
        top = part.placement.translation.y + definition.footprint.maximum.y;
        if (i > 0) {
            Connection link; link.id = id(1000 + i); link.a = {id(9 + i), SocketId{1}}; link.b = {part.id, SocketId{2}};
            link.strength = {1000, 900, 800, 700}; build.connections.push_back(link);
        }
        build.parts.push_back(part);
    }
    return build;
}
std::vector<StarterPart> allKinds() {
    return {StarterPart::Beam, StarterPart::Plate, StarterPart::Pontoon, StarterPart::Engine, StarterPart::Propeller,
        StarterPart::Helm, StarterPart::Winch, StarterPart::TowEye, StarterPart::CargoCradle,
        StarterPart::Brace, StarterPart::Ballast, StarterPart::RepairModule};
}
AssemblyFunctionPlan compile(const BuildSnapshot& build, const PartCatalog& definitions = catalog()) {
    AssemblyFunctionIssue issue; auto result = AssemblyFunctionPlan::compile(build, definitions, issue);
    if (!result) throw std::runtime_error("functional fixture " + std::to_string(static_cast<int>(issue.error))
        + "/" + std::to_string(static_cast<int>(issue.buoyancy.collision.assembly.error))
        + "/" + std::to_string(static_cast<int>(issue.buoyancy.collision.assembly.build.error)));
    return std::move(*result);
}
void strengthEqual(StrengthLimits a, StrengthLimits b) {
    EXPECT_EQ(a.tensionNewtons, b.tensionNewtons); EXPECT_EQ(a.shearNewtons, b.shearNewtons);
    EXPECT_EQ(a.bendingNewtonMetres, b.bendingNewtonMetres); EXPECT_EQ(a.torsionNewtonMetres, b.torsionNewtonMetres);
}
// Dense integer matrix oracle, not compose()/rotate()/transformPosition().
std::array<int64_t, 3> dense(CubeRotation rotation, GridPosition point) {
    const auto matrix = rotationMatrix(rotation)->elements;
    const std::array<int64_t, 3> input{point.x, point.y, point.z}; std::array<int64_t, 3> output{};
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 3; ++c) output[r] += matrix[r * 3 + c] * input[c];
    return output;
}
void expectComposition(GridTransform output, GridTransform part, GridTransform local, GridPosition rootAnchor) {
    auto position = dense(part.rotation, local.translation);
    position[0] += static_cast<int64_t>(part.translation.x) - rootAnchor.x;
    position[1] += static_cast<int64_t>(part.translation.y) - rootAnchor.y;
    position[2] += static_cast<int64_t>(part.translation.z) - rootAnchor.z;
    EXPECT_EQ(output.translation.x, position[0]); EXPECT_EQ(output.translation.y, position[1]); EXPECT_EQ(output.translation.z, position[2]);
    const auto a = rotationMatrix(part.rotation)->elements, b = rotationMatrix(local.rotation)->elements;
    const auto observed = rotationMatrix(output.rotation)->elements;
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 3; ++c) {
        int expected = 0;
        for (size_t k = 0; k < 3; ++k) expected += a[r * 3 + k] * b[k * 3 + c];
        EXPECT_EQ(observed[r * 3 + c], expected);
    }
}
static_assert(!std::is_default_constructible_v<AssemblyFunctionPlan>);
static_assert(std::is_same_v<decltype(std::declval<AssemblyFunctionPlan&>().modules()), std::span<const AssemblyModuleBinding>>);

TEST(AssemblyFunctions, EveryModuleKindRetainsParametersSettingsAndOwnedFrames) {
    const auto build = column(allKinds()); const auto result = compile(build);
    ASSERT_EQ(result.modules().size(), 12u); ASSERT_EQ(result.frames().size(), 19u); ASSERT_EQ(result.massPlan().roots().size(), 1u);
    const std::array<uint32_t, 12> counts{1,1,1,2,3,2,2,2,2,1,1,1};
    size_t sockets = 0;
    for (size_t i = 0; i < build.parts.size(); ++i) {
        const auto& definition = *catalog().lookup(build.parts[i].definition).definition;
        const auto& module = result.modules()[i]; ASSERT_EQ(result.module(build.parts[i].id), &module);
        EXPECT_EQ(module.part, build.parts[i].id); EXPECT_EQ(module.root, 0u);
        EXPECT_EQ(module.parameters.index(), definition.module.index()); EXPECT_EQ(module.settings, build.parts[i].settings);
        EXPECT_EQ(module.health, kFullHealth); strengthEqual(module.strength, definition.strength);
        EXPECT_EQ(module.frameCount, counts[i]); EXPECT_EQ(module.firstSocket, sockets); sockets += definition.sockets.size();
        const auto frames = result.moduleFrames(i); ASSERT_EQ(frames.size(), counts[i]);
        EXPECT_EQ(frames[0].kind, AssemblyFrameKind::PartOrigin); EXPECT_EQ(frames[0].rootFromFrame, result.massPlan().parts()[i].rootFromPart);
        EXPECT_EQ(frames[0].socket, kNoAssemblySocket);
        for (const auto& frame : frames) EXPECT_EQ(frame.module, i);
    }
    EXPECT_EQ(result.sockets().size(), sockets);
    EXPECT_EQ((std::get<FlotationModule>(result.modules()[2].parameters).dragCoefficients), (std::array<double,3>{0.9,1.2,0.4}));
    const auto engine = std::get<EngineModule>(result.modules()[3].parameters);
    EXPECT_EQ(engine.shaft, SocketId{10}); EXPECT_EQ(engine.maximumPowerWatts, 18000.0); EXPECT_EQ(engine.maximumTorqueNewtonMetres, 500.0);
    const auto propeller = std::get<PropellerModule>(result.modules()[4].parameters);
    EXPECT_EQ(propeller.maximumThrustNewtons, 2500.0); EXPECT_EQ(propeller.requiredPowerWatts, 18000.0);
    EXPECT_EQ(std::get<HelmModule>(result.modules()[5].parameters).maximumSteeringRadians, 0.6);
    const auto winch = std::get<WinchModule>(result.modules()[6].parameters);
    EXPECT_EQ(winch.minimumLengthMetres, 0.5); EXPECT_EQ(winch.maximumLengthMetres, 40.0);
    EXPECT_EQ(winch.reelSpeedMetresPerSecond, 2.0); EXPECT_EQ(winch.maximumForceNewtons, 12000.0);
    EXPECT_EQ(std::get<TowEyeModule>(result.modules()[7].parameters).eye, SocketId{10});
    const auto cradle = std::get<CargoCradleModule>(result.modules()[8].parameters);
    EXPECT_EQ(cradle.maximumCargoMassKg, 3000.0); EXPECT_EQ(cradle.captureDistanceMetres, 0.1);
    EXPECT_EQ(cradle.captureAngleRadians, 0.08726646259971647); EXPECT_EQ(cradle.captureSpeedMetresPerSecond, 0.5);
    EXPECT_EQ(cradle.captureAngularSpeedRadiansPerSecond, 0.5235987755982988);
    EXPECT_EQ(std::get<BraceModule>(result.modules()[9].parameters).loadTransferFactor, 1.5);
    const auto repair = std::get<RepairModule>(result.modules()[11].parameters);
    EXPECT_EQ(repair.reachMetres, 3.0); EXPECT_EQ(repair.healthFractionPerSecond, 0.1); EXPECT_EQ(repair.materialUnitsPerFullHealth, 12u);
    EXPECT_EQ(result.module(id(99999)), nullptr); EXPECT_EQ(result.socket({id(10), SocketId{9999}}), nullptr);
    EXPECT_TRUE(result.moduleFrames(12).empty());
}

TEST(AssemblyFunctions, AllRotationsAndLargeOffsetsMatchDenseFrameAndThrustOracle) {
    auto draft = makeStarterCatalogDraft();
    std::get<PropellerModule>(draft.definitions[4].module).forceFrame = {{3,-5,7},{4}};
    std::get<HelmModule>(draft.definitions[5].module).operatorFrame = {{-7,11,5},{9}};
    const auto definitions = validated(draft); const auto original = column(allKinds(), definitions);
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        auto build = original;
        const GridTransform global{{2000000000,-2000000000,1500000000},{rotation}};
        for (auto& part : build.parts) part.placement = *compose(global, part.placement);
        const auto result = compile(build, definitions); const auto anchor = build.parts[0].placement.translation;
        for (size_t i = 0; i < build.parts.size(); ++i) {
            const auto& part = build.parts[i]; const auto& definition = *definitions.lookup(part.definition).definition;
            for (const auto& source : definition.sockets) {
                const auto* socket = result.socket({part.id, source.id}); ASSERT_NE(socket, nullptr);
                expectComposition(socket->rootFromSocket, part.placement, source.frame, anchor);
                EXPECT_EQ(socket->definition.clearance, source.clearance); EXPECT_EQ(socket->definition.frame, source.frame);
                strengthEqual(socket->definition.strength, source.strength);
            }
            for (const auto& frame : result.moduleFrames(i)) {
                GridTransform local{};
                if (frame.socket != kNoAssemblySocket) {
                    ASSERT_LT(frame.socket, result.sockets().size()); const auto& source = result.sockets()[frame.socket];
                    EXPECT_EQ(source.endpoint.part, part.id); EXPECT_EQ(source.root, result.modules()[i].root); local = source.definition.frame;
                } else if (frame.kind == AssemblyFrameKind::Thrust) local = std::get<PropellerModule>(definition.module).forceFrame;
                else if (frame.kind == AssemblyFrameKind::Operator) local = std::get<HelmModule>(definition.module).operatorFrame;
                expectComposition(frame.rootFromFrame, part.placement, local, anchor);
                if (frame.kind == AssemblyFrameKind::Thrust) {
                    const auto localDirection = dense(local.rotation, {0,0,-1});
                    const auto expected = dense(part.placement.rotation, {static_cast<int32_t>(localDirection[0]),
                        static_cast<int32_t>(localDirection[1]),static_cast<int32_t>(localDirection[2])});
                    EXPECT_EQ(dense(frame.rootFromFrame.rotation, {0,0,-1}), expected);
                }
            }
        }
    }
}

TEST(AssemblyFunctions, EndpointReversalAndInsertionOrderPreserveCanonicalBindings) {
    auto build = column(allKinds()); const auto result = compile(build);
    std::reverse(build.parts.begin(), build.parts.end()); std::reverse(build.connections.begin(), build.connections.end());
    for (auto& link : build.connections) std::swap(link.a, link.b);
    const auto reversed = compile(build);
    EXPECT_TRUE(std::ranges::equal(result.frames(), reversed.frames()));
    for (size_t i = 0; i < result.sockets().size(); ++i) {
        EXPECT_EQ(result.sockets()[i].endpoint, reversed.sockets()[i].endpoint);
        EXPECT_EQ(result.sockets()[i].rootFromSocket, reversed.sockets()[i].rootFromSocket);
        EXPECT_EQ(result.sockets()[i].usedSlots, reversed.sockets()[i].usedSlots);
    }
    for (size_t i = 0; i < result.connections().size(); ++i) {
        const auto& a = result.connections()[i]; const auto& b = reversed.connections()[i];
        EXPECT_EQ(a.definition.id, b.definition.id); EXPECT_EQ(a.definition.a, b.definition.a); EXPECT_EQ(a.definition.b, b.definition.b);
        EXPECT_EQ(a.socketA, b.socketA); EXPECT_EQ(a.socketB, b.socketB);
        EXPECT_EQ(result.sockets()[a.socketA].root, result.sockets()[a.socketB].root);
    }
}

TEST(AssemblyFunctions, DisabledWeldRetainsEndpointsConditionAndReservedSlotsAcrossRoots) {
    auto build = column({StarterPart::Beam, StarterPart::Beam}); auto& link = build.connections[0];
    link.enabled = false; link.damage = 10000;
    const auto result = compile(build); ASSERT_EQ(result.massPlan().roots().size(), 2u);
    const auto& output = result.connections()[0]; EXPECT_FALSE(output.definition.enabled); EXPECT_EQ(output.definition.damage, 10000u);
    EXPECT_EQ(output.definition.kind, ConnectionKind::Weld); strengthEqual(output.definition.strength, link.strength);
    EXPECT_NE(result.sockets()[output.socketA].root, result.sockets()[output.socketB].root);
    EXPECT_EQ(result.sockets()[output.socketA].usedSlots, 1u); EXPECT_EQ(result.sockets()[output.socketB].usedSlots, 1u);
}

TEST(AssemblyFunctions, RopePreservesConnectedLengthAcrossSeparateAndSharedRoots) {
    auto build = column({StarterPart::Winch, StarterPart::TowEye});
    Connection rope; rope.id = id(2000); rope.a = {id(10), SocketId{10}}; rope.b = {id(11), SocketId{10}};
    rope.kind = ConnectionKind::Rope; rope.strength = {1100,900,800,700}; rope.damage = 231;
    rope.minimumLengthMillimetres = 500; rope.maximumLengthMillimetres = 40000; rope.restLengthMillimetres = 17000;
    build.parts[0].settings.defaultLineLengthMillimetres = 2000; build.connections.push_back(rope);
    for (bool weld : {false, true}) {
        build.connections[0].enabled = weld; const auto result = compile(build);
        ASSERT_EQ(result.massPlan().roots().size(), weld ? 1u : 2u);
        const auto& output = result.connections()[1]; EXPECT_EQ(output.definition.id, rope.id);
        EXPECT_EQ(output.definition.minimumLengthMillimetres, 500u); EXPECT_EQ(output.definition.maximumLengthMillimetres, 40000u);
        EXPECT_EQ(output.definition.restLengthMillimetres, 17000u); EXPECT_EQ(output.definition.damage, 231u);
        strengthEqual(output.definition.strength, rope.strength);
        EXPECT_EQ(result.modules()[0].settings.defaultLineLengthMillimetres, 2000u);
        EXPECT_EQ(result.sockets()[output.socketA].root == result.sockets()[output.socketB].root, weld);
        EXPECT_EQ(result.moduleFrames(0)[1].kind, AssemblyFrameKind::TowLine);
        EXPECT_EQ(result.moduleFrames(1)[1].kind, AssemblyFrameKind::TowEye);
    }
}

TEST(AssemblyFunctions, AuthoredLatchDoesNotMergeCargoOrDeclareCapture) {
    auto draft = makeStarterCatalogDraft(); auto cargo = draft.definitions[8];
    cargo.key.id.counter = 500; cargo.nameKey = "salvage.part.latch_frame_fixture"; cargo.module = StructureModule{};
    cargo.sockets.back().role = SocketRole::Plug; draft.definitions.push_back(cargo); const auto definitions = validated(draft);
    auto build = column({StarterPart::CargoCradle, StarterPart::CargoCradle}, definitions);
    build.parts[1].definition = cargo.key; build.connections.clear(); build.parts[1].placement.translation = {1000,200,300};
    Connection latch; latch.id = id(2000); latch.kind = ConnectionKind::Latch;
    latch.a = {id(10),SocketId{10}}; latch.b = {id(11),SocketId{10}}; latch.strength = {1000,900,800,700};
    build.connections.push_back(latch); const auto result = compile(build, definitions);
    ASSERT_EQ(result.massPlan().roots().size(), 2u); const auto& link = result.connections()[0];
    EXPECT_EQ(link.definition.kind, ConnectionKind::Latch); EXPECT_NE(result.sockets()[link.socketA].root, result.sockets()[link.socketB].root);
    EXPECT_EQ(result.massPlan().roots()[0].mass.dryMassKg, 90.0); EXPECT_EQ(result.massPlan().roots()[1].mass.dryMassKg, 90.0);
    EXPECT_EQ(result.moduleFrames(0)[1].kind, AssemblyFrameKind::CargoLatch);
}

TEST(AssemblyFunctions, CapacityProfilesAndNestedValidationFailExplicitly) {
    const auto build = column(allKinds()); const auto expected = compile(build); AssemblyFunctionIssue issue;
    AssemblyFunctionLimits exact{12, expected.sockets().size(), 11, 19};
    ASSERT_TRUE(AssemblyFunctionPlan::compile(build,catalog(),issue,{},{},{},exact));
    for (size_t field = 0; field < 4; ++field) {
        auto limits = exact; std::array<size_t*,4> values{&limits.modules,&limits.sockets,&limits.connections,&limits.frames}; --*values[field];
        EXPECT_FALSE(AssemblyFunctionPlan::compile(build,catalog(),issue,{},{},{},limits)); EXPECT_EQ(issue.error,AssemblyFunctionError::Capacity);
        limits = {}; values = {&limits.modules,&limits.sockets,&limits.connections,&limits.frames}; ++*values[field];
        EXPECT_FALSE(AssemblyFunctionPlan::compile(build,catalog(),issue,{},{},{},limits)); EXPECT_EQ(issue.error,AssemblyFunctionError::InvalidProfile);
    }
    auto invalid = build; invalid.parts[3].settings.kind = SettingsKind::Passive;
    EXPECT_FALSE(AssemblyFunctionPlan::compile(invalid,catalog(),issue));
    EXPECT_EQ(issue.buoyancy.collision.assembly.build.error,BuildError::InvalidSettings);
    auto mass = AssemblyMassLimits{}; mass.roots = 0; EXPECT_FALSE(AssemblyFunctionPlan::compile(build,catalog(),issue,mass));
    EXPECT_EQ(issue.buoyancy.collision.assembly.error,AssemblyError::InvalidProfile);
    EXPECT_EQ(issue.error,AssemblyFunctionError::None);
}

TEST(AssemblyFunctions, FullPartAndFunctionalFrameBudgetsCompileWithoutTruncation) {
    const auto build = column(std::vector<StarterPart>(256,StarterPart::Propeller)); const auto result = compile(build);
    EXPECT_EQ(result.modules().size(),256u); EXPECT_EQ(result.frames().size(),768u); EXPECT_EQ(result.sockets().size(),768u);
    EXPECT_EQ(result.connections().size(),255u); EXPECT_EQ(result.moduleFrames(255)[2].module,255u);
    EXPECT_EQ(result.moduleFrames(255)[2].kind,AssemblyFrameKind::Thrust);
}

TEST(AssemblyFunctions, MaximumSocketAndConnectionBudgetsRemainGloballyBounded) {
    auto draft = makeStarterCatalogDraft(); auto& beam = draft.definitions[0];
    while (beam.sockets.size() < 32) {
        auto extra = beam.sockets[0]; extra.id = SocketId{200 + beam.sockets.size()}; extra.family = SocketFamily::TowLine;
        extra.role = beam.sockets.size() % 2 == 0 ? SocketRole::Plug : SocketRole::Receptacle; beam.sockets.push_back(extra);
    }
    const auto definitions = validated(draft); auto build = column(std::vector<StarterPart>(256,StarterPart::Beam),definitions);
    auto denseWelds = build;
    for (size_t i = 1; i < denseWelds.parts.size(); ++i) for (uint64_t socket = 100; socket < 108 && denseWelds.connections.size() < 1024; socket += 2) {
        Connection link; link.id = id(2000 + denseWelds.connections.size()); link.a = {id(9+i),SocketId{socket}}; link.b = {id(10+i),SocketId{socket+1}};
        link.strength = {1000,900,800,700}; denseWelds.connections.push_back(link);
    }
    AssemblyFunctionIssue issue;
    EXPECT_FALSE(AssemblyFunctionPlan::compile(denseWelds,definitions,issue));
    EXPECT_EQ(issue.buoyancy.collision.assembly.build.error,BuildError::Capacity);
    EXPECT_EQ(issue.buoyancy.collision.assembly.build.field,"candidatePairs");
    // 255 actual welds plus authored ropes reach the record limits within the
    // separate canonical mating-work budget. 1,024 welds exceeded that budget;
    // record capacity alone never promises admission of every dense design.
    for (size_t i = 1; i < build.parts.size(); ++i) for (uint64_t socket = 210; socket < 232 && build.connections.size() < 1024; socket += 2) {
        Connection link; link.id = id(2000 + build.connections.size()); link.a = {id(9+i),SocketId{socket}}; link.b = {id(10+i),SocketId{socket+1}};
        link.kind = ConnectionKind::Rope; link.minimumLengthMillimetres = 1; link.restLengthMillimetres = 1000; link.maximumLengthMillimetres = 2000;
        link.strength = {1000,900,800,700}; build.connections.push_back(link);
    }
    const auto result = compile(build,definitions); EXPECT_EQ(result.sockets().size(),8192u); EXPECT_EQ(result.connections().size(),1024u);
    size_t used = 0; for (const auto& socket : result.sockets()) used += socket.usedSlots; EXPECT_EQ(used,2048u);
    AssemblyFunctionLimits limits; limits.sockets = 8191;
    EXPECT_FALSE(AssemblyFunctionPlan::compile(build,definitions,issue,{},{},{},limits)); EXPECT_EQ(issue.error,AssemblyFunctionError::Capacity);
    limits = {}; limits.connections = 1023;
    EXPECT_FALSE(AssemblyFunctionPlan::compile(build,definitions,issue,{},{},{},limits)); EXPECT_EQ(issue.error,AssemblyFunctionError::Capacity);
}

TEST(AssemblyFunctions, CurrentCatalogAndInstanceSettingsAreCopiedWithoutBorrowing) {
    auto draft = makeStarterCatalogDraft();
    auto build = column({StarterPart::Engine}); build.parts[0].health = 0;
    build.parts[0].settings.enabled = false; build.parts[0].settings.reversed = true;
    build.parts[0].settings.controlChannel = 15; build.parts[0].settings.limitPermille = 237;
    build.parts[0].provenance = {PartOrigin::StarterLoan,id(99999)};
    const auto old = compile(build); std::get<EngineModule>(draft.definitions[3].module).maximumPowerWatts = 12345.0;
    const auto fresh = [&] { const auto definitions = validated(draft); return compile(build,definitions); }();
    build.parts.clear(); build.connections.clear(); draft.definitions.clear();
    EXPECT_EQ(std::get<EngineModule>(old.modules()[0].parameters).maximumPowerWatts,18000.0);
    EXPECT_EQ(std::get<EngineModule>(fresh.modules()[0].parameters).maximumPowerWatts,12345.0);
    EXPECT_EQ(fresh.modules()[0].health,0u); EXPECT_FALSE(fresh.modules()[0].settings.enabled); EXPECT_TRUE(fresh.modules()[0].settings.reversed);
    EXPECT_EQ(fresh.modules()[0].settings.limitPermille,237u); EXPECT_EQ(fresh.modules()[0].settings.controlChannel,15u);
    EXPECT_EQ(fresh.massPlan().roots()[0].mass.dryMassKg,160.0);
    const auto copied = fresh; EXPECT_TRUE(std::ranges::equal(copied.frames(),fresh.frames()));
    EXPECT_NE(copied.frames().data(),fresh.frames().data()); EXPECT_NE(copied.sockets().data(),fresh.sockets().data());
}
} // namespace
} // namespace voxy::game::construction

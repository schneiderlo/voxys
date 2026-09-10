#include "game/construction/compiled_assembly.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace voxy::game::construction {
namespace {
constexpr WorldNamespace kWorld{{'c','a','c','h','e','-','f','i','x','t','u','r','e','-','0','1'}};
DurableId id(uint64_t n) { return {kWorld,n}; }
PartCatalogDraft goldenDraft() {
    PartDefinition part; part.key={id(1000),7}; part.nameKey="salvage.part.cache_fixture";
    part.footprint={{-10,-8,-6},{10,8,6}};
    part.solidOccupancy={{ProxyId{1},{{1,-1,0},{}},{4,3,2}}}; part.collision={{ProxyId{3},{},{10,8,6}}};
    part.mass={4.0,{.02,-.02,0.0},{{.02,0,0,0,.03,0,0,0,.04}}};
    part.buoyancy={{{ProxyId{5},{{2,-1,1},{4}},{4,3,2}},BuoyancyKind::SealedCompartment}};
    SocketDefinition socket; socket.id=SocketId{7}; socket.role=SocketRole::Plug;
    socket.frame={{0,8,0},{}}; socket.connectionCapacity=2; socket.clearance={{-1,-1,-1},{1,1,1}};
    socket.strength={100,90,80,70}; part.sockets={socket}; part.strength={200,180,160,140};
    part.module=RepairModule{2.25,.125,3}; part.cost={10,0}; part.salvageYield={3,0};
    part.visuals={{PrototypeBoxVisual{prototypeBoxAsset(),part.footprint},0}};
    return {kPartCatalogSchemaVersion,{part}};
}
BuildSnapshot goldenBuild() {
    BuildSnapshot build; build.id=id(1); build.owner=id(2); build.revision=TopologyRevision{9};
    PartInstance part; part.id=id(10); part.owningBuild=build.id; part.definition={id(1000),7};
    part.placement={{123,-77,55},{9}}; part.health=8123; part.settings.enabled=false; build.parts={part}; return build;
}
PartCatalog validated(PartCatalogDraft draft) {
    CatalogIssue issue; auto result=PartCatalog::create(draft,issue);
    if (!result) throw std::runtime_error("hash catalog fixture "+std::to_string(static_cast<int>(issue.error))+"/"+std::string(issue.field));
    return std::move(*result);
}
CompiledAssembly compile(BuildSnapshot build=goldenBuild(),PartCatalogDraft draft=goldenDraft(),AssemblyCompileProfile profile={}) {
    const auto catalog=validated(std::move(draft)); AssemblyFunctionIssue issue;
    auto result=CompiledAssembly::compile(build,catalog,issue,profile);
    if (!result) throw std::runtime_error("compiled fixture "+std::to_string(static_cast<int>(issue.error))
        +"/"+std::to_string(static_cast<int>(issue.buoyancy.collision.assembly.build.error)));
    return std::move(*result);
}
BuildSnapshot starterBuild(StarterPart kind) {
    const auto catalog=validated(makeStarterCatalogDraft()); const auto& definition=*catalog.lookup(starterPartKey(kind)).definition;
    auto build=goldenBuild(); auto& part=build.parts[0]; part.definition=definition.key; part.placement={};
    part.settings=defaultModuleSettings(definition); return build;
}
static_assert(!std::is_default_constructible_v<CompiledAssembly>);
static_assert(std::is_same_v<decltype(std::declval<CompiledAssembly&>().functions()),const AssemblyFunctionPlan&>);

TEST(CompiledAssembly, PhysicalInputIdentityMatchesIndependent670ByteFixture) {
    const auto result=compile();
    // Generated from explicitly specified values with Python struct/hashlib,
    // independently of production C++: stage-e1/golden-input.py and .bin.
    EXPECT_EQ(core::sha256Hex(result.inputIdentity()),"8c9012e1400f5e21fe00fdb6ba3e9b2aed1e5155074800e770de2d0a773452e0");
    EXPECT_EQ(result.mass().build(),id(1)); EXPECT_EQ(result.mass().revision(),TopologyRevision{9});
    ASSERT_EQ(result.mass().roots().size(),1u); EXPECT_EQ(result.mass().roots()[0].mass.dryMassKg,4.0);
    EXPECT_EQ(result.functions().modules()[0].health,8123u);
    EXPECT_EQ(result.collision().massPlan().parts()[0].part,id(10)); EXPECT_EQ(result.buoyancy().sources()[0].region,ProxyId{5});
}

TEST(CompiledAssembly, PhysicalDefinitionFieldsChangeIdentityEvenWithUnchangedKey) {
    const auto base=compile().inputIdentity();
    const std::vector<std::function<void(PartDefinition&)>> edits{
        [](auto& p){ p.permittedRotationMask &= ~(1u<<23); },
        [](auto& p){ p.footprint.maximum.x=11; std::get<PrototypeBoxVisual>(p.visuals[0].asset).bounds=p.footprint; },
        [](auto& p){ p.solidOccupancy[0].id=ProxyId{2}; },
        [](auto& p){ p.solidOccupancy[0].frame.translation.x=2; },
        [](auto& p){ p.solidOccupancy[0].frame.rotation={4}; },
        [](auto& p){ p.solidOccupancy[0].halfExtents.x=3; },
        [](auto& p){ p.collision[0].id=ProxyId{4}; },
        [](auto& p){ p.collision[0].halfExtents.x=9; },
        [](auto& p){ p.mass.dryMassKg=4.1; },
        [](auto& p){ p.mass.localCenterOfMass.x=.03; },
        [](auto& p){ p.mass.inertia.elements[0]=.021; },
        [](auto& p){ p.mass.inertia.elements[1]=p.mass.inertia.elements[3]=.001; },
        [](auto& p){ p.buoyancy[0].box.id=ProxyId{6}; },
        [](auto& p){ p.buoyancy[0].kind=BuoyancyKind::SolidMaterial; },
        [](auto& p){ p.buoyancy[0].box.frame.translation.x=3; },
        [](auto& p){ p.buoyancy[0].box.frame.rotation={0}; },
        [](auto& p){ p.buoyancy[0].box.halfExtents.x=3; },
        [](auto& p){ p.sockets[0].id=SocketId{8}; },
        [](auto& p){ p.sockets[0].family=SocketFamily::TowLine; },
        [](auto& p){ p.sockets[0].role=SocketRole::Receptacle; },
        [](auto& p){ p.sockets[0].profile=2; },
        [](auto& p){ p.sockets[0].frame.translation.x=1; },
        [](auto& p){ p.sockets[0].frame.rotation={4}; },
        [](auto& p){ p.sockets[0].connectionCapacity=3; },
        [](auto& p){ p.sockets[0].clearance.maximum.x=2; },
        [](auto& p){ p.sockets[0].strength.tensionNewtons=101; },
        [](auto& p){ p.sockets[0].strength.shearNewtons=91; },
        [](auto& p){ p.sockets[0].strength.bendingNewtonMetres=81; },
        [](auto& p){ p.sockets[0].strength.torsionNewtonMetres=71; },
        [](auto& p){ p.strength.tensionNewtons=201; },
        [](auto& p){ p.strength.shearNewtons=181; },
        [](auto& p){ p.strength.bendingNewtonMetres=161; },
        [](auto& p){ p.strength.torsionNewtonMetres=141; },
        [](auto& p){ std::get<RepairModule>(p.module).reachMetres=2.5; },
        [](auto& p){ std::get<RepairModule>(p.module).healthFractionPerSecond=.25; },
        [](auto& p){ std::get<RepairModule>(p.module).materialUnitsPerFullHealth=4; },
        [](auto& p){ p.module=StructureModule{}; },
    };
    for (size_t i=0;i<edits.size();++i) { SCOPED_TRACE(i); auto draft=goldenDraft(); edits[i](draft.definitions[0]); EXPECT_NE(compile(goldenBuild(),draft).inputIdentity(),base); }
}

TEST(CompiledAssembly, EveryProfileFieldParticipatesAndInvalidProfilesCannotProduceAnIdentity) {
    const auto base=compile().inputIdentity();
    const std::vector<std::function<void(AssemblyCompileProfile&)>> edits{
        [](auto& p){ --p.mass.parts; },[](auto& p){ --p.mass.roots; },[](auto& p){ --p.mass.radiusTicks; },
        [](auto& p){ --p.collision.inputBoxes; },[](auto& p){ --p.collision.cells; },[](auto& p){ --p.collision.faces; },
        [](auto& p){ --p.collision.scratchPieces; },[](auto& p){ --p.collision.clipTests; },[](auto& p){ --p.collision.radiusTicks; },
        [](auto& p){ --p.buoyancy.inputBoxes; },[](auto& p){ --p.buoyancy.cells; },[](auto& p){ --p.buoyancy.references; },
        [](auto& p){ --p.buoyancy.scratchPieces; },[](auto& p){ --p.buoyancy.clipTests; },[](auto& p){ --p.buoyancy.referenceWrites; },
        [](auto& p){ --p.buoyancy.radiusTicks; },[](auto& p){ --p.functions.modules; },[](auto& p){ --p.functions.sockets; },
        [](auto& p){ --p.functions.connections; },[](auto& p){ --p.functions.frames; },
    };
    for (size_t i=0;i<edits.size();++i) { SCOPED_TRACE(i); AssemblyCompileProfile profile; edits[i](profile); EXPECT_NE(compile(goldenBuild(),goldenDraft(),profile).inputIdentity(),base); }
    const auto catalog=validated(goldenDraft()); AssemblyFunctionIssue issue; AssemblyCompileProfile bad; bad.mass.parts=0;
    EXPECT_FALSE(CompiledAssembly::compile(goldenBuild(),catalog,issue,bad)); EXPECT_EQ(issue.buoyancy.collision.assembly.error,AssemblyError::InvalidProfile);
    auto invalid=goldenBuild(); invalid.parts[0].owningBuild=id(99);
    EXPECT_FALSE(CompiledAssembly::compile(invalid,catalog,issue)); EXPECT_EQ(issue.buoyancy.collision.assembly.build.error,BuildError::WrongBuild);
}

TEST(CompiledAssembly, TopologyIdentityPlacementAndInstanceSettingsAreBound) {
    const auto base=compile().inputIdentity();
    for (int change=0;change<5;++change) {
        auto build=goldenBuild();
        if (change==0) { build.id=id(3); build.parts[0].owningBuild=build.id; }
        if (change==1) build.revision=TopologyRevision{10};
        if (change==2) build.parts[0].id=id(11);
        if (change==3) ++build.parts[0].placement.translation.x;
        if (change==4) --build.parts[0].health;
        EXPECT_NE(compile(build).inputIdentity(),base);
    }
    for (uint8_t rotation=0;rotation<24;++rotation) if (rotation!=9) {
        auto build=goldenBuild(); build.parts[0].placement.rotation={rotation}; EXPECT_NE(compile(build).inputIdentity(),base);
    }
    auto build=starterBuild(StarterPart::Engine); const auto powerBase=compile(build,makeStarterCatalogDraft()).inputIdentity();
    for (int change=0;change<4;++change) {
        auto changed=build; auto& s=changed.parts[0].settings;
        if (change==0) s.enabled=false;
        if (change==1) s.controlChannel=1;
        if (change==2) s.limitPermille=999;
        if (change==3) s.reversed=true;
        EXPECT_NE(compile(changed,makeStarterCatalogDraft()).inputIdentity(),powerBase);
    }
    auto winch=starterBuild(StarterPart::Winch); const auto winchBase=compile(winch,makeStarterCatalogDraft()).inputIdentity();
    ++winch.parts[0].settings.defaultLineLengthMillimetres; EXPECT_NE(compile(winch,makeStarterCatalogDraft()).inputIdentity(),winchBase);
    auto versioned=goldenBuild(); auto draft=goldenDraft(); ++versioned.parts[0].definition.version; ++draft.definitions[0].key.version;
    EXPECT_NE(compile(versioned,draft).inputIdentity(),base);
}

TEST(CompiledAssembly, CosmeticAuthorityAndUnusedContentChangesDoNotChangePhysicalIdentity) {
    const auto base=compile().inputIdentity(); auto draft=goldenDraft(); auto build=goldenBuild();
    build.owner=id(99); build.editLease=EditLease{id(100),AuthorityEpoch{2},SimulationTick{9}};
    build.parts[0].paint={10,20,30,40}; build.parts[0].provenance={PartOrigin::StarterLoan,id(101)};
    auto& definition=draft.definitions[0]; definition.nameKey="salvage.part.renamed_fixture";
    definition.cost={20,1}; definition.salvageYield={7,1}; definition.material={{.1,.2,.3},.3,.9};
    auto unused=definition; unused.key.id=id(1001); unused.mass.dryMassKg=9; draft.definitions.push_back(unused);
    EXPECT_EQ(compile(build,draft).inputIdentity(),base);
    // Exclusion is not permission to bypass validation of canonical authority.
    build.parts[0].provenance.starterEntitlement={}; const auto catalog=validated(draft); AssemblyFunctionIssue issue;
    EXPECT_FALSE(CompiledAssembly::compile(build,catalog,issue)); EXPECT_EQ(issue.buoyancy.collision.assembly.build.error,BuildError::InvalidProvenance);
}

TEST(CompiledAssembly, FloatingZeroNormalizationAndCanonicalCollectionOrderAreStable) {
    auto draft=goldenDraft(); const auto base=compile().inputIdentity();
    draft.definitions[0].mass.localCenterOfMass.z=-0.0;
    for (auto& entry:draft.definitions[0].mass.inertia.elements) if (entry==0.0) entry=-0.0;
    EXPECT_EQ(compile(goldenBuild(),draft).inputIdentity(),base);
    auto build=starterBuild(StarterPart::Beam); auto second=build.parts[0]; second.id=id(11); second.placement.translation.y=48;
    build.parts.push_back(second); Connection link; link.id=id(20); link.a={id(10),SocketId{1}}; link.b={id(11),SocketId{2}};
    link.strength={1000,900,800,700}; build.connections={link}; auto starter=makeStarterCatalogDraft();
    const auto ordered=compile(build,starter).inputIdentity(); std::reverse(build.parts.begin(),build.parts.end());
    std::swap(build.connections[0].a,build.connections[0].b); std::reverse(starter.definitions.begin(),starter.definitions.end());
    for (auto& p:starter.definitions) {
        std::reverse(p.sockets.begin(),p.sockets.end()); std::reverse(p.collision.begin(),p.collision.end());
        std::reverse(p.solidOccupancy.begin(),p.solidOccupancy.end()); std::reverse(p.buoyancy.begin(),p.buoyancy.end());
    }
    EXPECT_EQ(compile(build,starter).inputIdentity(),ordered);
}

TEST(CompiledAssembly, ConnectionStateStrengthAndAttachedRopeLengthsChangeIdentity) {
    auto build=starterBuild(StarterPart::Winch); const auto catalog=validated(makeStarterCatalogDraft());
    auto eye=build.parts[0]; eye.id=id(11); eye.definition=starterPartKey(StarterPart::TowEye); eye.placement.translation.x=1000;
    eye.settings=defaultModuleSettings(*catalog.lookup(eye.definition).definition); build.parts.push_back(eye);
    Connection rope; rope.id=id(20); rope.a={id(10),SocketId{10}}; rope.b={id(11),SocketId{10}};
    rope.kind=ConnectionKind::Rope; rope.strength={1000,900,800,700}; rope.minimumLengthMillimetres=1000;
    rope.restLengthMillimetres=5000; rope.maximumLengthMillimetres=10000; build.connections={rope};
    const auto base=compile(build,makeStarterCatalogDraft()).inputIdentity();
    for (int change=0;change<10;++change) {
        auto changed=build; auto& link=changed.connections[0];
        if (change==0) link.id=id(21);
        if (change==1) link.enabled=false;
        if (change==2) link.damage=1;
        if (change==3) ++link.strength.tensionNewtons;
        if (change==4) ++link.strength.shearNewtons;
        if (change==5) ++link.strength.bendingNewtonMetres;
        if (change==6) ++link.strength.torsionNewtonMetres;
        if (change==7) ++link.minimumLengthMillimetres;
        if (change==8) ++link.maximumLengthMillimetres;
        if (change==9) ++link.restLengthMillimetres;
        EXPECT_NE(compile(changed,makeStarterCatalogDraft()).inputIdentity(),base);
    }
    build.connections.clear(); EXPECT_NE(compile(build,makeStarterCatalogDraft()).inputIdentity(),base);
}

TEST(CompiledAssembly, ClosedModuleVariantsBindTheirNumericParameters) {
    using Edit=std::function<void(PartModule&)>;
    const std::vector<std::pair<StarterPart,Edit>> edits{
        {StarterPart::Pontoon,[](auto& p){ ++std::get<FlotationModule>(p).dragCoefficients[0]; }},
        {StarterPart::Engine,[](auto& p){ ++std::get<EngineModule>(p).maximumPowerWatts; }},
        {StarterPart::Engine,[](auto& p){ ++std::get<EngineModule>(p).maximumTorqueNewtonMetres; }},
        {StarterPart::Propeller,[](auto& p){ ++std::get<PropellerModule>(p).maximumThrustNewtons; }},
        {StarterPart::Propeller,[](auto& p){ ++std::get<PropellerModule>(p).requiredPowerWatts; }},
        {StarterPart::Propeller,[](auto& p){ ++std::get<PropellerModule>(p).forceFrame.translation.x; }},
        {StarterPart::Helm,[](auto& p){ std::get<HelmModule>(p).maximumSteeringRadians=.7; }},
        {StarterPart::Helm,[](auto& p){ std::get<HelmModule>(p).operatorFrame.rotation={4}; }},
        {StarterPart::Winch,[](auto& p){ std::get<WinchModule>(p).minimumLengthMetres=.4; }},
        {StarterPart::Winch,[](auto& p){ ++std::get<WinchModule>(p).maximumLengthMetres; }},
        {StarterPart::Winch,[](auto& p){ ++std::get<WinchModule>(p).reelSpeedMetresPerSecond; }},
        {StarterPart::Winch,[](auto& p){ --std::get<WinchModule>(p).maximumForceNewtons; }},
        {StarterPart::CargoCradle,[](auto& p){ ++std::get<CargoCradleModule>(p).maximumCargoMassKg; }},
        {StarterPart::CargoCradle,[](auto& p){ std::get<CargoCradleModule>(p).captureDistanceMetres=.2; }},
        {StarterPart::CargoCradle,[](auto& p){ std::get<CargoCradleModule>(p).captureAngleRadians=.1; }},
        {StarterPart::CargoCradle,[](auto& p){ std::get<CargoCradleModule>(p).captureSpeedMetresPerSecond=.6; }},
        {StarterPart::CargoCradle,[](auto& p){ std::get<CargoCradleModule>(p).captureAngularSpeedRadiansPerSecond=.6; }},
        {StarterPart::Brace,[](auto& p){ std::get<BraceModule>(p).loadTransferFactor=1.6; }},
    };
    for (size_t i=0;i<edits.size();++i) {
        SCOPED_TRACE(i); const auto& [kind,edit]=edits[i]; auto draft=makeStarterCatalogDraft(); const auto build=starterBuild(kind);
        const auto base=compile(build,draft).inputIdentity(); edit(draft.definitions[static_cast<size_t>(kind)-1].module);
        EXPECT_NE(compile(build,draft).inputIdentity(),base);
    }
}

TEST(CompiledAssembly, CompleteOwnedOutputSurvivesSourceDestructionAndCopy) {
    const auto result=compile(); const auto copy=result;
    EXPECT_EQ(copy.inputIdentity(),result.inputIdentity()); EXPECT_NE(copy.functions().frames().data(),result.functions().frames().data());
    EXPECT_NE(copy.collision().roots()[0].shape.cells().data(),result.collision().roots()[0].shape.cells().data());
    EXPECT_NE(copy.buoyancy().roots()[0].coverage.references().data(),result.buoyancy().roots()[0].coverage.references().data());
    EXPECT_EQ(copy.profile().functions.frames,768u); EXPECT_EQ(copy.profile().collision.clipTests,8388608u);
}
} // namespace
} // namespace voxy::game::construction

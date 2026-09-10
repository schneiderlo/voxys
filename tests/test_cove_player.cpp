#include "game/expedition/cove_player.hpp"
#include "game/expedition/cove_boat.hpp"
#include "game/expedition/cove_rigid_roots.hpp"
#include "game/expedition/cove_harbor_lift.hpp"
#include "game/expedition/cove_harbor_runtime.hpp"
#include "game/expedition/cove_workshop.hpp"
#include "game/expedition/cove_build.hpp"
#include "game/expedition/cove_restore.hpp"
#include "game/expedition/game_session.hpp"
#include "game/construction/assembly_fracture.hpp"
#include "terrain/lego_surface.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>
#include <limits>
#if defined(VOXY_NATIVE)
#include "physics/gpu/gpu_physics_backend.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice,WGPUBool,const WGPUWrappedSubmissionIndex*);
#endif

namespace voxy::game::expedition {
namespace {
class CoveMovement : public testing::Test {
protected:
    std::unique_ptr<const assets::LoadedAssetFixture> scene;
    CovePlayer player;
    void SetUp() override {
        std::string error;
        // Bazel runfiles are symlinks. The test's byte provider reads those
        // trusted build inputs; production no-follow file admission is tested
        // separately and remains enabled in the actual browser/native app.
        std::ifstream file("data/salvage/fixture-cove-r01.json", std::ios::binary | std::ios::ate);
        ASSERT_TRUE(file);
        const auto size = file.tellg();
        ASSERT_GT(size, 0); ASSERT_LE(size, 65536);
        std::string json(static_cast<size_t>(size), '\0');
        file.seekg(0);
        ASSERT_TRUE(file.read(json.data(), size));
        auto registry = assets::parseAssetFixtureRegistry(json, error);
        ASSERT_TRUE(registry) << error;
        auto loaded = std::make_unique<assets::LoadedAssetFixture>();
        loaded->registry = *registry;
        for (const auto& spec : registry->bundles) {
            auto bundle = assets::admitCookedPartBundle(spec.selection,
                [directory = std::filesystem::path("data/salvage") / spec.directory]
                (std::string_view name, size_t maximum, std::string& reason)
                    -> std::optional<std::vector<uint8_t>> {
                    std::ifstream input(directory / name, std::ios::binary | std::ios::ate);
                    const auto bytes = input.tellg();
                    if (!input || bytes <= 0 || static_cast<uint64_t>(bytes) > maximum) {
                        reason = "missing/oversized cooked test input"; return std::nullopt;
                    }
                    std::vector<uint8_t> data(static_cast<size_t>(bytes));
                    input.seekg(0);
                    if (!input.read(reinterpret_cast<char*>(data.data()), bytes)) return std::nullopt;
                    return data;
                }, error);
            ASSERT_TRUE(bundle) << error;
            loaded->bundles.push_back(std::move(bundle));
        }
        scene = std::move(loaded);
        ASSERT_TRUE(player.initialize(*scene, [](double, double) { return -5.0; }, error)) << error;
    }
    void move(glm::dvec2 direction, int ticks) {
        for (int i = 0; i < ticks; ++i) player.advance(CovePlayer::fixedStep, {direction, false});
    }
    void approach() {
        move({-1, 0}, 25); // Walk beside the generator, not through it.
        move({0, -1}, 66);
    }
    void interact() {
        ASSERT_TRUE(player.requestInteraction());
        player.advance(CovePlayer::fixedStep, {});
    }
};

TEST_F(CoveMovement, BrickPointerPlacementRotatesStacksRefusesOverlapAndRestoresOwnedDesign) {
    using namespace construction;using Action=CoveWorkshop::Action;std::string error;
    const auto expanded=assets::appendAssetFixtureCatalog(*scene,std::filesystem::canonical("data/salvage/cove-bricks-r02.json"),error);
    ASSERT_TRUE(expanded)<<error;
    auto workshop=CoveWorkshop::create(*expanded,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_EQ(workshop->catalogCount(),11u);
    const auto choose=[&](std::string_view name) {
        for(uint32_t i=0;i<workshop->catalogCount();++i)if(workshop->catalogNameAt(i)==name)return workshop->selectCatalogAt(i);
        return false;
    };
    // Make an actual building bay without blocking the boarding/helm anchors.
    ASSERT_TRUE(workshop->selectPart(10));
    ASSERT_TRUE(workshop->command(Action::Remove));ASSERT_TRUE(workshop->command(Action::Keep));
    ASSERT_TRUE(choose("Brick 2 x 4"));ASSERT_TRUE(workshop->addPart());
    ASSERT_TRUE(workshop->valid())<<workshop->message();
    const auto first=workshop->selected();const auto base=workshop->preview().registry.placements[first].placement;
    ASSERT_TRUE(workshop->command(Action::Keep));
    ASSERT_TRUE(choose("Brick 1 x 2"));ASSERT_TRUE(workshop->addPart());
    const auto second=workshop->selected();
    ASSERT_TRUE(workshop->command(Action::Rotate));
    const auto rotated=workshop->preview().registry.placements[second].placement.rotation;
    const glm::dvec3 top=glm::dvec3(base.translation.x,base.translation.y,base.translation.z)*.02+glm::dvec3(0,.66,0);
    (void)workshop->aimAt({first,top});
    EXPECT_EQ(workshop->preview().registry.placements[second].placement.rotation,rotated);
    ASSERT_TRUE(workshop->valid())<<workshop->message();
    const auto placed=workshop->preview().registry.placements[second].placement;
    EXPECT_EQ(placed.translation.y,base.translation.y+48);
    ASSERT_TRUE(workshop->command(Action::Keep));
    const auto bytes=workshop->blueprintBytes(error);ASSERT_FALSE(bytes.empty())<<error;
    const auto rayOrigin=glm::dvec3(placed.translation.x,placed.translation.y,placed.translation.z)*.02+glm::dvec3(0,5,0);
    const auto picked=workshop->pick(rayOrigin,{0,-1,0});ASSERT_TRUE(picked);EXPECT_EQ(picked->placement,second);
    EXPECT_FALSE(workshop->pick(rayOrigin,{0,0,0}));
    ASSERT_TRUE(workshop->command(Action::Remove));ASSERT_TRUE(workshop->command(Action::Keep));
    ASSERT_TRUE(workshop->command(Action::Undo));EXPECT_EQ(workshop->blueprintBytes(error),bytes);
    ASSERT_TRUE(workshop->selectPart(first));ASSERT_TRUE(workshop->command(Action::Raise));
    EXPECT_FALSE(workshop->valid());EXPECT_FALSE(workshop->command(Action::Keep));
    ASSERT_TRUE(workshop->command(Action::Revert));EXPECT_EQ(workshop->blueprintBytes(error),bytes);
    const WorldNamespace world{{'b','r','i','c','k','-','o','w','n','e','d'}};
    auto seed=prepareCoveBuild(*expanded,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto request=prepareCoveRefit(workshop->design(),*owned,seed->catalog,error);ASSERT_TRUE(request)<<error;
    BuildIssue issue;auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);
    uint64_t issued=seed->issuedThrough;
    auto bought=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},issue);ASSERT_TRUE(bought)<<issue.field;
    EXPECT_EQ(bought->debit.salvageMaterial,7u);
    auto mapped=prepareCoveExpandedLaunchDesign(*expanded,*expanded,bought->after,seed->placements,seed->placements,error);ASSERT_TRUE(mapped)<<error;
    EXPECT_EQ(mapped->scene.registry.navigation->boatPlacements.size(),12u);
    EXPECT_TRUE(CoveBoatAssembly::compileBuild(bought->after,seed->catalog,mapped->placements,error))<<error;
    auto restored=CoveWorkshop::create(mapped->scene,error,25,expanded->registry.navigation->boatPlacements);ASSERT_TRUE(restored)<<error;
    EXPECT_EQ(restored->blueprintBytes(error),bytes);
}

TEST_F(CoveMovement, SixtyFourLargeBricksCompileRoundTripAndRefuseWholeSceneOverflow) {
    using namespace construction;using Action=CoveWorkshop::Action;std::string error;
    const auto expanded=assets::appendAssetFixtureCatalog(*scene,std::filesystem::canonical("data/salvage/cove-bricks-r02.json"),error);ASSERT_TRUE(expanded)<<error;
    auto workshop=CoveWorkshop::create(*expanded,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_TRUE(workshop->selectPart(10));ASSERT_TRUE(workshop->command(Action::Remove));ASSERT_TRUE(workshop->command(Action::Keep));
    for(uint32_t i=0;i<workshop->catalogCount();++i)if(workshop->catalogNameAt(i)=="Brick 2 x 4"){ASSERT_TRUE(workshop->selectCatalogAt(i));}
    ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->valid())<<workshop->message();ASSERT_TRUE(workshop->command(Action::Keep));
    const auto first=CoveBoatAssembly::compile(workshop->design(),error);ASSERT_TRUE(first)<<error;
    const auto original=CoveBoatAssembly::compile(*expanded,error);ASSERT_TRUE(original)<<error;
    const auto catalog=makeCovePartCatalog(*expanded,error);ASSERT_TRUE(catalog)<<error;
    auto build=first->build();std::vector<CoveBoatAssembly::Part> mappings(first->parts().begin(),first->parts().end());
    const auto brick=build.parts.back();const auto* definition=catalog->lookup(brick.definition).definition;ASSERT_NE(definition,nullptr);
    const auto append=[&](uint32_t number) {
        auto part=brick;part.id.counter=100000+number;part.placement.translation.y+=static_cast<int32_t>(48*number);
        const auto parent=build.parts.back().id;
        build.parts.push_back(part);mappings.push_back({25+number,part.id});
        for(uint64_t stud=0;stud<8;++stud) {
            Connection weld;weld.id={build.id.world,200000+number*8+stud};
            weld.a={parent,SocketId{100+2*stud}};weld.b={part.id,SocketId{101+2*stud}};
            weld.strength=definition->sockets.front().strength;build.connections.push_back(weld);
        }
    };
    for(uint32_t number=1;number<64;++number)append(number);
    const auto compiled=CoveBoatAssembly::compileBuild(build,*catalog,mappings,error);ASSERT_TRUE(compiled)<<error;
    EXPECT_EQ(compiled->parts().size(),74u);EXPECT_NEAR(compiled->massKg(),945+64*48,1e-8);
    EXPECT_LE(compiled->primaryRoot().cells.size(),2048u); // Live water-driver input budget.
    RecordProperty("brick_count",64);RecordProperty("water_cells",static_cast<int>(compiled->primaryRoot().cells.size()));
    RecordProperty("collision_cells",static_cast<int>(compiled->assembly().collision().roots()[0].shape.cells().size()));
    const auto bytes=makeCoveRecoveryDesign(build,*catalog);ASSERT_FALSE(bytes.empty());EXPECT_LE(bytes.size(),kMaximumCoveRecoveryDesignBytes);
    RecordProperty("recovery_design_bytes",static_cast<int>(bytes.size()));
    auto loaded=CoveWorkshop::create(*expanded,error);ASSERT_TRUE(loaded)<<error;ASSERT_TRUE(loaded->loadBlueprint(bytes,error))<<error;
    EXPECT_EQ(loaded->design().registry.navigation->boatPlacements.size(),74u);
    const auto mapped=prepareCoveExpandedLaunchDesign(*expanded,*expanded,build,original->parts(),original->parts(),error);ASSERT_TRUE(mapped)<<error;
    EXPECT_EQ(mapped->scene.registry.placements.size(),89u);
    EXPECT_EQ(mapped->placements.size(),74u);
    const auto intact=loaded->blueprintBytes(error);ASSERT_FALSE(intact.empty());
    // The full design remains editable through the ordinary workshop; removal
    // and Undo preserve the exact accepted layout at the supported capacity.
    const auto& members=loaded->design().registry.navigation->boatPlacements;
    const auto highest=*std::max_element(members.begin(),members.end(),[&](auto a,auto b){
        return loaded->design().registry.placements[a].placement.translation.y
            <loaded->design().registry.placements[b].placement.translation.y;
    });
    ASSERT_TRUE(loaded->selectPart(highest));ASSERT_TRUE(loaded->command(Action::Remove));
    ASSERT_TRUE(loaded->command(Action::Keep));EXPECT_EQ(loaded->design().registry.navigation->boatPlacements.size(),73u);
    ASSERT_TRUE(loaded->command(Action::Undo));EXPECT_EQ(loaded->blueprintBytes(error),intact);
    // Grow only the candidate mapping beyond available scene slots. Nothing
    // is published, no owned ID is dropped and the accepted design is exact.
    for(uint32_t number=64;number<72;++number)append(number);
    EXPECT_FALSE(prepareCoveExpandedLaunchDesign(*expanded,mapped->scene,build,original->parts(),mapped->placements,error));
    EXPECT_NE(error.find("no room"),std::string::npos)<<error;
    EXPECT_EQ(loaded->blueprintBytes(error),intact);
    EXPECT_EQ(mapped->scene.registry.placements.size(),89u);
    // Increasing paid build capacity never widens the starter grant ID array.
    EXPECT_FALSE(prepareCoveStarterKit(build,{build.id.world,999999},error));
}

TEST_F(CoveMovement, HarborLiftUsesActualHullSupportsAndCanonicalBodyFrames) {
    std::string error;const auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    const auto lift=CoveHarborLift::prepare(*boat,*scene->registry.navigation,error);ASSERT_TRUE(lift)<<error;
    EXPECT_EQ(lift->structure().size(),10u);EXPECT_EQ(lift->center(),scene->registry.navigation->delivery->center);
    const auto anchor=boat->assembly().mass().roots()[0].buildFromRoot.translation;
    physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute(
        {anchor.x*.02,-200+boat->equilibriumRootHeight(),anchor.z*.02});
    physics::AuthoredRootMotion gantry;gantry.position=physics::worldPositionFromAbsolute(glm::dvec3(0,-200,0)+lift->center());
    const auto lines=lift->attach(motion,gantry,{1,9},{2,7},error);ASSERT_TRUE(lines)<<error;
    for(size_t i=0;i<lines->size();++i){
        const auto& line=(*lines)[i];EXPECT_EQ(line.bodyA,(physics::BodyHandle{2,7}));
        EXPECT_EQ(line.bodyB,(physics::BodyHandle{1,9}));EXPECT_EQ(line.motorSpeed,0);
        EXPECT_EQ(line.maximumForce,30000);EXPECT_EQ(line.breakForce,45000);
        const auto expected=boat->shape().massFrame().rootPoint(
            {line.localAnchorB.x,line.localAnchorB.y,line.localAnchorB.z});
        EXPECT_NEAR(expected[0],lift->boatPoints()[i].x,1e-6);
        EXPECT_NEAR(expected[1],lift->boatPoints()[i].y,1e-6);
        EXPECT_NEAR(expected[2],lift->boatPoints()[i].z,1e-6);
        bool solid=false;
        for(const auto& cell:boat->shape().cells()){
            const auto point=lift->boatPoints()[i];
            if(std::abs(point.y-float(cell.minimum[1])*.02f)<1e-5f&&point.x>=float(cell.minimum[0])*.02f
                &&point.x<=float(cell.maximum[0])*.02f&&point.z>=float(cell.minimum[2])*.02f&&point.z<=float(cell.maximum[2])*.02f)solid=true;
        }
        EXPECT_TRUE(solid);EXPECT_GE(line.targetLength,kCoveHarborLiftMinimumLength);
        EXPECT_LE(line.targetLength,kCoveHarborLiftMaximumLength);
    }
    // No authority/inventory/physics mutation occurs during this preparation.
    EXPECT_EQ(boat->assembly().mass().roots()[0].mass.dryMassKg,1035);
}

TEST_F(CoveMovement, HarborLiftRefusesUnsafeAttachmentAndUnsupportedHulls) {
    std::string error;auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    auto lift=CoveHarborLift::prepare(*boat,*scene->registry.navigation,error);ASSERT_TRUE(lift)<<error;
    const auto anchor=boat->assembly().mass().roots()[0].buildFromRoot.translation;
    physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute(
        {anchor.x*.02,-200+boat->equilibriumRootHeight(),anchor.z*.02});
    physics::AuthoredRootMotion gantry;gantry.position=physics::worldPositionFromAbsolute(glm::dvec3(0,-200,0)+lift->center());
    for(int fault=0;fault<8;++fault){
        auto changed=motion;auto base=gantry;physics::BodyHandle body{1,9};
        switch(fault){
        case 0:changed.originVelocity.x=1;break;
        case 1:changed.angularVelocity.z=1;break;
        case 2:changed.orientation={0,1,0,0};break;
        case 3:changed.position=physics::worldPositionFromAbsolute({90,-200,-54});break;
        case 4:changed.orientation.w=std::numeric_limits<float>::quiet_NaN();break;
        case 5:body={};break;
        case 6:body={2,7};break;
        case 7:base.orientation={0,1,0,0};break;
        }
        EXPECT_FALSE(lift->attach(changed,base,body,{2,7},error))<<fault;EXPECT_FALSE(error.empty());
    }
    auto missing=*scene->registry.navigation;missing.delivery.reset();
    EXPECT_FALSE(CoveHarborLift::prepare(*boat,missing,error));
    const auto cargo=CoveBoatAssembly::compileCargo(*scene,24,error);ASSERT_TRUE(cargo)<<error;
    EXPECT_FALSE(CoveHarborLift::prepare(*cargo,*scene->registry.navigation,error));
}

TEST_F(CoveMovement, OwnedRestorePreparesIndependentBodiesPlayerAndTowWithoutPublishing) {
    using namespace construction;
    const WorldNamespace world{{42,7,3}};const DurableId owner{world,1};
    std::string error;auto seed=prepareCoveBuild(*scene,owner,4,error);ASSERT_TRUE(seed)<<error;
    auto cargo=CoveBoatAssembly::compileCargo(*scene,scene->registry.navigation->cargoPlacements.front(),error);
    ASSERT_TRUE(cargo)<<error;
    class Unused final:public PreparationAdapter {
    public:
        size_t calls=0;
        PreparationResult begin(const PreparationRequest& request)override{++calls;return {request.ticket,PreparationState::Rejected};}
        PreparationResult poll(PreparationTicket ticket)noexcept override{++calls;return {ticket,PreparationState::Rejected};}
        bool canActivate(PreparationTicket)const noexcept override{return false;}
        void activate(PreparationTicket,SimulationTick)noexcept override{++calls;}
        void discard(PreparationTicket)noexcept override{++calls;}
    } adapter;
    SessionBootstrap bootstrap;bootstrap.world=world;bootstrap.caller={owner,{world,2}};
    bootstrap.lastIssuedId=seed->issuedThrough;bootstrap.tick=SimulationTick{9'007'199'254'740'993ull};
    bootstrap.inventory={24,0};bootstrap.builds.push_back(seed->build);bootstrap.starterEntitlements.push_back(seed->starterEntitlement);
    bootstrap.jobs.push_back({{world,3}});
    const auto definition=cargo->build().parts.front().definition;
    bootstrap.cargoDefinitions.push_back({definition,420,cargo->displacementCubicMetres(),{60,0},CargoRecoveryRule::PreserveUnique});
    bootstrap.cargo.push_back({{world,4},definition,owner,{-4.5,-3,-58},{},DurableId{world,3}});
    CoveSaveContext context;context.identity.world=world;context.identity.content.manifest={{{{92,1}},1},1};
    context.identity.content.manifestDigest[0]=std::byte{1};context.boat=seed->build.id;
    context.job={world,3};context.cargo={world,4};context.cargoDefinition=bootstrap.cargoDefinitions.front();
    CovePhysicalSave physical;physical.tick=bootstrap.tick;physical.boat=context.boat;
    physical.cargo=context.cargo;physical.job=context.job;physical.cargoDefinition=definition;
    physical.boatMotion.position={-3,-1,-50};physical.boatMotion.originVelocity={.5f,0,-1};
    physical.boatMotion.angularVelocity={0,.1f,0};physical.cargoMotion.position={-4.5,-2.5,-58};
    physical.cargoMotion.originVelocity={.25f,0,0};physical.player.aboard=true;physical.player.mode=CoveSavedPlayerMode::Helm;
    const auto helm=scene->registry.navigation->helmStanding;physical.player.feet={helm.x,helm.y+.005,helm.z};
    physical.player.tick=bootstrap.tick.value();physical.player.interactions=2;physical.water.seconds=321.5;
    physical.cargoState=CoveSavedCargoState::Towed;physical.ropeLength=6;
    for(const auto& part:seed->build.parts)if(std::holds_alternative<WinchModule>(seed->catalog.lookup(part.definition).definition->module))physical.winchPart=part.id;
    const auto encode=[&]()->std::vector<std::byte>{
        EventStreamIncarnation incarnation;incarnation.bytes[0]=9;SessionIssue si;
        auto session=GameSession::create(bootstrap,incarnation,seed->catalog,adapter,si);EXPECT_TRUE(session);if(!session)return {};
        RecoveryIssue ri;auto image=SessionRecovery::capture(*session,context.identity.content,ri);EXPECT_TRUE(image);if(!image)return {};
        auto admitted=SessionRecovery::admit(*image,context.identity,seed->catalog,ri);EXPECT_TRUE(admitted);if(!admitted)return {};
        CoveSaveIssue ci;std::vector<std::byte> bytes;
        EXPECT_TRUE(CoveSaveCodec::encode(*admitted,nullptr,physical,context,seed->catalog,bytes,ci));return bytes;
    };
    auto bytes=encode();ASSERT_FALSE(bytes.empty());
    auto restored=CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error);
    ASSERT_TRUE(restored)<<error;
    EXPECT_EQ(restored->archive->physical,physical);EXPECT_EQ(restored->boat->build().id,seed->build.id);
    EXPECT_DOUBLE_EQ(restored->boat->assembly().mass().roots().front().mass.dryMassKg,1035);
    EXPECT_DOUBLE_EQ(restored->cargo->assembly().mass().roots().front().mass.dryMassKg,420);
    EXPECT_EQ(restored->cargoMotionType,physics::AuthoredBodyMotionType::Dynamic);
    EXPECT_EQ(restored->boatMotion.originVelocity,glm::vec3(.5f,0,-1));
    EXPECT_EQ(restored->player->state().feet,glm::dvec3(helm.x,helm.y+.005,helm.z));
    EXPECT_EQ(restored->player->state().tick,bootstrap.tick.value());EXPECT_TRUE(restored->hasWinch);
    EXPECT_FLOAT_EQ(restored->tow.targetLength,6);EXPECT_FLOAT_EQ(restored->tow.motorSpeed,0);
    EXPECT_GT(restored->tow.breakForce,0);EXPECT_FALSE(restored->tow.bodyA.valid());EXPECT_FALSE(restored->tow.bodyB.valid());
    EXPECT_EQ(adapter.calls,0u);
    physical.player.feet.y+=1;bytes=encode();ASSERT_FALSE(bytes.empty());
    EXPECT_FALSE(CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error));
    physical.player.feet.y-=1;physical.cargoState=CoveSavedCargoState::Banked;physical.winchPart={};physical.ropeLength=0;
    physical.cargoMotion.originVelocity={};bootstrap.cargo.clear();bootstrap.jobs.front().phase=JobPhase::Completed;
    bootstrap.jobs.front().acceptedBy=owner;bootstrap.inventory.salvageMaterial+=60;
    bytes=encode();ASSERT_FALSE(bytes.empty());
    restored=CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error);
    ASSERT_TRUE(restored)<<error;EXPECT_EQ(restored->cargoMotionType,physics::AuthoredBodyMotionType::Static);
    EXPECT_TRUE(restored->archive->current->snapshot().accepted.cargo.empty());
    EXPECT_EQ(restored->archive->current->snapshot().accepted.inventory.salvageMaterial,84u);
    EXPECT_DOUBLE_EQ(restored->boat->assembly().mass().roots().front().mass.dryMassKg,1035);EXPECT_EQ(adapter.calls,0u);
    physical.harborLift.profile=1;physical.harborLift.mode=CoveHarborLiftMode::Attached;physical.harborLift.lengths={4,4,4,4};
    bytes=encode();ASSERT_FALSE(bytes.empty());
    restored=CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error);
    ASSERT_TRUE(restored)<<error;EXPECT_EQ(restored->archive->physical.harborLift,physical.harborLift);EXPECT_EQ(adapter.calls,0u);
    physical.harborLift.lengths={2.5f,2.5f,2.5f,2.5f};bytes=encode();ASSERT_FALSE(bytes.empty());
    EXPECT_FALSE(CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error));
    EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveMovement, HarborParkingUsesExistingPierAndAvoidsPlayerOccupancy) {
    std::string error;auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    auto cargo=CoveBoatAssembly::compileCargo(*scene,scene->registry.navigation->cargoPlacements.front(),error);ASSERT_TRUE(cargo)<<error;
    auto scenery=CoveSceneryCollision::compile(*scene,error);ASSERT_TRUE(scenery)<<error;
    auto runtime=CoveHarborRuntime::create(*scene->registry.navigation,{},*boat,{},error);ASSERT_TRUE(runtime)<<error;
    ASSERT_TRUE(runtime->requestInstall());physics::AuthoredRootMotion boatPose;
    boatPose.position=physics::worldPositionFromAbsolute({-.5,0,-54});
    EXPECT_FALSE(runtime->prepareCargoParking(*cargo,*boat,boatPose,*scenery,{},player.feet(),[](double,double){return std::numeric_limits<double>::quiet_NaN();}));
    const auto parked=runtime->prepareCargoParking(*cargo,*boat,boatPose,*scenery,{},player.feet(),[](double,double){return -12.;});
    ASSERT_TRUE(parked);const auto first=physics::worldPositionToAbsolute(parked->position);
    EXPECT_DOUBLE_EQ(first.x,7);EXPECT_DOUBLE_EQ(first.z,-47);EXPECT_GT(first.y,1.28);EXPECT_EQ(parked->originVelocity,glm::vec3(0));
    const auto occupied=runtime->prepareCargoParking(*cargo,*boat,boatPose,*scenery,{},first,[](double,double){return -12.;});
    ASSERT_TRUE(occupied);EXPECT_GT(physics::worldPositionToAbsolute(occupied->position).z,first.z);
    auto noPier=*scene->registry.navigation;noPier.spawn.x+=20;
    auto unsupported=CoveHarborRuntime::create(noPier,{},*boat,{},error);ASSERT_TRUE(unsupported);ASSERT_TRUE(unsupported->requestInstall());
    EXPECT_FALSE(unsupported->prepareCargoParking(*cargo,*boat,boatPose,*scenery,{},player.feet(),[](double,double){return -12.;}));
    EXPECT_FALSE(runtime->installed());EXPECT_FALSE(runtime->body().valid());
}

TEST_F(CoveMovement, HarborPostsBlockWalkingAndSavedPlayerOccupancy) {
    std::string error;auto structure=CoveHarborLift::prepareStructure(*scene->registry.navigation,error);
    ASSERT_TRUE(structure)<<error;ASSERT_TRUE(structure->applyPlayerCollision(player));
    const auto center=structure->center();CovePlayer::State state;
    state.mode=CovePlayer::Mode::Airborne;state.feet=center+glm::dvec3(3,2,-4);
    ASSERT_TRUE(player.restore(state));move({1,0},12);
    EXPECT_LE(player.feet().x,center.x+3.5);EXPECT_GT(player.feet().x,center.x+3.3);
    const auto preserved=player.state();state.feet=center+glm::dvec3(4,2,-4);
    EXPECT_FALSE(player.restore(state));EXPECT_EQ(player.state().feet,preserved.feet);
    ASSERT_TRUE(player.setStaticObstacles({}));EXPECT_TRUE(player.restore(state));
}

TEST_F(CoveMovement, CanonicalStarterBuildOwnsActualPartsWeldsAndLoanIdentity) {
    using namespace construction;
    const WorldNamespace world{{'c','o','v','e','-','b','u','i','l','d','-','t','e','s','t','1'}};
    std::string error;auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    EXPECT_EQ(seed->starterEntitlement.counter,5u);EXPECT_EQ(seed->build.id.counter,6u);
    EXPECT_EQ(seed->issuedThrough,34u);ASSERT_EQ(seed->build.parts.size(),11u);ASSERT_EQ(seed->build.connections.size(),17u);
    for(const auto& part:seed->build.parts) {
        EXPECT_EQ(part.id.world,world);EXPECT_EQ(part.owningBuild,seed->build.id);
        EXPECT_EQ(part.provenance.origin,PartOrigin::StarterLoan);
        EXPECT_EQ(part.provenance.starterEntitlement,seed->starterEntitlement);
    }
    SessionBootstrap boot;boot.world=world;boot.caller={{world,1},{world,2}};boot.lastIssuedId=seed->issuedThrough;
    boot.builds={seed->build};boot.starterEntitlements={seed->starterEntitlement};boot.workshopEnabled=true;
    EXPECT_FALSE(GameSession::validateInitialState(boot,seed->catalog));
    boot.starterEntitlements.clear();EXPECT_TRUE(GameSession::validateInitialState(boot,seed->catalog));
    const auto baseline=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(baseline)<<error;
    const auto canonical=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(canonical)<<error;
    EXPECT_EQ(canonical->build().id,seed->build.id);
    EXPECT_EQ(canonical->shape().packedMass().centerInverseMass,baseline->shape().packedMass().centerInverseMass);
    EXPECT_DOUBLE_EQ(canonical->displacementCubicMetres(),baseline->displacementCubicMetres());
    EXPECT_DOUBLE_EQ(canonical->equilibriumRootHeight(),baseline->equilibriumRootHeight());
    EXPECT_EQ(canonical->shape().cells().size(),baseline->shape().cells().size());
    for(const auto& binding:canonical->parts()) {
        const auto& members=canonical->assembly().mass().parts();
        const auto member=std::find_if(members.begin(),members.end(),[&](const auto& part){return part.part==binding.id;});
        ASSERT_NE(member,members.end());
        const auto frame=compose(canonical->assembly().mass().roots()[0].buildFromRoot,member->rootFromPart);
        ASSERT_TRUE(frame);EXPECT_EQ(*frame,scene->registry.placements[binding.placement].placement);
    }
}

TEST_F(CoveMovement, CanonicalStarterBuildRejectsExhaustionAndAmbiguousSceneMapping) {
    using namespace construction;
    const WorldNamespace world{{1}};std::string error;
    EXPECT_FALSE(prepareCoveBuild(*scene,{world,1},std::numeric_limits<uint64_t>::max()-20,error));
    EXPECT_FALSE(prepareCoveBuild(*scene,{world,5},4,error));
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto map=seed->placements;map.at(1).placement=map.at(0).placement;
    EXPECT_FALSE(CoveBoatAssembly::compileBuild(seed->build,seed->catalog,map,error));
    map=seed->placements;map.at(1).id=map.at(0).id;
    EXPECT_FALSE(CoveBoatAssembly::compileBuild(seed->build,seed->catalog,map,error));
    EXPECT_TRUE(CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error));
    EXPECT_EQ(seed->build.parts.size(),11u);EXPECT_EQ(scene->registry.navigation->boatPlacements.size(),11u);
}

TEST_F(CoveMovement, WorkshopDesignBecomesCanonicalConnectedEditWithRecompiledHull) {
    using namespace construction;
    const WorldNamespace world{{'c','o','v','e','-','r','e','f','i','t','-','t','e','s','t','1'}};
    for(bool removeCradle:{false,true}) {
        std::string error;auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
        const auto original=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(original)<<error;
        auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
        if(removeCradle) {ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Next));ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Remove));}
        else {ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Snap));}
        ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
        const auto proposed=prepareCoveRefit(workshop->design(),*original,seed->catalog,error);ASSERT_TRUE(proposed)<<error;
        class CompilerAdapter final:public PreparationAdapter {
        public:
            const PartCatalog* catalog=nullptr;
            std::span<const CoveBoatAssembly::Part> placements;
            std::unique_ptr<CoveBoatAssembly> prepared;
            bool activated=false;
            PreparationResult begin(const PreparationRequest& request) override {
                std::string why;
                if(request.changedBuild)prepared=CoveBoatAssembly::compileBuild(*request.changedBuild,*catalog,placements,why);
                return {request.ticket,prepared?PreparationState::Ready:PreparationState::Rejected};
            }
            PreparationResult poll(PreparationTicket ticket) noexcept override {return {ticket,prepared?PreparationState::Ready:PreparationState::Rejected};}
            bool canActivate(PreparationTicket) const noexcept override {return bool(prepared);}
            void activate(PreparationTicket,SimulationTick) noexcept override {activated=true;}
            void discard(PreparationTicket) noexcept override {prepared.reset();}
        } adapter;
        adapter.catalog=&seed->catalog;adapter.placements=proposed->placements;
        SessionBootstrap boot;boot.world=world;boot.caller={{world,1},{world,2}};boot.lastIssuedId=seed->issuedThrough;
        boot.workshopEnabled=true;boot.builds={seed->build};boot.starterEntitlements={seed->starterEntitlement};
        SessionIssue issue;auto session=GameSession::create(boot,EventStreamIncarnation{{1}},seed->catalog,adapter,issue);ASSERT_TRUE(session);
        const Command request{AuthorityEpoch{1},RequestSequence{1},{},RefitBuild{{seed->build.id,{}},proposed->design}};
        ASSERT_EQ(session->submit(boot.caller,request).state,ReceiptState::PendingPreparation);
        ASSERT_TRUE(adapter.prepared);EXPECT_EQ(session->snapshot().builds[0].parts.size(),11u);
        EXPECT_DOUBLE_EQ(adapter.prepared->assembly().mass().roots()[0].mass.dryMassKg,workshop->massKg());
        ASSERT_TRUE(session->advanceOneTick());ASSERT_TRUE(adapter.activated);
        const auto accepted=session->snapshot();EXPECT_EQ(accepted.builds[0].revision,TopologyRevision{1});
        EXPECT_EQ(accepted.builds[0].parts.size(),removeCradle?10u:11u);EXPECT_EQ(accepted.inventory,ResourceAmounts{});
        for(const auto& part:accepted.builds[0].parts) {
            EXPECT_EQ(part.provenance.origin,PartOrigin::StarterLoan);
            EXPECT_TRUE(std::any_of(seed->build.parts.begin(),seed->build.parts.end(),[&](const auto& old){return old.id==part.id;}));
        }
        // Adapter compilation is a CPU contract check, not a live GPU launch.
        EXPECT_EQ(adapter.prepared->build().parts,accepted.builds[0].parts);
        EXPECT_EQ(scene->registry.navigation->boatPlacements.size(),11u);
        if(removeCradle){EXPECT_FALSE(prepareCoveRefit(*scene,*adapter.prepared,seed->catalog,error,scene->registry.navigation->boatPlacements));}
        // Launch derives every visible and walkable slot from the accepted
        // identity mapping; removed boat slots must not turn into fixed walls.
        const auto launched=prepareCoveLaunchDesign(*scene,accepted.builds.at(0),seed->placements,error);
        ASSERT_TRUE(launched)<<error;
        EXPECT_TRUE(workshop->matchesDesign(launched->scene));
        CovePlayer updated;
        const bool walkable=updated.initialize(launched->scene,[](double,double){return -5.;},error,
            scene->registry.navigation->boatPlacements);
        if(removeCradle) {
            ASSERT_TRUE(walkable)<<error;EXPECT_LT(updated.collisionBoxes(),player.collisionBoxes());
        } else {
            // Nearest free socket is structurally sound, but blocks a required
            // standing point. Live Launch must refuse this otherwise valid edit.
            EXPECT_FALSE(walkable);EXPECT_NE(error.find("standing room"),std::string::npos);
        }
        const auto resetEditor=CoveWorkshop::create(launched->scene,error);ASSERT_TRUE(resetEditor)<<error;
        EXPECT_TRUE(resetEditor->matchesDesign(launched->scene));EXPECT_EQ(resetEditor->undoCount(),0u);
        // The original trusted bindings also restore dormant loan parts for
        // authoritative Undo, even though the active boat no longer has them.
        const auto restored=prepareCoveLaunchDesign(*scene,seed->build,seed->placements,error);ASSERT_TRUE(restored)<<error;
        EXPECT_FALSE(resetEditor->matchesDesign(restored->scene));
        const auto originalEditor=CoveWorkshop::create(*scene,error);ASSERT_TRUE(originalEditor)<<error;
        EXPECT_TRUE(originalEditor->matchesDesign(restored->scene));
        auto wrong=seed->placements;wrong.at(0).id={world,999};
        EXPECT_FALSE(prepareCoveLaunchDesign(*scene,seed->build,wrong,error));
    }
}


TEST_F(CoveMovement, SettingsSurviveLaunchMappingAndUndoWithoutCostOrIdentityChanges) {
    using namespace construction;
    using Setting=CoveWorkshop::SettingAction;using Action=CoveWorkshop::Action;
    std::string error;const WorldNamespace world{{'c','o','v','e','-','s','e','t','t','i','n','g','s','0','0','1'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto original=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(original)<<error;
    auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_EQ(workshop->selectedName(),"Winch");ASSERT_TRUE(workshop->configurable());
    EXPECT_FALSE(workshop->hasOutputLimit());EXPECT_FALSE(workshop->configure(Setting::CycleLimit));
    EXPECT_FALSE(workshop->configure(Setting::Reverse));
    ASSERT_TRUE(workshop->configure(Setting::Toggle));EXPECT_FALSE(workshop->selectedSettings().enabled);
    EXPECT_TRUE(workshop->changed());ASSERT_TRUE(workshop->command(Action::Revert));
    EXPECT_TRUE(workshop->selectedSettings().enabled);EXPECT_FALSE(workshop->changed());
    ASSERT_TRUE(workshop->configure(Setting::Toggle));ASSERT_TRUE(workshop->command(Action::Keep));
    ASSERT_TRUE(workshop->command(Action::Undo));EXPECT_TRUE(workshop->selectedSettings().enabled);
    ASSERT_TRUE(workshop->configure(Setting::Toggle));ASSERT_TRUE(workshop->command(Action::Keep));
    for(unsigned i=0;i<11&&workshop->selectedName()!="Helm";++i)ASSERT_TRUE(workshop->command(Action::Next));
    ASSERT_EQ(workshop->selectedName(),"Helm");EXPECT_FALSE(workshop->canReverse());
    ASSERT_TRUE(workshop->configure(Setting::CycleLimit));ASSERT_TRUE(workshop->configure(Setting::CycleLimit));
    EXPECT_EQ(workshop->selectedSettings().limitPermille,500u);ASSERT_TRUE(workshop->command(Action::Keep));
    for(unsigned i=0;i<11&&workshop->selectedName()!="Propeller";++i)ASSERT_TRUE(workshop->command(Action::Next));
    ASSERT_EQ(workshop->selectedName(),"Propeller");ASSERT_TRUE(workshop->canReverse());
    for(unsigned i=0;i<4;++i)ASSERT_TRUE(workshop->configure(Setting::CycleLimit));
    EXPECT_EQ(workshop->selectedSettings().limitPermille,0u);
    ASSERT_TRUE(workshop->configure(Setting::CycleLimit));EXPECT_EQ(workshop->selectedSettings().limitPermille,1000u);
    ASSERT_TRUE(workshop->configure(Setting::CycleLimit));ASSERT_TRUE(workshop->configure(Setting::Reverse));
    ASSERT_TRUE(workshop->command(Action::Keep));EXPECT_FALSE(workshop->matchesDesign(*scene));
    const auto quote=quoteCoveDesign(workshop->design(),*original,seed->catalog);ASSERT_TRUE(quote);
    EXPECT_EQ(quote->debit,ResourceAmounts{});EXPECT_EQ(quote->credit,ResourceAmounts{});
    const auto request=prepareCoveRefit(workshop->design(),*original,seed->catalog,error);ASSERT_TRUE(request)<<error;
    class SettingsAdapter final:public PreparationAdapter {
    public:
        const PartCatalog* catalog=nullptr;std::span<const CoveBoatAssembly::Part> placements;
        std::unique_ptr<CoveBoatAssembly> prepared,live;
        PreparationResult begin(const PreparationRequest& r) override {
            std::string why;if(r.changedBuild)prepared=CoveBoatAssembly::compileBuild(*r.changedBuild,*catalog,placements,why);
            return {r.ticket,prepared?PreparationState::Ready:PreparationState::Rejected};
        }
        PreparationResult poll(PreparationTicket t) noexcept override {return {t,prepared?PreparationState::Ready:PreparationState::Rejected};}
        bool canActivate(PreparationTicket) const noexcept override {return bool(prepared);}
        void activate(PreparationTicket,SimulationTick) noexcept override {live=std::move(prepared);}
        void discard(PreparationTicket) noexcept override {prepared.reset();}
    } adapter;
    adapter.catalog=&seed->catalog;adapter.placements=seed->placements;
    SessionBootstrap boot;boot.world=world;boot.caller={{world,1},{world,2}};boot.lastIssuedId=seed->issuedThrough;
    boot.workshopEnabled=true;boot.builds={seed->build};boot.starterEntitlements={seed->starterEntitlement};boot.inventory=coveStartingMaterials;
    SessionIssue issue;auto session=GameSession::create(boot,EventStreamIncarnation{{1}},seed->catalog,adapter,issue);ASSERT_TRUE(session);
    const auto submit=[&](Intent intent) {
        const auto r=session->submit(boot.caller,{AuthorityEpoch{1},RequestSequence{session->processedSequence()+1},session->snapshot().revision,std::move(intent)});
        return r.state==ReceiptState::PendingPreparation&&session->advanceOneTick();
    };
    ASSERT_TRUE(submit(RefitBuild{{seed->build.id,{}},request->design}));ASSERT_TRUE(adapter.live);
    const auto check=[&](bool configured) {
        const auto state=session->snapshot();EXPECT_EQ(state.inventory,boot.inventory);
        EXPECT_EQ(state.builds.at(0).parts.size(),seed->build.parts.size());
        for(size_t i=0;i<seed->build.parts.size();++i) {
            const auto& before=seed->build.parts.at(i);const auto& after=state.builds.at(0).parts.at(i);
            EXPECT_EQ(before.id,after.id);EXPECT_EQ(before.provenance,after.provenance);EXPECT_EQ(before.placement,after.placement);
        }
        for(const auto& module:adapter.live->assembly().functions().modules()) {
            if(std::holds_alternative<WinchModule>(module.parameters)){EXPECT_EQ(coveModuleOutput(module),configured?0.f:1.f);}
            if(std::holds_alternative<HelmModule>(module.parameters)){EXPECT_EQ(coveModuleOutput(module),configured?.5f:1.f);}
            if(std::holds_alternative<PropellerModule>(module.parameters)) {
                EXPECT_EQ(coveModuleOutput(module),configured?.75f:1.f);EXPECT_EQ(module.settings.reversed,configured);
            }
        }
    };
    check(true);
    const auto mapped=prepareCoveLaunchDesign(*scene,adapter.live->build(),seed->placements,error);ASSERT_TRUE(mapped)<<error;
    EXPECT_TRUE(workshop->matchesDesign(mapped->scene));
    auto reopened=CoveWorkshop::create(mapped->scene,error);ASSERT_TRUE(reopened)<<error;
    EXPECT_FALSE(reopened->changed());EXPECT_TRUE(reopened->matchesDesign(mapped->scene));
    EXPECT_FALSE(reopened->selectedSettings().enabled); // Winch selected on entry.
    ASSERT_TRUE(reopened->command(Action::Next));EXPECT_FALSE(reopened->configurable());
    EXPECT_FALSE(reopened->configure(Setting::Toggle)); // Passive cradle stays passive.
    const auto unchanged=prepareCoveRefit(reopened->design(),*adapter.live,seed->catalog,error);ASSERT_TRUE(unchanged)<<error;
    for(const auto& part:unchanged->design->parts()) {
        const auto module=adapter.live->assembly().functions().module(part.source);ASSERT_TRUE(module);
        EXPECT_EQ(part.design.settings,module->settings);
    }
    auto history=session->history();ASSERT_TRUE(history.undo);
    ASSERT_TRUE(submit(Undo{history.undo->target,history.undo->entry,history.generation}));check(false);
    history=session->history();ASSERT_TRUE(history.redo);
    ASSERT_TRUE(submit(Redo{history.redo->target,history.redo->entry,history.generation}));check(true);
}

TEST_F(CoveMovement, SavedDesignReloadReusesOwnedPartsAndPricesAdditionsWithoutGrantingLoans) {
    using namespace construction;using Action=CoveWorkshop::Action;
    std::string error;const WorldNamespace world{{'s','a','v','e','d','-','d','e','s','i','g','n','-','0','0','1'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    const auto originalBytes=workshop->blueprintBytes(error);ASSERT_FALSE(originalBytes.empty())<<error;
    ASSERT_TRUE(workshop->configure(CoveWorkshop::SettingAction::Toggle));ASSERT_TRUE(workshop->command(Action::Keep));
    ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->command(Action::Keep));
    const auto bytes=workshop->blueprintBytes(error);ASSERT_FALSE(bytes.empty())<<error;
    auto fresh=CoveWorkshop::create(*scene,error);ASSERT_TRUE(fresh)<<error;
    ASSERT_TRUE(fresh->loadBlueprint(bytes,error))<<error;EXPECT_FALSE(fresh->changed());EXPECT_EQ(fresh->undoCount(),1u);
    const auto quoted=quoteCoveDesign(fresh->design(),*owned,seed->catalog);ASSERT_TRUE(quoted);EXPECT_EQ(quoted->debit.salvageMaterial,24u);EXPECT_EQ(quoted->credit.salvageMaterial,0u);
    auto request=prepareCoveRefit(fresh->design(),*owned,seed->catalog,error,scene->registry.navigation->boatPlacements);ASSERT_TRUE(request)<<error;
    EXPECT_EQ(std::count_if(request->design->parts().begin(),request->design->parts().end(),[](const auto& p){return !isValid(p.source);}),1);
    EXPECT_EQ(fresh->blueprintBytes(error),bytes);ASSERT_TRUE(fresh->command(Action::Undo));EXPECT_TRUE(fresh->matchesDesign(*scene));
    auto corrupt=bytes;corrupt.at(27)^=std::byte{1};EXPECT_FALSE(fresh->loadBlueprint(corrupt,error));EXPECT_TRUE(fresh->matchesDesign(*scene));
    // Retiring an original loan and importing its old blueprint must request a
    // paid replacement, not resurrect the old entitlement or consume its slot.
    auto reduced=seed->build;const auto cradle=std::find_if(reduced.parts.begin(),reduced.parts.end(),[&](const auto& p){return std::holds_alternative<CargoCradleModule>(seed->catalog.lookup(p.definition).definition->module);});
    ASSERT_NE(cradle,reduced.parts.end());const auto cradleId=cradle->id,cradleDefinition=cradle->definition.id;
    const auto price=seed->catalog.lookup(cradle->definition).definition->cost;
    reduced.parts.erase(cradle);std::erase_if(reduced.connections,[&](const auto& c){return c.a.part==cradleId||c.b.part==cradleId;});
    auto mappings=seed->placements;std::erase_if(mappings,[&](const auto& p){return p.id==cradleId;});
    auto reducedBoat=CoveBoatAssembly::compileBuild(reduced,seed->catalog,mappings,error);ASSERT_TRUE(reducedBoat)<<error;
    const auto installed=prepareCoveLaunchDesign(*scene,reduced,seed->placements,error);ASSERT_TRUE(installed)<<error;
    auto restored=CoveWorkshop::create(installed->scene,error,25,scene->registry.navigation->boatPlacements);ASSERT_TRUE(restored)<<error;
    ASSERT_TRUE(restored->loadBlueprint(originalBytes,error))<<error;
    const auto restoredPrice=quoteCoveDesign(restored->design(),*reducedBoat,seed->catalog);ASSERT_TRUE(restoredPrice);EXPECT_EQ(restoredPrice->debit,price);
    request=prepareCoveRefit(restored->design(),*reducedBoat,seed->catalog,error,scene->registry.navigation->boatPlacements);ASSERT_TRUE(request)<<error;
    const auto replacement=std::find_if(request->design->parts().begin(),request->design->parts().end(),[&](const auto& p){return p.design.definition.id==cradleDefinition;});
    ASSERT_NE(replacement,request->design->parts().end());EXPECT_FALSE(isValid(replacement->source));
}

TEST_F(CoveMovement, ReplacementGrantMapsTheSameInstalledLegoPiecesWithNewIdentities) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'s','t','a','r','t','e','r','-','m','a','p'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto kit=prepareCoveStarterKit(seed->build,seed->starterEntitlement,error);ASSERT_TRUE(kit)<<error;
    BuildIssue issue;auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);
    uint64_t issued=seed->issuedThrough;
    auto plan=prepareBuildRefit(*model,*kit->design,seed->catalog,[&]{return DurableId{world,++issued};},issue,{{},kit->entitlement});
    ASSERT_TRUE(plan)<<issue.field;auto current=*kit;current.partIds={};
    std::copy_n(plan->createdIds.begin(),current.partCount,current.partIds.begin());
    auto bindings=bindCoveStarterKit(*kit,&current,seed->placements,error);ASSERT_TRUE(bindings)<<error;
    auto design=prepareCoveExpandedLaunchDesign(*scene,*scene,plan->after,*bindings,seed->placements,error);ASSERT_TRUE(design)<<error;
    auto physical=CoveBoatAssembly::compileBuild(plan->after,seed->catalog,design->placements,error);ASSERT_TRUE(physical)<<error;
    auto original=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(original)<<error;
    EXPECT_EQ(physical->assembly().mass().roots()[0].mass.dryMassKg,original->assembly().mass().roots()[0].mass.dryMassKg);
    EXPECT_EQ(design->scene.registry.navigation->boatPlacements,scene->registry.navigation->boatPlacements);
    for(size_t i=0;i<bindings->size();++i){EXPECT_EQ((*bindings)[i].placement,seed->placements[i].placement);EXPECT_NE((*bindings)[i].id,seed->placements[i].id);}
    current.entitlement={world,999};EXPECT_FALSE(bindCoveStarterKit(*kit,&current,seed->placements,error));
}
TEST_F(CoveMovement, StoredPontoonIsQuotedAndLaunchedWithItsExactConditionAndConfiguration) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'s','t','o','r','e','d','-','c','o','v','e'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    const auto source=std::find_if(seed->build.parts.begin(),seed->build.parts.end(),[&](const auto& p){return p.definition==workshop->catalogDefinition();});
    ASSERT_NE(source,seed->build.parts.end());auto paid=*source;paid.id={world,1000};paid.provenance={};paid.health=4321;paid.paint={20,40,60,255};
    const std::array stock{paid};const auto* available=availableCoveStoredPart(workshop->design(),*owned,stock,paid.definition);ASSERT_NE(available,nullptr);
    ASSERT_TRUE(workshop->addPart(available));ASSERT_TRUE(workshop->valid())<<workshop->message();ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
    EXPECT_EQ(availableCoveStoredPart(workshop->design(),*owned,stock,paid.definition),nullptr);
    auto quote=quoteCoveDesign(workshop->design(),*owned,seed->catalog,stock);ASSERT_TRUE(quote);EXPECT_EQ(quote->debit,ResourceAmounts{});
    auto request=prepareCoveRefit(workshop->design(),*owned,seed->catalog,error,scene->registry.navigation->boatPlacements,stock);ASSERT_TRUE(request)<<error;
    EXPECT_EQ(request->design->parts().back().source,paid.id);
    BuildIssue issue;auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);uint64_t issued=1000;
    auto plan=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},issue,{stock,{}});ASSERT_TRUE(plan)<<issue.field;
    EXPECT_TRUE(plan->storedPartsAfter.empty());EXPECT_EQ(plan->debit,ResourceAmounts{});
    const auto fitted=std::find_if(plan->after.parts.begin(),plan->after.parts.end(),[&](const auto& p){return p.id==paid.id;});ASSERT_NE(fitted,plan->after.parts.end());
    EXPECT_EQ(fitted->health,paid.health);EXPECT_EQ(fitted->paint,paid.paint);EXPECT_EQ(fitted->settings,paid.settings);
    auto design=prepareCoveExpandedLaunchDesign(*scene,*scene,plan->after,seed->placements,owned->parts(),error);ASSERT_TRUE(design)<<error;
    EXPECT_TRUE(CoveBoatAssembly::compileBuild(plan->after,seed->catalog,design->placements,error))<<error;
}

TEST_F(CoveMovement, NormalizedRecoveryRestoresWeldedLayoutAndSettingsUsingStoredPaidPart) {
    using namespace construction;using Action=CoveWorkshop::Action;
    std::string error;const WorldNamespace world{{'r','e','c','o','v','e','r','y','-','d','e','s','i','g','n'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto custom=CoveWorkshop::create(*scene,error);ASSERT_TRUE(custom)<<error;
    ASSERT_TRUE(custom->configure(CoveWorkshop::SettingAction::Toggle));ASSERT_TRUE(custom->command(Action::Keep));
    ASSERT_TRUE(custom->addPart());ASSERT_TRUE(custom->command(Action::Keep));
    auto request=prepareCoveRefit(custom->design(),*owned,seed->catalog,error);ASSERT_TRUE(request)<<error;
    BuildIssue issue;auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);
    uint64_t issued=seed->issuedThrough;
    auto bought=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},issue);ASSERT_TRUE(bought)<<issue.field;
    auto paid=std::find_if(bought->after.parts.begin(),bought->after.parts.end(),[](const auto& p){return p.provenance.origin==PartOrigin::Paid;});
    ASSERT_NE(paid,bought->after.parts.end());paid->paint={20,40,60,255};paid->health=4321;
    const auto recovery=makeCoveRecoveryDesign(bought->after,seed->catalog);ASSERT_FALSE(recovery.empty());
    const std::array stock{*paid};auto restored=CoveWorkshop::create(*scene,error);ASSERT_TRUE(restored)<<error;
    ASSERT_TRUE(restored->loadBlueprint(recovery,error))<<error;
    const auto quote=quoteCoveDesign(restored->design(),*owned,seed->catalog,stock);ASSERT_TRUE(quote);
    EXPECT_EQ(quote->debit,ResourceAmounts{});EXPECT_EQ(quote->credit,ResourceAmounts{});
    request=prepareCoveRefit(restored->design(),*owned,seed->catalog,error,scene->registry.navigation->boatPlacements,stock);ASSERT_TRUE(request)<<error;
    auto rebuilt=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},issue,{stock,{}});ASSERT_TRUE(rebuilt)<<issue.field;
    // The new weld may receive an ID; every physical part must remain owned.
    for(const auto& part:rebuilt->after.parts)EXPECT_TRUE(part.id==paid->id
        ||std::any_of(seed->build.parts.begin(),seed->build.parts.end(),[&](const auto& p){return p.id==part.id;}));
    EXPECT_TRUE(rebuilt->storedPartsAfter.empty());
    EXPECT_EQ(makeCoveRecoveryDesign(rebuilt->after,seed->catalog),recovery);
    const auto fitted=std::find_if(rebuilt->after.parts.begin(),rebuilt->after.parts.end(),[&](const auto& p){return p.id==paid->id;});
    ASSERT_NE(fitted,rebuilt->after.parts.end());EXPECT_EQ(*fitted,*paid);
    auto design=prepareCoveExpandedLaunchDesign(*scene,*scene,rebuilt->after,seed->placements,owned->parts(),error);ASSERT_TRUE(design)<<error;
    EXPECT_TRUE(CoveBoatAssembly::compileBuild(rebuilt->after,seed->catalog,design->placements,error))<<error;
}

TEST_F(CoveMovement, CuttingAddedPontoonPreparesTwoCompleteRootsWithoutLosingOwnedParts) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','o','v','e','-','c','u','t','-','r','o','o','t','s'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
    auto request=prepareCoveRefit(workshop->design(),*owned,seed->catalog,error);ASSERT_TRUE(request)<<error;
    BuildIssue buildIssue;auto model=BuildModel::create(seed->build,seed->catalog,buildIssue);ASSERT_TRUE(model);
    uint64_t issued=seed->issuedThrough;
    auto bought=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},buildIssue);ASSERT_TRUE(bought)<<buildIssue.field;
    const auto paid=std::find_if(bought->after.parts.begin(),bought->after.parts.end(),[](const auto& p){return p.provenance.origin==PartOrigin::Paid;});
    ASSERT_NE(paid,bought->after.parts.end());std::vector<DurableId> cuts;
    for(const auto& link:bought->after.connections)if(link.a.part==paid->id||link.b.part==paid->id)cuts.push_back(link.id);
    ASSERT_FALSE(cuts.empty());AssemblyFractureIssue issue;
    auto plan=AssemblyFracturePlan::prepare(bought->after,bought->after.revision,cuts,seed->catalog,issue);ASSERT_TRUE(plan)<<int(issue.error);
    EXPECT_EQ(plan->afterBuild().parts,bought->after.parts);
    ASSERT_EQ(plan->after().mass().roots().size(),2u);EXPECT_EQ(plan->after().collision().roots().size(),2u);
    EXPECT_EQ(plan->after().buoyancy().roots().size(),2u);EXPECT_EQ(plan->after().functions().modules().size(),12u);
    double mass=0;for(const auto& root:plan->after().mass().roots())mass+=root.mass.dryMassKg;
    EXPECT_DOUBLE_EQ(mass,1155);EXPECT_DOUBLE_EQ(mass,plan->before().mass().roots()[0].mass.dryMassKg);
    const auto* detached=plan->after().functions().module(paid->id);ASSERT_NE(detached,nullptr);
    EXPECT_EQ(plan->after().mass().roots()[detached->root].partCount,1u);
    const std::array motion{AssemblyRootMotion{plan->before().mass().roots()[0].key,{{0,0,0},{1,0,0,0,1,0,0,0,1}},{2,0,-3},{.2,.1,-.3}}};
    const AssemblyMotionSource source{bought->after.id,bought->after.revision,SimulationTick{30},motion};
    auto fragments=plan->inheritMotion(source,SimulationTick{30},issue);ASSERT_TRUE(fragments);EXPECT_EQ(fragments->size(),2u);
    // This is prepared content, not a claim that the one-body live adapter can
    // publish it. Each resulting shape must still pass backend admission.
    for(const auto& root:plan->after().collision().roots()) {
        const auto& m=plan->after().mass().roots()[root.massRoot].mass;
        physics::AuthoredShapeIssue shapeIssue;
        auto shape=physics::AuthoredShape::prepare(root.shape,{m.dryMassKg,{m.localCenterOfMass.x,m.localCenterOfMass.y,m.localCenterOfMass.z},m.inertia.elements},shapeIssue);
        EXPECT_TRUE(shape)<<int(shapeIssue.error);
    }
}

TEST_F(CoveMovement, FragmentRootsPreserveBindingsAndKeepHelmOffTheFirstDetachedPontoon) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'r','o','o','t','-','c','o','v','e'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto helm=intact->primaryRoot().helm;ASSERT_TRUE(helm);
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    ASSERT_FALSE(cuts.empty());BuildIssue issue;auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);
    ASSERT_FALSE(model->cutWelds(seed->build.revision,cuts,seed->catalog));
    const auto fragments=CoveBoatAssembly::compileFragments(model->snapshot(),seed->catalog,seed->placements,*helm,error);ASSERT_TRUE(fragments)<<error;
    ASSERT_EQ(fragments->roots().size(),2u);ASSERT_EQ(fragments->rootForPart(first),0u);
    EXPECT_EQ(fragments->primaryRootIndex(),1u);EXPECT_EQ(fragments->rootForPart(*helm),1u);
    EXPECT_EQ(fragments->build().parts,seed->build.parts);EXPECT_EQ(fragments->parts().size(),seed->placements.size());
    EXPECT_EQ(fragments->primaryRoot().helm,helm);EXPECT_EQ(fragments->primaryRoot().propeller,intact->primaryRoot().propeller);
    EXPECT_FALSE(fragments->roots()[0].helm);EXPECT_FALSE(fragments->roots()[0].propeller);
    EXPECT_EQ(&fragments->shape(),&fragments->roots()[1].shape);
    double mass=0,volume=0;
    for(size_t index=0;index<fragments->roots().size();++index) {
        const auto& root=fragments->roots()[index];const auto water=root.water({uint32_t(index+1),7});
        EXPECT_EQ(water.body,(physics::BodyHandle{uint32_t(index+1),7}));
        EXPECT_EQ(water.cells.data(),root.cells.data());EXPECT_FALSE(water.cells.empty());
        mass+=fragments->assembly().mass().roots()[index].mass.dryMassKg;
        for(const auto& cell:water.cells){const auto extent=glm::dvec3(cell.maximum)-glm::dvec3(cell.minimum);volume+=extent.x*extent.y*extent.z;}
    }
    // The actual GPU cells use f32 metres; canonical coverage uses integer ticks.
    EXPECT_DOUBLE_EQ(mass,1035);EXPECT_NEAR(volume,fragments->displacementCubicMetres(),volume*1e-6);
    EXPECT_NEAR(fragments->displacementCubicMetres(),intact->displacementCubicMetres(),1e-12);
    const auto propeller=*fragments->primaryRoot().propeller;
    const auto* part=fragments->assembly().functions().module(propeller);ASSERT_NE(part,nullptr);
    for(const auto& frame:fragments->assembly().functions().frames())if(frame.kind==AssemblyFrameKind::Thrust) {
        const auto point=frame.rootFromFrame.translation;
        EXPECT_EQ(fragments->primaryRoot().propellerPoint,glm::vec3(point.x,point.y,point.z)*.02f);
    }
    EXPECT_FALSE(CoveBoatAssembly::compileBuild(model->snapshot(),seed->catalog,seed->placements,error));
    EXPECT_FALSE(CoveBoatAssembly::compileFragments(model->snapshot(),seed->catalog,seed->placements,first,error));
    EXPECT_FALSE(fragments->rootForPart({world,999}));
    auto bindings=seed->placements;ASSERT_GT(bindings.size(),1u);bindings.at(bindings.size()-1).id=bindings.at(0).id;
    EXPECT_FALSE(CoveBoatAssembly::compileFragments(model->snapshot(),seed->catalog,bindings,*helm,error));
}

TEST_F(CoveMovement, EverySeparatedPartHasItsOwnShapeWaterAndModuleConfiguration) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'a','l','l','-','c','o','v','e','-','r','o','o','t','s'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact);
    auto build=seed->build;for(auto& link:build.connections){link.enabled=false;link.damage=kFullHealth;}
    const auto propeller=*intact->primaryRoot().propeller;const auto helm=*intact->primaryRoot().helm;
    for(auto& part:build.parts)if(part.id==propeller){part.settings.limitPermille=370;part.settings.reversed=true;}
    const auto fragments=CoveBoatAssembly::compileFragments(build,seed->catalog,seed->placements,helm,error);ASSERT_TRUE(fragments)<<error;
    ASSERT_EQ(fragments->roots().size(),build.parts.size());EXPECT_EQ(fragments->build().parts,build.parts);
    EXPECT_EQ(fragments->primaryRoot().maximumThrustNewtons,0);EXPECT_FALSE(fragments->primaryRoot().propeller);
    const auto powered=fragments->rootForPart(propeller);ASSERT_TRUE(powered);EXPECT_NE(*powered,fragments->primaryRootIndex());
    EXPECT_EQ(fragments->roots()[*powered].propeller,propeller);EXPECT_FALSE(fragments->roots()[*powered].helm);
    EXPECT_FLOAT_EQ(fragments->roots()[*powered].maximumThrustNewtons,intact->primaryRoot().maximumThrustNewtons*.37f);
    EXPECT_EQ(fragments->roots()[*powered].propellerDirection,-intact->primaryRoot().propellerDirection);
    double total=0;
    for(size_t index=0;index<fragments->roots().size();++index){
        const auto& root=fragments->roots()[index];const auto& mass=fragments->assembly().mass().roots()[index];
        EXPECT_EQ(mass.partCount,1u);EXPECT_DOUBLE_EQ(root.shape.massFrame().inverseMass(),1.0/mass.mass.dryMassKg);
        EXPECT_EQ(fragments->rootForPart(mass.key),index);total+=mass.mass.dryMassKg;
    }
    EXPECT_DOUBLE_EQ(total,1035);
}

TEST_F(CoveMovement, CutProtectsIntactDesignAndReopensBrokenWorkshopWithoutRepairingOwnership) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','u','t','-','p','r','o','t','e','c','t'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    // A distinct paid/configured design must consume one backup slot before
    // cutting. Repeated cuts and rebuilding cannot silently manufacture it.
    auto build=seed->build;build.parts.back().paint[0]=17;
    const auto builtIn=makeCoveRecoveryDesign(seed->build,seed->catalog);
    const auto intactBytes=makeCoveRecoveryDesign(build,seed->catalog);ASSERT_NE(builtIn,intactBytes);
    auto intact=CoveBoatAssembly::compileBuild(build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    CoveRecoveryDesigns backups;size_t selected=0;
    ASSERT_TRUE(protectCoveRecoveryDesign(build,seed->catalog,builtIn,backups,selected,error))<<error;
    ASSERT_EQ(backups.size(),1u);EXPECT_EQ(backups[0],intactBytes);
    const auto first=build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(build,build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto& broken=split->afterBuild();ASSERT_TRUE(makeCoveRecoveryDesign(broken,seed->catalog).empty());
    for(int attempt=0;attempt<3;++attempt)ASSERT_TRUE(protectCoveRecoveryDesign(broken,seed->catalog,builtIn,backups,selected,error))<<error;
    EXPECT_EQ(backups.size(),1u);EXPECT_EQ(broken.parts,build.parts);
    EXPECT_EQ(std::count_if(broken.connections.begin(),broken.connections.end(),[](const auto& link){return !link.enabled;}),cuts.size());
    CoveRecoveryDesigns missing;size_t unchanged=77;
    EXPECT_FALSE(protectCoveRecoveryDesign(broken,seed->catalog,builtIn,missing,unchanged,error));EXPECT_TRUE(missing.empty());EXPECT_EQ(unchanged,77u);
    for(int i=1;i<4;++i){auto other=build;other.parts.back().paint[0]=static_cast<uint8_t>(17+i);
        ASSERT_EQ(rememberCoveRecoveryDesign(backups,makeCoveRecoveryDesign(other,seed->catalog),seed->catalog),CoveRememberDesign::Stored);}
    const auto full=backups;auto extra=build;extra.parts.back().paint[0]=22;
    EXPECT_FALSE(protectCoveRecoveryDesign(extra,seed->catalog,builtIn,backups,unchanged,error));EXPECT_EQ(backups,full);EXPECT_EQ(unchanged,77u);
    EXPECT_TRUE(protectCoveRecoveryDesign(broken,seed->catalog,builtIn,backups,selected,error));EXPECT_EQ(backups,full);
    const auto expanded=prepareCoveExpandedLaunchDesign(*scene,*scene,broken,seed->placements,{},error,CoveSceneTopology::AcceptedRoots);ASSERT_TRUE(expanded)<<error;
    EXPECT_FALSE(CoveWorkshop::create(expanded->scene,error));
    auto workshop=CoveWorkshop::create(expanded->scene,error,0,{},true);ASSERT_TRUE(workshop)<<error;
    EXPECT_FALSE(workshop->valid());EXPECT_FALSE(workshop->changed());EXPECT_TRUE(workshop->matchesDesign(expanded->scene));
    EXPECT_DOUBLE_EQ(workshop->massKg(),1035);
    ASSERT_TRUE(workshop->loadBlueprint(backups[0],error))<<error;EXPECT_TRUE(workshop->valid());EXPECT_FALSE(workshop->matchesDesign(expanded->scene));
    auto invalid=expanded->scene;invalid.registry.navigation->boatPlacements.push_back(invalid.registry.navigation->boatPlacements.front());
    EXPECT_FALSE(CoveWorkshop::create(invalid,error,0,{},true));
}

TEST_F(CoveMovement, CutterReachUsesObservedSocketPoseAndRejectsStaleDisabledOrRemoteWelds) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','u','t','-','r','e','a','c','h'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto boat=CoveBoatAssembly::compileFragments(split->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(boat)<<error;
    auto roots=CoveRigidRoots::prepare(*boat,error);ASSERT_TRUE(roots);ASSERT_EQ(roots->roots().size(),2u);
    // These handles model the joined owner precondition only; actual GPU
    // admission and rollback are exercised separately below.
    for(size_t i=0;i<roots->roots().size();++i){auto& root=roots->roots()[i];root.body={uint32_t(i+1),1};root.shape={uint32_t(i+1),1,1};
        root.admissionTick=8;root.observedTick=10;
        root.observed.position=physics::worldPositionFromAbsolute({12000+double(i)*23000,-200,-8000});
        root.observed.orientation=glm::angleAxis(.7f,glm::normalize(glm::vec3(1,2,3)));}
    const auto& functions=boat->assembly().functions();
    const auto link=std::find_if(functions.connections().begin(),functions.connections().end(),[](const auto& candidate){return candidate.definition.enabled;});ASSERT_NE(link,functions.connections().end());
    const auto& socket=functions.sockets()[link->socketA];const auto& motion=roots->roots()[socket.root].observed;
    const auto p=socket.rootFromSocket.translation;
    const auto point=physics::worldPositionToAbsolute(motion.position)+glm::normalize(glm::dquat(motion.orientation))*glm::dvec3(p.x,p.y,p.z)*.02;
    const auto target=roots->reachableWeld(*boat,point,link->definition.id);ASSERT_TRUE(target);EXPECT_NEAR(target->distance,0,1e-8);
    EXPECT_FALSE(roots->reachableWeld(*boat,point+glm::dvec3(2.01,0,0),link->definition.id));
    EXPECT_FALSE(roots->reachableWeld(*boat,point,cuts.front()));
    EXPECT_FALSE(roots->reachableWeld(*boat,{},link->definition.id));
    roots->roots()[0].observedTick=9;EXPECT_FALSE(roots->reachableWeld(*boat,point,link->definition.id));
    roots->roots()[0].observedTick=10;EXPECT_FALSE(roots->reachableWeld(*boat,{NAN,0,0}));
}

TEST_F(CoveMovement, CutTransfersStandingPlayerToDetachedSupportWithoutMovingTheirWorldFeet) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','u','t','-','r','i','d','e','r'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    auto source=CoveRigidRoots::prepare(*intact,error);ASSERT_TRUE(source);
    const glm::dvec3 origin{12000,-200,-8000};auto& root=source->primary();const auto anchor=intact->primaryMassRoot().buildFromRoot.translation;
    root.shape={1,1,1};root.body={1,1};root.admissionTick=1;root.observedTick=10;
    root.observed.position=physics::worldPositionFromAbsolute(origin+glm::dvec3(anchor.x,anchor.y,anchor.z)*.02);
    root.observed.originVelocity={3,2,-1};root.observed.angularVelocity={0,.2f,0};
    ASSERT_TRUE(source->bindPlayer(player,*intact,origin,error));
    CovePlayer::State rider;rider.onBoat=true;rider.root=source->primary().key;rider.tick=40;
    rider.feet=scene->registry.navigation->boatBoarding+glm::dvec3(0,.005,0);
    ASSERT_TRUE(player.restore(rider));const auto support=player.supportingPart();ASSERT_TRUE(isValid(support));
    const auto oldFeet=player.feet();std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==support||link.b.part==support)cuts.push_back(link.id);
    ASSERT_FALSE(cuts.empty());AssemblyFractureIssue issue;
    const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto boat=CoveBoatAssembly::compileFragments(split->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(boat)<<error;
    auto target=CoveRigidRoots::prepare(*boat,error);ASSERT_TRUE(target);
    const auto ridden=target->indexForPart(*boat,support);ASSERT_TRUE(ridden);ASSERT_NE(target->roots()[*ridden].key,target->primary().key);
    ASSERT_TRUE(target->inheritFractureMotion(*boat,*split,*intact,*source,10,error))<<error;
    for(auto& child:target->roots())child.observed=child.spawn;
    CovePlayer destination;ASSERT_TRUE(destination.initialize(*scene,[](double,double){return -20.;},error));
    ASSERT_TRUE(target->transferCutPlayer(destination,*boat,player,origin,error))<<error;
    EXPECT_EQ(destination.state().root,target->roots()[*ridden].key);EXPECT_EQ(destination.supportingPart(),support);
    // Independently rounded float sector-local root origins permit sub-mm differences.
    EXPECT_LT(glm::length(destination.feet()-oldFeet),.00005);
    EXPECT_EQ(destination.state().tick,rider.tick);EXPECT_EQ(player.state().root,source->primary().key);
    auto airborne=player.state();airborne.mode=CovePlayer::Mode::Airborne;airborne.feet.y+=1;ASSERT_TRUE(player.restore(airborne));
    const auto before=destination.state();EXPECT_FALSE(target->transferCutPlayer(destination,*boat,player,origin,error));
    EXPECT_EQ(destination.state().feet,before.feet);EXPECT_EQ(destination.state().root,before.root);
}

TEST_F(CoveMovement, DetachedRiderFollowsItsSectionAndCannotStandOnAnotherSectionsOldDeck) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'r','i','d','e','r','-','r','o','o','t'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;
    const auto cut=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(cut);
    const auto boat=CoveBoatAssembly::compileFragments(cut->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);
    ASSERT_TRUE(boat)<<error;ASSERT_EQ(boat->roots().size(),2u);ASSERT_EQ(boat->primaryRootIndex(),1u);
    auto roots=CoveRigidRoots::prepare(*boat,error);ASSERT_TRUE(roots)<<error;
    for(size_t i=0;i<roots->roots().size();++i){
        const auto p=boat->assembly().mass().roots()[i].buildFromRoot.translation;
        roots->roots()[i].observed.position=physics::worldPositionFromAbsolute(glm::dvec3(p.x,p.y,p.z)*.02);
    }
    ASSERT_TRUE(roots->bindPlayer(player,*boat,{},error))<<error;
    // Pick a real top surface belonging only to the detached pontoon. These
    // are the actual cooked collision cells, not an invented support plane.
    CovePlayer::State rider;rider.onBoat=true;rider.root=roots->roots()[0].key;rider.tick=101;
    const auto p=boat->assembly().mass().roots()[0].buildFromRoot.translation;
    const auto anchor=glm::dvec3(p.x,p.y,p.z)*.02;bool found=false;
    for(const auto& cell:boat->roots()[0].shape.cells()){
        rider.feet=anchor+glm::dvec3((cell.minimum[0]+cell.maximum[0])*.01,cell.maximum[1]*.02+.005,
            (cell.minimum[2]+cell.maximum[2])*.01);
        if(!player.restore(rider))continue;
        auto phantom=rider;phantom.root=roots->primary().key;
        if(!player.restore(phantom)){found=true;break;}
    }
    ASSERT_TRUE(found);ASSERT_TRUE(player.restore(rider));EXPECT_EQ(player.supportingPart(),first);
    const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(12,4,-7))
        *glm::rotate(glm::dmat4(1),.6,glm::dvec3(0,1,0));
    ASSERT_TRUE(player.setBoatRootTransform(rider.root,transform));
    const auto expected=glm::dvec3(transform*glm::dvec4(rider.feet,1));
    EXPECT_LT(glm::length(player.feet()-expected),1e-9);
    const auto helmPose=glm::translate(glm::dmat4(1),glm::dvec3(-20,1,6));
    ASSERT_TRUE(player.setBoatRootTransform(roots->primary().key,helmPose));
    EXPECT_EQ(player.feet(),expected);
    EXPECT_LT(glm::length(player.helmPoint()-glm::dvec3(helmPose*glm::dvec4(scene->registry.navigation->helmStanding,1))),1e-9);
    auto invalid=rider;invalid.root={world,999};EXPECT_FALSE(player.restore(invalid));
    auto badPose=transform;badPose[0][0]=2;EXPECT_FALSE(player.setBoatRootTransform(rider.root,badPose));
    const auto oldMotion=roots->roots().back().observed;
    roots->roots().back().observed.originVelocity.x=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(roots->bindPlayer(player,*boat,{},error));
    EXPECT_EQ(player.feet(),expected);EXPECT_EQ(player.state().root,rider.root);EXPECT_EQ(player.state().tick,101u);
    roots->roots().back().observed=oldMotion;
    EXPECT_FALSE(roots->allAdmitted());EXPECT_EQ(roots->joinedTick(),0u);
}

TEST_F(CoveMovement, FragmentArchiveRestoresEveryPoseDetachedRiderAndWinchWithoutPublishing) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'r','e','s','t','o','r','e','-','r','o','o','t'}};const DurableId owner{world,1};
    auto seed=prepareCoveBuild(*scene,owner,4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    DurableId winch;for(const auto& part:seed->build.parts)
        if(std::holds_alternative<WinchModule>(seed->catalog.lookup(part.definition).definition->module))winch=part.id;
    ASSERT_TRUE(isValid(winch));const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)
        if(link.a.part==first||link.b.part==first||link.a.part==winch||link.b.part==winch)cuts.push_back(link.id);
    AssemblyFractureIssue fi;
    const auto cut=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,fi);ASSERT_TRUE(cut);
    const auto boat=CoveBoatAssembly::compileFragments(cut->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);
    ASSERT_TRUE(boat)<<error;ASSERT_EQ(boat->roots().size(),3u);ASSERT_EQ(boat->primaryRootIndex(),1u);
    const auto cargo=CoveBoatAssembly::compileCargo(*scene,scene->registry.navigation->cargoPlacements.front(),error);ASSERT_TRUE(cargo)<<error;
    class Unused final:public PreparationAdapter {
    public:
        size_t calls=0;
        PreparationResult begin(const PreparationRequest& request)override{++calls;return {request.ticket,PreparationState::Rejected};}
        PreparationResult poll(PreparationTicket ticket)noexcept override{++calls;return {ticket,PreparationState::Rejected};}
        bool canActivate(PreparationTicket)const noexcept override{return false;}
        void activate(PreparationTicket,SimulationTick)noexcept override{++calls;}
        void discard(PreparationTicket)noexcept override{++calls;}
    } adapter;
    SessionBootstrap bootstrap;bootstrap.world=world;bootstrap.caller={owner,{world,2}};bootstrap.lastIssuedId=seed->issuedThrough;
    bootstrap.tick=SimulationTick{900};bootstrap.inventory={24,0};bootstrap.builds.push_back(cut->afterBuild());
    bootstrap.starterEntitlements.push_back(seed->starterEntitlement);bootstrap.jobs.push_back({{world,3}});
    const auto definition=cargo->build().parts.front().definition;
    bootstrap.cargoDefinitions.push_back({definition,420,cargo->displacementCubicMetres(),{60,0},CargoRecoveryRule::PreserveUnique});
    bootstrap.cargo.push_back({{world,4},definition,owner,{-4.5,-3,-58},{},DurableId{world,3}});
    CoveSaveContext context;context.identity.world=world;context.identity.content.manifest={{{{92,1}},1},1};
    context.identity.content.manifestDigest[0]=std::byte{1};context.boat=seed->build.id;
    context.job={world,3};context.cargo={world,4};context.cargoDefinition=bootstrap.cargoDefinitions.front();context.origin={100,-200,300};
    CovePhysicalSave physical;physical.tick=bootstrap.tick;physical.origin=context.origin;physical.boat=context.boat;
    physical.cargo=context.cargo;physical.job=context.job;physical.cargoDefinition=definition;physical.controlPart=*intact->primaryRoot().helm;
    for(size_t i=0;i<boat->roots().size();++i){
        const auto index=static_cast<double>(i);
        CoveSavedRoot root;root.key=boat->assembly().mass().roots()[i].key;
        root.motion.position={100+10.*index,-195.+index,280.-12.*index};
        const auto q=glm::angleAxis(float(.2+.3*index),glm::normalize(glm::vec3(1,2,3)));
        root.motion.orientation=*canonicalQuaternion(q.x,q.y,q.z,q.w);
        root.motion.originVelocity={float(i),.1f,-.2f};root.motion.angularVelocity={.03f,.1f,float(.02*index)};
        physical.boatRoots.push_back(root);
    }
    physical.boatMotion=physical.boatRoots[boat->primaryRootIndex()].motion;physical.playerRoot=physical.boatRoots[0].key;
    physical.player.aboard=true;physical.player.mode=CoveSavedPlayerMode::Airborne;physical.player.feet={-.5,4,-54};physical.player.verticalSpeed=-1;
    physical.player.tick=900;physical.player.interactions=3;physical.water.seconds=12.5;physical.water.height=-200;
    physical.cargoMotion.position={121,-194,256};physical.cargoState=CoveSavedCargoState::Towed;physical.winchPart=winch;physical.ropeLength=6;
    EventStreamIncarnation incarnation;incarnation.bytes[0]=9;SessionIssue si;
    const auto session=GameSession::create(bootstrap,incarnation,seed->catalog,adapter,si);ASSERT_TRUE(session);
    RecoveryIssue ri;const auto checkpoint=SessionRecovery::capture(*session,context.identity.content,ri);ASSERT_TRUE(checkpoint);
    const auto admitted=SessionRecovery::admit(*checkpoint,context.identity,seed->catalog,ri);ASSERT_TRUE(admitted);
    CoveSaveIssue ci;std::vector<std::byte> bytes;
    ASSERT_TRUE(CoveSaveCodec::encode(*admitted,nullptr,physical,context,seed->catalog,bytes,ci))<<static_cast<int>(ci.error);
    const auto restored=CoveRestoreCandidate::prepare(bytes,context,*scene,seed->catalog,seed->placements,[](double,double){return -5.;},error);
    ASSERT_TRUE(restored)<<error;ASSERT_EQ(restored->roots->roots().size(),3u);
    EXPECT_EQ(restored->boat->build().parts,seed->build.parts);
    EXPECT_TRUE(std::equal(restored->boat->build().connections.begin(),restored->boat->build().connections.end(),
        cut->afterBuild().connections.begin(),cut->afterBuild().connections.end(),sameConnection));
    EXPECT_EQ(restored->scene->registry.connections.size(),scene->registry.connections.size()-cuts.size());
    EXPECT_FALSE(prepareCoveExpandedLaunchDesign(*scene,*scene,cut->afterBuild(),seed->placements,{},error));
    EXPECT_EQ(restored->roots->joinedTick(),0u);EXPECT_FALSE(restored->roots->allAdmitted());
    for(size_t i=0;i<physical.boatRoots.size();++i){
        const auto& expected=physical.boatRoots[i];const auto& root=restored->roots->roots()[i];
        EXPECT_EQ(root.key,expected.key);EXPECT_FALSE(root.body.valid());EXPECT_FALSE(root.shape.valid());EXPECT_EQ(root.admissionTick,0u);EXPECT_EQ(root.observedTick,0u);
        EXPECT_LT(glm::length(physics::worldPositionToAbsolute(root.observed.position)
            -glm::dvec3(expected.motion.position.x,expected.motion.position.y,expected.motion.position.z)),1e-6);
        EXPECT_EQ(root.observed.originVelocity,glm::vec3(expected.motion.originVelocity[0],expected.motion.originVelocity[1],expected.motion.originVelocity[2]));
    }
    const auto& rider=restored->roots->roots()[0];const auto anchor=boat->assembly().mass().roots()[0].buildFromRoot.translation;
    const auto local=glm::dvec3(physical.player.feet.x,physical.player.feet.y,physical.player.feet.z)-glm::dvec3(anchor.x,anchor.y,anchor.z)*.02;
    const auto expectedFeet=physics::worldPositionToAbsolute(rider.observed.position)-glm::dvec3(100,-200,300)
        +glm::normalize(glm::dquat(rider.observed.orientation))*local;
    EXPECT_LT(glm::length(restored->player->feet()-expectedFeet),1e-6);EXPECT_EQ(restored->player->state().root,rider.key);
    const auto winchRoot=boat->rootForPart(winch);ASSERT_TRUE(winchRoot);EXPECT_NE(*winchRoot,boat->primaryRootIndex());
    EXPECT_EQ(restored->towRoot,restored->roots->roots()[*winchRoot].key);
    physics::AuthoredFrameError frameError;
    const auto expectedAnchor=physics::AuthoredBodyFrame(boat->roots()[*winchRoot].shape).bodyPoint(restored->towBoatPoint,frameError);ASSERT_TRUE(expectedAnchor);
    EXPECT_EQ(restored->tow.localAnchorA,*expectedAnchor);EXPECT_FLOAT_EQ(restored->tow.targetLength,6);
    EXPECT_FALSE(restored->tow.bodyA.valid());EXPECT_FALSE(restored->tow.bodyB.valid());EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveMovement, RecoveryPreparesEveryCutPartWithoutChangingOwnershipOrMovingOnlySomeSections) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'r','e','c','o','v','e','r','-','a','l','l'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    std::vector<DurableId> cuts;for(const auto& link:seed->build.connections)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto boat=CoveBoatAssembly::compileFragments(split->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(boat)<<error;
    auto roots=CoveRigidRoots::prepare(*boat,error);ASSERT_TRUE(roots);ASSERT_EQ(roots->roots().size(),11u);
    // Every original part is a separate root. Recovery must keep the complete
    // layout's berth rather than deriving it from the isolated helm's waterline.
    ASSERT_EQ(boat->roots().size(),boat->build().parts.size());
    for(size_t i=0;i<roots->roots().size();++i){
        auto& root=roots->roots()[i];const auto offset=static_cast<double>(i);
        // CPU preparation only. The actual GPU recovery case obtains real handles/poses.
        root.body={static_cast<uint32_t>(i+1),2};root.shape={static_cast<uint32_t>(i+1),3,4};
        root.admissionTick=20;root.observedTick=100;
        root.observed.position=physics::worldPositionFromAbsolute({12000+offset*300,-200,-14000-offset*100});
        root.observed.orientation=glm::angleAxis(.8f,glm::normalize(glm::vec3(1,2,3)));
        root.observed.originVelocity={12,-3,9};root.observed.angularVelocity={.2f,-.4f,.6f};root.spawn.originVelocity={8,9,10};
    }
    const auto origin=physics::worldPositionFromAbsolute({100,-200,300});
    roots->roots().back().observedTick=99;EXPECT_FALSE(roots->prepareRecovery(*boat,origin,.25,100,error));
    roots->roots().back().observedTick=100;EXPECT_FALSE(roots->prepareRecovery(*intact,origin,.25,100,error));
    const auto lastBody=roots->roots().back().body;roots->roots().back().body=roots->primary().body;
    EXPECT_FALSE(roots->prepareRecovery(*boat,origin,.25,100,error));roots->roots().back().body=lastBody;
    roots->roots().back().observed.angularVelocity.x=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(roots->prepareRecovery(*boat,origin,.25,100,error));roots->roots().back().observed.angularVelocity.x=.2f;
    EXPECT_FALSE(roots->prepareRecovery(*boat,origin,std::numeric_limits<double>::infinity(),100,error));
    EXPECT_FALSE(roots->prepareRecovery(*boat,origin,.25,101,error));
    const auto recovered=roots->prepareRecovery(*boat,origin,.25,100,error);ASSERT_TRUE(recovered)<<error;
    ASSERT_EQ(recovered->count,11u);ASSERT_EQ(recovered->bodyCommands().size(),33u);EXPECT_EQ(recovered->sourceTick,100u);
    const double lift=intact->equilibriumRootHeight()-double(intact->primaryMassRoot().buildFromRoot.translation.y)*.02;
    for(size_t i=0;i<recovered->count;++i){
        const auto anchor=boat->assembly().mass().roots()[i].buildFromRoot.translation;
        const auto expected=glm::dvec3(100,-200,300)+glm::dvec3(anchor.x,anchor.y,anchor.z)*.02+glm::dvec3(0,lift+.25,0);
        EXPECT_LT(glm::length(physics::worldPositionToAbsolute(recovered->targets[i].position)-expected),.00002);
        EXPECT_EQ(recovered->targets[i].orientation,glm::quat(1,0,0,0));EXPECT_EQ(recovered->targets[i].originVelocity,glm::vec3(0));
        const auto& command=recovered->commands[3*i];EXPECT_EQ(command.body,roots->roots()[i].body);
        EXPECT_EQ(command.type,physics::PhysicsCommandType::Teleport);
        physics::AuthoredFrameError frameError;
        const auto actual=physics::AuthoredBodyFrame(boat->roots()[i].shape).rootMotion({
            .centerPosition={command.sector,glm::vec3(command.a)},.orientation={command.b.w,command.b.x,command.b.y,command.b.z}},frameError);
        ASSERT_TRUE(actual);EXPECT_LT(glm::length(physics::worldPositionToAbsolute(actual->position)-expected),.00002);
        EXPECT_EQ(recovered->commands[3*i+1].type,physics::PhysicsCommandType::SetVelocity);
        EXPECT_EQ(recovered->commands[3*i+2].type,physics::PhysicsCommandType::SetAngularVelocity);
        EXPECT_EQ(recovered->commands[3*i+1].a,glm::vec4(0));EXPECT_EQ(recovered->commands[3*i+2].a,glm::vec4(0));
        EXPECT_EQ(roots->roots()[i].spawn.originVelocity,glm::vec3(8,9,10));
        EXPECT_GT(glm::length(physics::worldPositionToAbsolute(roots->roots()[i].observed.position)-expected),10000);
    }
    EXPECT_EQ(roots->joinedTick(),100u);EXPECT_EQ(boat->build().parts,seed->build.parts);
    EXPECT_TRUE(std::all_of(boat->build().connections.begin(),boat->build().connections.end(),[](const auto& c){return !c.enabled;}));
}

TEST_F(CoveMovement, RecoveryReturnsAnUnbuoyantLoadWithoutInventingFlotation) {
    const auto placement=scene->registry.navigation->cargoPlacements.front();std::string error;
    const auto cargo=CoveBoatAssembly::compileCargo(*scene,placement,error);ASSERT_TRUE(cargo)<<error;
    auto roots=CoveRigidRoots::prepare(*cargo,error);ASSERT_TRUE(roots)<<error;
    auto& root=roots->primary();root.body={1,2};root.shape={1,3,4};root.admissionTick=1;root.observedTick=30;
    root.observed.position=physics::worldPositionFromAbsolute({-5000,-900,10000});
    const auto recovery=roots->prepareRecovery(*cargo,physics::worldPositionFromAbsolute({0,-200,0}),.1,30,error);ASSERT_TRUE(recovery)<<error;
    double bottom=std::numeric_limits<double>::infinity();
    for(const auto& cell:cargo->shape().cells())bottom=std::min(bottom,double(cell.minimum[1])*.02);
    EXPECT_NEAR(physics::worldPositionToAbsolute(recovery->targets[0].position).y+bottom,-199.85,.00002);
    EXPECT_LT(cargo->displacementCubicMetres()*1000,cargo->primaryMassRoot().mass.dryMassKg);
    EXPECT_EQ(roots->primary().observedTick,30u);
    EXPECT_EQ(physics::worldPositionToAbsolute(root.observed.position),glm::dvec3(-5000,-900,10000));
}

TEST_F(CoveMovement, RecoveryOfPaidLayoutStartsClearOfTheDock) {
    std::string error;auto design=*scene;
    for(int purchase=0;purchase<2;++purchase){
        auto workshop=CoveWorkshop::create(design,error);ASSERT_TRUE(workshop)<<error;
        ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
        design=workshop->design();
    }
    const auto boat=CoveBoatAssembly::compile(design,error);ASSERT_TRUE(boat)<<error;
    const auto scenery=CoveSceneryCollision::compile(*scene,error);ASSERT_TRUE(scenery)<<error;
    const auto anchor=boat->primaryMassRoot().buildFromRoot.translation;
    const glm::dvec3 home(double(anchor.x)*.02,boat->equilibriumRootHeight(),double(anchor.z)*.02);
    for(const auto& hull:boat->shape().cells())for(const auto& dock:scenery->shape().cells()){
        const auto lower=home+glm::dvec3(hull.minimum[0],hull.minimum[1],hull.minimum[2])*.02;
        const auto upper=home+glm::dvec3(hull.maximum[0],hull.maximum[1],hull.maximum[2])*.02;
        const auto dockLower=scenery->origin()+glm::dvec3(dock.minimum[0],dock.minimum[1],dock.minimum[2])*.02;
        const auto dockUpper=scenery->origin()+glm::dvec3(dock.maximum[0],dock.maximum[1],dock.maximum[2])*.02;
        const auto depth=glm::min(upper,dockUpper)-glm::max(lower,dockLower);
        EXPECT_FALSE(glm::all(glm::greaterThan(depth,glm::dvec3(.0001))))
            <<"home "<<home.x<<","<<home.y<<","<<home.z<<" overlap "<<depth.x<<","<<depth.y<<","<<depth.z;
    }
}

TEST_F(CoveMovement, LiveRootOwnerBoundsEverySectionAndRefusesMixedObservations) {
    using namespace construction;
    static_assert(CoveRigidRoots::maximumRoots==kMaximumCoveSavedRoots);
    std::string error;const WorldNamespace world{{'l','i','v','e','-','r','o','o','t','-','b','o','u','n','d'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact);
    auto build=seed->build;auto bindings=seed->placements;
    for(auto& link:build.connections){link.enabled=false;link.damage=kFullHealth;}
    const auto add=[&]{
        auto part=seed->build.parts.front();const auto index=build.parts.size();
        part.id={world,1000+index};part.provenance={};part.placement.translation.x=static_cast<int32_t>(index*200);
        build.parts.push_back(part);bindings.push_back({static_cast<uint32_t>(1000+index),part.id});
    };
    while(build.parts.size()<CoveRigidRoots::maximumRoots)add();
    for(size_t i=0;i<bindings.size();++i)bindings[i].placement=static_cast<uint32_t>(i);
    const auto assembly=CoveBoatAssembly::compileFragments(build,seed->catalog,bindings,*intact->primaryRoot().helm,error);ASSERT_TRUE(assembly)<<error;
    auto owned=CoveRigidRoots::prepare(*assembly,error);ASSERT_TRUE(owned)<<error;ASSERT_EQ(owned->roots().size(),32u);
    EXPECT_TRUE(owned->matches(*assembly));EXPECT_FALSE(owned->matches(*intact));
    EXPECT_FALSE(owned->indexForPart(*intact,intact->build().parts.front().id));
    EXPECT_FALSE(owned->indexForKey({world,999999}));EXPECT_EQ(owned->joinedTick(),0u);
    // These handles/ticks exercise only the host join predicate. The actual
    // GPU split case below supplies completed authored body observations.
    for(size_t i=0;i<owned->roots().size();++i){
        auto& root=owned->roots()[i];EXPECT_EQ(owned->indexForKey(root.key),i);
        root.body={static_cast<uint32_t>(2*i+1),3};root.shape={static_cast<uint32_t>(i+1),7,11};
        root.admissionTick=100;root.observedTick=100;
    }
    EXPECT_EQ(owned->joinedTick(),100u);
    auto& last=owned->roots().back();last.observedTick=99;EXPECT_EQ(owned->joinedTick(),0u);
    last.observedTick=101;EXPECT_EQ(owned->joinedTick(),0u);
    last.observedTick=100;last.retired=true;EXPECT_EQ(owned->joinedTick(),0u);
    last.retired=false;last.shape={};EXPECT_EQ(owned->joinedTick(),0u);
    uint32_t first=UINT32_MAX,end=0;owned->includeBodyRange(first,end);EXPECT_EQ(first,1u);EXPECT_EQ(end,63u);
    // Scene part capacity exceeds the independent 32-root owner budget.
    // Refuse the 33rd physical section without changing the existing owner.
    add();bindings.back().placement=static_cast<uint32_t>(bindings.size()-1);
    const auto tooMany=CoveBoatAssembly::compileFragments(build,seed->catalog,bindings,*intact->primaryRoot().helm,error);
    ASSERT_TRUE(tooMany)<<error;EXPECT_FALSE(CoveRigidRoots::prepare(*tooMany,error));EXPECT_FALSE(error.empty());EXPECT_TRUE(owned->matches(*assembly));
}

TEST_F(CoveMovement, CutMotionStagesAllIndependentParentsOrLeavesEveryChildUnchanged) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','u','t','-','o','w','n','e','r','-','m','o','t','i','o','n'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto cutsAt=[&](const BuildSnapshot& build,DurableId part){
        std::vector<DurableId> cuts;
        for(const auto& link:build.connections)if(link.enabled&&(link.a.part==part||link.b.part==part))cuts.push_back(link.id);
        return cuts;
    };
    AssemblyFractureIssue issue;
    const auto firstCuts=cutsAt(seed->build,seed->build.parts[0].id);
    const auto first=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,firstCuts,seed->catalog,issue);ASSERT_TRUE(first);
    const auto source=CoveBoatAssembly::compileFragments(first->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);
    ASSERT_TRUE(source)<<error;ASSERT_EQ(source->roots().size(),2u);
    const auto nextCuts=cutsAt(source->build(),seed->build.parts[1].id);ASSERT_FALSE(nextCuts.empty());
    const auto next=AssemblyFracturePlan::prepare(source->build(),source->build().revision,nextCuts,seed->catalog,issue);ASSERT_TRUE(next);
    const auto destination=CoveBoatAssembly::compileFragments(next->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);
    ASSERT_TRUE(destination)<<error;ASSERT_EQ(destination->roots().size(),4u);
    auto parents=CoveRigidRoots::prepare(*source,error),children=CoveRigidRoots::prepare(*destination,error);
    ASSERT_TRUE(parents);ASSERT_TRUE(children);
    for(size_t i=0;i<parents->roots().size();++i) {
        auto& root=parents->roots()[i];
        // CPU frame/transaction validation only. The separate native GPU cut
        // obtains handles and completed poses from the actual backend.
        root.body={uint32_t(i+1),2};root.shape={uint32_t(i+1),3,4};root.admissionTick=50;root.observedTick=60;
        const auto offset=static_cast<double>(i);
        root.observed.position=physics::worldPositionFromAbsolute({120.0+40*offset,-200.0,30.0-20*offset});
        root.observed.orientation=glm::angleAxis(float(.2+.3*offset),glm::normalize(glm::vec3(1,2,3)));
        root.observed.originVelocity={float(2+offset),-.2f,float(-1.0-offset)};
        root.observed.angularVelocity={.03f,float(.2+.1*offset),-.04f};
    }
    for(auto& child:children->roots())child.spawn.originVelocity={9,8,7};
    const auto unchanged=[&]{for(const auto& child:children->roots())EXPECT_EQ(child.spawn.originVelocity,glm::vec3(9,8,7));};
    auto& last=parents->roots().back();last.observedTick=59;
    EXPECT_FALSE(children->inheritFractureMotion(*destination,*next,*source,*parents,60,error));unchanged();
    last.observedTick=60;const auto velocity=last.observed.originVelocity;
    last.observed.originVelocity.x=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(children->inheritFractureMotion(*destination,*next,*source,*parents,60,error));unchanged();
    last.observed.originVelocity=velocity;
    EXPECT_FALSE(children->inheritFractureMotion(*destination,*first,*source,*parents,60,error));unchanged();
    ASSERT_TRUE(children->inheritFractureMotion(*destination,*next,*source,*parents,60,error))<<error;
    for(size_t i=0;i<children->roots().size();++i) {
        const auto binding=next->fragments()[i];const auto& parent=parents->roots()[binding.parentRoot].observed;
        const auto& child=children->roots()[i].spawn;const auto& p=binding.parentFromRoot;
        const auto offset=glm::dquat(parent.orientation)*glm::dvec3(p[0],p[1],p[2]);
        EXPECT_LT(glm::length(physics::worldPositionToAbsolute(child.position)
            -(physics::worldPositionToAbsolute(parent.position)+offset)),.00002);
        EXPECT_LT(glm::length(glm::dvec3(child.originVelocity)
            -(glm::dvec3(parent.originVelocity)+glm::cross(glm::dvec3(parent.angularVelocity),offset))),.00001);
        EXPECT_EQ(child.orientation,parent.orientation);EXPECT_EQ(child.angularVelocity,parent.angularVelocity);
        EXPECT_FALSE(children->roots()[i].body.valid());EXPECT_EQ(children->roots()[i].observedTick,0u);
    }
    EXPECT_EQ(children->joinedTick(),0u); // Prepared motion is never completed evidence.
}

TEST_F(CoveMovement, PaidAdditionLaunchMappingChargesOnceAndUndoRestoresItsExactIdentity) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'p','a','i','d','-','c','o','v','e','-','t','e','s','t','0','1'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    class PaidAdapter final:public PreparationAdapter {
    public:
        const assets::LoadedAssetFixture* original=nullptr;
        const PartCatalog* catalog=nullptr;
        std::vector<CoveBoatAssembly::Part> initial;
        assets::LoadedAssetFixture live;
        std::optional<CoveLaunchDesign> pending;
        std::unique_ptr<CoveBoatAssembly> boat,prepared;
        std::string why;
        PreparationResult begin(const PreparationRequest& request) override {
            if(!request.changedBuild)return {request.ticket,PreparationState::Rejected};
            pending=prepareCoveExpandedLaunchDesign(*original,live,*request.changedBuild,initial,boat->parts(),why);
            if(pending)prepared=CoveBoatAssembly::compileBuild(*request.changedBuild,*catalog,pending->placements,why);
            return {request.ticket,prepared?PreparationState::Ready:PreparationState::Rejected};
        }
        PreparationResult poll(PreparationTicket ticket) noexcept override {return {ticket,prepared?PreparationState::Ready:PreparationState::Rejected};}
        bool canActivate(PreparationTicket) const noexcept override {return bool(prepared);}
        void activate(PreparationTicket,SimulationTick) noexcept override {
            live=std::move(pending->scene);boat=std::move(prepared);pending.reset();
        }
        void discard(PreparationTicket) noexcept override {prepared.reset();pending.reset();}
    } adapter;
    adapter.original=scene.get();adapter.catalog=&seed->catalog;adapter.initial=seed->placements;adapter.live=*scene;
    adapter.boat=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(adapter.boat)<<error;
    SessionBootstrap boot;boot.world=world;boot.caller={{world,1},{world,2}};boot.lastIssuedId=seed->issuedThrough;
    boot.workshopEnabled=true;boot.builds={seed->build};boot.starterEntitlements={seed->starterEntitlement};boot.inventory=coveStartingMaterials;
    SessionIssue issue;auto session=GameSession::create(boot,EventStreamIncarnation{{1}},seed->catalog,adapter,issue);ASSERT_TRUE(session);
    auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_EQ(workshop->catalogName(),"Pontoon");const auto cost=workshop->catalogCost();EXPECT_EQ(cost.salvageMaterial,24u);
    ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->valid())<<workshop->message();
    ASSERT_EQ(workshop->selected(),25u);ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
    const auto quote=quoteCoveDesign(workshop->design(),*adapter.boat,seed->catalog);ASSERT_TRUE(quote);
    EXPECT_EQ(quote->debit,cost);EXPECT_EQ(quote->credit,ResourceAmounts{});EXPECT_TRUE(quote->affordable(boot.inventory));
    const auto proposed=prepareCoveRefit(workshop->design(),*adapter.boat,seed->catalog,error,scene->registry.navigation->boatPlacements);
    ASSERT_TRUE(proposed)<<error;
    ASSERT_EQ(proposed->design->parts().size(),12u);EXPECT_FALSE(isValid(proposed->design->parts().back().source));
    const auto submit=[&](Intent intent) {
        const Command request{AuthorityEpoch{1},RequestSequence{session->processedSequence()+1},session->snapshot().revision,std::move(intent)};
        const auto result=session->submit(boot.caller,request);EXPECT_EQ(result.state,ReceiptState::PendingPreparation)<<adapter.why;
        if(result.state!=ReceiptState::PendingPreparation)return false;
        return session->advanceOneTick();
    };
    ASSERT_TRUE(submit(RefitBuild{{seed->build.id,{}},proposed->design}));
    auto accepted=session->snapshot();ASSERT_EQ(accepted.builds.at(0).parts.size(),12u);
    EXPECT_EQ(accepted.inventory.salvageMaterial,48u-cost.salvageMaterial);
    const auto paid=std::find_if(accepted.builds.at(0).parts.begin(),accepted.builds.at(0).parts.end(),[](const auto& p){return p.provenance.origin==PartOrigin::Paid;});
    ASSERT_NE(paid,accepted.builds.at(0).parts.end());const auto paidId=paid->id;
    EXPECT_GT(paidId.counter,seed->issuedThrough);EXPECT_EQ(paid->owningBuild,seed->build.id);
    EXPECT_EQ(adapter.boat->parts().back().placement,25u);EXPECT_EQ(adapter.boat->parts().back().id,paidId);
    EXPECT_TRUE(workshop->matchesDesign(adapter.live));
    std::vector<uint32_t> boatSlots=scene->registry.navigation->boatPlacements;boatSlots.push_back(25);
    CovePlayer expanded;ASSERT_TRUE(expanded.initialize(adapter.live,[](double,double){return -5.;},error,boatSlots))<<error;
    const auto twelveBoxes=expanded.collisionBoxes();EXPECT_GT(twelveBoxes,player.collisionBoxes());
    auto history=session->history();ASSERT_TRUE(history.undo);
    ASSERT_TRUE(submit(Undo{history.undo->target,history.undo->entry,history.generation}));
    EXPECT_EQ(session->snapshot().inventory,boot.inventory);EXPECT_EQ(adapter.boat->parts().size(),11u);
    ASSERT_TRUE(expanded.initialize(adapter.live,[](double,double){return -5.;},error,boatSlots))<<error;
    EXPECT_EQ(expanded.collisionBoxes(),player.collisionBoxes());
    history=session->history();ASSERT_TRUE(history.redo);
    ASSERT_TRUE(submit(Redo{history.redo->target,history.redo->entry,history.generation}));
    EXPECT_EQ(session->snapshot().inventory.salvageMaterial,48u-cost.salvageMaterial);
    EXPECT_EQ(adapter.boat->parts().back().id,paidId);EXPECT_EQ(adapter.boat->parts().back().placement,25u);
    EXPECT_EQ(adapter.live.registry.placements.size(),26u);
    auto second=CoveWorkshop::create(adapter.live,error,25);ASSERT_TRUE(second)<<error;
    ASSERT_TRUE(second->addPart());ASSERT_TRUE(second->valid())<<second->message();
    boatSlots.push_back(26);
    ASSERT_TRUE(expanded.initialize(second->preview(),[](double,double){return -5.;},error,boatSlots))<<error;
    auto added=CoveWorkshop::create(adapter.live,error,25);ASSERT_TRUE(added)<<error;
    for(unsigned i=0;i<12 && added->selected()!=25;++i)ASSERT_TRUE(added->command(CoveWorkshop::Action::Next));
    ASSERT_EQ(added->selected(),25u);ASSERT_TRUE(added->command(CoveWorkshop::Action::Remove));ASSERT_TRUE(added->command(CoveWorkshop::Action::Keep));
    const auto removalQuote=quoteCoveDesign(added->design(),*adapter.boat,seed->catalog);ASSERT_TRUE(removalQuote);
    EXPECT_EQ(removalQuote->debit,ResourceAmounts{});EXPECT_GT(removalQuote->credit.salvageMaterial,0u);
    EXPECT_LT(removalQuote->credit.salvageMaterial,cost.salvageMaterial);
    const auto removal=prepareCoveRefit(added->design(),*adapter.boat,seed->catalog,error,scene->registry.navigation->boatPlacements);ASSERT_TRUE(removal)<<error;
    ASSERT_TRUE(submit(RefitBuild{{seed->build.id,adapter.boat->build().revision},removal->design}));
    EXPECT_EQ(session->snapshot().inventory.salvageMaterial,48u-cost.salvageMaterial+removalQuote->credit.salvageMaterial);
    EXPECT_EQ(adapter.boat->parts().size(),11u);
    EXPECT_TRUE(CoveWorkshop::create(adapter.live,error,25)->canAdd()); // Dormant paid slot can be reused after Launch.
}

TEST_F(CoveMovement, AddedGhostCanBeCanceledWithoutChangingBlueprintOrSpending) {
    std::string error;auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    ASSERT_TRUE(workshop->addPart());EXPECT_TRUE(workshop->changed());EXPECT_FALSE(workshop->canAdd());
    ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Revert));
    EXPECT_TRUE(workshop->matchesDesign(*scene));EXPECT_EQ(workshop->preview().registry.placements.size(),25u);
    EXPECT_FALSE(workshop->changed());EXPECT_TRUE(workshop->canAdd());
    ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Remove));
    EXPECT_TRUE(workshop->matchesDesign(*scene));EXPECT_FALSE(workshop->changed());EXPECT_LT(workshop->selected(),25u);
    EXPECT_EQ(workshop->undoCount(),0u);EXPECT_EQ(workshop->revision(),0u);
}

TEST_F(CoveMovement, WorkshopRefusesDisconnectedChangeWithoutEditingCraft) {
    std::string error;auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    using Action=CoveWorkshop::Action;
    ASSERT_EQ(workshop->selectedName(),"Winch");const auto selected=workshop->selected();
    const auto before=scene->registry.placements[selected].placement;
    ASSERT_TRUE(workshop->command(Action::Raise));EXPECT_FALSE(workshop->valid());
    EXPECT_FALSE(workshop->command(Action::Keep));EXPECT_EQ(workshop->revision(),0u);
    EXPECT_EQ(workshop->design().registry.placements[selected].placement,before);
    EXPECT_EQ(scene->registry.placements[selected].placement,before);
    ASSERT_TRUE(workshop->command(Action::Revert));EXPECT_TRUE(workshop->valid());
    EXPECT_FALSE(workshop->changed());EXPECT_DOUBLE_EQ(workshop->massKg(),1035);
}

TEST_F(CoveMovement, WorkshopSnapsWinchToRealFreeSocketAndUndoRestoresExactDesign) {
    std::string error;auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    using Action=CoveWorkshop::Action;
    const auto selected=workshop->selected();const auto before=workshop->design().registry.placements[selected].placement;
    ASSERT_TRUE(workshop->command(Action::Snap))<<workshop->message();ASSERT_TRUE(workshop->valid());
    ASSERT_NE(workshop->preview().registry.placements[selected].placement,before);
    ASSERT_TRUE(workshop->command(Action::Keep));EXPECT_EQ(workshop->revision(),1u);EXPECT_EQ(workshop->undoCount(),1u);
    EXPECT_DOUBLE_EQ(workshop->massKg(),1035);EXPECT_EQ(scene->registry.placements[selected].placement,before);
    const auto boat=CoveBoatAssembly::compile(workshop->design(),error);ASSERT_TRUE(boat)<<error;
    EXPECT_EQ(boat->parts().size(),11u);EXPECT_EQ(boat->assembly().mass().roots().size(),1u);
    ASSERT_TRUE(workshop->command(Action::Undo));EXPECT_EQ(workshop->revision(),2u);
    EXPECT_EQ(workshop->design().registry.placements[selected].placement,before);EXPECT_EQ(workshop->undoCount(),0u);
    EXPECT_EQ(workshop->design().registry.connections.size(),scene->registry.connections.size());
}

TEST_F(CoveMovement, WorkshopRemovesCradleAndUndoRestoresItsMassAndConnections) {
    std::string error;auto workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
    using Action=CoveWorkshop::Action;
    ASSERT_TRUE(workshop->command(Action::Next));ASSERT_EQ(workshop->selectedName(),"Cargo cradle");
    ASSERT_TRUE(workshop->command(Action::Remove));ASSERT_TRUE(workshop->valid())<<workshop->message();
    EXPECT_LT(workshop->massKg(),1035);ASSERT_TRUE(workshop->command(Action::Keep));
    EXPECT_EQ(workshop->design().registry.navigation->boatPlacements.size(),10u);
    ASSERT_TRUE(workshop->command(Action::Undo));EXPECT_DOUBLE_EQ(workshop->massKg(),1035);
    EXPECT_EQ(workshop->design().registry.navigation->boatPlacements.size(),11u);
    EXPECT_EQ(scene->registry.navigation->boatPlacements.size(),11u);
}

TEST_F(CoveMovement, CompilesActualBoatMassHullFlotationAndPartTransforms) {
    std::string error;
    const auto boat = CoveBoatAssembly::compile(*scene, error);
    ASSERT_TRUE(boat) << error;
    ASSERT_EQ(boat->parts().size(), 11u);
    const auto& assembly = boat->assembly();
    ASSERT_EQ(assembly.mass().roots().size(), 1u);
    EXPECT_EQ(assembly.mass().roots()[0].partCount, 11u);
    double expectedMass = 0;
    for (const auto& binding : boat->parts()) {
        const auto& placed = scene->registry.placements[binding.placement];
        expectedMass += scene->bundles[placed.bundleIndex]->sidecar().part.mass.dryMassKg;
        const auto found = std::find_if(assembly.mass().parts().begin(), assembly.mass().parts().end(),
            [&](const auto& part) { return part.part == binding.id; });
        ASSERT_NE(found, assembly.mass().parts().end());
        const auto restored = construction::compose(assembly.mass().roots()[0].buildFromRoot, found->rootFromPart);
        ASSERT_TRUE(restored); EXPECT_EQ(*restored, placed.placement);
    }
    EXPECT_DOUBLE_EQ(assembly.mass().roots()[0].mass.dryMassKg, expectedMass);
    EXPECT_NEAR(1.0 / double(boat->shape().packedMass().centerInverseMass[3]), expectedMass, .001);
    EXPECT_GT(boat->displacementCubicMetres() * 1000, expectedMass);
    const double draft=boat->equilibriumRootHeight();
    double submerged=0;
    for (const auto& cell:assembly.buoyancy().roots()[0].coverage.cells()) {
        const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
        const double height=std::max(0.0,std::min(double(b.y)*.02,-draft)-double(a.y)*.02);
        submerged+=height*double(b.x-a.x)*double(b.z-a.z)*.0004;
    }
    EXPECT_NEAR(submerged*1000,expectedMass,1e-6);
    EXPECT_GT(boat->shape().faces().size(), 6u);
    EXPECT_EQ(assembly.functions().modules().size(), 11u);
    for (const auto& face : boat->shape().faces()) {
        const auto* source = assembly.collision().source(face.source);
        ASSERT_NE(source, nullptr);
        EXPECT_TRUE(std::any_of(boat->parts().begin(), boat->parts().end(),
            [&](const auto& part) { return part.id == source->part; }));
    }
}

TEST_F(CoveMovement, SalvageLoadHasIndependentMassTowEyeAndNoStaticDuplicate) {
    std::string error;ASSERT_EQ(scene->registry.navigation->cargoPlacements.size(),1u);
    const auto ordinal=scene->registry.navigation->cargoPlacements[0];
    const auto cargo=CoveBoatAssembly::compileCargo(*scene,ordinal,error);ASSERT_TRUE(cargo)<<error;
    const auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    const auto scenery=CoveSceneryCollision::compile(*scene,error);ASSERT_TRUE(scenery)<<error;
    EXPECT_DOUBLE_EQ(cargo->assembly().mass().roots()[0].mass.dryMassKg,420);
    EXPECT_DOUBLE_EQ(boat->assembly().mass().roots()[0].mass.dryMassKg,1035);
    EXPECT_LT(cargo->displacementCubicMetres()*1000,420); // Flooded machine sinks; don't invent sealed flotation.
    EXPECT_TRUE(std::any_of(cargo->assembly().functions().frames().begin(),cargo->assembly().functions().frames().end(),
        [](const auto& frame){return frame.kind==construction::AssemblyFrameKind::TowEye;}));
    EXPECT_TRUE(std::none_of(scenery->sources().begin(),scenery->sources().end(),
        [&](const auto& source){return source.placement==ordinal;}));
    EXPECT_EQ(scenery->sources().size(),13u);
    auto invalid=*scene;invalid.registry.connections.push_back({0,ordinal,construction::SocketId{101},construction::SocketId{10}});
    EXPECT_FALSE(CoveBoatAssembly::compileCargo(invalid,ordinal,error));
}

#if defined(VOXY_NATIVE)
TEST_F(CoveMovement, WaterPhaseIsBoundToOwnedTickAndSurvivesLargeRestoreEpoch) {
    std::string error;
    const auto boat = CoveBoatAssembly::compile(*scene, error);
    ASSERT_TRUE(boat) << error;
    gpu::Context gpu;
    ASSERT_TRUE(gpu.initHeadless());
    // A zero spectral texture isolates the shipping analytical swell forces.
    auto textureDesc = gpu::TextureDesc::tex2D(1, 1, WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding, "clock_test_zero_spectrum");
    textureDesc.depthOrArrayLayers = 4;
    const auto texture = gpu::createTexture(gpu.getDevice(), textureDesc);
    ASSERT_NE(texture, nullptr);
    gpu::TextureViewDesc viewDesc;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.arrayLayerCount = 4;
    const auto view = gpu::createTextureView(texture, viewDesc);
    const auto sampler = gpu::createSampler(gpu.getDevice(), gpu::SamplerDesc::repeat());
    ASSERT_NE(view, nullptr); ASSERT_NE(sampler, nullptr);
    std::array<glm::vec3, 3> velocities;
    for (size_t run = 0; run < velocities.size(); ++run) {
        physics::GpuPhysicsBackend backend;
        physics::PhysicsInitContext config;
        config.device = gpu.getDevice(); config.queue = gpu.getQueue();
        config.maxBodies = 8; config.maxActiveBodies = 8;
        config.maxPairs = 32; config.maxContacts = 64; config.maxManifolds = 64;
        config.gpu.commandCapacity = 32; config.gpu.maximumCatchUpTicks = 1;
        config.gpu.initialTick = run == 1 ? (uint64_t{1} << 60) + 600 : 0;
        ASSERT_TRUE(backend.initialize(config));
        EXPECT_FALSE(backend.stageWaterGpuFrame({0, config.gpu.initialTick, 20.5f}));
        ASSERT_EQ(backend.enableAuthoredShapeResources({}), physics::ShapeResourceError::None);
        auto& resources = *backend.authoredShapeResources();
        const auto drain = [&] { (void)wgpuDevicePoll(gpu.getDevice(), true, nullptr); resources.poll(); backend.stepCpu(0); };
        drain();
        physics::ShapeResourceError issue;
        auto shape = boat->shape();
        const auto handle = resources.upload(std::move(shape), issue);
        ASSERT_TRUE(handle.valid()); drain();
        physics::AuthoredRootMotion initial;
        initial.position = physics::worldPositionFromAbsolute({0, -200 + boat->equilibriumRootHeight(), 0});
        const auto body = backend.spawnAuthoredBody({.shape = handle, .motion = initial});
        ASSERT_TRUE(body);
        ASSERT_EQ(backend.configureAuthoredWaterBody(boat->primaryRoot().water(body.body)), physics::AuthoredBodyError::None);
        backend.setWaterPlane(-200, true);
        backend.setWaterGpuResources({view, sampler, .08f});
        for (int step = 0; step < 3; ++step) {
            ASSERT_TRUE(backend.scheduleFixedTicks(1));
            if (step == 2) backend.requestDebugSnapshot({body.body.index, 1});
            const auto ticket = backend.prepareGpuSubmission(issue); ASSERT_TRUE(ticket.valid());
            const auto frontier = backend.tickFrontier();
            const physics::WaterGpuFrame frame{frontier.incarnation, frontier.scheduled, run == 2 ? 100.f : 20.5f};
            EXPECT_FALSE(backend.stageWaterGpuFrame({frame.incarnation + 1, frame.tick, frame.phaseSeconds}));
            EXPECT_FALSE(backend.stageWaterGpuFrame({frame.incarnation, frame.tick - 1, frame.phaseSeconds}));
            EXPECT_FALSE(backend.stageWaterGpuFrame({frame.incarnation, frame.tick, std::numeric_limits<float>::quiet_NaN()}));
            EXPECT_FALSE(backend.stageWaterGpuFrame({frame.incarnation, frame.tick, 4096}));
            ASSERT_TRUE(backend.stageWaterGpuFrame(frame));
            WGPUCommandEncoderDescriptor ed{};
            const auto encoder = wgpuDeviceCreateCommandEncoder(gpu.getDevice(), &ed); ASSERT_NE(encoder, nullptr);
            const auto report = backend.encodeGpuStepChecked(encoder); ASSERT_TRUE(report.succeeded());
            EXPECT_FALSE(backend.stageWaterGpuFrame(frame));
            WGPUCommandBufferDescriptor cd{};
            const auto command = wgpuCommandEncoderFinish(encoder, &cd); ASSERT_NE(command, nullptr);
            ASSERT_EQ(backend.submitGpuSubmission(ticket, std::span{&command, 1}), physics::ShapeResourceError::None);
            wgpuCommandBufferRelease(command); wgpuCommandEncoderRelease(encoder); drain();
        }
        std::optional<physics::DebugSnapshot> snapshot;
        for (int poll = 0; poll < 100 && !snapshot; ++poll) {
            drain(); snapshot = backend.pollDebugSnapshot();
        }
        ASSERT_TRUE(snapshot);
        ASSERT_EQ(snapshot->bodies.size(), 1u); ASSERT_TRUE(snapshot->bodies[0].alive);
        velocities[run] = snapshot->bodies[0].linearVelocity;
        // A previous phase cannot silently authorize the following physics tick.
        ASSERT_TRUE(backend.scheduleFixedTicks(1));
        const auto ticket = backend.prepareGpuSubmission(issue); ASSERT_TRUE(ticket.valid());
        WGPUCommandEncoderDescriptor ed{};
        const auto encoder = wgpuDeviceCreateCommandEncoder(gpu.getDevice(), &ed); ASSERT_NE(encoder, nullptr);
        const auto rejected = backend.encodeGpuStepChecked(encoder);
        EXPECT_EQ(rejected.status, physics::PhysicsEncodeStatus::WaterFrameUnavailable);
        EXPECT_TRUE(rejected.failStopped);
        (void)backend.discardGpuSubmission(ticket);
        wgpuCommandEncoderRelease(encoder);
        backend.shutdown();
    }
    EXPECT_LT(glm::length(velocities[0] - velocities[1]), 1e-6f);
    EXPECT_GT(glm::length(velocities[0] - velocities[2]), .001f);
    wgpuSamplerRelease(sampler); wgpuTextureViewRelease(view); wgpuTextureRelease(texture);
}

TEST_F(CoveMovement, ActualRecoveryMovesRemoteSectionsTogetherOrLeavesEveryBodyInPlace) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'g','p','u','-','r','e','c','o','v','e','r'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto cut=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(cut);
    const auto boat=CoveBoatAssembly::compileFragments(cut->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(boat)<<error;
    auto roots=CoveRigidRoots::prepare(*boat,error);ASSERT_TRUE(roots);ASSERT_EQ(roots->roots().size(),2u);ASSERT_EQ(boat->primaryRootIndex(),1u);
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());physics::GpuPhysicsBackend backend;physics::PhysicsInitContext config;
    config.device=gpu.getDevice();config.queue=gpu.getQueue();config.maxBodies=8;config.maxActiveBodies=8;
    config.maxPairs=16;config.maxContacts=128;config.maxManifolds=128;config.gpu.commandCapacity=8;
    config.gpu.debugReadbackBodyCapacity=2;config.gpu.gravity={0,0,0};config.gpu.maximumCatchUpTicks=1;
    config.gpu.linearDamping=0;config.gpu.angularDamping=0;
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(-200,false);
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();physics::ShapeResourceError resourceError;
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};drain();
    const auto step=[&]{
        backend.stepCpu(1.0f/60.0f);
        const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return submitted==physics::ShapeResourceError::None;
    };
    for(size_t i=0;i<2;++i){
        auto shape=boat->roots()[i].shape;roots->roots()[i].shape=resources.upload(std::move(shape),resourceError);
        ASSERT_TRUE(roots->roots()[i].shape.valid());
    }
    drain();
    for(size_t i=0;i<2;++i){
        auto& root=roots->roots()[i];const auto offset=static_cast<double>(i);
        physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute({12000-30000*offset,-199,-8000+21000*offset});
        motion.orientation=glm::angleAxis(.3f,glm::normalize(glm::vec3(1,2,3)));
        motion.originVelocity={2,3,-4};motion.angularVelocity={.1f,.2f,-.3f};
        const auto body=backend.spawnAuthoredBody({.shape=root.shape,.motion=motion});ASSERT_TRUE(body);
        root.body=body.body;root.admissionTick=1;
    }
    const auto observe=[&]()->std::optional<physics::DebugSnapshot>{
        backend.requestDebugSnapshot({1,2});if(!step())return {};
        for(int attempt=0;attempt<100;++attempt){
            auto snapshot=backend.pollDebugSnapshot();drain();if(!snapshot)continue;
            if(snapshot->bodies.size()!=2)return {};
            for(const auto& body:snapshot->bodies){
                const auto found=std::find_if(roots->roots().begin(),roots->roots().end(),[&](const auto& root){return root.body==body.handle;});
                if(found==roots->roots().end()||!body.alive||body.authoredShape!=found->shape)return {};
                const auto index=static_cast<size_t>(found-roots->roots().begin());physics::AuthoredFrameError frameError;
                const auto motion=physics::AuthoredBodyFrame(boat->roots()[index].shape).rootMotion({
                    .centerPosition={body.sector,body.position},.orientation=body.orientation,
                    .centerVelocity=body.linearVelocity,.angularVelocity=body.angularVelocity},frameError);
                if(!motion)return {};
                found->observed=*motion;found->observedTick=snapshot->tick;
            }
            return snapshot;
        }
        return {};
    };
    ASSERT_TRUE(observe());const auto origin=physics::worldPositionFromAbsolute({0,-200,0});
    for(int fault=0;fault<3;++fault){
        const auto recovery=roots->prepareRecovery(*boat,origin,0,roots->joinedTick(),error);ASSERT_TRUE(recovery)<<error;
        std::vector<physics::PhysicsCommand> commands(recovery->bodyCommands().begin(),recovery->bodyCommands().end());
        ASSERT_TRUE(backend.setSchedulingPaused(true));
        if(fault==0){while(commands.size()<9)commands.push_back(commands.back());}
        if(fault==1)++commands.back().body.generation;
        const auto prepared=backend.prepareMutationBatch({.bodyCommands=commands,
            .joinedBoundary=physics::PhysicsMutationJoin{backend.tickFrontier().incarnation,roots->joinedTick()}});
        if(fault==0){EXPECT_EQ(prepared.status,physics::PhysicsMutationStatus::CapacityExceeded);}
        if(fault==1){EXPECT_EQ(prepared.status,physics::PhysicsMutationStatus::InvalidInput);}
        if(fault==2){ASSERT_TRUE(prepared);ASSERT_TRUE(backend.discardPrepared(prepared));}
        ASSERT_TRUE(backend.setSchedulingPaused(false));ASSERT_TRUE(observe());
        for(size_t i=0;i<2;++i){
            EXPECT_GT(glm::length(physics::worldPositionToAbsolute(roots->roots()[i].observed.position)
                -physics::worldPositionToAbsolute(recovery->targets[i].position)),10000);
            EXPECT_GT(glm::length(roots->roots()[i].observed.originVelocity),1);
        }
    }
    const auto recovery=roots->prepareRecovery(*boat,origin,0,roots->joinedTick(),error);ASSERT_TRUE(recovery)<<error;
    ASSERT_TRUE(backend.setSchedulingPaused(true));
    const auto prepared=backend.prepareMutationBatch({.bodyCommands=recovery->bodyCommands(),
        .joinedBoundary=physics::PhysicsMutationJoin{backend.tickFrontier().incarnation,roots->joinedTick()}});ASSERT_TRUE(prepared);
    const auto result=backend.commitPrepared(prepared);ASSERT_TRUE(result);EXPECT_EQ(result.bodyCommandCount,6u);
    ASSERT_TRUE(backend.setSchedulingPaused(false));const auto recovered=observe();ASSERT_TRUE(recovered);
    EXPECT_EQ(recovered->tick,recovery->sourceTick+1);EXPECT_EQ(roots->joinedTick(),recovered->tick);
    for(size_t i=0;i<2;++i){
        const auto& actual=roots->roots()[i].observed;
        EXPECT_LT(glm::length(physics::worldPositionToAbsolute(actual.position)-physics::worldPositionToAbsolute(recovery->targets[i].position)),.0001);
        EXPECT_GT(std::abs(actual.orientation.w),.99999f);
        EXPECT_LT(glm::length(actual.originVelocity),.00001f);EXPECT_LT(glm::length(actual.angularVelocity),.00001f);
    }
    EXPECT_EQ(boat->build().parts,seed->build.parts);EXPECT_EQ(backend.stats().authoredAdmissionFailures,0u);
    for(const auto& root:roots->roots())ASSERT_TRUE(backend.destroyBody(root.body));
    ASSERT_TRUE(step());for(const auto& root:roots->roots())ASSERT_EQ(resources.retire(root.shape),physics::ShapeResourceError::None);
    drain();resources.close();drain();EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed);backend.shutdown();
}

TEST_F(CoveMovement, CutBodyAndWaterCapacityRefusalCancelsEveryChildAndReusesAllReservations) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'c','u','t','-','c','a','p','a','c','i','t','y'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto pieces=CoveBoatAssembly::compileFragments(split->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(pieces)<<error;
    ASSERT_EQ(pieces->roots().size(),2u);
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());
    for(const bool waterCapacity:{false,true}) {
    SCOPED_TRACE(waterCapacity?"water driver capacity":"body capacity");
    physics::GpuPhysicsBackend backend;physics::PhysicsInitContext config;
    config.device=gpu.getDevice();config.queue=gpu.getQueue();config.maxBodies=waterCapacity?32:16;config.maxActiveBodies=config.maxBodies;
    config.maxPairs=64;config.maxContacts=128;config.maxManifolds=128;config.gpu.commandCapacity=128;
    config.gpu.debugReadbackBodyCapacity=32;config.gpu.gravity={0,0,0};config.gpu.maximumCatchUpTicks=1;
    config.gpu.linearDamping=0;config.gpu.angularDamping=0;
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(-200,false);
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();physics::ShapeResourceError resourceError;
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};drain();
    const auto step=[&]{
        backend.stepCpu(1.0f/60.0f);const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);if(!encoder)return false;
        const auto result=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=result.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return submitted==physics::ShapeResourceError::None;
    };
    auto copied=intact->shape();const auto parentShape=resources.upload(std::move(copied),resourceError);ASSERT_TRUE(parentShape.valid());
    std::array<physics::ShapeHandle,2> shapes{};
    for(size_t i=0;i<2;++i){auto shape=pieces->roots()[i].shape;shapes[i]=resources.upload(std::move(shape),resourceError);ASSERT_TRUE(shapes[i].valid());}
    drain();std::vector<physics::BodyHandle> original;
    // Fifteen real parents/fillers leave one body or water slot for two children.
    for(int i=0;i<15;++i){physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute({100.*i,10,-1000});
        const auto body=backend.spawnAuthoredBody({.shape=parentShape,.motion=motion});ASSERT_TRUE(body);original.push_back(body.body);
        ASSERT_EQ(backend.configureAuthoredWaterBody(intact->primaryRoot().water(body.body)),physics::AuthoredBodyError::None);}
    ASSERT_TRUE(step());const auto before=backend.tickFrontier();
    std::array<physics::BodyHandle,2> canceled{};
    for(int attempt=0;attempt<3;++attempt){
        for(size_t i=0;i<2;++i){physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute({100.*double(20+i),10,-1000});
            const auto child=backend.spawnAuthoredBody({.shape=shapes[i],.motion=motion});
            if(!waterCapacity&&i==1){EXPECT_FALSE(child);continue;}
            ASSERT_TRUE(child);
            if(attempt){EXPECT_EQ(child.body.index,canceled[i].index);EXPECT_NE(child.body.generation,canceled[i].generation);}
            canceled[i]=child.body;
            EXPECT_EQ(backend.configureAuthoredWaterBody(pieces->roots()[i].water(child.body)),
                i==0?physics::AuthoredBodyError::None:physics::AuthoredBodyError::Capacity);
        }
        for(const auto body:canceled){if(body.valid()){ASSERT_TRUE(backend.destroyBody(body));}}
        EXPECT_EQ(backend.tickFrontier().scheduled,before.scheduled);EXPECT_EQ(backend.tickFrontier().completed,before.completed);
    }
    backend.requestDebugSnapshot({1,std::min(17u,config.maxBodies)});ASSERT_TRUE(step());std::optional<physics::DebugSnapshot> observed;
    for(int i=0;i<100&&!observed;++i){observed=backend.pollDebugSnapshot();drain();}ASSERT_TRUE(observed);
    size_t alive=0;
    for(const auto& body:observed->bodies)if(body.alive){++alive;EXPECT_NE(std::find(original.begin(),original.end(),body.handle),original.end());
        EXPECT_EQ(body.authoredShape,parentShape);EXPECT_LT(glm::length(body.linearVelocity),1e-6f);}
    EXPECT_EQ(alive,original.size());
    // Release one filler at a completed tick. Both children must now acquire
    // the same slots/shape references successfully; no failed attempt leaked.
    ASSERT_TRUE(backend.destroyBody(original.back()));original.pop_back();ASSERT_TRUE(step());
    for(size_t i=0;i<2;++i){physics::AuthoredRootMotion motion;motion.position=physics::worldPositionFromAbsolute({2000.+100.*double(i),10,-1000});
        const auto child=backend.spawnAuthoredBody({.shape=shapes[i],.motion=motion});ASSERT_TRUE(child);
        ASSERT_EQ(backend.configureAuthoredWaterBody(pieces->roots()[i].water(child.body)),physics::AuthoredBodyError::None);
        original.push_back(child.body);}
    ASSERT_TRUE(step());for(const auto body:original)ASSERT_TRUE(backend.destroyBody(body));ASSERT_TRUE(step());
    ASSERT_EQ(resources.retire(parentShape),physics::ShapeResourceError::None);
    for(const auto shape:shapes)ASSERT_EQ(resources.retire(shape),physics::ShapeResourceError::None);
    drain();resources.close();drain();EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed);backend.shutdown();
    }
}

TEST_F(CoveMovement, ActualCutReplacesOneMovingSkiffWithTwoCompleteFloatingGpuBodies) {
    using namespace construction;
    std::string error;const WorldNamespace world{{'g','p','u','-','c','u','t','-','c','o','v','e'}};
    auto seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(intact)<<error;
    const auto first=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& link:seed->build.connections)if(link.a.part==first||link.b.part==first)cuts.push_back(link.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto pieces=CoveBoatAssembly::compileFragments(split->afterBuild(),seed->catalog,seed->placements,*intact->primaryRoot().helm,error);ASSERT_TRUE(pieces)<<error;
    ASSERT_EQ(pieces->roots().size(),2u);ASSERT_EQ(pieces->primaryRootIndex(),1u);
    auto live=CoveRigidRoots::prepare(*pieces,error);ASSERT_TRUE(live)<<error;
    ASSERT_TRUE(live->matches(*pieces));EXPECT_FALSE(live->matches(*intact));
    EXPECT_EQ(live->build(),pieces->build().id);EXPECT_EQ(live->revision(),pieces->build().revision);
    EXPECT_EQ(live->primary().key,pieces->primaryMassRoot().key);EXPECT_EQ(live->joinedTick(),0u);
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());physics::GpuPhysicsBackend backend;physics::PhysicsInitContext config;
    config.device=gpu.getDevice();config.queue=gpu.getQueue();config.maxBodies=16;config.maxActiveBodies=16;
    config.maxPairs=64;config.maxContacts=128;config.maxManifolds=128;config.gpu.commandCapacity=128;
    config.gpu.debugReadbackBodyCapacity=4;config.gpu.gravity={0,-9.81f,0};config.gpu.maximumCatchUpTicks=1;
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(-200,false);
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();physics::ShapeResourceError resourceError;
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};drain();
    const auto step=[&]{
        backend.stepCpu(1.0f/60.0f);
        const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return submitted==physics::ShapeResourceError::None;
    };
    const auto observe=[&](uint32_t begin,uint32_t count)->std::optional<physics::DebugSnapshot>{
        backend.requestDebugSnapshot({begin,count});if(!step())return {};
        for(int attempt=0;attempt<100;++attempt){auto value=backend.pollDebugSnapshot();drain();if(value)return value;}return {};
    };
    auto parentShape=intact->shape();const auto parentHandle=resources.upload(std::move(parentShape),resourceError);ASSERT_TRUE(parentHandle.valid());drain();
    physics::AuthoredRootMotion parentMotion;const auto anchor=intact->primaryMassRoot().buildFromRoot.translation;
    parentMotion.position=physics::worldPositionFromAbsolute({anchor.x*.02,-200+intact->equilibriumRootHeight(),anchor.z*.02});
    parentMotion.orientation=glm::angleAxis(.12f,glm::normalize(glm::vec3(.1f,1,.2f)));
    parentMotion.originVelocity={.4f,-.1f,.2f};parentMotion.angularVelocity={.01f,.04f,-.02f};
    const auto parent=backend.spawnAuthoredBody({.shape=parentHandle,.motion=parentMotion});ASSERT_TRUE(parent);
    const auto before=observe(parent.body.index,1);ASSERT_TRUE(before);ASSERT_EQ(before->bodies.size(),1u);
    ASSERT_EQ(before->confirmedIncarnation,backend.tickFrontier().incarnation);ASSERT_EQ(before->tick,backend.tickFrontier().completed);
    physics::AuthoredFrameError frameError;const auto& old=before->bodies[0];ASSERT_TRUE(old.alive);
    const auto root=physics::AuthoredBodyFrame(intact->shape()).rootMotion({.centerPosition={old.sector,old.position},
        .orientation=old.orientation,.centerVelocity=old.linearVelocity,.angularVelocity=old.angularVelocity},frameError);ASSERT_TRUE(root);
    auto parentRoots=CoveRigidRoots::prepare(*intact,error);ASSERT_TRUE(parentRoots)<<error;
    auto& parentRoot=parentRoots->primary();parentRoot.shape=parentHandle;parentRoot.body=parent.body;
    parentRoot.observed=*root;parentRoot.observedTick=before->tick;parentRoot.admissionTick=before->tick;
    const auto unchanged=live->primary().spawn;
    EXPECT_FALSE(live->inheritFractureMotion(*pieces,*split,*intact,*parentRoots,before->tick+1,error));
    EXPECT_FALSE(live->inheritFractureMotion(*intact,*split,*intact,*parentRoots,before->tick,error));
    parentRoot.retired=true;
    EXPECT_FALSE(live->inheritFractureMotion(*pieces,*split,*intact,*parentRoots,before->tick,error));
    parentRoot.retired=false;
    EXPECT_EQ(live->primary().spawn.position.local,unchanged.position.local);
    EXPECT_EQ(live->primary().spawn.originVelocity,unchanged.originVelocity);
    ASSERT_TRUE(live->inheritFractureMotion(*pieces,*split,*intact,*parentRoots,before->tick,error))<<error;
    std::array<physics::ShapeHandle,2> shapes;std::array<physics::BodyHandle,2> bodies;
    for(size_t i=0;i<2;++i){
        auto shape=pieces->roots()[i].shape;shapes[i]=resources.upload(std::move(shape),resourceError);ASSERT_TRUE(shapes[i].valid());
        live->roots()[i].shape=shapes[i];
    }
    drain();
    for(size_t i=0;i<2;++i){
        const auto& spawn=live->roots()[i].spawn;
        const auto child=backend.spawnAuthoredBody({.shape=shapes[i],.motion=spawn});ASSERT_TRUE(child);bodies[i]=child.body;
        live->roots()[i].body=child.body;live->roots()[i].spawn=spawn;live->roots()[i].admissionTick=before->tick+1;
    }
    EXPECT_FALSE(live->inheritFractureMotion(*pieces,*split,*intact,*parentRoots,before->tick,error));
    // Every child is admitted before removing the parent, and all mutations
    // execute in one next tick. This test does not replace GameSession's future
    // multi-root transaction/rollback owner or claim an exposed cutter control.
    const std::array retiredParents{parent.body};
    using Status=physics::PhysicsMutationStatus;
    const physics::PhysicsMutationJoin join{before->confirmedIncarnation,before->tick};
    EXPECT_EQ(backend.prepareMutationBatch({.bodyDestroys=retiredParents}).status,Status::IncompatibleSchedulingMode);
    EXPECT_EQ(backend.prepareMutationBatch({.bodyDestroys=retiredParents,
        .joinedBoundary=physics::PhysicsMutationJoin{join.incarnation+1,join.completedTick}}).status,Status::JoinedBoundaryUnavailable);
    EXPECT_EQ(backend.prepareMutationBatch({.bodyDestroys=retiredParents,
        .joinedBoundary=physics::PhysicsMutationJoin{join.incarnation,join.completedTick+1}}).status,Status::JoinedBoundaryUnavailable);
    // Paused service and unpaused workshop preparation share the same actual
    // completed frontier. Neither a paused flag nor an expected tick is proof.
    ASSERT_TRUE(backend.setSchedulingPaused(true));
    const auto paused=backend.prepareMutationBatch({.bodyDestroys=retiredParents,.joinedBoundary=join});ASSERT_TRUE(paused);
    ASSERT_TRUE(backend.discardPrepared(paused));ASSERT_TRUE(backend.setSchedulingPaused(false));
    const auto retirement=backend.prepareMutationBatch({.bodyDestroys=retiredParents,.joinedBoundary=join});ASSERT_TRUE(retirement);
    backend.stepCpu(1.0f);EXPECT_EQ(backend.tickFrontier().scheduled,join.completedTick);
    ASSERT_EQ(retirement.targetTick,before->tick+1);
    const auto committedRetirement=backend.commitPrepared(retirement);ASSERT_TRUE(committedRetirement);
    ASSERT_EQ(committedRetirement.destroyedBodyCount,1u);
    const auto end=std::max({parent.body.index,bodies[0].index,bodies[1].index});
    const auto after=observe(1,end);ASSERT_TRUE(after);ASSERT_EQ(after->tick,before->tick+1);
    EXPECT_EQ(backend.prepareMutationBatch({.bodyDestroys=retiredParents,.joinedBoundary=join}).status,Status::JoinedBoundaryUnavailable);
    glm::dvec3 momentum(0);size_t alive=0;
    for(const auto& state:after->bodies){
        if(state.handle.index==parent.body.index){EXPECT_FALSE(state.alive);continue;}
        if(!state.alive)continue;
        const auto found=std::find(bodies.begin(),bodies.end(),state.handle);ASSERT_NE(found,bodies.end());
        const auto index=size_t(found-bodies.begin());EXPECT_EQ(state.authoredShape,shapes[index]);++alive;
        momentum+=glm::dvec3(state.linearVelocity)*pieces->assembly().mass().roots()[index].mass.dryMassKg;
        const auto observed=physics::AuthoredBodyFrame(pieces->roots()[index].shape).rootMotion({.centerPosition={state.sector,state.position},
            .orientation=state.orientation,.centerVelocity=state.linearVelocity,.angularVelocity=state.angularVelocity},frameError);ASSERT_TRUE(observed);
        live->roots()[index].observed=*observed;live->roots()[index].observedTick=after->tick;
        if(alive==1){EXPECT_EQ(live->joinedTick(),0u);}
    }
    EXPECT_EQ(alive,2u);EXPECT_EQ(live->joinedTick(),after->tick);EXPECT_EQ(live->primary().body,bodies[1]);
    for(const auto& member:pieces->assembly().mass().parts()) {
        const auto index=live->indexForPart(*pieces,member.part);ASSERT_TRUE(index);
        EXPECT_EQ(live->roots()[*index].body,bodies[member.root]);
    }
    uint32_t rangeFirst=UINT32_MAX,rangeLast=0;live->includeBodyRange(rangeFirst,rangeLast);
    EXPECT_EQ(rangeFirst,std::min(bodies[0].index,bodies[1].index));EXPECT_EQ(rangeLast,std::max(bodies[0].index,bodies[1].index));
    const auto mass=intact->primaryMassRoot().mass.dryMassKg;
    auto expectedVelocity=glm::dvec3(old.linearVelocity);
    const double substep=double(config.gpu.fixedTickSeconds)/config.gpu.substeps;
    for(uint32_t i=0;i<config.gpu.substeps;++i)
        expectedVelocity=(expectedVelocity+glm::dvec3(config.gpu.gravity)*substep)/(1+double(config.gpu.linearDamping)*substep);
    const auto expectedMomentum=expectedVelocity*mass;
    EXPECT_LT(glm::length(momentum-expectedMomentum),.05);
    ASSERT_EQ(resources.retire(parentHandle),physics::ShapeResourceError::None);drain();
    backend.setWaterPlane(-200,true);
    for(size_t i=0;i<2;++i)ASSERT_EQ(backend.configureAuthoredWaterBody(pieces->roots()[i].water(bodies[i])),physics::AuthoredBodyError::None);
    // A lighter detached hull rises from the loaded skiff's draft. Observe the
    // physical transient, then require bounded flotation after twenty seconds.
    for(int tick=0;tick<1200;++tick){
        ASSERT_TRUE(step());
        if(tick==359||tick==719){
            const auto transient=observe(1,end);ASSERT_TRUE(transient);
            for(const auto& state:transient->bodies)if(state.alive&&state.handle==bodies[0])
                std::cout<<"Detached pontoon tick "<<transient->tick<<" y="<<state.position.y<<" vy="<<state.linearVelocity.y<<'\n';
        }
    }
    const auto afloat=observe(1,end);ASSERT_TRUE(afloat);alive=0;
    for(const auto& state:afloat->bodies)if(state.alive){
        const auto found=std::find(bodies.begin(),bodies.end(),state.handle);ASSERT_NE(found,bodies.end());const auto index=size_t(found-bodies.begin());++alive;
        const auto observed=physics::AuthoredBodyFrame(pieces->roots()[index].shape).rootMotion({.centerPosition={state.sector,state.position},
            .orientation=state.orientation,.centerVelocity=state.linearVelocity,.angularVelocity=state.angularVelocity},frameError);ASSERT_TRUE(observed);
        EXPECT_TRUE(std::isfinite(glm::length(observed->originVelocity)));EXPECT_TRUE(std::isfinite(glm::length(observed->angularVelocity)));
        if(index==0){EXPECT_GT(physics::worldPositionToAbsolute(observed->position).y,-201);EXPECT_LT(std::abs(observed->originVelocity.y),.25f);}
    }
    EXPECT_EQ(alive,2u);EXPECT_EQ(pieces->build().parts,seed->build.parts);
    EXPECT_EQ(backend.stats().authoredAdmissionFailures,0u);EXPECT_FALSE(backend.stats().contactCapacityOverflow);
    for(auto body:bodies){ASSERT_TRUE(backend.destroyBody(body));}
    ASSERT_TRUE(step());
    for(auto shape:shapes)ASSERT_EQ(resources.retire(shape),physics::ShapeResourceError::None);
    drain();resources.close();drain();EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed);backend.shutdown();
}

TEST_F(CoveMovement, ActualSkiffHitsFixedDockButPassesThroughOpenWater) {
    std::string error;const auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    const auto scenery=CoveSceneryCollision::compile(*scene,error);ASSERT_TRUE(scenery)<<error;
    for(const auto& source:scenery->sources()) EXPECT_GE(source.placement,11u);
    for(const auto& face:scenery->shape().faces()) {
        EXPECT_GT(face.source,0u);EXPECT_LE(face.source,scenery->sources().size());
    }
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());
    physics::GpuPhysicsBackend backend;physics::PhysicsInitContext config;
    config.device=gpu.getDevice();config.queue=gpu.getQueue();
    config.maxBodies=32;config.maxActiveBodies=32;config.maxPairs=64;config.maxContacts=64;config.maxManifolds=64;
    config.gpu.commandCapacity=128;config.gpu.debugReadbackBodyCapacity=4;config.gpu.gravity={0,0,0};
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};
    drain();ASSERT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Ready);
    physics::ShapeResourceError resourceError;auto fixedShape=scenery->shape(),hull=boat->shape();
    const auto fixedHandle=resources.upload(std::move(fixedShape),resourceError);ASSERT_TRUE(fixedHandle.valid());drain();
    const auto hullHandle=resources.upload(std::move(hull),resourceError);ASSERT_TRUE(hullHandle.valid());drain();
    physics::AuthoredBodySpawnDesc desc;desc.shape=fixedHandle;desc.motionType=physics::AuthoredBodyMotionType::Static;
    desc.motion.position=physics::worldPositionFromAbsolute(scenery->origin());
    const auto fixed=backend.spawnAuthoredBody(desc);ASSERT_TRUE(fixed);
    desc.shape=hullHandle;desc.motionType=physics::AuthoredBodyMotionType::Dynamic;
    desc.motion.position=physics::worldPositionFromAbsolute({-.5,boat->equilibriumRootHeight(),-54});
    desc.motion.originVelocity={2,0,0};
    const auto impact=backend.spawnAuthoredBody(desc);ASSERT_TRUE(impact);
    desc.motion.position=physics::worldPositionFromAbsolute({-.5,boat->equilibriumRootHeight(),-70});
    const auto clear=backend.spawnAuthoredBody(desc);ASSERT_TRUE(clear);
    const auto step=[&]{
        if(!backend.scheduleFixedTicks(1))return false;
        const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);
        if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto result=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return result==physics::ShapeResourceError::None;
    };
    for(int i=0;i<180;++i)ASSERT_TRUE(step());
    backend.requestDebugSnapshot({fixed.body.index,3});ASSERT_TRUE(step());
    std::optional<physics::DebugSnapshot> snapshot;
    for(int i=0;i<100&&!snapshot;++i){snapshot=backend.pollDebugSnapshot();drain();}
    ASSERT_TRUE(snapshot);ASSERT_EQ(snapshot->bodies.size(),3u);
    physics::AuthoredFrameError frameError;
    const auto root=[&](size_t i,const physics::AuthoredShape& shape){const auto& state=snapshot->bodies[i];
        return physics::AuthoredBodyFrame(shape).rootMotion({.centerPosition={state.sector,state.position},
            .orientation=state.orientation,.centerVelocity=state.linearVelocity,.angularVelocity=state.angularVelocity},frameError);};
    const auto dock=root(0,scenery->shape()),struck=root(1,boat->shape()),free=root(2,boat->shape());
    ASSERT_TRUE(dock);ASSERT_TRUE(struck);ASSERT_TRUE(free);
    EXPECT_LT(glm::length(physics::worldPositionToAbsolute(dock->position)-scenery->origin()),.0001);
    EXPECT_EQ(dock->originVelocity,glm::vec3(0));EXPECT_EQ(dock->angularVelocity,glm::vec3(0));
    EXPECT_LT(physics::worldPositionToAbsolute(struck->position).x,2.0);
    EXPECT_GT(physics::worldPositionToAbsolute(free->position).x,4.0);
    EXPECT_GT(backend.stats().highContacts,0u);
    EXPECT_FALSE(backend.stats().contactCapacityOverflow);EXPECT_EQ(backend.stats().authoredAdmissionFailures,0u);
    std::cout<<"Actual dock impact: blocked hull x "<<struck->position.local.x<<", clear hull x "<<free->position.local.x
        <<", peak contacts "<<backend.stats().highContacts<<", scenery proxies "<<scenery->sources().size()<<'\n';
    for(auto body:{fixed.body,impact.body,clear.body})ASSERT_TRUE(backend.destroyBody(body));
    ASSERT_TRUE(step());
    ASSERT_EQ(resources.retire(fixedHandle),physics::ShapeResourceError::None);
    ASSERT_EQ(resources.retire(hullHandle),physics::ShapeResourceError::None);drain();resources.close();drain();
    EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed);backend.shutdown();
}

TEST_F(CoveMovement, ActualCargoAndSkiffGroundOnBrickSeabed) {
    for (const bool skiff : {false, true}) {
    SCOPED_TRACE(skiff ? "actual skiff grounding" : "actual generator settling");
    std::string error;const auto cargo=skiff ? CoveBoatAssembly::compile(*scene,error)
        : CoveBoatAssembly::compileCargo(*scene,24,error);ASSERT_TRUE(cargo)<<error;
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());physics::GpuPhysicsBackend backend;
    physics::PhysicsInitContext config;config.device=gpu.getDevice();config.queue=gpu.getQueue();
    config.maxBodies=32;config.maxActiveBodies=32;config.maxPairs=64;config.maxContacts=64;config.maxManifolds=64;
    config.gpu.commandCapacity=128;config.gpu.debugReadbackBodyCapacity=4;
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(skiff ? -205.f : -200.f,true);
    const auto raw=static_cast<uint16_t>(std::lround((-204.0/600+1)*32767.5));
    const std::vector<uint16_t> samples(257*257,raw);
    ASSERT_TRUE(backend.setLegoTerrain(samples,257,257,600,1));
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};drain();
    physics::ShapeResourceError resourceError;auto shape=cargo->shape();
    const auto handle=resources.upload(std::move(shape),resourceError);ASSERT_TRUE(handle.valid());drain();
    const auto authored=cargo->assembly().mass().roots()[0].buildFromRoot.translation;
    glm::dvec3 spawn=glm::dvec3(-19,-200,-37)+glm::dvec3(authored.x,authored.y,authored.z)*.02;
    const double plate=double(terrain::lego::top(raw,600,1));
    double lowest=std::numeric_limits<double>::infinity();
    for(const auto& cell:cargo->shape().cells()) lowest=std::min(lowest,double(cell.minimum[1])*.02);
    // Lower test water exposes the actual hull to grounding instead of buoyancy.
    // Cargo keeps its shipping authored placement and water plane.
    if(skiff) spawn.y=plate+.18-lowest+.02;
    ASSERT_GE(spawn.y+lowest,plate+.18);
    physics::AuthoredBodySpawnDesc desc;desc.shape=handle;desc.motion.position=physics::worldPositionFromAbsolute(spawn);
    const auto body=backend.spawnAuthoredBody(desc);ASSERT_TRUE(body);
    std::vector<physics::AuthoredWaterCell> cells;
    for(const auto& cell:cargo->assembly().buoyancy().roots()[0].coverage.cells()) {
        const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
        cells.push_back({glm::vec3(a.x,a.y,a.z)*.02f,glm::vec3(b.x,b.y,b.z)*.02f});
    }
    ASSERT_EQ(backend.configureAuthoredWaterBody({.body=body.body,.cells=cells}),physics::AuthoredBodyError::None);
    const auto step=[&]{
        if(!backend.scheduleFixedTicks(1))return false;
        const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);
        if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return submitted==physics::ShapeResourceError::None;
    };
    for(int i=0;i<360;i++)ASSERT_TRUE(step());
    backend.requestDebugSnapshot({body.body.index,1});ASSERT_TRUE(step());
    std::optional<physics::DebugSnapshot> snapshot;
    for(int i=0;i<100 && !snapshot;i++){snapshot=backend.pollDebugSnapshot();drain();}
    ASSERT_TRUE(snapshot);ASSERT_EQ(snapshot->bodies.size(),1u);
    const auto& state=snapshot->bodies[0];ASSERT_TRUE(state.alive);
    physics::AuthoredFrameError frameError;
    const auto root=physics::AuthoredBodyFrame(cargo->shape()).rootMotion({.centerPosition={state.sector,state.position},
        .orientation=state.orientation,.centerVelocity=state.linearVelocity,.angularVelocity=state.angularVelocity},frameError);
    ASSERT_TRUE(root);const auto position=physics::worldPositionToAbsolute(root->position);
    if(!skiff) {
        EXPECT_LT(std::hypot(position.x-spawn.x,position.z-spawn.z),.25);
        EXPECT_NEAR(position.y,plate+.18+.64,.06);
    }
    double bottom=std::numeric_limits<double>::infinity();
    for(const auto& cell:cargo->shape().cells()) for(unsigned corner=0;corner<8;++corner) {
        const glm::dvec3 p{double((corner&1u)?cell.maximum[0]:cell.minimum[0])*.02,
            double((corner&2u)?cell.maximum[1]:cell.minimum[1])*.02,
            double((corner&4u)?cell.maximum[2]:cell.minimum[2])*.02};
        bottom=std::min(bottom,position.y+(glm::dquat(root->orientation)*p).y);
    }
    EXPECT_GE(bottom,plate-.04);EXPECT_LE(bottom,plate+.18+.06);
    EXPECT_LT(glm::length(root->originVelocity),.15f);
    EXPECT_LT(glm::length(root->angularVelocity),.2f);
    EXPECT_FALSE(backend.stats().contactCapacityOverflow);
    EXPECT_EQ(backend.stats().authoredAdmissionFailures,0u);
    std::cout<<(skiff?"Grounded skiff":"Settled generator")<<": bottom "<<bottom
        <<", plate "<<plate<<", speed "<<glm::length(root->originVelocity)<<'\n';
    ASSERT_TRUE(backend.destroyBody(body.body));ASSERT_TRUE(step());
    ASSERT_EQ(resources.retire(handle),physics::ShapeResourceError::None);drain();resources.close();drain();backend.shutdown();
    }
}

TEST_F(CoveMovement, HarborLiftActuallyRaisesHoldsAndLowersTheAuthoredSkiff) {
    std::string error;const auto boat=CoveBoatAssembly::compile(*scene,error);ASSERT_TRUE(boat)<<error;
    const auto lift=CoveHarborLift::prepare(*boat,*scene->registry.navigation,error);ASSERT_TRUE(lift)<<error;
    gpu::Context gpu;ASSERT_TRUE(gpu.initHeadless());physics::GpuPhysicsBackend backend;
    physics::PhysicsInitContext config;config.device=gpu.getDevice();config.queue=gpu.getQueue();
    config.maxBodies=32;config.maxActiveBodies=32;config.maxPairs=64;config.maxContacts=128;config.maxManifolds=128;
    config.gpu.commandCapacity=128;config.gpu.debugReadbackBodyCapacity=4;config.gpu.debugReadbackAttachmentCapacity=4;
    config.gpu.gravity={0,-9.81f,0};config.gpu.maximumCatchUpTicks=1;
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(-200,true);
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};drain();
    physics::ShapeResourceError resourceError;
    auto boatShape=boat->shape();const auto shape=resources.upload(std::move(boatShape),resourceError);ASSERT_TRUE(shape.valid());drain();
    auto gantryShape=lift->shape();const auto fixedShape=resources.upload(std::move(gantryShape),resourceError);ASSERT_TRUE(fixedShape.valid());drain();
    const auto anchor=boat->assembly().mass().roots()[0].buildFromRoot.translation;
    physics::AuthoredRootMotion initial;initial.position=physics::worldPositionFromAbsolute(
        {anchor.x*.02,-200+boat->equilibriumRootHeight(),anchor.z*.02});
    physics::AuthoredRootMotion fixed;fixed.position=physics::worldPositionFromAbsolute(glm::dvec3(0,-200,0)+lift->center());
    const auto moving=backend.spawnAuthoredBody({.shape=shape,.motion=initial});ASSERT_TRUE(moving);
    const auto gantry=backend.spawnAuthoredBody({.shape=fixedShape,.motion=fixed,.motionType=physics::AuthoredBodyMotionType::Static});ASSERT_TRUE(gantry);
    std::vector<physics::AuthoredWaterCell> cells;
    for(const auto& cell:boat->assembly().buoyancy().roots()[0].coverage.cells()){
        const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
        cells.push_back({glm::vec3(a.x,a.y,a.z)*.02f,glm::vec3(b.x,b.y,b.z)*.02f});
    }
    ASSERT_EQ(backend.configureAuthoredWaterBody({.body=moving.body,.cells=cells}),physics::AuthoredBodyError::None);
    const auto step=[&]{
        if(!backend.scheduleFixedTicks(1))return false;
        const auto ticket=backend.prepareGpuSubmission(resourceError);if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);
        if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);WGPUCommandBufferDescriptor cd{};
        const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});wgpuCommandBufferRelease(command);
        drain();backend.stepCpu(0);drain();return submitted==physics::ShapeResourceError::None;
    };
    std::array<physics::AttachmentHandle,4> ropes{};
    const auto observe=[&]()->std::optional<physics::DebugSnapshot>{
        backend.requestDebugSnapshot({moving.body.index,1,ropes[0].index,ropes[0].valid()?4u:0u});
        if(!step())return {};
        for(int i=0;i<100;++i){auto snapshot=backend.pollDebugSnapshot();if(snapshot)return snapshot;drain();}
        return {};
    };
    const auto rootOf=[&](const physics::DebugSnapshot& snapshot){
        const auto& body=snapshot.bodies.at(0);physics::AuthoredFrameError issue;
        return physics::AuthoredBodyFrame(boat->shape()).rootMotion({.centerPosition={body.sector,body.position},
            .orientation=body.orientation,.centerVelocity=body.linearVelocity,.angularVelocity=body.angularVelocity},issue);
    };
    for(int i=0;i<120;++i)ASSERT_TRUE(step());
    auto before=observe();ASSERT_TRUE(before);auto rest=rootOf(*before);ASSERT_TRUE(rest);
    const auto descs=lift->attach(*rest,fixed,moving.body,gantry.body,error);ASSERT_TRUE(descs)<<error;
    for(const auto& line:*descs)EXPECT_FLOAT_EQ(line.springCompliance,kCoveHarborLiftCompliance);
    for(size_t i=0;i<4;++i){ropes[i]=backend.createDistanceAttachment((*descs)[i]);ASSERT_TRUE(ropes[i].valid());
        ASSERT_TRUE(backend.setAttachmentMotorSpeed(ropes[i],kCoveHarborLiftReelSpeed));}
    ASSERT_EQ(ropes[3].index-ropes[0].index,3u);
    for(int i=0;i<600;++i)ASSERT_TRUE(step());
    for(auto rope:ropes)ASSERT_TRUE(backend.setAttachmentMotorSpeed(rope,0));
    for(int i=0;i<120;++i)ASSERT_TRUE(step());
    auto raised=observe();ASSERT_TRUE(raised);const auto lifted=rootOf(*raised);ASSERT_TRUE(lifted);
    const auto raisedPosition=physics::worldPositionToAbsolute(lifted->position);
    const auto restPosition=physics::worldPositionToAbsolute(rest->position);
    EXPECT_GT(raisedPosition.y-restPosition.y,3.0);
    EXPECT_GT((lifted->orientation*glm::vec3(0,1,0)).y,.97f);
    EXPECT_LT(glm::length(lifted->originVelocity),.15f);
    ASSERT_EQ(raised->attachments.size(),4u);
    for(const auto& rope:raised->attachments){EXPECT_TRUE(rope.alive);EXPECT_FALSE(rope.broken);
        std::cout<<"Lift line "<<rope.handle.index<<": force="<<rope.requiredForce<<", break tick="<<rope.breakTick<<", length="<<rope.distance.targetLength<<" min="<<rope.distance.minimumLength<<"\n";
        EXPECT_EQ(rope.distance.motorSpeed,0);EXPECT_NEAR(rope.distance.targetLength,lift->minimumLength(),.01f);}
    double bottom=1e9;
    for(const auto& cell:boat->shape().cells())for(unsigned corner=0;corner<8;++corner){
        const glm::vec3 point{float((corner&1u)?cell.maximum[0]:cell.minimum[0])*.02f,
            float((corner&2u)?cell.maximum[1]:cell.minimum[1])*.02f,float((corner&4u)?cell.maximum[2]:cell.minimum[2])*.02f};
        bottom=std::min(bottom,raisedPosition.y+double((lifted->orientation*point).y));
    }
    EXPECT_GT(bottom,-198.0); // Entire actual hull clears the water by two metres.
    for(auto rope:ropes)ASSERT_TRUE(backend.setAttachmentMotorSpeed(rope,-kCoveHarborLiftLowerSpeed));
    for(int i=0;i<1320;++i){
        ASSERT_TRUE(step());
        if(i%240==239){
            const auto phase=observe();ASSERT_TRUE(phase);const auto pose=rootOf(*phase);ASSERT_TRUE(pose);
            for(const auto& rope:phase->attachments){EXPECT_TRUE(rope.alive);EXPECT_FALSE(rope.broken);}
            const auto p=physics::worldPositionToAbsolute(pose->position);
            std::cout<<"Lower tick "<<phase->tick<<" position="<<p.x<<","<<p.y<<","<<p.z
                <<" up="<<(pose->orientation*glm::vec3(0,1,0)).y<<" speed="<<glm::length(pose->originVelocity)<<"\n";
            for(const auto& rope:phase->attachments)std::cout<<"  rope "<<rope.handle.index<<" length="<<rope.distance.targetLength
                <<" force="<<rope.requiredForce<<" break="<<rope.breakTick<<"\n";
        }
    }
    for(auto rope:ropes){ASSERT_TRUE(backend.destroyAttachment(rope));}
    ropes={};
    for(int i=0;i<120;++i)ASSERT_TRUE(step());
    auto lowered=observe();ASSERT_TRUE(lowered);const auto afloat=rootOf(*lowered);ASSERT_TRUE(afloat);
    EXPECT_NEAR(physics::worldPositionToAbsolute(afloat->position).y,restPosition.y,.1);
    EXPECT_GT((afloat->orientation*glm::vec3(0,1,0)).y,.97f);
    EXPECT_LT(glm::length(afloat->originVelocity),.2f);
    EXPECT_EQ(boat->assembly().mass().roots()[0].mass.dryMassKg,1035);
    EXPECT_FALSE(backend.stats().contactCapacityOverflow);EXPECT_EQ(backend.stats().authoredAdmissionFailures,0u);
    std::cout<<"Harbor lift actual 1035 kg skiff: rest="<<restPosition.y<<", raised="<<raisedPosition.y
        <<", hull bottom="<<bottom<<", lowered="<<physics::worldPositionToAbsolute(afloat->position).y
        <<", upright="<<(lifted->orientation*glm::vec3(0,1,0)).y<<'\n';
    ASSERT_TRUE(backend.destroyBody(moving.body));ASSERT_TRUE(backend.destroyBody(gantry.body));ASSERT_TRUE(step());
    ASSERT_EQ(resources.retire(shape),physics::ShapeResourceError::None);
    ASSERT_EQ(resources.retire(fixedShape),physics::ShapeResourceError::None);drain();resources.close();drain();
    EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed);backend.shutdown();
}

TEST_F(CoveMovement, ActualSkiffSettlesUprightAtItsDryDraft) {
    std::string error; const auto boat=CoveBoatAssembly::compile(*scene,error); ASSERT_TRUE(boat) << error;
    gpu::Context gpu; ASSERT_TRUE(gpu.initHeadless());
    physics::GpuPhysicsBackend backend;
    physics::PhysicsInitContext config; config.device=gpu.getDevice(); config.queue=gpu.getQueue();
    config.maxBodies=32; config.maxActiveBodies=32; config.maxPairs=64; config.maxContacts=64; config.maxManifolds=64;
    config.gpu.commandCapacity=128; config.gpu.debugReadbackBodyCapacity=4;
    config.gpu.gravity={0,-9.81f,0};
    // Use the shipping cove's negative world datum, not a special origin case.
    ASSERT_TRUE(backend.initialize(config)); backend.setWaterPlane(-200,true);
    ASSERT_EQ(backend.enableAuthoredShapeResources({}),physics::ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();
    const auto drain=[&]{(void)wgpuDevicePoll(gpu.getDevice(),true,nullptr);resources.poll();};
    drain(); ASSERT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Ready);
    auto shape=boat->shape(); physics::ShapeResourceError resourceError;
    const auto handle=resources.upload(std::move(shape),resourceError); ASSERT_TRUE(handle.valid()); drain();
    physics::AuthoredBodySpawnDesc desc; desc.shape=handle;
    desc.motion.position=physics::worldPositionFromAbsolute({-19.5,-200+boat->equilibriumRootHeight(),-91});
    const auto body=backend.spawnAuthoredBody(desc); ASSERT_TRUE(body);
    std::vector<physics::AuthoredWaterCell> cells;
    for(const auto& cell:boat->assembly().buoyancy().roots()[0].coverage.cells()) {
        const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
        cells.push_back({glm::vec3(a.x,a.y,a.z)*.02f,glm::vec3(b.x,b.y,b.z)*.02f});
    }
    ASSERT_EQ(backend.configureAuthoredWaterBody({.body=body.body,.cells=cells}),physics::AuthoredBodyError::None);
    const auto step=[&]{
        if(!backend.scheduleFixedTicks(1))return false;
        const auto ticket=backend.prepareGpuSubmission(resourceError); if(!ticket.valid())return false;
        WGPUCommandEncoderDescriptor ed{}; const auto encoder=wgpuDeviceCreateCommandEncoder(gpu.getDevice(),&ed);
        if(!encoder)return false;
        const auto report=backend.encodeGpuStepChecked(encoder);
        WGPUCommandBufferDescriptor cd{};const auto command=report.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
        wgpuCommandEncoderRelease(encoder);
        if(!command){(void)backend.discardGpuSubmission(ticket);return false;}
        const auto submitted=backend.submitGpuSubmission(ticket,std::span{&command,1});
        wgpuCommandBufferRelease(command);drain();backend.stepCpu(0);drain();
        return submitted==physics::ShapeResourceError::None;
    };
    for(int i=0;i<360;++i)ASSERT_TRUE(step());
    backend.requestDebugSnapshot({body.body.index,1}); ASSERT_TRUE(step());
    std::optional<physics::DebugSnapshot> snapshot;
    for(int poll=0;poll<100 && !snapshot;++poll) {
        snapshot=backend.pollDebugSnapshot();drain();
    }
    ASSERT_TRUE(snapshot); ASSERT_EQ(snapshot->bodies.size(),1u);
    const auto& state=snapshot->bodies[0]; ASSERT_TRUE(state.alive);
    physics::AuthoredFrameError frameError;
    const auto root=physics::AuthoredBodyFrame(boat->shape()).rootMotion({.centerPosition={state.sector,state.position},
        .orientation=state.orientation,.centerVelocity=state.linearVelocity,.angularVelocity=state.angularVelocity},frameError);
    ASSERT_TRUE(root);
    const auto up=root->orientation*glm::vec3(0,1,0);
    EXPECT_GT(up.y,.9f); EXPECT_LT(glm::length(root->angularVelocity),.2f);
    EXPECT_LT(std::abs(root->originVelocity.y),.2f);
    std::cout << "Actual skiff: root height " << root->position.local.y << ", up " << up.y
        << ", vertical speed " << root->originVelocity.y << ", angular speed " << glm::length(root->angularVelocity) << '\n';
    ASSERT_TRUE(backend.destroyBody(body.body)); ASSERT_TRUE(step());
    ASSERT_EQ(resources.retire(handle),physics::ShapeResourceError::None);drain();resources.close();drain();
    EXPECT_EQ(resources.stats().phase,physics::ShapeResourcePhase::Closed); backend.shutdown();
}
#endif

TEST_F(CoveMovement, DisconnectedBoatIsRejectedBeforePhysicsAdmission) {
    auto disconnected = *scene;
    disconnected.registry.connections.clear();
    std::string error;
    EXPECT_FALSE(CoveBoatAssembly::compile(disconnected, error));
    EXPECT_NE(error.find("one welded body"), std::string::npos) << error;
}

TEST_F(CoveMovement, WalksAcrossAuthoredDeckSeamsAndBoardsUsesHelmAndReturns) {
    EXPECT_GT(player.collisionBoxes(), 24u);
    EXPECT_FALSE(player.requestInteraction()); // Spawn is too far from boat.
    approach();
    ASSERT_EQ(player.interaction(), CovePlayer::Interaction::Board);
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
    interact();
    ASSERT_TRUE(player.onBoat());
    EXPECT_NEAR(player.feet().y, .965, 1e-9);
    move({-1, 0}, 35);
    move({0, 0}, 20);
    ASSERT_EQ(player.interaction(), CovePlayer::Interaction::UseHelm);
    interact();
    ASSERT_EQ(player.mode(), CovePlayer::Mode::Helm);
    const auto atHelm = player.feet();
    move({1, 1}, 120);
    EXPECT_EQ(player.feet(), atHelm); // Moored helm does not pretend to sail.
    interact();
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
    move({1, 0}, 35);
    move({0, 0}, 20);
    ASSERT_EQ(player.interaction(), CovePlayer::Interaction::ReturnToDock);
    interact();
    EXPECT_FALSE(player.onBoat());
    EXPECT_EQ(player.interactions(), 4u);
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
}

TEST_F(CoveMovement, ObservedBoatPoseCarriesHelmAndResetReturnsToFixedDock) {
    approach(); interact(); move({-1,0},35); move({0,0},20); interact();
    ASSERT_EQ(player.mode(),CovePlayer::Mode::Helm);
    const auto before=player.feet();
    const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(30,.4,-20))
        *glm::rotate(glm::dmat4(1),.5,glm::dvec3(0,1,0));
    player.setBoatTransform(transform);
    const auto expected=glm::dvec3(transform*glm::dvec4(before,1));
    EXPECT_LT(glm::length(player.feet()-expected),1e-9);
    move({1,0},30); EXPECT_LT(glm::length(player.feet()-expected),1e-9);
    interact(); ASSERT_EQ(player.mode(),CovePlayer::Mode::Walking);
    move({1,0},2); EXPECT_GT(player.feet().x,expected.x);
    player.reset(); EXPECT_FALSE(player.onBoat());
    EXPECT_LT(glm::length(player.feet()-(scene->registry.navigation->spawn+glm::dvec3(0,.005,0))),1e-9);
    approach(); EXPECT_EQ(player.interaction(),CovePlayer::Interaction::None); // Boat is no longer alongside.
}

TEST_F(CoveMovement, ResumeKeepsBoatLocalHelmPoseOnRestoredMovingCraft) {
    approach();interact();move({-1,0},35);move({0,0},20);interact();
    ASSERT_EQ(player.mode(),CovePlayer::Mode::Helm);
    const auto transform=glm::translate(glm::dmat4(1),glm::dvec3(30,.4,-20))
        *glm::rotate(glm::dmat4(1),.5,glm::dvec3(0,1,0));
    player.setBoatTransform(transform);
    const auto saved=player.state();const auto feet=player.feet();
    CovePlayer restored;std::string error;
    ASSERT_TRUE(restored.initialize(*scene,[](double,double){return -5.;},error));
    EXPECT_FALSE(restored.restore(saved)); // The boat pose is required first.
    restored.setBoatTransform(transform);ASSERT_TRUE(restored.restore(saved));
    EXPECT_EQ(restored.feet(),feet);EXPECT_EQ(restored.state().feet,saved.feet);
    EXPECT_TRUE(restored.onBoat());EXPECT_EQ(restored.mode(),CovePlayer::Mode::Helm);
    EXPECT_EQ(restored.tick(),saved.tick);EXPECT_EQ(restored.interactions(),saved.interactions);
    ASSERT_TRUE(restored.requestInteraction());restored.advance(CovePlayer::fixedStep,{});
    EXPECT_EQ(restored.mode(),CovePlayer::Mode::Walking);EXPECT_TRUE(restored.onBoat());
    const auto walking=restored.state();ASSERT_TRUE(restored.restore(walking));
    restored.advance(CovePlayer::fixedStep,{{1,0},false});EXPECT_NE(restored.feet(),feet);
}

TEST_F(CoveMovement, ResumePreservesJumpVelocityAndSwimmingWithoutPendingControls) {
    player.advance(CovePlayer::fixedStep,{{0,0},true});move({0,0},8);
    const auto airborne=player.state();ASSERT_EQ(airborne.mode,CovePlayer::Mode::Airborne);
    CovePlayer restored;std::string error;
    ASSERT_TRUE(restored.initialize(*scene,[](double,double){return -5.;},error));
    ASSERT_TRUE(restored.restore(airborne));
    for(int i=0;i<60;++i) {
        player.advance(CovePlayer::fixedStep,{});restored.advance(CovePlayer::fixedStep,{});
        EXPECT_EQ(restored.feet(),player.feet());EXPECT_DOUBLE_EQ(restored.state().verticalSpeed,player.state().verticalSpeed);
    }
    move({1,0},90);move({0,0},90);ASSERT_EQ(player.mode(),CovePlayer::Mode::Swimming);
    ASSERT_TRUE(restored.restore(player.state()));EXPECT_EQ(restored.feet(),player.feet());
    restored.advance(CovePlayer::fixedStep,{{1,0},false});EXPECT_EQ(restored.mode(),CovePlayer::Mode::Swimming);
    player.reset();approach();const auto dock=player.state();ASSERT_TRUE(player.requestInteraction());
    player.advance(CovePlayer::fixedStep/4,{{0,0},true});
    ASSERT_TRUE(player.restore(dock));
    player.advance(CovePlayer::fixedStep*.75,{});EXPECT_EQ(player.tick(),dock.tick);
    player.advance(CovePlayer::fixedStep*.25,{});EXPECT_EQ(player.tick(),dock.tick+1);
    EXPECT_FALSE(player.onBoat());EXPECT_EQ(player.mode(),CovePlayer::Mode::Walking);
    EXPECT_EQ(player.interactions(),dock.interactions);
}

TEST_F(CoveMovement, InvalidResumePreservesPlayerAndQueuedInteraction) {
    approach();const auto before=player.state();ASSERT_TRUE(player.requestInteraction());
    for(int sample=0;sample<9;++sample) {
        auto invalid=before;
        switch(sample) {
        case 0:invalid.feet.x=std::numeric_limits<double>::quiet_NaN();break;
        case 1:invalid.feet.x=1'000'001;break;
        case 2:invalid.verticalSpeed=1;break;
        case 3:invalid.mode=static_cast<CovePlayer::Mode>(255);break;
        case 4:invalid.mode=CovePlayer::Mode::Helm;break;
        case 5:invalid.mode=CovePlayer::Mode::Swimming;break;
        case 6:invalid.feet={6,1.28,-53.5};break; // Inside generator.
        case 7:invalid.feet.y+=10;break; // Walking without support.
        case 8:invalid.feet.y=-6;invalid.mode=CovePlayer::Mode::Airborne;break;
        }
        EXPECT_FALSE(player.restore(invalid))<<sample;EXPECT_EQ(player.feet(),before.feet);
        EXPECT_EQ(player.mode(),before.mode);EXPECT_EQ(player.tick(),before.tick);
    }
    player.advance(CovePlayer::fixedStep,{});EXPECT_TRUE(player.onBoat());
    EXPECT_EQ(player.interactions(),before.interactions+1);
}

TEST_F(CoveMovement, SolidGeneratorStopsWalkingAndLargeFramesDoNotTunnel) {
    for (int i = 0; i < 24; ++i) player.advance(.25, {{0, -1}, false});
    EXPECT_GT(player.feet().z, -52.41);
    EXPECT_LT(player.feet().z, -52.3);
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
}

TEST_F(CoveMovement, JumpingAboardUsesActualSupportingPartInsteadOfBoardButtonHistory) {
    approach();
    player.advance(CovePlayer::fixedStep, {{-1, 0}, true});
    move({-1, 0}, 39);
    move({0, 0}, 40);
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
    EXPECT_TRUE(player.onBoat());
    EXPECT_EQ(player.interactions(), 0u); // Landing supplies deck ownership.
    player.advance(CovePlayer::fixedStep, {{1, 0}, true});
    move({1, 0}, 39);
    move({0, 0}, 40);
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
    EXPECT_FALSE(player.onBoat());
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
}

TEST_F(CoveMovement, JumpLandsOnDockAndResetRecoversFromWater) {
    player.advance(CovePlayer::fixedStep, {{0, 0}, true});
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Airborne);
    EXPECT_GT(player.feet().y, 1.285);
    move({0, 0}, 100);
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
    move({1, 0}, 90);
    move({0, 0}, 90);
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Swimming);
    EXPECT_NEAR(player.feet().y, -.8, 1e-9);
    EXPECT_FALSE(player.requestInteraction());
    player.reset();
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Walking);
    EXPECT_NEAR(player.feet().x, 6, 1e-9);
    EXPECT_NEAR(player.feet().y, 1.285, 1e-9);
}

TEST_F(CoveMovement, WalksUpBrickTerraceAndJumpLandsOnStudSurface) {
    const auto encode=[](double y){return static_cast<uint16_t>(std::lround((y/600+1)*32767.5));};
    std::vector<uint16_t> samples(257*257,encode(0));
    for(size_t z=0;z<257;++z) for(size_t x=150;x<257;++x) samples[z*257+x]=encode(.32);
    const terrain::lego::Surface surface{samples,257,257,600,1};
    auto land=*scene;land.registry.navigation->spawn={20.5,.18,20.5};
    std::string error;
    ASSERT_TRUE(player.initialize(land,[&](double x,double z){
        return x>10 ? double(surface.heightAt(float(x),float(z))) : -5.0;
    },error))<<error;
    move({1,0},80);move({0,0},30);
    EXPECT_GT(player.feet().x,23);
    EXPECT_NEAR(player.feet().y,.32+.18+.005,.001);
    EXPECT_EQ(player.mode(),CovePlayer::Mode::Walking);
    player.advance(CovePlayer::fixedStep,{{0,0},true});
    EXPECT_EQ(player.mode(),CovePlayer::Mode::Airborne);
    move({0,0},100);
    EXPECT_EQ(player.mode(),CovePlayer::Mode::Walking);
    EXPECT_NEAR(player.feet().y,.32+.18+.005,.001);
}

TEST_F(CoveMovement, FixedTicksDoNotDependOnRenderRateAndJumpSurvivesShortFrame) {
    CovePlayer other;
    std::string error;
    ASSERT_TRUE(other.initialize(*scene, [](double,double) { return -5.; }, error));
    for (int i=0; i<120; ++i) player.advance(1./120., {{0,1}, false});
    for (int i=0; i<30; ++i) other.advance(1./30., {{0,1}, false});
    EXPECT_EQ(player.tick(), 60u);
    EXPECT_EQ(player.feet(), other.feet());
    player.advance(1./240., {{0,0}, true});
    player.advance(1./80., {});
    EXPECT_EQ(player.mode(), CovePlayer::Mode::Airborne);
}

TEST_F(CoveMovement, InvalidNavigationPreservesExistingPlayerAndPendingActionsDoNotDuplicate) {
    auto changed = *scene;
    changed.registry.navigation->spawn = {6, 1.28, -53.5}; // Inside generator.
    const auto before = player.feet();
    std::string error;
    EXPECT_FALSE(player.initialize(changed, [](double,double) { return -5.; }, error));
    EXPECT_EQ(player.feet(), before);
    changed.registry.navigation->spawn = {100, 100, 100};
    EXPECT_FALSE(player.initialize(changed, [](double,double) { return -5.; }, error));
    approach();
    ASSERT_TRUE(player.requestInteraction());
    EXPECT_FALSE(player.requestInteraction());
    player.advance(CovePlayer::fixedStep, {});
    EXPECT_EQ(player.interactions(), 1u);
    ASSERT_TRUE(player.requestInteraction());
    player.reset();
    player.advance(CovePlayer::fixedStep, {});
    EXPECT_FALSE(player.onBoat());
    EXPECT_EQ(player.interactions(), 0u);
}

TEST(CoveNavigation, RejectsUnknownMissingNonfiniteAndDistantBoardingData) {
    std::ifstream input("data/salvage/fixture-cove-r01.json");
    nlohmann::json source; input >> source;
    std::string error;
    ASSERT_TRUE(assets::parseAssetFixtureRegistry(source.dump(), error)) << error;
    auto changed = source; changed["navigation"]["dock_boarding"] = {100, 1, 0};
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
    changed = source; changed["navigation"]["teleport"] = true;
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
    changed = source; changed["navigation"].erase("spawn");
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
    changed = source; changed["navigation"]["spawn"] = {nullptr, 1, 0};
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
    changed = source; changed["navigation"]["boat_placements"] = {0,0};
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
    changed = source; changed["navigation"]["boat_placements"] = {99};
    EXPECT_FALSE(assets::parseAssetFixtureRegistry(changed.dump(), error));
}
} // namespace
} // namespace voxy::game::expedition

#include "game/expedition/cove_test_session.hpp"
#include "game/expedition/cove_build.hpp"
#include "game/expedition/cove_rigid_roots.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>

namespace voxy::game::expedition {
namespace {
using namespace construction;

class PracticeAdapter final:public PreparationAdapter {
public:
    size_t begins=0,activations=0,discards=0;
    PreparationResult begin(const PreparationRequest& r)override{++begins;return {r.ticket,PreparationState::Ready};}
    PreparationResult poll(PreparationTicket t)noexcept override{return {t,PreparationState::Ready};}
    bool canActivate(PreparationTicket)const noexcept override{return true;}
    void activate(PreparationTicket,SimulationTick)noexcept override{++activations;}
    void discard(PreparationTicket)noexcept override{++discards;}
};

class CovePracticeTest:public testing::Test {
protected:
    WorldNamespace world{{'p','r','a','c','t','i','c','e','-','t','e','s','t'}};
    std::unique_ptr<const assets::LoadedAssetFixture> scene;
    std::optional<CoveBuildSeed> seed;
    std::unique_ptr<CoveBoatAssembly> owned,cargo;
    std::unique_ptr<CoveWorkshop> workshop;
    PracticeAdapter adapter;
    SessionBootstrap bootstrap;
    std::unique_ptr<GameSession> session;
    CoveSaveContext context;
    CovePhysicalSave physical;
    std::string error;

    void SetUp()override {
        scene=assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
        ASSERT_TRUE(scene)<<error;
        scene=assets::appendAssetFixtureCatalog(*scene,std::filesystem::canonical("data/salvage/cove-bricks-r02.json"),error);
        ASSERT_TRUE(scene)<<error;
        seed=prepareCoveBuild(*scene,{world,1},4,error);ASSERT_TRUE(seed)<<error;
        owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
        cargo=CoveBoatAssembly::compileCargo(*scene,scene->registry.navigation->cargoPlacements.front(),error);ASSERT_TRUE(cargo)<<error;
        workshop=CoveWorkshop::create(*scene,error);ASSERT_TRUE(workshop)<<error;
        bootstrap.world=world;bootstrap.caller={{world,1},{world,2}};bootstrap.lastIssuedId=seed->issuedThrough;
        bootstrap.inventory=coveStartingMaterials;bootstrap.workshopEnabled=true;
        bootstrap.builds={seed->build};bootstrap.starterEntitlements={seed->starterEntitlement};bootstrap.jobs.push_back({{world,3}});
        const auto& placed=scene->registry.placements[cargo->parts().front().placement];
        const auto& definition=scene->bundles[placed.bundleIndex]->sidecar().part;
        bootstrap.cargoDefinitions.push_back({definition.key,definition.mass.dryMassKg,cargo->displacementCubicMetres(),
            definition.salvageYield,CargoRecoveryRule::PreserveUnique});
        bootstrap.cargo.push_back({{world,4},definition.key,{world,1},{-4.5,-3,-58},{},DurableId{world,3}});
        context.identity.world=world;context.identity.content.manifest={{{{92,1}},1},1};context.identity.content.manifestDigest[0]=std::byte{1};
        context.boat=seed->build.id;context.job={world,3};context.cargo={world,4};context.cargoDefinition=bootstrap.cargoDefinitions.front();
        physical.boat=context.boat;physical.cargo=context.cargo;physical.job=context.job;physical.cargoDefinition=definition.key;
        physical.boatMotion.position={-3,-1,-50};physical.boatMotion.originVelocity={.2f,0,-.1f};physical.cargoMotion.position={-4.5,-3,-58};
        physical.player.feet={6,1.285,-49};physical.player.mode=CoveSavedPlayerMode::Walking;physical.water.seconds=321.5;
        auto roots=CoveRigidRoots::prepare(*owned,error);ASSERT_TRUE(roots)<<error;
        physical.boatRoots.push_back({roots->primary().key,physical.boatMotion});physical.controlPart=*owned->primaryRoot().helm;
    }
    void start(ResourceAmounts inventory=coveStartingMaterials) {
        bootstrap.inventory=inventory;EventStreamIncarnation observer;observer.bytes[0]=19;SessionIssue issue;
        session=GameSession::create(bootstrap,observer,seed->catalog,adapter,issue);ASSERT_TRUE(session)<<int(issue.error);
        // A real accepted job and its receipt/history are part of the protected
        // session. Practice may neither consume its cargo nor award its yield.
        const Command command{AuthorityEpoch{1},RequestSequence{1},session->snapshot().revision,AcceptJob{context.job}};
        ASSERT_TRUE(session->submit(bootstrap.caller,command).admitted);ASSERT_TRUE(session->advanceOneTick());
        ASSERT_EQ(session->receipt(bootstrap.caller,command.sequence).state,ReceiptState::Committed);
    }
    std::vector<std::byte> encode() {
        physical.tick=session->snapshot().tick;physical.player.tick=physical.tick.value();
        RecoveryIssue ri;auto image=SessionRecovery::capture(*session,context.identity.content,ri);
        if(!image)return {};
        auto checked=SessionRecovery::admit(*image,context.identity,seed->catalog,ri);if(!checked)return {};
        CoveSaveIssue ci;std::vector<std::byte> bytes;
        if(!CoveSaveCodec::encode(*checked,nullptr,physical,context,seed->catalog,bytes,ci))return {};
        return bytes;
    }
    void addPontoon() {
        ASSERT_TRUE(workshop->addPart());ASSERT_TRUE(workshop->valid())<<workshop->message();
        ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Keep));
    }
};

TEST_F(CovePracticeTest, UnaffordableKeptDraftRunsWithoutPurchasesOrCanonicalChanges) {
    start({});ASSERT_TRUE(session);addPontoon();
    const auto quote=quoteCoveDesign(workshop->design(),*owned,seed->catalog,{});ASSERT_TRUE(quote);EXPECT_FALSE(quote->affordable({}));
    const auto bytes=encode();ASSERT_FALSE(bytes.empty());const auto calls=adapter.begins;const auto issued=session->lastIssuedId();
    auto practice=CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error);ASSERT_TRUE(practice)<<error;
    auto boat=practice->takeDraftBoat();ASSERT_TRUE(boat);EXPECT_EQ(boat->parts().size(),owned->parts().size()+1);
    EXPECT_NE(boat->build().id.world,world);EXPECT_EQ(practice->physical(),physical);
    ASSERT_TRUE(practice->stage(8,physical.tick.value()+1,false));ASSERT_TRUE(practice->confirm(8,physical.tick.value()+1,*session,error))<<error;
    ASSERT_TRUE(practice->stage(8,physical.tick.value()+300,true));ASSERT_TRUE(practice->confirm(8,physical.tick.value()+300,*session,error))<<error;
    EXPECT_EQ(practice->phase(),CoveTestSession::Phase::Complete);EXPECT_EQ(adapter.begins,calls);EXPECT_EQ(session->lastIssuedId(),issued);
    EXPECT_EQ(encode(),bytes);EXPECT_EQ(session->snapshot().inventory,ResourceAmounts{});
}

TEST_F(CovePracticeTest, RefusedOrStaleReturnKeepsTheOnlyRollbackAndEditorHistory) {
    start();ASSERT_TRUE(session);addPontoon();const auto draft=workshop->blueprintBytes(error);
    const auto undo=workshop->undoCount();const auto selected=workshop->selected();const auto bytes=encode();ASSERT_FALSE(bytes.empty());
    auto practice=CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error);ASSERT_TRUE(practice)<<error;
    ASSERT_TRUE(practice->stage(7,2,false));EXPECT_FALSE(practice->confirm(8,2,*session,error));EXPECT_FALSE(practice->confirm(7,1,*session,error));
    ASSERT_TRUE(practice->confirm(7,2,*session,error));EXPECT_FALSE(practice->stage(8,3,true));EXPECT_FALSE(practice->stage(7,2,true));
    ASSERT_TRUE(practice->stage(7,30,true));EXPECT_FALSE(practice->confirm(7,29,*session,error));
    EXPECT_EQ(std::vector<std::byte>(practice->rollbackBytes().begin(),practice->rollbackBytes().end()),bytes);
    EXPECT_EQ(workshop->blueprintBytes(error),draft);EXPECT_EQ(workshop->undoCount(),undo);EXPECT_EQ(workshop->selected(),selected);
    ASSERT_TRUE(practice->confirm(7,30,*session,error));EXPECT_FALSE(practice->stage(7,31,true));
    EXPECT_EQ(encode(),bytes);
}

TEST_F(CovePracticeTest, RealDeliveryDuringPracticeIsDetectedBeforeReturnPublication) {
    start();ASSERT_TRUE(session);const auto bytes=encode();auto practice=CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error);ASSERT_TRUE(practice)<<error;
    ASSERT_TRUE(practice->stage(7,2,false));ASSERT_TRUE(practice->confirm(7,2,*session,error));
    const auto before=session->snapshot().inventory;
    const Command deliver{AuthorityEpoch{1},RequestSequence{2},session->snapshot().revision,DeliverCargo{context.cargo}};
    ASSERT_TRUE(session->submit(bootstrap.caller,deliver).admitted);ASSERT_TRUE(session->advanceOneTick());
    ASSERT_EQ(session->receipt(bootstrap.caller,deliver.sequence).state,ReceiptState::Committed);
    EXPECT_EQ(session->snapshot().inventory.salvageMaterial,before.salvageMaterial+context.cargoDefinition.value.salvageMaterial);
    ASSERT_TRUE(practice->stage(7,3,true));EXPECT_FALSE(practice->confirm(7,3,*session,error));
    EXPECT_EQ(practice->phase(),CoveTestSession::Phase::Returning);
    EXPECT_EQ(std::vector<std::byte>(practice->rollbackBytes().begin(),practice->rollbackBytes().end()),bytes);
}

TEST_F(CovePracticeTest, NormalLaunchAfterPracticeStillPaysOnceAndKeepsOwnedIdentities) {
    start();ASSERT_TRUE(session);addPontoon();const auto bytes=encode();
    auto practice=CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error);ASSERT_TRUE(practice)<<error;
    ASSERT_TRUE(practice->stage(7,2,false));ASSERT_TRUE(practice->confirm(7,2,*session,error));
    ASSERT_TRUE(practice->stage(7,3,true));ASSERT_TRUE(practice->confirm(7,3,*session,error));
    auto refit=prepareCoveRefit(workshop->design(),*owned,seed->catalog,error,scene->registry.navigation->boatPlacements);ASSERT_TRUE(refit)<<error;
    const auto quote=quoteCoveDesign(workshop->design(),*owned,seed->catalog,{});ASSERT_TRUE(quote);
    const auto before=session->snapshot();const auto issued=session->lastIssuedId();
    const Command launch{AuthorityEpoch{1},RequestSequence{2},before.revision,RefitBuild{{context.boat,owned->build().revision},refit->design}};
    ASSERT_TRUE(session->submit(bootstrap.caller,launch).admitted);ASSERT_TRUE(session->advanceOneTick());
    ASSERT_EQ(session->receipt(bootstrap.caller,launch.sequence).state,ReceiptState::Committed);
    const auto after=session->snapshot();EXPECT_EQ(after.inventory.salvageMaterial,before.inventory.salvageMaterial-quote->debit.salvageMaterial);
    EXPECT_GT(session->lastIssuedId(),issued);for(const auto& part:seed->build.parts)
        EXPECT_NE(std::find_if(after.builds.front().parts.begin(),after.builds.front().parts.end(),[&](const auto& p){return p.id==part.id;}),after.builds.front().parts.end());
    EXPECT_EQ(session->submit(bootstrap.caller,launch).state,ReceiptState::Committed);EXPECT_EQ(session->snapshot().inventory,after.inventory);
}

TEST_F(CovePracticeTest, CorruptRollbackAndUnkeptEditNeverChangeTheOriginal) {
    start();ASSERT_TRUE(session);const auto bytes=encode();ASSERT_FALSE(bytes.empty());
    auto damaged=bytes;damaged.back()^=std::byte{1};
    EXPECT_FALSE(CoveTestSession::prepare(damaged,context,seed->catalog,*workshop,error));
    ASSERT_TRUE(workshop->command(CoveWorkshop::Action::Raise));EXPECT_TRUE(workshop->changed());
    EXPECT_FALSE(CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error));
    EXPECT_EQ(encode(),bytes);EXPECT_TRUE(workshop->changed());
}

TEST_F(CovePracticeTest, UnusedBrickGhostIsNeitherTestedNorRemovedFromTheEditor) {
    start();ASSERT_TRUE(session);const auto bytes=encode();ASSERT_FALSE(bytes.empty());
    const auto kept=workshop->blueprintBytes(error);const auto count=workshop->design().registry.navigation->boatPlacements.size();
    uint32_t brick=static_cast<uint32_t>(workshop->catalogCount());
    for(uint32_t i=0;i<workshop->catalogCount();++i)if(workshop->catalogNameAt(i)=="Brick 1 x 2")brick=i;
    ASSERT_LT(brick,workshop->catalogCount());ASSERT_TRUE(workshop->beginBrickTool(brick));
    ASSERT_TRUE(workshop->brickToolActive());const auto ghost=workshop->selected();const auto transform=workshop->preview().registry.placements[ghost].placement;
    auto practice=CoveTestSession::prepare(bytes,context,seed->catalog,*workshop,error);ASSERT_TRUE(practice)<<error;
    EXPECT_EQ(practice->draft().registry.navigation->boatPlacements.size(),count);
    EXPECT_EQ(std::vector<std::byte>(practice->blueprint().begin(),practice->blueprint().end()),kept);
    EXPECT_TRUE(workshop->brickToolActive());EXPECT_EQ(workshop->selected(),ghost);
    EXPECT_EQ(workshop->preview().registry.placements[ghost].placement,transform);EXPECT_EQ(encode(),bytes);
}

} // namespace
} // namespace voxy::game::expedition

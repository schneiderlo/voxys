#include "game/expedition/cove_save.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::game::expedition {
namespace {
using namespace construction;
constexpr WorldNamespace world{{'c','o','v','e','-','s','a','v','e','-','t','e','s','t','0','1'}};
DurableId id(uint64_t n){return {world,n};}
class UnusedAdapter:public PreparationAdapter {
public:
    size_t calls=0;
    PreparationResult begin(const PreparationRequest& request)override{++calls;return {request.ticket,PreparationState::Rejected};}
    PreparationResult poll(PreparationTicket ticket)noexcept override{++calls;return {ticket,PreparationState::Rejected};}
    bool canActivate(PreparationTicket)const noexcept override{return false;}
    void activate(PreparationTicket,SimulationTick)noexcept override{++calls;}
    void discard(PreparationTicket)noexcept override{++calls;}
};
class CoveSave:public testing::Test {
protected:
    std::optional<PartCatalog> catalog;
    UnusedAdapter adapter;
    SessionBootstrap bootstrap;
    CoveSaveContext context;
    CovePhysicalSave physical;
    std::unique_ptr<GameSession> session;
    std::unique_ptr<ValidatedRecoveryCheckpoint> checkpoint;
    void SetUp()override{
        CatalogIssue ci;catalog=PartCatalog::create(makeStarterCatalogDraft(),ci);ASSERT_TRUE(catalog);
        bootstrap.world=world;bootstrap.caller={id(1),id(2)};bootstrap.lastIssuedId=100;
        bootstrap.tick=SimulationTick{9'007'199'254'740'995ull};bootstrap.inventory={48,0};bootstrap.workshopEnabled=true;
        BuildSnapshot build;build.id=id(3);build.owner=id(1);
        PartInstance part;part.id=id(10);part.definition=starterPartKey(StarterPart::Winch);part.owningBuild=build.id;
        part.settings=defaultModuleSettings(*catalog->lookup(part.definition).definition);build.parts.push_back(part);
        bootstrap.builds.push_back(build);bootstrap.jobs.push_back({id(5),7,JobPhase::Available,{}});
        bootstrap.cargoDefinitions.push_back({{id(1000),1},500,.3,{50,1},CargoRecoveryRule::PreserveUnique});
        bootstrap.cargo.push_back({id(6),{id(1000),1},id(1),{10,2,-30},{},id(5)});
        context.identity.world=world;context.identity.content.manifest={id(9000),3};
        context.identity.content.manifestDigest[0]=std::byte{0x5a};
        context.boat=id(3);context.cargo=id(6);context.job=id(5);context.cargoDefinition=bootstrap.cargoDefinitions.front();
        physical.tick=bootstrap.tick;physical.boat=context.boat;physical.cargo=context.cargo;
        physical.job=context.job;physical.cargoDefinition=context.cargoDefinition.key;
        physical.boatMotion.position={100,20,-300};physical.boatMotion.originVelocity={1.25f,0,-2.5f};
        physical.boatMotion.angularVelocity={.125f,-.25f,.375f};
        physical.cargoMotion.position={101,18,-304};physical.cargoMotion.originVelocity={.5f,-.25f,-1};
        physical.player.feet={2,.965,-54};physical.player.aboard=true;physical.player.mode=CoveSavedPlayerMode::Helm;
        physical.player.tick=UINT64_MAX-7;physical.player.interactions=UINT64_MAX-11;
        physical.player.viewYaw=-2.5f;physical.player.viewPitch=-.25f;physical.water.seconds=123.125;
        physical.cargoState=CoveSavedCargoState::Towed;physical.winchPart=part.id;physical.ropeLength=6.25f;
        recapture();ASSERT_TRUE(checkpoint);
    }
    void recapture(){
        EventStreamIncarnation incarnation;incarnation.bytes[0]=2;
        SessionIssue si;session=GameSession::create(bootstrap,incarnation,*catalog,adapter,si);ASSERT_TRUE(session);
        RecoveryIssue ri;auto image=SessionRecovery::capture(*session,context.identity.content,ri);ASSERT_TRUE(image);
        checkpoint=SessionRecovery::admit(*image,context.identity,*catalog,ri);ASSERT_TRUE(checkpoint);
    }
    std::vector<std::byte> encode(){
        std::vector<std::byte> bytes;CoveSaveIssue issue;
        EXPECT_TRUE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,bytes,issue));
        return bytes;
    }
    static void checksum(std::vector<std::byte>& bytes){
        const auto digest=core::sha256(std::span(bytes).first(bytes.size()-32));
        std::copy(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32);
    }
    void fragments(size_t count=2){
        ASSERT_GE(count,2u);ASSERT_LE(count,kMaximumCoveSavedRoots);
        auto& build=bootstrap.builds.front();
        PartInstance helm;helm.id=id(20);helm.owningBuild=build.id;helm.definition=starterPartKey(StarterPart::Helm);
        helm.placement.translation={0,72,0};helm.settings=defaultModuleSettings(*catalog->lookup(helm.definition).definition);
        build.parts.push_back(helm);
        Connection weld;weld.id=id(90);weld.a={id(10),SocketId{1}};weld.b={id(20),SocketId{2}};
        weld.strength={1000,1000,1000,1000};build.connections.push_back(weld);
        for(size_t i=2;i<count;++i){
            PartInstance part;part.id=id(20+i);part.owningBuild=build.id;part.definition=starterPartKey(StarterPart::Beam);
            part.placement.translation={static_cast<int32_t>(1000*(i-1)),0,0};
            part.settings=defaultModuleSettings(*catalog->lookup(part.definition).definition);build.parts.push_back(part);
        }
        BuildIssue issue;auto model=BuildModel::create(build,*catalog,issue);ASSERT_TRUE(model)<<issue.field;
        const std::array cuts{weld.id};ASSERT_FALSE(model->cutWelds(build.revision,cuts,*catalog));build=model->snapshot();
        recapture();ASSERT_TRUE(checkpoint);
        physical.controlPart=helm.id;physical.playerRoot=helm.id;
        for(size_t i=0;i<count;++i){
            auto state=physical.boatMotion;
            if(i!=1){state.position={-900000+20000*double(i),-500+double(i),100+double(i)};state.originVelocity[0]=float(i)+.5f;}
            physical.boatRoots.push_back({build.parts[i].id,state});
        }
    }
    void secondCargo() {
        fragments();
        const CargoDefinition crate{{id(1001),1},700,.1536,{100,0},CargoRecoveryRule::PreserveUnique};
        context.additionalCargo.push_back({id(8),id(7),crate});
        bootstrap.cargoDefinitions.push_back(crate);bootstrap.jobs.push_back({id(7)});
        bootstrap.cargo.push_back({id(8),crate.key,id(1),{-30,-4,-70},{},id(7)});
        CoveSavedCargo saved;saved.cargo=id(8);saved.job=id(7);saved.definition=crate.key;
        saved.motion.position={-31,-3.5,-74};saved.motion.originVelocity={.25f,-.5f,.75f};
        saved.motion.orientation={0,1,0,0};physical.additionalCargo.push_back(saved);
        recapture();
    }
    void bankGenerator() {
        bootstrap.jobs[0].phase=JobPhase::Completed;bootstrap.jobs[0].acceptedBy=id(1);
        std::erase_if(bootstrap.cargo,[&](const auto& cargo){return cargo.id==context.cargo;});
        bootstrap.inventory.salvageMaterial+=context.cargoDefinition.value.salvageMaterial;
        bootstrap.inventory.specialMachinery+=context.cargoDefinition.value.specialMachinery;
        physical.cargoState=CoveSavedCargoState::Banked;physical.winchPart={};physical.ropeLength=0;
        physical.cargoMotion.originVelocity={};physical.cargoMotion.angularVelocity={};recapture();
    }
};

TEST_F(CoveSave, CharacterArchivePreservesAirborneMomentumCameraAndExactLegacyEncoding) {
    fragments();
    const auto legacy=encode();ASSERT_EQ(legacy[4],std::byte{4});
    physical.character.profile=1;
    physical.character.worldVelocity={4.5,8.25,-3.125};
    physical.character.facingYaw=-1.25;physical.character.cameraDistance=7.75;
    physical.character.chaseCamera=false;physical.character.reducedMotion=true;physical.character.loadView=true;
    physical.player.mode=CoveSavedPlayerMode::Airborne;physical.player.aboard=false;physical.playerRoot={};
    physical.player.verticalSpeed=8.25;
    const auto bytes=encode();ASSERT_EQ(bytes[4],std::byte{6});
    CoveSaveIssue issue;auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);
    ASSERT_TRUE(read)<<int(issue.error);EXPECT_EQ(read->physical,physical);
    std::vector<std::byte> again;
    ASSERT_TRUE(CoveSaveCodec::encode(*read->current,nullptr,read->physical,context,*catalog,again,issue));
    EXPECT_EQ(again,bytes);
    auto old=CoveSaveCodec::decode(legacy,context,*catalog,issue);ASSERT_TRUE(old)<<int(issue.error);
    EXPECT_EQ(old->physical.character,CoveSavedCharacter{});
    ASSERT_TRUE(CoveSaveCodec::encode(*old->current,nullptr,old->physical,context,*catalog,again,issue));
    EXPECT_EQ(again,legacy);
    // One-byte truncation with a repaired checksum must not consume logical
    // checkpoint bytes as a partially initialized character extension.
    auto truncated=bytes;truncated.erase(truncated.end()-33);checksum(truncated);
    EXPECT_FALSE(CoveSaveCodec::decode(truncated,context,*catalog,issue));
}

TEST_F(CoveSave, CharacterArchiveRejectsContradictoryMomentumWithoutChangingOutput) {
    fragments();physical.character.profile=1;
    physical.player.mode=CoveSavedPlayerMode::Airborne;physical.player.aboard=false;physical.playerRoot={};
    physical.player.verticalSpeed=6;physical.character.worldVelocity={2,6,3};
    const auto good=physical;const auto original=encode();
    for(int defect=0;defect<8;++defect) {
        physical=good;
        switch(defect) {
        case 0:physical.character.worldVelocity[0]=std::numeric_limits<double>::quiet_NaN();break;
        case 1:physical.character.worldVelocity[2]=151;break;
        case 2:physical.character.worldVelocity[1]=5;break;
        case 3:physical.player.aboard=true;physical.playerRoot=physical.boatRoots[0].key;break;
        case 4:physical.character.profile=2;break;
        case 5:physical.character.profile=0;break;
        case 6:physical.character.cameraDistance=100;break;
        case 7:physical.character.facingYaw=100;break;
        }
        std::vector<std::byte> output=original;CoveSaveIssue issue;
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue))<<defect;
        EXPECT_EQ(output,original)<<defect;
    }
}

TEST_F(CoveSave, CharacterExtensionRetainsBothCargoRecords) {
    secondCargo();physical.character.profile=1;
    const auto bytes=encode();ASSERT_EQ(bytes[4],std::byte{6});CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);
    ASSERT_TRUE(read)<<int(issue.error);EXPECT_EQ(read->physical,physical);
}

TEST_F(CoveSave, TwoJobArchiveRetainsBothLoadsAcrossGeneratorBankingCrateTowAndBothReceipts) {
    secondCargo();const auto originalParts=bootstrap.builds[0].parts;
    const auto originalCargo=physical.additionalCargo[0];
    const auto roundTrip=[&] {
        const auto bytes=encode();EXPECT_EQ(bytes[4],std::byte{5});CoveSaveIssue issue;
        auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);EXPECT_TRUE(read)<<int(issue.error);
        if(read) {
            EXPECT_EQ(read->physical,physical);const auto& accepted=read->current->snapshot().accepted;
            EXPECT_EQ(accepted.inventory,bootstrap.inventory);EXPECT_EQ(accepted.builds[0].parts,originalParts);
            EXPECT_EQ(accepted.cargo,bootstrap.cargo);EXPECT_EQ(accepted.jobs,bootstrap.jobs);
            std::vector<std::byte> again;EXPECT_TRUE(CoveSaveCodec::encode(*read->current,nullptr,read->physical,context,*catalog,again,issue));
            EXPECT_EQ(again,bytes);
        }
    };
    roundTrip();bankGenerator();roundTrip(); // Crate survives the first payout.
    physical.harborLift.profile=kCoveHarborLiftProfile;
    bootstrap.jobs[1].phase=JobPhase::Accepted;bootstrap.jobs[1].acceptedBy=id(1);recapture();
    physical.additionalCargo[0].state=CoveSavedCargoState::Towed;
    physical.additionalCargo[0].winchPart=id(10);physical.additionalCargo[0].ropeLength=7.5f;roundTrip();
    physical.additionalCargo[0].state=CoveSavedCargoState::BrokenTow;roundTrip();
    physical.additionalCargo[0].state=CoveSavedCargoState::Loose;
    physical.additionalCargo[0].winchPart={};physical.additionalCargo[0].ropeLength=0;roundTrip();
    bootstrap.jobs[1].phase=JobPhase::Completed;bootstrap.cargo.clear();bootstrap.inventory.salvageMaterial+=100;recapture();
    physical.additionalCargo[0].state=CoveSavedCargoState::Banked;
    physical.additionalCargo[0].motion.originVelocity={};physical.additionalCargo[0].motion.angularVelocity={};roundTrip();
    EXPECT_EQ(physical.additionalCargo[0].motion.position,originalCargo.motion.position);
    EXPECT_EQ(bootstrap.inventory,(ResourceAmounts{198,1}));EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, SecondJobCannotSkipGeneratorRewardOrPoweredHarbor) {
    secondCargo();bootstrap.jobs[1].phase=JobPhase::Accepted;bootstrap.jobs[1].acceptedBy=id(1);recapture();
    const auto refused=[&] {
        std::vector<std::byte> output{std::byte{42}};CoveSaveIssue issue;
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
        EXPECT_EQ(issue.error,CoveSaveError::LogicalState);EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
    };
    refused();physical.harborLift.profile=kCoveHarborLiftProfile;refused();
    bankGenerator();physical.harborLift.profile=0;refused();
    physical.harborLift.profile=kCoveHarborLiftProfile;EXPECT_FALSE(encode().empty());
}

TEST_F(CoveSave, TwoCargoCannotDropAliasOrInventAJobAndCannotShareOneWinch) {
    secondCargo();const auto before=physical;const auto expected=context;
    for(int fault=0;fault<16;++fault) {
        physical=before;context=expected;
        switch(fault) {
        case 0:physical.additionalCargo.clear();break;
        case 1:context.additionalCargo.clear();break;
        case 2:physical.additionalCargo.push_back(physical.additionalCargo[0]);break;
        case 3:context.additionalCargo[0].job=context.job;break;
        case 4:context.additionalCargo[0].cargo=context.cargo;break;
        case 5:physical.additionalCargo[0].cargo=context.cargo;break;
        case 6:physical.additionalCargo[0].job=context.job;break;
        case 7:physical.additionalCargo[0].definition=physical.cargoDefinition;break;
        case 8:context.additionalCargo[0].definition.massKg+=1;break;
        case 9:physical.additionalCargo[0].motion.position.x=std::numeric_limits<double>::quiet_NaN();break;
        case 10:physical.additionalCargo[0].state=CoveSavedCargoState::Banked;break;
        case 11:physical.additionalCargo[0].winchPart=id(10);break;
        case 12:physical.boatRoots.clear();physical.controlPart={};physical.playerRoot={};break;
        case 13:physical.additionalCargo[0].state=CoveSavedCargoState::Towed;physical.additionalCargo[0].winchPart=id(20);physical.additionalCargo[0].ropeLength=7.5f;break;
        case 14:physical.additionalCargo[0].state=CoveSavedCargoState::Towed;physical.additionalCargo[0].winchPart=id(10);physical.additionalCargo[0].ropeLength=7.5f;break;
        case 15:context.additionalCargo[0].job.world.bytes[0]^=1;break;
        }
        SCOPED_TRACE(fault);CoveSaveIssue issue;std::vector<std::byte> output{std::byte{42}};
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
        EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
    }
    physical=before;context=expected;EXPECT_FALSE(encode().empty());
}

TEST_F(CoveSave, VersionFiveBoundsPhysicalCargoBeforeAllocationAndDoesNotPermitLegacyDowngrade) {
    const auto legacy=encode();secondCargo();const auto bytes=encode();CoveSaveIssue issue;
    auto oneJobContext=context;oneJobContext.additionalCargo.clear();
    EXPECT_FALSE(CoveSaveCodec::decode(bytes,oneJobContext,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Identity);
    EXPECT_FALSE(CoveSaveCodec::decode(legacy,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Identity);
    const size_t countAt=445+52+88*physical.boatRoots.size();
    ASSERT_EQ(bytes.at(countAt),std::byte{1});
    for(uint8_t count:{uint8_t{0},uint8_t{2},uint8_t{255}}) {
        auto corrupt=bytes;corrupt[countAt]=std::byte{count};checksum(corrupt);
        EXPECT_FALSE(CoveSaveCodec::decode(corrupt,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Capacity);
    }
    auto truncated=bytes;truncated.resize(countAt+4+168+32);checksum(truncated);
    EXPECT_FALSE(CoveSaveCodec::decode(truncated,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Encoding);
    auto downgraded=bytes;downgraded[4]=std::byte{4};checksum(downgraded);
    EXPECT_FALSE(CoveSaveCodec::decode(downgraded,context,*catalog,issue));
    EXPECT_TRUE(CoveSaveCodec::decode(bytes,context,*catalog,issue));
}

TEST_F(CoveSave, TowingRoundTripPreservesMotionIdentityAndLargeCounters){
    const auto bytes=encode();ASSERT_FALSE(bytes.empty());CoveSaveIssue issue;
    const auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);
    EXPECT_EQ(read->physical,physical);EXPECT_FALSE(read->retiredParent);
    EXPECT_EQ(read->current->snapshot().accepted.tick,bootstrap.tick);
    EXPECT_EQ(read->current->snapshot().accepted.inventory,bootstrap.inventory);
    const auto& build=read->current->snapshot().accepted.builds.front();
    EXPECT_EQ(build.id,bootstrap.builds.front().id);
    EXPECT_EQ(build.parts,bootstrap.builds.front().parts);
    std::vector<std::byte> buildBytes,expectedBuildBytes;
    ASSERT_FALSE(encodeBuild(build,*catalog,buildBytes));
    ASSERT_FALSE(encodeBuild(bootstrap.builds.front(),*catalog,expectedBuildBytes));
    EXPECT_EQ(buildBytes,expectedBuildBytes);
    // Logical cargo location is its accepted transaction/recovery record;
    // the physical section independently owns its current dynamic root motion.
    EXPECT_EQ(read->current->snapshot().accepted.cargo,bootstrap.cargo);
    EXPECT_NE(read->physical.cargoMotion.position,bootstrap.cargo.front().position);
    std::vector<std::byte> again;
    ASSERT_TRUE(CoveSaveCodec::encode(*read->current,nullptr,read->physical,context,*catalog,again,issue));EXPECT_EQ(again,bytes);
    EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, RecoveryDesignIgnoresOwnershipConditionAndOrderingButPreservesConfiguration) {
    auto build=bootstrap.builds.front();auto other=build.parts.front();other.id=id(11);other.placement.translation={1000,0,0};build.parts.push_back(other);
    const auto first=makeCoveRecoveryDesign(build,*catalog);ASSERT_FALSE(first.empty());
    std::swap(build.parts[0].id,build.parts[1].id);build.parts[0].health=1234;
    build.parts[0].provenance={PartOrigin::StarterLoan,id(90)};
    EXPECT_EQ(makeCoveRecoveryDesign(build,*catalog),first);
    build.parts[0].paint[0]=42;EXPECT_NE(makeCoveRecoveryDesign(build,*catalog),first);
    build.parts[0].paint[0]=255;build.parts[0].settings.enabled=false;EXPECT_NE(makeCoveRecoveryDesign(build,*catalog),first);
}
TEST_F(CoveSave, RecoveryDesignCapacityDeduplicatesAndNeverOverwritesProtectedLayouts) {
    CoveRecoveryDesigns designs;
    for(size_t i=0;i<kMaximumCoveRecoveryDesigns;++i) {
        auto build=bootstrap.builds.front();build.parts.front().placement.translation.x=static_cast<int32_t>(i*1000);
        const auto bytes=makeCoveRecoveryDesign(build,*catalog);ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(rememberCoveRecoveryDesign(designs,bytes,*catalog),CoveRememberDesign::Stored);
        EXPECT_EQ(rememberCoveRecoveryDesign(designs,bytes,*catalog),CoveRememberDesign::Known);
    }
    const auto before=designs;auto build=bootstrap.builds.front();build.parts.front().placement.translation.y=1000;
    EXPECT_EQ(rememberCoveRecoveryDesign(designs,makeCoveRecoveryDesign(build,*catalog),*catalog),CoveRememberDesign::Full);
    auto bad=designs[0];bad.back()^=std::byte{1};
    EXPECT_EQ(rememberCoveRecoveryDesign(designs,bad,*catalog),CoveRememberDesign::Invalid);EXPECT_EQ(designs,before);
}
TEST_F(CoveSave, RecoveryEnvelopeRoundTripRetainsDesignsWithAndWithoutInstalledHarbor) {
    const auto baseline=encode();ASSERT_EQ(baseline[4],std::byte{1});
    physical.recoveryDesigns={makeCoveRecoveryDesign(bootstrap.builds.front(),*catalog)};
    for(bool installed:{false,true}) {
        physical.harborLift.profile=installed?1u:0u;
        const auto bytes=encode();ASSERT_EQ(bytes[4],std::byte{3});CoveSaveIssue issue;
        auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);
        EXPECT_EQ(read->physical,physical);EXPECT_EQ(read->current->snapshot().accepted.inventory,bootstrap.inventory);
        EXPECT_EQ(read->current->snapshot().accepted.builds.front().parts,bootstrap.builds.front().parts);
        auto bad=bytes;bad.at(441)=std::byte{5};checksum(bad);
        EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Capacity);
    }
    physical.harborLift={};physical.recoveryDesigns.clear();EXPECT_EQ(encode(),baseline);
}
TEST_F(CoveSave, InvalidDuplicateAndOverCapacityRecoveryDesignsPreserveEncodeOutput) {
    const auto design=makeCoveRecoveryDesign(bootstrap.builds.front(),*catalog);ASSERT_FALSE(design.empty());
    for(int fault=0;fault<4;++fault) {
        physical.recoveryDesigns={design};
        if(fault==0)physical.recoveryDesigns[0].clear();
        if(fault==1)physical.recoveryDesigns[0].back()^=std::byte{1};
        if(fault==2)physical.recoveryDesigns.push_back(design);
        if(fault==3)physical.recoveryDesigns.resize(kMaximumCoveRecoveryDesigns+1,design);
        std::vector<std::byte> output{std::byte{42}};CoveSaveIssue issue;
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
        EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
    }
}

TEST_F(CoveSave, LooseBrokenAndBankedCargoHaveDistinctMassAndRewardMeaning){
    CoveSaveIssue issue;
    physical.cargoState=CoveSavedCargoState::BrokenTow;
    auto read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);
    EXPECT_EQ(read->physical.cargoState,CoveSavedCargoState::BrokenTow);EXPECT_EQ(read->physical.ropeLength,6.25f);
    physical.cargoState=CoveSavedCargoState::Loose;physical.winchPart={};physical.ropeLength=0;
    read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);
    EXPECT_EQ(read->physical.cargoState,CoveSavedCargoState::Loose);
    bootstrap.jobs.front().phase=JobPhase::Completed;bootstrap.jobs.front().acceptedBy=id(1);
    bootstrap.cargo.clear();bootstrap.inventory.salvageMaterial+=50;recapture();
    physical.cargoState=CoveSavedCargoState::Banked;
    physical.cargoMotion.originVelocity={};physical.cargoMotion.angularVelocity={};
    read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);
    EXPECT_TRUE(read->current->snapshot().accepted.cargo.empty());
    EXPECT_EQ(read->current->snapshot().accepted.inventory.salvageMaterial,98u);
    EXPECT_EQ(read->physical.cargoState,CoveSavedCargoState::Banked);
}

TEST_F(CoveSave, HarborExtensionPreservesFourLengthsAndIndividualBreaksOnlyAfterBanking){
    physical.harborLift.profile=1;physical.harborLift.mode=CoveHarborLiftMode::Attached;physical.harborLift.lengths={4.1f,4.2f,4.3f,4.4f};
    CoveSaveIssue issue;std::vector<std::byte> refused{std::byte{42}};
    EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,refused,issue));
    EXPECT_EQ(issue.error,CoveSaveError::LogicalState);EXPECT_EQ(refused,(std::vector<std::byte>{std::byte{42}}));
    physical.cargoState=CoveSavedCargoState::Banked;physical.winchPart={};physical.ropeLength=0;
    physical.cargoMotion.originVelocity={};physical.cargoMotion.angularVelocity={};
    bootstrap.jobs.front().phase=JobPhase::Completed;bootstrap.jobs.front().acceptedBy=id(1);
    bootstrap.cargo.clear();bootstrap.inventory.salvageMaterial+=50;recapture();
    for(const uint8_t mask:std::array<uint8_t,4>{0,1,10,15}){
        physical.harborLift.mode=mask?CoveHarborLiftMode::Broken:CoveHarborLiftMode::Attached;
        physical.harborLift.brokenMask=mask;const auto bytes=encode();ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(bytes.at(4),std::byte{2});EXPECT_EQ(bytes.at(419),std::byte{1});
        auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);
        EXPECT_EQ(read->physical,physical);EXPECT_EQ(read->current->snapshot().accepted.inventory.salvageMaterial,98u);
        EXPECT_EQ(adapter.calls,0u);
        auto bad=bytes;bad.at(419)=std::byte{2};checksum(bad);
        EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::UnsupportedSchema);
        bad=bytes;bad.at(424)=std::byte{16};checksum(bad);
        EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);
    }
    const auto valid=physical.harborLift;
    for(int fault=0;fault<6;++fault){
        physical.harborLift=valid;
        switch(fault){
        case 0:physical.harborLift.mode=static_cast<CoveHarborLiftMode>(3);break;
        case 1:physical.harborLift.brokenMask=0;break;
        case 2:physical.harborLift.lengths[0]=2.49f;break;
        case 3:physical.harborLift.lengths[3]=12.01f;break;
        case 4:physical.harborLift.lengths[1]=std::numeric_limits<float>::quiet_NaN();break;
        case 5:physical.harborLift.mode=CoveHarborLiftMode::Detached;break;
        }
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,refused,issue))<<fault;
        EXPECT_EQ(issue.error,CoveSaveError::PhysicalState)<<fault;EXPECT_EQ(refused,(std::vector<std::byte>{std::byte{42}}));
    }
}

TEST_F(CoveSave, LegacyArchivesRemainIdenticalAndInstallingTheLiftIsExplicit){
    const auto legacy=encode();ASSERT_EQ(legacy.at(4),std::byte{1});CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(legacy,context,*catalog,issue);ASSERT_TRUE(read);
    EXPECT_EQ(read->physical.harborLift,CoveHarborLiftState{});
    auto alternate=legacy;alternate.at(4)=std::byte{2};
    alternate.insert(alternate.begin()+419,22,std::byte{});checksum(alternate);
    EXPECT_FALSE(CoveSaveCodec::decode(alternate,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::UnsupportedSchema);
    physical.harborLift.profile=1;
    const auto installed=encode();ASSERT_EQ(installed.at(4),std::byte{2});
    read=CoveSaveCodec::decode(installed,context,*catalog,issue);ASSERT_TRUE(read);
    EXPECT_EQ(read->physical.harborLift,physical.harborLift);
    EXPECT_EQ(read->physical.harborLift.mode,CoveHarborLiftMode::Detached);
    EXPECT_NE(installed,legacy); // No implicit collision/content upgrade of old saves.
}

TEST_F(CoveSave, RefusesMismatchedRolesTickCargoAndImpossibleMotionWithoutChangingOutput){
    const auto original=physical;
    for(int fault=0;fault<17;++fault){
        physical=original;
        switch(fault){
        case 0:physical.tick=SimulationTick{3};break;
        case 1:physical.boat=id(30);break;
        case 2:physical.cargo=id(60);break;
        case 3:physical.origin.x=1;break;
        case 4:physical.cargoState=CoveSavedCargoState::Banked;break;
        case 5:physical.cargoState=static_cast<CoveSavedCargoState>(99);break;
        case 6:physical.winchPart=id(99);break;
        case 7:physical.ropeLength=-1;break;
        case 8:physical.boatMotion.orientation.w=0;break;
        case 9:physical.boatMotion.originVelocity[0]=std::numeric_limits<float>::infinity();break;
        case 10:physical.cargoMotion.position.x=std::numeric_limits<double>::quiet_NaN();break;
        case 11:physical.player.aboard=false;break;
        case 12:physical.player.mode=CoveSavedPlayerMode::Swimming;break;
        case 13:physical.player.verticalSpeed=1;break;
        case 14:physical.water.model=2;break;
        case 15:physical.water.patchLengths[0]=0;break;
        case 16:physical.water.height=1;break;
        }
        CoveSaveIssue issue;std::vector<std::byte> output{std::byte{77}};
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue))<<fault;
        EXPECT_TRUE(issue)<<fault;EXPECT_EQ(output,(std::vector<std::byte>{std::byte{77}}))<<fault;
    }
    EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, RefusesCorruptionTruncationTrailingDataAndUnknownVersions){
    const auto good=encode();CoveSaveIssue issue;
    for(const size_t size:std::array<size_t,7>{0,4,8,40,100,good.size()-1,good.size()-32})
        EXPECT_FALSE(CoveSaveCodec::decode(std::span(good).first(size),context,*catalog,issue))<<size;
    auto bad=good;bad.at(48)^=std::byte{1};EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Checksum);
    bad=good;bad.at(4)=std::byte{7};EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));
    EXPECT_EQ(issue.error,CoveSaveError::UnsupportedSchema);
    bad=good;bad.insert(bad.end()-32,std::byte{});checksum(bad);
    EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Encoding);
    bad.assign(kMaximumCoveSaveBytes+1,std::byte{});
    EXPECT_FALSE(CoveSaveCodec::decode(bad,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Capacity);
    auto wrong=context;wrong.identity.world.bytes[0]^=1;
    EXPECT_FALSE(CoveSaveCodec::decode(good,wrong,*catalog,issue));
    wrong=context;wrong.cargoDefinition.massKg+=1;EXPECT_FALSE(CoveSaveCodec::decode(good,wrong,*catalog,issue));
}

TEST_F(CoveSave, RefusesResignedInvalidFieldsAndNestedLengths){
    const auto good=encode();CoveSaveIssue issue;
    const auto reject=[&](size_t offset,std::byte value,CoveSaveError expected){
        auto bytes=good;bytes.at(offset)=value;checksum(bytes);
        EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue))<<offset;
        EXPECT_EQ(issue.error,expected)<<offset;
    };
    // SVCE v1 fixed fields, independent of C++ padding and pointer width.
    reject(23,std::byte{0x80},CoveSaveError::NonCanonical); // origin.x = -0
    reject(316,std::byte{99},CoveSaveError::PhysicalState); // player mode
    reject(317,std::byte{2},CoveSaveError::Encoding); // aboard is a closed bool
    reject(326,std::byte{2},CoveSaveError::PhysicalState); // water algorithm
    reject(390,std::byte{99},CoveSaveError::PhysicalState); // cargo mode
    auto bytes=good;
    for(size_t i=419;i<423;++i)bytes.at(i)=std::byte{255};
    checksum(bytes);EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Capacity);
    bytes=good;bytes.at(455)^=std::byte{1};checksum(bytes);
    EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));
    EXPECT_EQ(issue.error,CoveSaveError::LogicalState);
    EXPECT_EQ(issue.session.error,SaveCodecError::Checksum);
    EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, FrozenPortableArchive){
    const auto bytes=encode();
    EXPECT_EQ(core::sha256Hex(core::sha256(bytes)),"46757880e6db9f473cab1e3ba0b2a31cb08cb30e50a3cdb3405a66195c5f366d");
    EXPECT_GT(bytes.size(),423u);
}

TEST_F(CoveSave, RecoveredArchiveRequiresExactRetiredParentAndRejectsOldToken){
    const auto bytes=encode();CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read);
    session.reset();UnusedAdapter replacement;RecoveryIssue ri;
    EventStreamIncarnation incarnation;incarnation.bytes[0]=1;
    auto recovered=SessionRecovery::restore(read->current,incarnation,replacement,ri);ASSERT_TRUE(recovered);
    std::vector<std::byte> output;
    EXPECT_FALSE(CoveSaveCodec::encode(*recovered->initial,nullptr,physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Lineage);
    EXPECT_FALSE(CoveSaveCodec::encode(*recovered->initial,checkpoint.get(),physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Lineage);
    ASSERT_TRUE(CoveSaveCodec::encode(*recovered->initial,recovered->retired.get(),physical,context,*catalog,output,issue));
    read=CoveSaveCodec::decode(output,context,*catalog,issue);ASSERT_TRUE(read);ASSERT_TRUE(read->retiredParent);
    EXPECT_EQ(read->physical,physical);EXPECT_FALSE(read->retiredParent->snapshot().admission.open);
    EXPECT_NE(read->current->snapshot().accepted.caller.sessionToken,bootstrap.caller.sessionToken);
    Command retry;retry.sequence=RequestSequence{1};retry.expectedRevision=bootstrap.revision;retry.intent=AcceptJob{id(5)};
    EXPECT_EQ(recovered->session->submit(bootstrap.caller,retry).issue.error,SessionError::WrongToken);
    EXPECT_EQ(recovered->session->snapshot().inventory,bootstrap.inventory);EXPECT_EQ(replacement.calls,0u);
}

TEST_F(CoveSave, FragmentArchiveKeepsRemoteMotionAndWinchApartFromTheHelmRoot){
    fragments();ASSERT_EQ(physical.boatRoots.size(),2u);
    const auto bytes=encode();ASSERT_FALSE(bytes.empty());EXPECT_EQ(bytes.at(4),std::byte{4});
    RecordProperty("fragment_archive_sha256",core::sha256Hex(core::sha256(bytes)));
    RecordProperty("fragment_archive_bytes",static_cast<int>(bytes.size()));
    EXPECT_EQ(bytes.size(),1878u);
    EXPECT_EQ(core::sha256Hex(core::sha256(bytes)),"dd3edb534176d1444915ba6b981e1a3404c32f003c6915c60623c31c00293882");
    CoveSaveIssue issue;auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);
    EXPECT_EQ(read->physical,physical);EXPECT_EQ(read->physical.boatRoots[1].motion,physical.boatMotion);
    EXPECT_EQ(read->physical.winchPart,physical.boatRoots[0].key);EXPECT_EQ(read->physical.controlPart,physical.boatRoots[1].key);
    EXPECT_EQ(read->current->snapshot().accepted.inventory,bootstrap.inventory);
    EXPECT_EQ(read->current->snapshot().accepted.tick,physical.tick);
    const auto& build=read->current->snapshot().accepted.builds.front();
    EXPECT_EQ(build.parts,bootstrap.builds.front().parts);ASSERT_EQ(build.connections.size(),1u);
    EXPECT_FALSE(build.connections[0].enabled);EXPECT_EQ(build.connections[0].damage,kFullHealth);
    std::vector<std::byte> again;ASSERT_TRUE(CoveSaveCodec::encode(*read->current,nullptr,read->physical,context,*catalog,again,issue));
    EXPECT_EQ(again,bytes);EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, FragmentsCannotBeEncodedOrResignedAsAnyLegacySingleMotionVersion){
    const auto backup=makeCoveRecoveryDesign(bootstrap.builds.front(),*catalog);fragments();
    const auto original=physical;
    for(uint8_t version=1;version<=3;++version){
        SCOPED_TRACE(version);physical=original;
        if(version==2)physical.harborLift.profile=1;
        if(version==3)physical.recoveryDesigns={backup};
        auto bytes=encode();ASSERT_EQ(bytes.at(4),std::byte{4});
        const size_t rootStart=445+(version==3?4+backup.size():0),rootSize=52+88*physical.boatRoots.size();
        bytes.erase(bytes.begin()+static_cast<std::ptrdiff_t>(rootStart),bytes.begin()+static_cast<std::ptrdiff_t>(rootStart+rootSize));
        if(version==1)bytes.erase(bytes.begin()+419,bytes.begin()+445);
        if(version==2)bytes.erase(bytes.begin()+441,bytes.begin()+445);
        bytes.at(4)=std::byte{version};checksum(bytes);CoveSaveIssue issue;
        EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);
        physical.boatRoots.clear();physical.controlPart={};physical.playerRoot={};
        std::vector<std::byte> output{std::byte{42}};
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
        EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
    }
}

TEST_F(CoveSave, FragmentRootMembershipMotionControlAndRiderMustMatchTheAcceptedBuild){
    fragments();const auto original=physical;
    for(int fault=0;fault<17;++fault){
        SCOPED_TRACE(fault);physical=original;
        switch(fault){
        case 0:physical.boatRoots.pop_back();break;
        case 1:physical.boatRoots.push_back({id(77),physical.boatMotion});break;
        case 2:physical.boatRoots[1].key=physical.boatRoots[0].key;break;
        case 3:std::swap(physical.boatRoots[0],physical.boatRoots[1]);break;
        case 4:physical.boatRoots[0].key.world.bytes[0]^=1;break;
        case 5:physical.controlPart=id(77);break;
        case 6:physical.controlPart=physical.winchPart;break;
        case 7:physical.boatMotion.originVelocity[0]+=1;break;
        case 8:physical.playerRoot={};break;
        case 9:physical.playerRoot=physical.boatRoots[0].key;break;
        case 10:physical.playerRoot=id(77);break;
        case 11:physical.player.aboard=false;physical.player.mode=CoveSavedPlayerMode::Walking;break;
        case 12:physical.boatRoots[0].motion.position.x=1e6+1;break;
        case 13:physical.boatRoots[0].motion.orientation.w=0;break;
        case 14:physical.boatRoots[0].motion.originVelocity[0]=std::numeric_limits<float>::quiet_NaN();break;
        case 15:physical.boatRoots[0].motion.angularVelocity[2]=10001;break;
        case 16:physical.harborLift={.profile=1,.mode=CoveHarborLiftMode::Attached,.lengths={4,4,4,4}};break;
        }
        CoveSaveIssue issue;std::vector<std::byte> output{std::byte{42}};
        EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
        EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
    }
}

TEST_F(CoveSave, FragmentRiderCanWalkOnAnotherRootButCannotUseADisabledOrRemoteHelm){
    fragments();physical.player.mode=CoveSavedPlayerMode::Walking;physical.playerRoot=physical.boatRoots[0].key;
    physical.player.feet={0,.96,0};CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);EXPECT_EQ(read->physical,physical);
    physical.player.aboard=false;physical.playerRoot={};
    read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);EXPECT_EQ(read->physical,physical);
    bootstrap.builds.front().parts[1].settings.enabled=false;recapture();
    // Disabled helm still identifies the primary root while the player is ashore.
    read=CoveSaveCodec::decode(encode(),context,*catalog,issue);ASSERT_TRUE(read);
    physical.player.aboard=true;physical.player.mode=CoveSavedPlayerMode::Helm;physical.playerRoot=id(20);
    std::vector<std::byte> output;EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);
}

TEST_F(CoveSave, ThirtyTwoFragmentMotionsAndFourProtectedDesignsFitTheBoundedArchive){
    const auto design=bootstrap.builds.front();fragments(kMaximumCoveSavedRoots);
    for(int i=0;i<4;++i){auto backup=design;backup.parts[0].placement.translation.x=i*1000;
        physical.recoveryDesigns.push_back(makeCoveRecoveryDesign(backup,*catalog));}
    physical.harborLift.profile=1;const auto bytes=encode();ASSERT_LT(bytes.size(),kMaximumCoveSaveBytes);
    RecordProperty("maximum_roots_archive_sha256",core::sha256Hex(core::sha256(bytes)));
    RecordProperty("maximum_roots_archive_bytes",static_cast<int>(bytes.size()));
    EXPECT_EQ(bytes.size(),8862u);
    EXPECT_EQ(core::sha256Hex(core::sha256(bytes)),"e909ab20940e0e820cec4ff2f3c44f451b49678c9a5277ac83f4e57fb4772bda");
    CoveSaveIssue issue;auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);
    EXPECT_EQ(read->physical,physical);EXPECT_EQ(read->physical.boatRoots.size(),32u);
    physical.boatRoots.push_back({id(80),physical.boatMotion});std::vector<std::byte> output{std::byte{42}};
    EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Capacity);EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
}

TEST_F(CoveSave, ResignedFragmentCountsAndRootRecordsAreValidatedBeforeAdmission){
    fragments();const auto good=encode();ASSERT_GT(good.size(),673u);CoveSaveIssue issue;
    // No designs: v4 control at 445, rider at 469, count at 493, roots at 497.
    for(uint32_t count:std::array<uint32_t,3>{0,33,UINT32_MAX}){
        auto bytes=good;for(size_t b=0;b<4;++b)bytes.at(493+b)=std::byte((count>>(8*b))&255);checksum(bytes);
        EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Capacity);
    }
    auto bytes=good;for(size_t i=0;i<24;++i)bytes.at(585+i)=bytes.at(497+i);checksum(bytes);
    EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::PhysicalState);
    bytes=good;bytes.resize(497+87);bytes.resize(bytes.size()+32);checksum(bytes);
    EXPECT_FALSE(CoveSaveCodec::decode(bytes,context,*catalog,issue));EXPECT_EQ(issue.error,CoveSaveError::Encoding);
    EXPECT_EQ(adapter.calls,0u);
}

TEST_F(CoveSave, ExplicitSingleRootArchiveUsesItsLeastMemberKeyAndCanReturnToLegacyBytes){
    fragments();auto& build=bootstrap.builds.front();build.connections[0].enabled=true;build.connections[0].damage=0;recapture();
    physical.boatRoots={{id(10),physical.boatMotion}};physical.playerRoot=id(10);
    const auto bytes=encode();ASSERT_EQ(bytes.at(4),std::byte{4});CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read);EXPECT_EQ(read->physical,physical);
    physical.boatRoots[0].key=id(20);physical.playerRoot=id(20);std::vector<std::byte> output;
    EXPECT_FALSE(CoveSaveCodec::encode(*checkpoint,nullptr,physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::PhysicalState); // A member ID is not the canonical root key.
    physical.boatRoots.clear();physical.controlPart={};physical.playerRoot={};
    const auto legacy=encode();ASSERT_EQ(legacy.at(4),std::byte{1});
    read=CoveSaveCodec::decode(legacy,context,*catalog,issue);ASSERT_TRUE(read);EXPECT_EQ(read->physical,physical);
}

TEST_F(CoveSave, FragmentArchiveSurvivesFreshWriterRecoveryWithItsExactRetiredParent){
    fragments();const auto bytes=encode();CoveSaveIssue issue;
    auto read=CoveSaveCodec::decode(bytes,context,*catalog,issue);ASSERT_TRUE(read);
    session.reset();UnusedAdapter replacement;RecoveryIssue recoveryIssue;
    EventStreamIncarnation incarnation;incarnation.bytes[0]=7;
    auto recovered=SessionRecovery::restore(read->current,incarnation,replacement,recoveryIssue);ASSERT_TRUE(recovered);
    std::vector<std::byte> saved;
    ASSERT_TRUE(CoveSaveCodec::encode(*recovered->initial,recovered->retired.get(),physical,context,*catalog,saved,issue));
    read=CoveSaveCodec::decode(saved,context,*catalog,issue);ASSERT_TRUE(read)<<int(issue.error);ASSERT_TRUE(read->retiredParent);
    EXPECT_EQ(read->physical,physical);EXPECT_EQ(read->current->snapshot().accepted.builds.front().parts,bootstrap.builds.front().parts);
    EXPECT_EQ(read->current->snapshot().accepted.inventory,bootstrap.inventory);EXPECT_FALSE(read->retiredParent->snapshot().admission.open);
    EXPECT_EQ(replacement.calls,0u);
    std::vector<std::byte> output{std::byte{42}};
    EXPECT_FALSE(CoveSaveCodec::encode(*recovered->initial,checkpoint.get(),physical,context,*catalog,output,issue));
    EXPECT_EQ(issue.error,CoveSaveError::Lineage);EXPECT_EQ(output,(std::vector<std::byte>{std::byte{42}}));
}
} // namespace
} // namespace voxy::game::expedition

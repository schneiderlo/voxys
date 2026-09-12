#include "game/expedition/session_recovery.hpp"
#include <cstdio>
#include <limits>

#define REQUIRE(value) do { if (!(value)) { std::printf("failure line=%d case=%u\n", __LINE__, caseIndex); return 1; } } while (false)

int main() {
    using namespace voxy::game::construction;
    using namespace voxy::game::expedition;
    class Adapter final : public PreparationAdapter {
    public:
        size_t begins=0,activations=0;
        PreparationResult begin(const PreparationRequest& request) override {++begins;return {request.ticket,PreparationState::Ready};}
        PreparationResult poll(PreparationTicket ticket) noexcept override {return {ticket,PreparationState::Ready};}
        bool canActivate(PreparationTicket) const noexcept override {return true;}
        void activate(PreparationTicket,SimulationTick) noexcept override {++activations;}
        void discard(PreparationTicket) noexcept override {}
    };
    const WorldNamespace world{{'i','n','v','e','r','s','e','-','c','r','e','d','i','t','-','1'}};
    const auto id=[&](uint64_t n){return DurableId{world,n};};
    CatalogIssue catalogIssue;auto catalog=PartCatalog::create(makeStarterCatalogDraft(),catalogIssue);
    unsigned caseIndex=0;REQUIRE(catalog);
    const auto key=starterPartKey(StarterPart::Engine);
    const auto& definition=*catalog->lookup(key).definition;
    size_t rejectedOverflows=0,admittedCheckpoints=0;
    for(caseIndex=0;caseIndex<3;++caseIndex) {
        const bool redo=caseIndex>=2, machinery=(caseIndex%2)!=0;
        const auto amount=redo?definition.salvageYield:definition.cost;
        const auto selected=machinery?amount.specialMachinery:amount.salvageMaterial;
        REQUIRE(selected>0);
        Adapter adapter;SessionBootstrap boot;boot.world=world;boot.caller={id(1),id(2)};
        boot.lastIssuedId=100;boot.workshopEnabled=true;boot.inventory={10000,10000};
        auto& bounded=machinery?boot.inventory.specialMachinery:boot.inventory.salvageMaterial;
        bounded=std::numeric_limits<uint64_t>::max()-(redo?selected:0);
        BuildSnapshot build;build.id=id(3);build.owner=id(1);
        if(redo) {
            PartInstance part;part.id=id(10);part.owningBuild=build.id;part.definition=key;
            part.settings=defaultModuleSettings(definition);build.parts={part};
        }
        boot.builds={build};boot.jobs={{id(5),1,JobPhase::Accepted,id(1)}};
        const ResourceAmounts reward=machinery?ResourceAmounts{0,selected}:ResourceAmounts{selected,0};
        boot.cargoDefinitions={{{id(1000),1},500,.3,reward,CargoRecoveryRule::PreserveUnique}};
        boot.cargo={{id(6),{id(1000),1},id(1),{10,2,-30},{},id(5)}};
        SessionIssue issue;auto session=GameSession::create(boot,EventStreamIncarnation{{1}},*catalog,adapter,issue);
        REQUIRE(session);
        const auto command=[&](Intent intent){return Command{AuthorityEpoch{1},RequestSequence{session->admissionState().admittedThrough.value()+1},session->snapshot().revision,std::move(intent)};};
        const auto commit=[&](Command value){return session->submit(boot.caller,value).state==ReceiptState::PendingPreparation
            &&session->advanceOneTick()&&session->receipt(boot.caller,value.sequence).state==ReceiptState::Committed;};
        if(redo) {
            REQUIRE(commit(command(RemovePart{{id(3),{}},id(10)})));
            const auto h=session->history();REQUIRE(h.undo);
            REQUIRE(commit(command(Undo{h.undo->target,h.undo->entry,h.generation})));
        } else REQUIRE(commit(command(AddPart{{id(3),{}},key,{}})));
        REQUIRE(commit(command(DeliverCargo{id(6)})));
        const auto before=session->snapshot();const auto history=session->history();
        const auto begins=adapter.begins,activations=adapter.activations;const auto issued=session->lastIssuedId();
        std::vector<std::byte> bytesBefore,bytesAfter;
        REQUIRE(!encodeBuild(before.builds.front(),*catalog,bytesBefore));
        const auto choice=redo?history.redo:history.undo;REQUIRE(choice);
        const auto inverse=redo?command(Redo{choice->target,choice->entry,history.generation}):command(Undo{choice->target,choice->entry,history.generation});
        const auto refusal=session->submit(boot.caller,inverse);
        REQUIRE(refusal.state==ReceiptState::Rejected&&refusal.issue.error==SessionError::ResourceOverflow);
        REQUIRE(session->submit(boot.caller,inverse).issue.error==SessionError::ResourceOverflow);
        const auto after=session->snapshot();REQUIRE(!encodeBuild(after.builds.front(),*catalog,bytesAfter));
        REQUIRE(after.revision==before.revision&&after.tick==before.tick&&after.inventory==before.inventory
            &&after.jobs==before.jobs&&after.cargo==before.cargo&&after.storedParts==before.storedParts
            &&bytesAfter==bytesBefore&&session->history()==history&&session->lastIssuedId()==issued
            &&adapter.begins==begins&&adapter.activations==activations&&!session->journalFaulted());
        ++rejectedOverflows;
        RecoveryContentIdentity content;content.manifest={id(9000),1};content.manifestDigest.fill(std::byte{0x51});
        RecoveryIssue recoveryIssue;auto snapshot=SessionRecovery::capture(*session,content,recoveryIssue);REQUIRE(snapshot);
        auto accepted=SessionRecovery::admit(*snapshot,{world,content},*catalog,recoveryIssue);
        if(accepted)++admittedCheckpoints;
        std::printf("case=%u direction=%s currency=%s overflow_refusal_preserved=true checkpoint_admitted=%s recovery_error=%u\n",
            caseIndex,redo?"dismantle-redo":"purchase-undo",machinery?"machinery":"material",accepted?"true":"false",unsigned(recoveryIssue.error));
    }
    std::printf("overflow_cases=%zu checkpoints_admitted=%zu passed=%s\n",rejectedOverflows,admittedCheckpoints,
        rejectedOverflows==3&&admittedCheckpoints==3?"true":"false");
    return rejectedOverflows==3&&admittedCheckpoints==3?0:1;
}

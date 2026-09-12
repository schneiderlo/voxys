#include "game/expedition/cove_test_session.hpp"

#include <limits>
#include <new>

namespace voxy::game::expedition {

std::unique_ptr<CoveTestSession> CoveTestSession::prepare(
    std::span<const std::byte> rollback, const CoveSaveContext& context,
    const construction::PartCatalog& catalog, const CoveWorkshop& workshop, std::string& error) {
    const auto fail=[&](const char* why)->std::unique_ptr<CoveTestSession>{error=why;return {};};
    try {
        if(workshop.changed()&&!workshop.brickToolActive())
            return fail("Keep or cancel the current change before testing.");
        auto result=std::unique_ptr<CoveTestSession>(new CoveTestSession);
        CoveSaveIssue issue;
        result->archive_=CoveSaveCodec::decode(rollback,context,catalog,issue);
        if(!result->archive_)return fail("The original expedition could not be protected. Nothing was changed.");
        const auto& physical=result->physical();
        if(physical.player.aboard || !physical.additionalCargo.empty()
            || physical.cargoState==CoveSavedCargoState::Towed || physical.cargoState==CoveSavedCargoState::BrokenTow
            || physical.harborLift.mode!=CoveHarborLiftMode::Detached)
            return fail("Stand at the workshop and release every cable before testing.");
        result->blueprint_=workshop.blueprintBytes(error);
        if(result->blueprint_.empty())return {};
        result->draft_=workshop.design(); // Never include the next unused ghost.
        result->draftBoat_=CoveBoatAssembly::compile(result->draft_,error);
        if(!result->draftBoat_)return {};
        if(result->draftBoat_->roots().size()!=1 || !result->draftBoat_->primaryRoot().helm
            || !result->draftBoat_->primaryRoot().propeller)
            return fail("Testing needs one connected boat with one helm and one propeller.");
        if(result->draftBoat_->build().id.world==context.identity.world)
            return fail("The temporary identity namespace conflicts with this expedition.");
        SaveCodecIssue saveIssue;
        if(!SessionSaveCodec::encodeCheckpoint(*result->archive_->current,result->canonical_,saveIssue))
            return fail("The original ownership state could not be protected.");
        result->rollback_.assign(rollback.begin(),rollback.end());
        result->context_=context;result->catalog_=&catalog;
        error.clear();return result;
    } catch(const std::bad_alloc&) {return fail("Not enough memory to protect the original expedition.");}
}

bool CoveTestSession::canonicalUnchanged(const GameSession& session,std::string& error) const {
    try {
        RecoveryIssue issue;
        const auto image=SessionRecovery::capture(session,context_.identity.content,issue);
        const auto admitted=image?SessionRecovery::admit(*image,context_.identity,*catalog_,issue):nullptr;
        SaveCodecIssue saveIssue;std::vector<std::byte> bytes;
        if(!admitted || !SessionSaveCodec::encodeCheckpoint(*admitted,bytes,saveIssue) || bytes!=canonical_) {
            error="The protected expedition changed during testing. Return was stopped.";return false;
        }
        error.clear();return true;
    } catch(const std::bad_alloc&) {error="Not enough memory to verify the protected expedition.";return false;}
}

bool CoveTestSession::stage(uint64_t incarnation,uint64_t tick,bool returning) noexcept {
    if(!incarnation || tick==std::numeric_limits<uint64_t>::max())return false;
    if(returning) {
        if(phase_!=Phase::Running || incarnation!=incarnation_ || tick<=enteredTick_)return false;
        phase_=Phase::Returning;
    } else {
        if(phase_!=Phase::Prepared || tick<=physical().tick.value())return false;
        incarnation_=incarnation;phase_=Phase::Entering;
    }
    executionTick_=tick;return true;
}

bool CoveTestSession::confirm(uint64_t incarnation,uint64_t tick,const GameSession& session,std::string& error) {
    if((phase_!=Phase::Entering&&phase_!=Phase::Returning) || incarnation!=incarnation_ || tick<executionTick_) {
        error="The test replacement has not completed on its owning physics timeline.";return false;
    }
    if(!canonicalUnchanged(session,error))return false;
    if(phase_==Phase::Entering){enteredTick_=executionTick_;phase_=Phase::Running;}
    else phase_=Phase::Complete;
    return true;
}

} // namespace voxy::game::expedition

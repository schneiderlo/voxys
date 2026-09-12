#include "game/expedition/cove_restore.hpp"
#include "game/expedition/cove_harbor_lift.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <new>

namespace voxy::game::expedition {
namespace {
using namespace construction;
physics::AuthoredRootMotion motion(const CoveSavedMotion& value){
    const auto& p=value.position;const auto& q=value.orientation;
    return {physics::worldPositionFromAbsolute({p.x,p.y,p.z}),{q.w,q.x,q.y,q.z},
        {value.originVelocity[0],value.originVelocity[1],value.originVelocity[2]},
        {value.angularVelocity[0],value.angularVelocity[1],value.angularVelocity[2]}};
}
}
std::unique_ptr<CoveRestoreCandidate> CoveRestoreCandidate::prepare(std::span<const std::byte> bytes,
    const CoveSaveContext& context,const assets::LoadedAssetFixture& installed,const PartCatalog& catalog,
    std::span<const CoveBoatAssembly::Part> originalBindings,CovePlayer::Ground ground,std::string& error,const StarterKit* installedStarter,CovePlayer::GroundSupport support){
    const auto fail=[&](const char* reason)->std::unique_ptr<CoveRestoreCandidate>{error=reason;return {};};
    try {
        auto result=std::make_unique<CoveRestoreCandidate>();CoveSaveIssue issue;
        result->archive=CoveSaveCodec::decode(bytes,context,catalog,issue);
        if(!result->archive)return fail("Expedition archive is damaged or incompatible.");
        if(!result->archive->physical.additionalCargo.empty())
            return fail("This runtime cannot restore both mission loads yet.");
        if(!installed.registry.navigation || installed.registry.navigation->cargoPlacements.size()!=1)
            return fail("The saved cove requires one independent salvage load.");
        const auto& physical=result->archive->physical;
        const auto& build=result->archive->current->snapshot().accepted.builds.front();
        auto bindings=std::vector<CoveBoatAssembly::Part>(originalBindings.begin(),originalBindings.end());
        const auto& kits=result->archive->current->snapshot().accepted.starterKits;
        if(!kits.empty()) {
            if(!installedStarter||kits.size()!=1)return fail("Saved starter policy is unavailable in this cove.");
            auto mapped=bindCoveStarterKit(*installedStarter,&kits.front(),originalBindings,error);
            if(!mapped)return {};
            bindings=std::move(*mapped);
        }
        auto design=prepareCoveExpandedLaunchDesign(installed,installed,build,bindings,{},error,
            physical.boatRoots.empty()?CoveSceneTopology::Welded:CoveSceneTopology::AcceptedRoots);
        if(!design)return {};
        result->boat=physical.boatRoots.empty()?CoveBoatAssembly::compileBuild(build,catalog,design->placements,error)
            :CoveBoatAssembly::compileFragments(build,catalog,design->placements,physical.controlPart,error);
        if(!result->boat)return {};
        result->roots=CoveRigidRoots::prepare(*result->boat,error);if(!result->roots)return {};
        physics::AuthoredFrameError frameError;
        for(size_t i=0;i<result->roots->roots().size();++i) {
            auto& root=result->roots->roots()[i];
            if(!physical.boatRoots.empty()&&(i>=physical.boatRoots.size()||physical.boatRoots[i].key!=root.key))
                return fail("Saved machine sections do not match the accepted build.");
            root.observed=motion(physical.boatRoots.empty()?physical.boatMotion:physical.boatRoots[i].motion);
            if(!physics::AuthoredBodyFrame(result->boat->roots()[i].shape).bodyMotion(root.observed,frameError))
                return fail("A saved section's motion cannot be represented by this craft.");
        }
        result->boatMotion=result->roots->primary().observed;
        result->scene=std::make_unique<const assets::LoadedAssetFixture>(std::move(design->scene));
        result->cargo=CoveBoatAssembly::compileCargo(installed,installed.registry.navigation->cargoPlacements.front(),error);
        if(!result->cargo)return {};
        const auto& cargoDefinition=context.cargoDefinition;
        if(result->cargo->build().parts.front().definition!=cargoDefinition.key
            ||result->cargo->assembly().mass().roots().front().mass.dryMassKg!=cargoDefinition.massKg
            ||result->cargo->displacementCubicMetres()!=cargoDefinition.displacedVolumeCubicMetres)
            return fail("Saved cargo does not match installed cargo mass or displacement.");
        const auto& functions=result->boat->assembly().functions();
        size_t helms=0,propellers=0,winches=0;bool line=false,eye=false;
        for(size_t i=0;i<functions.modules().size();++i){
            const auto& module=functions.modules()[i];
            if(std::holds_alternative<HelmModule>(module.parameters))++helms;
            if(std::holds_alternative<PropellerModule>(module.parameters))++propellers;
            if(const auto* winch=std::get_if<WinchModule>(&module.parameters)){
                ++winches;result->hasWinch=module.settings.enabled;
                const auto root=result->boat->rootForPart(module.part);if(!root)return fail("Saved winch has no physical section.");
                result->towRoot=result->boat->assembly().mass().roots()[*root].key;
                result->tow.minimumLength=static_cast<float>(winch->minimumLengthMetres);
                result->tow.maximumLength=static_cast<float>(winch->maximumLengthMetres);
                result->tow.maximumForce=static_cast<float>(winch->maximumForceNewtons);
                result->reelSpeed=static_cast<float>(winch->reelSpeedMetresPerSecond)*coveModuleOutput(module);
                for(const auto& frame:functions.moduleFrames(i))if(frame.kind==AssemblyFrameKind::TowLine){
                    if(line || frame.socket>=functions.sockets().size())return fail("Ambiguous saved winch anchor.");
                    const auto p=frame.rootFromFrame.translation;result->towBoatPoint=glm::vec3(p.x,p.y,p.z)*.02f;line=true;
                    result->tow.breakForce=static_cast<float>(functions.sockets()[frame.socket].definition.strength.tensionNewtons);
                }
            }
        }
        if(helms!=1 || propellers!=1 || winches>1)return fail("This cove needs one helm, one propeller and at most one winch.");
        const auto& cargoFunctions=result->cargo->assembly().functions();
        for(const auto& frame:cargoFunctions.frames())if(frame.kind==AssemblyFrameKind::TowEye){
            if(eye || frame.socket>=cargoFunctions.sockets().size())return fail("Ambiguous saved cargo tow eye.");
            const auto p=frame.rootFromFrame.translation;result->towCargoPoint=glm::vec3(p.x,p.y,p.z)*.02f;eye=true;
            result->tow.breakForce=std::min(result->tow.breakForce,
                static_cast<float>(cargoFunctions.sockets()[frame.socket].definition.strength.tensionNewtons));
        }
        if(!eye || (winches && !line))return fail("The saved boat or cargo has no usable tow anchor.");
        if(winches){
            const auto root=result->roots->indexForKey(result->towRoot);if(!root)return fail("Saved winch section is unavailable.");
            const auto a=physics::AuthoredBodyFrame(result->boat->roots()[*root].shape).bodyPoint(result->towBoatPoint,frameError);
            const auto b=physics::AuthoredBodyFrame(result->cargo->shape()).bodyPoint(result->towCargoPoint,frameError);
            if(!a || !b)return fail("Saved tow anchors cannot be represented in body coordinates.");
            result->tow.localAnchorA=*a;result->tow.localAnchorB=*b;
        }
        result->tow.targetLength=physical.ropeLength;result->tow.motorSpeed=0;
        result->cargoMotion=motion(physical.cargoMotion);
        if(!physics::AuthoredBodyFrame(result->cargo->shape()).bodyMotion(result->cargoMotion,frameError))
            return fail("Saved body motion cannot be represented by this craft.");
        if(physical.cargoState==CoveSavedCargoState::Banked)result->cargoMotionType=physics::AuthoredBodyMotionType::Static;
        result->player=std::make_unique<CovePlayer>();
        auto boatSlots=installed.registry.navigation->boatPlacements;
        for(size_t i=installed.registry.placements.size();i<result->scene->registry.placements.size();++i)
            boatSlots.push_back(static_cast<uint32_t>(i));
        if(!result->player->initialize(*result->scene,std::move(ground),error,boatSlots,std::move(support))
            ||!result->roots->bindPlayer(*result->player,*result->boat,{context.origin.x,context.origin.y,context.origin.z},error))return {};
        if(physical.harborLift.profile){
            auto structure=CoveHarborLift::prepareStructure(*installed.registry.navigation,error);
            if(!structure||!structure->applyPlayerCollision(*result->player))return fail("Cannot restore harbor collision.");
            if(physical.harborLift.mode!=CoveHarborLiftMode::Detached){
                auto rig=CoveHarborLift::prepare(*result->boat,*installed.registry.navigation,error);
                if(!rig||!rig->restoreLines(physical.harborLift,error))return {};
            }
        }
        // Cargo is an oriented member of the same scene packet, including
        // during nonactivating load validation. Harbor installation must not
        // retain an additional broad AABB that blocks empty beam corners.
        const auto cargoBounds=result->cargo->shape().rootBounds();
        const auto& minimum=cargoBounds.minimum;const auto& maximum=cargoBounds.maximum;
        const CovePlayer::SceneObstacle cargoObstacle{
            glm::dvec3(minimum.x,minimum.y,minimum.z)*.02,glm::dvec3(maximum.x,maximum.y,maximum.z)*.02,
            glm::translate(glm::dmat4(1),physics::worldPositionToAbsolute(result->cargoMotion.position)
                -glm::dvec3(context.origin.x,context.origin.y,context.origin.z))
                *glm::mat4_cast(glm::normalize(glm::dquat(result->cargoMotion.orientation)))};
        if(!result->player->setSceneObstacles(std::span(&cargoObstacle,1),result->player->collisionTick()))
            return fail("Saved cargo collision cannot join the restored craft.");
        const auto& saved=physical.player;CovePlayer::State state;
        state.feet={saved.feet.x,saved.feet.y,saved.feet.z};state.verticalSpeed=saved.verticalSpeed;
        state.tick=saved.tick;state.interactions=saved.interactions;state.onBoat=saved.aboard;
        state.locomotionVersion=physical.character.profile;
        state.worldVelocity={physical.character.worldVelocity[0],physical.character.worldVelocity[1],physical.character.worldVelocity[2]};
        state.facingYaw=physical.character.facingYaw;
        if(saved.aboard)state.root=physical.boatRoots.empty()?result->roots->primary().key:physical.playerRoot;
        switch(saved.mode){
        case CoveSavedPlayerMode::Walking:state.mode=CovePlayer::Mode::Walking;break;
        case CoveSavedPlayerMode::Airborne:state.mode=CovePlayer::Mode::Airborne;break;
        case CoveSavedPlayerMode::Swimming:state.mode=CovePlayer::Mode::Swimming;break;
        case CoveSavedPlayerMode::Helm:state.mode=CovePlayer::Mode::Helm;break;
        }
        if(!result->player->restore(state))return fail("Saved player position is blocked or unsupported.");
        error.clear();return result;
    }catch(const std::bad_alloc&){return fail("Not enough memory to prepare this expedition.");}
}
} // namespace voxy::game::expedition

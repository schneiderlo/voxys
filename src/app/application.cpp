// ═══════════════════════════════════════════════════════════════════════════════
// application.cpp - Main Application Shell Implementation
// ═══════════════════════════════════════════════════════════════════════════════

// NOTE: Include terrain/heightmap.hpp BEFORE any headers that might include X11
// because X11 defines "None" as a macro which conflicts with HeightmapError::None
#include "terrain/heightmap.hpp"

#include "app/application.hpp"
#include "app/salvage_preview_readback.hpp"
#include "engine/platform/window.hpp"
#include "engine/platform/input.hpp"

// Xlib exports `None` as a macro. Do not let that platform detail rewrite
// scoped Wreckwater enum values in headers included below.
#if defined(None)
    #undef None
#endif

#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/adventure_save.hpp"
#include "app/debug_overlay.hpp"
#include "render/cove_hud.hpp"
#include "render/cove_recovery_guidance.hpp"
#include "game/expedition/game_session.hpp"
#include "game/expedition/cove_player.hpp"
#include "game/expedition/cove_water_clock.hpp"
#include "game/expedition/cove_mechanisms.hpp"
#include "game/expedition/cove_effects.hpp"
#include "render/cove_effects_path.hpp"
#include "game/expedition/cove_camera.hpp"
#include "game/expedition/cove_character.hpp"
#include "game/assets/robot_asset.hpp"
#include "game/assets/cove_environment.hpp"
#include "game/expedition/cove_environment_collision.hpp"
#include "game/expedition/cove_boat.hpp"
#include "game/expedition/cove_rigid_roots.hpp"
#include "game/construction/assembly_fracture.hpp"
#include "game/expedition/cove_build.hpp"
#include "game/expedition/cove_workshop.hpp"
#include "game/expedition/cove_test_session.hpp"
#include "game/expedition/brick_paint.hpp"
#include "game/expedition/cove_save.hpp"
#include "game/expedition/cove_restore.hpp"
#include "app/cove_ui_state.hpp"
#include <json.hpp>
#include "game/expedition/cove_harbor_runtime.hpp"
#include "core/sha256.hpp"
#include <bit>
#include "physics/authored_body_frame.hpp"
#include "physics/authored_shape_resources.hpp"
#include "game/assets/fixture_registry.hpp"
#include "camera/camera.hpp"
#include "camera/controller.hpp"
#include "camera/character_controller.hpp"
#include "client/wreckwater_application_client.hpp"
#include "core/timer.hpp"
#include "perf/benchmark.hpp"
#include "core/log.hpp"
#include "core/timer.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "moto/race.hpp"
#include "moto/session.hpp"
#include "moto/world.hpp"
#include "physics/terrain_topology.hpp"
#include "terrain/lego_surface.hpp"
#include "terrain/lego_layout_cache.hpp"
#include "terrain/authored_cove.hpp"
#include "terrain/textures.hpp"
#include "terrain/shadow_bake.hpp"
#include "render/triangle_path.hpp"
#include "render/raycast_path.hpp"
#include "render/blit_path.hpp"
#include "render/water_simulation.hpp"
#include "render/primitive_path.hpp"
#include "render/primitive_culling.hpp"
#include "render/mesh_path.hpp"
#include "render/salvage_asset_fixture.hpp"
#include "render/cove_dock_markings.hpp"
#include "physics/physics_world.hpp"

#if !defined(VOXY_WASM)
    #include "network/native_tcp_transport.hpp"
#endif

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <numbers>
#include <random>
#include <span>
#include <system_error>
#include <vector>

#if defined(VOXY_NATIVE)
    #include <stb_image_write.h>
    #include "engine/platform/native/workshop_menu.hpp"
    #include "engine/platform/native/cove_preferences_store.hpp"
    // Platform-specific sleep includes for async polling
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <unistd.h>
    #endif
    // wgpu-native extras (not in the core WebGPU header)
    #ifndef WGPUWrappedSubmissionIndex
    using WGPUSubmissionIndex = uint64_t;
    typedef struct WGPUWrappedSubmissionIndex {
        WGPUQueue queue;
        WGPUSubmissionIndex submissionIndex;
    } WGPUWrappedSubmissionIndex;
    #endif
    extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(WGPUQueue queue, size_t commandCount, const WGPUCommandBuffer* commands);
    extern "C" WGPUBool wgpuDevicePoll(WGPUDevice device, WGPUBool wait, const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);
#endif

#if defined(VOXY_WASM)
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

namespace voxy {

struct WreckwaterApplicationClientState {
    client::WreckwaterApplicationClient client;
    client::WreckwaterCameraObstructionProbeRequest
        cameraProbe{};
    client::WreckwaterCameraTerrainCastResult
        terrainProbe{};
    bool cameraProbeSubmitted = false;
#if !defined(VOXY_WASM)
    network::NativeTcpClientTransport* transport = nullptr;
    network::NativeTcpClientState observedTransportState =
        network::NativeTcpClientState::Disconnected;
#endif
    client::WreckwaterGraphicalClientFrameStatus observedFrameStatus =
        client::WreckwaterGraphicalClientFrameStatus::NotInitialized;
    network::WreckwaterClientRuntimeError observedRuntimeError =
        network::WreckwaterClientRuntimeError::None;
};

namespace {

// Scene adapter stages physical delivery before execution. Its completion
// callback joins actual body/event evidence; activation publishes mappings only.
class CovePreparationAdapter final : public game::expedition::PreparationAdapter {
public:
    using Ticket=game::expedition::PreparationTicket;
    using State=game::expedition::PreparationState;
    using Delivery=game::expedition::CargoDeliveryTransition;
    struct Slot {Ticket ticket{};bool used=false,staged=false;std::optional<Delivery> delivery;game::construction::SimulationTick tick{};bool build=false;};
    std::array<Slot,2> slots{};
    std::function<bool(const Delivery&)> eligible;
    std::function<State(const Delivery&,game::construction::SimulationTick)> secure;
    std::function<bool(game::construction::SimulationTick)> observed;
    std::function<void()> publish;
    std::function<State(const game::expedition::PreparationRequest&)> prepareBuild;
    std::function<State()> pollBuild;
    std::function<State(game::construction::SimulationTick)> stageBuild;
    std::function<bool(game::construction::SimulationTick)> observedBuild;
    std::function<void()> publishBuild,discardBuild;
    game::expedition::PreparationResult begin(const game::expedition::PreparationRequest& request) override {
        if(request.removedBuild || (request.changedBuild && (!prepareBuild || request.delivery)) || (request.delivery && (!eligible || !eligible(*request.delivery))))
            return {request.ticket,State::Rejected};
        for(auto& slot:slots) if(!slot.used) {
            slot={request.ticket,true,false,request.delivery?std::optional<Delivery>{*request.delivery}:std::nullopt,{},request.changedBuild!=nullptr};
            return {request.ticket,slot.build?prepareBuild(request):State::Ready};
        }
        return {request.ticket,State::Rejected};
    }
    game::expedition::PreparationResult poll(Ticket ticket) noexcept override {
        for(const auto& slot:slots)if(slot.used && slot.ticket==ticket)
            return {ticket,slot.build?pollBuild():State::Ready};
        return {ticket,State::Rejected};
    }
    bool canActivate(Ticket ticket) const noexcept override {
        return std::any_of(slots.begin(),slots.end(),[&](const auto& slot){return slot.used && slot.ticket==ticket;});
    }
    State stage(Ticket ticket,game::construction::SimulationTick tick) noexcept override {
        for(auto& slot:slots) if(slot.used && slot.ticket==ticket && !slot.staged) {
            const auto result=slot.build?stageBuild(tick):slot.delivery?secure(*slot.delivery,tick):State::Ready;
            if(result==State::Ready) {slot.staged=true;slot.tick=tick;}
            return result;
        }
        return State::Rejected;
    }
    bool executionComplete(Ticket ticket,game::construction::SimulationTick tick) const noexcept override {
        for(const auto& slot:slots) if(slot.used && slot.ticket==ticket && slot.staged && slot.tick==tick)
            return slot.build?observedBuild(tick):!slot.delivery || observed(tick);
        return false;
    }
    void activate(Ticket ticket,game::construction::SimulationTick tick) noexcept override {
        for(auto& slot:slots) if(slot.used && slot.ticket==ticket && slot.staged && slot.tick==tick) {
            if(slot.build) publishBuild();
            else if(slot.delivery) publish();
            slot={};return;
        }
        std::terminate();
    }
    void discard(Ticket ticket) noexcept override {
        for(auto& slot:slots) if(slot.used && slot.ticket==ticket) {if(slot.build)discardBuild();slot={};}
    }
};

} // namespace

struct CoveResumeSource {
    std::array<uint8_t,16> world{};
    uint64_t tick=0;
    std::vector<std::byte> source,recovered;
    std::string recoveredDigest;
    bool awaitingCommit=true,staged=false,ready=false;
};

struct SalvageLocalSessionState {
    // Reverse destruction keeps both dependencies alive until the session dies.
    std::optional<game::construction::PartCatalog> catalog;
    CovePreparationAdapter preparation;
    game::expedition::CallerContext caller{};
    game::construction::DurableId cargoId{},jobId{},boatId{},starterEntitlement{};
    bool executionBound=false;
    bool storageRevoked=false,storageHostReady=false;
    std::optional<game::expedition::CoveSaveContext> saveContext;
    std::unique_ptr<game::expedition::ValidatedRecoveryCheckpoint> retiredSaveParent;
    std::optional<game::construction::RequestSequence> jobRequest,launchRequest;
    std::unique_ptr<game::expedition::GameSession> session;
    std::optional<game::expedition::SalvagePreview::Action> pendingControl;
    struct AssetPreview {
        std::unique_ptr<const game::assets::LoadedAssetFixture> content;
        render::SalvageAssetFixture fixture;
        glm::dvec3 origin{0};
        std::shared_ptr<std::atomic<bool>> deviceLost = std::make_shared<std::atomic<bool>>(false);
        render::SalvageFixtureStatus status = render::SalvageFixtureStatus::Uninitialized;
        bool viewsDirty = true;
        bool leaving = false;
        uint64_t forcedLod = 0;
        render::InspectionGuides guides = render::InspectionGuides::Off;
        std::vector<uint64_t> selectedLods;
        std::unique_ptr<game::expedition::CovePlayer> player;
        std::shared_ptr<const game::assets::RigidAnimationAsset> robot;
        game::expedition::CoveCharacter character;
        game::expedition::CoveCamera characterCamera;
        game::expedition::CoveCamera::Input characterCameraInput;
        bool characterDiscontinuity=true;
        uint64_t playerObservedTick=0;
        const game::expedition::CovePlayer* terrainBoundPlayer=nullptr;
        bool characterViewReady=false;
        // CPU-owned semantic presentation only: no submitted ticket, view,
        // bind group or GPU body handle survives in this snapshot.
        struct CharacterPresentation {
            bool valid=false,robotVisible=false;
            Camera camera;
            std::array<render::SalvageFixturePlacement,render::SalvageAssetFixture::maximumPlacements> placements{};
            size_t placementCount=0,solidCount=0,cableCount=0;
            game::assets::RigidAnimationPose robot;
            std::array<render::SalvageFixtureSolid,10> solids{};
            std::array<std::array<glm::vec3,2>,4> cables{};
            std::optional<std::array<glm::vec3,2>> tow;
            std::optional<glm::dmat4> dock;
            std::optional<glm::dmat4> scenery,gantry;
            uint32_t sceneryLod=0,gantryLod=0;
            glm::vec4 robotSurface{},environmentSurface{},ropeSurface{};
            uint64_t tick=0;
        } characterPresentation;
        std::unique_ptr<game::expedition::CoveBoatAssembly> boat;
        std::unique_ptr<game::expedition::CoveRigidRoots> boatRoots;
        auto& boatRoot() noexcept { return boatRoots->primary(); }
        const auto& boatRoot() const noexcept { return boatRoots->primary(); }
        size_t towRootIndex=0;
        auto& towRoot() noexcept { return boatRoots->roots()[towRootIndex]; }
        const auto& towRoot() const noexcept { return boatRoots->roots()[towRootIndex]; }
        std::unique_ptr<game::expedition::CoveWorkshop> workshop;
        std::unique_ptr<const game::assets::LoadedAssetFixture> liveScene;
        std::vector<game::expedition::CoveBoatAssembly::Part> initialBindings;
        game::expedition::StarterKit initialStarterKit;
        std::vector<std::byte> initialRecoveryDesign;
        game::expedition::CoveRecoveryDesigns recoveryDesigns;
        size_t selectedRecoveryDesign=0;
        const game::assets::LoadedAssetFixture& acceptedScene() const noexcept { return liveScene?*liveScene:*content; }
        struct Launch {
            enum class Purpose { Expedition, TestEnter, TestReturn };
            Purpose purpose=Purpose::Expedition;
            std::unique_ptr<const game::assets::LoadedAssetFixture> scene;
            std::unique_ptr<game::expedition::CoveBoatAssembly> boat;
            std::unique_ptr<game::expedition::CoveRigidRoots> roots;
            std::vector<physics::AuthoredShape> preparedShapes;
            std::optional<game::construction::AssemblyFracturePlan> fracture;
            std::unique_ptr<game::expedition::CovePlayer> player;
            std::unique_ptr<game::expedition::CoveWorkshop> workshop;
            std::vector<uint32_t> boatSlots;
            std::vector<uint64_t> lods;
            game::expedition::CoveRecoveryDesigns recoveryDesigns;
            size_t selectedRecoveryDesign=0;
            physics::AuthoredWaterBodyDesc water;
            physics::DistanceAttachmentDesc tow;
            glm::vec3 towPoint{};
            size_t towRootIndex=0;
            float reelSpeed=0;
            struct Parent {
                physics::ShapeHandle shape{};
                physics::BodyHandle body{};
                uint64_t deadTick=0;
            };
            std::array<Parent,game::expedition::CoveRigidRoots::maximumRoots> parents{};
            size_t parentCount=0;
            uint64_t executionTick=0;
            bool staged=false,published=false,canceled=false,retired=false,ownershipService=false;
        };
        std::unique_ptr<Launch> launch;
        enum class PracticePhase { None, Capturing, Entering, Running, Returning };
        PracticePhase practicePhase=PracticePhase::None;
        std::string practiceMessage;
        uint64_t practiceRuns=0,practiceReturns=0;
        struct Practice {
            std::unique_ptr<game::expedition::CoveTestSession> proof;
            std::unique_ptr<Launch> original;
            game::expedition::CoveCamera camera;
            game::expedition::CoveCharacter character;
            game::expedition::WorkshopCamera workshopCamera;
        };
        std::unique_ptr<Practice> practice;
        std::string launchMessage;
        uint64_t launchCount=0,lastLaunchTick=0;
        bool workshopOpen=false;
        enum class Pause { Running, Requested, Draining, Paused };
        Pause pause=Pause::Running;
        uint64_t pauseTick=0;
        double waterTime=0,pauseWaitSeconds=0;
        game::expedition::CoveWaterClock waterClock;
        game::expedition::CoveMechanisms mechanisms;
        game::expedition::CoveEffects effects;
        render::CoveEffectsPath effectsPath;
        struct EffectsDrive {
            uint64_t epoch=0,tick=0;
            physics::BodyHandle body{};
            double drive=0;
            float waterPhase=0;
        };
        std::array<EffectsDrive,32> effectsDrives{};
        std::array<game::expedition::CoveEffects::Impact,game::expedition::CoveEffects::maximumImpacts> effectsImpacts{};
        size_t effectsImpactCount=0;
        uint64_t effectsImpactTick=0,effectsSkippedContacts=0,effectsBuildRevision=0;
        bool effectsRescuing=false;
        physics::BodyHandle rotorCommandBody{};
        double rotorCommandDrive=0;
        uint32_t mechanismPlacements=0;
        std::optional<render::CoveDockMarkings> dockMarkings;
        game::expedition::WorkshopCamera workshopCamera;
        int workshopFrameRequest=0;
        bool workshopFrameWhole=true;
        bool workshopPointerPlacement=false;
        bool workshopPointerTarget=false;
        bool controllerMenuOwner=false,menuConsumedFrame=false;
        bool controllerMotor=false;
#if defined(VOXY_NATIVE)
        std::optional<game::expedition::CoveDesignCost> menuQuote;
        uint64_t menuQuoteRevision=std::numeric_limits<uint64_t>::max();
#endif
        struct WorkshopView { glm::vec3 position; float yaw,pitch; glm::ivec3 sector; };
        std::optional<WorkshopView> workshopReturnView;
        std::unique_ptr<game::expedition::CoveSceneryCollision> scenery;
        physics::ShapeHandle sceneryShape{};
        physics::BodyHandle sceneryBody{};
        bool sceneryRetired=false;
        std::shared_ptr<const game::assets::CoveEnvironmentAsset> environment;
        std::unique_ptr<game::expedition::CoveEnvironmentCollision> environmentCollision;
        std::array<game::expedition::CovePlayer::StaticObstacle,game::assets::kCoveEnvironmentMaximumCollisionBoxes> environmentObstacles{};
        size_t environmentObstacleCount=0;
        auto environmentObstacleSpan() const noexcept { return std::span(environmentObstacles).first(environmentObstacleCount); }
        physics::ShapeHandle environmentShape{};
        physics::BodyHandle environmentBody{};
        uint64_t environmentAdmissionTick=0,environmentRemovalTick=0;
        bool environmentRetired=true,environmentObserved=false;
        struct SurfaceState { game::construction::DurableId part{}; float wet=0; };
        std::array<SurfaceState,render::SalvageAssetFixture::maximumPlacements> surfaceStates{};
        uint64_t surfaceTick=0;
        float robotWet=0;
        std::unique_ptr<game::expedition::CoveBoatAssembly> cargo;
        physics::ShapeHandle cargoShape{};
        physics::BodyHandle cargoBody{};
        physics::AuthoredRootMotion cargoSpawn{},cargoObserved{};
        uint64_t cargoObservedTick=0;
        bool cargoRetired=false;
        bool cargoBanked=false,cargoSecuring=false;
        bool checkpointPending=false,deliveryDurable=false,workshopSavePending=false;
        // Rescue reuses canonical ownership and physical bodies. Each phase
        // crosses a joined neutral tick; no relocation can pull a live cable.
        enum class Rescue { None, Requested, Releasing, Moving, Saving };
        Rescue rescue=Rescue::None;
        physics::AttachmentHandle rescueRope{};
        uint64_t rescueRopeTick=0,rescues=0;
        std::unique_ptr<game::expedition::CoveHarborRuntime> harbor;
        bool harborKeyboardMotor=false;
        std::string checkpointDigest;
        std::optional<game::expedition::CovePhysicalSave> restorePhysical;
        physics::BodyHandle cargoReplacedBody{};
        uint64_t cargoReplacementDeadTick=0;
        physics::AuthoredRootMotion cargoDeliveredPose{};
        std::string jobMessage;
        physics::AttachmentHandle towRope{};
        physics::DistanceAttachmentDesc towDesc{};
        physics::DebugAttachmentState towObserved{};
        uint64_t towObservedTick=0;
        glm::vec3 towBoatPoint{},towCargoPoint{};
        float towReelSpeed=0,towMotor=0;
        float thrustLimit=0,steeringLimit=0;glm::vec3 thrustDirection{};
        uint64_t towChangedTick=0;
        int towAction=0;
        bool towBroken=false;
        bool boatObservationPending=false;
        uint64_t boatEventsThrough=0, boatAttachmentBreaks=0;
        std::string playerPrompt;
        std::string cutLabel(const game::expedition::CoveRigidRoots::CutTarget& target) const {
            const auto name=[&](game::construction::DurableId part)->std::string_view {
                const auto* module=boat->assembly().functions().module(part);
                using namespace game::construction;
                if(!module)return "Frame";
                if(std::holds_alternative<FlotationModule>(module->parameters))return "Pontoon";
                if(std::holds_alternative<HelmModule>(module->parameters))return "Helm";
                if(std::holds_alternative<WinchModule>(module->parameters))return "Winch";
                if(std::holds_alternative<PropellerModule>(module->parameters))return "Propeller";
                if(std::holds_alternative<EngineModule>(module->parameters))return "Engine";
                if(std::holds_alternative<CargoCradleModule>(module->parameters))return "Cargo cradle";
                return "Frame";
            };
            return std::string(name(target.partA))+" / "+std::string(name(target.partB));
        }

        std::optional<game::expedition::CoveRigidRoots::CutTarget> cutTarget(
            std::optional<game::construction::DurableId> exact={}) const noexcept {
            if(!player||!boat||!boatRoots||leaving||workshopOpen||checkpointPending||pause!=Pause::Running
                ||practicePhase!=PracticePhase::None
                ||rescue!=Rescue::None||cargoSecuring||towRope.valid()||towAction
                ||(harbor&&(harbor->hasRopes()||harbor->busy()))
                ||(player->mode()!=game::expedition::CovePlayer::Mode::Walking
                    &&player->mode()!=game::expedition::CovePlayer::Mode::Helm))return {};
            return boatRoots->reachableWeld(*boat,origin+player->feet()+glm::dvec3(0,.9,0),exact);
        }

    };
    std::unique_ptr<AssetPreview> asset;
    // Adapter cancellation still needs AssetPreview during session teardown.
    ~SalvageLocalSessionState() { if(session)session->closeAdmission(); }
};

namespace {
void countCoveEffectSkip(uint64_t& counter,uint64_t count=1) noexcept {
    counter+=std::min(count,std::numeric_limits<uint64_t>::max()-counter);
}

bool coveEffectsRunning(const SalvageLocalSessionState& local) noexcept {
    const auto& asset=*local.asset;
    return !asset.leaving&&!asset.workshopOpen&&!asset.checkpointPending
        &&asset.pause==SalvageLocalSessionState::AssetPreview::Pause::Running
        &&asset.rescue==SalvageLocalSessionState::AssetPreview::Rescue::None
        &&!local.storageRevoked&&!local.pendingControl&&!local.launchRequest;
}

// This publication runs after the player and camera have accepted the same
// root/cargo packet. Earlier updateCoveBoat only pairs copied contact facts.
bool observeCoveEffects(SalvageLocalSessionState& local,const render::WaterSimulation& water,
    uint64_t epoch,float baseWaterHeight,float waveStrength,float currentPhase) {
    using Effects=game::expedition::CoveEffects;
    auto& asset=*local.asset;const auto tick=asset.characterPresentation.tick;
    if(!asset.effectsPath.initialized()||!asset.player||!asset.boatRoots||!asset.characterViewReady
        ||!asset.characterPresentation.valid||tick!=asset.player->collisionTick()
        ||tick!=asset.boatRoots->joinedTick()||(asset.cargo&&asset.cargoObservedTick!=tick)
        ||asset.boatEventsThrough<tick)return true;
    if(asset.effects.stats().epoch!=epoch)return true; // updateCoveBoat binds/reset this owner first.
    if(tick<=asset.effects.stats().tick)return true;
    const auto& drive=asset.effectsDrives[tick%asset.effectsDrives.size()];
    const bool driveKnown=drive.epoch==epoch&&drive.tick==tick;
    const float phase=driveKnown?drive.waterPhase:currentPhase;
    const auto height=[&](glm::dvec3 point){
        const auto absolute=asset.origin+point;
        return double(baseWaterHeight)+double(water.sampleSurface(glm::vec2(absolute.x,absolute.z),phase,waveStrength).heightOffset)-asset.origin.y;
    };
    std::array<Effects::MotionSource,Effects::maximumSources> sources{};size_t count=0;
    const auto add=[&](Effects::SourceId id,const physics::AuthoredShape& shape,
        const physics::AuthoredRootMotion& motion,const game::expedition::CoveBoatAssembly::Root* boatRoot){
        if(count==sources.size())return false;
        auto& source=sources[count++];source.id=id;
        const auto bounds=shape.rootBounds();
        const auto a=glm::dvec3(bounds.minimum.x,bounds.minimum.y,bounds.minimum.z)*.02;
        const auto b=glm::dvec3(bounds.maximum.x,bounds.maximum.y,bounds.maximum.z)*.02;
        const auto origin=physics::worldPositionToAbsolute(motion.position)-asset.origin;
        const auto rotation=glm::normalize(glm::dquat(motion.orientation));
        const auto point=glm::dvec3((a.x+b.x)*.5,a.y,(a.z+b.z)*.5);
        const auto offset=rotation*point;source.point=origin+offset;
        source.pointVelocity=glm::dvec3(motion.originVelocity)+glm::cross(glm::dvec3(motion.angularVelocity),offset);
        source.footprintRadius=std::clamp(glm::length(glm::dvec2(b.x-a.x,b.z-a.z))*.25,.15,16.);
        source.waterHeight=height(source.point);
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(unsigned corner=0;corner<8;++corner){
            const auto p=origin+rotation*glm::dvec3((corner&1)?b.x:a.x,(corner&2)?b.y:a.y,(corner&4)?b.z:a.z);
            low=std::min(low,p.y);high=std::max(high,p.y);
        }
        // Cosmetic surface crossing of the physical envelope. This bounded
        // CPU wave approximation is not an authoritative submerged volume.
        source.waterContact=source.waterHeight>=low-.03&&source.waterHeight<=high+.03;
        if(boatRoot) {
            source.hasPropeller=boatRoot->propeller.has_value()&&boatRoot->maximumThrustNewtons>0;
            if(boatRoot->cells.empty())source.waterContact=false;
            if(source.hasPropeller){
                const auto propellerOffset=rotation*glm::dvec3(boatRoot->propellerPoint);
                source.propellerPoint=origin+propellerOffset;
                source.propellerVelocity=glm::dvec3(motion.originVelocity)
                    +glm::cross(glm::dvec3(motion.angularVelocity),propellerOffset);
                source.propellerWaterHeight=height(source.propellerPoint);
                source.effectiveDrive=driveKnown&&drive.body==id.body?drive.drive:0;
            }
        }
        return true;
    };
    for(size_t i=0;i<asset.boatRoots->roots().size();++i){
        const auto& owned=asset.boatRoots->roots()[i];const auto& root=asset.boat->roots()[i];
        if(owned.observedTick!=tick||!owned.body.valid())return true;
        if(!add({owned.key,owned.body,Effects::SourceKind::Boat},root.shape,owned.observed,&root))return false;
    }
    if(asset.cargo&&asset.cargoBody.valid())
        if(!add({local.cargoId,asset.cargoBody,Effects::SourceKind::Cargo},asset.cargo->shape(),asset.cargoObserved,nullptr))return false;
    if(count==sources.size())return false;
    auto& player=sources[count++];player.id={local.caller.participant,{},Effects::SourceKind::Player};
    player.point=asset.player->feet();player.pointVelocity=asset.player->worldVelocity();
    player.waterHeight=height(player.point);player.footprintRadius=game::expedition::CovePlayer::radius;
    player.waterContact=asset.player->mode()==game::expedition::CovePlayer::Mode::Swimming;
    std::span<const Effects::Impact> impacts;
    if(asset.effectsImpactTick==tick)impacts={asset.effectsImpacts.data(),asset.effectsImpactCount};
    else if(asset.effectsImpactCount)countCoveEffectSkip(asset.effectsSkippedContacts,asset.effectsImpactCount);
    const bool accepted=asset.effects.observe({epoch,tick,coveEffectsRunning(local),std::span(sources).first(count),impacts});
    asset.effectsImpactCount=0;asset.effectsImpactTick=0;
    return accepted;
}

struct RenderFrameGuard {
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer command = nullptr;
    render::SalvageAssetFixture* fixture = nullptr;
    render::SalvageFixtureTicket ticket{};
    render::BlitPath* blit = nullptr;
    render::RaycastPath* raycast = nullptr;
    physics::PhysicsWorld* physicsWorld = nullptr;
    physics::ShapeResourceSubmission physicsTicket{};
    render::WaterSimulation* water = nullptr;
    double* waterTime = nullptr;
    double previousWaterTime = 0;
    game::expedition::CoveMechanisms* mechanisms = nullptr;
    game::expedition::CoveMechanisms previousMechanisms;
    bool submitted = false;
    ~RenderFrameGuard() {
        if (command) wgpuCommandBufferRelease(command);
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (!submitted) {
            if (waterTime) *waterTime = previousWaterTime;
            if (mechanisms) *mechanisms = previousMechanisms;
            if (water) water->discardUpdate();
            if (blit) blit->discardEncoding();
            if (raycast) raycast->discardEncoding();
            if (physicsWorld && physicsTicket.valid())
                (void)physicsWorld->discardGpuSubmission(physicsTicket);
        }
        if (fixture && ticket.serial != 0) {
            try {
                std::string error;
                if (!fixture->discarded(ticket, error)) fixture->shutdown();
            } catch (...) { fixture->shutdown(); }
        }
    }
};
} // namespace

namespace {

enum RendererSettingsDirty : uint32_t {
    RendererUniformsDirty = 1u << 0u,
    RendererCameraDirty = 1u << 1u,
    RendererWaterPhysicsDirty = 1u << 2u,
    RendererWaterCoastDirty = 1u << 3u,
    RendererWaterSpectrumDirty = 1u << 4u,
    RendererSunShadowDirty = 1u << 5u,
};

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;
constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
constexpr uint32_t kPortableMaximumTextureDimension2D = 8'192u;
constexpr float kBrowserJourneyTargetX = 250.0f;
constexpr float kBrowserJourneyTargetZ = 0.0f;
constexpr float kCubeTriangleCubeSize = 1.1f;
constexpr float kCubeTriangleHorizontalPitch =
    kCubeTriangleCubeSize + 0.16f;
constexpr uint32_t kCubeTriangleImpactWakeRadiusColumns = 2u;
constexpr uint16_t kLocalMotoRacePlayer = 0u;

bool flattenCubeTriangleArena(
    terrain::Heightmap& heightmap, float cellScale) {
    if (!heightmap.isLoaded() || !std::isfinite(cellScale)
        || cellScale <= 0.0f) {
        return false;
    }

    // The 141-row wall is roughly 316 m wide. Flatten enough land to contain
    // its base and the starting camera, while sampling the target height only
    // near the wall so distant terrain cannot bias the platform elevation.
    constexpr float innerHalfWidth = 180.0f;
    constexpr float innerHalfDepth = 520.0f;
    constexpr float heightSampleHalfWidth = 160.0f;
    constexpr float heightSampleHalfDepth = 10.0f;
    constexpr float feather = 40.0f;
    const uint32_t width = heightmap.getWidth();
    const uint32_t height = heightmap.getHeight();
    const auto sampleCoordinate = [cellScale](
        float world, uint32_t sampleCount) {
        return static_cast<int32_t>(std::lround(
            world / cellScale
            + 0.5f * static_cast<float>(sampleCount - 1u)));
    };
    const int32_t centerX = sampleCoordinate(
        kBrowserJourneyTargetX, width);
    const int32_t centerZ = sampleCoordinate(
        kBrowserJourneyTargetZ, height);
    const int32_t outerSamplesX =
        static_cast<int32_t>(std::ceil(
            (innerHalfWidth + feather) / cellScale));
    const int32_t outerSamplesZ =
        static_cast<int32_t>(std::ceil(
            (innerHalfDepth + feather) / cellScale));

    const auto clampX = [width](int32_t value) {
        return std::clamp(value, 0, static_cast<int32_t>(width) - 1);
    };
    const auto clampZ = [height](int32_t value) {
        return std::clamp(value, 0, static_cast<int32_t>(height) - 1);
    };
    const int32_t heightSamplesX =
        static_cast<int32_t>(std::ceil(
            heightSampleHalfWidth / cellScale));
    const int32_t heightSamplesZ =
        static_cast<int32_t>(std::ceil(
            heightSampleHalfDepth / cellScale));
    const int32_t sampleMinX = clampX(centerX - heightSamplesX);
    const int32_t sampleMaxX = clampX(centerX + heightSamplesX);
    const int32_t sampleMinZ = clampZ(centerZ - heightSamplesZ);
    const int32_t sampleMaxZ = clampZ(centerZ + heightSamplesZ);

    uint64_t sum = 0u;
    uint64_t samples = 0u;
    const std::span<const uint16_t> source = heightmap.getData();
    for (int32_t z = sampleMinZ; z <= sampleMaxZ; ++z) {
        for (int32_t x = sampleMinX; x <= sampleMaxX; ++x) {
            sum += source[static_cast<size_t>(z) * width
                          + static_cast<uint32_t>(x)];
            ++samples;
        }
    }
    if (samples == 0u) return false;
    const uint16_t arenaHeight = static_cast<uint16_t>(
        std::min<uint64_t>((sum + samples / 2u) / samples, 65'535u));

    std::span<uint16_t> data = heightmap.getMutableData();
    const int32_t outerMinX = clampX(centerX - outerSamplesX);
    const int32_t outerMaxX = clampX(centerX + outerSamplesX);
    const int32_t outerMinZ = clampZ(centerZ - outerSamplesZ);
    const int32_t outerMaxZ = clampZ(centerZ + outerSamplesZ);
    for (int32_t z = outerMinZ; z <= outerMaxZ; ++z) {
        const float distanceZ =
            std::abs(static_cast<float>(z - centerZ)) * cellScale;
        const float edgeZ =
            (distanceZ - innerHalfDepth) / feather;
        for (int32_t x = outerMinX; x <= outerMaxX; ++x) {
            const float distanceX =
                std::abs(static_cast<float>(x - centerX)) * cellScale;
            const float edgeX =
                (distanceX - innerHalfWidth) / feather;
            const float edge = std::clamp(
                std::max(edgeX, edgeZ), 0.0f, 1.0f);
            const float smoothEdge = edge * edge * (3.0f - 2.0f * edge);
            const size_t index = static_cast<size_t>(z) * width
                               + static_cast<uint32_t>(x);
            data[index] = static_cast<uint16_t>(std::lround(
                static_cast<float>(arenaHeight) * (1.0f - smoothEdge)
                + static_cast<float>(data[index]) * smoothEdge));
        }
    }

    LOG_INFO(
        "Flattened cube-triangle arena: {:.0f}x{:.0f} m with {:.0f} m feather",
        innerHalfWidth * 2.0f, innerHalfDepth * 2.0f, feather);
    return true;
}

void setCameraWorldPose(
    Camera& camera, const glm::dvec3& absolutePosition,
    const glm::dvec3& absoluteTarget) {
    const physics::WorldPosition position =
        physics::worldPositionFromAbsolute(absolutePosition);
    const glm::dvec3 sectorOrigin =
        glm::dvec3(position.sector) *
        static_cast<double>(physics::kWorldSectorSize);
    camera.setWorldPosition(position.sector, position.local);
    camera.lookAt(glm::vec3(absoluteTarget - sectorOrigin));
}

[[nodiscard]] float finiteClamp(double value, float minimum, float maximum) {
    if (!std::isfinite(value)) return minimum;
    return std::clamp(static_cast<float>(value), minimum, maximum);
}

[[nodiscard]] float sunAzimuthDegrees(const glm::vec3& direction) {
    return std::atan2(direction.z, direction.x) * kRadiansToDegrees;
}

[[nodiscard]] float sunElevationDegrees(const glm::vec3& direction) {
    const glm::vec3 normalized = glm::dot(direction, direction) > 1.0e-10f
        ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
    return std::asin(std::clamp(normalized.y, -1.0f, 1.0f))
         * kRadiansToDegrees;
}

[[nodiscard]] glm::vec3 sunDirectionFromDegrees(float azimuth,
                                                float elevation) {
    const float azimuthRadians = azimuth * kDegreesToRadians;
    const float elevationRadians = elevation * kDegreesToRadians;
    const float horizontal = std::cos(elevationRadians);
    return glm::normalize(glm::vec3(
        horizontal * std::cos(azimuthRadians),
        std::sin(elevationRadians),
        horizontal * std::sin(azimuthRadians)));
}

[[nodiscard]] render::WaterSpectrumConfig makeWaterSpectrumConfig(
    const WaterSpectrumSettings& settings) {
    return render::WaterSpectrumConfig{
        .significantWaveHeight = settings.significantWaveHeight,
        .directionRadians = settings.directionDegrees * kDegreesToRadians,
        .choppiness = settings.choppiness,
        .peakEnhancement = settings.peakEnhancement,
        .windAlignment = settings.windAlignment,
        .animationSpeed = settings.animationSpeed,
        .patchLengths = settings.patchLengths,
        .cascadeAmplitudes = settings.cascadeAmplitudes,
        .directionalSineScale = settings.directionalSineScale,
    };
}

[[nodiscard]] WaterSpectrumSettings makeWaterSpectrumSettings(
    const render::WaterSpectrumConfig& config) {
    return WaterSpectrumSettings{
        .significantWaveHeight = config.significantWaveHeight,
        .directionDegrees = config.directionRadians * kRadiansToDegrees,
        .choppiness = config.choppiness,
        .peakEnhancement = config.peakEnhancement,
        .windAlignment = config.windAlignment,
        .animationSpeed = config.animationSpeed,
        .patchLengths = config.patchLengths,
        .cascadeAmplitudes = config.cascadeAmplitudes,
        .directionalSineScale = config.directionalSineScale,
    };
}

[[nodiscard]] bool finiteVector(const glm::vec2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool validWreckwaterApplicationClientConfig(
    const ApplicationConfig& application) noexcept {
    if (!application.wreckwaterClient) return true;
#if defined(VOXY_WASM)
    return false;
#else
    const auto& config = *application.wreckwaterClient;
    if (config.serverAddress.empty()
        || config.serverAddress.size() > 255u
        || config.serverPort == 0u
        || config.peerId == 0u || config.peerId > 4u
        || config.sessionId == 0u || config.matchId == 0u
        || config.worldId == 0u || config.worldEpoch == 0u
        || config.authorityEpoch == 0u
        || config.inputLeadTicks == 0u
        || config.inputLeadTicks > 16u
        || config.interpolationDelayTicks
            > client::
                kWreckwaterClientVisualClockMaximumInterpolationDelayTicks) {
        return false;
    }
    for (const char character : config.serverAddress) {
        const auto value =
            static_cast<unsigned char>(character);
        if (value <= 0x20u || value == 0x7fu) return false;
    }
    bool anyKeyByte = false;
    bool anyDigestByte = false;
    for (const std::byte value : config.authenticationKey) {
        anyKeyByte = anyKeyByte || value != std::byte{};
    }
    for (const std::byte value : config.expectedContentDigest) {
        anyDigestByte = anyDigestByte || value != std::byte{};
    }
    const bool automationDisabled =
        !application.benchmarkOnStartup
        && !application.exitAfterBenchmark
        && application.benchmarkBodyCount == 0u
        && application.cubePyramidBodyCount == 0u
        && !application.initialTeleportIndex
        && !application.screenshotPath
        && application.screenshotTourIndices.empty();
    return anyKeyByte && anyDigestByte && automationDisabled;
#endif
}

#if !defined(VOXY_WASM)
[[nodiscard]] WreckwaterApplicationConnectionState
wreckwaterApplicationConnectionState(
    network::NativeTcpClientState state) noexcept {
    switch (state) {
        case network::NativeTcpClientState::Disconnected:
        case network::NativeTcpClientState::Connecting:
            return WreckwaterApplicationConnectionState::Connecting;
        case network::NativeTcpClientState::Authenticating:
            return WreckwaterApplicationConnectionState::Authenticating;
        case network::NativeTcpClientState::Connected:
            return WreckwaterApplicationConnectionState::Connected;
        case network::NativeTcpClientState::Failed:
            return WreckwaterApplicationConnectionState::Failed;
    }
    return WreckwaterApplicationConnectionState::Failed;
}

[[nodiscard]] const char* nativeTcpClientStateName(
    network::NativeTcpClientState state) noexcept {
    switch (state) {
        case network::NativeTcpClientState::Disconnected:
            return "disconnected";
        case network::NativeTcpClientState::Connecting:
            return "connecting";
        case network::NativeTcpClientState::Authenticating:
            return "authenticating";
        case network::NativeTcpClientState::Connected:
            return "connected";
        case network::NativeTcpClientState::Failed:
            return "failed";
    }
    return "unknown";
}
#endif

[[nodiscard]] bool validApplicationConfig(
    const ApplicationConfig& config) noexcept {
    if(config.adventureEnabled&&(config.salvagePreviewEnabled||config.motoEnabled||config.wreckwaterClient
        ||!config.legoTerrainEnabled||config.renderPath!=RenderPath::Raycast))return false;
    const bool validRenderPath = config.renderPath == RenderPath::Triangle
                              || config.renderPath == RenderPath::Raycast;
    const bool validPhysicsBackend =
        config.physicsBackend == physics::BackendType::JoltLegacy
        || config.physicsBackend == physics::BackendType::Box3DReference
        || config.physicsBackend == physics::BackendType::WebGpuSoft;
    const bool validScheduler =
        config.joltJobSystem == physics::JoltJobSystemMode::SingleThreaded
        || config.joltJobSystem == physics::JoltJobSystemMode::ThreadPool;
    const auto& spectrum = config.waterSpectrum;
    const uint64_t heightSamples =
        static_cast<uint64_t>(config.heightmapWidth)
        * config.heightmapHeight;
    const float cellsPerSector =
        256.0f / config.gpuPhysicsBroadPhaseCellSize;
    const float roundedCellsPerSector = std::round(cellsPerSector);
    const double scaledWindowWidth =
        static_cast<double>(config.windowWidth)
        * static_cast<double>(config.resolutionScale);
    const double scaledWindowHeight =
        static_cast<double>(config.windowHeight)
        * static_cast<double>(config.resolutionScale);
    const bool validGpuCellSize =
        std::isfinite(config.gpuPhysicsBroadPhaseCellSize)
        && config.gpuPhysicsBroadPhaseCellSize > 0.0f
        && std::isfinite(cellsPerSector)
        && roundedCellsPerSector >= 1.0f
        && roundedCellsPerSector <= 2'097'152.0f
        && std::abs(cellsPerSector - roundedCellsPerSector) <= 1.0e-5f;
    constexpr uint32_t kCpuBenchmarkBodyCapacity = 16'384u;
    const uint32_t benchmarkBodyCapacity =
        config.physicsBackend == physics::BackendType::WebGpuSoft
        ? config.gpuPhysicsMaxBodies
        : kCpuBenchmarkBodyCapacity;
    const uint64_t startupBodyCount =
        uint64_t{config.benchmarkBodyCount}
        + config.cubePyramidBodyCount;
    const bool validSalvage = !config.salvagePreviewEnabled
        || (config.legoTerrainEnabled && !config.motoEnabled && !config.wreckwaterClient
            && config.physicsBackend == physics::BackendType::WebGpuSoft
            && !config.physicsCpuFallback && config.renderPath == RenderPath::Raycast
            && !config.initialTeleportIndex && !config.benchmarkOnStartup
            && !config.exitAfterBenchmark && config.benchmarkBodyCount == 0u
            && config.cubePyramidBodyCount == 0u && config.screenshotTourIndices.empty());
    const bool validAssetFixture = !config.salvageAssetFixtureRegistry
        || (config.salvagePreviewEnabled && !config.salvageAssetFixtureRegistry->empty()
            && config.salvageAssetFixtureRegistry->size() <= 4096
            && config.salvageAssetFixtureRegistry->find('\0') == std::string::npos);
    const bool validGuides = config.salvageAssetFixtureGuides == "off"
        || (config.salvageAssetFixtureRegistry && (config.salvageAssetFixtureGuides == "dimensions"
            || config.salvageAssetFixtureGuides == "sockets"));
    return validAssetFixture && validGuides && validSalvage && validRenderPath && validPhysicsBackend && validScheduler
        && validWreckwaterApplicationClientConfig(config)
        && config.windowWidth > 0 && config.windowHeight > 0
        && config.heightmapWidth > 0u && config.heightmapHeight > 0u
        && config.heightmapWidth <= kPortableMaximumTextureDimension2D
        && config.heightmapHeight <= kPortableMaximumTextureDimension2D
        && heightSamples
            <= std::numeric_limits<size_t>::max() / sizeof(uint16_t)
        && std::isfinite(config.resolutionScale)
        && config.resolutionScale >= 0.25f
        && config.resolutionScale <= 2.0f
        && scaledWindowWidth
            <= static_cast<double>(kPortableMaximumTextureDimension2D)
        && scaledWindowHeight
            <= static_cast<double>(kPortableMaximumTextureDimension2D)
        && std::isfinite(config.heightScale) && config.heightScale > 0.0f
        && config.heightScale <= 1.0e6f
        && std::isfinite(config.cellScale) && config.cellScale > 0.0f
        && config.cellScale <= 1.0e6f
        && finiteVector(config.sunDirection)
        && finiteVector(config.sunColor)
        && finiteVector(config.ambientColor)
        && std::isfinite(config.ambientIntensity)
        && std::isfinite(config.fogDensity)
        && finiteVector(config.fogColor)
        && std::isfinite(config.waterHeight)
        && finiteVector(config.waterShallowColor)
        && finiteVector(config.waterDeepColor)
        && std::isfinite(config.waterRoughness)
        && std::isfinite(config.waterWaveStrength)
        && std::isfinite(config.waterReflectionStrength)
        && std::isfinite(config.waterShoreFade)
        && config.waterShoreFade > 0.0f
        && std::isfinite(spectrum.significantWaveHeight)
        && std::isfinite(spectrum.directionDegrees)
        && std::isfinite(spectrum.choppiness)
        && std::isfinite(spectrum.peakEnhancement)
        && std::isfinite(spectrum.windAlignment)
        && std::isfinite(spectrum.animationSpeed)
        && finiteVector(spectrum.patchLengths)
        && spectrum.patchLengths.x > 0.0f
        && spectrum.patchLengths.y > 0.0f
        && finiteVector(spectrum.cascadeAmplitudes)
        && std::isfinite(spectrum.directionalSineScale)
        && config.gpuPhysicsMaxBodies > 0u
        && config.gpuPhysicsMaxBodies
            != std::numeric_limits<uint32_t>::max()
        && config.gpuPhysicsMaxPairs > 0u
        && config.gpuPhysicsMaxCandidatePairs >= config.gpuPhysicsMaxPairs
        && (config.gpuPhysicsSolverWorkgroupSize == 128u
            || config.gpuPhysicsSolverWorkgroupSize == 256u)
        && config.benchmarkBodyCount <= kMaximumBenchmarkBodyCount
        && config.cubePyramidBodyCount <= kMaximumBenchmarkBodyCount
        && (config.benchmarkBodyCount == 0u
            || config.cubePyramidBodyCount == 0u)
        && startupBodyCount <= benchmarkBodyCapacity
        && validGpuCellSize
        && config.gpuPhysicsMaximumCatchUpTicks > 0u
        && config.gpuPhysicsMaximumCatchUpTicks <= 1'024u
        && std::isfinite(config.gpuPhysicsTimestampPeriodNanoseconds)
        && config.gpuPhysicsTimestampPeriodNanoseconds > 0.0
        && config.box3dWorkerThreads > 0u
        && finiteVector(config.cameraStartPos)
        && std::isfinite(config.cameraFovDegrees)
        && config.cameraFovDegrees > 0.0f
        && config.cameraFovDegrees < 180.0f
        && std::isfinite(config.cameraNear) && config.cameraNear > 0.0f
        && std::isfinite(config.cameraFar)
        && config.cameraFar > config.cameraNear
        && std::isfinite(config.cameraMoveSpeed)
        && config.cameraMoveSpeed >= 0.0f
        && config.cameraMoveSpeed <= 1'000.0f
        && std::isfinite(config.cameraMouseSensitivity)
        && config.cameraMouseSensitivity >= 0.0f
        && config.cameraMouseSensitivity <= 0.02f
        && std::isfinite(config.cameraEyeHeight)
        && config.cameraEyeHeight > 0.0f
        && config.cameraEyeHeight <= 10.0f
        && std::isfinite(config.fpsLogIntervalSeconds)
        && config.fpsLogIntervalSeconds > 0.0f
        && std::isfinite(config.benchmarkMinimumFps)
        && config.benchmarkMinimumFps >= 0.0
        && std::isfinite(config.benchmarkFixedDeltaSeconds)
        && config.benchmarkFixedDeltaSeconds >= 0.0f
        && config.screenshotFrameDelay >= 0
        && config.screenshotTourIndices.size() <= 256u;
}

[[nodiscard]] uint32_t scaledRenderExtent(uint32_t extent,
                                          float scale) noexcept {
    const double scaled = std::round(
        static_cast<double>(extent) * static_cast<double>(scale));
    return static_cast<uint32_t>(std::clamp(
        scaled, 1.0,
        static_cast<double>(kPortableMaximumTextureDimension2D)));
}

#if defined(VOXY_NATIVE)
struct ScreenshotMapState {
    std::atomic<uint32_t> references{2u};
    std::atomic<bool> done{false};
    std::atomic<bool> succeeded{false};
};

void releaseScreenshotMapState(ScreenshotMapState* state) noexcept {
    if (state->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
        delete state;
    }
}

void onScreenshotBufferMapped(WGPUBufferMapAsyncStatus status,
                              void* userdata) {
    auto* state = static_cast<ScreenshotMapState*>(userdata);
    state->succeeded.store(
        status == WGPUBufferMapAsyncStatus_Success,
        std::memory_order_relaxed);
    state->done.store(true, std::memory_order_release);
    if (status != WGPUBufferMapAsyncStatus_Success) {
        LOG_ERROR("Failed to map screenshot buffer: status={}",
                  static_cast<int>(status));
    }
    releaseScreenshotMapState(state);
}
#endif

} // namespace

constexpr size_t kPrimitiveOverlayHeadroom = 1u + 5u * 7u;
constexpr uint32_t kRenderGpuQueriesPerStage = 2u;
constexpr uint32_t kRenderGpuFrameBeginQuery =
    static_cast<uint32_t>(kRenderGpuStageCount) * kRenderGpuQueriesPerStage;
constexpr uint32_t kRenderGpuFrameEndQuery = kRenderGpuFrameBeginQuery + 1u;
constexpr uint32_t kRenderGpuTimestampCount = kRenderGpuFrameEndQuery + 1u;

// Portable timestamp boundaries. Keep the ended pass handle out of submission.
// The WASM loader translates the missing optional endpoint sentinel.
static bool writeFrameBoundary(WGPUCommandEncoder encoder, WGPUQuerySet querySet,
                               uint32_t index, bool beginning) {
    WGPUComputePassDescriptor descriptor{};
    WGPU_SET_LABEL(descriptor, beginning ? "frame_gpu_begin" : "frame_gpu_end");
    gpu::CompatPassTimestampWrites writes{};
    writes.querySet = querySet;
    writes.beginningOfPassWriteIndex = beginning ? index : WGPU_QUERY_SET_INDEX_UNDEFINED;
    writes.endOfPassWriteIndex = beginning ? WGPU_QUERY_SET_INDEX_UNDEFINED : index;
    descriptor.timestampWrites = &writes;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &descriptor);
    if (!pass) return false;
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    return true;
}
constexpr uint32_t kRenderGpuProfilingIntervalFrames = 30u;

void appendObjectCount(
    std::vector<physics::PhysicsWorld::DynamicBodySnapshot>& instances,
    size_t objectCount, const glm::vec3& planeCenter,
    const glm::vec3& cameraRight, const glm::vec3& cameraUp,
    const glm::quat& cameraRotation, float halfWidth, float halfHeight) {
    // Segment bits: top, upper-right, lower-right, bottom,
    // lower-left, upper-left, middle.
    constexpr std::array<uint8_t, 10> digitMasks = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f,
    };

    const std::string digits = std::to_string(objectCount);
    const float screenScale = std::min(halfWidth, halfHeight);
    const float digitHeight = screenScale * 0.18f;
    const float digitWidth = digitHeight * 0.52f;
    const float thickness = digitHeight * 0.11f;
    const float verticalLength = digitHeight * 0.39f;
    const float gap = thickness * 1.4f;
    const float advance = digitWidth + gap;
    const float totalWidth = digitWidth * static_cast<float>(digits.size())
                           + gap * static_cast<float>(digits.size() - 1);
    const float leftEdge = halfWidth * 0.90f - totalWidth;
    const float centerY = -halfHeight * 0.38f;

    const auto addSegment = [&](float centerX, float segmentY,
                                float width, float height) {
        instances.push_back({
            physics::PhysicsWorld::ThrowableShape::Cube,
            planeCenter + cameraRight * centerX + cameraUp * segmentY,
            cameraRotation,
            glm::vec3(width, height, thickness * 0.35f)});
    };

    for (size_t digitIndex = 0; digitIndex < digits.size(); ++digitIndex) {
        const uint8_t mask = digitMasks[static_cast<size_t>(digits[digitIndex] - '0')];
        const float centerX = leftEdge + digitWidth * 0.5f
                            + static_cast<float>(digitIndex) * advance;
        const float xSide = digitWidth * 0.5f;
        const float ySide = digitHeight * 0.25f;
        const float yEdge = digitHeight * 0.5f;

        if (mask & (1u << 0u)) addSegment(centerX, centerY + yEdge, digitWidth, thickness);
        if (mask & (1u << 1u)) addSegment(centerX + xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 2u)) addSegment(centerX + xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 3u)) addSegment(centerX, centerY - yEdge, digitWidth, thickness);
        if (mask & (1u << 4u)) addSegment(centerX - xSide, centerY - ySide, thickness, verticalLength);
        if (mask & (1u << 5u)) addSegment(centerX - xSide, centerY + ySide, thickness, verticalLength);
        if (mask & (1u << 6u)) addSegment(centerX, centerY, digitWidth, thickness);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

const char* renderPathToString(RenderPath path) noexcept {
    switch (path) {
        case RenderPath::Triangle: return "triangle";
        case RenderPath::Raycast:  return "raycast";
    }
    return "unknown";
}

const char* debugVisModeToString(DebugVisMode mode) noexcept {
    switch (mode) {
        case DebugVisMode::Off:       return "off";
        case DebugVisMode::Depth:     return "depth";
        case DebugVisMode::Normals:   return "normals";
        case DebugVisMode::MipLevels: return "mip_levels";
    }
    return "unknown";
}

const char* controllerModeToString(ControllerMode mode) noexcept {
    switch (mode) {
        case ControllerMode::FreeFly:   return "free-fly";
        case ControllerMode::Character: return "character";
    }
    return "unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// WASM Global State (for Emscripten main loop)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Application::Application() = default;

Application::~Application() {
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initWreckwaterClient() {
    wreckwaterClientState_.reset();
    wreckwaterClientTelemetry_ = {};
    if (!config_.wreckwaterClient) return true;
#if defined(VOXY_WASM)
    LOG_ERROR(
        "WRECKWATER native TCP bootstrap is unavailable on WebAssembly");
    return false;
#else
    const auto& bootstrap = *config_.wreckwaterClient;
    const client::WreckwaterThirdPersonCameraRig::Config
        cameraConfig;
    const client::WreckwaterCameraTerrainView terrainView{
        .samples = heightmap_ != nullptr
            ? heightmap_->getData()
            : std::span<const uint16_t>{},
        .width = heightmap_ != nullptr
            ? heightmap_->getWidth() : 0u,
        .height = heightmap_ != nullptr
            ? heightmap_->getHeight() : 0u,
        .heightScale = config_.heightScale,
        .cellScale = config_.cellScale,
    };
    constexpr float maximumCameraProbePath = 16.0f;
    const float cameraProbeRadius =
        cameraConfig.obstructionProbeRadius
        + cameraConfig.maximumProbeEndpointDrift;
    if (!physicsWorld_
        || !physicsWorld_->capabilities().asynchronousQueries
        || !client::wreckwaterCameraTerrainViewSupports(
            terrainView, maximumCameraProbePath,
            cameraProbeRadius)) {
        LOG_ERROR(
            "WRECKWATER camera requires async rigid-body queries "
            "and bounded CPU heightfield probe coverage");
        return false;
    }

    network::NativeTcpClientConfig transportConfig;
    transportConfig.serverAddress = bootstrap.serverAddress;
    transportConfig.serverPort = bootstrap.serverPort;
    transportConfig.credential = {
        .peerId = bootstrap.peerId,
        .authenticationKey = bootstrap.authenticationKey,
    };
    transportConfig.expectedContentDigest =
        bootstrap.expectedContentDigest;
    transportConfig.requirePrivateServerAddress = true;

    std::string transportError;
    network::NativeTcpClientCreateStatus transportStatus =
        network::NativeTcpClientCreateStatus::
            InvalidConfiguration;
    auto transport = network::NativeTcpClientTransport::create(
        std::move(transportConfig), &transportError,
        &transportStatus);
    if (transport == nullptr) {
        LOG_ERROR(
            "Failed to create WRECKWATER native TCP client ({}): {}",
            network::nativeTcpClientCreateStatusName(
                transportStatus),
            transportError);
        return false;
    }

    auto state =
        std::make_unique<WreckwaterApplicationClientState>();
    state->transport = transport.get();

    client::WreckwaterApplicationClient::Config clientConfig;
    clientConfig.runtime = {
        .serverPeerId = 0u,
        .serverConnectionSerial = 0u,
        .sessionId = bootstrap.sessionId,
        .matchId = bootstrap.matchId,
        .worldId = bootstrap.worldId,
        .worldEpoch = bootstrap.worldEpoch,
        .authorityEpoch = bootstrap.authorityEpoch,
        .localPlayerId = bootstrap.peerId,
        .maximumFramesPerPump = 64u,
        .firstClientRequestSequence = 1u,
    };
    clientConfig.graphical.maximumCatchUpTicks = 4u;
    clientConfig.graphical.inputLeadTicks =
        bootstrap.inputLeadTicks;
    clientConfig.graphical.useCertifiedVisualClock = true;
    clientConfig.graphical.visualClock.interpolationDelayTicks =
        bootstrap.interpolationDelayTicks;
    clientConfig.graphical.visualClock.maximumExtrapolationTicks =
        network::kWreckwaterClientMaximumExtrapolationTicks;
    clientConfig.graphical.controller.localPlayerId =
        bootstrap.peerId;
    clientConfig.graphical.presentation.localPlayerId =
        bootstrap.peerId;
    clientConfig.graphical.presentation.roster = {{
        {
            .playerId = 1u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 0u,
        },
        {
            .playerId = 2u,
            .crew = game::CrewId::CrewOne,
            .crewSlot = 1u,
        },
        {
            .playerId = 3u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 0u,
        },
        {
            .playerId = 4u,
            .crew = game::CrewId::CrewTwo,
            .crewSlot = 1u,
        },
    }};
    clientConfig.camera = cameraConfig;
    if (!state->client.initialize(
            clientConfig, std::move(transport))) {
        LOG_ERROR(
            "Failed to initialize WRECKWATER graphical client");
        return false;
    }

    state->observedTransportState = state->transport->state();
    wreckwaterClientTelemetry_.enabled = true;
    wreckwaterClientTelemetry_.connectionState =
        wreckwaterApplicationConnectionState(
            state->observedTransportState);
    wreckwaterClientState_ = std::move(state);

    LOG_WARN(
        "WRECKWATER native TCP sends its bootstrap credential in "
        "plaintext; use only loopback or a trusted private development "
        "network");
    LOG_INFO(
        "WRECKWATER client starting: {}:{} peer {}",
        bootstrap.serverAddress, bootstrap.serverPort,
        bootstrap.peerId);
    return true;
#endif
}

void Application::setSalvageSaveStatus(std::string status) {
    salvageSaveStatus_=std::move(status);
#if defined(VOXY_NATIVE)
    if(window_ && !salvageSaveStatus_.empty())window_->setTitle(("Cove | "+salvageSaveStatus_).c_str());
#endif
}

std::string Application::covePreferencesAction(int action,std::string_view text) {
    using namespace game::expedition;
    if(!coveUi_)coveUi_=std::make_unique<CoveUiState>();
    auto& ui=*coveUi_;std::string error,result;
    if(action==1)return encodeCoveInputPreferences(ui.preferences,result,error)?result:"";
    if(action!=2&&action!=3)return "Unknown settings action.";
    CoveInputPreferences next;
    if(action==2&&!parseCoveInputPreferences(text,next,error))return error;
    if(action==3&&!text.empty())return "Reset settings takes no extra data.";
    ui.preferences=std::move(next);ui.input.reset();ui.orbitLatched=false;
    if(input_){(void)input_->setGamepadDeadzones(float(ui.preferences.moveDeadzone),float(ui.preferences.lookDeadzone));input_->resetState();}
    ui.preferenceStatus.clear();
#if defined(VOXY_NATIVE)
    if(!ui.preferencesPath.empty()) {
        const auto stored=platform::saveNativeCovePreferences(ui.preferencesPath,ui.preferences,ui.preferenceStatus);
        // Optional settings failure is visible but never revokes world saving.
        if(stored==platform::CovePreferencesStatus::Saved)ui.preferenceStatus="Settings saved.";
    }
#endif
    return "ok";
}
bool Application::consumeCoveSaveRequest() noexcept {
    if(!coveUi_||!coveUi_->saveRequested)return false;
    coveUi_->saveRequested=false;return true;
}
void Application::noteCoveSaveCompleted() noexcept {
    if(coveUi_)coveUi_->saveConfirmed=true;
}
void Application::setCoveSavedWorlds(std::vector<std::pair<std::string,std::string>> worlds,std::string current) {
    if(!coveUi_)coveUi_=std::make_unique<CoveUiState>();
    if(worlds.size()>64)worlds.resize(64);
    coveUi_->worlds=std::move(worlds);coveUi_->currentWorld=std::move(current);
}
void Application::setCoveHostBusy(bool busy) noexcept {if(coveUi_)coveUi_->hostBusy=busy;}
std::optional<std::string> Application::consumeCoveWorldRequest() {
    if(!coveUi_)return {};
    return std::exchange(coveUi_->worldRequest,{});
}
bool Application::completeCoveWorldPreparation() {
    if(!coveUi_||!coveUi_->worldPreparing||!salvagePreviewAction(2))return false;
    coveUi_->exitAfterLeave=true;return true;
}
bool Application::coveWorldTransitionDrained() const noexcept {
    return coveUi_&&coveUi_->worldPreparing&&coveUi_->exitAfterLeave&&!salvagePreviewFailed_
        &&salvagePreview_&&salvagePreview_->phase()==game::expedition::SalvagePreview::Phase::Empty
        &&!coveAssetsDraining();
}
void Application::cancelCoveWorldPreparation(std::string message) {
    if(!coveUi_)return;
    coveUi_->worldPreparing=false;coveUi_->worldRequest.reset();coveUi_->worldStatus=std::move(message);
}

bool Application::preflightCoveWorld(std::array<uint8_t,16> world,std::span<const std::byte> bytes,std::string& error) {
    using namespace game::expedition;
    using namespace game::construction;
    if(!salvageLocalSession_||!salvageLocalSession_->asset||!salvageLocalSession_->saveContext
        ||!isValid(WorldNamespace{world})){error="The current Cove is not ready to check another save.";return false;}
    const auto& local=*salvageLocalSession_;const auto& asset=*local.asset;
    // Independent trusted bootstrap for validation only. Never publishes a
    // session, mutates the current allocator, or trusts content from the save.
    auto seed=prepareCoveBuild(*asset.content,{WorldNamespace{world},1},4,error);
    if(!seed)return false;
    auto starter=prepareCoveStarterKit(seed->build,seed->starterEntitlement,error);
    if(!starter)return false;
    auto context=*local.saveContext;context.identity.world={world};
    context.boat=seed->build.id;context.cargo={WorldNamespace{world},4};context.job={WorldNamespace{world},3};
    const auto origin=asset.origin;
    auto candidate=CoveRestoreCandidate::prepare(bytes,context,*asset.content,seed->catalog,seed->placements,
        [this,origin](double x,double z){return double(sampleTerrainHeight(float(x+origin.x),float(z+origin.z)))-origin.y;},
        error,&*starter,coveGroundSupport(origin),asset.environmentObstacleSpan());
    return bool(candidate);
}

bool Application::stageCoveResume(std::array<uint8_t,16> world,std::span<const std::byte> bytes) {
    if(initialized_ || physicsWorld_ || coveResume_ || !game::construction::isValid(game::construction::WorldNamespace{world})
        ||bytes.size()<459 ||bytes.size()>game::expedition::kMaximumCoveSaveBytes)return false;
    constexpr std::array<std::byte,4> header{std::byte{'S'},std::byte{'V'},std::byte{'C'},std::byte{'E'}};
    if(!std::equal(header.begin(),header.end(),bytes.begin()))return false;
    uint32_t schema=0;for(size_t i=0;i<4;++i)schema|=uint32_t(std::to_integer<uint8_t>(bytes[4+i]))<<(8*i);
    if(schema!=game::expedition::kCoveSaveSchema&&schema!=game::expedition::kCoveSaveHarborSchema
        &&schema!=game::expedition::kCoveSaveRecoverySchema&&schema!=game::expedition::kCoveSaveRootsSchema
        &&schema!=game::expedition::kCoveSaveCharacterSchema)return false;
    const auto digest=core::sha256(bytes.first(bytes.size()-32));
    if(!std::equal(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32))return false;
    try {
        auto source=std::make_unique<CoveResumeSource>();source->world=world;
        for(size_t i=0;i<8;++i)source->tick|=uint64_t(std::to_integer<uint8_t>(bytes[8+i]))<<(8*i);
        source->source.assign(bytes.begin(),bytes.end());coveResume_=std::move(source);return true;
    }catch(const std::bad_alloc&){return false;}
}

bool Application::init(const ApplicationConfig& config) {
    if (initialized_) {
        LOG_WARN("Application already initialized");
        return true;
    }
    if (!validApplicationConfig(config) || (coveResume_ && (!config.salvagePreviewEnabled
        || !config.salvageAssetFixtureWaterAnchor || config.physicsBackend!=physics::BackendType::WebGpuSoft))) {
        LOG_ERROR("Application configuration is invalid "
                  "(window {}x{}, heightmap {}x{}, scale {})",
                  config.windowWidth, config.windowHeight,
                  config.heightmapWidth, config.heightmapHeight,
                  config.resolutionScale);
        return false;
    }

    stats_ = {};
    physicsGpuTimingSamples_ = {};
    physicsGpuTimingSampleHead_ = 0u;
    physicsGpuTimingSampleCount_ = 0u;
    renderGpuTimingSamples_ = {};
    renderGpuTimingSampleHead_ = 0u;
    renderGpuTimingSampleCount_ = 0u;
    shouldExit_ = false;
    debugVisMode_ = DebugVisMode::Off;
    wireframeEnabled_ = false;
    legoMode_ = config.legoTerrainEnabled;
    controllerMode_ = ControllerMode::FreeFly;
    selectedThrowable_ = 0u;
    throwableBodyLimit_ = 0u;
    throwableWheelAccumulator_ = 0.0f;
    throwableCooldown_ = 0.0f;
    benchmarkRunner_.reset();
    browserJourneyBenchmark_.reset();
    benchmarkSubmissionIndices_.clear();
    lastFrameTime_ = 0.0;
    fpsAccumulator_ = 0.0;
    fpsFrameCount_ = 0;
    recordedPositions_.clear();
    tourActive_ = false;
    tourStep_ = 0u;
    tourFrameCounter_ = 0u;
    tourCurrentPath_.clear();

    config_ = config;
    if(config_.salvageAssetFixtureWaterAnchor) {
        if(!coveUi_)coveUi_=std::make_unique<CoveUiState>();
#if defined(VOXY_NATIVE)
        if(!coveUi_->preferencesLoaded) {
            game::expedition::StoreIssue issue;
            auto root=config_.salvageDesignLibraryRoot.empty()?platform::NativeDesignLibrary::defaultRoot(issue):config_.salvageDesignLibraryRoot;
            if(!issue) {
                coveUi_->preferencesPath=root.parent_path()/"cove-preferences.json";
                (void)platform::loadNativeCovePreferences(coveUi_->preferencesPath,coveUi_->preferences,coveUi_->preferenceStatus);
            }
            coveUi_->preferencesLoaded=true;
        }
#endif
    }
    // The playable cove retains the original brick terrain. Authored machine
    // contacts use the same plate quantization and round studs as rendering.
    if (config_.salvageAssetFixtureWaterAnchor) {
        config_.legoTerrainEnabled=true;
        legoMode_=true;
    }
    rendererSettings_.sunDirection = glm::dot(config_.sunDirection,
                                              config_.sunDirection) > 1.0e-10f
        ? glm::normalize(config_.sunDirection)
        : RendererRuntimeSettings{}.sunDirection;
    rendererSettings_.sunColor = config_.sunColor;
    rendererSettings_.ambientColor = config_.ambientColor;
    rendererSettings_.ambientIntensity = config_.ambientIntensity;
    rendererSettings_.fogDensity = config_.fogDensity;
    rendererSettings_.fogColor = config_.fogColor;
    rendererSettings_.waterEnabled = config_.waterEnabled;
    rendererSettings_.waterHeight = config_.waterHeight;
    rendererSettings_.waterShallowColor = config_.waterShallowColor;
    rendererSettings_.waterDeepColor = config_.waterDeepColor;
    rendererSettings_.waterRoughness = config_.waterRoughness;
    rendererSettings_.waterWaveStrength = config_.waterWaveStrength;
    rendererSettings_.waterReflectionStrength = config_.waterReflectionStrength;
    rendererSettings_.waterShoreFade = config_.waterShoreFade;
    rendererSettings_.waterSpectrum = config_.waterSpectrum;
    rendererSettings_.cameraFovDegrees = config_.cameraFovDegrees;
    rendererSettings_.cameraNear = config_.cameraNear;
    rendererSettings_.cameraFar = config_.cameraFar;
    rendererSettings_.cameraMoveSpeed = config_.cameraMoveSpeed;
    rendererSettings_.cameraMouseSensitivity = config_.cameraMouseSensitivity;
    rendererSettings_.cameraEyeHeight = config_.cameraEyeHeight;
    appliedWaterCoastHeight_ = rendererSettings_.waterHeight;
    appliedShadowSunDirection_ = rendererSettings_.sunDirection;
    rendererSettingsRevision_ = 1u;
    appliedRendererSettingsRevision_ = 1u;
    rendererSettingsDirty_ = 0u;
    uncappedFPS_ = !config_.vsync;
    primitiveCullController_.reset();
#if defined(VOXY_NATIVE)
    nativeWorkshopMenu_.reset();
    nativeCoveHud_.reset();
#endif
    legoPlayground_.reset();
    legoPlaygroundActive_ = false;
    salvagePreviewFailed_ = false;
    salvageLocalSession_.reset();
    salvageRetirementSeconds_ = 0;
    preSalvageCamera_.reset();

    // Initialize teleport targets
    // Paste recorded positions here!
    teleportTargets_ = {
        { { 202.53f, 120.92f, -27.16f }, 1.4578f, -0.0944f },
        { { 202.53f, 120.92f, -27.16f }, -0.2070f, -0.3556f },
        { { 179.01f, 121.64f, -28.72f }, -1.3958f, -0.1900f },
        { { 179.01f, 121.64f, -28.72f }, -2.0606f, -0.0380f },
    };

    LOG_INFO("═══════════════════════════════════════════════════════════════");
    LOG_INFO("  {}", config_.motoEnabled ? "RIDGEBREAK - Motocross Freeride" : "Voxy - Terrain Renderer");
#if defined(VOXY_NATIVE)
    LOG_INFO("  Build target: native");
#elif defined(VOXY_WASM)
    LOG_INFO("  Build target: wasm");
#endif
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("═══════════════════════════════════════════════════════════════");

    const auto failInitialization = [this]() {
        shutdown();
        return false;
    };

    // Initialize subsystems in order
    {
        LOG_SCOPE("Application::init");

        if (!initWindow()) {
            LOG_ERROR("Failed to initialize window");
            return failInitialization();
        }

        if (!initGPU()) {
            LOG_ERROR("Failed to initialize GPU context");
            return failInitialization();
        }

        if (!initRenderGpuProfiling()) {
            LOG_ERROR("Failed to initialize render GPU profiling");
            return failInitialization();
        }

        if (!initInput()) {
            LOG_ERROR("Failed to initialize input system");
            return failInitialization();
        }

        if (!initCamera()) {
            LOG_ERROR("Failed to initialize camera");
            return failInitialization();
        }

        if (!initTerrain()) {
            LOG_ERROR("Failed to initialize terrain");
            return failInitialization();
        }

        if (!initRenderers()) {
            LOG_ERROR("Failed to initialize renderers");
            return failInitialization();
        }

        if (!initMoto()) {
            LOG_ERROR("Failed to initialize RIDGEBREAK freeride session");
            return failInitialization();
        }

        if (!initWreckwaterClient()) {
            return failInitialization();
        }

        if (!initSalvagePreview()) return failInitialization();
        if(config_.adventureEnabled) {
            adventure_=std::make_unique<game::adventure::AdventureRuntime>();
            std::string error;
            if(!adventure_->initialize({heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),config_.heightScale,config_.cellScale},
                gpuContext_->getDevice(),gpuContext_->getQueue(),config_.shaderDir,config_.colorFormat,error)) {
                LOG_ERROR("Adventure could not start: {}",error);return failInitialization();
            }
        }

        setupCallbacks();
    }

    if (config_.legoTerrainEnabled && !config_.salvagePreviewEnabled && !config_.adventureEnabled) {
        // The same shoreline viewpoint, in the original landscape coordinates.
        const bool fullWorld = heightmap_->getWidth() > terrain::lego::kMaximumStudySamples;
        const float x = fullWorld ? -1164.0f : -12.0f;
        const float z = fullWorld ? 3398.0f : -58.0f;
        const float y = sampleTerrainHeight(x,z) + config_.cameraEyeHeight;
        setCameraWorldPose(*camera_, {x,y,z}, {x+8.0f,y-6.0f,z-35.0f});
        throwableBodyLimit_ = 32u;
    }
    initialized_ = true;
    shouldExit_ = false;

    LOG_INFO("Application initialized successfully");
    LOG_INFO("  Window: {}x{}", config_.windowWidth, config_.windowHeight);
    LOG_INFO("  Terrain: {}x{} (mips: {})", 
             stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    LOG_INFO("  Render path: {}", renderPathToString(config_.renderPath));
    LOG_INFO("");
    LOG_INFO("Controls:");
    if(config_.adventureEnabled) {
        LOG_INFO("  WASD / left stick - Walk; Space / A - Jump");
        LOG_INFO("  B / View - Build; F2 / Menu - Options; F5 - Save");
        LOG_INFO("  E / X - Use; Right drag / right stick - Look");
    } else if (motoSession_ && motoSession_->isInitialized()) {
        LOG_INFO("  W/Up      - Throttle");
        LOG_INFO("  S/Down    - Brake");
        LOG_INFO("  A/D       - Steer");
        LOG_INFO("  Q/E       - Rider lean");
        LOG_INFO("  Shift     - Stand");
        LOG_INFO("  Ctrl      - Duck");
        LOG_INFO("  Z/X       - Shift down/up");
        LOG_INFO("  R         - Remount after a crash");
        LOG_INFO("  C         - Start/restart circuit from the grid");
        LOG_INFO("  Backspace - Reset bike / restart active circuit");
    } else if (wreckwaterClientState_) {
        LOG_INFO("  WASD      - Camera-relative movement");
        LOG_INFO("  Space     - Jump / swim stroke");
        LOG_INFO("  E         - Board");
        LOG_INFO("  Mouse     - Orbit (click to capture)");
        LOG_INFO("  Wheel     - Camera distance");
        LOG_INFO("  Q         - Swap camera shoulder");
    } else {
        LOG_INFO("  WASD      - Move camera");
        LOG_INFO("  Mouse     - Look around (click to capture)");
        LOG_INFO("  Shift     - Speed boost / Run");
        LOG_INFO("  E/Space   - Move up / Jump");
        LOG_INFO("  Q/Ctrl    - Move down");
    }
    LOG_INFO("  F1        - Toggle debug overlay");
    LOG_INFO("  F2        - Toggle wireframe mode");
    LOG_INFO("  F3        - Toggle render path");
    LOG_INFO("  F4        - Toggle depth visualization");
    LOG_INFO("  F5        - Toggle normal visualization");
    LOG_INFO("  F6        - Toggle mip level heat map");
    if (!motoSession_ && !config_.salvagePreviewEnabled) {
        LOG_INFO("  F7        - Toggle benchmark mode");
        LOG_INFO("  F8        - Toggle controller (free-fly/character)");
    }
    LOG_INFO("  F9        - Toggle uncapped/VSync presentation");
    LOG_INFO("  Escape    - Release mouse / Exit");
    if (config_.salvagePreviewEnabled) LOG_INFO("  R         - Reset cove preview");
    if (!wreckwaterClientState_ && !motoSession_ && !config_.salvagePreviewEnabled) {
        LOG_INFO("  Wheel     - Select throwable object");
        LOG_INFO(
            "  Left click- Capture mouse / throw selected object");
        LOG_INFO("  Right click- Throw 128 selected objects");
    }
    LOG_INFO("");

#if defined(VOXY_WASM)
    // RIDGEBREAK opens as a game, not as an engine diagnostics screen.
    // The F1 overlay remains available to developers and benchmarks.
    getDebugOverlay().setVisible(!motoSession_ && !config_.salvagePreviewEnabled && !config_.adventureEnabled);
    if (!motoSession_ && !config_.salvageAssetFixtureRegistry && !config_.adventureEnabled) {
        // Retain the legacy terrain-sandbox default outside the moto product.
        setControllerMode(ControllerMode::Character);
    }
#endif

    if (config_.legoTerrainEnabled && !config_.salvageAssetFixtureRegistry && !config_.adventureEnabled)
        setControllerMode(ControllerMode::Character);

    // Handle initial teleportation
    if (config_.initialTeleportIndex.has_value() && camera_) {
        int index = config_.initialTeleportIndex.value();
        if (index >= 0 && static_cast<size_t>(index) < teleportTargets_.size()) {
            const auto& target = teleportTargets_[static_cast<size_t>(index)];
            camera_->setWorldPosition(target.sector, target.position);
            camera_->setYaw(target.yaw);
            camera_->setPitch(target.pitch);
            LOG_INFO("Applied initial teleport to index {} (Pos: {:.2f}, {:.2f}, {:.2f})",
                     index, target.position.x, target.position.y, target.position.z);
        } else {
            LOG_WARN("Invalid initial teleport index: {}", index);
        }
    }

    // If a screenshot tour is requested, prepare it now (after initial teleport).
    startScreenshotTour();

    if (!spawnBenchmarkBodies()) {
        LOG_ERROR("Failed to create the deterministic benchmark body set");
        return failInitialization();
    }
#if !defined(VOXY_WASM)
    if (config_.cubePyramidBodyCount != 0u
        && !startCubePyramidExperiment()) {
        LOG_ERROR("Failed to create the cube triangle experiment");
        return failInitialization();
    }
#endif

    if (config_.benchmarkOnStartup) {
        startBenchmark();
    }

    return true;
}

// Run is now handled by the platform entry point (entry.cpp)
// This method is deprecated and should be removed or made empty if the interface requires it.
// For now, we will leave it empty as the logic is moved to entry.cpp
void Application::run() {
    LOG_WARN("Application::run() is deprecated. Use platform entry point instead.");
}

void Application::requestExit() {
    LOG_INFO("Exit requested");
    shouldExit_ = true;

#if defined(VOXY_NATIVE)
    if (window_) {
        window_->requestClose();
    }
#endif
}

void Application::shutdown() {
    const bool hasResources = initialized_
        || wreckwaterClientState_ || salvagePreview_
        || window_ || gpuContext_ || input_ || camera_ || freeFlyController_
        || physicsWorld_ || characterController_ || heightmap_
        || terrainTextures_ || waterSimulation_ || primitivePath_
        || meshPath_ || motoSession_
        || trianglePath_ || raycastPath_ || blitPath_
        || renderGpuQuerySet_ || renderGpuResolveBuffer_
        || depthTexture_ || depthView_
        || benchmarkTargetTexture_ || benchmarkTargetView_
        || shadowMapTexture_ || shadowMapView_;
    if (!hasResources) {
        return;
    }

    LOG_INFO("Shutting down application...");

    if (wreckwaterClientState_) {
        wreckwaterClientState_->client.close();
        wreckwaterClientState_.reset();
    }
    wreckwaterClientTelemetry_ = {};
    if (config_.wreckwaterClient) {
        config_.wreckwaterClient->authenticationKey.fill(
            std::byte{});
    }

    retireBenchmarkSubmissions(true);

    // Final interruption/loss may have outstanding work. The fixture releases
    // external handles without Destroy unless its completion fence has passed.
    adventure_.reset();
    if (salvageLocalSession_) salvageLocalSession_->asset.reset();
    salvageRetirementReadback_.shutdown();
    salvageMetadataSource_.clear();
    salvageRetirementBytes_ = 0;
    renderGpuReadback_.shutdown();
    if (renderGpuResolveBuffer_) {
        wgpuBufferDestroy(renderGpuResolveBuffer_);
        wgpuBufferRelease(renderGpuResolveBuffer_);
        renderGpuResolveBuffer_ = nullptr;
    }
    if (renderGpuQuerySet_) {
        wgpuQuerySetRelease(renderGpuQuerySet_);
        renderGpuQuerySet_ = nullptr;
    }

// No WASM global state needed in Application anymore

    // Release GPU resources
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    if (benchmarkTargetView_) {
        wgpuTextureViewRelease(benchmarkTargetView_);
        benchmarkTargetView_ = nullptr;
    }
    if (benchmarkTargetTexture_) {
        wgpuTextureDestroy(benchmarkTargetTexture_);
        wgpuTextureRelease(benchmarkTargetTexture_);
        benchmarkTargetTexture_ = nullptr;
    }

    if (shadowMapView_) {
        wgpuTextureViewRelease(shadowMapView_);
        shadowMapView_ = nullptr;
    }
    if (shadowMapTexture_) {
        wgpuTextureDestroy(shadowMapTexture_);
        wgpuTextureRelease(shadowMapTexture_);
        shadowMapTexture_ = nullptr;
    }

    // Shutdown renderers
    motoRaceSession_.reset();
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = 0u;
    if (motoSession_) {
        motoSession_->shutdown();
        motoSession_.reset();
    }
    if (meshPath_) {
        meshPath_->shutdown();
        meshPath_.reset();
    }
    legoPlayground_.reset();
    legoPlaygroundActive_ = false;
    if (salvageLocalSession_ && salvageLocalSession_->session)
        salvageLocalSession_->session->closeAdmission();
    if (salvagePreview_ && !salvagePreview_->shutdown()) {
        LOG_ERROR("Cove cleanup could not queue every removal; releasing its physics world.");
    }
    salvagePreview_.reset();
    salvageLocalSession_.reset();
#if defined(VOXY_NATIVE)
    nativeWorkshopMenu_.reset();
    nativeCoveHud_.reset();
#endif
    coveResume_.reset();
    preSalvageCamera_.reset();
    if (primitivePath_) {
        primitivePath_->shutdown();
        primitivePath_.reset();
    }
    if (blitPath_) {
        blitPath_->shutdown();
        blitPath_.reset();
    }
    if (raycastPath_) {
        raycastPath_->shutdown();
        raycastPath_.reset();
    }
    if (trianglePath_) {
        trianglePath_->shutdown();
        trianglePath_.reset();
    }
    if (waterSimulation_) {
        if (physicsWorld_) {
            physicsWorld_->setWaterSurfaceSampler({});
            physicsWorld_->setWaterGpuResources({});
        }
        waterSimulation_->shutdown();
        waterSimulation_.reset();
    }

    if (terrainTextures_) {
        terrainTextures_->shutdown();
        terrainTextures_.reset();
    }

    // Shutdown other subsystems
    characterController_.reset();
    if (physicsWorld_) {
        physicsWorld_->shutdown();
        physicsWorld_.reset();
    }

    // Jolt streams directly from the heightmap, so release it after physics.
    legoLayoutCache_.reset();
    if (legoLayoutView_) { wgpuTextureViewRelease(legoLayoutView_); legoLayoutView_ = nullptr; }
    if (legoLayoutTexture_) { wgpuTextureRelease(legoLayoutTexture_); legoLayoutTexture_ = nullptr; }
    if (heightmap_) {
        heightmap_->release();
        heightmap_.reset();
    }

    freeFlyController_.reset();
    camera_.reset();
    input_.reset();

    if (gpuContext_) {
        gpuContext_->shutdown();
        gpuContext_.reset();
    }

    if (window_) {
        window_->shutdown();
        window_.reset();
    }

#if defined(VOXY_NATIVE)
    Window::terminateGLFW();
#endif

    initialized_ = false;
    cubePyramidSpawnAttempted_ = false;
    cubePyramidSpawned_ = false;
    cubeTriangleColumns_.clear();
    cubeTriangleColumnsWokenThisTick_.clear();
    cubeTriangleWakeDedupTick_ = std::numeric_limits<uint64_t>::max();
    LOG_INFO("Application shutdown complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame Processing
// ─────────────────────────────────────────────────────────────────────────────

void Application::beginFrame() {
    // Save previous input state BEFORE polling new events
    if (input_) {
        input_->beginFrame();
    }

#if defined(VOXY_NATIVE)
    // Poll events to get fresh input state
    if (window_) {
        window_->pollEvents();
    }
#endif

    // Compute input deltas AFTER events are polled
    if (input_) {
        input_->computeDeltas();
    }
}

void Application::update(float deltaTime) {
    update(deltaTime, deltaTime);
}

void Application::updateWreckwaterClient(float deltaTime) {
    if (!wreckwaterClientState_) return;

    pollWreckwaterCameraProbe();

    client::WreckwaterApplicationClientFrameInput frame;
    const double elapsedNanoseconds =
        static_cast<double>(deltaTime) * 1'000'000'000.0;
    frame.elapsedNanoseconds =
        elapsedNanoseconds
                >= static_cast<double>(
                    std::numeric_limits<uint64_t>::max())
        ? std::numeric_limits<uint64_t>::max()
        : static_cast<uint64_t>(
            std::llround(elapsedNanoseconds));

    if (camera_) {
        frame.cameraSector = camera_->worldSector();
    }
    if (input_ && camera_) {
        client::WreckwaterApplicationDigitalControls digital;
        digital.forward =
            input_->isKeyDown(Key::W)
            || input_->isKeyDown(Key::Up);
        digital.backward =
            input_->isKeyDown(Key::S)
            || input_->isKeyDown(Key::Down);
        digital.left =
            input_->isKeyDown(Key::A)
            || input_->isKeyDown(Key::Left);
        digital.right =
            input_->isKeyDown(Key::D)
            || input_->isKeyDown(Key::Right);
        digital.jumpDown = input_->isKeyDown(Key::Space);
        digital.boardDown = input_->isKeyDown(Key::E);
        if (!client::mapWreckwaterCameraRelativeControls(
                digital, camera_->forward(), camera_->right(),
                frame.controls)) {
            frame.controls = {
                .jumpDown = digital.jumpDown,
                .boardDown = digital.boardDown,
            };
        }

        if (input_->wasMouseButtonPressed(MouseButton::Left)
            && !input_->isMouseCaptured()
            && !input_->wasKeyPressed(Key::Escape)) {
            input_->captureMouse();
        }
        const float cameraDelta =
            std::clamp(deltaTime, 0.0f, 0.05f);
        if (input_->isMouseCaptured()
            && cameraDelta > 0.0f) {
            const glm::vec2 mouse = input_->mouseDelta();
            constexpr float yawSpeed = 3.5f;
            constexpr float pitchSpeed = 2.5f;
            frame.camera.orbit.x = std::clamp(
                mouse.x * config_.cameraMouseSensitivity
                    / (yawSpeed * cameraDelta),
                -1.0f, 1.0f);
            frame.camera.orbit.y = std::clamp(
                -mouse.y * config_.cameraMouseSensitivity
                    / (pitchSpeed * cameraDelta),
                -1.0f, 1.0f);
        }
        if (cameraDelta > 0.0f) {
            constexpr float zoomSpeed = 5.0f;
            constexpr float metersPerWheelStep = 0.35f;
            frame.camera.zoom = std::clamp(
                -input_->scrollDelta() * metersPerWheelStep
                    / (zoomSpeed * cameraDelta),
                -1.0f, 1.0f);
        }
        frame.camera.shoulderSwapDown =
            input_->isKeyDown(Key::Q);
    }

    const client::WreckwaterApplicationClientFrameResult result =
        wreckwaterClientState_->client.frame(frame);
    if (result.cameraUpdated && camera_) {
        const auto& pose =
            wreckwaterClientState_->client.cameraRig().pose();
        camera_->setWorldPosition(
            pose.cameraPosition.sector,
            pose.cameraPosition.local);
        camera_->lookAt(pose.cameraSectorLookTarget);
        camera_->setFovY(pose.fovYRadians);
    }

    auto addSaturated = [](uint64_t& value, uint64_t amount) {
        if (amount
            > std::numeric_limits<uint64_t>::max() - value) {
            value = std::numeric_limits<uint64_t>::max();
        } else {
            value += amount;
        }
    };
    addSaturated(wreckwaterClientTelemetry_.frames, 1u);
    addSaturated(
        wreckwaterClientTelemetry_.snapshotsAccepted,
        result.graphical.runtime.snapshotsAccepted);
    addSaturated(
        wreckwaterClientTelemetry_.rejectedFrames,
        result.graphical.runtime.framesRejected);
    wreckwaterClientTelemetry_.visibleProxies =
        static_cast<uint32_t>(
            wreckwaterClientState_->client
                .visibleInstances().size());
    wreckwaterClientTelemetry_.lastFrameStatus =
        static_cast<uint32_t>(result.graphical.status);
    wreckwaterClientTelemetry_.lastRuntimeError =
        static_cast<uint32_t>(
            result.graphical.runtime.lastError);
    wreckwaterClientTelemetry_.lastReplicationError =
        static_cast<uint32_t>(
            result.graphical.runtime.lastReplicationError);
    wreckwaterClientTelemetry_.cameraValid =
        result.cameraUpdated;
    wreckwaterClientTelemetry_.cameraProbeOutstanding =
        wreckwaterClientState_->cameraProbeSubmitted;

#if !defined(VOXY_WASM)
    const network::NativeTcpClientState transportState =
        wreckwaterClientState_->transport->state();
    wreckwaterClientTelemetry_.connectionState =
        wreckwaterApplicationConnectionState(transportState);
    const auto& transportTelemetry =
        wreckwaterClientState_->transport->telemetry();
    wreckwaterClientTelemetry_.socketErrors =
        transportTelemetry.socketErrors;
    if (transportState
        != wreckwaterClientState_->observedTransportState) {
        if (transportState
            == network::NativeTcpClientState::Failed) {
            LOG_ERROR(
                "WRECKWATER connection state: failed "
                "(auth failures {}, content mismatches {}, "
                "socket errors {})",
                transportTelemetry.authenticationFailures,
                transportTelemetry.contentDigestMismatches,
                transportTelemetry.socketErrors);
        } else {
            LOG_INFO(
                "WRECKWATER connection state: {}",
                nativeTcpClientStateName(transportState));
        }
        wreckwaterClientState_->observedTransportState =
            transportState;
    }
#endif

    if (result.graphical.runtime.lastError
            != network::WreckwaterClientRuntimeError::None
        && result.graphical.runtime.lastError
            != wreckwaterClientState_->observedRuntimeError) {
        LOG_WARN(
            "WRECKWATER runtime frame: {}",
            network::wreckwaterClientRuntimeErrorName(
                result.graphical.runtime.lastError));
    }
    wreckwaterClientState_->observedRuntimeError =
        result.graphical.runtime.lastError;
    wreckwaterClientState_->observedFrameStatus =
        result.graphical.status;

    submitWreckwaterCameraProbe();
    wreckwaterClientTelemetry_.cameraProbeOutstanding =
        wreckwaterClientState_->cameraProbeSubmitted;
}

void Application::pollWreckwaterCameraProbe() {
    if (!wreckwaterClientState_
        || !wreckwaterClientState_->cameraProbeSubmitted
        || !physicsWorld_) {
        return;
    }
    const std::optional<physics::PhysicsQueryBatch> batch =
        physicsWorld_->pollQueryResults();
    if (!batch) return;

    const client::WreckwaterCameraObstructionProbeResult merged =
        client::mergeWreckwaterCameraObstructionProbeResult(
            wreckwaterClientState_->cameraProbe,
            wreckwaterClientState_->terrainProbe, *batch);
    const client::WreckwaterThirdPersonCameraStatus status =
        wreckwaterClientState_->client
            .resolveCameraObstructionProbe(merged);
    wreckwaterClientState_->cameraProbeSubmitted = false;
    wreckwaterClientState_->cameraProbe = {};
    wreckwaterClientState_->terrainProbe = {};
    if (status
        != client::WreckwaterThirdPersonCameraStatus::Accepted) {
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
    }
}

void Application::submitWreckwaterCameraProbe() {
    if (!wreckwaterClientState_
        || wreckwaterClientState_->cameraProbeSubmitted
        || !physicsWorld_ || !heightmap_) {
        return;
    }

    client::WreckwaterCameraObstructionProbeRequest request;
    const client::WreckwaterThirdPersonCameraStatus takeStatus =
        wreckwaterClientState_->client
            .takeCameraObstructionProbe(request);
    if (takeStatus
        == client::WreckwaterThirdPersonCameraStatus::
            ProbeUnavailable
        || takeStatus
        == client::WreckwaterThirdPersonCameraStatus::
            ProbeOutstanding) {
        return;
    }
    if (takeStatus
        != client::WreckwaterThirdPersonCameraStatus::Accepted) {
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }

    const client::WreckwaterCameraTerrainView terrainView{
        .samples = heightmap_->getData(),
        .width = heightmap_->getWidth(),
        .height = heightmap_->getHeight(),
        .heightScale = config_.heightScale,
        .cellScale = config_.cellScale,
    };
    const client::WreckwaterCameraTerrainCastResult terrain =
        client::wreckwaterCameraTerrainSphereCast(
            request, terrainView);
    if (!terrain) {
        client::WreckwaterCameraObstructionProbeResult failed;
        failed.identity = request.identity;
        failed.overflow = true;
        static_cast<void>(
            wreckwaterClientState_->client
                .resolveCameraObstructionProbe(failed));
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }

    if (!physicsWorld_->submitQueries(
            std::span(&request.physicsQuery, 1u))) {
        static_cast<void>(
            wreckwaterClientState_->client
                .cancelCameraObstructionProbe(
                    request.identity));
        if (wreckwaterClientTelemetry_.cameraProbeFailures
            != std::numeric_limits<uint64_t>::max()) {
            ++wreckwaterClientTelemetry_.cameraProbeFailures;
        }
        return;
    }
    wreckwaterClientState_->cameraProbe = request;
    wreckwaterClientState_->terrainProbe = terrain;
    wreckwaterClientState_->cameraProbeSubmitted = true;
}

void Application::updateMoto(float deltaTime) {
    if (!motoSession_ || !motoSession_->isInitialized() || !input_) return;

    const bool circuitRequested = input_->wasKeyPressed(Key::C);
    const bool resetRequested = input_->wasKeyPressed(Key::Backspace);
    const moto::RaceMode raceMode = motoRaceSession_
        ? motoRaceSession_->config().mode : moto::RaceMode::Freeride;
    const moto::RacePhase racePhase = motoRaceSession_
        ? motoRaceSession_->phase() : moto::RacePhase::Lobby;
    const uint32_t countdown = motoRaceSession_
        ? motoRaceSession_->countdownTicksRemaining() : 0u;
    const moto::MotoRaceApplicationPolicy applicationPolicy =
        moto::evaluateMotoRaceApplicationPolicy(
            circuitRequested, resetRequested, raceMode, racePhase, countdown);
    if (applicationPolicy.action
            == moto::MotoRaceControlAction::StartCircuit
        && !startMotoCircuit()) {
        LOG_ERROR("Could not start or restart RIDGEBREAK circuit");
    } else if (applicationPolicy.action
               == moto::MotoRaceControlAction::ResetPractice) {
        motoSession_->reset();
        motoRaceObservedTricksLanded_ = 0u;
    }

    moto::BikeInput bikeInput;
    bikeInput.throttle =
        (input_->isKeyDown(Key::W) || input_->isKeyDown(Key::Up)) ? 1.0f : 0.0f;
    bikeInput.brake =
        (input_->isKeyDown(Key::S) || input_->isKeyDown(Key::Down)) ? 1.0f : 0.0f;
    bikeInput.steer =
        (input_->isKeyDown(Key::D) || input_->isKeyDown(Key::Right) ? 1.0f : 0.0f)
        - (input_->isKeyDown(Key::A) || input_->isKeyDown(Key::Left) ? 1.0f : 0.0f);
    bikeInput.lean = (input_->isKeyDown(Key::E) ? 1.0f : 0.0f)
        - (input_->isKeyDown(Key::Q) ? 1.0f : 0.0f);
    bikeInput.seated = !(input_->isKeyDown(Key::LeftShift)
                         || input_->isKeyDown(Key::RightShift));
    bikeInput.duck = input_->isKeyDown(Key::LeftControl)
        || input_->isKeyDown(Key::RightControl);
    bikeInput.shiftDown = input_->wasKeyPressed(Key::Z);
    bikeInput.shiftUp = input_->wasKeyPressed(Key::X);
    bikeInput.remount = input_->wasKeyPressed(Key::R);

    if (!resetRequested || circuitRequested) {
        motoSession_->update(
            bikeInput,
            [this](float x, float z) { return sampleTerrainHeight(x, z); },
            deltaTime,
            [this]() {
                if (!motoRaceSession_) {
                    return moto::MotoFixedStepMode::Simulate;
                }
                const moto::MotoRaceApplicationPolicy policy =
                    moto::evaluateMotoRaceApplicationPolicy(
                        false, false, motoRaceSession_->config().mode,
                        motoRaceSession_->phase(),
                        motoRaceSession_->countdownTicksRemaining());
                return policy.gridOwned
                    ? moto::MotoFixedStepMode::HoldGrid
                    : moto::MotoFixedStepMode::Simulate;
            },
            [this](const moto::BikeState& state) {
                advanceMotoRaceFixedStep(state);
            });
    }

    if (camera_) {
        const moto::MotoCameraPose& pose = motoSession_->cameraPose();
        camera_->setWorldPosition({0, 0, 0}, pose.position);
        camera_->lookAt(pose.target);
    }
}

void Application::advanceMotoRaceFixedStep(const moto::BikeState& state) {
    if (!motoRaceSession_) return;

    std::array<moto::RaceTrickEvent, 4> trickEvents{};
    size_t trickEventCount = 0u;
    if (state.tricksLanded < motoRaceObservedTricksLanded_) {
        motoRaceObservedTricksLanded_ = state.tricksLanded;
    }
    const bool newlyCanonicalLanding =
        state.tricksLanded > motoRaceObservedTricksLanded_
        && state.lastLandedTricks != 0u
        && state.lastLanding != moto::LandingQuality::Crashed
        && motoRaceSession_->phase() == moto::RacePhase::Running;
    if (newlyCanonicalLanding) {
        const moto::RaceRiderState* rider =
            motoRaceSession_->rider(kLocalMotoRacePlayer);
        if (rider != nullptr
            && rider->lastTrickSequence
                   != std::numeric_limits<uint64_t>::max()
            && rider->lastLandingIdentity
                   != std::numeric_limits<uint64_t>::max()) {
            uint64_t sequence = rider->lastTrickSequence;
            const uint64_t landingIdentity =
                rider->lastLandingIdentity + 1u;
            const uint64_t authorityTick = motoRaceSession_->tick() + 1u;
            const auto append = [&](moto::TrickFlags flag,
                                    moto::RaceTrick trick) {
                if ((state.lastLandedTricks
                     & static_cast<uint32_t>(flag)) == 0u) return;
                trickEvents[trickEventCount++] = {
                    .sequence = ++sequence,
                    .landingIdentity = landingIdentity,
                    .authorityTick = authorityTick,
                    .trick = trick,
                };
            };
            append(moto::TrickFlags::Whip, moto::RaceTrick::Whip);
            append(moto::TrickFlags::Backflip, moto::RaceTrick::Backflip);
            append(moto::TrickFlags::Frontflip, moto::RaceTrick::Frontflip);
            append(moto::TrickFlags::BarrelRoll,
                   moto::RaceTrick::BarrelRoll);
        }
    }
    motoRaceObservedTricksLanded_ = state.tricksLanded;

    const moto::RaceRiderFrame frame{
        .player = kLocalMotoRacePlayer,
        .position = state.chassisPosition,
        .trickEvents = std::span(trickEvents.data(), trickEventCount),
    };
    motoRaceSession_->step(std::span(&frame, 1u));
    if (const moto::RaceRiderState* rider =
            motoRaceSession_->rider(kLocalMotoRacePlayer)) {
        motoRaceTrickSequence_ = rider->lastTrickSequence;
        motoRaceLandingIdentity_ = rider->lastLandingIdentity;
    }
}

MotoHudState Application::getMotoHudState() const noexcept {
    MotoHudState hud;
    if (!motoSession_ || !motoSession_->isInitialized()) return hud;

    const moto::BikeState& bike = motoSession_->bikeState();
    hud.active = true;
    hud.speedKilometersPerHour = std::abs(bike.speed) * 3.6f;
    hud.engineRpm = bike.engineRPM;
    hud.gear = bike.gear;
    hud.crashState = static_cast<uint32_t>(bike.crash);
    hud.combo = bike.comboCount;
    hud.score = bike.trickScore;
    if (motoRaceSession_) {
        const moto::MotoRaceApplicationPolicy policy =
            moto::evaluateMotoRaceApplicationPolicy(
                false, false, motoRaceSession_->config().mode,
                motoRaceSession_->phase(),
                motoRaceSession_->countdownTicksRemaining());
        hud.raceMode = static_cast<uint32_t>(
            motoRaceSession_->config().mode);
        hud.raceActive = policy.hudRaceActive;
        if (const moto::RaceRiderState* rider =
                motoRaceSession_->rider(kLocalMotoRacePlayer)) {
            hud.score = rider->score;
            if (hud.raceActive) {
                hud.racePhase = static_cast<uint32_t>(policy.hudPhase);
                hud.checkpointCount = static_cast<uint32_t>(
                    motoRaceSession_->checkpoints().size());
                hud.lapCount = motoRaceSession_->config().lapCount;
                hud.countdownTicksRemaining = policy.hudCountdownTicks;
                hud.nextCheckpoint = rider->nextCheckpoint;
                hud.completedLaps = rider->completedLaps;
                hud.finishPlace = rider->finishPlace;
                hud.didNotFinish = rider->didNotFinish;
            }
        }
    }
    return hud;
}

void Application::update(float simulationDeltaTime, float frameDeltaTime) {
    if (!std::isfinite(simulationDeltaTime)
        || simulationDeltaTime < 0.0f) {
        simulationDeltaTime = 0.0f;
    }
    if (!std::isfinite(frameDeltaTime) || frameDeltaTime < 0.0f) {
        frameDeltaTime = 0.0f;
    }
    landingMenuConsumed_=false;
#if defined(VOXY_WASM)
    if(input_&&config_.legoTerrainEnabled&&!config_.salvagePreviewEnabled&&!config_.adventureEnabled){
        const auto& pad=input_->gamepad();
        const bool before=EM_ASM_INT({return typeof window['voxyCoveLandingMenuActive']==='function'&&window['voxyCoveLandingMenuActive']();})!=0;
        const bool consumed=EM_ASM_INT({return typeof window['voxyCoveLandingMenuInput']==='function'&&window['voxyCoveLandingMenuInput'](
            {'up':!!$0,'down':!!$1,'left':!!$2,'right':!!$3,'confirm':!!$4,'back':!!$5,'menu':!!$6});},
            pad.navigation(0),pad.navigation(1),pad.navigation(2),pad.navigation(3),pad.pressed(PadButton::Confirm),pad.pressed(PadButton::Back),pad.pressed(PadButton::Menu))!=0;
        const bool after=EM_ASM_INT({return typeof window['voxyCoveLandingMenuActive']==='function'&&window['voxyCoveLandingMenuActive']();})!=0;
        landingMenuConsumed_=before||after||consumed;
        if(before!=after)input_->resetState();
    }
#endif
    updateSalvagePreview(frameDeltaTime);
    if (salvagePreviewFailed_) return;
#if defined(VOXY_NATIVE)
    updateNativeWorkshopMenu();
#endif
    applyRendererSettings();

    const bool browserJourneyWasRunning = browserJourneyBenchmark_
        && browserJourneyBenchmark_->isRunning();
    if (browserJourneyWasRunning) {
        updateBrowserJourneyBenchmark();
    }

    // Command-line benchmarks are scripted workloads. Ignoring gameplay input
    // keeps their camera and body count stable even if the window has focus.
    const bool scriptedBenchmark =
        (config_.benchmarkOnStartup || config_.exitAfterBenchmark)
        && isBenchmarkRunning();
    if(adventure_) {
        adventure_->update(simulationDeltaTime,*input_,*camera_,gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight());
    } else if (wreckwaterClientState_) {
        handleKeyboardShortcuts();
        updateWreckwaterClient(simulationDeltaTime);
    } else if (motoSession_ && motoSession_->isInitialized()
               && !scriptedBenchmark && !browserJourneyWasRunning) {
        updateMoto(simulationDeltaTime);
        handleKeyboardShortcuts();
    } else if (!scriptedBenchmark && !browserJourneyWasRunning && !landingMenuConsumed_
               && (!config_.salvagePreviewEnabled || (salvagePreview_
                   && salvagePreview_->phase() == game::expedition::SalvagePreview::Phase::Ready))) {
        processInput(simulationDeltaTime);
        handleKeyboardShortcuts();

        // Update camera controller based on active mode
        if (input_ && salvageLocalSession_ && salvageLocalSession_->asset
            && salvageLocalSession_->asset->player) {
            updateCovePlayer(simulationDeltaTime);
            updateCoveOnboarding();
        } else if (input_) {
            switch (controllerMode_) {
                case ControllerMode::FreeFly:
                    if (freeFlyController_) {
                        freeFlyController_->update(simulationDeltaTime, *input_);
                    }
                    break;
                case ControllerMode::Character:
                    if (characterController_) {
                        characterController_->update(simulationDeltaTime, *input_);
                    }
                    break;
            }
        }
    }

    if (config_.salvagePreviewEnabled && salvagePreview_ && salvagePreview_->busy())
        handleKeyboardShortcuts();
    if (legoPlayground_) legoPlayground_->step(simulationDeltaTime);
    updateCoveCharacterView(frameDeltaTime);
    if (physicsWorld_) {
        const auto* cove=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
        const bool covePaused=(cove && cove->pause!=SalvageLocalSessionState::AssetPreview::Pause::Running)
            || (coveResume_ && (coveResume_->awaitingCommit || !coveResume_->staged))
            || (salvageLocalSession_ && salvageLocalSession_->storageRevoked && !salvageLocalSession_->pendingControl);
        const bool awaitingCoveExecution=salvageLocalSession_ && salvageLocalSession_->session
            && salvageLocalSession_->session->hasPending() && !salvageLocalSession_->session->executionInFlight();
        physicsWorld_->update(awaitingCoveExecution || covePaused?0:simulationDeltaTime);
        const physics::PhysicsStepStats stepStats =
            physicsWorld_->lastStepStats();
        stats_.physicsSimulationMs = stepStats.simulationMs;
        stats_.physicsWaterMs = stepStats.waterMs;
    } else {
        stats_.physicsSimulationMs = 0.0;
        stats_.physicsWaterMs = 0.0;
    }

    // Custom update callback
    if (updateCallback_) {
        updateCallback_(simulationDeltaTime);
    }

    // Inspection models use camera-sector-relative matrices. Rebase after both
    // free flight and scripted placement, before any renderer reads the camera.
    // Other scenes retain their existing controller/coordinate contracts.
    if (camera_ && salvageLocalSession_ && salvageLocalSession_->asset) {
        const auto position = physics::canonicalWorldPosition(
            camera_->worldSector(), glm::dvec3(camera_->position()));
        if (position.sector != camera_->worldSector() || position.local != camera_->position())
            camera_->setWorldPosition(position.sector, position.local);
    }

    // Update statistics
    updateStats(frameDeltaTime);
}

void Application::render() {
    stats_.physicsSnapshotMs = 0.0;
    stats_.primitiveCullMs = 0.0;
    stats_.primitivePackingMs = 0.0;
    stats_.primitiveUploadMs = 0.0;
    stats_.primitiveRenderMs = 0.0;
    if (!gpuContext_ || !gpuContext_->isInitialized()) {
        return;
    }
    
    // Skip rendering if window is minimized (zero-size swapchain)
    // Requesting a texture from a zero-sized or unconfigured surface is undefined behavior
#if defined(VOXY_NATIVE)
    if (window_ && (window_->getWidth() == 0 || window_->getHeight() == 0)) {
        return;  // Window is minimized, skip rendering
    }
#endif
    
    // Also check swapchain dimensions
    if (gpuContext_->getSwapchainWidth() == 0 || gpuContext_->getSwapchainHeight() == 0) {
        return;  // Invalid swapchain dimensions
    }

    // Scripted performance runs render the same frame offscreen so window
    // server presentation and occlusion cannot contaminate GPU throughput.
    WGPUTextureView targetView = config_.benchmarkOnStartup
        ? benchmarkTargetView_ : gpuContext_->getCurrentTextureView();
    if (!targetView) {
        return;
    }

    RenderFrameGuard frameGuard;
    frameGuard.blit=blitPath_.get(); frameGuard.raycast=raycastPath_.get();
    const bool retiredBoat=salvageLocalSession_ && salvageLocalSession_->asset
        && salvageLocalSession_->asset->boat
        && salvageLocalSession_->asset->leaving && salvageLocalSession_->asset->boatRoots->allRetired()
        && salvageLocalSession_->asset->sceneryRetired
        && salvageLocalSession_->asset->environmentRetired
        && (!salvageLocalSession_->asset->harbor||salvageLocalSession_->asset->harbor->stage()==game::expedition::CoveHarborRuntime::Stage::Drained)
        && (!salvageLocalSession_->asset->cargo || salvageLocalSession_->asset->cargoRetired);
    if (physicsWorld_ && physicsWorld_->authoredShapeResources() && !retiredBoat) {
        physics::ShapeResourceError error;
        frameGuard.physicsTicket=physicsWorld_->prepareGpuSubmission(error);
        if (!frameGuard.physicsTicket.valid()) {
            if (error!=physics::ShapeResourceError::NotReady && error!=physics::ShapeResourceError::Busy) {
                LOG_ERROR("Boat physics submission failed: {}",static_cast<int>(error)); requestExit();
            }
            return;
        }
        frameGuard.physicsWorld=physicsWorld_.get();
    }
    // Create command encoder after reserving the complete shape use.
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        gpuContext_->getDevice(), &encoderDesc);
    if (!encoder) {
        LOG_ERROR("Failed to create the frame command encoder");
        return;
    }
    frameGuard.encoder=encoder;
    if (salvageLocalSession_ && salvageLocalSession_->asset)
        frameGuard.fixture = &salvageLocalSession_->asset->fixture;

    std::optional<game::expedition::CoveWaterClock> coveWaterFrame;
    auto* cove = salvageLocalSession_ ? salvageLocalSession_->asset.get() : nullptr;
    if (cove && cove->player && frameGuard.physicsTicket.valid()) {
        const auto frontier = physicsWorld_->tickFrontier();
        if (!cove->waterClock.initialized()
            && !cove->waterClock.reset(frontier.encoded, cove->waterTime)) {
            LOG_ERROR("Invalid cove water clock"); requestExit(); return;
        }
        const bool advancing = cove->pause == SalvageLocalSessionState::AssetPreview::Pause::Running
            && !cove->leaving && !salvageLocalSession_->storageRevoked
            && !(coveResume_ && (coveResume_->awaitingCommit || !coveResume_->staged));
        coveWaterFrame = cove->waterClock.prepare(frontier.scheduled, advancing);
        if (!coveWaterFrame || !physicsWorld_->stageWaterGpuFrame(
                {frontier.incarnation, frontier.scheduled, coveWaterFrame->phase()})) {
            LOG_ERROR("Cove water requires its exact single physics tick"); requestExit(); return;
        }
        frameGuard.waterTime = &cove->waterTime;
        frameGuard.previousWaterTime = cove->waterTime;
        cove->waterTime = coveWaterFrame->seconds();
        if (cove->mechanisms.incarnation()!=frontier.incarnation
            && !cove->mechanisms.reset(frontier.incarnation,frontier.encoded)) {
            LOG_ERROR("Invalid Cove mechanism clock"); requestExit(); return;
        }
        const bool mechanismsRunning=advancing && !cove->workshopOpen && !cove->checkpointPending
            && !salvageLocalSession_->launchRequest && !salvageLocalSession_->pendingControl
            && cove->rescue==SalvageLocalSessionState::AssetPreview::Rescue::None;
        const double drive=cove->boatRoots && cove->rotorCommandBody==cove->boatRoot().body
            ?cove->rotorCommandDrive:0;
        std::optional<game::expedition::CoveMechanisms::RopeSample> rope;
        // The last accepted sample remains a valid baseline while a motor
        // command awaits confirmation. Treating that gap as detach loses the
        // first accepted cable-length changes when reeling starts or stops.
        if(mechanismsRunning && cove->towRope.valid() && !cove->towBroken
            && cove->towObserved.handle==cove->towRope && cove->towObserved.alive
            && !cove->towObserved.broken && cove->towObservedTick>0)
            rope=game::expedition::CoveMechanisms::RopeSample{
                cove->towRope,cove->towObservedTick,static_cast<double>(cove->towObserved.distance.targetLength)};
        const auto mechanisms=cove->mechanisms.prepare(frontier.scheduled,mechanismsRunning,drive,rope);
        if(!mechanisms) {LOG_ERROR("Invalid Cove mechanism frame");requestExit();return;}
        // The same instance transforms feed color, depth and current shadows.
        // A failed encode/submit restores the previous phase and rope sample.
        frameGuard.mechanisms=&cove->mechanisms;
        frameGuard.previousMechanisms=cove->mechanisms;
        cove->mechanisms=*mechanisms;
    }

    // Update camera uniforms for all renderers
    updateLegoLayout();
    updateCameraUniforms();

    renderGpuProfilingFrame_ = renderGpuQuerySet_
        && config_.renderPath == RenderPath::Raycast
        && raycastPath_ && raycastPath_->isInitialized()
        && blitPath_ && blitPath_->isInitialized()
        && stats_.frameCount % kRenderGpuProfilingIntervalFrames == 0u;

    if (renderGpuProfilingFrame_ && !writeFrameBoundary(
            encoder, renderGpuQuerySet_, kRenderGpuFrameBeginQuery, true)) {
        renderGpuProfilingFrame_ = false;
    }

    // Evolve the authoritative surface before physics samples it. Keeping
    // this outside the raycast path also gives the triangle renderer and GPU
    // physics the same animated ocean instead of a never-updated texture.
    if (rendererSettings_.waterEnabled && waterSimulation_
        && waterSimulation_->isInitialized()) {
        frameGuard.water = waterSimulation_.get();
        constexpr uint32_t stage =
            static_cast<uint32_t>(RenderGpuStage::WaterSimulation);
        waterSimulation_->update(
            encoder,
            waterPhaseSeconds(),
            renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
            stage * kRenderGpuQueriesPerStage,
            stage * kRenderGpuQueriesPerStage + 1u);
    }

    // GPU physics writes persistent poses into this frame's command stream.
    // CPU backends intentionally no-op here.
    if (physicsWorld_) {
        if (frameGuard.physicsTicket.valid()) {
            const auto report=physicsWorld_->encodeGpuStepChecked(encoder);
            if (!report.succeeded()) {
                LOG_ERROR("Boat physics frame failed: {}",static_cast<int>(report.status)); requestExit(); return;
            }
        } else if(!retiredBoat) physicsWorld_->encodeGpuStep(encoder);
        encodeSalvageRetirement(encoder);
    }

    // Render based on active path
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            renderTrianglePath(encoder, targetView);
            break;
        case RenderPath::Raycast:
            if (!renderRaycastPath(encoder, targetView, frameGuard.ticket)) return;
            break;
    }

    if (config_.renderPath == RenderPath::Raycast
        && !config_.salvageAssetFixtureWaterAnchor
        && !config_.adventureEnabled
        && ((primitivePath_ && primitivePath_->isInitialized())
            || (meshPath_ && meshPath_->isInitialized()))) {
        clearRayObjectDepth(encoder);
    }

    if (primitivePath_ && primitivePath_->isInitialized() && physicsWorld_) {
        perf::Timer primitiveStageTimer;
        const auto capabilities = physicsWorld_->capabilities();
        size_t objectCount = physicsWorld_->stats().residentBodies;
        if (capabilities.gpuResidentState && capabilities.directRenderView) {
            // Persistent GPU buffers flow straight into culling/rendering.
            // No body snapshot or transform upload occurs on this path.
            primitivePath_->setPhysicsRenderView(physicsWorld_->renderView());
            stats_.physicsSnapshotMs = 0.0;
            stats_.primitiveBodyLockedReadCount = 0;
            stats_.primitiveBodyCachedReadCount = 0;
        } else {
            primitiveStageTimer.start();
            auto bodies = physicsWorld_->dynamicBodies();
            primitiveStageTimer.stop();
            stats_.physicsSnapshotMs = primitiveStageTimer.elapsedMs();
            const auto bodyReadStats = physicsWorld_->lastDynamicBodyReadStats();
            stats_.primitiveBodyLockedReadCount =
                static_cast<uint32_t>(bodyReadStats.lockedBodyCount);
            stats_.primitiveBodyCachedReadCount =
                static_cast<uint32_t>(bodyReadStats.cachedBodyCount);
            objectCount = bodies.size();
            primitivePath_->setCompactPhysicsInstances(bodies);
        }

        std::span<const physics::DynamicBodySnapshot> avatarProxies;
        if (wreckwaterClientState_) {
            avatarProxies =
                wreckwaterClientState_->client.visibleInstances();
        }
        std::vector<physics::PhysicsWorld::DynamicBodySnapshot> overlays;
        overlays.reserve(
            kPrimitiveOverlayHeadroom + avatarProxies.size() + (legoPlayground_ ? game::LegoPlayground::MaxDust + 8u : 0u));
        // The camera-locked throwable preview and body counter are developer
        // controls, not scene content. Keep them out of deterministic review
        // captures and benchmark workloads so visual evidence is clean and
        // performance measurements represent the game frame.
        const bool showPrimitiveHud =
            !wreckwaterClientState_
            && !config_.legoTerrainEnabled
            && !motoSession_
            && !config_.benchmarkOnStartup
            && !config_.screenshotPath.has_value()
            && !tourActive_;
        if (showPrimitiveHud) {
            const auto selectedShape =
                static_cast<physics::PhysicsWorld::ThrowableShape>(
                    selectedThrowable_);
            const float previewAngle = static_cast<float>(
                std::fmod(
                    stats_.totalTimeSeconds * 1.8,
                    2.0 * std::numbers::pi));
            const float previewDistance = 1.35f;
            const float viewportWidth = static_cast<float>(
                std::max(gpuContext_->getSwapchainWidth(), 1u));
            const float viewportHeight = static_cast<float>(
                std::max(gpuContext_->getSwapchainHeight(), 1u));
            const float halfHeight =
                std::tan(camera_->fovY() * 0.5f) * previewDistance;
            const float halfWidth =
                halfHeight * viewportWidth / viewportHeight;
            const float previewScale =
                std::min(halfWidth, halfHeight) * 0.22f;
            const glm::vec3 previewPlane =
                camera_->position()
                + camera_->forward() * previewDistance;
            const glm::quat cameraRotation = glm::quat_cast(glm::mat3(
                camera_->right(), camera_->up(), camera_->forward()));
            overlays.push_back({
                selectedShape,
                previewPlane
                    + camera_->right() * (halfWidth * 0.76f)
                    - camera_->up() * (halfHeight * 0.72f),
                glm::angleAxis(
                    previewAngle,
                    glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))),
                physics::PhysicsWorld::throwableShapeDimensions(
                    selectedShape) * previewScale});
            appendObjectCount(
                overlays, objectCount, previewPlane, camera_->right(),
                camera_->up(), cameraRotation, halfWidth, halfHeight);
        }
        overlays.insert(
            overlays.end(),
            avatarProxies.begin(), avatarProxies.end());
        if (salvagePreview_) {
            primitivePath_->setLegoBodyIds(salvagePreview_->legoBodyIds());
        } else if (!legoPlayground_) {
            primitivePath_->setLegoBodyIds({});
        }
        if (legoPlayground_) {
            primitivePath_->setLegoBodyIds(legoPlayground_->bodyIds());
            for (auto body : legoPlayground_->instances()) {
                // Overlay shader uses camera-sector local coordinates.
                body.position -= glm::vec3(camera_->worldSector()) * physics::kWorldSectorSize;
                overlays.push_back(body);
            }
        }
        const size_t submittedObjectCount =
            objectCount + avatarProxies.size();
        stats_.primitiveCullMs = 0.0;
        stats_.primitiveInputCount =
            static_cast<uint32_t>(submittedObjectCount);
        // The exact visible count remains GPU-resident by design.
        stats_.primitiveSubmittedCount =
            static_cast<uint32_t>(submittedObjectCount);
        stats_.primitiveCullRejectionRatio = 0.0f;
        stats_.primitiveCullEvaluated =
            submittedObjectCount != 0u;
        stats_.primitiveCullingEnabled = true;
        primitivePath_->setInstances(overlays);
        const auto& cpuTimings = primitivePath_->lastCpuTimings();
        stats_.primitivePackingMs = cpuTimings.packingMs;
        stats_.primitiveUploadMs = cpuTimings.uploadMs;
        const auto& uploadStats = primitivePath_->lastUploadStats();
        const auto& compactUploadStats =
            primitivePath_->lastCompactUploadStats();
        stats_.primitiveInstanceUploadBytes = uploadStats.bytesUploaded
                                            + compactUploadStats.bytesUploaded;
        stats_.primitiveInstanceUploadCalls = uploadStats.writeCalls
                                            + compactUploadStats.writeCalls;
        stats_.primitiveInstanceFullUpload = uploadStats.fullUpload
                                           || compactUploadStats.fullUpload;
        WGPUTextureView objectDepth = getOrCreateDepthView();
        if (objectDepth) {
            const render::PrimitiveLighting primitiveLighting{
                .direction = rendererSettings_.sunDirection,
                .sunColor = rendererSettings_.sunColor,
                .sunIntensity = rendererSettings_.sunIntensity,
                .ambientColor = rendererSettings_.ambientColor,
                .ambientIntensity = rendererSettings_.ambientIntensity,
                .fogColor = rendererSettings_.fogColor,
                .fogDensity = rendererSettings_.fogDensity,
                .exposure = rendererSettings_.exposure,
            };
            primitiveStageTimer.restart();
            primitivePath_->render(
                encoder, targetView, objectDepth, camera_->viewMatrix(),
                camera_->projectionMatrix(), camera_->position(),
                primitiveLighting,
                gpuContext_->getSwapchainWidth(), gpuContext_->getSwapchainHeight(),
                config_.renderPath == RenderPath::Raycast,
                camera_->worldSector(),
                renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage,
                static_cast<uint32_t>(RenderGpuStage::Primitives)
                    * kRenderGpuQueriesPerStage + 1u);
            primitiveStageTimer.stop();
            stats_.primitiveRenderMs = primitiveStageTimer.elapsedMs();
        }
    }

    renderMoto(encoder, targetView);
    try {
        if (!config_.salvageAssetFixtureWaterAnchor
            && !renderSalvageAsset(encoder, targetView, frameGuard.ticket)) return;
    } catch (const std::exception& failure) {
        LOG_ERROR("Asset preview frame failed: {}", failure.what());
        salvagePreviewFailed_ = true;
        requestExit();
        return; // Guard releases the encoder before acknowledging discard.
    }

    if(adventure_&&!adventure_->renderHud(encoder,targetView,gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight())) {
        LOG_ERROR("Could not draw adventure controls");requestExit();return;
    }
#if defined(VOXY_NATIVE)
    if(!renderNativeCoveHud(encoder,targetView)){
        LOG_ERROR("Could not draw the Cove controls");
        requestExit();return;
    }
#endif
    if (renderGpuProfilingFrame_ && writeFrameBoundary(
            encoder, renderGpuQuerySet_, kRenderGpuFrameEndQuery, false)) {
        wgpuCommandEncoderResolveQuerySet(
            encoder, renderGpuQuerySet_, 0u, kRenderGpuTimestampCount,
            renderGpuResolveBuffer_, 0u);
        static_cast<void>(renderGpuReadback_.encodeCopy(
            encoder, renderGpuResolveBuffer_, 0u,
            kRenderGpuTimestampCount * sizeof(uint64_t), stats_.frameCount,
            // Reuse this generic ring's metadata words to retain the sampled
            // internal resolution, rather than reporting the current viewport
            // after an asynchronous resize.
            raycastPath_->getOutputWidth(), raycastPath_->getOutputHeight()));
    }

    // Submit commands
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    frameGuard.command = cmdBuffer;
    if (!cmdBuffer) {
        LOG_ERROR("Failed to finish the frame command buffer");
        return;
    }
    if (frameGuard.physicsTicket.valid()) {
        if (physicsWorld_->submitGpuSubmission(frameGuard.physicsTicket,std::span{&cmdBuffer,1})
            != physics::ShapeResourceError::None) {
            LOG_ERROR("Boat frame submission rejected"); requestExit(); return;
        }
        frameGuard.physicsTicket={};
    } else {
#if defined(VOXY_NATIVE)
    if (config_.benchmarkOnStartup) {
        benchmarkSubmissionIndices_.push_back(wgpuQueueSubmitForIndex(
            gpuContext_->getQueue(), 1, &cmdBuffer));
    } else {
        wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
    }
#else
    wgpuQueueSubmit(gpuContext_->getQueue(), 1, &cmdBuffer);
#endif
    }

    frameGuard.submitted = true;
    if (coveWaterFrame) cove->waterClock = *coveWaterFrame;
    if(coveWaterFrame&&cove&&cove->effectsPath.initialized()&&cove->boatRoots) {
        const auto tick=cove->mechanisms.tick();
        cove->effectsDrives[tick%cove->effectsDrives.size()]={cove->mechanisms.incarnation(),tick,
            cove->boatRoot().body,cove->mechanisms.effectiveDrive(),waterPhaseSeconds()};
    }
    if (frameGuard.fixture && frameGuard.ticket.serial != 0) {
        std::string error;
        if (!frameGuard.fixture->submitted(frameGuard.ticket, error)) {
            LOG_ERROR("Asset preview submission acknowledgment failed: {}", error);
            salvagePreviewFailed_ = true;
            requestExit();
        }
        // The queue has accepted the command. Never classify it as discarded.
        frameGuard.ticket = {};
    }

    // Automated screenshots
    if (tourActive_) {
        if (tourFrameCounter_ >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {
            captureScreenshot(tourCurrentPath_);
            tourStep_++;
            scheduleNextTourStep();
        } else {
            tourFrameCounter_++;
        }
    } else if (config_.screenshotPath.has_value() &&
               stats_.frameCount >= static_cast<uint64_t>(config_.screenshotFrameDelay)) {

        captureScreenshot(config_.screenshotPath.value());
        config_.screenshotPath.reset(); // Capture once
        requestExit();
    }
}

void Application::endFrame() {
    if (input_) {
        input_->endFrame();
    }

    if (gpuContext_) {
        if (!config_.benchmarkOnStartup) {
            gpuContext_->present();
        }
#if defined(VOXY_NATIVE)
        if (config_.benchmarkOnStartup) {
            const bool scenarioBoundary = benchmarkRunner_
                && benchmarkRunner_->willCompleteScenarioAfterCurrentFrame();
            retireBenchmarkSubmissions(scenarioBoundary);
        }
#endif
        gpuContext_->tick();
    }

    stats_.frameCount++;
}

void Application::retireBenchmarkSubmissions(bool drain) {
#if defined(VOXY_NATIVE)
    if (!gpuContext_ || benchmarkSubmissionIndices_.empty()) return;
    constexpr size_t kMaximumFramesInFlight = 3u;
    if (drain) {
        const WGPUWrappedSubmissionIndex submission{
            gpuContext_->getQueue(), benchmarkSubmissionIndices_.back()};
        static_cast<void>(wgpuDevicePoll(
            gpuContext_->getDevice(), true, &submission));
        benchmarkSubmissionIndices_.clear();
        return;
    }
    if (benchmarkSubmissionIndices_.size() < kMaximumFramesInFlight) return;
    const WGPUWrappedSubmissionIndex submission{
        gpuContext_->getQueue(), benchmarkSubmissionIndices_.front()};
    static_cast<void>(wgpuDevicePoll(
        gpuContext_->getDevice(), true, &submission));
    benchmarkSubmissionIndices_.pop_front();
#else
    static_cast<void>(drain);
#endif
}

void Application::startScreenshotTour() {
    if (config_.screenshotTourIndices.empty()) {
        return;
    }

    // Ensure output directory exists
    std::error_code ec;
    std::filesystem::create_directories(config_.screenshotTourDir, ec);
    if (ec) {
        LOG_WARN("Failed to create screenshot tour directory '{}': {}", config_.screenshotTourDir.string(), ec.message());
    }

    tourActive_ = true;
    tourStep_ = 0;
    scheduleNextTourStep();
}

void Application::scheduleNextTourStep() {
    if (!tourActive_) {
        return;
    }

    while (tourStep_ < config_.screenshotTourIndices.size()) {
        const int index = config_.screenshotTourIndices[tourStep_];
        if (camera_ && index >= 0
            && static_cast<size_t>(index) < teleportTargets_.size()) {
            const auto& target = teleportTargets_[static_cast<size_t>(index)];
            camera_->setWorldPosition(target.sector, target.position);
            camera_->setYaw(target.yaw);
            camera_->setPitch(target.pitch);
            LOG_INFO(
                "Screenshot tour: teleported to index {} (step {})",
                index, tourStep_);
            tourCurrentPath_ = (
                config_.screenshotTourDir
                / ("view_" + std::to_string(tourStep_) + ".png")).string();
            tourFrameCounter_ = 0;
            return;
        }
        LOG_WARN(
            "Screenshot tour: invalid teleport index {} (step {}), skipping",
            index, tourStep_);
        tourStep_++;
    }

    tourActive_ = false;
    requestExit();
}

void Application::processFrame(float deltaTime) {
    processFrame(deltaTime, deltaTime);
}

void Application::processFrame(float simulationDeltaTime,
                               float frameDeltaTime) {
    // Static frame timer for performance instrumentation
    static perf::FrameTimer frameTimer;
    
    // Begin frame timing
    frameTimer.beginFrame();
    
    beginFrame();
    
    // Update phase
    update(simulationDeltaTime, frameDeltaTime);
    frameTimer.markUpdate();
    if (shouldExit()) {
        // A terminal update (including device loss) must not acquire another
        // swapchain texture or encode one last frame on failed resources.
        frameTimer.endFrame();
        return;
    }
    
    // Render phase
    render();
    if (!shouldExit() && captureCallback_) captureCallback_();
    frameTimer.markRender();
    
    // Present phase
    endFrame();
    frameTimer.markPresent();
    
    // End frame timing
    frameTimer.endFrame();

    if (browserJourneyBenchmark_) {
        const perf::FrameStats frameStats = frameTimer.getLastFrameStats();
        browserJourneyBenchmark_->recordFrame({
            .frame = stats_.frameCount,
            .physicsTick = physicsWorld_ ? physicsWorld_->encodedTick() : 0u,
            .phase = perf::BrowserJourneyStatus::Idle,
            .wallMilliseconds = static_cast<float>(stats_.frameTimeMs),
            .cpuMilliseconds = static_cast<float>(frameStats.totalMs),
            .residentBodies = stats_.physicsResidentBodies,
            .activeBodies = stats_.physicsActiveBodies,
            .candidatePairs = stats_.physics.candidatePairUsage.current,
            .contacts = stats_.physics.contactUsage.current,
            .terrainContactBodies = stats_.physics.terrainContactBodies,
            .submittedPrimitives = stats_.primitiveSubmittedCount,
            .capacityOverflowMask =
                (stats_.physics.candidatePairUsage.overflow ? 1u : 0u)
                | (stats_.physics.uniquePairUsage.overflow ? 2u : 0u)
                | (stats_.physics.contactUsage.overflow ? 4u : 0u)
                | (stats_.physics.overflowConstraintUsage.overflow
                       ? 8u : 0u),
            .physicsErrorMask =
                (stats_.physics.invalidManifolds != 0u ? 1u : 0u)
                | (stats_.physics.colorConflictErrors != 0u ? 2u : 0u)
                | (stats_.physics.islandRootErrors != 0u ? 4u : 0u)
                | (stats_.physics.ccdFailures != 0u ? 8u : 0u),
        });
    }
    
    // Update benchmark if running (use frame timer stats)
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        perf::FrameStats frameStats = frameTimer.getLastFrameStats();
        frameStats.physicsSimulationMs = stats_.physicsSimulationMs;
        frameStats.physicsWaterMs = stats_.physicsWaterMs;
        frameStats.physicsSnapshotMs = stats_.physicsSnapshotMs;
        frameStats.primitiveCullMs = stats_.primitiveCullMs;
        frameStats.primitivePackingMs = stats_.primitivePackingMs;
        frameStats.primitiveUploadMs = stats_.primitiveUploadMs;
        frameStats.primitiveRenderMs = stats_.primitiveRenderMs;
        frameStats.physicsResidentBodies = stats_.physicsResidentBodies;
        frameStats.physicsActiveBodies = stats_.physicsActiveBodies;
        frameStats.physicsActiveBodiesObserved =
            stats_.physicsBackend != physics::BackendType::WebGpuSoft
            || stats_.physics.telemetryTick != 0u;
        const bool stillRunning = benchmarkRunner_->onFrame(frameStats);
        if (!stillRunning && config_.exitAfterBenchmark) {
            requestExit();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Path Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setRenderPath(RenderPath path) {
    if ((config_.salvageAssetFixtureWaterAnchor || config_.adventureEnabled) && path != RenderPath::Raycast) {
        LOG_WARN("World construction requires the opaque/water raycast composition path");
        return;
    }
    if (path != RenderPath::Triangle && path != RenderPath::Raycast) {
        LOG_ERROR("Ignoring invalid render path {}",
                  static_cast<int>(path));
        return;
    }
    if (config_.renderPath != path) {
        config_.renderPath = path;
        stats_.activeRenderPath = path;
        LOG_INFO("Render path changed to: {}", renderPathToString(path));
    }
}

void Application::toggleRenderPath() {
    switch (config_.renderPath) {
        case RenderPath::Triangle:
            setRenderPath(RenderPath::Raycast);
            break;
        case RenderPath::Raycast:
            setRenderPath(RenderPath::Triangle);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Uncapped FPS Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::toggleUncappedFPS() {
    const bool nextUncapped = !uncappedFPS_;
    if (gpuContext_
        && !gpuContext_->setPresentMode(
            nextUncapped ? WGPUPresentMode_Immediate
                         : WGPUPresentMode_Fifo)) {
        LOG_WARN("Requested presentation behavior is unavailable");
        return;
    }
    uncappedFPS_ = nextUncapped;
    config_.vsync = !nextUncapped;
    LOG_INFO("Uncapped FPS mode: {}", uncappedFPS_
        ? "ENABLED (immediate presentation)"
        : "DISABLED (FIFO VSync)");

#if defined(VOXY_WASM)
    // Loop strategy update is handled by the platform entry point (entry.cpp) via isUncappedFPS()
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Resize Handling
// ─────────────────────────────────────────────────────────────────────────────

void Application::onResize(uint32_t width, uint32_t height) {
    LOG_DEBUG("Application resize: {}x{}", width, height);
    
    // Ignore zero-size (minimized window)
    if (width == 0 || height == 0) {
        return;
    }
    const uint32_t renderWidth =
        scaledRenderExtent(width, config_.resolutionScale);
    const uint32_t renderHeight =
        scaledRenderExtent(height, config_.resolutionScale);

    // The benchmark color attachment must remain the same size as the depth
    // attachment. Its replacement is transactional, so stop before committing
    // any other size when allocation fails.
    if (config_.benchmarkOnStartup
        && !createBenchmarkTarget(renderWidth, renderHeight)) {
        LOG_ERROR("Failed to resize the benchmark render target to {}x{}",
                  renderWidth, renderHeight);
        return;
    }

    if (gpuContext_
        && !gpuContext_->resizeSwapchain(renderWidth, renderHeight)) {
        LOG_ERROR("Failed to resize the swapchain to {}x{}",
                  renderWidth, renderHeight);
        return;
    }

    bool raycastResizeSucceeded = true;
    if (raycastPath_) {
        raycastResizeSucceeded =
            raycastPath_->resize(renderWidth, renderHeight);
    }
    if (raycastResizeSucceeded && blitPath_) {
        raycastResizeSucceeded =
            blitPath_->resize(renderWidth, renderHeight);
    }
    if (raycastPath_) {
        // Rebind every borrowed view after a successful ray-output replacement.
        // This is also required when the later blit-cache allocation failed.
        if (blitPath_) {
            blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
            blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
            blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
            blitPath_->setStaticTerrainTextures(
                raycastPath_->getTerrainDepthCacheView(),
                raycastPath_->getTerrainShadowCacheView());
        }
        if (primitivePath_) {
            primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
        }
        if (meshPath_) {
            meshPath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
        }
        if (salvageLocalSession_ && salvageLocalSession_->asset)
            salvageLocalSession_->asset->viewsDirty = true;
    }

    if (!raycastResizeSucceeded && (config_.salvageAssetFixtureWaterAnchor || config_.adventureEnabled)) {
        LOG_ERROR("World construction targets could not resize to {}x{}", renderWidth, renderHeight);
        salvagePreviewFailed_ = true;
        requestExit();
        return;
    }
    if (!raycastResizeSucceeded) {
        LOG_ERROR(
            "Raycast renderer resize failed at {}x{}; using the independent triangle path",
            renderWidth, renderHeight);
        if (config_.renderPath == RenderPath::Raycast) {
            setRenderPath(RenderPath::Triangle);
        }
    }

    // Projection must match the actual render target after independent extent
    // rounding, especially at fractional resolution scales.
    if (camera_) {
        camera_->setAspectRatio(renderWidth, renderHeight);
    }
    
    // Invalidate depth buffer for triangle path
    depthWidth_ = 0;
    depthHeight_ = 0;
}

bool Application::setRendererSetting(std::string_view name, double value,
                                     bool commit) {
    if (!std::isfinite(value)) return false;
    uint32_t dirty = RendererUniformsDirty;
    auto setScalar = [&](float& destination, float minimum, float maximum) {
        destination = finiteClamp(value, minimum, maximum);
    };
    auto setColor = [&](float& destination) {
        setScalar(destination, 0.0f, 4.0f);
    };

    if (name == "render.path") {
        if (value != 0.0 && value != 1.0) return false;
        setRenderPath(value == 0.0 ? RenderPath::Triangle
                                   : RenderPath::Raycast);
    } else if (name == "lighting.sunAzimuth") {
        const float azimuth = finiteClamp(value, -180.0f, 180.0f);
        rendererSettings_.sunDirection = sunDirectionFromDegrees(
            azimuth, sunElevationDegrees(rendererSettings_.sunDirection));
        if (commit) dirty |= RendererSunShadowDirty;
    } else if (name == "lighting.sunElevation") {
        const float elevation = finiteClamp(value, 1.0f, 89.0f);
        rendererSettings_.sunDirection = sunDirectionFromDegrees(
            sunAzimuthDegrees(rendererSettings_.sunDirection), elevation);
        if (commit) dirty |= RendererSunShadowDirty;
    } else if (name == "lighting.sunColor.r") {
        setColor(rendererSettings_.sunColor.r);
    } else if (name == "lighting.sunColor.g") {
        setColor(rendererSettings_.sunColor.g);
    } else if (name == "lighting.sunColor.b") {
        setColor(rendererSettings_.sunColor.b);
    } else if (name == "lighting.sunIntensity") {
        setScalar(rendererSettings_.sunIntensity, 0.0f, 10.0f);
    } else if (name == "lighting.ambientColor.r") {
        setColor(rendererSettings_.ambientColor.r);
    } else if (name == "lighting.ambientColor.g") {
        setColor(rendererSettings_.ambientColor.g);
    } else if (name == "lighting.ambientColor.b") {
        setColor(rendererSettings_.ambientColor.b);
    } else if (name == "lighting.ambientIntensity") {
        setScalar(rendererSettings_.ambientIntensity, 0.0f, 4.0f);
    } else if (name == "lighting.fogColor.r") {
        setColor(rendererSettings_.fogColor.r);
    } else if (name == "lighting.fogColor.g") {
        setColor(rendererSettings_.fogColor.g);
    } else if (name == "lighting.fogColor.b") {
        setColor(rendererSettings_.fogColor.b);
    } else if (name == "lighting.fogDensity") {
        setScalar(rendererSettings_.fogDensity, 0.0f, 0.01f);
    } else if (name == "lighting.exposure") {
        setScalar(rendererSettings_.exposure, 0.05f, 8.0f);
    } else if (name == "water.enabled") {
        rendererSettings_.waterEnabled = value >= 0.5;
        dirty |= RendererWaterPhysicsDirty;
    } else if (name == "water.height") {
        setScalar(rendererSettings_.waterHeight, -4000.0f, 4000.0f);
        dirty |= RendererWaterPhysicsDirty;
        if (commit) dirty |= RendererWaterCoastDirty;
    } else if (name == "water.shallowColor.r") {
        setColor(rendererSettings_.waterShallowColor.r);
    } else if (name == "water.shallowColor.g") {
        setColor(rendererSettings_.waterShallowColor.g);
    } else if (name == "water.shallowColor.b") {
        setColor(rendererSettings_.waterShallowColor.b);
    } else if (name == "water.deepColor.r") {
        setColor(rendererSettings_.waterDeepColor.r);
    } else if (name == "water.deepColor.g") {
        setColor(rendererSettings_.waterDeepColor.g);
    } else if (name == "water.deepColor.b") {
        setColor(rendererSettings_.waterDeepColor.b);
    } else if (name == "water.roughness") {
        setScalar(rendererSettings_.waterRoughness, 0.02f, 1.0f);
    } else if (name == "water.waveStrength") {
        setScalar(rendererSettings_.waterWaveStrength, 0.0f, 2.0f);
        dirty |= RendererWaterPhysicsDirty;
    } else if (name == "water.reflectionStrength") {
        setScalar(rendererSettings_.waterReflectionStrength, 0.0f, 1.0f);
    } else if (name == "water.shoreFade") {
        setScalar(rendererSettings_.waterShoreFade, 0.01f, 500.0f);
    } else if (name == "water.ior") {
        setScalar(rendererSettings_.waterIor, 1.0f, 2.0f);
    } else if (name == "water.distortion") {
        setScalar(rendererSettings_.waterDistortion, 0.0f, 1.0f);
    } else if (name == "water.absorptionScale") {
        setScalar(rendererSettings_.waterAbsorptionScale, 0.0f, 5.0f);
    } else if (name == "water.scatterStrength") {
        setScalar(rendererSettings_.waterScatterStrength, 0.0f, 5.0f);
    } else if (name == "water.foamSize") {
        setScalar(rendererSettings_.waterFoamSize, 8.0f, 2000.0f);
    } else if (name == "water.foamOpacity") {
        setScalar(rendererSettings_.waterFoamOpacity, 0.0f, 1.0f);
    } else if (name == "water.foamCoverage") {
        setScalar(rendererSettings_.waterFoamCoverage, 0.0f, 1.0f);
    } else if (name == "water.reflectionDistance") {
        setScalar(rendererSettings_.waterReflectionDistance, 10.0f, 20000.0f);
    } else if (name == "water.spectrum.significantHeight") {
        setScalar(rendererSettings_.waterSpectrum.significantWaveHeight, 0.1f, 100.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.direction") {
        setScalar(rendererSettings_.waterSpectrum.directionDegrees, -180.0f, 180.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.choppiness") {
        setScalar(rendererSettings_.waterSpectrum.choppiness, 0.0f, 5.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.peakEnhancement") {
        setScalar(rendererSettings_.waterSpectrum.peakEnhancement, 0.05f, 10.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.windAlignment") {
        setScalar(rendererSettings_.waterSpectrum.windAlignment, 0.0f, 1.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.speed") {
        setScalar(rendererSettings_.waterSpectrum.animationSpeed, 0.0f, 5.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.largePatch") {
        setScalar(rendererSettings_.waterSpectrum.patchLengths.x, 64.0f, 8192.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.detailPatch") {
        setScalar(rendererSettings_.waterSpectrum.patchLengths.y, 16.0f, 2048.0f);
        if (commit) dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.largeAmplitude") {
        setScalar(rendererSettings_.waterSpectrum.cascadeAmplitudes.x, 0.0f, 2.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.detailAmplitude") {
        setScalar(rendererSettings_.waterSpectrum.cascadeAmplitudes.y, 0.0f, 2.0f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "water.spectrum.directionalSine") {
        setScalar(rendererSettings_.waterSpectrum.directionalSineScale, 0.0f, 1.5f);
        dirty |= RendererWaterSpectrumDirty;
    } else if (name == "camera.fov") {
        setScalar(rendererSettings_.cameraFovDegrees, 20.0f, 120.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.near") {
        setScalar(rendererSettings_.cameraNear, 0.01f,
                  std::max(rendererSettings_.cameraFar - 0.01f, 0.01f));
        dirty |= RendererCameraDirty;
    } else if (name == "camera.far") {
        setScalar(rendererSettings_.cameraFar,
                  rendererSettings_.cameraNear + 0.01f, 100000.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.moveSpeed") {
        setScalar(rendererSettings_.cameraMoveSpeed, 0.1f, 1000.0f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.mouseSensitivity") {
        setScalar(rendererSettings_.cameraMouseSensitivity, 0.00001f, 0.02f);
        dirty |= RendererCameraDirty;
    } else if (name == "camera.eyeHeight") {
        setScalar(rendererSettings_.cameraEyeHeight, 0.5f, 10.0f);
        dirty |= RendererCameraDirty;
    } else {
        return false;
    }

    rendererSettingsDirty_ |= dirty;
    ++rendererSettingsRevision_;
    return true;
}

std::optional<double> Application::getRendererSetting(
    std::string_view name) const noexcept {
    const auto& s = rendererSettings_;
    if (name == "render.path") {
        return static_cast<double>(config_.renderPath);
    }
    if (name == "lighting.sunAzimuth") return sunAzimuthDegrees(s.sunDirection);
    if (name == "lighting.sunElevation") return sunElevationDegrees(s.sunDirection);
    if (name == "lighting.sunColor.r") return s.sunColor.r;
    if (name == "lighting.sunColor.g") return s.sunColor.g;
    if (name == "lighting.sunColor.b") return s.sunColor.b;
    if (name == "lighting.sunIntensity") return s.sunIntensity;
    if (name == "lighting.ambientColor.r") return s.ambientColor.r;
    if (name == "lighting.ambientColor.g") return s.ambientColor.g;
    if (name == "lighting.ambientColor.b") return s.ambientColor.b;
    if (name == "lighting.ambientIntensity") return s.ambientIntensity;
    if (name == "lighting.fogColor.r") return s.fogColor.r;
    if (name == "lighting.fogColor.g") return s.fogColor.g;
    if (name == "lighting.fogColor.b") return s.fogColor.b;
    if (name == "lighting.fogDensity") return s.fogDensity;
    if (name == "lighting.exposure") return s.exposure;
    if (name == "water.enabled") return s.waterEnabled ? 1.0 : 0.0;
    if (name == "water.height") return s.waterHeight;
    if (name == "water.shallowColor.r") return s.waterShallowColor.r;
    if (name == "water.shallowColor.g") return s.waterShallowColor.g;
    if (name == "water.shallowColor.b") return s.waterShallowColor.b;
    if (name == "water.deepColor.r") return s.waterDeepColor.r;
    if (name == "water.deepColor.g") return s.waterDeepColor.g;
    if (name == "water.deepColor.b") return s.waterDeepColor.b;
    if (name == "water.roughness") return s.waterRoughness;
    if (name == "water.waveStrength") return s.waterWaveStrength;
    if (name == "water.reflectionStrength") return s.waterReflectionStrength;
    if (name == "water.shoreFade") return s.waterShoreFade;
    if (name == "water.ior") return s.waterIor;
    if (name == "water.distortion") return s.waterDistortion;
    if (name == "water.absorptionScale") return s.waterAbsorptionScale;
    if (name == "water.scatterStrength") return s.waterScatterStrength;
    if (name == "water.foamSize") return s.waterFoamSize;
    if (name == "water.foamOpacity") return s.waterFoamOpacity;
    if (name == "water.foamCoverage") return s.waterFoamCoverage;
    if (name == "water.reflectionDistance") return s.waterReflectionDistance;
    if (name == "water.spectrum.significantHeight") return s.waterSpectrum.significantWaveHeight;
    if (name == "water.spectrum.direction") return s.waterSpectrum.directionDegrees;
    if (name == "water.spectrum.choppiness") return s.waterSpectrum.choppiness;
    if (name == "water.spectrum.peakEnhancement") return s.waterSpectrum.peakEnhancement;
    if (name == "water.spectrum.windAlignment") return s.waterSpectrum.windAlignment;
    if (name == "water.spectrum.speed") return s.waterSpectrum.animationSpeed;
    if (name == "water.spectrum.largePatch") return s.waterSpectrum.patchLengths.x;
    if (name == "water.spectrum.detailPatch") return s.waterSpectrum.patchLengths.y;
    if (name == "water.spectrum.largeAmplitude") return s.waterSpectrum.cascadeAmplitudes.x;
    if (name == "water.spectrum.detailAmplitude") return s.waterSpectrum.cascadeAmplitudes.y;
    if (name == "water.spectrum.directionalSine") return s.waterSpectrum.directionalSineScale;
    if (name == "camera.fov") return s.cameraFovDegrees;
    if (name == "camera.near") return s.cameraNear;
    if (name == "camera.far") return s.cameraFar;
    if (name == "camera.moveSpeed") return s.cameraMoveSpeed;
    if (name == "camera.mouseSensitivity") return s.cameraMouseSensitivity;
    if (name == "camera.eyeHeight") return s.cameraEyeHeight;
    return std::nullopt;
}

void Application::applyRendererSettings() {
    if (rendererSettingsDirty_ == 0u) return;
    const uint32_t dirty = rendererSettingsDirty_;
    rendererSettingsDirty_ = 0u;

    if ((dirty & RendererCameraDirty) != 0u) {
        if (camera_) {
            camera_->setFovYDegrees(rendererSettings_.cameraFovDegrees);
            camera_->setClipPlanes(rendererSettings_.cameraNear,
                                   rendererSettings_.cameraFar);
        }
        if (freeFlyController_) {
            freeFlyController_->setBaseSpeed(rendererSettings_.cameraMoveSpeed);
            freeFlyController_->setMouseSensitivity(
                rendererSettings_.cameraMouseSensitivity);
        }
        if (characterController_) {
            CharacterConfig character = characterController_->config();
            character.walkSpeed = rendererSettings_.cameraMoveSpeed;
            character.runSpeed = rendererSettings_.cameraMoveSpeed * 2.0f;
            character.mouseSensitivity = rendererSettings_.cameraMouseSensitivity;
            character.groundOffset = rendererSettings_.cameraEyeHeight;
            character.collisionHeight = std::max(
                rendererSettings_.cameraEyeHeight, 0.82f);
            characterController_->setConfig(character);
        }
    }

    if ((dirty & RendererWaterSpectrumDirty) != 0u && waterSimulation_) {
        if (!waterSimulation_->reconfigure(makeWaterSpectrumConfig(
                rendererSettings_.waterSpectrum))) {
            LOG_ERROR("Runtime water-spectrum update failed");
            // Keep uniforms and CPU/GPU physics matched to the spectrum which
            // is still live after the transactional rebuild failed.
            rendererSettings_.waterSpectrum = makeWaterSpectrumSettings(
                waterSimulation_->spectrumConfig());
        }
        updateWaterPhysicsBindings();
    } else if ((dirty & RendererWaterPhysicsDirty) != 0u) {
        updateWaterPhysicsBindings();
    }

    if ((dirty & RendererWaterCoastDirty) != 0u &&
        !rebuildWaterCoastField()) {
        LOG_ERROR("Runtime coastal-field update failed");
        rendererSettings_.waterHeight = appliedWaterCoastHeight_;
        updateWaterPhysicsBindings();
    }
    if ((dirty & RendererSunShadowDirty) != 0u &&
        !rebuildSunShadowMap()) {
        LOG_ERROR("Runtime sun-shadow update failed");
        rendererSettings_.sunDirection = appliedShadowSunDirection_;
    }

    appliedRendererSettingsRevision_ = rendererSettingsRevision_;
}

void Application::updateWaterPhysicsBindings() {
    if (!physicsWorld_) return;
    physicsWorld_->setWaterPlane(rendererSettings_.waterHeight,
                                 rendererSettings_.waterEnabled);
    if (!waterSimulation_) return;

    const float strength = rendererSettings_.waterWaveStrength;
    if (physicsWorld_->backendType() == physics::BackendType::WebGpuSoft) {
        physicsWorld_->setWaterGpuResources({
            waterSimulation_->getOutputView(), waterSimulation_->getSampler(),
            strength,
            rendererSettings_.waterSpectrum.patchLengths.x,
            rendererSettings_.waterSpectrum.patchLengths.y});
    } else {
        physicsWorld_->setWaterSurfaceSampler(
            [simulation = waterSimulation_.get(), strength](
                glm::vec2 position, float timeSeconds) {
                const auto sample = simulation->sampleSurface(
                    position, timeSeconds, strength);
                return physics::PhysicsWorld::WaterSurfaceSample{
                    sample.heightOffset, sample.slope, sample.velocity};
            });
    }
}

bool Application::rebuildWaterCoastField() {
    if (!waterSimulation_ || !heightmap_) return false;
    if (!waterSimulation_->rebuildCoastField(
            heightmap_->getData(), heightmap_->getWidth(),
            heightmap_->getHeight(), config_.heightScale,
            config_.cellScale, rendererSettings_.waterHeight)) {
        return false;
    }
    if (raycastPath_) {
        raycastPath_->setWaterSimulation(
            waterSimulation_->getOutputView(),
            waterSimulation_->getCoastView(),
            waterSimulation_->getSampler());
    }
    appliedWaterCoastHeight_ = rendererSettings_.waterHeight;
    return true;
}

bool Application::rebuildSunShadowMap() {
    if (!gpuContext_ || !heightmap_ || !raycastPath_) return false;

    perf::Timer bakeTimer;
    bakeTimer.start();
    terrain::ShadowBakeConfig bakeConfig;
    bakeConfig.lightDir = rendererSettings_.sunDirection;
    bakeConfig.heightScale = config_.heightScale;
    bakeConfig.cellScale = config_.cellScale;
    const auto baked = terrain::bakeShadowHeightField(
        heightmap_->getData(), heightmap_->getWidth(),
        heightmap_->getHeight(), bakeConfig);
    if (baked.data.empty()) return false;

    const WGPUDevice device = gpuContext_->getDevice();
    const WGPUQueue queue = gpuContext_->getQueue();
    gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
        baked.width, baked.height, WGPUTextureFormat_R16Uint,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "baked_shadow_height_runtime");
    WGPUTexture nextTexture = gpu::createTextureWithData(
        device, queue, desc,
        std::as_bytes(std::span<const uint16_t>(baked.data)),
        baked.width * sizeof(uint16_t));
    if (!nextTexture) return false;
    WGPUTextureView nextView = gpu::createTextureView(nextTexture);
    if (!nextView) {
        wgpuTextureRelease(nextTexture);
        return false;
    }

    raycastPath_->setShadowMap(nextView);
    if (blitPath_ && waterSimulation_) {
        blitPath_->setWaterCompositeResources(
            heightmap_->getTextureView(), nextView,
            waterSimulation_->getOutputView(),
            waterSimulation_->getSampler());
    }

    if (shadowMapView_) wgpuTextureViewRelease(shadowMapView_);
    if (shadowMapTexture_) wgpuTextureRelease(shadowMapTexture_);
    shadowMapTexture_ = nextTexture;
    shadowMapView_ = nextView;
    appliedShadowSunDirection_ = rendererSettings_.sunDirection;
    LOG_INFO("Rebuilt sun shadow field: {}x{} ({:.1f} ms)",
             baked.width, baked.height, bakeTimer.elapsedMs());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization Control
// ─────────────────────────────────────────────────────────────────────────────

void Application::setDebugVisMode(DebugVisMode mode) {
    if (debugVisMode_ != mode) {
        debugVisMode_ = mode;
        LOG_INFO("Debug visualization mode: {}", debugVisModeToString(mode));
        
        // Update blit path with new debug mode
        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setDebugMode(static_cast<uint32_t>(mode));
        }
    }
}

void Application::cycleDebugVisMode() {
    uint32_t nextMode = (static_cast<uint32_t>(debugVisMode_) + 1) % 4;
    setDebugVisMode(static_cast<DebugVisMode>(nextMode));
}

void Application::toggleWireframe() {
    wireframeEnabled_ = !wireframeEnabled_;
    LOG_INFO("Wireframe mode: {}", wireframeEnabled_ ? "enabled" : "disabled");
    
    // Update triangle path wireframe state
    if (trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setWireframe(wireframeEnabled_);
    }
}

void Application::toggleLegoMode() {
    if (config_.salvagePreviewEnabled) return;
    const bool next = !legoMode_;
    if (!config_.legoTerrainEnabled) {
        if (!heightmap_ || !physicsWorld_
            || physicsWorld_->backendType() != physics::BackendType::WebGpuSoft) {
            LOG_WARN("LEGO terrain requires the WebGPU physics backend");
            return;
        }
        if (next && !initLegoLayout()) return;
        const auto attach = [&](bool lego) {
            return lego ? physicsWorld_->setLegoTerrain(heightmap_->getData(),
                heightmap_->getWidth(),heightmap_->getHeight(),config_.heightScale,config_.cellScale)
                : physicsWorld_->setTerrain(heightmap_->getData(),heightmap_->getWidth(),
                    heightmap_->getHeight(),config_.heightScale,config_.cellScale);
        };
        if (!attach(next)) {
            static_cast<void>(attach(legoMode_));
            LOG_ERROR("Could not switch terrain collision surface");
            return;
        }
        if (characterController_) {
            auto character = characterController_->config();
            character.legoTerrain = next;
            characterController_->setConfig(character);
        }
    }
    legoMode_ = next;
    LOG_INFO("LEGO appearance: {}", legoMode_ ? "grouped bricks"
        : config_.legoTerrainEnabled ? "single bricks" : "landscape");
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark Mode
// ─────────────────────────────────────────────────────────────────────────────

bool Application::spawnBenchmarkBodies() {
    const uint32_t bodyCount = config_.benchmarkBodyCount;
    if (bodyCount == 0u) return true;
    if (!physicsWorld_ || !characterController_) return false;

    uint32_t columns = 1u;
    while (uint64_t{columns} * columns < bodyCount) ++columns;
    const uint32_t rows = (bodyCount + columns - 1u) / columns;
    constexpr float spacing = 2.2f;
    const float halfColumns = 0.5f * static_cast<float>(columns - 1u);
    const float halfRows = 0.5f * static_cast<float>(rows - 1u);
    constexpr uint32_t shapeCount = static_cast<uint32_t>(
        physics::ThrowableShape::Count);

    for (uint32_t index = 0u; index < bodyCount; ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const float x = (static_cast<float>(column) - halfColumns) * spacing;
        const float z = (static_cast<float>(row) - halfRows) * spacing;
        const float terrainHeight =
            characterController_->sampleTerrainHeight(x, z);

        physics::BodySpawnDesc body;
        body.shape = static_cast<physics::ThrowableShape>(index % shapeCount);
        // Keep every body active for the complete five-scenario benchmark.
        // At 400 Hz the 1,500 frames advance 3.75 simulated seconds; the
        // initial downward velocity plus gravity covers less than 73 metres.
        body.position = {x, terrainHeight + 100.0f, z};
        body.linearVelocity = {
            static_cast<float>(static_cast<int32_t>(index % 7u) - 3) * 0.2f,
            -1.0f,
            static_cast<float>(static_cast<int32_t>(index % 5u) - 2) * 0.2f};
        body.angularVelocity = {0.1f, 0.2f, 0.05f};
        body.dimensions = physics::throwableShapeDimensions(body.shape);
        if (!physicsWorld_->spawnBody(body).valid()) return false;
    }

    LOG_INFO("Spawned {} benchmark bodies 100 m above terrain with linear and angular velocity",
             bodyCount);
    return true;
}

bool Application::spawnCubePyramidExperiment() {
    const uint32_t bodyCount = config_.cubePyramidBodyCount;
    if (bodyCount == 0u) return true;
    if (!physicsWorld_ || !characterController_ || !camera_) return false;

    // One cube of thickness. Row widths are 2n, 2(n-1), ... 2, so a complete
    // n-row wall contains n(n+1) cubes and has 45-degree sides.
    constexpr float cubeSize = kCubeTriangleCubeSize;
    // Leave 160 mm between columns, safely outside the 20 mm
    // speculative-contact distance. A single impact then wakes the struck
    // stacks instead of instantly turning all 20k staged cubes into one
    // solver island. The explicit 128-projectile benchmark still exercises
    // a wide collapse.
    constexpr float horizontalPitch = kCubeTriangleHorizontalPitch;
    // Exact face contact keeps the staged towers physically supported. Before
    // an impact, selected complete columns are woken together so removing a
    // lower cube cannot strand a still-sleeping cube above it.
    constexpr float verticalPitch = cubeSize;
    constexpr float terrainClearance = 0.05f;

    struct TriangleRow {
        uint32_t width = 0u;
        uint32_t bodies = 0u;
    };
    std::vector<TriangleRow> rows;
    if (bodyCount == kDefaultCubeTriangleBodyCount) {
        // 141 * 142 = 20,022: the closest n(n+1) triangle to 20,000.
        static_assert(
            kDefaultCubeTriangleRowCount
                * (kDefaultCubeTriangleRowCount + 1u)
            == kDefaultCubeTriangleBodyCount);
        rows.reserve(kDefaultCubeTriangleRowCount);
        for (uint32_t halfWidth = kDefaultCubeTriangleRowCount;
             halfWidth != 0u; --halfWidth) {
            const uint32_t width = 2u * halfWidth;
            rows.push_back({width, width});
        }
    } else {
        const auto completeTriangleBodies = [](uint32_t rowCount) {
            return uint64_t{rowCount} * (rowCount + 1u);
        };
        uint32_t rowCount = 1u;
        while (completeTriangleBodies(rowCount) < bodyCount) ++rowCount;
        rows.reserve(rowCount);
        uint32_t remaining = bodyCount;
        for (uint32_t halfWidth = rowCount;
             remaining != 0u; --halfWidth) {
            const uint32_t width = 2u * halfWidth;
            const uint32_t capacity = width;
            const uint32_t rowBodies = std::min(remaining, capacity);
            rows.push_back({width, rowBodies});
            remaining -= rowBodies;
        }
    }
    if (rows.empty()) return false;
    const uint32_t baseWidth = rows.front().width;
    cubeTriangleColumns_.assign(baseWidth, {});
    cubeTriangleColumnsWokenThisTick_.assign(baseWidth, 0u);
    cubeTriangleWakeDedupTick_ = std::numeric_limits<uint64_t>::max();

    const float halfWidth =
        0.5f * static_cast<float>(baseWidth - 1u) * horizontalPitch
        + cubeSize * 0.5f;
    const float halfDepth = cubeSize * 0.5f;

    // Find a horizontal plane above the complete footprint. The experiment
    // already flattened this runtime heightmap for render and collision. The
    // static bottom row remains a cube-built foundation, with no hidden shape.
    float maximumTerrainHeight = -std::numeric_limits<float>::infinity();
    const uint32_t xSampleIntervals = std::max(baseWidth * 2u, 1u);
    constexpr uint32_t zSampleIntervals = 2u;
    for (uint32_t z = 0u; z <= zSampleIntervals; ++z) {
        const float zOffset = -halfDepth + 2.0f * halfDepth
                * static_cast<float>(z)
                / static_cast<float>(zSampleIntervals);
        for (uint32_t x = 0u; x <= xSampleIntervals; ++x) {
            const float xOffset = -halfWidth + 2.0f * halfWidth
                    * static_cast<float>(x)
                    / static_cast<float>(xSampleIntervals);
            const float height = characterController_->sampleTerrainHeight(
                kBrowserJourneyTargetX + xOffset,
                kBrowserJourneyTargetZ + zOffset);
            if (std::isfinite(height)) {
                maximumTerrainHeight =
                    std::max(maximumTerrainHeight, height);
            }
        }
    }
    if (!std::isfinite(maximumTerrainHeight)) return false;

    const float baseCenterY =
        maximumTerrainHeight + terrainClearance + cubeSize * 0.5f;
    const physics::PhysicsMaterial triangleMaterial{
        .friction = 0.85f,
        .restitution = 0.0f,
        .rollingResistance = 0.02f,
        .density = 1.0f,
        .flags = 0u,
    };

    uint32_t spawned = 0u;
    std::vector<physics::PhysicsCommand> initialSleepCommands;
    initialSleepCommands.reserve(
        bodyCount - std::min(bodyCount, rows.front().bodies));
    for (size_t row = 0u; row < rows.size(); ++row) {
        const uint32_t width = rows[row].width;
        const uint32_t capacity = width;
        const uint32_t rowBodies = rows[row].bodies;
        std::vector<uint32_t> slots(capacity);
        for (uint32_t slot = 0u; slot < capacity; ++slot) {
            slots[slot] = slot;
        }

        // Exploratory non-20k counts can end in a partial row. Fill it from
        // the center out so the visible triangle stays balanced.
        if (rowBodies != capacity) {
            std::stable_sort(
                slots.begin(), slots.end(),
                [width](uint32_t lhs, uint32_t rhs) {
                    const auto distanceFromCenter = [width](uint32_t slot) {
                        return std::abs(
                            static_cast<int32_t>(2u * slot)
                            - static_cast<int32_t>(width - 1u));
                    };
                    const int32_t lhsDistance = distanceFromCenter(lhs);
                    const int32_t rhsDistance = distanceFromCenter(rhs);
                    return lhsDistance != rhsDistance
                        ? lhsDistance < rhsDistance : lhs < rhs;
                });
        }

        const float rowHalf =
            0.5f * static_cast<float>(width - 1u) * horizontalPitch;
        for (uint32_t index = 0u; index < rowBodies; ++index) {
            const uint32_t slot = slots[index];
            const uint32_t column = slot;
            const uint32_t foundationColumn =
                static_cast<uint32_t>(row) + column;

            physics::BodySpawnDesc body;
            body.shape = physics::ThrowableShape::Cube;
            body.position = {
                kBrowserJourneyTargetX
                    + static_cast<float>(column) * horizontalPitch
                    - rowHalf,
                baseCenterY + static_cast<float>(row) * verticalPitch,
                kBrowserJourneyTargetZ,
            };
            body.dimensions = glm::vec3(cubeSize);
            body.inverseMass = row == 0u ? 0.0f : 1.0f;
            body.material = triangleMaterial;
            const physics::BodyHandle handle = physicsWorld_->spawnBody(body);
            if (!handle.valid()) {
                LOG_ERROR(
                    "Cube triangle stopped at body {} of {}",
                    spawned, bodyCount);
                return false;
            }
            if (row != 0u) {
                cubeTriangleColumns_[foundationColumn].push_back(handle);
                physics::PhysicsCommand sleep;
                sleep.type = physics::PhysicsCommandType::Sleep;
                sleep.body = handle;
                initialSleepCommands.push_back(sleep);
            }
            ++spawned;
        }

    }
    physicsWorld_->enqueue(initialSleepCommands);

    const float triangleHeight =
        cubeSize + static_cast<float>(rows.size() - 1u) * verticalPitch;
    const float viewDistance = std::max(80.0f, halfWidth * 1.45f);
    const glm::dvec3 target{
        kBrowserJourneyTargetX,
        baseCenterY + triangleHeight * 0.46f,
        kBrowserJourneyTargetZ};
    const glm::dvec3 position{
        kBrowserJourneyTargetX - halfWidth * 0.12f,
        baseCenterY + triangleHeight * 0.52f,
        kBrowserJourneyTargetZ - viewDistance};
    setCameraWorldPose(*camera_, position, target);
    controllerMode_ = ControllerMode::FreeFly;
    stats_.activeController = controllerMode_;
    selectedThrowable_ =
        static_cast<uint32_t>(physics::ThrowableShape::Cube);

    LOG_INFO(
        "Cube triangle experiment: {} cubes, {} base width, {} rows, one cube thick, {} static foundation cubes",
        spawned, baseWidth, rows.size(), rows.front().bodies);
    return spawned == bodyCount;
}

void Application::wakeCubeTriangleImpactColumns(double impactWorldX) {
    if (!physicsWorld_ || cubeTriangleColumns_.empty()
        || !std::isfinite(impactWorldX)) {
        return;
    }

    const uint64_t tick = physicsWorld_->encodedTick();
    if (cubeTriangleWakeDedupTick_ != tick) {
        std::fill(
            cubeTriangleColumnsWokenThisTick_.begin(),
            cubeTriangleColumnsWokenThisTick_.end(), 0u);
        cubeTriangleWakeDedupTick_ = tick;
    }

    const double rowHalf = 0.5
        * static_cast<double>(cubeTriangleColumns_.size() - 1u)
        * static_cast<double>(kCubeTriangleHorizontalPitch);
    const double columnPosition =
        (impactWorldX - static_cast<double>(kBrowserJourneyTargetX)
            + rowHalf)
        / static_cast<double>(kCubeTriangleHorizontalPitch);
    const int64_t centerColumn =
        static_cast<int64_t>(std::llround(columnPosition));
    const int64_t firstColumn = std::max<int64_t>(
        0, centerColumn
            - static_cast<int64_t>(kCubeTriangleImpactWakeRadiusColumns));
    const int64_t lastColumn = std::min<int64_t>(
        static_cast<int64_t>(cubeTriangleColumns_.size()) - 1,
        centerColumn
            + static_cast<int64_t>(kCubeTriangleImpactWakeRadiusColumns));
    if (firstColumn > lastColumn) return;

    std::vector<physics::PhysicsCommand> commands;
    for (int64_t column = firstColumn; column <= lastColumn; ++column) {
        const size_t index = static_cast<size_t>(column);
        if (cubeTriangleColumnsWokenThisTick_[index] != 0u) continue;
        cubeTriangleColumnsWokenThisTick_[index] = 1u;
        for (const physics::BodyHandle body : cubeTriangleColumns_[index]) {
            physics::PhysicsCommand wake;
            wake.type = physics::PhysicsCommandType::Wake;
            wake.body = body;
            commands.push_back(wake);
        }
    }
    if (!commands.empty()) {
        physicsWorld_->enqueue(commands);
    }
}

bool Application::startCubePyramidExperiment() {
    if (config_.salvagePreviewEnabled) return false;
    if (!initialized_) {
        LOG_ERROR("Cannot create the cube triangle before application initialization");
        return false;
    }
    if (config_.cubePyramidBodyCount == 0u) {
        LOG_ERROR("No cube triangle was configured at application initialization");
        return false;
    }
    if (cubePyramidSpawnAttempted_) return cubePyramidSpawned_;

    cubePyramidSpawnAttempted_ = true;
    cubePyramidSpawned_ = spawnCubePyramidExperiment();
    return cubePyramidSpawned_;
}

void Application::startBenchmark() {
    if (config_.salvagePreviewEnabled) return;
    if (!benchmarkRunner_) {
        benchmarkRunner_ = std::make_unique<perf::BenchmarkRunner>();
        
        // Set up camera callback for benchmark
        benchmarkRunner_->setCameraCallback([this](const glm::vec3& pos, const glm::vec3& target) {
            if (camera_) {
                camera_->setWorldPosition(glm::ivec3(0), pos);
                camera_->lookAt(target);
            }
        });
    }

    benchmarkRunner_->setPhysicsBackend(
        physicsWorld_
            ? physics::backendTypeName(physicsWorld_->backendType())
            : "none");
    benchmarkRunner_->setExpectedBodyCount(config_.benchmarkBodyCount);
    benchmarkRunner_->setMinimumThroughputFps(config_.benchmarkMinimumFps);
    if (config_.benchmarkFixedDeltaSeconds > 0.0f) {
        LOG_INFO("Benchmark scripted time step: {:.6f} s ({:.1f} Hz)",
                 config_.benchmarkFixedDeltaSeconds,
                 1.0f / config_.benchmarkFixedDeltaSeconds);
    }
    benchmarkRunner_->start();
    LOG_INFO("Benchmark started");
}

void Application::stopBenchmark() {
    if (benchmarkRunner_ && benchmarkRunner_->isRunning()) {
        benchmarkRunner_->stop();
        LOG_INFO("Benchmark stopped");
    }
}

bool Application::isBenchmarkRunning() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->isRunning();
}

bool Application::benchmarkPassed() const noexcept {
    return benchmarkRunner_ && benchmarkRunner_->passed();
}

void Application::toggleBenchmark() {
    if (isBenchmarkRunning()) {
        stopBenchmark();
    } else {
        startBenchmark();
    }
}

bool Application::startBrowserJourneyBenchmark(
    uint32_t targetBodies, uint32_t warmupTicks, uint32_t impactTicks,
    uint32_t settleTicks, uint32_t bodiesPerVolley,
    uint32_t ticksPerVolley, perf::BrowserJourneyLayout layout,
    perf::BrowserJourneyShape shape) {
    if (config_.salvagePreviewEnabled || !initialized_ || !physicsWorld_ || !camera_ || !characterController_
        || isBenchmarkRunning()
        || (browserJourneyBenchmark_
            && browserJourneyBenchmark_->isRunning())) {
        return false;
    }

    const physics::PhysicsStats physicsStats = physicsWorld_->stats();
    const uint32_t availableBodies =
        physicsStats.bodyCapacity >= physicsStats.residentBodies
        ? physicsStats.bodyCapacity - physicsStats.residentBodies : 0u;
    if (uint64_t{physicsStats.residentBodies} + targetBodies
        > physicsStats.bodyCapacity) {
        LOG_ERROR(
            "Browser journey requires {} additional bodies, but only {} slots remain",
            targetBodies, availableBodies);
        return false;
    }

    if (!browserJourneyBenchmark_) {
        browserJourneyBenchmark_ =
            std::make_unique<perf::BrowserJourneyBenchmark>();
    }
    const perf::BrowserJourneyConfig journeyConfig{
        .targetBodies = targetBodies,
        .warmupTicks = warmupTicks,
        .impactTicks = impactTicks,
        .settleTicks = settleTicks,
        .bodiesPerVolley = bodiesPerVolley,
        .ticksPerVolley = ticksPerVolley,
        .layout = layout,
        .shape = shape,
    };
    const bool started = browserJourneyBenchmark_->start(
        journeyConfig, physicsStats.residentBodies, stats_.frameCount,
        physicsWorld_->encodedTick());
    if (started) {
        if (layout != perf::BrowserJourneyLayout::CubePyramid) {
            prepareBrowserJourneyCamera();
        }
        LOG_INFO(
            "Browser journey armed: {} bodies, {} layout, {} shapes, {} per real volley every {} ticks, phases {} warmup / {} impact / {} settle",
            targetBodies, perf::browserJourneyLayoutName(layout),
            perf::browserJourneyShapeName(shape),
            bodiesPerVolley, ticksPerVolley, warmupTicks, impactTicks,
            settleTicks);
    } else {
        LOG_ERROR("Browser journey rejected: {}",
                  browserJourneyBenchmark_->failureReason());
    }
    return started;
}

int Application::browserJourneyBenchmarkStatus() const noexcept {
    return browserJourneyBenchmark_
        ? static_cast<int>(browserJourneyBenchmark_->status())
        : static_cast<int>(perf::BrowserJourneyStatus::Idle);
}

std::string Application::browserJourneyBenchmarkJson() const {
    return browserJourneyBenchmark_
        ? browserJourneyBenchmark_->resultJson() : std::string{};
}

void Application::prepareBrowserJourneyCamera() {
    if (!camera_ || !characterController_) return;

    // Aim a repeatable player-like view at the actual production terrain.
    // The downward ray makes each rapid volley enter a dense terrain/contact
    // phase within a few seconds instead of remaining airborne for the run.
    const float terrainY =
        characterController_->sampleTerrainHeight(
            kBrowserJourneyTargetX, kBrowserJourneyTargetZ);
    const glm::dvec3 target{
        kBrowserJourneyTargetX, terrainY + 2.0f,
        kBrowserJourneyTargetZ};
    const glm::dvec3 position{
        kBrowserJourneyTargetX - 36.0f, terrainY + 24.0f,
        kBrowserJourneyTargetZ - 20.0f};
    setCameraWorldPose(*camera_, position, target);
    controllerMode_ = ControllerMode::FreeFly;
    stats_.activeController = controllerMode_;
}

void Application::aimBrowserJourneyVolley(uint32_t volleyIndex) {
    if (!camera_ || !characterController_) return;

    // A deterministic sunflower sweep models a player traversing the terrain
    // and changing aim. It keeps thousands of bodies from becoming an
    // artificial single-column pile.
    // The production body limit needs at most 1,024 full volleys, so each
    // volley gets its own terrain zone.
    constexpr uint32_t impactZoneCount = 1'024u;
    constexpr float impactZoneSpacing = 24.0f;
    constexpr float goldenAngle = 2.39996323f;
    const uint32_t zone = volleyIndex % impactZoneCount;
    const float radius = impactZoneSpacing
        * std::sqrt(static_cast<float>(zone));
    const float angle = static_cast<float>(zone) * goldenAngle;
    const float targetX =
        kBrowserJourneyTargetX + std::cos(angle) * radius;
    const float targetZ =
        kBrowserJourneyTargetZ + std::sin(angle) * radius;
    const float terrainY =
        characterController_->sampleTerrainHeight(targetX, targetZ);
    const glm::dvec3 target{targetX, terrainY + 2.0f, targetZ};
    const glm::dvec3 position{
        targetX - 36.0f, terrainY + 24.0f, targetZ - 20.0f};
    setCameraWorldPose(*camera_, position, target);
}

void Application::prepareBrowserJourneyOverview(uint32_t volleyCount) {
    if (!camera_ || !characterController_) return;

    constexpr float impactZoneSpacing = 24.0f;
    const float radius = volleyCount > 1u
        ? impactZoneSpacing
            * std::sqrt(static_cast<float>(volleyCount - 1u))
        : 0.0f;
    const float terrainY =
        characterController_->sampleTerrainHeight(
            kBrowserJourneyTargetX, kBrowserJourneyTargetZ);
    const float distance = 60.0f + radius * 2.3f;
    const float height = 60.0f + radius * 1.3f;
    const glm::dvec3 position{
        kBrowserJourneyTargetX, terrainY + height,
        kBrowserJourneyTargetZ - distance};
    const glm::dvec3 target{
        kBrowserJourneyTargetX, terrainY + 2.0f,
        kBrowserJourneyTargetZ};
    setCameraWorldPose(*camera_, position, target);
}

void Application::updateBrowserJourneyBenchmark() {
    if (!browserJourneyBenchmark_ || !physicsWorld_
        || !browserJourneyBenchmark_->isRunning()) {
        return;
    }

    const uint64_t physicsTick = physicsWorld_->encodedTick();
    const uint32_t requested = browserJourneyBenchmark_->advance(
        physicsTick, physicsWorld_->stats().residentBodies);
    if (requested != 0u) {
        if (browserJourneyBenchmark_->layout()
            == perf::BrowserJourneyLayout::TerrainSweep) {
            aimBrowserJourneyVolley(browserJourneyBenchmark_->volleyCount());
        }
        constexpr uint32_t shapeCount =
            static_cast<uint32_t>(physics::ThrowableShape::Count);
        const uint32_t benchmarkShape = static_cast<uint32_t>(
            browserJourneyBenchmark_->shape());
        selectedThrowable_ = benchmarkShape < shapeCount
            ? benchmarkShape
            : browserJourneyBenchmark_->volleyCount() % shapeCount;
        const auto shape =
            static_cast<physics::ThrowableShape>(selectedThrowable_);
        const uint32_t spawned = throwThrowableBatch(shape, requested);
        static_cast<void>(browserJourneyBenchmark_->reportVolley(
            physicsTick, requested, spawned));
        if (browserJourneyBenchmark_->status()
                == perf::BrowserJourneyStatus::Impact
            && browserJourneyBenchmark_->layout()
                == perf::BrowserJourneyLayout::TerrainSweep) {
            prepareBrowserJourneyOverview(
                browserJourneyBenchmark_->volleyCount());
        }
    }

    if (!browserJourneyBenchmark_->isRunning()) {
        if (browserJourneyBenchmark_->passed()) {
            LOG_INFO("Browser journey complete: {} bodies across {} volleys",
                     browserJourneyBenchmark_->spawnedBodies(),
                     browserJourneyBenchmark_->volleyCount());
        } else {
            LOG_ERROR("Browser journey failed: {}",
                      browserJourneyBenchmark_->failureReason());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Controller Mode
// ─────────────────────────────────────────────────────────────────────────────

void Application::setControllerMode(ControllerMode mode) {
    if (controllerMode_ != mode) {
        controllerMode_ = mode;
        stats_.activeController = mode;
        
        // Log current camera position for debugging
        if (camera_) {
            const auto& pos = camera_->position();
            LOG_INFO("Controller mode changed to: {} (camera at {:.1f}, {:.1f}, {:.1f})", 
                     controllerModeToString(mode), pos.x, pos.y, pos.z);
        } else {
            LOG_INFO("Controller mode changed to: {}", controllerModeToString(mode));
        }
        
        // When switching to character mode, log terrain info for debugging
        if (mode == ControllerMode::Character && characterController_ && camera_) {
            characterController_->syncPhysicsPosition();
            const auto& cfg = characterController_->config();
            LOG_INFO("Character config: terrainWidth={:.1f}, terrainHeight={:.1f}, heightScale={:.1f}", 
                     cfg.terrainWidth, cfg.terrainHeight, cfg.heightScale);
            
            const auto& pos = camera_->position();
            float terrainH = characterController_->sampleTerrainHeight(pos.x, pos.z);
            LOG_INFO("Terrain height at camera XZ ({:.1f}, {:.1f}) = {:.1f}", 
                     pos.x, pos.z, terrainH);
        }
    }
}

void Application::toggleControllerMode() {
    switch (controllerMode_) {
        case ControllerMode::FreeFly:
            setControllerMode(ControllerMode::Character);
            break;
        case ControllerMode::Character:
            setControllerMode(ControllerMode::FreeFly);
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initWindow() {
#if defined(VOXY_NATIVE)
    LOG_DEBUG("Initializing window...");

    window_ = std::make_unique<Window>();

    WindowConfig windowConfig;
    windowConfig.width = config_.windowWidth;
    windowConfig.height = config_.windowHeight;
    windowConfig.title = config_.windowTitle.c_str();
    windowConfig.fullscreen = config_.fullscreen;
    windowConfig.vsync = config_.vsync;
    windowConfig.resizable = true;

    if (!window_->init(windowConfig)) {
        return false;
    }

    LOG_DEBUG("Window created: {}x{}", window_->getWidth(), window_->getHeight());
    return true;

#elif defined(VOXY_WASM)
    // WASM uses canvas, no window needed
    LOG_DEBUG("WASM build - no window initialization needed");
    return true;
#else
    return false;
#endif
}

bool Application::initGPU() {
    LOG_DEBUG("Initializing GPU context...");

    gpuContext_ = std::make_unique<gpu::Context>();

    gpu::ContextConfig gpuConfig;
    gpuConfig.powerPreference = WGPUPowerPreference_HighPerformance;
    gpuConfig.enableValidation = config_.enableValidation;
    gpuConfig.enableTimestamps = config_.gpuPhysicsStageProfiling
        || config_.gpuRenderStageProfiling;
    gpuConfig.preferredFormat = config_.colorFormat;
    gpuConfig.presentMode = config_.vsync ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;

#if defined(VOXY_NATIVE)
    if (!window_) {
        LOG_ERROR("Window must be initialized before GPU context");
        return false;
    }

    gpuConfig.swapchainWidth = scaledRenderExtent(
        static_cast<uint32_t>(window_->getFramebufferWidth()),
        config_.resolutionScale);
    gpuConfig.swapchainHeight = scaledRenderExtent(
        static_cast<uint32_t>(window_->getFramebufferHeight()),
        config_.resolutionScale);

    if (!gpuContext_->init(*window_, gpuConfig)) {
        return false;
    }

#elif defined(VOXY_WASM)
    gpuConfig.swapchainWidth = scaledRenderExtent(
        static_cast<uint32_t>(config_.windowWidth), config_.resolutionScale);
    gpuConfig.swapchainHeight = scaledRenderExtent(
        static_cast<uint32_t>(config_.windowHeight), config_.resolutionScale);

    if (!gpuContext_->initFromCanvas("#voxy-canvas", gpuConfig)) {
        return false;
    }
#endif

    // Surface capability negotiation may select a different format than the
    // preference. Every attachment pipeline must use the negotiated format.
    config_.colorFormat = gpuContext_->getSwapchainFormat();

    if (config_.benchmarkOnStartup) {
        if (!createBenchmarkTarget(gpuContext_->getSwapchainWidth(),
                                   gpuContext_->getSwapchainHeight())) {
            LOG_ERROR("Failed to create the benchmark offscreen target");
            return false;
        }
    }

    LOG_DEBUG("GPU context initialized");
    return true;
}

bool Application::createBenchmarkTarget(uint32_t width, uint32_t height) {
    if (!gpuContext_ || width == 0u || height == 0u) return false;

    gpu::TextureDesc targetDesc = gpu::TextureDesc::renderTarget(
        width, height, gpuContext_->getSwapchainFormat(),
        "benchmark_offscreen_target");
    targetDesc.usage = WGPUTextureUsage_RenderAttachment
                     | WGPUTextureUsage_CopySrc;
    WGPUTexture nextTexture = gpu::createTexture(
        gpuContext_->getDevice(), targetDesc);
    if (!nextTexture) return false;
    WGPUTextureView nextView = gpu::createTextureView(nextTexture);
    if (!nextView) {
        wgpuTextureRelease(nextTexture);
        return false;
    }

    // Releasing the application handles is safe with in-flight submissions:
    // WebGPU command buffers retain the resources they reference.
    if (benchmarkTargetView_) wgpuTextureViewRelease(benchmarkTargetView_);
    if (benchmarkTargetTexture_) wgpuTextureRelease(benchmarkTargetTexture_);
    benchmarkTargetTexture_ = nextTexture;
    benchmarkTargetView_ = nextView;
    return true;
}

bool Application::initRenderGpuProfiling() {
    if (!config_.gpuRenderStageProfiling || !gpuContext_
        || !wgpuDeviceHasFeature(
            gpuContext_->getDevice(), WGPUFeatureName_TimestampQuery)) {
        return true;
    }

    WGPUQuerySetDescriptor queryDesc{};
    WGPU_SET_LABEL(queryDesc, "render_stage_timestamps");
    queryDesc.type = WGPUQueryType_Timestamp;
    queryDesc.count = kRenderGpuTimestampCount;
    renderGpuQuerySet_ = wgpuDeviceCreateQuerySet(
        gpuContext_->getDevice(), &queryDesc);
    renderGpuResolveBuffer_ = gpu::createBuffer(
        gpuContext_->getDevice(), gpu::BufferDesc{
            .label = "render_stage_timestamp_resolve",
            .size = kRenderGpuTimestampCount * sizeof(uint64_t),
            .usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc,
        });
    if (!renderGpuQuerySet_ || !renderGpuResolveBuffer_
        || !renderGpuReadback_.initialize(
            gpuContext_->getDevice(), 4u,
            kRenderGpuTimestampCount * sizeof(uint64_t))) {
        renderGpuReadback_.shutdown();
        if (renderGpuResolveBuffer_) {
            wgpuBufferDestroy(renderGpuResolveBuffer_);
            wgpuBufferRelease(renderGpuResolveBuffer_);
            renderGpuResolveBuffer_ = nullptr;
        }
        if (renderGpuQuerySet_) {
            wgpuQuerySetRelease(renderGpuQuerySet_);
            renderGpuQuerySet_ = nullptr;
        }
        return false;
    }
    return true;
}

bool Application::initInput() {
    LOG_DEBUG("Initializing input system...");

    input_ = std::make_unique<Input>();

#if defined(VOXY_NATIVE)
    if (window_) {
        input_->attachToWindow(*window_);
    }
#elif defined(VOXY_WASM)
    input_->setupEmscriptenCallbacks("#voxy-canvas");
#endif

    LOG_DEBUG("Input system initialized");
    return true;
}

bool Application::initCamera() {
    LOG_DEBUG("Initializing camera...");

    // Create camera
    CameraConfig camConfig;
    camConfig.fovY = glm::radians(config_.cameraFovDegrees);
    camConfig.nearPlane = config_.cameraNear;
    camConfig.farPlane = config_.cameraFar;

    camConfig.aspectRatio =
        static_cast<float>(gpuContext_->getSwapchainWidth())
        / static_cast<float>(gpuContext_->getSwapchainHeight());

    const glm::vec3 defaultStart{0.0f, 80.0f, 0.0f};
    glm::vec3 startPos = config_.cameraStartPos;
    if (glm::length(startPos - defaultStart) < 0.001f) {
        const float viewDistance = std::max(200.0f, config_.cellScale * 256.0f);
        const float viewHeight = std::max(80.0f, config_.heightScale * 1.25f);
        startPos = glm::vec3(0.0f, viewHeight, -viewDistance);
    }

    camera_ = std::make_unique<Camera>(startPos, camConfig);

    // The renderer centers the terrain at (0,0,0)
    // So we should look at the origin, not the calculated positive center
    camera_->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));

    // Create free-fly camera controller
    FreeFlyConfig flyConfig;
    flyConfig.baseSpeed = config_.cameraMoveSpeed;
    flyConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    flyConfig.boostMultiplier = 5.0f;

    freeFlyController_ = std::make_unique<FreeFlyController>(*camera_, flyConfig);

    physicsWorld_ = std::make_unique<physics::PhysicsWorld>();
    physics::PhysicsInitContext physicsContext;
    physicsContext.requestedBackend = config_.physicsBackend;
    physicsContext.allowCpuFallback = config_.physicsCpuFallback && !config_.legoTerrainEnabled && !coveResume_;
    if(coveResume_)physicsContext.gpu.initialTick=coveResume_->tick;
    physicsContext.device = gpuContext_->getDevice();
    physicsContext.queue = gpuContext_->getQueue();
    if (config_.physicsBackend == physics::BackendType::WebGpuSoft) {
        physicsContext.gpu.enableRenderInterpolation = true;
        physicsContext.maxBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.maxActiveBodies = config_.gpuPhysicsMaxBodies;
        physicsContext.maxPairs = config_.gpuPhysicsMaxPairs;
        physicsContext.maxContacts = config_.gpuPhysicsMaxPairs;
        physicsContext.maxManifolds = config_.gpuPhysicsMaxPairs;
        physicsContext.maxCandidatePairs =
            config_.gpuPhysicsMaxCandidatePairs;
        if (config_.cubePyramidBodyCount != 0u) {
            // Run broad/narrow at 30 Hz and solve at 60 Hz. Two substeps avoid
            // the extra overlap seen at one while CCD protects the projectile.
            physicsContext.gpu.fixedTickSeconds = 1.0f / 30.0f;
            physicsContext.gpu.substeps = 2u;
            physicsContext.gpu.solverColorCount = 8u;
            physicsContext.gpu.solverParallelColorCount = 0u;
            physicsContext.gpu.maximumCatchUpTicks = 3u;
        } else {
            physicsContext.gpu.maximumCatchUpTicks =
                config_.gpuPhysicsMaximumCatchUpTicks;
        }
        physicsContext.gpu.broadPhaseCellSize =
            config_.gpuPhysicsBroadPhaseCellSize;
        physicsContext.gpu.solverWorkgroupSize =
            config_.gpuPhysicsSolverWorkgroupSize;
    }
    physicsContext.gpu.shaderPath =
        (config_.shaderDir / "physics_ballistic.wgsl").string();
    physicsContext.gpu.enableStageProfiling =
        config_.gpuPhysicsStageProfiling;
    if(config_.salvageAssetFixtureWaterAnchor) {
        // This cove shares one wave field per submission. Both hosts must
        // encode at most one tick until SIM-05 supplies per-tick catch-up fields.
        physicsContext.gpu.maximumCatchUpTicks=1;
        // Bounded starter-cove event stream; overflow is fatal evidence loss.
        // Solver/contact capacities remain independent of this readback budget.
        physicsContext.gpu.eventReadbackCapacity=512;
    }
    // Interactive stage timings sample asynchronously at a low rate.
    // Timestamp queries remain enabled, but resolving and mapping them every
    // physics tick serializes browser GPU work and makes the profiler change
    // the performance it is measuring.
    constexpr uint32_t kInteractiveStageProfilingIntervalTicks = 30u;
    constexpr uint32_t kInteractiveTelemetryIntervalTicks = 30u;
    const uint32_t profilingInterval =
        config_.benchmarkBodyCount != 0u
            ? 1u : kInteractiveStageProfilingIntervalTicks;
    const uint32_t telemetryInterval =
        config_.gpuPhysicsStageProfiling
            || config_.benchmarkBodyCount != 0u
        ? 1u : kInteractiveTelemetryIntervalTicks;
    physicsContext.gpu.stageProfilingIntervalTicks = profilingInterval;
    physicsContext.gpu.enableTelemetryReadback = true;
    physicsContext.gpu.telemetryReadbackIntervalTicks = telemetryInterval;
    physicsContext.gpu.stageProfilingTimestampPeriodNanoseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds;
    physicsContext.enableValidation = config_.enableValidation;
    physicsContext.joltJobSystem = config_.joltJobSystem;
    physicsContext.joltWorkerThreads = config_.joltWorkerThreads;
    physicsContext.box3dWorkerThreads = config_.box3dWorkerThreads;
    if (!physicsWorld_->initialize(physicsContext)) {
        LOG_ERROR("Failed to initialize '{}' physics backend",
                  physics::backendTypeName(config_.physicsBackend));
        return false;
    }
    const char* scheduler = config_.physicsBackend
            == physics::BackendType::JoltLegacy
        ? physics::joltJobSystemModeName(config_.joltJobSystem)
        : "internal";
    LOG_INFO("Physics backend: {} (scheduler {}, concurrency {})",
             physics::backendTypeName(physicsWorld_->backendType()),
             scheduler, physicsWorld_->stats().workerConcurrency);
    physicsWorld_->setWaterPlane(rendererSettings_.waterHeight,
                                 rendererSettings_.waterEnabled);

    // Create character controller (will be fully initialized after terrain loads)
    CharacterConfig charConfig;
    charConfig.walkSpeed = config_.cameraMoveSpeed;
    charConfig.runSpeed = config_.cameraMoveSpeed * 2.0f;
    charConfig.mouseSensitivity = config_.cameraMouseSensitivity;
    charConfig.heightScale = config_.heightScale;
    charConfig.cellScale = config_.cellScale;
    charConfig.legoTerrain = config_.legoTerrainEnabled;
    charConfig.groundOffset = config_.cameraEyeHeight;
    charConfig.collisionHeight = std::max(config_.cameraEyeHeight, 0.82f);
    charConfig.terrainWidth = terrainSampleExtent(config_.heightmapWidth, config_.cellScale);
    charConfig.terrainHeight = terrainSampleExtent(config_.heightmapHeight, config_.cellScale);
    
    // Character controller is created without heightmap first, will be set after terrain init
    characterController_ = std::make_unique<CharacterController>();
    characterController_->attachCamera(*camera_);
    characterController_->setConfig(charConfig);
    characterController_->attachPhysicsWorld(*physicsWorld_);

    LOG_DEBUG("Camera initialized at ({}, {}, {})",
              config_.cameraStartPos.x, config_.cameraStartPos.y, config_.cameraStartPos.z);
    return true;
}

bool Application::initTerrain() {
    LOG_DEBUG("Initializing terrain...");

    motoWorldSpawnValid_ = false;
    motoWorldSpawn_ = {};
    motoWorldSpawnYaw_ = 0.0f;
    motoRaceRoute_.clear();
    motoTrackPoses_.clear();
    motoSurfaceMapSize_ = 0u;
    motoSurfaceMap_.clear();
    heightmap_ = std::make_unique<terrain::Heightmap>();

    if (!config_.heightmapPath.empty()) {
        // Load from file
        auto result = heightmap_->loadFromFile(config_.heightmapPath);
        if (!result) {
            LOG_ERROR("Failed to load heightmap: {}", 
                      terrain::errorToString(result.error()));
            return false;
        }
        LOG_INFO("Loaded heightmap: {}", config_.heightmapPath.string());

        // LEGO layout and collision use the original samples. Never inflate
        // this source to a launcher's generic landscape resolution: doing so
        // changes the terrain and can exhaust the fixed browser heap.
        if (config_.legoTerrainEnabled || config_.salvageAssetFixtureWaterAnchor) {
            config_.heightmapWidth = heightmap_->getWidth();
            config_.heightmapHeight = heightmap_->getHeight();
        }
        
        // Check if upscaling is needed (e.g. loaded 4k, requested 8k)
        if (heightmap_->getWidth() < config_.heightmapWidth || 
            heightmap_->getHeight() < config_.heightmapHeight) {
            
            LOG_INFO("Upscaling heightmap from {}x{} to {}x{}", 
                     heightmap_->getWidth(), heightmap_->getHeight(),
                     config_.heightmapWidth, config_.heightmapHeight);
                     
            auto resizeResult = heightmap_->resize(config_.heightmapWidth, config_.heightmapHeight);
            if (!resizeResult) {
                LOG_ERROR("Failed to upscale heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
        
        // Resize to power of 2 if needed (required for mip chain generation)
        if (!terrain::isPowerOfTwo(heightmap_->getWidth()) || 
            !terrain::isPowerOfTwo(heightmap_->getHeight())) {
            LOG_INFO("Heightmap dimensions {}x{} are not power of 2, resizing...",
                     heightmap_->getWidth(), heightmap_->getHeight());
            auto resizeResult = heightmap_->resizeToPowerOfTwo();
            if (!resizeResult) {
                LOG_ERROR("Failed to resize heightmap: {}", 
                          terrain::errorToString(resizeResult.error()));
                return false;
            }
        }
    } else if (config_.motoEnabled) {
        // Empty terrain paths select the canonical RIDGEBREAK world. Keep
        // generation deterministic so native, browser, authority, replay and
        // capture runs all refer to the same terrain without shipping a large
        // baked heightmap.
        moto::WorldGenConfig worldConfig;
        worldConfig.size = config_.heightmapWidth;
        worldConfig.heightScale = config_.heightScale;
        worldConfig.cellScale = config_.cellScale;

        moto::WorldGenResult world;
        std::string error;
        if (!moto::generateWorld(worldConfig, &world, &error)) {
            LOG_ERROR("Failed to generate RIDGEBREAK world: {}", error);
            return false;
        }
        motoWorldSpawn_ = world.spawnPosition;
        motoWorldSpawnYaw_ = world.spawnHeading;
        motoWorldSpawnValid_ = true;
        motoRaceRoute_ = std::move(world.raceRoute);
        motoSurfaceMapSize_ = world.surfaceMapSize;
        motoSurfaceMap_ = std::move(world.surfaceMap);
        const size_t featureCount = world.features.size();
        *heightmap_ = terrain::Heightmap::createFromData(
            std::move(world.samples), worldConfig.size, worldConfig.size);
        if (!heightmap_->isLoaded()) {
            LOG_ERROR("RIDGEBREAK world produced an invalid heightmap");
            return false;
        }
        motoTrackPoses_.reserve(world.trackProps.size());
        for (const moto::WorldTrackProp& prop : world.trackProps) {
            const glm::vec3 flatForward(
                std::sin(prop.heading), 0.0f, std::cos(prop.heading));
            const glm::vec3 flatRight(
                flatForward.z, 0.0f, -flatForward.x);
            constexpr float probe = 0.8f;
            const float frontY = sampleTerrainHeight(
                prop.position.x + flatForward.x * probe,
                prop.position.y + flatForward.z * probe);
            const float backY = sampleTerrainHeight(
                prop.position.x - flatForward.x * probe,
                prop.position.y - flatForward.z * probe);
            const float rightY = sampleTerrainHeight(
                prop.position.x + flatRight.x * probe,
                prop.position.y + flatRight.z * probe);
            const float leftY = sampleTerrainHeight(
                prop.position.x - flatRight.x * probe,
                prop.position.y - flatRight.z * probe);
            glm::vec3 forward = glm::normalize(glm::vec3(
                flatForward.x, (frontY - backY) / (2.0f * probe),
                flatForward.z));
            glm::vec3 right = glm::normalize(glm::vec3(
                flatRight.x, (rightY - leftY) / (2.0f * probe),
                flatRight.z));
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            // Re-orthogonalize both tangents. The previous longitudinal-only
            // fit visibly clipped narrow rut decals on cross-slopes.
            right = glm::normalize(glm::cross(up, forward));
            forward = glm::normalize(glm::cross(right, up));
            glm::mat4 basis(1.0f);
            basis[0] = glm::vec4(right, 0.0f);
            basis[1] = glm::vec4(up, 0.0f);
            basis[2] = glm::vec4(forward, 0.0f);
            const bool isDecal = prop.kind == moto::TrackPropKind::RutStrip ||
                                 prop.kind == moto::TrackPropKind::LandingPatch;
            basis[3] = glm::vec4(
                prop.position.x,
                sampleTerrainHeight(prop.position.x, prop.position.y) +
                    (isDecal ? 0.025f : 0.0f),
                prop.position.y, 1.0f);
            basis = basis * glm::scale(
                glm::mat4(1.0f), glm::vec3(std::max(prop.scale, 0.1f)));
            motoTrackPoses_.push_back({
                .meshIndex = static_cast<uint32_t>(prop.kind),
                .modelMatrix = basis,
                .tintColor = glm::vec4(1.0f),
            });
        }
        LOG_INFO(
            "Generated RIDGEBREAK world: {}x{}, {} route points, {} features, "
            "{} static track props, {}x{} surface splat",
            worldConfig.size, worldConfig.size, motoRaceRoute_.size(),
            featureCount, motoTrackPoses_.size(), motoSurfaceMapSize_,
            motoSurfaceMapSize_);
    } else {
        // Create procedural wavy heightmap
        uint32_t w = config_.heightmapWidth;
        uint32_t h = config_.heightmapHeight;
        std::vector<uint16_t> data(
            static_cast<size_t>(w) * static_cast<size_t>(h));
        
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                // Create some sine waves
                float u = static_cast<float>(x) / static_cast<float>(w) * 10.0f;
                float v = static_cast<float>(y) / static_cast<float>(h) * 10.0f;
                float height = 0.5f + 0.2f * std::sin(u) + 0.2f * std::cos(v);
                data[static_cast<size_t>(y) * w + x] =
                    static_cast<uint16_t>(height * 65535.0f);
            }
        }
        *heightmap_ = terrain::Heightmap::createFromData(std::move(data), w, h);
        LOG_INFO("Created procedural heightmap: {}x{}", 
                 config_.heightmapWidth, config_.heightmapHeight);
    }

    // The authored cove is content for the canonical Wreckwater terrain, not
    // a filter applied to arbitrary user heightmaps.
    if (!config_.adventureEnabled && !config_.heightmapPath.empty() &&
        config_.heightmapPath.filename() ==
            "td_seed_1234_8192.ldh") {
        terrain::AuthoredCoveConfig coveConfig;
        coveConfig.waterHeight = rendererSettings_.waterHeight;
        terrain::AuthoredCoveStats coveStats;
        if (!terrain::applyAuthoredCove(
                heightmap_->getMutableData(),
                heightmap_->getWidth(),
                heightmap_->getHeight(),
                config_.heightScale,
                config_.cellScale,
                coveConfig,
                &coveStats)) {
            LOG_ERROR("Failed to author the Wreckwater review cove");
            return false;
        }
        LOG_INFO(
            "Authored Wreckwater cove: {} core samples, {} feather samples, "
            "height {:.1f}..{:.1f} m",
            coveStats.fullyAuthoredSamples,
            coveStats.touchedSamples - coveStats.fullyAuthoredSamples,
            coveStats.minimumAuthoredHeight,
            coveStats.maximumAuthoredHeight);
    }

    if (config_.cubePyramidBodyCount != 0u
        && !flattenCubeTriangleArena(*heightmap_, config_.cellScale)) {
        LOG_ERROR("Failed to flatten the cube-triangle terrain arena");
        return false;
    }

    if (config_.salvageAssetFixtureWaterAnchor && config_.salvageAssetFixtureRegistry
        && std::filesystem::path(*config_.salvageAssetFixtureRegistry).filename()=="fixture-cove-r01.json"
        && config_.heightmapPath.filename()=="lego_shore.ldh") {
        if (!terrain::applySalvageBerth(heightmap_->getMutableData(),heightmap_->getWidth(),heightmap_->getHeight(),
            config_.heightScale,config_.cellScale,rendererSettings_.waterHeight)) return false;
    }
    // Upload to GPU with mip chain
    // Note: Use CPU mip generation for better compatibility across drivers
    // (GPU mip generation requires StorageBinding on R16Uint which isn't universally supported)
    auto uploadResult = heightmap_->uploadToGPUWithMips(
        gpuContext_->getDevice(),
        gpuContext_->getQueue(),
        false,  // Use CPU mip generation for compatibility
        config_.shaderDir / "mip_generate.wgsl",
        "terrain_heightmap"
    );

    if (!uploadResult) {
        LOG_ERROR("Failed to upload heightmap to GPU: {}", 
                  terrain::errorToString(uploadResult.error()));
        return false;
    }

    // Update stats
    stats_.terrainWidth = heightmap_->getWidth();
    stats_.terrainHeight = heightmap_->getHeight();
    stats_.terrainMipLevels = heightmap_->getMipLevelCount();
    
    // Bind heightmap to character controller
    if (characterController_) {
        characterController_->setHeightmap(heightmap_.get());
        
        // Update terrain dimensions in character controller config
        CharacterConfig charConfig = characterController_->config();
        charConfig.terrainWidth = terrainSampleExtent(stats_.terrainWidth, config_.cellScale);
        charConfig.terrainHeight = terrainSampleExtent(stats_.terrainHeight, config_.cellScale);
        charConfig.heightScale = config_.heightScale;
        charConfig.cellScale = config_.cellScale;
        charConfig.legoTerrain = config_.legoTerrainEnabled;
        charConfig.groundOffset = config_.cameraEyeHeight;
        characterController_->setConfig(charConfig);
    }

    if (physicsWorld_) {
        physicsWorld_->setTerrainGpuResources({
            heightmap_->getTextureView(), heightmap_->getMipLevelCount()});
    }
    const bool terrainReady = physicsWorld_ && (config_.legoTerrainEnabled
        ? physicsWorld_->setLegoTerrain(heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),
            config_.heightScale,config_.cellScale)
        : physicsWorld_->setTerrain(heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),
            config_.heightScale,config_.cellScale));
    if (!terrainReady) {
        LOG_ERROR("Failed to attach the requested terrain surface to the physics backend");
        return false;
    }

    LOG_DEBUG("Terrain initialized: {}x{} with {} mip levels",
              stats_.terrainWidth, stats_.terrainHeight, stats_.terrainMipLevels);
    return true;
}

bool Application::initRenderers() {
    LOG_DEBUG("Initializing renderers...");

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // The spectral ocean is shared by ray intersection and final shading.
    // Initialize it before either consumer creates its bind group.
    waterSimulation_ = std::make_unique<render::WaterSimulation>();
    if (!waterSimulation_->init(device, queue, config_.shaderDir,
                                heightmap_->getData(), heightmap_->getWidth(),
                                heightmap_->getHeight(), config_.heightScale,
                                config_.cellScale, rendererSettings_.waterHeight,
                                makeWaterSpectrumConfig(
                                    rendererSettings_.waterSpectrum))) {
        LOG_ERROR("Failed to initialize FFT water simulation");
        return false;
    }
    // WaterSimulation owns the canonical clamping/wrapping rules. Use exactly
    // the values represented by its newly-created CPU and GPU spectrum.
    rendererSettings_.waterSpectrum = makeWaterSpectrumSettings(
        waterSimulation_->spectrumConfig());
    if (physicsWorld_) {
        const float waveStrength = rendererSettings_.waterWaveStrength;
        if (physicsWorld_->backendType() == physics::BackendType::WebGpuSoft) {
            physicsWorld_->setWaterGpuResources({
                waterSimulation_->getOutputView(),
                waterSimulation_->getSampler(), waveStrength,
                rendererSettings_.waterSpectrum.patchLengths.x,
                rendererSettings_.waterSpectrum.patchLengths.y});
        } else {
            physicsWorld_->setWaterSurfaceSampler(
                [simulation = waterSimulation_.get(), waveStrength](
                    glm::vec2 position, float timeSeconds) {
                    const auto sample = simulation->sampleSurface(
                        position, timeSeconds, waveStrength);
                    return physics::PhysicsWorld::WaterSurfaceSample{
                        sample.heightOffset, sample.slope, sample.velocity};
                });
        }
    }

    // Initialize triangle path
    {
        render::TrianglePathConfig triConfig = render::TrianglePathConfig::defaults();
        triConfig.shaderPath = config_.shaderDir / "terrain.wgsl";
        triConfig.colorFormat = config_.colorFormat;
        triConfig.heightScale = config_.heightScale;
        triConfig.cellScale = config_.cellScale;

        trianglePath_ = std::make_unique<render::TrianglePath>();
        if (!trianglePath_->init(device, queue, triConfig)) {
            LOG_ERROR("Failed to initialize triangle path");
            return false;
        }

        // Bind heightmap
        if (!trianglePath_->setHeightmap(
                heightmap_->getTextureView(),
                heightmap_->getWidth(),
                heightmap_->getHeight(),
                heightmap_->getData())) {
            LOG_ERROR("Failed to bind terrain to triangle path");
            return false;
        }
    }

    // Initialize raycast path
    {
        render::RaycastPathConfig rayConfig = render::RaycastPathConfig::defaults();
        rayConfig.shaderPath = config_.shaderDir / "terrain_raycast.wgsl";
        rayConfig.heightScale = config_.heightScale;
        rayConfig.cellScale = config_.cellScale;

        raycastPath_ = std::make_unique<render::RaycastPath>();
        if (!raycastPath_->init(device, queue, width, height, rayConfig)) {
            LOG_ERROR("Failed to initialize raycast path");
            return false;
        }

        // Bind heightmap
        if (!raycastPath_->setHeightmap(
                heightmap_->getTextureView(),
                heightmap_->getWidth(),
                heightmap_->getHeight())) {
            LOG_ERROR("Failed to bind terrain to raycast path");
            return false;
        }
        raycastPath_->setWaterSimulation(waterSimulation_->getOutputView(),
                                         waterSimulation_->getCoastView(),
                                         waterSimulation_->getSampler());

        // Bake the static sun shadow height field. The raycast shader then
        // replaces its per-pixel shadow DDA with a single texture lookup.
        {
            perf::Timer bakeTimer;
            bakeTimer.start();
            terrain::ShadowBakeConfig bakeConfig;
            bakeConfig.lightDir = rendererSettings_.sunDirection;
            bakeConfig.heightScale = config_.heightScale;
            bakeConfig.cellScale = config_.cellScale;

            const auto baked = terrain::bakeShadowHeightField(
                heightmap_->getData(), heightmap_->getWidth(),
                heightmap_->getHeight(), bakeConfig);

            if (!baked.data.empty()) {
                gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
                    baked.width, baked.height, WGPUTextureFormat_R16Uint,
                    WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                    "baked_shadow_height");
                shadowMapTexture_ = gpu::createTextureWithData(
                    device, queue, desc,
                    std::as_bytes(std::span<const uint16_t>(baked.data)),
                    baked.width * sizeof(uint16_t));
                if (shadowMapTexture_) {
                    shadowMapView_ = gpu::createTextureView(shadowMapTexture_);
                }
                if (shadowMapView_) {
                    raycastPath_->setShadowMap(shadowMapView_);
                    LOG_INFO("Baked shadow height field: {}x{} ({:.1f} ms)",
                             baked.width, baked.height, bakeTimer.elapsedMs());
                } else {
                    LOG_WARN("Failed to upload baked shadow map; shadows disabled");
                }
            } else {
                LOG_WARN("Shadow bake produced no data; shadows disabled");
            }
        }
    }

    // Initialize terrain textures
    terrainTextures_ = std::make_unique<terrain::TerrainTextures>();
    
    terrain::TerrainTextureConfig textureConfig;
    textureConfig.albedoPath = config_.albedoPath;
    textureConfig.lightmapPath = config_.lightmapPath;
    // This fallback is a smooth UV color pattern, not per-sample terrain data.
    // Bound it independently of terrain resolution (256 KiB instead of a
    // possible 256 MiB allocation for an 8K map with missing albedo).
    textureConfig.placeholderWidth = std::min(heightmap_->getWidth(), 256u);
    textureConfig.placeholderHeight = std::min(heightmap_->getHeight(), 256u);

    if (!terrainTextures_->init(device, queue, textureConfig)) {
        LOG_ERROR("Failed to initialize terrain textures and fallbacks");
        return false;
    }
    if (motoSurfaceMapSize_ != 0u
        && !terrainTextures_->createWorldSurfaceMap(
            motoSurfaceMap_, motoSurfaceMapSize_, motoSurfaceMapSize_)) {
        LOG_ERROR("Failed to upload RIDGEBREAK world surface splat");
        return false;
    }
    // CPU staging is no longer needed after the complete mip chain is resident.
    motoSurfaceMap_.clear();
    motoSurfaceMap_.shrink_to_fit();

    // Pass textures to triangle path
    trianglePath_->setAlbedo(terrainTextures_->getAlbedoView());
    trianglePath_->setLightmap(terrainTextures_->getLightmapView());
    trianglePath_->setSampler(terrainTextures_->getSampler());

    // Initialize blit path
    {
        render::BlitPathConfig blitConfig = render::BlitPathConfig::defaults();
        blitConfig.shaderPath = config_.shaderDir / "ray_blit.wgsl";
        blitConfig.colorFormat = config_.colorFormat;
        blitConfig.heightScale = config_.heightScale;
        blitConfig.cellScale = config_.cellScale;
    blitConfig.enableOpaqueScene = config_.salvageAssetFixtureWaterAnchor || config_.adventureEnabled;
        blitConfig.coveVisuals = config_.salvageAssetFixtureWaterAnchor
            && config_.salvageAssetFixtureRegistry
            && std::filesystem::path(*config_.salvageAssetFixtureRegistry).filename()=="fixture-cove-r01.json";

        blitPath_ = std::make_unique<render::BlitPath>();
        if (!blitPath_->init(device, queue, blitConfig)) {
            LOG_ERROR("Failed to initialize blit path");
            return false;
        }
        if (!blitPath_->resize(raycastPath_->getOutputWidth(),
                               raycastPath_->getOutputHeight())) {
            LOG_ERROR("Failed to create blit background cache");
            return false;
        }

        // Bind textures to blit path
        blitPath_->setDepthTexture(raycastPath_->getDepthOutputView());
        blitPath_->setShadowTexture(raycastPath_->getShadowOutputView());
        blitPath_->setMaterialTexture(raycastPath_->getMaterialOutputView());
        blitPath_->setStaticTerrainTextures(
            raycastPath_->getTerrainDepthCacheView(),
            raycastPath_->getTerrainShadowCacheView());
        blitPath_->setWaterCompositeResources(
            heightmap_->getTextureView(), raycastPath_->getShadowMapView(),
            waterSimulation_->getOutputView(), waterSimulation_->getSampler());
        
        // TerrainTextures guarantees valid views after init (either loaded or placeholder)
        blitPath_->setTerrainTexture(terrainTextures_->getAlbedoView());
        blitPath_->setTerrainMaterialTextures(
            terrainTextures_->getMaterialAlbedoView(),
            terrainTextures_->getMaterialNormalRoughnessView());
        blitPath_->setLightmapTexture(terrainTextures_->getLightmapView()); 
        blitPath_->setTerrainSize(heightmap_->getWidth(), heightmap_->getHeight());
        if (config_.legoTerrainEnabled && !initLegoLayout()) return false;
    }

    {
        render::PrimitivePathConfig primitiveConfig;
        primitiveConfig.shaderPath = config_.shaderDir / "physics_primitives.wgsl";
        primitiveConfig.colorFormat = config_.colorFormat;
        primitivePath_ = std::make_unique<render::PrimitivePath>();
        if (!primitivePath_->init(device, queue, primitiveConfig)) {
            LOG_ERROR("Failed to initialize physics primitive renderer");
            return false;
        }
        primitivePath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
    }

    LOG_DEBUG("Renderers initialized");
    return true;
}

bool Application::initLegoLayout() {
    if (legoLayoutView_) return true;
    if (!heightmap_ || !blitPath_) return false;
    const auto device = gpuContext_->getDevice();
    const auto queue = gpuContext_->getQueue();
    const terrain::lego::Surface surface{heightmap_->getData(),heightmap_->getWidth(),
        heightmap_->getHeight(),config_.heightScale,config_.cellScale};
    if (!surface.valid() || surface.width > 8192 || surface.height > 8192) return false;
    const bool cached = surface.width > terrain::lego::kMaximumStudySamples
        || surface.height > terrain::lego::kMaximumStudySamples;
    if (cached) {
        const auto desc = gpu::TextureDesc::tex2D(terrain::lego::kCacheCells,
            terrain::lego::kCacheCells,WGPUTextureFormat_R32Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,"lego_world_layout");
        legoLayoutTexture_ = gpu::createTexture(device,desc);
        legoLayoutCache_ = std::make_unique<terrain::lego::LayoutCache>();
    } else {
        const bool shore = config_.legoTerrainEnabled;
        const auto layout = terrain::lego::buildLayout(surface,rendererSettings_.waterHeight,
            shore ? 2816u : 0u,shore ? 7424u : 0u);
        const auto desc = gpu::TextureDesc::tex2D(surface.width,surface.height,WGPUTextureFormat_R16Uint,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,"lego_brick_layout");
        legoLayoutTexture_ = gpu::createTextureWithData(device,queue,desc,
            std::as_bytes(std::span<const uint16_t>(layout.cells)),surface.width*sizeof(uint16_t));
    }
    if (legoLayoutTexture_) legoLayoutView_ = gpu::createTextureView(legoLayoutTexture_);
    if (!legoLayoutView_) {
        if (legoLayoutTexture_) wgpuTextureRelease(legoLayoutTexture_);
        legoLayoutTexture_ = nullptr;
        legoLayoutCache_.reset();
        LOG_ERROR("Could not allocate LEGO layout texture");
        return false;
    }
    blitPath_->setLegoLayoutTexture(legoLayoutView_);
    LOG_INFO("LEGO layout: {} ({} bytes)",cached ? "streamed world" : "shore study",
        cached ? terrain::lego::kCacheGpuBytes : surface.width*surface.height*2u);
    return true;
}

void Application::updateLegoLayout() {
    if (!legoLayoutCache_ || !legoMode_ || !camera_ || !heightmap_) return;
    const terrain::lego::Surface surface{heightmap_->getData(),heightmap_->getWidth(),
        heightmap_->getHeight(),config_.heightScale,config_.cellScale};
    const auto position = physics::worldPositionToAbsolute(physics::canonicalWorldPosition(
        camera_->worldSector(),glm::dvec3(camera_->position())));
    legoLayoutCache_->request(surface.width,surface.height,
        (glm::vec2(float(position.x),float(position.z))+surface.origin())/surface.cellScale);
    std::array<uint32_t,terrain::lego::kChunkCells*terrain::lego::kChunkCells> packed;
    for (uint32_t i=0; i<terrain::lego::kChunksPerFrame; ++i) {
        const auto request = legoLayoutCache_->next();
        if (!request) break;
        const auto chunk = terrain::lego::buildChunk(surface,request->x,request->z,rendererSettings_.waterHeight);
        for (size_t j=0; j<packed.size(); ++j)
            packed[j] = terrain::lego::LayoutCache::pack(chunk.cells[j],request->key);
        const WGPUOrigin3D origin{(request->x % terrain::lego::kCacheChunks)*terrain::lego::kChunkCells,
            (request->z % terrain::lego::kCacheChunks)*terrain::lego::kChunkCells,0};
        if (!gpu::writeTexture(gpuContext_->getQueue(),legoLayoutTexture_,
                std::as_bytes(std::span<const uint32_t>(packed)),terrain::lego::kChunkCells,
                terrain::lego::kChunkCells,terrain::lego::kChunkCells*sizeof(uint32_t),0,origin)) {
            legoLayoutCache_->invalidate();
            break;
        }
        legoLayoutCache_->uploaded(*request);
        blitPath_->invalidateLegoLayout();
    }
}

float Application::sampleTerrainHeight(float worldX, float worldZ) const {
    if (!heightmap_ || !heightmap_->isLoaded()
        || !std::isfinite(worldX) || !std::isfinite(worldZ)
        || !std::isfinite(config_.cellScale) || config_.cellScale <= 0.0f) {
        return 0.0f;
    }

    const glm::vec2 origin = physics::terrain_topology::centeredOrigin(
        heightmap_->getWidth(), heightmap_->getHeight(), config_.cellScale);
    if (config_.legoTerrainEnabled || legoMode_) {
        return terrain::lego::Surface{heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),
            config_.heightScale,config_.cellScale}.heightAt(worldX,worldZ);
    }
    const float sampleX = (worldX + origin.x) / config_.cellScale;
    const float sampleZ = (worldZ + origin.y) / config_.cellScale;
    return physics::terrain_topology::worldHeight(
        heightmap_->sampleBilinear(sampleX, sampleZ), config_.heightScale);
}

glm::vec3 Application::findMotoSpawn() const {
    if (!heightmap_ || !heightmap_->isLoaded()) {
        return {0.0f, 1.0f, 0.0f};
    }

    const glm::vec2 origin = physics::terrain_topology::centeredOrigin(
        heightmap_->getWidth(), heightmap_->getHeight(), config_.cellScale);
    const float searchRadius = std::max(
        0.0f, std::min({600.0f, origin.x * 0.9f, origin.y * 0.9f}));
    constexpr float spacing = 20.0f;
    constexpr float slopeProbe = 1.5f;
    float bestScore = std::numeric_limits<float>::infinity();
    glm::vec3 best(0.0f, sampleTerrainHeight(0.0f, 0.0f), 0.0f);

    for (float z = -searchRadius; z <= searchRadius; z += spacing) {
        for (float x = -searchRadius; x <= searchRadius; x += spacing) {
            const float center = sampleTerrainHeight(x, z);
            const float slope =
                std::abs(sampleTerrainHeight(x + slopeProbe, z)
                         - sampleTerrainHeight(x - slopeProbe, z))
                + std::abs(sampleTerrainHeight(x, z + slopeProbe)
                           - sampleTerrainHeight(x, z - slopeProbe));
            const float waterPenalty = std::max(
                rendererSettings_.waterHeight + 2.0f - center, 0.0f)
                * 1'000.0f;
            const float distancePenalty =
                0.0005f * (x * x + z * z);
            const float score = slope + waterPenalty + distancePenalty;
            if (score < bestScore) {
                bestScore = score;
                best = {x, center, z};
            }
        }
    }

    best.y += 1.0f;
    return best;
}

bool Application::initMoto() {
    if (!config_.motoEnabled || config_.wreckwaterClient || config_.benchmarkOnStartup
        || config_.exitAfterBenchmark || config_.cubePyramidBodyCount != 0u) {
        return true;
    }

    render::MeshPathConfig meshConfig;
    meshConfig.shaderPath = config_.shaderDir / "mesh_path.wgsl";
    meshConfig.colorFormat = config_.colorFormat;
    meshPath_ = std::make_unique<render::MeshPath>();
    if (!meshPath_->init(gpuContext_->getDevice(), gpuContext_->getQueue(),
                         meshConfig)) {
        LOG_ERROR("Failed to initialize RIDGEBREAK mesh renderer");
        return false;
    }
    meshPath_->setEnvironmentTexture(blitPath_->getEnvironmentTextureView());
    meshPath_->setRayDepthTexture(raycastPath_->getDepthOutputView());
    if (!meshPath_->loadMesh("data/moto/bike.vmesh")
        || !meshPath_->loadMesh("data/moto/rider.vmesh")
        || !meshPath_->loadMesh("data/moto/track.vmesh")) {
        LOG_ERROR("Failed to load RIDGEBREAK bike, rider and track meshes");
        return false;
    }

    moto::MotoSessionConfig sessionConfig;
    sessionConfig.spawnPosition = motoWorldSpawnValid_
        ? motoWorldSpawn_ : findMotoSpawn();
    sessionConfig.spawnYaw = motoWorldSpawnValid_ ? motoWorldSpawnYaw_ : 0.0f;
    sessionConfig.waterHeight = rendererSettings_.waterHeight;
    motoSession_ = std::make_unique<moto::MotoSession>();
    std::string error;
    if (!motoSession_->initialize(sessionConfig, &error)) {
        LOG_ERROR("Failed to initialize RIDGEBREAK session: {}", error);
        return false;
    }

    std::vector<moto::RaceCheckpoint> raceCheckpoints;
    if (!motoRaceRoute_.empty()
        && !moto::buildDirectedRaceCheckpoints(
            motoRaceRoute_, 18.0f, 12.0f, &raceCheckpoints, &error)) {
        LOG_ERROR("Failed to build RIDGEBREAK race gates: {}", error);
        return false;
    }
    for (moto::RaceCheckpoint& checkpoint : raceCheckpoints) {
        checkpoint.center.y = sampleTerrainHeight(
            checkpoint.center.x, checkpoint.center.z) + 2.0f;
    }

    moto::RaceConfig raceConfig = moto::makeUntimedPracticeRaceConfig();
    // Launch into honest, untimed practice. The authored circuit is validated
    // above but does not start until C explicitly moves the rider to its grid
    // and resets race authority.
    motoRaceSession_ = std::make_unique<moto::RaceSession>();
    if (!motoRaceSession_->configure(raceConfig, raceCheckpoints, &error)
        || !motoRaceSession_->join(kLocalMotoRacePlayer)
        || !motoRaceSession_->start()) {
        LOG_ERROR("Failed to initialize RIDGEBREAK race rules: {}", error);
        return false;
    }
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = motoSession_->bikeState().tricksLanded;

    const moto::MotoCameraPose& pose = motoSession_->cameraPose();
    camera_->setWorldPosition({0, 0, 0}, pose.position);
    camera_->lookAt(pose.target);
    camera_->setFovYDegrees(55.0f);
    LOG_INFO("RIDGEBREAK untimed practice spawn: ({:.1f}, {:.1f}, {:.1f}), "
             "{} validated circuit gates",
             sessionConfig.spawnPosition.x, sessionConfig.spawnPosition.y,
             sessionConfig.spawnPosition.z, raceCheckpoints.size());
    return true;
}

bool Application::startMotoCircuit() {
    if (!motoSession_ || !motoSession_->isInitialized()
        || motoRaceRoute_.empty()) {
        return false;
    }

    std::string error;
    std::vector<moto::RaceCheckpoint> checkpoints;
    if (!moto::buildDirectedRaceCheckpoints(
            motoRaceRoute_, 18.0f, 12.0f, &checkpoints, &error)) {
        LOG_ERROR("Failed to build circuit gates: {}", error);
        return false;
    }
    for (moto::RaceCheckpoint& checkpoint : checkpoints) {
        checkpoint.center.y = sampleTerrainHeight(
            checkpoint.center.x, checkpoint.center.z) + 2.0f;
    }

    moto::CircuitGridPose grid;
    if (!moto::buildCircuitGridPose(
            checkpoints, 10.0f, &grid, &error)) {
        LOG_ERROR("Failed to build circuit grid: {}", error);
        return false;
    }
    grid.position.y = sampleTerrainHeight(
        grid.position.x, grid.position.z) + 1.0f;

    auto stagedRace = std::make_unique<moto::RaceSession>();
    const moto::RaceConfig circuitConfig;
    if (!stagedRace->configure(circuitConfig, checkpoints, &error)
        || !stagedRace->join(kLocalMotoRacePlayer)
        || !stagedRace->start()) {
        LOG_ERROR("Failed to stage circuit authority: {}", error);
        return false;
    }

    motoSession_->resetAt(grid.position, grid.yaw);
    motoRaceSession_ = std::move(stagedRace);
    motoRaceTrickSequence_ = 0u;
    motoRaceLandingIdentity_ = 0u;
    motoRaceObservedTricksLanded_ = 0u;
    LOG_INFO("RIDGEBREAK circuit staged: {} gates, {:.1f}s countdown, "
             "{:.1f}s race budget",
             checkpoints.size(),
             static_cast<float>(circuitConfig.countdownTicks)
                 / static_cast<float>(circuitConfig.tickRate),
             static_cast<float>(circuitConfig.durationTicks)
                 / static_cast<float>(circuitConfig.tickRate));
    return true;
}

void Application::setupCallbacks() {
#if defined(VOXY_NATIVE)
    if (!window_) return;

    // Resize callback - use shared onResize() method
    window_->setResizeCallback([this](int width, int height) {
        if (width <= 0 || height <= 0) return;
        onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    });

    // Close callback
    window_->setCloseCallback([this]() {
        LOG_DEBUG("Window close requested");
        requestExit();
    });
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering Helpers
// ─────────────────────────────────────────────────────────────────────────────

void Application::pollRenderGpuTimings() {
    auto raw = renderGpuReadback_.poll();
    if (!raw || raw->bytes.size()
        != kRenderGpuTimestampCount * sizeof(uint64_t)) return;

    std::array<uint64_t, kRenderGpuTimestampCount> timestamps{};
    std::memcpy(timestamps.data(), raw->bytes.data(), raw->bytes.size());
    RenderGpuStageTiming timing;
    timing.frame = raw->tick;
    const double tickToMilliseconds =
        config_.gpuPhysicsTimestampPeriodNanoseconds * 1.0e-6;
    for (size_t stage = 0; stage < kRenderGpuStageCount; ++stage) {
        const uint64_t start =
            timestamps[stage * kRenderGpuQueriesPerStage];
        const uint64_t end =
            timestamps[stage * kRenderGpuQueriesPerStage + 1u];
        if (end >= start) {
            timing.milliseconds[stage] = static_cast<double>(
                end - start) * tickToMilliseconds;
        }
    }
    const uint64_t frameBegin = timestamps[kRenderGpuFrameBeginQuery];
    const uint64_t frameEnd = timestamps[kRenderGpuFrameEndQuery];
    timing.frameIntervalAvailable = frameBegin != 0u && frameEnd >= frameBegin;
    if (timing.frameIntervalAvailable) {
        timing.frameMilliseconds = static_cast<double>(frameEnd - frameBegin)
            * tickToMilliseconds;
    }
    timing.renderWidth = raw->firstBody;
    timing.renderHeight = raw->bodyCount;
    stats_.renderGpuTiming = timing;
    if (renderGpuTimingSampleCount_ == kRenderGpuTimingSampleCapacity) {
        renderGpuTimingSampleHead_ =
            (renderGpuTimingSampleHead_ + 1u)
            % kRenderGpuTimingSampleCapacity;
        --renderGpuTimingSampleCount_;
    }
    const size_t destination =
        (renderGpuTimingSampleHead_ + renderGpuTimingSampleCount_)
        % kRenderGpuTimingSampleCapacity;
    renderGpuTimingSamples_[destination] = timing;
    ++renderGpuTimingSampleCount_;
}

void Application::clearRayObjectDepth(WGPUCommandEncoder encoder) {
    WGPUTextureView depthView = getOrCreateDepthView();
    if (!encoder || !depthView) return;

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor descriptor{};
    WGPU_SET_LABEL(descriptor, "ray_object_depth_clear");
    descriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &descriptor);
    if (!pass) return;
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

bool Application::renderSalvageAsset(WGPUCommandEncoder encoder, WGPUTextureView colorView,
    render::SalvageFixtureTicket& ticket, WGPUTextureView linearDepthOutput,
    render::SceneShadowConsumer beforeColor) {
    if (!salvageLocalSession_ || !salvageLocalSession_->asset || !camera_) return true;
    auto& asset = *salvageLocalSession_->asset;
    if (asset.leaving || asset.status != render::SalvageFixtureStatus::Active
        || salvagePreviewFailed_) return true;
    auto& presentation=asset.characterPresentation;
    if(asset.workshopOpen)presentation.valid=false;
    const bool hold=asset.player&&!asset.workshopOpen&&!asset.characterViewReady;
    // Before the first complete camera packet there is no safe actor frame.
    // GPU simulation/readback keeps progressing through the normal frame.
    if(hold&&!presentation.valid)return true;
    const auto depth = getOrCreateDepthView();
    if (!depth) return false;
    const auto cameraSectorOrigin = glm::dvec3(camera_->worldSector()) * static_cast<double>(physics::kWorldSectorSize);
    const auto root = glm::translate(glm::dmat4(1.0), asset.origin - cameraSectorOrigin);
    const auto viewProjection = glm::dmat4(camera_->projectionMatrix() * camera_->viewMatrix());
    std::array<render::SalvageFixturePlacement, render::SalvageAssetFixture::maximumPlacements> placements{};
    size_t placementCount=0;
    render::SalvageFixtureFrame frame;
    game::assets::RigidAnimationPose robotPose;
    std::array<render::SalvageFixtureSolid,10> harborSolids;
    std::array<std::array<glm::vec3,2>,4> harborCables;
    size_t cableCount=0;
    if(!hold) {
    const auto visualTick=asset.player?asset.player->collisionTick():0;
    const double surfaceSeconds=visualTick>asset.surfaceTick && asset.pause==SalvageLocalSessionState::AssetPreview::Pause::Running
        && !asset.workshopOpen?std::min(1.0,double(visualTick-asset.surfaceTick)/60.0):0.0;
    std::array<SalvageLocalSessionState::AssetPreview::SurfaceState,render::SalvageAssetFixture::maximumPlacements> nextSurfaces{};
    size_t nextSurfaceCount=0;
    asset.mechanismPlacements=0;
    const auto& visibleScene=asset.workshopOpen?asset.workshop->preview():asset.acceptedScene();
    for (size_t i = 0; i < visibleScene.registry.placements.size(); ++i) {
        const auto* navigation=asset.content->registry.navigation?&*asset.content->registry.navigation:nullptr;
        const bool boatSlot=navigation && (i>=asset.content->registry.placements.size()
            || std::find(navigation->boatPlacements.begin(),navigation->boatPlacements.end(),i)!=navigation->boatPlacements.end());
        const bool designPart=boatSlot && asset.workshopOpen;
        const auto& scene=designPart?asset.workshop->preview():asset.acceptedScene();
        if(boatSlot) {
            const auto& members=scene.registry.navigation->boatPlacements;
            if(std::find(members.begin(),members.end(),i)==members.end())continue;
        }
        const auto& source=scene.registry.placements[i];
        auto& rendered=placements[placementCount++];
        rendered = {.bundleIndex = source.bundleIndex, .lodId = 0,
            .cameraRelativeRoot = root, .placement = source.placement, .prototype = source.prototype};
        if(asset.player)rendered.surface={0,0,1,0};
        if(!source.prototype && game::expedition::isPaintableBrick(scene.bundles[source.bundleIndex]->sidecar().part.nameKey)) {
            const auto paint=source.paint.value_or(game::expedition::kOriginalBrickPaint);
            if(paint!=game::expedition::kOriginalBrickPaint)rendered.baseColorOverride=render::opaqueSrgbPaintOverride(paint);
        }
        if(designPart) {
            rendered.cameraRelativeRoot=glm::translate(root,glm::dvec3(0,4,0));
            if(asset.workshop->isSelected(static_cast<uint32_t>(i)))rendered.tint=!asset.workshop->valid()
                ? glm::vec4(1.f,.22f,.18f,1.f)
                : asset.workshopPointerPlacement&&!asset.workshopPointerTarget
                    ? glm::vec4(1.f,.75f,.25f,1.f):glm::vec4(.45f,1.f,.55f,1.f);
        }
        if (asset.content->assembly && !asset.liveScene && i<asset.content->connectedSockets.size()) rendered.selectedSockets = asset.content->connectedSockets[i];
        for(const auto& [assembly,body]:{std::pair{asset.boat.get(),asset.boat?asset.boatRoot().body:physics::BodyHandle{}},std::pair{asset.cargo.get(),asset.cargoBody}}) {
            if(!assembly || !body.valid() || (designPart && assembly==asset.boat.get())) continue;
            for (const auto& part:assembly->parts()) if (part.placement==i) {
                const auto& massParts=assembly->assembly().mass().parts();
                const auto member=std::find_if(massParts.begin(),massParts.end(),[&](const auto& p){return p.part==part.id;});
                if (member==massParts.end()) return false;
                if(assembly==asset.boat.get()) {
                    const auto index=asset.boatRoots->indexForPart(*asset.boat,part.id);
                    if(!index || !asset.boatRoots->roots()[*index].body.valid())return false;
                    rendered.physicsBody=asset.boatRoots->roots()[*index].body;
                    if(!source.prototype) {
                        const auto& art=asset.content->renderBundles()[source.bundleIndex];
                        const auto lods=art->lods();
                        const auto& binding=lods.front().prefab.mechanism;
                        const auto* module=assembly->assembly().functions().module(part.id);
                        if(binding && module) {
                            using Kind=game::assets::RigidMechanismKind;
                            if(binding->kind==Kind::PropellerRotor
                                && std::holds_alternative<game::construction::PropellerModule>(module->parameters)) {
                                const double angle=assembly->primaryRoot().propeller==part.id?asset.mechanisms.rotorRadians():0;
                                rendered.mechanism=game::assets::RigidMechanismPose{Kind::PropellerRotor,angle};
                            } else if(binding->kind==Kind::WinchDrum && *index==asset.towRootIndex
                                && std::holds_alternative<game::construction::WinchModule>(module->parameters))
                                rendered.mechanism=game::assets::RigidMechanismPose{Kind::WinchDrum,asset.mechanisms.drumRadians()};
                            if(rendered.mechanism)++asset.mechanismPlacements;
                        }
                    }
                } else rendered.physicsBody=body;
                // Cove third-person presentation uses the same completed
                // packet as character collision and camera sweeps. Other
                // routes retain direct GPU body transforms.
                const physics::AuthoredRootMotion* motion=&asset.cargoObserved;
                if(assembly==asset.boat.get()) {
                    const auto index=asset.boatRoots->indexForPart(*asset.boat,part.id);
                    if(!index)return false;
                    motion=&asset.boatRoots->roots()[*index].observed;
                }
                rendered.physicsBody={};
                rendered.cameraRelativeRoot=glm::translate(glm::dmat4(1),physics::worldPositionToAbsolute(motion->position)-cameraSectorOrigin)
                    *glm::mat4_cast(glm::normalize(glm::dquat(motion->orientation)));
                rendered.placement=member->rootFromPart;
                if(asset.player && !source.prototype) {
                    const auto& prior=asset.surfaceStates;
                    const auto old=std::find_if(prior.begin(),prior.end(),[&](const auto& s){return s.part==part.id;});
                    float wet=old==prior.end()?0.f:std::max(0.f,old->wet-float(surfaceSeconds/8.0));
                    const auto& bounds=asset.content->renderBundles()[source.bundleIndex]->lods().front().prefab.canonicalBounds;
                    const auto rotation=game::construction::rotationMatrix(member->rootFromPart.rotation);
                    const auto p=member->rootFromPart.translation;
                    glm::dmat4 grid(1);grid[3]={double(p.x)*.02,double(p.y)*.02,double(p.z)*.02,1};
                    if(!rotation)return false;
                    for(int c=0;c<3;++c)for(int r=0;r<3;++r)grid[c][r]=rotation->elements[size_t(r*3+c)];
                    const auto world=glm::translate(glm::dmat4(1),cameraSectorOrigin)*rendered.cameraRelativeRoot*grid;
                    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                    for(unsigned corner=0;corner<8;++corner){
                        const auto point=world*glm::dvec4(corner&1?bounds.maximum.x:bounds.minimum.x,
                            corner&2?bounds.maximum.y:bounds.minimum.y,corner&4?bounds.maximum.z:bounds.minimum.z,1);
                        lo=std::min(lo,point.y);hi=std::max(hi,point.y);
                    }
                    const auto center=world*glm::dvec4((bounds.minimum+bounds.maximum)*.5,1);
                    // Cosmetic coverage uses the bounded wave approximation;
                    // physical buoyancy and water pixels retain the GPU field.
                    const auto water=double(rendererSettings_.waterHeight)+double(waterSimulation_?waterSimulation_->sampleSurface(
                        glm::vec2(center.x,center.z),waterPhaseSeconds(),rendererSettings_.waterWaveStrength).heightOffset:0.0f);
                    const auto contact=std::clamp((water-lo)/std::max(.02,hi-lo),0.0,1.0);
                    wet=std::max(wet,float(contact));
                    const auto& parts=assembly->build().parts;
                    const auto condition=std::find_if(parts.begin(),parts.end(),[&](const auto& value){return value.id==part.id;});
                    const float wear=condition==parts.end()?0.f:1.f-float(condition->health)/float(game::construction::kFullHealth);
                    rendered.surface={wet,wear,1,float(contact)};
                    if(nextSurfaceCount>=nextSurfaces.size())return false;
                    nextSurfaces[nextSurfaceCount++]={part.id,wet};
                }
            }
        }
        // Select against the accepted pose, including moved/cut roots and the
        // raised workbench. Durable LOD identities remain unchanged.
        std::optional<uint64_t> lod{0};
        if(!source.prototype)lod=asset.forcedLod?std::optional<uint64_t>(asset.forcedLod)
            :game::assets::selectFixtureLod(*asset.content->renderBundles()[source.bundleIndex],rendered.cameraRelativeRoot,
                rendered.placement,viewProjection,gpuContext_->getSwapchainHeight());
        if(!lod){LOG_ERROR("Asset fixture could not project its bounded LOD envelope");return false;}
        rendered.lodId=*lod;
        if(i<asset.selectedLods.size())asset.selectedLods[i]=*lod;
    }
    if(asset.player&&!asset.workshopOpen){asset.surfaceStates=nextSurfaces;asset.surfaceTick=visualTick;}
#if defined(VOXY_NATIVE)
    if(asset.workshopOpen) {
        uint32_t thumb=0;
        for(uint32_t i=0;i<asset.workshop->catalogCount()&&thumb<render::SalvageAssetFixture::maximumPalettePlacements;++i) {
            if(!asset.workshop->catalogNameAt(i).starts_with("Brick "))continue;
            const auto bundle=asset.workshop->catalogBundleAt(i);if(!bundle)continue;
            const double x=.32+.18*thumb++;
            const auto& projection=camera_->projectionMatrix();
            auto model=glm::inverse(glm::dmat4(camera_->viewMatrix()));
            model=glm::translate(model,glm::dvec3((2*x-1)*2/double(projection[0][0]),-.72*2/double(projection[1][1]),-2));
            model=glm::rotate(model,.48,glm::dvec3(1,0,0));
            model=glm::rotate(model,-.35,glm::dvec3(0,1,0));
            model=glm::scale(model,glm::dvec3(.1));
            auto& thumbnail=placements[placementCount++];
            thumbnail={.bundleIndex=*bundle,.lodId=1,.cameraRelativeRoot=model,.castsSunShadow=false};
            if(const auto paint=asset.workshop->brushPaint();paint&&*paint!=game::expedition::kOriginalBrickPaint)
                thumbnail.baseColorOverride=render::opaqueSrgbPaintOverride(*paint);
        }
    }
#endif
    if(asset.robot&&!asset.workshopOpen&&asset.characterViewReady&&!asset.characterCamera.pose().hideAvatar) {
        const auto model=glm::translate(glm::dmat4(1),asset.origin+asset.player->feet()-cameraSectorOrigin)
            *glm::rotate(glm::dmat4(1),asset.player->facingYaw(),glm::dvec3(0,1,0));
        std::string error;
        if(!asset.character.sample(*asset.robot,model,robotPose,error)){
            LOG_ERROR("Cove robot animation failed: {}",error);return false;
        }
        frame.robot=&robotPose;
    }
    if(asset.player) {
        asset.robotWet=std::max(0.f,asset.robotWet-float(surfaceSeconds/8.0));
        if(asset.player->mode()==game::expedition::CovePlayer::Mode::Swimming)asset.robotWet=1;
        frame.robotSurface={asset.robotWet,0,1,asset.player->mode()==game::expedition::CovePlayer::Mode::Swimming?1.f:0.f};
        frame.environmentSurface={0,0,1,0};frame.ropeSurface={0,0,1,0};
    }
    const auto environmentLod=[&](const auto& lods,const glm::dmat4& model) {
        const auto& b=lods.front().prefab.canonicalBounds;
        double low=std::numeric_limits<double>::infinity(),high=-low;
        for(unsigned corner=0;corner<8;++corner) {
            const auto clip=viewProjection*model*glm::dvec4(corner&1?b.maximum.x:b.minimum.x,
                corner&2?b.maximum.y:b.minimum.y,corner&4?b.maximum.z:b.minimum.z,1);
            if(clip.w<=1e-8)return 0u;
            low=std::min(low,clip.y/clip.w);high=std::max(high,clip.y/clip.w);
        }
        const auto pixels=(high-low)*.5*gpuContext_->getSwapchainHeight();
        return pixels>=320?0u:pixels>=110?1u:2u;
    };
    if(asset.environment&&asset.environmentObserved&&!asset.workshopOpen) {
        frame.sceneryRoot=root;frame.sceneryLod=environmentLod(asset.environment->scenery,root);
    }
    if(asset.dockMarkings && !asset.workshopOpen)frame.dockMarkingsRoot=root;
    if(!asset.workshopOpen && asset.towRope.valid() && !asset.towBroken && asset.towRoot().observedTick>=asset.towChangedTick) {
        const auto a=physics::worldPositionToAbsolute(asset.towRoot().observed.position)
            +glm::dvec3(asset.towRoot().observed.orientation*asset.towBoatPoint)-cameraSectorOrigin;
        const auto b=physics::worldPositionToAbsolute(asset.cargoObserved.position)
            +glm::dvec3(asset.cargoObserved.orientation*asset.towCargoPoint)-cameraSectorOrigin;
        frame.towCable=std::array{glm::vec3(a),glm::vec3(b)};
    }
    if(!asset.workshopOpen&&asset.harbor&&asset.harbor->durable()&&asset.harbor->body().valid()){
        const auto fixed=physics::worldPositionToAbsolute(asset.harbor->motion().position)-cameraSectorOrigin;
        if(asset.environment) {
            frame.gantryRoot=glm::translate(glm::dmat4(1),fixed);
            frame.gantryLod=environmentLod(asset.environment->gantry,*frame.gantryRoot);
        }
        size_t solidIndex=0;
        for(const auto& solid:asset.harbor->structure().structure()){
            const auto a=solid.bounds.minimum,b=solid.bounds.maximum;
            const auto lo=glm::dvec3(a.x,a.y,a.z)*.02,hi=glm::dvec3(b.x,b.y,b.z)*.02;
            harborSolids[solidIndex++]={glm::translate(glm::mat4(1),glm::vec3(fixed+(lo+hi)*.5))
                *glm::scale(glm::mat4(1),glm::vec3(hi-lo)),{.95f,.56f,.035f,1}};
        }
        if(!frame.gantryRoot)frame.harborStructure=harborSolids;
        if(const auto* rig=asset.harbor->rig())for(size_t i=0;i<asset.harbor->ropes().size();++i){
            if(!asset.harbor->ropes()[i].valid()||!asset.harbor->observedRopes()[i].alive
                ||asset.harbor->observedRopes()[i].broken)continue;
            const auto hull=physics::worldPositionToAbsolute(asset.boatRoot().observed.position)-cameraSectorOrigin;
            harborCables[cableCount++]={glm::vec3(fixed)+rig->overheadPoints()[i],
                glm::vec3(hull)+asset.boatRoot().observed.orientation*rig->boatPoints()[i]};
        }
        frame.harborCables=std::span(harborCables).first(cableCount);
    }
    } else {
        placements=presentation.placements;placementCount=presentation.placementCount;
        if(presentation.robotVisible)frame.robot=&presentation.robot;
        frame.harborStructure=std::span(presentation.solids).first(presentation.solidCount);
        frame.harborCables=std::span(presentation.cables).first(presentation.cableCount);
        frame.towCable=presentation.tow;frame.dockMarkingsRoot=presentation.dock;
        frame.sceneryRoot=presentation.scenery;frame.gantryRoot=presentation.gantry;
        frame.sceneryLod=presentation.sceneryLod;frame.gantryLod=presentation.gantryLod;
        frame.robotSurface=presentation.robotSurface;frame.environmentSurface=presentation.environmentSurface;frame.ropeSurface=presentation.ropeSurface;
    }
    frame.view = camera_->viewMatrix(); frame.projection = camera_->projectionMatrix();
    frame.cameraPosition = camera_->position();
    frame.physics=physicsWorld_->renderView();
    frame.worldCamera={camera_->worldSector(),camera_->position()};
    frame.shadowFrameWorldOrigin=glm::vec3(cameraSectorOrigin);
    frame.width = gpuContext_->getSwapchainWidth(); frame.height = gpuContext_->getSwapchainHeight();
    frame.useRayDepth = config_.renderPath == RenderPath::Raycast;
    frame.guides = asset.guides;
    frame.linearDepthOutput = linearDepthOutput;
    frame.beforeColor = beforeColor;
    frame.lighting = {.direction = rendererSettings_.sunDirection,
        .sunColor = rendererSettings_.sunColor, .sunIntensity = rendererSettings_.sunIntensity,
        .ambientColor = rendererSettings_.ambientColor, .ambientIntensity = rendererSettings_.ambientIntensity,
        .fogColor = rendererSettings_.fogColor, .fogDensity = rendererSettings_.fogDensity,
        .exposure = rendererSettings_.exposure};
    std::string error;
    if (!asset.fixture.encode(encoder, colorView, depth,
        std::span(placements).first(placementCount), frame, ticket, error)) {
        LOG_ERROR("Asset fixture frame rejected: {}", error);
        salvagePreviewFailed_ = true; requestExit(); return false;
    }
    if(asset.player&&!asset.workshopOpen&&!hold) {
        presentation.camera=*camera_;presentation.placements=placements;presentation.placementCount=placementCount;
        // Cove guides are off; do not retain borrowed socket spans.
        for(auto& placement:presentation.placements)placement.selectedSockets.reset();
        presentation.robotVisible=frame.robot!=nullptr;if(frame.robot)presentation.robot=*frame.robot;
        presentation.solidCount=frame.harborStructure.size();presentation.cableCount=frame.harborCables.size();
        std::copy(frame.harborStructure.begin(),frame.harborStructure.end(),presentation.solids.begin());
        std::copy(frame.harborCables.begin(),frame.harborCables.end(),presentation.cables.begin());
        presentation.tow=frame.towCable;presentation.dock=frame.dockMarkingsRoot;
        presentation.scenery=frame.sceneryRoot;presentation.gantry=frame.gantryRoot;
        presentation.sceneryLod=frame.sceneryLod;presentation.gantryLod=frame.gantryLod;
        presentation.robotSurface=frame.robotSurface;presentation.environmentSurface=frame.environmentSurface;presentation.ropeSurface=frame.ropeSurface;
        presentation.tick=asset.player->collisionTick();presentation.valid=true;
    }
    return true;
}

void Application::renderMoto(WGPUCommandEncoder encoder,
                             WGPUTextureView colorView) {
    if (!meshPath_ || !meshPath_->isInitialized()
        || !motoSession_ || !motoSession_->isInitialized() || !camera_
        || isBenchmarkRunning()
        || (browserJourneyBenchmark_ && browserJourneyBenchmark_->isRunning())) {
        return;
    }

    meshPath_->clearInstances();
    for (const moto::MotoPartPose& pose : motoSession_->partPoses()) {
        meshPath_->addInstance({
            .assetIndex = pose.assetIndex,
            .meshIndex = pose.meshIndex,
            .modelMatrix = pose.modelMatrix,
            .tintColor = pose.tintColor,
            .emissiveBoost = pose.emissiveBoost,
        });
    }
    for (const MotoTrackRenderPose& pose : motoTrackPoses_) {
        meshPath_->addInstance({
            .assetIndex = 2u,
            .meshIndex = pose.meshIndex,
            .modelMatrix = pose.modelMatrix,
            .tintColor = pose.tintColor,
        });
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) return;
    const render::PrimitiveLighting lighting{
        .direction = rendererSettings_.sunDirection,
        .sunColor = rendererSettings_.sunColor,
        .sunIntensity = rendererSettings_.sunIntensity,
        .ambientColor = rendererSettings_.ambientColor,
        .ambientIntensity = rendererSettings_.ambientIntensity,
        .fogColor = rendererSettings_.fogColor,
        .fogDensity = rendererSettings_.fogDensity,
        .exposure = rendererSettings_.exposure,
    };
    meshPath_->render(
        encoder, colorView, depthView, camera_->viewMatrix(),
        camera_->projectionMatrix(), camera_->position(), lighting,
        gpuContext_->getSwapchainWidth(), gpuContext_->getSwapchainHeight(),
        config_.renderPath == RenderPath::Raycast);
}

void Application::renderTrianglePath(WGPUCommandEncoder encoder, WGPUTextureView colorView) {
    if (!trianglePath_ || !trianglePath_->isInitialized()) {
        return;
    }

    WGPUTextureView depthView = getOrCreateDepthView();
    if (!depthView) {
        return;
    }

    trianglePath_->render(encoder, colorView, depthView);
}

bool Application::renderRaycastPath(WGPUCommandEncoder encoder, WGPUTextureView colorView,
    render::SalvageFixtureTicket& ticket) {
    if (!raycastPath_ || !raycastPath_->isInitialized()) {
        return false;
    }
    if (!blitPath_ || !blitPath_->isInitialized()) {
        return false;
    }

    // Dispatch ray-cast compute shader
    constexpr uint32_t raycastStage =
        static_cast<uint32_t>(RenderGpuStage::TerrainRaycast);
    raycastPath_->dispatch(
        encoder, renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        raycastStage * kRenderGpuQueriesPerStage,
        raycastStage * kRenderGpuQueriesPerStage + 1u,
        true);

    if (raycastPath_->isUsingStaticCache()) {
        ++stats_.raycastStaticCacheFrames;
    }
    if (raycastPath_->didRefreshStaticCache()) {
        ++stats_.raycastTerrainCacheRefreshes;
    }

    blitPath_->setStaticCacheState(
        raycastPath_->isUsingStaticCache(),
        raycastPath_->didRefreshStaticCache());
    blitPath_->setLinearDepthRequired(
        (primitivePath_ && primitivePath_->isInitialized()
         && (stats_.physicsResidentBodies != 0u || legoPlayground_))
        || (meshPath_ && meshPath_->isInitialized()
            && motoSession_ && motoSession_->isInitialized())
        || adventure_
        || (salvageLocalSession_ && salvageLocalSession_->asset
            && !salvageLocalSession_->asset->leaving));
    // Render blit pass
    constexpr uint32_t blitStage =
        static_cast<uint32_t>(RenderGpuStage::LightingBlit);
    struct DrawContext { Application* app; render::SalvageFixtureTicket* ticket; } context{this, &ticket};
    render::OpaqueSceneDraw opaque{};
    if (config_.salvageAssetFixtureWaterAnchor || adventure_) {
        clearRayObjectDepth(encoder);
        opaque.context = &context;
        opaque.encode = [](void* opaqueContext, WGPUCommandEncoder commands,
                           WGPUTextureView color, WGPUTextureView depth, render::SceneShadowConsumer background) {
            auto& draw = *static_cast<DrawContext*>(opaqueContext);
            auto& app=*draw.app;
            if(app.adventure_) {
                const render::PrimitiveLighting lighting{
                    .direction=app.rendererSettings_.sunDirection,.sunColor=app.rendererSettings_.sunColor,
                    .sunIntensity=app.rendererSettings_.sunIntensity,.ambientColor=app.rendererSettings_.ambientColor,
                    .ambientIntensity=app.rendererSettings_.ambientIntensity,.fogColor=app.rendererSettings_.fogColor,
                    .fogDensity=app.rendererSettings_.fogDensity,.exposure=app.rendererSettings_.exposure};
                return app.adventure_->render(commands,color,app.getOrCreateDepthView(),depth,
                    app.blitPath_->getEnvironmentTextureView(),app.raycastPath_->getTerrainDepthCacheView(),*app.camera_,lighting,
                    app.gpuContext_->getSwapchainWidth(),app.gpuContext_->getSwapchainHeight(),background);
            }
            return app.renderSalvageAsset(commands, color, *draw.ticket, depth, background);
        };
    }
    try {
    if (!blitPath_->render(
        encoder, colorView,
        renderGpuProfilingFrame_ ? renderGpuQuerySet_ : nullptr,
        blitStage * kRenderGpuQueriesPerStage,
        blitStage * kRenderGpuQueriesPerStage + 1u, opaque)) return false;
    } catch (const std::exception& failure) {
        LOG_ERROR("Opaque scene frame failed: {}", failure.what());
        salvagePreviewFailed_ = true;
        requestExit();
        return false;
    }
    if(salvageLocalSession_&&salvageLocalSession_->asset) {
        auto& local=*salvageLocalSession_;auto& asset=*local.asset;
        asset.effectsPath.clearEncodedObservation();
        if(asset.effectsPath.initialized()&&waterSimulation_&&waterSimulation_->isInitialized()
            &&!asset.leaving&&!asset.workshopOpen&&asset.status==render::SalvageFixtureStatus::Active
            &&asset.characterPresentation.valid) {
            const auto& camera=blitPath_->getUniforms();
            if(!observeCoveEffects(local,*waterSimulation_,physicsWorld_->tickFrontier().incarnation,
                camera.waterParams.x,camera.waterParams.z,camera.waterMotion.x)) {
                // A cosmetic packet refusal never submits a partial effect or
                // changes physical ownership; the previous accepted pool stays.
                if(asset.effects.stats().rejected<=1)LOG_WARN("Cove effects refused an incoherent or out-of-bounds visual packet");
            }
            if(asset.effects.stats().tick==asset.characterPresentation.tick) {
                const render::CoveEffectsFrame frame{&camera,asset.origin,
                    {waterSimulation_->getOutputView(),waterSimulation_->getSampler(),camera.waterParams.z,
                        camera.waterSpectrum.x,camera.waterSpectrum.y}};
                if(!asset.effectsPath.render(encoder,colorView,blitPath_->finalLinearDepthView(),frame,asset.effects.instances()))return false;
            }
        }
    }
    if (blitPath_->didUseGeometryWaterPath()) {
        ++stats_.geometryWaterFrames;
    }
    return true;
}

float Application::waterPhaseSeconds() const noexcept {
    const auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
    const double seconds=asset && asset->player ? asset->waterTime : stats_.totalTimeSeconds;
    const float phase=static_cast<float>(std::fmod(seconds,4096.0));
    return phase<4096.0f?phase:0.0f;
}

void Application::updateCameraUniforms() {
    if (!camera_) return;

    const auto& cameraView = camera_->viewMatrix();
    const auto& proj = camera_->projectionMatrix();
    const auto& pos = camera_->position();
    const physics::WorldPosition cameraWorld =
        physics::canonicalWorldPosition(
            camera_->worldSector(), glm::dvec3(pos));
    const glm::dvec3 terrainPosition64 =
        physics::worldPositionToAbsolute(cameraWorld);
    const glm::vec3 terrainPosition{
        static_cast<float>(terrainPosition64.x),
        static_cast<float>(terrainPosition64.y),
        static_cast<float>(terrainPosition64.z)};
    // Terrain remains authored in sector-zero coordinates. Keep the camera's
    // stable rotation, but reconstruct its terrain-space translation after the
    // camera local position crosses a sector boundary. Physics primitives use
    // cameraView separately and remain camera-relative.
    const glm::mat4 terrainViewRotation{glm::mat3(cameraView)};
    const glm::mat4 terrainView = terrainViewRotation
        * glm::translate(glm::mat4(1.0f), -terrainPosition);
    const float ambient = rendererSettings_.ambientIntensity;
    const uint32_t terrainWidth = heightmap_ ? heightmap_->getWidth() : stats_.terrainWidth;
    const uint32_t terrainHeight = heightmap_ ? heightmap_->getHeight() : stats_.terrainHeight;
    const uint32_t lodStep =
        (config_.renderPath == RenderPath::Triangle && trianglePath_) ? trianglePath_->getLODStep() : 1u;
    const glm::vec3 worldLightDir = rendererSettings_.sunDirection;

    render::CameraUniforms uniforms;
    if (!uniforms.setTerrain(
            std::max(terrainWidth, 1u), std::max(terrainHeight, 1u),
            config_.heightScale, config_.cellScale,
            static_cast<float>(lodStep), rendererSettings_.fogDensity)
        || !uniforms.setCamera(terrainView, proj, terrainPosition)
        || !uniforms.setLightDirection(
            worldLightDir, terrainView, ambient)
        || !uniforms.setWater(
            rendererSettings_.waterEnabled,
            rendererSettings_.waterHeight,
            rendererSettings_.waterShallowColor,
            rendererSettings_.waterDeepColor,
            rendererSettings_.waterRoughness,
            rendererSettings_.waterWaveStrength,
            rendererSettings_.waterReflectionStrength,
            rendererSettings_.waterShoreFade)
        || !uniforms.setRendererMaterial(
            rendererSettings_.sunColor,
            rendererSettings_.sunIntensity,
            rendererSettings_.ambientColor,
            rendererSettings_.fogColor,
            rendererSettings_.exposure,
            rendererSettings_.waterIor,
            rendererSettings_.waterDistortion,
            rendererSettings_.waterAbsorptionScale,
            rendererSettings_.waterScatterStrength,
            rendererSettings_.waterFoamSize,
            rendererSettings_.waterFoamOpacity,
            rendererSettings_.waterFoamCoverage,
            rendererSettings_.waterReflectionDistance,
            rendererSettings_.waterSpectrum.patchLengths)) {
        LOG_ERROR("Application: refused invalid camera uniform state");
        return;
    }
    uniforms.setLegoMode(legoMode_);
    // K compares grouping only in this physical LEGO scene. It never changes
    // the ground under a resting object or invalidates contact feature IDs.
    if (config_.legoTerrainEnabled || legoMode_)
        uniforms.invProjParams.z = legoLayoutCache_ ? (legoMode_ ? 4.0f : 5.0f)
                                                  : (legoMode_ ? 2.0f : 3.0f);
    uniforms.invProjParams.w = config_.motoEnabled ? 1.0f : 0.0f;
    // Wrap before fp32 loses the sub-frame precision used by short waves.
    const float waterTime = waterPhaseSeconds();
    if (!uniforms.setWaterTime(waterTime)) return;
    float cameraSurfaceOffset = 0.0f;
    if (rendererSettings_.waterEnabled && waterSimulation_ &&
        waterSimulation_->isInitialized()) {
        cameraSurfaceOffset = waterSimulation_->sampleSurface(
            glm::vec2{terrainPosition.x, terrainPosition.z}, waterTime,
            rendererSettings_.waterWaveStrength).heightOffset;
    }
    if (!uniforms.setCameraWaterSurfaceOffset(cameraSurfaceOffset)) return;

    if (config_.renderPath == RenderPath::Triangle &&
        trianglePath_ && trianglePath_->isInitialized()) {
        trianglePath_->setCameraUniforms(uniforms);
    }

    if (config_.renderPath == RenderPath::Raycast) {
        if (raycastPath_ && raycastPath_->isInitialized()) {
            raycastPath_->setCameraUniforms(uniforms);
        }

        if (blitPath_ && blitPath_->isInitialized()) {
            blitPath_->setCameraUniforms(uniforms);
        }
    }
}

void Application::updateStats(float deltaTime) {
    pollRenderGpuTimings();
    stats_.frameTimeMs = static_cast<double>(deltaTime) * 1000.0;
    stats_.totalTimeSeconds += static_cast<double>(deltaTime);
    stats_.activeRenderPath = config_.renderPath;
    if (physicsWorld_) {
        const physics::PhysicsStats physicsStats = physicsWorld_->stats();
        const uint32_t previousVisibleHigh =
            stats_.physics.visibleBodyUsage.highWater;
        stats_.physics = physicsStats;
        stats_.physics.visibleBodyUsage.current =
            stats_.primitiveSubmittedCount;
        stats_.physics.visibleBodyUsage.capacity =
            physicsStats.bodyCapacity;
        stats_.physics.visibleBodyUsage.highWater = std::max(
            previousVisibleHigh, stats_.primitiveSubmittedCount);
        while (auto timing = physicsWorld_->pollGpuStageTimings()) {
            stats_.physicsGpuTiming = *timing;
            if (physicsGpuTimingSampleCount_
                == kPhysicsGpuTimingSampleCapacity) {
                physicsGpuTimingSampleHead_ =
                    (physicsGpuTimingSampleHead_ + 1u)
                    % kPhysicsGpuTimingSampleCapacity;
                --physicsGpuTimingSampleCount_;
            }
            const size_t destination =
                (physicsGpuTimingSampleHead_ + physicsGpuTimingSampleCount_)
                % kPhysicsGpuTimingSampleCapacity;
            physicsGpuTimingSamples_[destination] = std::move(*timing);
            ++physicsGpuTimingSampleCount_;
        }
        stats_.physicsBackend = physicsStats.backend;
        stats_.physicsResidentBodies = physicsStats.residentBodies;
        stats_.physicsActiveBodies = physicsStats.activeBodies;
        stats_.physicsBodyCapacity = physicsStats.bodyCapacity;
        stats_.physicsEstimatedPersistentBytes =
            physicsStats.estimatedPersistentBytes;
        stats_.physicsScratchBytes = physicsStats.scratchBytes;
    }

    // FPS calculation
    fpsAccumulator_ += static_cast<double>(deltaTime);
    fpsFrameCount_++;

    if (fpsAccumulator_ >= static_cast<double>(config_.fpsLogIntervalSeconds)) {
        stats_.avgFrameTimeMs = (fpsAccumulator_ * 1000.0) / fpsFrameCount_;
        stats_.fps = 1000.0 / stats_.avgFrameTimeMs;

        if (config_.showFPS && !getDebugOverlay().isVisible()) {
            // Only log FPS if debug overlay is not visible
            LOG_DEBUG("FPS: {:.1f} ({:.2f} ms) | Path: {}", 
                      stats_.fps, stats_.avgFrameTimeMs, 
                      renderPathToString(config_.renderPath));
        }

        fpsAccumulator_ = 0.0;
        fpsFrameCount_ = 0;
    }
    
    // Update debug overlay
    DebugOverlayStats overlayStats;
    overlayStats.fps = stats_.fps;
    overlayStats.frameTimeMs = stats_.frameTimeMs;
    overlayStats.avgFrameTimeMs = stats_.avgFrameTimeMs;
    overlayStats.frameCount = stats_.frameCount;
    overlayStats.renderPath = config_.renderPath;
    overlayStats.terrainWidth = stats_.terrainWidth;
    overlayStats.terrainHeight = stats_.terrainHeight;
    overlayStats.terrainMipLevels = stats_.terrainMipLevels;
    overlayStats.physics = stats_.physics;
    overlayStats.physicsGpuTiming = stats_.physicsGpuTiming;
    if (stats_.renderGpuTiming) {
        overlayStats.renderGpuMilliseconds =
            stats_.renderGpuTiming->milliseconds;
    }
    
    if (camera_) {
        overlayStats.cameraPosition = camera_->position();
        overlayStats.cameraYaw = camera_->yaw();
        overlayStats.cameraPitch = camera_->pitch();
    }
    
    // Estimate memory usage (heightmap texture only for now)
    if (heightmap_) {
        // Heightmap is R16Uint (2 bytes per pixel)
        size_t baseSize = static_cast<size_t>(stats_.terrainWidth) * 
                          static_cast<size_t>(stats_.terrainHeight) * 2;
        // Account for mip chain (roughly 1.33x base size)
        overlayStats.estimatedMemoryBytes = static_cast<size_t>(static_cast<double>(baseSize) * 1.33);
    }
    overlayStats.estimatedMemoryBytes +=
        stats_.physics.estimatedPersistentBytes + stats_.physics.scratchBytes;
    
    getDebugOverlay().update(overlayStats);
    getDebugOverlay().render();
}

std::optional<physics::PhysicsGpuStageTiming>
Application::pollPhysicsGpuTimingSample() noexcept {
    if (physicsGpuTimingSampleCount_ == 0u) return std::nullopt;
    physics::PhysicsGpuStageTiming result =
        std::move(physicsGpuTimingSamples_[physicsGpuTimingSampleHead_]);
    physicsGpuTimingSampleHead_ =
        (physicsGpuTimingSampleHead_ + 1u)
        % kPhysicsGpuTimingSampleCapacity;
    --physicsGpuTimingSampleCount_;
    return result;
}

std::optional<RenderGpuStageTiming>
Application::pollRenderGpuTimingSample() noexcept {
    if (renderGpuTimingSampleCount_ == 0u) return std::nullopt;
    RenderGpuStageTiming result =
        renderGpuTimingSamples_[renderGpuTimingSampleHead_];
    renderGpuTimingSampleHead_ =
        (renderGpuTimingSampleHead_ + 1u)
        % kRenderGpuTimingSampleCapacity;
    --renderGpuTimingSampleCount_;
    return result;
}

WGPUTextureView Application::getOrCreateDepthView() {
    if (!gpuContext_) return nullptr;

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    // Check if we need to recreate
    if (depthView_ && depthWidth_ == width && depthHeight_ == height) {
        return depthView_;
    }

    // Create depth texture
    WGPUTextureDescriptor desc = {};
    WGPU_SET_LABEL(desc, "depth_texture");
    desc.usage = WGPUTextureUsage_RenderAttachment;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {width, height, 1};
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    WGPUTexture nextTexture =
        wgpuDeviceCreateTexture(gpuContext_->getDevice(), &desc);
    if (!nextTexture) {
        LOG_ERROR("Failed to create depth texture");
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    WGPU_SET_LABEL(viewDesc, "depth_view");
    viewDesc.format = WGPUTextureFormat_Depth32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    WGPUTextureView nextView = wgpuTextureCreateView(nextTexture, &viewDesc);
    if (!nextView) {
        LOG_ERROR("Failed to create depth texture view");
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return nullptr;
    }

    if (depthView_) wgpuTextureViewRelease(depthView_);
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
    }
    depthTexture_ = nextTexture;
    depthView_ = nextView;
    depthWidth_ = width;
    depthHeight_ = height;

    return depthView_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Input Processing
// ─────────────────────────────────────────────────────────────────────────────

bool Application::initSalvagePreview() {
    if (!config_.salvagePreviewEnabled) return true;
    if (config_.salvageAssetFixtureWaterAnchor
        && (config_.renderPath != RenderPath::Raycast || config_.salvageAssetFixtureGuides != "off")) {
        LOG_ERROR("Authored cove requires raycast composition with inspection guides off");
        return false;
    }
    if (!physicsWorld_ || !camera_ || !characterController_ || !heightmap_) return false;
    auto newLocal = std::make_unique<SalvageLocalSessionState>();
    game::construction::CatalogIssue catalogIssue;
    newLocal->catalog = game::construction::PartCatalog::create(
        game::construction::makeStarterCatalogDraft(), catalogIssue);
    if (!newLocal->catalog) {
        LOG_ERROR("Cove session catalog failed validation: {}", static_cast<int>(catalogIssue.error));
        return false;
    }
    game::expedition::SessionBootstrap bootstrap;
    game::expedition::EventStreamIncarnation observer;
    try {
        // A new scene session gets a new namespace. Reset retains this session;
        // neither a static fixture token nor a saved campaign world is reused.
        std::random_device entropy;
        std::uniform_int_distribution<uint32_t> word;
        for (size_t i = 0; i < bootstrap.world.bytes.size(); i += 4) {
            const uint32_t value = word(entropy);
            for (size_t byte = 0; byte < 4; ++byte)
                bootstrap.world.bytes[i + byte] = static_cast<uint8_t>(value >> (byte * 8));
        }
        // Separate public observation identity; never encode a token into it.
        for (size_t i = 0; i < observer.bytes.size(); i += 4) {
            const uint32_t value = word(entropy);
            for (size_t byte = 0; byte < 4; ++byte)
                observer.bytes[i + byte] = static_cast<uint8_t>(value >> (byte * 8));
        }
    } catch (const std::exception& error) {
        LOG_ERROR("Could not initialize cove session identity: {}", error.what());
        return false;
    }
    if(coveResume_)bootstrap.world.bytes=coveResume_->world;
    bootstrap.caller = {{bootstrap.world, 1}, {bootstrap.world, 2}};
    bootstrap.lastIssuedId = 2;
    bootstrap.limits.cargo = 32;
    newLocal->caller=bootstrap.caller;
    game::expedition::SessionIssue sessionIssue;
    salvageLocalSession_ = std::move(newLocal);
    preSalvageCamera_ = CameraState{camera_->position(), camera_->yaw(),
                                   camera_->pitch(), camera_->worldSector()};
    preSalvageController_ = controllerMode_;
    // This authored footprint stays inside the compact shoreline crop. Raise
    // the pier above its highest terrain sample so its deck stays walkable.
    glm::vec3 origin{-12.0f, config_.waterHeight + .7f, -32.0f};
    for (int z = -11; z <= 10; ++z) {
        for (int x = -12; x <= 4; ++x) {
            origin.y = std::max(origin.y, sampleTerrainHeight(origin.x + float(x),
                                                            origin.z + float(z)) + .3f);
        }
    }
    salvagePreview_ = std::make_unique<game::expedition::SalvagePreview>();
    const auto scenery = config_.salvageAssetFixtureRegistry
        ? game::expedition::SalvagePreview::Scenery::Inspection
        : game::expedition::SalvagePreview::Scenery::Cove;
    if (!salvagePreview_->initialize(*physicsWorld_, origin, scenery)) {
        LOG_ERROR("Failed to create cove preview: {}", salvagePreview_->error());
        return false;
    }
    if (salvagePreview_->bodyCount() != 0
        && !salvageMetadataSource_.capture(physicsWorld_->renderView().metadataBuffer)) {
        LOG_ERROR("Cove preview has no readable GPU lifetime metadata.");
        return false;
    }
    if (config_.salvageAssetFixtureRegistry) {
        auto asset = std::make_unique<SalvageLocalSessionState::AssetPreview>();
        std::filesystem::path registry = *config_.salvageAssetFixtureRegistry;
#if defined(VOXY_NATIVE)
        // bazel run exposes data through symlink runfiles. Read the trusted
        // workspace package directly, retaining production no-follow behavior.
        if (registry.is_relative()) {
            if (const char* workspace = std::getenv("BUILD_WORKSPACE_DIRECTORY"); workspace && *workspace)
                registry = std::filesystem::path(workspace) / registry;
        }
#endif
        std::string error;
        asset->content = game::assets::loadAssetFixture(registry, error);
        if (!asset->content) {
            LOG_ERROR("Asset fixture package admission failed: {}", error);
            return false;
        }
        if(config_.salvageAssetFixtureCatalog) {
            std::filesystem::path catalog=*config_.salvageAssetFixtureCatalog;
#if defined(VOXY_NATIVE)
            if(catalog.is_relative())if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)
                catalog=std::filesystem::path(workspace)/catalog;
#endif
            auto expanded=game::assets::appendAssetFixtureCatalog(*asset->content,catalog,error);
            if(!expanded){LOG_ERROR("Construction catalogue admission failed: {}",error);return false;}
            asset->content=std::move(expanded);
        }
        asset->origin = glm::dvec3(origin) + glm::dvec3(-7,3,-5);
        // Cove placements use the declared water datum. Do not lift submerged
        // hulls to hide the pending opaque/water composition work.
        if (config_.salvageAssetFixtureWaterAnchor) asset->origin.y = config_.waterHeight;
        if (config_.salvageAssetFixtureWaterAnchor && asset->content->registry.navigation) {
            asset->boat = game::expedition::CoveBoatAssembly::compile(*asset->content, error);
            if (!asset->boat) {
                LOG_ERROR("Cove boat physical preparation failed: {}", error);
                return false;
            }
            asset->boatRoots=game::expedition::CoveRigidRoots::prepare(*asset->boat,error);
            if(!asset->boatRoots){LOG_ERROR("Cove root ownership: {}",error);return false;}
            const auto& cargo=asset->content->registry.navigation->cargoPlacements;
            if(!cargo.empty()) {
                asset->cargo=game::expedition::CoveBoatAssembly::compileCargo(*asset->content,cargo[0],error);
                if(!asset->cargo) {LOG_ERROR("Cove cargo: {}",error);return false;}
            }
            asset->scenery = game::expedition::CoveSceneryCollision::compile(*asset->content,error);
            if(!asset->scenery) { LOG_ERROR("{}",error); return false; }
            if (physicsWorld_->enableAuthoredShapeResources() != physics::ShapeResourceError::None) {
                LOG_ERROR("Cove sailing requires authored GPU physics"); return false;
            }
            if(!physicsWorld_->setEventReadbackEnabled(true)) {
                LOG_ERROR("Cove sailing requires physics event evidence");return false;
            }
            LOG_INFO("Cove boat prepared: {} welded parts, {} kg dry mass, {} cubic metres displacement",
                asset->boat->parts().size(), asset->boat->primaryMassRoot().mass.dryMassKg,
                asset->boat->displacementCubicMetres());
            asset->player = std::make_unique<game::expedition::CovePlayer>();
            const auto localOrigin = asset->origin;
            if (!asset->player->initialize(*asset->content,
                    [this, localOrigin](double x, double z) {
                        return double(sampleTerrainHeight(float(x + localOrigin.x), float(z + localOrigin.z))) - localOrigin.y;
                    }, error,{},coveGroundSupport(localOrigin))) {
                LOG_ERROR("Cove movement initialization failed: {}", error);
                return false;
            }
        }
        if(asset->player&&!asset->effectsPath.init(gpuContext_->getDevice(),gpuContext_->getQueue(),
            config_.colorFormat,config_.shaderDir/"cove_effects.wgsl")) {
            LOG_ERROR("Cove effects GPU preparation failed");return false;
        }
        asset->forcedLod = config_.salvageAssetFixtureLod;
        if (asset->forcedLod != 0) for (const auto& bundle : asset->content->renderBundles()) {
            if (std::none_of(bundle->lods().begin(), bundle->lods().end(),
                    [id=asset->forcedLod](const auto& lod) { return lod.id == id; })) {
                LOG_ERROR("Requested initial asset inspection detail is unavailable: {}", asset->forcedLod);
                return false;
            }
        }
        asset->guides = config_.salvageAssetFixtureGuides == "dimensions" ? render::InspectionGuides::Dimensions
            : config_.salvageAssetFixtureGuides == "sockets" ? render::InspectionGuides::Sockets : render::InspectionGuides::Off;
        asset->selectedLods.resize(asset->content->registry.placements.size());
        render::SalvageFixtureConfig fixtureConfig;
        fixtureConfig.shaderPath = config_.shaderDir / "mesh_path.wgsl";
        fixtureConfig.linearHdrOutput = config_.salvageAssetFixtureWaterAnchor;
        fixtureConfig.colorFormat = fixtureConfig.linearHdrOutput ? WGPUTextureFormat_RGBA16Float : config_.colorFormat;
        fixtureConfig.filteredEnvironment = config_.salvageAssetFixtureFilteredLighting;
        fixtureConfig.sunShadows = config_.salvageAssetFixtureWaterAnchor;
        fixtureConfig.reservedExternalGpuBytes=asset->player?render::CoveEffectsPath::residentBytes:0;
        if(asset->player) {
            std::filesystem::path robotDirectory="data/salvage/robot-r01";
#if defined(VOXY_NATIVE)
            if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)
                robotDirectory=std::filesystem::path(workspace)/robotDirectory;
#endif
            asset->robot=game::assets::loadRobotAsset(robotDirectory,error);
            if(!asset->robot){LOG_ERROR("Cove robot preparation failed: {}",error);return false;}
            asset->environment=game::assets::loadCoveEnvironment(robotDirectory.parent_path()/"cove-environment-r01",error);
            if(!asset->environment){LOG_ERROR("Cove environment preparation failed: {}",error);return false;}
            asset->environmentCollision=game::expedition::CoveEnvironmentCollision::compile(asset->environment->collision,error);
            if(!asset->environmentCollision){LOG_ERROR("Cove environment collision failed: {}",error);return false;}
            for(const auto& box:asset->environment->collision) {
                const auto a=box.minimum,b=box.maximum;
                asset->environmentObstacles[asset->environmentObstacleCount++]={glm::dvec3(a.x,a.y,a.z)*.02,glm::dvec3(b.x,b.y,b.z)*.02};
            }
            if(!asset->player->setEnvironmentObstacles(asset->environmentObstacleSpan()))return false;
            asset->environmentRetired=false;
            asset->dockMarkings.emplace();
            if(!render::makeCoveDockMarkings(*asset->content,*asset->dockMarkings,error)) {
                LOG_ERROR("Cove dock marking preparation failed: {}",error);return false;
            }
        }
        if (!asset->fixture.init(gpuContext_->getDevice(), gpuContext_->getQueue(), fixtureConfig, error)
            || !asset->fixture.beginCandidate(asset->content->renderBundles(), error, asset->content->prototypes,
                asset->dockMarkings?&*asset->dockMarkings:nullptr,asset->robot,asset->environment)) {
            LOG_ERROR("Asset fixture initialization failed: {}", error);
            return false;
        }
        asset->status = asset->fixture.poll();
#if defined(VOXY_NATIVE)
        gpuContext_->setDeviceLostCallback([lost = asset->deviceLost](WGPUDeviceLostReason, const char*) {
            lost->store(true, std::memory_order_release);
        });
#endif
        salvageLocalSession_->asset = std::move(asset);
        LOG_INFO("Pontoon inspection: authored candidate; 0 auto LOD, 1/2/3 fixed LOD, G guides, R reset");
    }
    if(auto* loadedScene=salvageLocalSession_->asset.get();loadedScene && loadedScene->cargo && loadedScene->content->registry.navigation->delivery) {
        auto& configuredSession=*salvageLocalSession_;
        configuredSession.jobId={bootstrap.world,3};configuredSession.cargoId={bootstrap.world,4};bootstrap.lastIssuedId=4;
        const auto& part=loadedScene->content->registry.placements[loadedScene->cargo->parts()[0].placement];
        const auto& definition=loadedScene->content->bundles[part.bundleIndex]->sidecar().part;
        const auto base=loadedScene->cargo->primaryMassRoot().buildFromRoot.translation;
        const auto position=loadedScene->origin+glm::dvec3(base.x,base.y,base.z)*.02;
        bootstrap.cargoDefinitions.push_back({definition.key,definition.mass.dryMassKg,loadedScene->cargo->displacementCubicMetres(),
            definition.salvageYield,game::expedition::CargoRecoveryRule::PreserveUnique});
        bootstrap.cargo.push_back({configuredSession.cargoId,definition.key,bootstrap.caller.participant,{position.x,position.y,position.z},{},configuredSession.jobId});
        bootstrap.jobs.push_back({configuredSession.jobId});
        configuredSession.preparation.eligible=[this](const auto& delivery) {
            const auto& local=*salvageLocalSession_;const auto& asset=*local.asset;
            if(delivery.cargo.id!=local.cargoId || !asset.cargoBody.valid() || asset.cargoBanked || asset.cargoSecuring
                || !asset.player->onBoat() || !asset.cargoObservedTick) return false;
            const auto& zone=*asset.content->registry.navigation->delivery;
            const auto p=physics::worldPositionToAbsolute(asset.cargoObserved.position)-asset.origin;
            return std::hypot(p.x-zone.center.x,p.z-zone.center.z)+1.1<=zone.radius
                && p.y>=zone.minimumHeight && double(glm::length(asset.cargoObserved.originVelocity))<=zone.maximumSpeed
                && double(glm::length(asset.cargoObserved.angularVelocity))<=zone.maximumAngularSpeed;
        };
        configuredSession.preparation.secure=[this](const auto& delivery,auto tick) {
            using State=game::expedition::PreparationState;
            auto& local=*salvageLocalSession_;auto& asset=*local.asset;const auto frontier=physicsWorld_->tickFrontier();
            if(frontier.scheduled!=frontier.completed || frontier.encoded!=frontier.completed
                || asset.cargoObservedTick!=frontier.completed || asset.boatEventsThrough<frontier.completed) return State::Pending;
            if(tick.value()!=frontier.completed+1 || !local.preparation.eligible(delivery)) return State::Rejected;
            // A harbour hand-off secures the load at its actual observed pose.
            // Admit the replacement before touching the old body. A capacity
            // refusal leaves the physical load and reward unchanged.
            auto motion=asset.cargoObserved;motion.originVelocity={0,0,0};motion.angularVelocity={0,0,0};
            const auto secured=physicsWorld_->spawnAuthoredBody({.shape=asset.cargoShape,.motion=motion,
                .motionType=physics::AuthoredBodyMotionType::Static});
            if(!secured) return State::Rejected;
            if(asset.towRope.valid() && !physicsWorld_->destroyAttachment(asset.towRope)) {
                (void)physicsWorld_->destroyBody(secured.body);return State::Rejected;
            }
            asset.towRope={};asset.towMotor=0;asset.towAction=0;asset.towBroken=false;
            if(!physicsWorld_->destroyBody(asset.cargoBody)) {
                (void)physicsWorld_->destroyBody(secured.body);salvagePreviewFailed_=true;requestExit();return State::Rejected;
            }
            asset.cargoReplacedBody=asset.cargoBody;asset.cargoBody=secured.body;
            asset.cargoSecuring=true;asset.cargoDeliveredPose=motion;return State::Ready;
        };
        configuredSession.preparation.observed=[this](auto tick) {
            const auto& asset=*salvageLocalSession_->asset;
            return asset.cargoSecuring && asset.cargoReplacementDeadTick>=tick.value()
                && asset.cargoObservedTick>=tick.value() && asset.boatEventsThrough>=tick.value()
                && glm::length(physics::worldPositionToAbsolute(asset.cargoObserved.position)
                    -physics::worldPositionToAbsolute(asset.cargoDeliveredPose.position))<.001
                && glm::length(asset.cargoObserved.originVelocity)<.001f && glm::length(asset.cargoObserved.angularVelocity)<.001f;
        };
        configuredSession.preparation.publish=[this] {
            auto& asset=*salvageLocalSession_->asset;asset.cargoBanked=true;asset.cargoSecuring=false;

        };
    }
    if(auto* asset=salvageLocalSession_->asset.get();asset && asset->boat) {
        std::string error;
        auto seed=game::expedition::prepareCoveBuild(*asset->content,bootstrap.caller.participant,bootstrap.lastIssuedId,error);
        if(!seed){LOG_ERROR("Cove build ownership: {}",error);return false;}
        auto physical=game::expedition::CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);
        if(!physical){LOG_ERROR("Canonical cove hull: {}",error);return false;}
        bootstrap.lastIssuedId=seed->issuedThrough;bootstrap.workshopEnabled=true;
        bootstrap.inventory=game::expedition::coveStartingMaterials;
        bootstrap.starterEntitlements.push_back(seed->starterEntitlement);
        salvageLocalSession_->boatId=seed->build.id;
        salvageLocalSession_->starterEntitlement=seed->starterEntitlement;
        auto starter=game::expedition::prepareCoveStarterKit(seed->build,seed->starterEntitlement,error);
        if(!starter){LOG_ERROR("Cove starter recipe: {}",error);return false;}
        asset->initialStarterKit=*starter;bootstrap.starterKits.push_back(*starter);
        asset->initialRecoveryDesign=game::expedition::makeCoveRecoveryDesign(seed->build,seed->catalog);
        if(asset->initialRecoveryDesign.empty())return false;
        bootstrap.builds.push_back(std::move(seed->build));
        salvageLocalSession_->catalog=std::move(seed->catalog);
        // No GPU body exists yet: initial physical preparation now uses the
        // same canonical part/weld identities that GameSession will admit.
        asset->boat=std::move(physical);
        asset->boatRoots=game::expedition::CoveRigidRoots::prepare(*asset->boat,error);
        if(!asset->boatRoots){LOG_ERROR("Canonical cove roots: {}",error);return false;}
        asset->initialBindings=std::move(seed->placements);
        configureCoveLaunch();
    }
    if(auto* asset=salvageLocalSession_->asset.get();asset && asset->boat && asset->cargo) {
        // Content identity comes from installed content and the actual terrain,
        // never from imported bytes. Profile 1 binds current cove/world rules;
        // incompatible geometry, units, physics or catalog changes bump it.
        game::expedition::CoveSaveContext context;
        context.identity.world=bootstrap.world;
        context.identity.content.manifest={{{'v','o','x','y','s','-','c','o','v','e','-','s','a','v','e','1'},1},1};
        core::Sha256 hash;hash.update(asset->content->installedRegistryDigest);
        const auto integer=[&](uint32_t value){
            std::array<std::byte,4> bytes{};
            for(size_t i=0;i<4;++i)bytes[i]=static_cast<std::byte>((value>>(8*i))&255);
            hash.update(bytes);
        };
        integer(1);integer(heightmap_->getWidth());integer(heightmap_->getHeight());
        integer(std::bit_cast<uint32_t>(config_.heightScale));integer(std::bit_cast<uint32_t>(config_.cellScale));
        integer(config_.legoTerrainEnabled||legoMode_?1u:0u);
        // Canonical u16 samples: no host-endian/raw-struct hashing.
        for(const auto sample:heightmap_->getData()) {
            const std::array<std::byte,2> bytes{static_cast<std::byte>(sample&255),static_cast<std::byte>(sample>>8)};
            hash.update(bytes);
        }
        context.identity.content.manifestDigest=hash.finish().bytes;
        context.boat=salvageLocalSession_->boatId;context.cargo=salvageLocalSession_->cargoId;
        context.job=salvageLocalSession_->jobId;context.cargoDefinition=bootstrap.cargoDefinitions.front();
        context.origin={asset->origin.x,asset->origin.y,asset->origin.z};
        salvageLocalSession_->saveContext=std::move(context);
    }
    if(coveResume_) {
        auto& local=*salvageLocalSession_;auto* asset=local.asset.get();
        if(!asset || !local.saveContext)return false;
        const auto loadOrigin=asset->origin;std::string error;
        auto candidate=game::expedition::CoveRestoreCandidate::prepare(coveResume_->source,*local.saveContext,
            *asset->content,*local.catalog,asset->initialBindings,[this,loadOrigin](double x,double z){
                return double(sampleTerrainHeight(float(x+loadOrigin.x),float(z+loadOrigin.z)))-loadOrigin.y;
            },error,&asset->initialStarterKit,coveGroundSupport(loadOrigin),asset->environmentObstacleSpan());
        if(!candidate || candidate->archive->physical.tick.value()!=coveResume_->tick){LOG_ERROR("Cove load: {}",error);return false;}
        game::expedition::RecoveryIssue issue;
        auto recovered=game::expedition::SessionRecovery::restore(candidate->archive->current,observer,local.preparation,issue);
        if(!recovered){LOG_ERROR("Cove session recovery failed: {}",static_cast<int>(issue.error));return false;}
        game::expedition::CoveSaveIssue saveIssue;
        if(!game::expedition::CoveSaveCodec::encode(*recovered->initial,recovered->retired.get(),candidate->archive->physical,
            *local.saveContext,*local.catalog,coveResume_->recovered,saveIssue))return false;
        coveResume_->recoveredDigest=core::sha256Hex(core::sha256(coveResume_->recovered));
        local.caller=recovered->initial->snapshot().accepted.caller;local.session=std::move(recovered->session);
        local.retiredSaveParent=std::move(recovered->retired);
        asset->restorePhysical=candidate->archive->physical;asset->boatEventsThrough=coveResume_->tick;
        asset->recoveryDesigns=asset->restorePhysical->recoveryDesigns;
        asset->selectedRecoveryDesign=asset->recoveryDesigns.empty()?0:asset->recoveryDesigns.size()-1;
        asset->liveScene=std::move(candidate->scene);asset->boat=std::move(candidate->boat);
        asset->boatRoots=std::move(candidate->roots);
        if(!asset->boatRoots||!asset->boatRoots->matches(*asset->boat)){LOG_ERROR("Restored cove roots: {}",error);return false;}
        asset->towRootIndex=asset->boatRoots->indexForKey(candidate->towRoot).value_or(asset->boat->primaryRootIndex());
        asset->cargo=std::move(candidate->cargo);asset->player=std::move(candidate->player);
        asset->cargoObserved=candidate->cargoMotion;
        asset->towDesc=candidate->tow;asset->towBoatPoint=candidate->towBoatPoint;asset->towCargoPoint=candidate->towCargoPoint;
        asset->towReelSpeed=candidate->reelSpeed;asset->cargoBanked=asset->restorePhysical->cargoState==game::expedition::CoveSavedCargoState::Banked;
        asset->towBroken=asset->restorePhysical->cargoState==game::expedition::CoveSavedCargoState::BrokenTow;
        if(asset->cargoBanked)asset->cargoDeliveredPose=candidate->cargoMotion;
        asset->selectedLods.resize(asset->acceptedScene().registry.placements.size());
        const auto& water=asset->restorePhysical->water;asset->waterTime=water.seconds;
        rendererSettings_.waterHeight=water.height;
        rendererSettings_.waterWaveStrength=water.strength;
        auto& settings=rendererSettings_.waterSpectrum;
        settings.significantWaveHeight=water.significantWaveHeight;settings.directionDegrees=glm::degrees(water.directionRadians);
        settings.choppiness=water.choppiness;settings.peakEnhancement=water.peakEnhancement;settings.windAlignment=water.windAlignment;
        settings.animationSpeed=water.animationSpeed;settings.patchLengths={water.patchLengths[0],water.patchLengths[1]};
        settings.cascadeAmplitudes={water.cascadeAmplitudes[0],water.cascadeAmplitudes[1]};settings.directionalSineScale=water.directionalSineScale;
        auto spectrum=makeWaterSpectrumConfig(settings);spectrum.directionRadians=water.directionRadians;
        if(!waterSimulation_ || !waterSimulation_->reconfigure(spectrum))return false;
        const auto eye=physics::worldPositionFromAbsolute(asset->origin+asset->player->feet()+glm::dvec3(0,game::expedition::CovePlayer::eyeHeight,0));
        camera_->setWorldPosition(eye.sector,eye.local);
        camera_->setYaw(asset->restorePhysical->player.viewYaw);camera_->setPitch(asset->restorePhysical->player.viewPitch);
        using CameraRig=game::expedition::CoveCamera;
        const auto& character=asset->restorePhysical->character;
        if(character.profile) {
            if(!asset->characterCamera.settings({character.chaseCamera?CameraRig::Mode::Chase:CameraRig::Mode::Orbit,
                character.reducedMotion,character.loadView})
                ||!asset->characterCamera.restoreOrbit(asset->restorePhysical->player.viewYaw,
                    std::clamp(double(asset->restorePhysical->player.viewPitch),CameraRig::minimumElevation,CameraRig::maximumElevation),
                    character.cameraDistance))return false;
        } else {
            const auto forward=camera_->forward();
            if(!asset->characterCamera.restoreOrbit(std::atan2(-double(forward.x),-double(forward.z)),
                std::clamp(-std::asin(double(forward.y)),CameraRig::minimumElevation,CameraRig::maximumElevation),5.5))return false;
        }
        coveResume_->source.clear();
    } else {
        salvageLocalSession_->session=game::expedition::GameSession::create(bootstrap,observer,
            *salvageLocalSession_->catalog,salvageLocalSession_->preparation,sessionIssue);
        if(!salvageLocalSession_->session) {LOG_ERROR("Cove session admission failed: {}",static_cast<int>(sessionIssue.error));return false;}
        resetSalvagePreviewView();
    }
    if(auto* asset=salvageLocalSession_->asset.get();asset&&asset->boat&&asset->cargo&&asset->player){
        std::string error;
        asset->harbor=game::expedition::CoveHarborRuntime::create(*asset->content->registry.navigation,asset->origin,*asset->boat,
            asset->restorePhysical?asset->restorePhysical->harborLift:game::expedition::CoveHarborLiftState{},error);
        if(!asset->harbor){LOG_ERROR("Harbor preparation failed: {}",error);return false;}
    }
    return true;
}

bool Application::covePracticeCanReturn() const {
    using Asset=SalvageLocalSessionState::AssetPreview;
    const auto* local=salvageLocalSession_.get();const auto* asset=local?local->asset.get():nullptr;
    return initialized_&&!salvagePreviewFailed_&&salvagePreview_&&!salvagePreview_->busy()
        &&local&&asset&&local->session&&physicsWorld_&&asset->player&&asset->boatRoots&&asset->boatRoot().body.valid()
        &&!asset->leaving&&!asset->launch&&!local->pendingControl&&!local->storageRevoked&&local->storageHostReady
        &&local->executionBound&&local->session->admissionOpen()&&!local->session->hasPending()
        &&!local->session->executionInFlight()&&!asset->checkpointPending&&!asset->cargoSecuring
        &&asset->rescue==Asset::Rescue::None&&asset->status==render::SalvageFixtureStatus::Active
        &&asset->practicePhase==Asset::PracticePhase::Running&&asset->practice
        &&(asset->pause==Asset::Pause::Running||asset->pause==Asset::Pause::Paused);
}

bool Application::covePracticeCanBegin() const {
    using Asset=SalvageLocalSessionState::AssetPreview;
    const auto* local=salvageLocalSession_.get();const auto* asset=local?local->asset.get():nullptr;
    if(!initialized_||salvagePreviewFailed_||!salvagePreview_||salvagePreview_->busy()||!local||!asset||!local->session
        ||!physicsWorld_||!asset->player||!asset->boat||!asset->boatRoots||!asset->boatRoot().body.valid()
        ||!local->saveContext||asset->leaving||asset->launch||local->pendingControl||local->storageRevoked||!local->storageHostReady
        ||!local->executionBound||!local->session->admissionOpen()||local->session->hasPending()||local->session->executionInFlight()
        ||asset->checkpointPending||asset->cargoSecuring||asset->rescue!=Asset::Rescue::None
        ||asset->status!=render::SalvageFixtureStatus::Active||asset->practicePhase!=Asset::PracticePhase::None
        ||!asset->workshopOpen||!asset->workshop||asset->player->onBoat()||asset->towRope.valid()||asset->towBroken||asset->towAction
        ||(asset->harbor&&(asset->harbor->hasRopes()||asset->harbor->busy()))
        ||(asset->pause!=Asset::Pause::Running&&asset->pause!=Asset::Pause::Paused)
        ||glm::length(asset->player->feet()-asset->content->registry.navigation->spawn)>3
        ||(asset->workshop->changed()&&!asset->workshop->brickToolActive())
        ||(!asset->workshop->brickToolActive()&&!asset->workshop->valid())
        ||asset->practiceRuns==std::numeric_limits<uint64_t>::max())return false;
    for(size_t i=0;i<asset->boatRoots->roots().size();++i) {
        const auto current=physics::worldPositionToAbsolute(asset->boatRoots->roots()[i].observed.position);
        const auto p=asset->boat->assembly().mass().roots()[i].buildFromRoot.translation;
        const auto home=asset->origin+glm::dvec3(p.x,p.y,p.z)*.02;
        if(std::hypot(current.x-home.x,current.z-home.z)>4)return false;
    }
    return asset->boat->roots().size()!=1||(asset->boatRoot().observed.orientation*glm::vec3(0,1,0)).y>=.7f;
}

bool Application::covePracticeAction(int action) {
    using Asset=SalvageLocalSessionState::AssetPreview;
    using Phase=Asset::PracticePhase;
    if((action==340&&!covePracticeCanBegin())||(action==341&&!covePracticeCanReturn())||(action!=340&&action!=341))return false;
    auto& local=*salvageLocalSession_;auto& asset=*local.asset;
    if(asset.leaving||asset.launch||local.pendingControl||local.storageRevoked||!local.storageHostReady
        ||!local.executionBound||!local.session->admissionOpen()||local.session->hasPending()
        ||local.session->executionInFlight()||asset.checkpointPending||asset.cargoSecuring
        ||asset.rescue!=Asset::Rescue::None||asset.status!=render::SalvageFixtureStatus::Active)return false;
    if(action==340) {
        if(asset.practicePhase!=Phase::None||!asset.workshopOpen||!asset.workshop||!asset.player
            ||asset.player->onBoat()||asset.towRope.valid()||asset.towBroken||asset.towAction
            ||(asset.harbor&&(asset.harbor->hasRopes()||asset.harbor->busy()))
            ||glm::length(asset.player->feet()-asset.content->registry.navigation->spawn)>3)return false;
        if(asset.workshop->changed()&&!asset.workshop->brickToolActive()) {
            asset.practiceMessage="Keep or cancel the current change before testing.";return false;
        }
        std::string error;
        if(asset.workshop->blueprintBytes(error).empty()){asset.practiceMessage=error;return false;}
        asset.practicePhase=Phase::Capturing;
        asset.practiceMessage="Protecting your expedition before testing…";
    } else {
        if(asset.practicePhase!=Phase::Running||!asset.practice)return false;
        asset.practicePhase=Phase::Returning;
        asset.practiceMessage="Discarding the test and restoring your workshop…";
    }
    if(asset.pause==Asset::Pause::Running){asset.pause=Asset::Pause::Requested;asset.pauseTick=0;asset.pauseWaitSeconds=0;}
    asset.player->discardPendingInput();asset.towAction=0;
    if(input_){input_->releaseMouse();input_->resetState();}
    return true;
}

bool Application::updateCovePractice() {
    using namespace game::expedition;
    using Asset=SalvageLocalSessionState::AssetPreview;
    using Phase=Asset::PracticePhase;
    using Purpose=Asset::Launch::Purpose;
    auto& local=*salvageLocalSession_;auto& asset=*local.asset;
    if(asset.practicePhase==Phase::None||asset.leaving)return true;
    // A requested Leave may discard RAM practice, but only after a staged
    // replacement has published. Its normal drain owns all GPU lifetimes.
    if(local.pendingControl && (!asset.launch||!asset.launch->staged||asset.launch->published)) {
        if(asset.launch&&!asset.launch->staged)asset.launch->canceled=true;
        return true;
    }
    const auto refuse=[&](std::string message) {
        asset.practiceMessage=std::move(message);
        if(asset.practicePhase==Phase::Capturing||asset.practicePhase==Phase::Entering) {
            asset.practicePhase=Phase::None;asset.practice.reset();
        } else asset.practicePhase=Phase::Running;
    };
    const auto nextTick=[&]() {
        const auto frontier=physicsWorld_->tickFrontier();
        if(frontier.scheduled!=frontier.completed||!physicsWorld_->setSchedulingPaused(false)
            ||!physicsWorld_->setSchedulingPaused(true,1))return false;
        asset.pause=Asset::Pause::Draining;asset.pauseTick=frontier.scheduled+1;asset.pauseWaitSeconds=0;return true;
    };
    try {
        if(asset.practicePhase==Phase::Capturing && asset.pause==Asset::Pause::Paused) {
            auto bytes=captureCoveCheckpoint(true);std::string error;
            auto proof=CoveTestSession::prepare(bytes,*local.saveContext,*local.catalog,*asset.workshop,error);
            if(!proof){refuse(error);return true;}
            auto practice=std::make_unique<Asset::Practice>();practice->proof=std::move(proof);
            practice->original=std::make_unique<Asset::Launch>();
            practice->camera=asset.characterCamera;practice->character=asset.character;practice->workshopCamera=asset.workshopCamera;
            auto& original=*practice->original;
            original.water=asset.boat->primaryRoot().water({});original.tow=asset.towDesc;
            original.towPoint=asset.towBoatPoint;original.towRootIndex=asset.towRootIndex;original.reelSpeed=asset.towReelSpeed;
            asset.practice=std::move(practice);asset.practicePhase=Phase::Entering;
        }
        if(asset.practicePhase==Phase::Returning && asset.pause==Asset::Pause::Paused && !asset.launch) {
            if(asset.towRope.valid()) {
                if(!physicsWorld_->destroyAttachment(asset.towRope))return false;
                asset.rescueRope=asset.towRope;asset.rescueRopeTick=physicsWorld_->tickFrontier().scheduled+1;
                asset.towRope={};asset.towMotor=0;asset.towAction=0;
                if(!nextTick())return false;
                return true;
            }
            if(asset.rescueRope.valid())return true;
        }
        const bool returning=asset.practicePhase==Phase::Returning;
        if((asset.practicePhase==Phase::Entering||returning)&&asset.pause==Asset::Pause::Paused&&!asset.launch) {
            auto& practice=*asset.practice;std::string error;
            if(!practice.proof->canonicalUnchanged(*local.session,error)){refuse(error);return true;}
            auto candidate=std::make_unique<Asset::Launch>();candidate->purpose=returning?Purpose::TestReturn:Purpose::TestEnter;
            const auto& scene=returning?(practice.original->scene?*practice.original->scene:*asset.content):practice.proof->draft();
            candidate->scene=std::make_unique<const game::assets::LoadedAssetFixture>(scene);
            if(returning) {
                const auto& original=*practice.original->boat;
                candidate->boat=original.roots().size()==1
                    ?CoveBoatAssembly::compileBuild(original.build(),*local.catalog,original.parts(),error)
                    :CoveBoatAssembly::compileFragments(original.build(),*local.catalog,original.parts(),*original.primaryRoot().helm,error);
            } else candidate->boat=practice.proof->takeDraftBoat();
            if(!candidate->boat){refuse(error.empty()?"The test boat could not be prepared.":error);return true;}
            candidate->roots=CoveRigidRoots::prepare(*candidate->boat,error);
            if(!candidate->roots){refuse(error);return true;}
            candidate->boatSlots=asset.content->registry.navigation->boatPlacements;
            for(size_t i=asset.content->registry.placements.size();i<scene.registry.placements.size();++i)
                candidate->boatSlots.push_back(static_cast<uint32_t>(i));
            candidate->player=std::make_unique<CovePlayer>();const auto origin=asset.origin;
            if(!candidate->player->initialize(scene,[this,origin](double x,double z){
                return double(sampleTerrainHeight(float(x+origin.x),float(z+origin.z)))-origin.y;
            },error,candidate->boatSlots,coveGroundSupport(origin))
                ||!candidate->player->setEnvironmentObstacles(asset.environmentObstacleSpan())
                ||(asset.harbor&&asset.harbor->installed()&&!asset.harbor->structure().applyPlayerCollision(*candidate->player))) {
                refuse(error.empty()?"The test player could not be prepared.":error);return true;
            }
            candidate->workshop=CoveWorkshop::create(scene,error,static_cast<uint32_t>(asset.content->registry.placements.size()),
                asset.content->registry.navigation->boatPlacements,returning&&candidate->boat->roots().size()!=1);
            if(!candidate->workshop){refuse(error);return true;}
            candidate->lods.resize(scene.registry.placements.size());candidate->recoveryDesigns=asset.recoveryDesigns;
            candidate->selectedRecoveryDesign=asset.selectedRecoveryDesign;
            if(returning) {
                const auto& original=*practice.original;
                candidate->water=original.water;candidate->tow=original.tow;candidate->towPoint=original.towPoint;
                candidate->towRootIndex=original.towRootIndex;candidate->reelSpeed=original.reelSpeed;
            } else {
                candidate->water=candidate->boat->primaryRoot().water({});unsigned winches=0;
                const auto& functions=candidate->boat->assembly().functions();
                for(size_t i=0;i<functions.modules().size();++i) {
                    const auto& module=functions.modules()[i];
                    if(const auto* winch=std::get_if<game::construction::WinchModule>(&module.parameters)) {
                        const auto root=candidate->boat->rootForPart(module.part);if(!root)return false;
                        candidate->towRootIndex=*root;
                        for(const auto& frame:functions.moduleFrames(i))if(frame.kind==game::construction::AssemblyFrameKind::TowLine) {
                            ++winches;const auto p=frame.rootFromFrame.translation;
                            candidate->towPoint=glm::vec3(p.x,p.y,p.z)*.02f;
                            candidate->reelSpeed=static_cast<float>(winch->reelSpeedMetresPerSecond)*coveModuleOutput(module);
                            candidate->tow.minimumLength=static_cast<float>(winch->minimumLengthMetres);
                            candidate->tow.maximumLength=static_cast<float>(winch->maximumLengthMetres);
                            candidate->tow.maximumForce=static_cast<float>(winch->maximumForceNewtons);
                            candidate->tow.breakForce=static_cast<float>(functions.sockets()[frame.socket].definition.strength.tensionNewtons);
                        }
                    }
                }
                if(winches>1){refuse("Testing supports at most one winch.");return true;}
                if(winches) {
                    const auto& cargo=asset.cargo->assembly().functions();unsigned eyes=0;
                    for(const auto& frame:cargo.frames())if(frame.kind==game::construction::AssemblyFrameKind::TowEye) {
                        ++eyes;candidate->tow.breakForce=std::min(candidate->tow.breakForce,
                            static_cast<float>(cargo.sockets()[frame.socket].definition.strength.tensionNewtons));
                    }
                    physics::AuthoredFrameError issue;
                    const auto a=physics::AuthoredBodyFrame(candidate->boat->roots()[candidate->towRootIndex].shape).bodyPoint(candidate->towPoint,issue);
                    const auto b=physics::AuthoredBodyFrame(asset.cargo->shape()).bodyPoint(asset.towCargoPoint,issue);
                    if(eyes!=1||!a||!b){refuse("The test cable anchors are invalid.");return true;}
                    candidate->tow.localAnchorA=*a;candidate->tow.localAnchorB=*b;
                }
            }
            candidate->preparedShapes.reserve(candidate->boat->roots().size());
            for(const auto& root:candidate->boat->roots())candidate->preparedShapes.push_back(root.shape);
            asset.launch=std::move(candidate);
        }
        if(!asset.launch||asset.launch->purpose==Purpose::Expedition)return true;
        auto& launch=*asset.launch;
        if(launch.canceled) {refuse("The test replacement was refused. The active boat is unchanged.");return true;}
        if(!launch.staged) {
            const auto ready=local.preparation.pollBuild();
            if(ready==PreparationState::Pending)return true;
            if(ready!=PreparationState::Ready){launch.canceled=true;refuse("The test could not be prepared. The active boat is unchanged.");return true;}
            const auto frontier=physicsWorld_->tickFrontier();
            const auto staged=local.preparation.stageBuild(game::construction::SimulationTick{frontier.completed+1});
            if(staged==PreparationState::Pending)return true;
            if(staged!=PreparationState::Ready){launch.canceled=true;refuse(asset.launchMessage);return true;}
            if(!asset.practice->proof->stage(frontier.incarnation,launch.executionTick,returning))return false;
            if(returning) {
                const auto seconds=asset.practice->proof->physical().water.seconds;
                if(!asset.waterClock.reset(frontier.scheduled,seconds))return false;
                asset.waterTime=seconds;
            }
            if(!nextTick())return false;
        }
        if(!launch.published&&local.preparation.observedBuild(game::construction::SimulationTick{launch.executionTick})) {
            std::string error;const auto frontier=physicsWorld_->tickFrontier();
            if(asset.cargoObservedTick<launch.executionTick
                ||(asset.harbor&&!asset.harbor->joined(launch.executionTick)))return true;
            if(!asset.practice->proof->confirm(frontier.incarnation,frontier.completed,*local.session,error))return false;
            local.preparation.publishBuild();auto& original=*asset.practice->original;
            if(launch.purpose==Purpose::TestEnter) {
                original.boat=std::move(launch.boat);original.player=std::move(launch.player);
                original.workshop=std::move(launch.workshop);original.scene=std::move(launch.scene);original.lods=std::move(launch.lods);
                asset.practicePhase=Phase::Running;++asset.practiceRuns;asset.workshopOpen=false;
                asset.practiceMessage="Test mode — no cost or rewards. Return restores your workshop. Quitting discards this test.";
                if(!physicsWorld_->setSchedulingPaused(false))return false;
                asset.pause=Asset::Pause::Running;asset.pauseTick=0;
            } else {
                asset.boat.swap(original.boat);asset.player.swap(original.player);asset.workshop.swap(original.workshop);
                asset.liveScene.swap(original.scene);asset.selectedLods.swap(original.lods);
                asset.characterCamera=asset.practice->camera;asset.character=asset.practice->character;asset.workshopCamera=asset.practice->workshopCamera;
                if(!asset.boatRoots->bindPlayer(*asset.player,*asset.boat,asset.origin,error))return false;
                asset.practicePhase=Phase::None;++asset.practiceReturns;asset.workshopOpen=true;
                asset.practiceMessage="Test discarded. Your original boat, inventory and workshop are restored. Resume to edit.";
                asset.practice.reset();asset.pause=Asset::Pause::Paused;asset.pauseTick=frontier.completed;
            }
            asset.playerObservedTick=frontier.completed;asset.terrainBoundPlayer=nullptr;
            asset.characterDiscontinuity=true;asset.characterViewReady=false;asset.characterPresentation.valid=false;
            asset.characterCameraInput={};asset.player->discardPendingInput();
            asset.towMotor=0;asset.towAction=0;asset.towBroken=false;asset.towObserved={};asset.towObservedTick=0;
            asset.rotorCommandBody={};asset.rotorCommandDrive=0;
            if(!asset.mechanisms.reset(frontier.incarnation,frontier.completed)
                ||!asset.effects.reset(frontier.incarnation,frontier.completed))return false;
            asset.effectsDrives={};asset.effectsImpactCount=0;asset.effectsImpactTick=0;asset.surfaceStates={};asset.surfaceTick=0;
            if(input_){input_->releaseMouse();input_->resetState();}
        }
        return true;
    } catch(const std::bad_alloc&) {
        // All allocations precede staging; post-stage failures use the normal
        // fatal drain rather than abandoning a committed physical replacement.
        if(asset.launch&&asset.launch->staged)return false;
        if(asset.launch)asset.launch->canceled=true;
        refuse("Not enough memory to prepare the test. The active boat is unchanged.");return true;
    }
}

void Application::configureCoveLaunch() {
    using State=game::expedition::PreparationState;
    using Frame=game::construction::AssemblyFrameKind;
    auto& local=*salvageLocalSession_;
    auto* asset=local.asset.get();
    asset->launchMessage.reserve(512);
    local.preparation.prepareBuild=[this,asset](const game::expedition::PreparationRequest& request) {
        const auto& build=*request.changedBuild;
        if(asset->launch || build.id!=salvageLocalSession_->boatId || asset->leaving
            ||(asset->harbor&&(asset->harbor->hasRopes()||asset->harbor->busy())))return State::Rejected;
        std::string error;
        auto bindings=game::expedition::bindCoveStarterKit(asset->initialStarterKit,request.starterKit,asset->initialBindings,error);
        if(!bindings){asset->launchMessage=error;return State::Rejected;}
        auto design=game::expedition::prepareCoveExpandedLaunchDesign(*asset->content,asset->acceptedScene(),build,
            *bindings,asset->boat->parts(),error,request.cut?game::expedition::CoveSceneTopology::AcceptedRoots
                :game::expedition::CoveSceneTopology::Welded);
        if(!design){asset->launchMessage=error;return State::Rejected;}
        auto candidate=std::make_unique<SalvageLocalSessionState::AssetPreview::Launch>();
        candidate->ownershipService=request.storage!=nullptr;
        candidate->recoveryDesigns=asset->recoveryDesigns;candidate->selectedRecoveryDesign=asset->selectedRecoveryDesign;
        if(request.cut || (request.storage&&request.storage->starterBefore)) {
            if(!game::expedition::protectCoveRecoveryDesign(asset->boat->build(),*salvageLocalSession_->catalog,
                asset->initialRecoveryDesign,candidate->recoveryDesigns,candidate->selectedRecoveryDesign,error)) {
                asset->launchMessage=error;return State::Rejected;
            }
        }
        if(request.cut) {
            if(!asset->cutTarget(request.cut->connection)){asset->launchMessage="Stand near that weld and release all cables before cutting.";return State::Rejected;}
            const std::array cuts{request.cut->connection};game::construction::AssemblyFractureIssue issue;
            candidate->fracture=game::construction::AssemblyFracturePlan::prepare(asset->boat->build(),
                asset->boat->build().revision,cuts,*salvageLocalSession_->catalog,issue);
            if(!candidate->fracture){asset->launchMessage="The complete split cannot be prepared.";return State::Rejected;}
            candidate->boat=game::expedition::CoveBoatAssembly::compileFragments(build,*salvageLocalSession_->catalog,
                design->placements,*asset->boat->primaryRoot().helm,error);
        } else candidate->boat=game::expedition::CoveBoatAssembly::compileBuild(build,*salvageLocalSession_->catalog,design->placements,error);
        if(!candidate->boat){asset->launchMessage=error;return State::Rejected;}
        candidate->roots=game::expedition::CoveRigidRoots::prepare(*candidate->boat,error);
        if(!candidate->roots){asset->launchMessage=error;return State::Rejected;}
        candidate->player=std::make_unique<game::expedition::CovePlayer>();
        const auto origin=asset->origin;
        candidate->boatSlots=asset->content->registry.navigation->boatPlacements;
        for(size_t i=asset->content->registry.placements.size();i<design->scene.registry.placements.size();++i)
            candidate->boatSlots.push_back(static_cast<uint32_t>(i));
        candidate->lods=asset->selectedLods;candidate->lods.resize(design->scene.registry.placements.size());
        if(!candidate->player->initialize(design->scene,[this,origin](double x,double z) {
                return double(sampleTerrainHeight(float(x+origin.x),float(z+origin.z)))-origin.y;
            },error,candidate->boatSlots,coveGroundSupport(origin))) {
            asset->launchMessage=error;return State::Rejected;
        }
        if(asset->harbor&&asset->harbor->installed()
            &&!asset->harbor->structure().applyPlayerCollision(*candidate->player))return State::Rejected;
        if(!candidate->player->setEnvironmentObstacles(asset->environmentObstacleSpan()))return State::Rejected;
        candidate->workshop=game::expedition::CoveWorkshop::create(design->scene,error,static_cast<uint32_t>(asset->content->registry.placements.size()),asset->content->registry.navigation->boatPlacements,request.cut!=nullptr);
        if(!candidate->workshop){asset->launchMessage=error;return State::Rejected;}
        candidate->scene=std::make_unique<const game::assets::LoadedAssetFixture>(std::move(design->scene));
        candidate->water=candidate->boat->primaryRoot().water({});
        auto& tow=candidate->tow;
        unsigned winches=0;
        const auto& functions=candidate->boat->assembly().functions();
        for(size_t i=0;i<functions.modules().size();++i) {
            const auto& module=functions.modules()[i];
            const auto output=game::expedition::coveModuleOutput(module);
            const auto& parameters=module.parameters;
            if(const auto* winch=std::get_if<game::construction::WinchModule>(&parameters)) {
                const auto root=candidate->boat->rootForPart(module.part);if(!root)return State::Rejected;
                candidate->towRootIndex=*root;
                for(const auto& frame:functions.moduleFrames(i))if(frame.kind==Frame::TowLine) {
                    ++winches;const auto p=frame.rootFromFrame.translation;
                    candidate->towPoint=glm::vec3(p.x,p.y,p.z)*.02f;
                    candidate->reelSpeed=static_cast<float>(winch->reelSpeedMetresPerSecond)*output;
                    tow.minimumLength=static_cast<float>(winch->minimumLengthMetres);
                    tow.maximumLength=static_cast<float>(winch->maximumLengthMetres);
                    tow.maximumForce=static_cast<float>(winch->maximumForceNewtons);
                    tow.breakForce=static_cast<float>(functions.sockets()[frame.socket].definition.strength.tensionNewtons);
                }
            }
        }
        if((!request.cut&&!candidate->boat->primaryRoot().propeller) || !candidate->boat->primaryRoot().helm || winches>1) {
            asset->launchMessage="This starter needs one helm, one propeller and at most one winch.";return State::Rejected;
        }
        if(winches && asset->cargo) {
            const auto& cargoFunctions=asset->cargo->assembly().functions();
            unsigned eyes=0;
            for(const auto& frame:cargoFunctions.frames())if(frame.kind==Frame::TowEye) {
                ++eyes;tow.breakForce=std::min(tow.breakForce,
                    static_cast<float>(cargoFunctions.sockets()[frame.socket].definition.strength.tensionNewtons));
            }
            physics::AuthoredFrameError frameError;
            const auto a=physics::AuthoredBodyFrame(candidate->boat->roots()[candidate->towRootIndex].shape).bodyPoint(candidate->towPoint,frameError);
            const auto b=physics::AuthoredBodyFrame(asset->cargo->shape()).bodyPoint(asset->towCargoPoint,frameError);
            if(eyes!=1 || !a || !b)return State::Rejected;
            tow.localAnchorA=*a;tow.localAnchorB=*b;
        }
        // Copy every upload payload before taking any GPU ownership. A
        // rejected partial upload remains owned by the retained candidate.
        try {
            candidate->preparedShapes.reserve(candidate->boat->roots().size());
            for(const auto& root:candidate->boat->roots())candidate->preparedShapes.push_back(root.shape);
        } catch(const std::bad_alloc&) {
            asset->launchMessage="Not enough memory to prepare every boat section.";return State::Rejected;
        }
        asset->launch=std::move(candidate);
        // Upload during pollBuild, after the resource owner has consumed GPU
        // completions. Temporary queue pressure keeps this compiled candidate.
        return State::Pending;
    };
    local.preparation.pollBuild=[this,asset] {
        if(!asset->launch || asset->launch->canceled)return State::Rejected;
        auto& launch=*asset->launch;
        auto* resources=physicsWorld_->authoredShapeResources();
        if(!resources){launch.canceled=true;return State::Rejected;}
        const auto result=launch.roots->prepareShapes(*resources,launch.preparedShapes);
        if(std::all_of(launch.roots->roots().begin(),launch.roots->roots().end(),
            [](const auto& root){return root.shape.valid();}))launch.preparedShapes.clear();
        if(result==physics::ShapeResourceError::Busy || result==physics::ShapeResourceError::NotReady)
            return State::Pending;
        if(result!=physics::ShapeResourceError::None) {
            launch.canceled=true;asset->launchMessage="Boat preparation failed. Your build is unchanged.";
            return State::Rejected;
        }
        return State::Ready;
    };
    local.preparation.stageBuild=[this,asset](auto tick) {
        if(!asset->launch || asset->launch->staged || asset->launch->canceled)return State::Rejected;
        auto& launch=*asset->launch;const auto frontier=physicsWorld_->tickFrontier();
        if(frontier.scheduled!=frontier.completed || frontier.encoded!=frontier.completed
            || frontier.pendingBatches!=0
            || asset->boatRoots->joinedTick()!=frontier.completed || asset->boatEventsThrough<frontier.completed)return State::Pending;
        if(tick.value()!=frontier.completed+1 || asset->leaving || !asset->boatRoot().body.valid()
            || asset->cargoSecuring || asset->towRope.valid()
            ||(asset->harbor&&(asset->harbor->hasRopes()||asset->harbor->busy())))return State::Rejected;
        std::string playerIssue;
        if(launch.purpose==SalvageLocalSessionState::AssetPreview::Launch::Purpose::TestReturn) {
            if(!asset->practice || !asset->practice->proof)return State::Rejected;
            const auto& saved=asset->practice->proof->physical();
            if(saved.boatRoots.size()!=launch.roots->roots().size())return State::Rejected;
            for(size_t i=0;i<saved.boatRoots.size();++i) {
                auto& child=launch.roots->roots()[i];const auto& source=saved.boatRoots[i];
                if(child.key!=source.key)return State::Rejected;
                const auto& p=source.motion.position;const auto& q=source.motion.orientation;
                child.spawn.position=physics::worldPositionFromAbsolute({p.x,p.y,p.z});
                child.spawn.orientation={q.w,q.x,q.y,q.z};
                child.spawn.originVelocity=glm::vec3(source.motion.originVelocity[0],source.motion.originVelocity[1],source.motion.originVelocity[2]);
                child.spawn.angularVelocity=glm::vec3(source.motion.angularVelocity[0],source.motion.angularVelocity[1],source.motion.angularVelocity[2]);
                child.observed=child.spawn;
            }
            if(!launch.roots->bindPlayer(*launch.player,*launch.boat,asset->origin,playerIssue))return State::Rejected;
        } else if(launch.fracture) {
            if(!asset->cutTarget(launch.fracture->cuts().front())) {
                asset->launchMessage="That weld moved out of reach. The build is unchanged.";return State::Rejected;
            }
            if(!launch.roots->inheritFractureMotion(*launch.boat,*launch.fracture,*asset->boat,
                *asset->boatRoots,frontier.completed,playerIssue)) {
                asset->launchMessage=playerIssue;return State::Rejected;
            }
            for(auto& child:launch.roots->roots())child.observed=child.spawn;
            if(!launch.roots->transferCutPlayer(*launch.player,*launch.boat,*asset->player,asset->origin,playerIssue)) {
                asset->launchMessage=playerIssue;return State::Rejected;
            }
        } else {
            if(!asset->workshopOpen || asset->player->onBoat()
                ||glm::length(asset->player->feet()-asset->content->registry.navigation->spawn)>3) {
                asset->launchMessage="Return to the dock workshop before launching.";return State::Rejected;
            }
            bool allHome=true;
            for(size_t i=0;i<asset->boatRoots->roots().size();++i) {
                const auto current=physics::worldPositionToAbsolute(asset->boatRoots->roots()[i].observed.position);
                const auto p=asset->boat->assembly().mass().roots()[i].buildFromRoot.translation;
                const auto home=asset->origin+glm::dvec3(p.x,p.y,p.z)*.02;
                allHome=allHome&&std::hypot(current.x-home.x,current.z-home.z)<=4;
            }
            const auto up=asset->boatRoot().observed.orientation*glm::vec3(0,1,0);
            // The shore workshop relaunches an unoccupied craft at this berth.
            // Wave-driven velocity/rocking is not a meaningful dock eligibility
            // test. No rider or tow line may be carried through the replacement.
            if(!allHome || (asset->boat->roots().size()==1 && up.y<.7f)) {
                asset->launchMessage="Bring every section home. Rescue can recover the whole build.";return State::Rejected;
            }
            const auto root=launch.boat->primaryMassRoot().buildFromRoot.translation;
            auto position=asset->origin+glm::dvec3(root.x,root.y,root.z)*.02;
            const auto surface=waterSimulation_->samplePlacementHeight(glm::vec2(position.x,position.z),
                waterPhaseSeconds(),rendererSettings_.waterWaveStrength);
            if(!surface){asset->launchMessage="The berth water surface is unavailable. Try Launch again.";return State::Rejected;}
            position.y=asset->origin.y+launch.boat->equilibriumRootHeight()+static_cast<double>(*surface);
            if(launch.roots->roots().size()!=1)return State::Rejected;
            launch.roots->primary().spawn.position=physics::worldPositionFromAbsolute(position);
            for(auto& child:launch.roots->roots())child.observed=child.spawn;
            if(!launch.roots->bindPlayer(*launch.player,*launch.boat,asset->origin,playerIssue)) {
                asset->launchMessage=playerIssue;return State::Rejected;
            }
        }
        const auto cancelChildren=[&] {
            bool canceled=true;
            for(auto& child:launch.roots->roots())if(child.body.valid()) {
                if(physicsWorld_->destroyBody(child.body)){child.body={};child.admissionTick=0;}
                else canceled=false;
            }
            if(!canceled){salvagePreviewFailed_=true;requestExit();}
        };
        bool configured=true;
        try {
            for(size_t i=0;i<launch.roots->roots().size();++i) {
                auto& child=launch.roots->roots()[i];
                const auto spawned=physicsWorld_->spawnAuthoredBody({.shape=child.shape,.motion=child.spawn});
                if(!spawned){configured=false;break;}
                child.body=spawned.body;child.admissionTick=tick.value();
                const auto& physical=launch.boat->roots()[i];
                if(!physical.cells.empty() && physicsWorld_->configureAuthoredWaterBody(physical.water(child.body))!=physics::AuthoredBodyError::None) {
                    configured=false;break;
                }
            }
        } catch(const std::bad_alloc&) {configured=false;}
        if(!configured){cancelChildren();asset->launchMessage="Boat setup was refused. Your original boat is unchanged.";return State::Rejected;}
        std::array<physics::BodyHandle,game::expedition::CoveRigidRoots::maximumRoots> parents{};
        const auto oldRoots=asset->boatRoots->roots();
        for(size_t i=0;i<oldRoots.size();++i)parents[i]=oldRoots[i].body;
        std::array<physics::PhysicsCommand,3> cargoCommands{};size_t cargoCommandCount=0;
        if(launch.purpose==SalvageLocalSessionState::AssetPreview::Launch::Purpose::TestReturn && !asset->cargoBanked) {
            const auto& saved=asset->practice->proof->physical().cargoMotion;
            physics::AuthoredRootMotion motion;
            motion.position=physics::worldPositionFromAbsolute({saved.position.x,saved.position.y,saved.position.z});
            motion.orientation={saved.orientation.w,saved.orientation.x,saved.orientation.y,saved.orientation.z};
            motion.originVelocity={saved.originVelocity[0],saved.originVelocity[1],saved.originVelocity[2]};
            motion.angularVelocity={saved.angularVelocity[0],saved.angularVelocity[1],saved.angularVelocity[2]};
            physics::AuthoredFrameError issue;
            const auto body=physics::AuthoredBodyFrame(asset->cargo->shape()).bodyMotion(motion,issue);
            if(!body){cancelChildren();return State::Rejected;}
            auto& teleport=cargoCommands[0];teleport.type=physics::PhysicsCommandType::Teleport;teleport.body=asset->cargoBody;
            teleport.sector=body->centerPosition.sector;teleport.a=glm::vec4(body->centerPosition.local,0);
            teleport.b={body->orientation.x,body->orientation.y,body->orientation.z,body->orientation.w};
            auto& velocity=cargoCommands[1];velocity.type=physics::PhysicsCommandType::SetVelocity;velocity.body=asset->cargoBody;
            velocity.a=glm::vec4(body->centerVelocity,0);
            auto& angular=cargoCommands[2];angular.type=physics::PhysicsCommandType::SetAngularVelocity;angular.body=asset->cargoBody;
            angular.a=glm::vec4(body->angularVelocity,0);cargoCommandCount=3;
        }
        const auto retirement=physicsWorld_->prepareMutationBatch({
            .bodyCommands=std::span<const physics::PhysicsCommand>{cargoCommands.data(),cargoCommandCount},
            .bodyDestroys=std::span<const physics::BodyHandle>{parents.data(),oldRoots.size()},
            .joinedBoundary=physics::PhysicsMutationJoin{frontier.incarnation,frontier.completed}});
        if(!retirement){cancelChildren();asset->launchMessage="Boat setup was refused. Your original boat is unchanged.";return State::Rejected;}
        if(retirement.targetTick!=tick.value()) {
            if(!physicsWorld_->discardPrepared(retirement)){salvagePreviewFailed_=true;requestExit();}
            else cancelChildren();
            return State::Rejected;
        }
        if(!physicsWorld_->commitPrepared(retirement)) {
            salvagePreviewFailed_=true;requestExit();return State::Rejected;
        }
        launch.parentCount=oldRoots.size();
        for(size_t i=0;i<oldRoots.size();++i)launch.parents[i]={oldRoots[i].shape,oldRoots[i].body,0};
        launch.executionTick=tick.value();launch.staged=true;return State::Ready;
    };
    local.preparation.observedBuild=[asset](auto tick) {
        if(!asset->launch || !asset->launch->staged || asset->launch->published)return false;
        const auto& launch=*asset->launch;
        return launch.executionTick==tick.value() && launch.roots->joinedTick()>=tick.value()
            && asset->boatEventsThrough>=tick.value()
            && std::all_of(launch.parents.begin(),launch.parents.begin()+launch.parentCount,
                [&](const auto& parent){return parent.deadTick>=tick.value();});
    };
    local.preparation.publishBuild=[asset] {
        auto& launch=*asset->launch;
        // No uploads, physics commands, allocations, or renderer publication
        // occur here. All fallible work and GPU execution already succeeded.
        asset->boat.swap(launch.boat);asset->player.swap(launch.player);
        asset->boatRoots.swap(launch.roots);
        asset->workshop.swap(launch.workshop);asset->liveScene.swap(launch.scene);asset->selectedLods.swap(launch.lods);
        asset->recoveryDesigns.swap(launch.recoveryDesigns);asset->selectedRecoveryDesign=launch.selectedRecoveryDesign;
        const auto anchor=asset->boat->primaryMassRoot().buildFromRoot.translation;
        const auto current=physics::worldPositionToAbsolute(asset->boatRoot().observed.position)-asset->origin;
        asset->player->setBoatTransform(glm::translate(glm::dmat4(1),current)
            *glm::mat4_cast(glm::dquat(asset->boatRoot().observed.orientation))
            *glm::translate(glm::dmat4(1),-glm::dvec3(anchor.x,anchor.y,anchor.z)*.02));
        asset->towRootIndex=launch.towRootIndex;
        asset->towDesc=launch.tow;asset->towDesc.bodyA=asset->towRoot().body;asset->towDesc.bodyB=asset->cargoBody;
        asset->towBoatPoint=launch.towPoint;asset->towReelSpeed=launch.reelSpeed;
        asset->thrustLimit=launch.water.maximumThrustNewtons;asset->steeringLimit=launch.water.maximumSteeringRadians;
        asset->thrustDirection=launch.water.propellerDirection;
        asset->towAction=0;asset->towMotor=0;asset->towBroken=false;
        if(launch.purpose==SalvageLocalSessionState::AssetPreview::Launch::Purpose::Expedition) {
            asset->lastLaunchTick=launch.executionTick;++asset->launchCount;
        }
        launch.published=true;
    };
    // Capture the stable asset, not Application's owning unique_ptr: session
    // closure can run while that unique_ptr itself is being reset.
    local.preparation.discardBuild=[asset] {
        if(asset->launch && !asset->launch->staged)asset->launch->canceled=true;
        // A staged launch is only discarded during fatal world teardown. Normal
        // Leave defers closure until the executed launch has been confirmed.
    };
}

void Application::resetSalvagePreviewView() {
    if (!salvagePreview_ || !camera_) return;
    if (input_) input_->resetState();
    if (salvageLocalSession_ && salvageLocalSession_->asset) {
        auto& asset = *salvageLocalSession_->asset;
        if (asset.player) {
            // This resets only the view/player. Physical recovery is a reserved
            // all-section mutation in the joined Rescue state machine below.
            asset.player->reset();
            asset.character.reset();asset.playerObservedTick=0;asset.characterDiscontinuity=true;
            setCameraWorldPose(*camera_, glm::vec3(asset.origin + asset.player->feet()
                + glm::dvec3(0, game::expedition::CovePlayer::eyeHeight, 0)),
                glm::vec3(asset.origin + asset.content->registry.navigation->lookTarget));
            const auto forward=camera_->forward();
            asset.characterCamera.reset();
            // New/reset Cove view looks across the berth toward the workshop.
            // Keep the boat, brick shore and walkable entrance in one readable
            // view. Loaded expeditions restore their own saved orbit above.
            if(asset.environment)
                (void)asset.characterCamera.restoreOrbit(-1.4,.60,12.0);
            else
                (void)asset.characterCamera.restoreOrbit(std::atan2(-double(forward.x),-double(forward.z)),.32,5.5);
            return;
        }
        setCameraWorldPose(*camera_, glm::vec3(asset.origin + asset.content->registry.cameraEye),
                          glm::vec3(asset.origin + asset.content->registry.cameraTarget));
        setControllerMode(ControllerMode::FreeFly);
        return;
    }
    const auto origin = salvagePreview_->origin();
    setCameraWorldPose(*camera_, origin + glm::vec3(0, .14f + config_.cameraEyeHeight, 8),
                      origin + glm::vec3(-7, 1.5f, -5));
    setControllerMode(ControllerMode::Character);
    // setControllerMode does not synchronize an already active character.
    if (characterController_) characterController_->syncPhysicsPosition();
}

bool Application::coveUiOwnsInput() const {
    if(landingMenuConsumed_)return true;
    if(!salvageLocalSession_||!salvageLocalSession_->asset)return false;
    const auto& asset=*salvageLocalSession_->asset;
#if defined(VOXY_WASM)
    if(EM_ASM_INT({return typeof window['voxyControllerMenuActive']==='function'&&window['voxyControllerMenuActive']();}))return true;
#endif
    return asset.controllerMenuOwner||asset.menuConsumedFrame;
}

std::string Application::salvageBlueprintAction(int action,std::string_view text) {
    if(!initialized_ || !salvageLocalSession_ || !salvageLocalSession_->asset || !salvageLocalSession_->session
        || salvagePreviewFailed_ || !salvagePreview_ || salvagePreview_->busy() || salvageLocalSession_->pendingControl)return {};
    auto& asset=*salvageLocalSession_->asset;
    if(asset.leaving || asset.practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None
        || !asset.workshopOpen || !asset.workshop || asset.launch || salvageLocalSession_->session->hasPending()
        || asset.status!=render::SalvageFixtureStatus::Active)return {};
    constexpr std::string_view digits="0123456789abcdef";
    if(action==1) {
        std::string error;const auto bytes=asset.workshop->blueprintBytes(error);
        if(bytes.empty()){asset.launchMessage=error;return {};}
        std::string result;result.reserve(bytes.size()*2);
        for(auto byte:bytes){const auto value=std::to_integer<uint8_t>(byte);result+=digits[value>>4];result+=digits[value&15];}
        asset.launchMessage.clear();return result;
    }
    if((action!=2 && action!=3) || text.empty() || text.size()>2*game::construction::kMaximumBlueprintBytes || text.size()%2)return {};
    std::vector<std::byte> bytes;bytes.reserve(text.size()/2);
    for(size_t i=0;i<text.size();i+=2) {
        const auto a=digits.find(text[i]),b=digits.find(text[i+1]);if(a==digits.npos || b==digits.npos)return {};
        bytes.push_back(static_cast<std::byte>(a*16+b));
    }
    if(action==3) {
        std::optional<game::construction::BuildBlueprint> checked;
        return game::construction::decodeBlueprint(bytes,*salvageLocalSession_->catalog,checked)?"":"ok";
    }
    std::string error;
    if(!asset.workshop->loadBlueprint(bytes,error)){asset.launchMessage=error;return {};}
    asset.launchMessage.clear();return "ok";
}

bool Application::salvageCheckpointNeedsSave() const noexcept {
    const auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
    return asset && asset->practicePhase==SalvageLocalSessionState::AssetPreview::PracticePhase::None && asset->checkpointPending
        && asset->pause==SalvageLocalSessionState::AssetPreview::Pause::Paused;
}

std::vector<std::byte> Application::captureCoveCheckpoint(bool allowWorkshop) {
    using namespace game::expedition;
    using namespace game::construction;
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    if(!salvageLocalSession_ || !salvageLocalSession_->asset || !salvageLocalSession_->session
        || !salvageLocalSession_->saveContext || !physicsWorld_ || !waterSimulation_)return {};
    auto& local=*salvageLocalSession_;auto& asset=*local.asset;
    if(asset.pause!=Pause::Paused || (!allowWorkshop&&asset.workshopOpen) || asset.launch
        || asset.leaving || asset.cargoSecuring || local.storageRevoked || local.session->hasPending()
        || local.session->executionInFlight() || !asset.player || !asset.boat || !asset.cargo
        || asset.practice)return {};
    try {
    const auto frontier=physicsWorld_->tickFrontier();
    if(frontier.scheduled!=asset.pauseTick || frontier.encoded!=asset.pauseTick
        || frontier.submitted!=asset.pauseTick || frontier.completed!=asset.pauseTick
        || asset.boatEventsThrough!=asset.pauseTick || asset.boatRoots->joinedTick()!=asset.pauseTick
        || asset.cargoObservedTick!=asset.pauseTick || (asset.towRope.valid() && asset.towObservedTick!=asset.pauseTick))return {};
    CovePhysicalSave physical;physical.recoveryDesigns=asset.recoveryDesigns;const auto& context=*local.saveContext;
    physical.tick=SimulationTick{asset.pauseTick};physical.origin=context.origin;
    physical.boat=context.boat;physical.cargo=context.cargo;physical.job=context.job;
    physical.cargoDefinition=context.cargoDefinition.key;
    const auto motion=[](const physics::AuthoredRootMotion& source,CoveSavedMotion& dest){
        const auto p=physics::worldPositionToAbsolute(source.position);
        const auto q=canonicalQuaternion(source.orientation.x,source.orientation.y,source.orientation.z,source.orientation.w);
        if(!q)return false;
        dest.position={p.x,p.y,p.z};dest.orientation=*q;
        dest.originVelocity={source.originVelocity.x,source.originVelocity.y,source.originVelocity.z};
        dest.angularVelocity={source.angularVelocity.x,source.angularVelocity.y,source.angularVelocity.z};return true;
    };
    if(!motion(asset.boatRoot().observed,physical.boatMotion)||!motion(asset.cargoObserved,physical.cargoMotion))return {};
    if(!asset.boat->primaryRoot().helm)return {};
    physical.controlPart=*asset.boat->primaryRoot().helm;
    physical.boatRoots.reserve(asset.boatRoots->roots().size());
    for(const auto& root:asset.boatRoots->roots()) {
        CoveSavedRoot saved;saved.key=root.key;if(!motion(root.observed,saved.motion))return {};
        physical.boatRoots.push_back(saved);
    }
    const auto player=asset.player->state();
    physical.player.feet={player.feet.x,player.feet.y,player.feet.z};physical.player.verticalSpeed=player.verticalSpeed;
    physical.player.tick=player.tick;physical.player.interactions=player.interactions;
    switch(player.mode){
    case CovePlayer::Mode::Walking:physical.player.mode=CoveSavedPlayerMode::Walking;break;
    case CovePlayer::Mode::Airborne:physical.player.mode=CoveSavedPlayerMode::Airborne;break;
    case CovePlayer::Mode::Swimming:physical.player.mode=CoveSavedPlayerMode::Swimming;break;
    case CovePlayer::Mode::Helm:physical.player.mode=CoveSavedPlayerMode::Helm;break;
    }
    physical.player.aboard=player.onBoat;
    physical.player.viewYaw=static_cast<float>(asset.characterCamera.pose().yaw);
    physical.player.viewPitch=static_cast<float>(asset.characterCamera.pose().elevation);
    physical.character.profile=1;
    physical.character.worldVelocity={player.worldVelocity.x,player.worldVelocity.y,player.worldVelocity.z};
    physical.character.facingYaw=player.facingYaw;
    physical.character.cameraDistance=asset.characterCamera.userDistance();
    const auto cameraSettings=asset.characterCamera.settings();
    physical.character.chaseCamera=cameraSettings.mode==game::expedition::CoveCamera::Mode::Chase;
    physical.character.reducedMotion=cameraSettings.reducedMotion;physical.character.loadView=cameraSettings.frameLoad;
    physical.playerRoot=player.root;
    auto& savedWater=physical.water;const auto& spectrum=waterSimulation_->spectrumConfig();
    savedWater.seconds=asset.waterTime;savedWater.height=rendererSettings_.waterHeight;
    savedWater.strength=rendererSettings_.waterWaveStrength;
    savedWater.significantWaveHeight=spectrum.significantWaveHeight;savedWater.directionRadians=spectrum.directionRadians;
    savedWater.choppiness=spectrum.choppiness;savedWater.peakEnhancement=spectrum.peakEnhancement;
    savedWater.windAlignment=spectrum.windAlignment;savedWater.animationSpeed=spectrum.animationSpeed;
    savedWater.patchLengths={spectrum.patchLengths.x,spectrum.patchLengths.y};
    savedWater.cascadeAmplitudes={spectrum.cascadeAmplitudes.x,spectrum.cascadeAmplitudes.y};
    savedWater.directionalSineScale=spectrum.directionalSineScale;
    physical.cargoState=asset.cargoBanked?CoveSavedCargoState::Banked:CoveSavedCargoState::Loose;
    if(asset.towRope.valid() || asset.towBroken) {
        if(asset.cargoBanked || (asset.towRope.valid() && (!asset.towObserved.alive || asset.towObserved.broken!=asset.towBroken))
            || (!asset.towBroken && asset.towObserved.distance.motorSpeed!=0))return {};
        physical.cargoState=asset.towBroken?CoveSavedCargoState::BrokenTow:CoveSavedCargoState::Towed;
        for(const auto& module:asset.boat->assembly().functions().modules())
            if(std::holds_alternative<WinchModule>(module.parameters) && module.settings.enabled) {
                if(isValid(physical.winchPart))return {};
                physical.winchPart=module.part;
            }
        physical.ropeLength=asset.towRope.valid()?asset.towObserved.distance.targetLength:asset.towDesc.targetLength;
    }
    if(asset.harbor){
        const auto saved=asset.harbor->capture(asset.pauseTick);if(!saved)return {};
        physical.harborLift=*saved;
    }
    RecoveryIssue recoveryIssue;
    auto snapshot=SessionRecovery::capture(*local.session,context.identity.content,recoveryIssue);
    if(!snapshot)return {};
    auto admitted=SessionRecovery::admit(*snapshot,context.identity,*local.catalog,recoveryIssue);
    if(!admitted)return {};
    CoveSaveIssue issue;std::vector<std::byte> bytes;
    if(!CoveSaveCodec::encode(*admitted,local.retiredSaveParent.get(),physical,context,*local.catalog,bytes,issue))return {};
        return bytes;
    } catch(const std::bad_alloc&) {return {};}
}

std::string Application::salvageExpeditionAction(int action,std::string_view text) {
    if(action==5 && salvageLocalSession_ && salvageLocalSession_->session){
        salvageLocalSession_->storageRevoked=true;salvageLocalSession_->session->closeAdmission();return "ok";
    }
    if(salvageLocalSession_ && salvageLocalSession_->storageRevoked)return {};
    if(salvageLocalSession_ && salvageLocalSession_->asset
        && salvageLocalSession_->asset->practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None)return {};
    if(action==6) {
        if(!text.empty() || !initialized_ || salvagePreviewFailed_ || !salvageLocalSession_
            || !salvageLocalSession_->saveContext || !salvageLocalSession_->asset)return {};
        salvageLocalSession_->storageHostReady=true;return "ok";
    }
    // Trusted storage-host handshake. The returned lineage must be published
    // as one generation before the host presents its exact digest here.
    if(action==3 || action==4){
        if(!initialized_ || salvagePreviewFailed_ || !coveResume_ || coveResume_->recovered.empty())return {};
        if(action==4){
            if(text!=coveResume_->recoveredDigest)return {};
            coveResume_->awaitingCommit=false;return "ok";
        }
        if(!text.empty())return {};
        try {
            constexpr std::string_view hex="0123456789abcdef";std::string result;result.reserve(coveResume_->recovered.size()*2);
            for(auto b:coveResume_->recovered){const auto value=std::to_integer<uint8_t>(b);result+=hex[value>>4];result+=hex[value&15];}
            return result;
        }catch(const std::bad_alloc&){return {};}
    }
    const bool preparingLoad=action==2 && coveResume_ && coveResume_->awaitingCommit;

    using namespace game::expedition;
    using namespace game::construction;
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    if(!initialized_ || salvagePreviewFailed_ || !salvageLocalSession_ || !physicsWorld_ || !camera_
        || !salvageLocalSession_->saveContext || !salvageLocalSession_->asset || !salvageLocalSession_->session
        || !salvagePreview_ || (!preparingLoad && salvagePreview_->busy()) || salvageLocalSession_->pendingControl)return {};
    auto& local=*salvageLocalSession_;auto& asset=*local.asset;
    if(asset.leaving || asset.deviceLost->load(std::memory_order_acquire) || (!preparingLoad && asset.pause!=Pause::Paused)
        || asset.workshopOpen || asset.launch || asset.cargoSecuring || local.session->hasPending()
        || local.session->executionInFlight() || !asset.player || !asset.boat || !asset.cargo
        || (!preparingLoad && asset.status!=render::SalvageFixtureStatus::Active))return {};
    if(action==7) {
        // Only the owning storage host may confirm this exact frozen haul.
        // A caller-provided digest is an acknowledgment, not disk verification.
        if(!asset.checkpointPending || asset.checkpointDigest.empty()
            || text!=asset.checkpointDigest)return {};
        const bool installing=asset.harbor&&asset.harbor->installationPending();
        const bool rescuing=asset.rescue==SalvageLocalSessionState::AssetPreview::Rescue::Saving;
        if(!rescuing&&!asset.cargoBanked&&!asset.workshopSavePending)return {};
        try {asset.jobMessage=asset.workshopSavePending?"Boat and owned parts saved. Resume when ready.":rescuing?"Boat recovered and saved. All fitted parts kept. Resume when ready.":installing?"Harbor lift powered and saved. Resume to use it."
            :"Generator delivered and saved. +60 salvage material. Resume when ready.";
            if(asset.workshopSavePending)asset.launchMessage=asset.jobMessage;}
        catch(const std::bad_alloc&){return {};}
        if(installing){
            if(!asset.harbor->structure().applyPlayerCollision(*asset.player)||!asset.harbor->acknowledgeInstallation())return {};
        }
        asset.checkpointPending=false;asset.workshopSavePending=false;asset.deliveryDurable=asset.cargoBanked;asset.checkpointDigest.clear();
        if(rescuing){asset.rescue=SalvageLocalSessionState::AssetPreview::Rescue::None;++asset.rescues;}
        return "ok";
    }
    constexpr std::string_view digits="0123456789abcdef";
    try {
        if(action==2) {
            if(text.empty() || text.size()>2*kMaximumCoveSaveBytes || text.size()%2)return {};
            std::vector<std::byte> bytes;bytes.reserve(text.size()/2);
            for(size_t i=0;i<text.size();i+=2){
                const auto a=digits.find(text[i]),b=digits.find(text[i+1]);
                if(a==digits.npos || b==digits.npos)return {};
                bytes.push_back(static_cast<std::byte>(a*16+b));
            }
            std::string error;const auto origin=asset.origin;
            auto candidate=CoveRestoreCandidate::prepare(bytes,*local.saveContext,*asset.content,*local.catalog,
                asset.initialBindings,[this,origin](double x,double z){
                    return double(sampleTerrainHeight(float(x+origin.x),float(z+origin.z)))-origin.y;
                },error,&asset.initialStarterKit,coveGroundSupport(origin),asset.environmentObstacleSpan());
            return candidate?"ok":"";
        }
        if(action!=1 || !text.empty() || !waterSimulation_)return {};
        if(asset.rescue!=SalvageLocalSessionState::AssetPreview::Rescue::None
            &&asset.rescue!=SalvageLocalSessionState::AssetPreview::Rescue::Saving)return {};
        auto bytes=captureCoveCheckpoint(false);
        if(bytes.empty())return {};
        if(asset.checkpointPending)asset.checkpointDigest=core::sha256Hex(core::sha256(bytes));
        std::string result;result.reserve(bytes.size()*2);
        for(auto byte:bytes){const auto value=std::to_integer<uint8_t>(byte);result+=digits[value>>4];result+=digits[value&15];}
        return result;
    } catch(const std::bad_alloc&){return {};}
}

bool Application::salvagePreviewAction(int action) {
    if(action==340||action==341)return covePracticeAction(action);
    if((action>=350&&action<=354)||action==360||(action>=4000&&action<4064)) {
        auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
        if(!initialized_||!asset||!asset->player||!coveUi_||asset->leaving||salvagePreviewFailed_)return false;
        auto& ui=*coveUi_;
        using Pause=SalvageLocalSessionState::AssetPreview::Pause;
        if(action==350) {
            if(asset->pause!=Pause::Paused||asset->workshopOpen||ui.hostBusy
                ||asset->practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None)return false;
#if defined(VOXY_WASM)
            return EM_ASM_INT({return typeof window['voxyCoveSave']==='function'&&window['voxyCoveSave']();})!=0;
#else
            ui.saveRequested=true;return true;
#endif
        }
        if(action==351) {
#if defined(VOXY_WASM)
            return EM_ASM_INT({return typeof window['voxyCoveMenu']==='function'&&window['voxyCoveMenu']('job');})!=0;
#else
            if(!nativeWorkshopMenu_)return false;
            nativeWorkshopMenu_->openJob();return true;
#endif
        }
        if(action==352) {ui.tutorial.restart();ui.walkedMetres=0;ui.sailedMetres=0;ui.saveConfirmed=false;return true;}
        if(action==353) {
            auto next=ui.preferences;next.tutorialsEnabled=!next.tutorialsEnabled;
            std::string encoded,error;
            return game::expedition::encodeCoveInputPreferences(next,encoded,error)&&covePreferencesAction(2,encoded)=="ok";
        }
#if defined(VOXY_NATIVE)
        if(ui.hostBusy||ui.saveRequested||ui.worldPreparing||asset->checkpointPending||asset->workshopSavePending
            ||asset->pause!=Pause::Paused)return false;
        if(action==354) {
            if(!salvagePreviewAction(2))return false;
            ui.exitAfterLeave=true;return true;
        }
        if(action==360||(action>=4000&&size_t(action-4000)<ui.worlds.size())) {
            const std::string world=action==360?"":ui.worlds[size_t(action-4000)].first;
            if(!world.empty()&&world==ui.currentWorld)return false;
            ui.worldRequest=world;ui.worldPreparing=true;ui.worldStatus="Opening expedition...";return true;
        }
#endif
        return false;
    }
    if((coveResume_ && !coveResume_->ready) || (salvageLocalSession_ && salvageLocalSession_->storageRevoked && action!=2))return false;
    if (!initialized_ || !config_.salvagePreviewEnabled || !salvagePreview_
        || salvagePreviewFailed_ || !salvageLocalSession_ || !salvageLocalSession_->session) return false;
    if(auto* asset=salvageLocalSession_->asset.get();asset&&asset->practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None) {
        if(action==1)return covePracticeAction(341);
        if(action!=2 && (asset->practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::Running
            || !(action==30||(action>=40&&action<=43)||action==90||action==91||(action>=320&&action<=325))))return false;
    }
    if(salvageLocalSession_->asset && (salvageLocalSession_->asset->checkpointPending
        ||salvageLocalSession_->asset->rescue!=SalvageLocalSessionState::AssetPreview::Rescue::None
        ||(salvageLocalSession_->asset->harbor&&salvageLocalSession_->asset->harbor->installationPending())))return false;
    using Preview = game::expedition::SalvagePreview;
    using Pause = SalvageLocalSessionState::AssetPreview::Pause;
    if(action>=320&&action<=325) {
        auto* asset=salvageLocalSession_->asset.get();
        if(!asset||!asset->player||asset->workshopOpen||asset->leaving||asset->launch
            ||salvageLocalSession_->launchRequest||asset->status!=render::SalvageFixtureStatus::Active)return false;
        auto settings=asset->characterCamera.settings();
        using CharacterCamera=game::expedition::CoveCamera;
        if(action==320)settings.mode=settings.mode==CharacterCamera::Mode::Chase?CharacterCamera::Mode::Orbit:CharacterCamera::Mode::Chase;
        if(action==321)return asset->characterCamera.restoreOrbit(asset->player->facingYaw(),
            asset->characterCamera.pose().elevation,asset->characterCamera.userDistance());
        if(action==322)settings.frameLoad=!settings.frameLoad;
        if(action==323)settings.reducedMotion=!settings.reducedMotion;
        if(action==324||action==325)return asset->characterCamera.setUserDistance(std::clamp(
            asset->characterCamera.userDistance()*std::exp(action==324?-.14:.14),
            CharacterCamera::minimumDistance,CharacterCamera::maximumDistance));
        return asset->characterCamera.settings(settings);
    }
    if(action==90 || action==91) {
        auto* asset=salvageLocalSession_->asset.get();
        if(!asset || !asset->player || !asset->boatRoot().body.valid() || asset->leaving || !physicsWorld_
            || salvagePreview_->busy() || salvageLocalSession_->pendingControl
            || asset->status!=render::SalvageFixtureStatus::Active)return false;
        if(action==90) {
            if(asset->pause!=Pause::Running || asset->workshopOpen || asset->launch || asset->cargoSecuring
                || salvageLocalSession_->session->hasPending() || asset->towAction
                || !salvageLocalSession_->executionBound)return false;
            asset->pause=Pause::Requested;asset->pauseTick=0;asset->pauseWaitSeconds=0;
        } else {
            if(asset->pause!=Pause::Paused || !physicsWorld_->setSchedulingPaused(false))return false;
            asset->pause=Pause::Running;asset->pauseTick=0;
        }
        asset->player->discardPendingInput();
        if(input_){input_->releaseMouse();input_->resetState();}
        return true;
    }
    if(salvageLocalSession_->asset && salvageLocalSession_->asset->pause!=Pause::Running && action!=2)return false;
    if(action==95) {
        auto& local=*salvageLocalSession_;auto* asset=local.asset.get();auto& session=*local.session;
        if(!asset||!local.storageHostReady||!local.executionBound||!session.admissionOpen()
            ||session.hasPending()||session.executionInFlight()||asset->launch||salvagePreview_->busy()
            ||local.pendingControl||asset->status!=render::SalvageFixtureStatus::Active)return false;
        const auto target=asset->cutTarget();
        if(!target){asset->launchMessage="Stand near a weld and release all cables before cutting.";return false;}
        game::expedition::Command command{session.events().identity().epoch,
            game::construction::RequestSequence{session.processedSequence()+1},session.snapshot().revision,
            game::expedition::CutWeld{{local.boatId,asset->boat->build().revision},target->weld}};
        asset->launchMessage="Preparing cut…";
        const auto result=session.submit(local.caller,command);local.launchRequest=command.sequence;
        asset->player->discardPendingInput();if(input_)input_->resetState();return result.admitted;
    }
    if(((action>=60 && action<=89)||(action>=92&&action<=94)||(action>=96&&action<=98)||(action>=300&&action<=309)||(action>=100&&action<100+int(game::assets::kMaximumFixtureBundles))
        ||(action>=200&&action<200+int(game::expedition::kBrickPaintPalette.size()))) && salvageLocalSession_->asset && salvageLocalSession_->asset->player) {
        auto& asset=*salvageLocalSession_->asset;
        if(asset.leaving || salvagePreview_->busy() || salvageLocalSession_->pendingControl
            || salvageLocalSession_->session->hasPending() || asset.launch || asset.status!=render::SalvageFixtureStatus::Active)return false;
        if(action==60) {
            if(!asset.workshopOpen) {
                if(asset.harbor&&asset.harbor->state().mode!=game::expedition::CoveHarborLiftMode::Detached)return false;
                if(asset.player->onBoat() || glm::length(asset.player->feet()-asset.content->registry.navigation->spawn)>3)return false;
                if(!asset.workshop) {
                    std::string error;asset.workshop=game::expedition::CoveWorkshop::create(asset.acceptedScene(),error,static_cast<uint32_t>(asset.content->registry.placements.size()),asset.content->registry.navigation->boatPlacements,asset.boat->roots().size()>1);
                    if(!asset.workshop){LOG_ERROR("Workshop: {}",error);return false;}
                }
                asset.workshopReturnView=SalvageLocalSessionState::AssetPreview::WorkshopView{camera_->position(),camera_->yaw(),camera_->pitch(),camera_->worldSector()};
                asset.workshopOpen=true;asset.workshopPointerPlacement=false;
#if defined(VOXY_NATIVE)
                asset.menuQuoteRevision=std::numeric_limits<uint64_t>::max();
#endif
                asset.workshopFrameRequest=2;
            } else {
                (void)asset.workshop->stopBrickTool();asset.workshopPointerPlacement=false;
                asset.workshopOpen=false;
                if(asset.workshopReturnView) {
                    const auto& v=*asset.workshopReturnView;
                    camera_->setWorldSector(v.sector);camera_->setPosition(v.position);camera_->setYaw(v.yaw);camera_->setPitch(v.pitch);
                }
            }
            if(input_){input_->releaseMouse();input_->resetState();}
            return true;
        }
        if(!asset.workshopOpen)return false;
        if(action==97||action==98){asset.workshopFrameRequest=action==97?1:2;return true;}
        if(action==96) {
            const bool stopped=asset.workshop->stopBrickTool();
            asset.workshopPointerPlacement=false;asset.workshopPointerTarget=false;
            if(stopped)asset.launchMessage.clear();
            return stopped;
        }
        // Launch, history and machinery operate on kept design data. The
        // continuously suggested brick is never part of their price or save.
        if((action>=79&&action<=89)||(action>=92&&action<=94)) {
            if(asset.workshop->stopBrickTool())asset.workshopPointerPlacement=false;
        }
        if(action==89||(action>=92&&action<=94)) {
            if(asset.recoveryDesigns.empty()||asset.selectedRecoveryDesign>=asset.recoveryDesigns.size()||asset.workshop->changed())return false;
            if(action==92||action==93) {
                asset.selectedRecoveryDesign=(asset.selectedRecoveryDesign+asset.recoveryDesigns.size()+(action==93?1:asset.recoveryDesigns.size()-1))%asset.recoveryDesigns.size();return true;
            }
            if(action==89) {
                std::string error;
                if(!asset.workshop->loadBlueprint(asset.recoveryDesigns[asset.selectedRecoveryDesign],error)){asset.launchMessage=error;return false;}
                asset.launchMessage="Recovered design loaded. Launch uses owned parts and normal costs.";return true;
            }
            if(!salvageLocalSession_->storageHostReady||!salvageLocalSession_->executionBound
                ||!asset.workshop->matchesDesign(asset.acceptedScene()))return false;
            if(std::any_of(asset.boat->build().connections.begin(),asset.boat->build().connections.end(),
                [](const auto& link){return !link.enabled;})) {
                asset.launchMessage="Rebuild the broken machine before removing protected designs.";return false;
            }
            asset.recoveryDesigns.erase(asset.recoveryDesigns.begin()+static_cast<std::ptrdiff_t>(asset.selectedRecoveryDesign));
            asset.selectedRecoveryDesign=asset.recoveryDesigns.empty()?0:std::min(asset.selectedRecoveryDesign,asset.recoveryDesigns.size()-1);
            asset.workshopOpen=false;asset.workshopSavePending=true;asset.checkpointPending=true;asset.checkpointDigest.clear();
            asset.pause=Pause::Requested;asset.pauseTick=0;asset.pauseWaitSeconds=0;asset.player->discardPendingInput();
            setCameraWorldPose(*camera_,glm::vec3(asset.origin+asset.player->feet()+glm::dvec3(0,game::expedition::CovePlayer::eyeHeight,0)),
                glm::vec3(asset.origin+asset.acceptedScene().registry.navigation->lookTarget));
            asset.launchMessage="Saving recovery design removal…";asset.jobMessage=asset.launchMessage;
            if(input_){input_->releaseMouse();input_->resetState();}return true;
        }
        if(action==88) {
            auto& local=*salvageLocalSession_;auto& session=*local.session;
            if(!local.storageHostReady||!local.executionBound||!session.admissionOpen()||asset.workshop->changed()
                ||!asset.workshop->matchesDesign(asset.acceptedScene())||asset.towRope.valid())return false;
            if(!session.starterKit(local.boatId)) {
                const auto issue=session.registerStarterKit(asset.initialStarterKit,session.snapshot().revision);
                if(issue){asset.launchMessage="Starter rebuild is unavailable. Your boat is unchanged.";return false;}
            }
            game::expedition::Command command{session.events().identity().epoch,
                game::construction::RequestSequence{session.processedSequence()+1},session.snapshot().revision,
                game::expedition::RebuildStarter{{local.boatId,asset.boat->build().revision}}};
            asset.launchMessage="Preparing boat…";
            const auto result=session.submit(local.caller,command);local.launchRequest=command.sequence;return result.admitted;
        }
        if(action>=300&&action<=309) {
            auto& workshop=*asset.workshop;
            using Workshop=game::expedition::CoveWorkshop;
            if(workshop.brickToolActive()) { (void)workshop.stopBrickTool();asset.workshopPointerPlacement=false; }
            bool edited=false;
            switch(action) {
                case 300:edited=workshop.selectAll();break;
                case 301:edited=workshop.selectOnlyPrimary();break;
                case 302:edited=workshop.duplicateSelection({32,0,0});break;
                case 303:edited=workshop.mirrorSelection(Workshop::Axis::X,0);break;
                case 304:edited=workshop.mirrorSelection(Workshop::Axis::Z,0);break;
                case 305:edited=workshop.replaceSelection(workshop.catalogIndex());break;
                case 306:edited=workshop.command(Workshop::Action::Redo);break;
                case 307:edited=workshop.rotateSelection(Workshop::Axis::X);break;
                case 308:edited=workshop.rotateSelection(Workshop::Axis::Z);break;
                case 309: {
                    const auto& parts=workshop.design().registry.navigation->boatPlacements;
                    const auto it=std::find(parts.begin(),parts.end(),workshop.selected());
                    if(!parts.empty())edited=workshop.selectPart(parts[(size_t(it-parts.begin())+1)%parts.size()],Workshop::SelectionMode::Toggle);
                    break;
                }
            }
            asset.workshopPointerPlacement=false;asset.workshopPointerTarget=false;
            if(edited)asset.launchMessage.clear();
            return edited;
        }
        if(action>=200) {
            if(!asset.workshop->setPaint(static_cast<uint32_t>(action-200)))return false;
            asset.launchMessage.clear();return true;
        }
        if(action>=100) {
            const auto index=static_cast<uint32_t>(action-100);
            if(index>=asset.workshop->catalogCount())return false;
            if(asset.workshop->catalogNameAt(index).starts_with("Brick ")) {
                const auto state=salvageLocalSession_->session->snapshot();
                const auto bundle=asset.workshop->catalogBundleAt(index);if(!bundle)return false;
                const auto key=asset.workshop->design().bundles[*bundle]->sidecar().part.key;
                const auto* stored=game::expedition::availableCoveStoredPart(asset.workshop->design(),*asset.boat,state.storedParts,key);
                if(!asset.workshop->beginBrickTool(index,stored))return false;
                asset.workshopPointerPlacement=true;asset.workshopPointerTarget=false;
                asset.launchMessage.clear();return true;
            }
            if(asset.workshop->stopBrickTool())asset.workshopPointerPlacement=false;
            if(!asset.workshop->canAdd()||!asset.workshop->selectCatalogAt(static_cast<uint32_t>(action-100)))return false;
            return salvagePreviewAction(84);
        }
        if(action>=85) {
            const bool changed=asset.workshop->configure(static_cast<game::expedition::CoveWorkshop::SettingAction>(action-85));
            if(changed)asset.launchMessage.clear();
            return changed;
        }
        if(action>=82) {
            const auto state=salvageLocalSession_->session->snapshot();
            const auto* stored=game::expedition::availableCoveStoredPart(asset.workshop->design(),*asset.boat,state.storedParts,asset.workshop->catalogDefinition());
            const bool changed=action==84?asset.workshop->addPart(stored):asset.workshop->selectCatalog(action==82?-1:1);
            if(changed){asset.launchMessage.clear();if(action==84)asset.workshopPointerPlacement=true;}
            return changed;
        }
        if(action>=79) {
            auto& local=*salvageLocalSession_;auto& session=*local.session;
            if(asset.launch || !local.executionBound || !session.admissionOpen())return false;
            game::expedition::Command command;
            command.epoch=session.events().identity().epoch;
            command.sequence=game::construction::RequestSequence{session.processedSequence()+1};
            command.expectedRevision=session.snapshot().revision;
            if(action==79) {
                if(asset.workshop->changed() || asset.workshop->matchesDesign(asset.acceptedScene()))return false;
                std::string error;
                const auto quote=game::expedition::quoteCoveDesign(asset.workshop->design(),*asset.boat,*local.catalog,session.snapshot().storedParts);
                if(!quote || !quote->affordable(session.snapshot().inventory)) {
                    asset.launchMessage="Not enough material or machinery for this design.";return false;
                }
                const auto refit=game::expedition::prepareCoveRefit(asset.workshop->design(),*asset.boat,*local.catalog,error,
                    asset.content->registry.navigation->boatPlacements,session.snapshot().storedParts);
                if(!refit){asset.launchMessage=error;return false;}
                command.intent=game::expedition::RefitBuild{{local.boatId,asset.boat->build().revision},refit->design};
            } else {
                if(asset.workshop->changed() || !asset.workshop->matchesDesign(asset.acceptedScene()))return false;
                const auto history=session.history();const auto choice=action==80?history.undo:history.redo;
                if(!choice || choice->target.build!=local.boatId)return false;
                command.intent=action==80?game::expedition::Intent{game::expedition::Undo{choice->target,choice->entry,history.generation}}
                    :game::expedition::Intent{game::expedition::Redo{choice->target,choice->entry,history.generation}};
            }
            asset.launchMessage="Preparing boat…";
            const auto result=session.submit(local.caller,command);local.launchRequest=command.sequence;
            return result.admitted;
        }
        if(action>=75) {
            if(action==75)asset.workshopCamera.orbit(-.25,0);
            if(action==76)asset.workshopCamera.orbit(.25,0);
            if(action==77)asset.workshopCamera.zoom(1);
            if(action==78)asset.workshopCamera.zoom(-1);
            return true;
        }
        const bool wasBrickTool=asset.workshop->brickToolActive();
        const bool edited=asset.workshop->command(static_cast<game::expedition::CoveWorkshop::Action>(action-61));
        if(wasBrickTool)asset.workshopPointerPlacement=asset.workshop->brickToolActive();
        else if(edited && ((action>=61&&action<=68)||action==70||action==71||action==72||action==73||action==74))asset.workshopPointerPlacement=false;
        if(edited)asset.launchMessage.clear();
        return edited;
    }
    if(salvageLocalSession_->asset && salvageLocalSession_->asset->workshopOpen && action!=2)return false;
    if(action==1 && salvageLocalSession_->asset && salvageLocalSession_->asset->player){
        auto& local=*salvageLocalSession_;auto& asset=*local.asset;
        if(!local.storageHostReady || !local.executionBound || !local.session->admissionOpen()
            || local.session->hasPending() || local.session->executionInFlight() || local.pendingControl
            || salvagePreview_->busy() || asset.leaving || asset.launch || asset.cargoSecuring || asset.towAction
            || !asset.boatRoot().body.valid() || !asset.cargoBody.valid()
            || asset.status!=render::SalvageFixtureStatus::Active)return false;
        asset.rescue=SalvageLocalSessionState::AssetPreview::Rescue::Requested;
        asset.pause=Pause::Requested;asset.pauseTick=0;asset.pauseWaitSeconds=0;
        asset.jobMessage="Recovering your boat. Fitted parts stay yours.";
        asset.player->discardPendingInput();if(input_){input_->releaseMouse();input_->resetState();}
        return true;
    }
    if((action==50 || action==51) && salvageLocalSession_->jobId.counter && salvageLocalSession_->asset) {
        auto& local=*salvageLocalSession_;auto& session=*local.session;auto& asset=*local.asset;
        if(asset.leaving || session.hasPending() || !local.executionBound || salvagePreview_->busy()
            || local.pendingControl || !session.admissionOpen()) return false;
        const auto state=session.snapshot();
        const auto job=std::find_if(state.jobs.begin(),state.jobs.end(),[&](const auto& j){return j.id==local.jobId;});
        if(job==state.jobs.end() || (action==50?job->phase!=game::expedition::JobPhase::Available
            :job->phase!=game::expedition::JobPhase::Accepted)) return false;
        game::expedition::Command command;command.sequence=game::construction::RequestSequence{session.processedSequence()+1};
        command.epoch=session.events().identity().epoch;
        command.expectedRevision=state.revision;
        if(action==51 && !local.storageHostReady)return false;
        command.intent=action==50?game::expedition::Intent{game::expedition::AcceptJob{local.jobId}}
            :game::expedition::Intent{game::expedition::DeliverCargo{local.cargoId}};
        const auto result=session.submit(local.caller,command);
        local.jobRequest=command.sequence;
        if(action==51 && result.admitted && result.state!=game::expedition::ReceiptState::Rejected) {
            asset.checkpointPending=true;asset.checkpointDigest.clear();
            asset.player->discardPendingInput();if(input_)input_->resetState();
        }
        asset.jobMessage=result.state==game::expedition::ReceiptState::Rejected
            ? "Bring the generator into the harbor area and slow down before delivering." : "Confirming harbor hand-off…";
        return result.admitted;
    }
    if(action>=52&&action<=57&&salvageLocalSession_->asset){
        auto& asset=*salvageLocalSession_->asset;
        if(!asset.harbor||asset.leaving||asset.workshopOpen||asset.launch||salvagePreview_->busy()
            ||salvageLocalSession_->pendingControl||salvageLocalSession_->session->hasPending()
            ||!asset.deliveryDurable||!asset.boatRoot().body.valid())return false;
        if(action==56)return asset.harbor->stop(*physicsWorld_);
        const auto& navigation=*asset.content->registry.navigation;
        const bool atDock=!asset.player->onBoat()&&glm::length(asset.player->feet()-navigation.dockBoarding)<=4;
        if(!atDock)return false;
        if(action==52){
            if(!salvageLocalSession_->storageHostReady||!asset.harbor->requestInstall())return false;
            asset.pause=Pause::Requested;asset.pauseTick=0;asset.pauseWaitSeconds=0;
            asset.player->discardPendingInput();if(input_)input_->resetState();return true;
        }
        using Action=game::expedition::CoveHarborRuntime::Action;
        const auto operation=action==53?Action::Attach:action==54?Action::Raise:action==55?Action::Lower:Action::Release;
        return asset.harbor->action(operation,*physicsWorld_,asset.boatRoot().body,asset.boatRoot().observed);
    }
    if(action>=40 && action<=43 && salvageLocalSession_->asset) {
        auto& asset=*salvageLocalSession_->asset;
        if(asset.towReelSpeed<=0 || !asset.cargoBody.valid() || asset.cargoBanked || asset.cargoSecuring || salvageLocalSession_->session->hasPending()
            || !asset.towRoot().body.valid() || !asset.player->onBoat()
            || asset.player->state().root!=asset.towRoot().key
            || asset.leaving || salvagePreview_->busy() || salvageLocalSession_->pendingControl
            || asset.towAction || asset.status!=render::SalvageFixtureStatus::Active) return false;
        if(action!=40 && (!asset.towRope.valid() || asset.towBroken)) return false;
        asset.towAction=action;return true;
    }
    if (action == 30 && salvageLocalSession_->asset && salvageLocalSession_->asset->player) {
        auto& asset = *salvageLocalSession_->asset;
        return !asset.leaving && !salvagePreview_->busy() && !salvageLocalSession_->pendingControl
            && !salvageLocalSession_->session->hasPending()
            && asset.status == render::SalvageFixtureStatus::Active && asset.player->requestInteraction();
    }
    if (action >= 20 && action <= 22 && salvageLocalSession_->asset) {
        if (config_.salvageAssetFixtureWaterAnchor && action != 20) return false;
        auto& asset = *salvageLocalSession_->asset;
        if (asset.leaving || salvagePreview_->busy() || salvageLocalSession_->pendingControl
            || asset.status != render::SalvageFixtureStatus::Active) return false;
        asset.guides = static_cast<render::InspectionGuides>(action - 20);
        return true;
    }
    if (action >= 10 && action <= 13 && salvageLocalSession_->asset) {
        auto& asset = *salvageLocalSession_->asset;
        if (asset.leaving || salvagePreview_->busy()) return false;
        const auto id = static_cast<uint64_t>(action - 10);
        if (id != 0) for (const auto& bundle : asset.content->bundles) {
            if (std::none_of(bundle->lods().begin(), bundle->lods().end(), [id](const auto& lod) { return lod.id == id; })) return false;
        }
        asset.forcedLod = id;
        return true;
    }
    if (action != static_cast<int>(Preview::Action::Reset)
        && action != static_cast<int>(Preview::Action::Leave)) return false;
    if (salvagePreview_->phase() == Preview::Phase::Empty
        || salvagePreview_->phase() == Preview::Phase::Failed) return false;
    const auto request = static_cast<Preview::Action>(action);
    if (request == Preview::Action::Reset && salvageLocalSession_->asset
        && (salvageLocalSession_->asset->status == render::SalvageFixtureStatus::ViewsValidating
            || salvageLocalSession_->asset->fixture.stats().active.generation == 0)) return false;
    auto& pending = salvageLocalSession_->pendingControl;
    if (request == Preview::Action::Reset
        && (!salvageLocalSession_->session->admissionOpen() || salvageLocalSession_->session->hasPending() || salvagePreview_->busy()
            || pending == Preview::Action::Leave)) return false;
    // UI submits intent only. Leave has its own reserved control slot and
    // supersedes Reset; authority/scene changes happen at the frame boundary.
    pending = request;
    return true;
}

#if defined(VOXY_NATIVE)
void Application::updateNativeWorkshopMenu() {
    auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
    if(!asset||!asset->player||!input_) {if(nativeWorkshopMenu_)nativeWorkshopMenu_->dismiss();return;}
    asset->menuConsumedFrame=false;
    if(!nativeWorkshopMenu_) {
        game::expedition::StoreIssue issue;
        auto root=config_.salvageDesignLibraryRoot.empty()?platform::NativeDesignLibrary::defaultRoot(issue):config_.salvageDesignLibraryRoot;
        nativeWorkshopMenu_=std::make_unique<platform::NativeWorkshopMenu>(std::move(root),
            [this](int action){
                const bool accepted=salvagePreviewAction(action);
                LOG_INFO("Cove menu action {}: {}",action,accepted?"accepted":"refused");
                return accepted;
            },
            [this](int action,std::string_view bytes){return salvageBlueprintAction(action,bytes);},
            [this](int action,std::string_view text){return covePreferencesAction(action,text);});
    }
    if(!nativeWorkshopMenu_)return;
    platform::NativeWorkshopMenu::Facts facts;
    facts.cameraAvailable=!asset->workshopOpen&&!asset->leaving&&!asset->launch&&!asset->checkpointPending
        &&!salvageLocalSession_->storageRevoked&&!salvageLocalSession_->launchRequest
        &&asset->status==render::SalvageFixtureStatus::Active&&!salvagePreview_->busy()
        &&!salvageLocalSession_->pendingControl&&asset->rescue==SalvageLocalSessionState::AssetPreview::Rescue::None
        &&!(asset->harbor&&asset->harbor->installationPending());
    const auto viewSettings=asset->characterCamera.settings();
    facts.chaseCamera=viewSettings.mode==game::expedition::CoveCamera::Mode::Chase;
    facts.reducedMotion=viewSettings.reducedMotion;facts.frameLoad=viewSettings.frameLoad;
    facts.cameraDistance=asset->characterCamera.userDistance();
    facts.workshopOpen=asset->workshopOpen&&!asset->leaving&&!salvagePreviewFailed_;
    facts.pending=salvageLocalSession_->storageRevoked
        ||salvageLocalSession_->pendingControl||salvageLocalSession_->session->hasPending()
        ||salvageLocalSession_->launchRequest||asset->launch||asset->checkpointPending
        ||asset->status!=render::SalvageFixtureStatus::Active||salvagePreview_->busy();
    if(asset->workshop&&facts.workshopOpen) {
        const auto& w=*asset->workshop;const bool clean=!w.changed();
        facts.canLoadBlueprint=clean&&!w.brickToolActive();
        facts.canKeep=w.changed()&&w.valid();facts.canUndo=w.undoCount()>0;facts.canRedo=clean&&w.redoCount()>0;
        facts.canPaint=w.canPaint();facts.canConfigure=w.configurable();facts.canAdd=w.canAdd();
        facts.brushActive=w.brickToolActive();facts.hasOutputLimit=w.hasOutputLimit();facts.canReverse=w.canReverse();
        facts.paintIndex=w.paintIndex();facts.selectedCount=w.selectedParts().size();
        facts.selectedName=std::string(w.selectedName());
        facts.message=asset->launchMessage.empty()?w.message():asset->launchMessage;
        const auto owned=salvageLocalSession_->session->snapshot();
        if(asset->menuQuoteRevision!=w.revision()) {
            asset->menuQuote=game::expedition::quoteCoveDesign(w.design(),*asset->boat,*salvageLocalSession_->catalog,owned.storedParts);
            asset->menuQuoteRevision=w.revision();
        }
        const bool same=w.matchesDesign(asset->acceptedScene());
        const bool kept=clean||w.brickToolActive();
        facts.canLaunch=kept&&!same&&asset->menuQuote&&asset->menuQuote->affordable(owned.inventory);
        if(asset->menuQuote)facts.message+=" | Launch "+std::to_string(asset->menuQuote->debit.salvageMaterial)
            +" material; return "+std::to_string(asset->menuQuote->credit.salvageMaterial);
        const auto history=salvageLocalSession_->session->history();
        facts.canUndoLaunch=kept&&same&&history.undo&&history.undo->target.build==salvageLocalSession_->boatId;
        facts.canRedoLaunch=kept&&same&&history.redo&&history.redo->target.build==salvageLocalSession_->boatId;
        facts.catalogIndex=w.catalogIndex();
        for(uint32_t i=0;i<w.catalogCount();++i)facts.catalogNames.emplace_back(w.catalogNameAt(i));
    }
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    using Practice=SalvageLocalSessionState::AssetPreview::PracticePhase;
    const auto& local=*salvageLocalSession_;const auto owned=local.session->snapshot();
    facts.gameAvailable=!asset->leaving&&!salvagePreviewFailed_&&!local.storageRevoked
        &&asset->status==render::SalvageFixtureStatus::Active;
    facts.paused=asset->pause==Pause::Paused;facts.pausePending=asset->pause!=Pause::Running&&!facts.paused;
    facts.canResume=facts.paused&&!asset->checkpointPending&&!local.pendingControl&&!asset->launch
        &&!(coveUi_&&coveUi_->worldPreparing);
    facts.testActive=asset->practicePhase!=Practice::None;
    facts.testBusy=facts.testActive&&asset->practicePhase!=Practice::Running;
    facts.testAvailable=covePracticeCanBegin();facts.canReturnTest=covePracticeCanReturn();
    facts.practiceMessage=asset->practiceMessage;
    const bool hostBusy=coveUi_&&(coveUi_->hostBusy||coveUi_->worldPreparing||coveUi_->saveRequested);
    facts.canSave=facts.paused&&!asset->workshopOpen&&!facts.testActive&&!hostBusy;
    facts.canRescue=!facts.testActive&&!facts.pending&&!asset->checkpointPending;
    facts.canQuit=facts.paused&&!facts.testBusy&&!hostBusy&&!asset->checkpointPending&&!asset->workshopSavePending;
    facts.canChangeWorld=facts.canQuit;
    facts.saveMessage=salvageSaveStatus_;
    if(coveUi_&&!coveUi_->worldStatus.empty())facts.saveMessage=coveUi_->worldStatus;
    facts.inventoryLines={"Material: "+std::to_string(owned.inventory.salvageMaterial),
        "Machinery: "+std::to_string(owned.inventory.specialMachinery),
        "Fitted parts: "+std::to_string(asset->boat->parts().size()),
        "Stored parts: "+std::to_string(owned.storedParts.size()),
        "Blueprints save a design.","Launch checks parts and material."};
    const auto distance=[&](std::string label,glm::dvec3 position){
        return label+": "+std::to_string(static_cast<int>(std::round(glm::length(position-asset->player->feet()))))+" m";
    };
    if(asset->content->registry.navigation){
        const auto& navigation=*asset->content->registry.navigation;
        facts.mapLines.push_back(distance("Boarding dock",navigation.dockBoarding));
        if(navigation.delivery)facts.mapLines.push_back(distance("Delivery harbor",navigation.delivery->center));
    }
    if(asset->boatRoot().observedTick)facts.mapLines.push_back(distance("Your boat",physics::worldPositionToAbsolute(asset->boatRoot().observed.position)-asset->origin));
    if(asset->cargoObservedTick&&!asset->cargoBanked)facts.mapLines.push_back(distance("Sunken generator",physics::worldPositionToAbsolute(asset->cargoObserved.position)-asset->origin));
    facts.mapLines.push_back("Distances from your robot.");
    const auto job=std::find_if(owned.jobs.begin(),owned.jobs.end(),[&](const auto& j){return j.id==local.jobId;});
    if(job!=owned.jobs.end()){
        using Phase=game::expedition::JobPhase;
        facts.jobTitle="Recover the generator";
        facts.jobText=job->phase==Phase::Available?"Accept the job. Sail to the sunken generator.":job->phase==Phase::Accepted
            ?"Hook the generator. Tow it into the delivery harbor.":asset->deliveryDurable?"Delivery saved. Your material reward is available.":"Delivered. Wait for save confirmation.";
        facts.jobAction=job->phase==Phase::Available?50:job->phase==Phase::Accepted?51:0;
        facts.jobActionLabel=job->phase==Phase::Available?"Accept job":"Deliver cargo";
        facts.jobActionEnabled=!facts.pending&&!facts.testActive&&!facts.paused&&local.session->admissionOpen();
        if(job->phase==Phase::Accepted){
            const auto cargo=std::find_if(owned.cargo.begin(),owned.cargo.end(),[&](const auto& c){return c.id==local.cargoId;});
            facts.jobActionEnabled=facts.jobActionEnabled&&local.storageHostReady&&cargo!=owned.cargo.end()
                &&local.preparation.eligible&&local.preparation.eligible({*cargo,*job,*job,{}});
        }
    }
    if(coveUi_){
        facts.preferences=&coveUi_->preferences;facts.preferencesMessage=coveUi_->preferenceStatus;
        for(const auto& [id,name]:coveUi_->worlds){(void)id;facts.worldNames.push_back(name);}
        const auto card=coveUi_->tutorial.card(input_->gamepad().connected(),facts.paused,asset->workshopOpen);
        facts.tutorialTitle=card.title;facts.tutorialText=card.text;facts.tutorialActionLabel=card.actionLabel;
        facts.tutorialAction=card.action;facts.tutorialComplete=card.complete;
        for(const auto& action:game::expedition::coveActionList()){
            const auto& binding=coveUi_->preferences.bindings[static_cast<size_t>(action.action)];
            if(!(action.contexts&(asset->workshopOpen?2:1)))continue;
            facts.helpLines.emplace_back(std::string(action.label)+": "+(input_->gamepad().connected()
                ?game::expedition::covePadLabel(binding.pad):game::expedition::coveKeyLabel(binding.key,binding.modifiers)));
        }
    }
    const bool before=nativeWorkshopMenu_->active();
    const auto width=gpuContext_->getSwapchainWidth(),height=gpuContext_->getSwapchainHeight();
    const glm::vec2 pointerScale{float(width)/float(std::max(window_->getWidth(),1)),
        float(height)/float(std::max(window_->getHeight(),1))};
    const bool consumed=nativeWorkshopMenu_->tick(*input_,facts,width,height,pointerScale);
    const bool after=nativeWorkshopMenu_->active();
    if(before!=after)input_->resetState();
    asset->controllerMenuOwner=after;asset->menuConsumedFrame=consumed;
}

render::CoveHudContent Application::nativeCoveHudContent() const {
    render::CoveHudContent hud;
    if(coveUi_){hud.textScale=static_cast<float>(coveUi_->preferences.textScale);hud.highContrast=coveUi_->preferences.highContrast;}
    const auto& local=*salvageLocalSession_;
    const auto& asset=*local.asset;
    using Tone=render::CoveHudTone;
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    using Action=game::expedition::CoveAction;
    static const game::expedition::CoveInputPreferences defaults;
    const auto& preferences=coveUi_?coveUi_->preferences:defaults;
    const bool controller=input_&&input_->gamepad().connected();
    const auto binding=[&](Action action) {
        const auto& b=preferences.bindings[static_cast<size_t>(action)];
        if(controller&&b.pad>=0)return game::expedition::covePadLabel(b.pad);
        if(b.key)return game::expedition::coveKeyLabel(b.key,b.modifiers);
        if(b.alternate)return game::expedition::coveKeyLabel(b.alternate,b.alternateModifiers);
        return std::string("Unbound");
    };
    const auto boundText=[&](std::string& text) {
        const std::pair<std::string_view,std::string> labels[]{
            {"Q/Z:",binding(Action::ReelIn)+"/"+binding(Action::PayOut)+":"},
            {"F10:",binding(Action::Save)+":"},{"P:",binding(Action::Pause)+":"},
            {"J:",binding(Action::AcceptJob)+":"},{"H:",binding(Action::Deliver)+":"},
            {"K:",binding(Action::PowerHarbor)+":"},{"B:",binding(Action::Workshop)+":"},
            {"E:",binding(Action::Interact)+":"},{"F:",binding(Action::Hook)+":"},
            {"Q:",binding(Action::ReelIn)+":"},{"Z:",binding(Action::PayOut)+":"}};
        // Only fixed formatter tokens are substituted. The formatter's numeric
        // eligibility and refusal text remain authoritative and unchanged.
        std::string output;output.reserve(text.size());
        for(size_t i=0;i<text.size();) {
            bool matched=false;
            for(const auto& [token,label]:labels)if(std::string_view(text).substr(i).starts_with(token)) {
                output+=label;i+=token.size();matched=true;break;
            }
            if(!matched)output+=text[i++];
        }
        text=std::move(output);
    };
    const auto finish=[&](bool guide=false) {
        if(guide&&coveUi_&&preferences.tutorialsEnabled&&hud.tone!=Tone::Blocked&&hud.tone!=Tone::Waiting) {
            const auto card=coveUi_->tutorial.card(controller,asset.pause==Pause::Paused,asset.workshopOpen);
            if(!card.complete)hud.title="Guide "+std::to_string(card.number)+"/"+std::to_string(card.total)+": "+std::string(card.title);
        }
        if(coveUi_&&preferences.captionsEnabled&&!coveUi_->caption.empty()
            &&hud.tone!=Tone::Blocked&&hud.tone!=Tone::Waiting)hud.hints[2]=coveUi_->caption;
        return hud;
    };
    if(nativeWorkshopMenu_&&nativeWorkshopMenu_->active()) {
        hud.menu=nativeWorkshopMenu_->menuContent();hud.title=hud.menu->title;
        hud.tone=Tone::Neutral;hud.objective="workshop-tools";return hud;
    }
    const auto owned=local.session->snapshot();
    hud.economy="Material "+std::to_string(owned.inventory.salvageMaterial);
    if(asset.practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None) {
        const bool running=asset.practicePhase==SalvageLocalSessionState::AssetPreview::PracticePhase::Running;
        hud.title=running?(asset.pause==Pause::Paused?"Test paused":"Testing your boat"):"Preparing test transition";
        hud.objective="practice";hud.selected="Free test — no rewards or saving";
        hud.status=running?"Return restores your original workshop":"Wait for the protected replacement";
        hud.tone=running?Tone::Neutral:Tone::Waiting;
        hud.hints={running?binding(Action::Pause)+": Menu / Return":"Controls wait for completion",
            "Quitting discards this test","Original expedition stays unchanged"};
        return finish();
    }
    if(asset.pause!=Pause::Running){
        render::CovePauseFacts facts;
        facts.paused=asset.pause==Pause::Paused;facts.storageRevoked=local.storageRevoked;
        facts.checkpointPending=asset.checkpointPending;
        facts.harborPending=asset.harbor&&asset.harbor->installationPending();
        facts.rescuePending=asset.rescue!=SalvageLocalSessionState::AssetPreview::Rescue::None;
        facts.cargoBanked=asset.cargoBanked;facts.deliveryDurable=asset.deliveryDurable;
        facts.harborPowered=asset.harbor&&asset.harbor->durable()&&asset.harbor->installed();
        facts.saveStatus=salvageSaveStatus_;
        if(asset.harbor&&asset.harbor->stage()==game::expedition::CoveHarborRuntime::Stage::Absent)
            facts.harborRefusal=asset.harbor->message();
        render::applyCovePauseGuidance(facts,hud);
        boundText(hud.status);boundText(hud.selected);for(auto& hint:hud.hints)boundText(hint);
        if(asset.workshopOpen&&!facts.storageRevoked&&!facts.checkpointPending&&!facts.harborPending&&!facts.rescuePending) {
            hud.title="Workshop paused";hud.objective="workshop-paused";hud.selected="Your draft is kept";
            hud.status=asset.practiceReturns?"Test discarded; original boat restored":"Resume to edit your boat";
            hud.hints={binding(Action::Pause)+": Menu / Resume","Save a design in workshop tools",""};
        }
        return finish();
    }
    if(asset.workshopOpen&&asset.workshop){
        const auto& workshop=*asset.workshop;
        hud.title="Brick workshop";hud.objective="workshop";
        hud.selected=std::string(workshop.brickToolActive()?workshop.catalogName():workshop.selectedName());
        if(workshop.canPaint()) {
            const auto index=workshop.paintIndex();
            hud.selected+=" / "+std::string(index?game::expedition::kBrickPaintPalette[*index].name:"Custom");
        }
        const auto quote=game::expedition::quoteCoveDesign(workshop.design(),*asset.boat,*local.catalog,owned.storedParts);
        hud.economy+=quote?"  Launch "+std::to_string(quote->debit.salvageMaterial):"  Cost unavailable";
        if(quote&&quote->credit.salvageMaterial)hud.economy+="  Return "+std::to_string(quote->credit.salvageMaterial);
        if(local.session->hasPending()||asset.launch||asset.checkpointPending){
            hud.status="Applying boat changes";hud.tone=Tone::Waiting;
        }else if(!workshop.valid()){
            hud.status="Blocked: "+workshop.message();hud.tone=Tone::Blocked;
        }else if(asset.workshopPointerPlacement&&!asset.workshopPointerTarget){
            hud.status="Point at a brick or deck";hud.tone=Tone::Waiting;
        }else if(quote&&!quote->affordable(owned.inventory)){
            hud.status="Not enough material to launch";hud.tone=Tone::Blocked;
        }else{
            hud.status=workshop.brickToolActive()?"Ready to place"
                :workshop.changed()?"Connected - keep your edit":"Design connected";
            hud.tone=Tone::Ready;
        }
        hud.hints={workshop.brickToolActive()?binding(Action::Keep)+": Place  "+binding(Action::RotateY)+": Rotate"
                :binding(Action::BrickOne)+" / "+binding(Action::BrickTwo)+" / "+binding(Action::BrickThree)+": Bricks",
            (workshop.canPaint()?binding(Action::Paint)+": Color  ":"")+binding(Action::Keep)+": Keep  "+binding(Action::Undo)+": Undo",
            binding(Action::Launch)+": Launch  "+binding(Action::ToolsMenu)+": Tools"};
        return finish(true);
    }
    using Player=game::expedition::CovePlayer;
    switch(asset.player->interaction()){
        case Player::Interaction::Board:hud.selected=binding(Action::Interact)+": Board boat";break;
        case Player::Interaction::ReturnToDock:hud.selected=binding(Action::Interact)+": Return to dock";break;
        case Player::Interaction::UseHelm:hud.selected=binding(Action::Interact)+": Use helm";break;
        case Player::Interaction::LeaveHelm:hud.selected=binding(Action::Interact)+": Leave helm";break;
        case Player::Interaction::None:hud.selected="Walk beside the boat";break;
    }
    if(asset.player->mode()==Player::Mode::Swimming)hud.selected="Swimming — "+binding(Action::Rescue)+": Rescue";
    render::CoveRecoveryFacts facts;
    const auto job=std::find_if(owned.jobs.begin(),owned.jobs.end(),[&](const auto& item){return item.id==local.jobId;});
    const auto cargo=std::find_if(owned.cargo.begin(),owned.cargo.end(),[&](const auto& item){return item.id==local.cargoId;});
    if(job!=owned.jobs.end()) {
        using Job=game::expedition::JobPhase;
        using FactJob=render::CoveRecoveryFacts::Job;
        facts.job=job->phase==Job::Available?FactJob::Available:job->phase==Job::Accepted?FactJob::Accepted:FactJob::Completed;
    }
    // Common outer and action 50/51/52 guards. Per-action readiness below is
    // deliberately sampled without submitting any intent or opening a ticket.
    facts.commandsReady=!(coveResume_&&!coveResume_->ready)&&!local.storageRevoked
        &&!asset.checkpointPending&&asset.rescue==SalvageLocalSessionState::AssetPreview::Rescue::None
        &&!(asset.harbor&&asset.harbor->installationPending())&&!asset.leaving
        &&!local.session->hasPending()&&!salvagePreview_->busy()&&!local.pendingControl;
    const bool jobCommands=facts.commandsReady&&local.executionBound&&local.session->admissionOpen();
    facts.storageReady=local.storageHostReady;
    facts.canAccept=jobCommands&&facts.job==render::CoveRecoveryFacts::Job::Available;
    facts.deliveryPending=asset.checkpointPending;
    facts.deliveryDurable=asset.deliveryDurable;facts.cargoBanked=asset.cargoBanked;
    facts.onBoat=asset.player->onBoat();
    facts.cargoObserved=asset.cargo&&asset.cargoBody.valid()&&!asset.cargoBanked&&!asset.cargoSecuring&&asset.cargoObservedTick!=0;
    if(jobCommands&&facts.storageReady&&facts.job==render::CoveRecoveryFacts::Job::Accepted
        &&cargo!=owned.cargo.end()&&local.preparation.eligible)
        facts.canDeliver=local.preparation.eligible({*cargo,*job,*job,{}});
    if(asset.content->registry.navigation&&asset.content->registry.navigation->delivery){
        const auto& zone=*asset.content->registry.navigation->delivery;
        const auto position=physics::worldPositionToAbsolute(asset.cargoObserved.position)-asset.origin;
        facts.harborDistance=std::hypot(position.x-zone.center.x,position.z-zone.center.z);
        facts.harborLimit=zone.radius-1.1; // Same current cargo allowance as eligible().
        facts.height=position.y;facts.minimumHeight=zone.minimumHeight;
        facts.speed=double(glm::length(asset.cargoObserved.originVelocity));facts.maximumSpeed=zone.maximumSpeed;
        facts.spin=double(glm::length(asset.cargoObserved.angularVelocity));facts.maximumSpin=zone.maximumAngularSpeed;
    }
    facts.hasWinch=asset.towReelSpeed>0;
    facts.onWinchRoot=facts.onBoat&&asset.player->state().root==asset.towRoot().key;
    facts.towAttached=asset.towRope.valid()&&!asset.towBroken;
    facts.towConfirmed=asset.boatEventsThrough>=asset.towChangedTick
        &&asset.towObservedTick>=asset.towChangedTick&&asset.towObserved.handle==asset.towRope;
    facts.towReady=facts.commandsReady&&facts.hasWinch&&facts.cargoObserved&&facts.onWinchRoot
        &&asset.towRoot().body.valid()&&asset.towRoot().observedTick&&!asset.towAction
        &&asset.status==render::SalvageFixtureStatus::Active;
    if(facts.cargoObserved&&asset.towRoot().observedTick){
        const auto a=physics::worldPositionToAbsolute(asset.towRoot().observed.position)
            +glm::dvec3(asset.towRoot().observed.orientation*asset.towBoatPoint);
        const auto b=physics::worldPositionToAbsolute(asset.cargoObserved.position)
            +glm::dvec3(asset.cargoObserved.orientation*asset.towCargoPoint);
        facts.hookDistance=glm::length(b-a);
    }
    if(asset.harbor){
        const auto& harbor=*asset.harbor;
        facts.harborPresent=true;facts.harborInstalled=harbor.installed();facts.harborDurable=harbor.durable();
        facts.harborPending=harbor.installationPending();
        facts.atDock=!facts.onBoat&&glm::length(asset.player->feet()-asset.content->registry.navigation->dockBoarding)<=4;
        const bool harborCommands=facts.commandsReady&&!asset.launch&&asset.deliveryDurable&&asset.boatRoot().body.valid()&&facts.atDock;
        facts.canInstall=harborCommands&&facts.storageReady&&harbor.stage()==game::expedition::CoveHarborRuntime::Stage::Absent&&!harbor.installed();
        facts.canUseLift=harborCommands&&harbor.durable()&&!harbor.busy()&&harbor.rig()
            &&harbor.stage()==game::expedition::CoveHarborRuntime::Stage::Ready;
    }
    const auto guidance=render::coveRecoveryGuidance(facts);
    hud.title=guidance.title;hud.status=guidance.status;hud.tone=guidance.tone;hud.objective=guidance.step;
    boundText(hud.status);std::string controls(guidance.controls);boundText(controls);
    const auto movement=controller?std::string("Left stick")
        :binding(Action::MoveForward)+"/"+binding(Action::MoveBack)+"/"+binding(Action::MoveLeft)+"/"+binding(Action::MoveRight);
    hud.hints[0]=movement+(asset.player->mode()==Player::Mode::Helm?": Drive / steer":": Walk  "+binding(Action::Jump)+": Jump");
    hud.hints[1]=controls;
    hud.hints[2]=binding(Action::Workshop)+": Build  "+binding(Action::CameraMenu)+": Camera  "+binding(Action::Pause)+": Menu";
    if(facts.deliveryPending||facts.harborPending)hud.hints={controls,"Controls wait for hand-off",""};
    return finish(true);
}

bool Application::renderNativeCoveHud(WGPUCommandEncoder encoder,WGPUTextureView target) {
    if(nativeCoveHud_)nativeCoveHud_->clearEncodedObservation();
    const auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
    if(!asset||!asset->player||asset->leaving||!asset->boat||!salvageLocalSession_->session
        ||salvagePreviewFailed_||!salvagePreview_||salvagePreview_->phase()==game::expedition::SalvagePreview::Phase::Empty)
        return true;
    if(!nativeCoveHud_){
        nativeCoveHud_=std::make_unique<render::CoveHudPath>();
        if(!nativeCoveHud_->init(gpuContext_->getDevice(),gpuContext_->getQueue(),config_.colorFormat,
            config_.shaderDir/"cove_hud.wgsl"))return false;
    }
    // Status is a read-only sample; bound the snapshot/quote work to 10 Hz.
    // Overlay geometry uploads only when text, colors or viewport actually change.
    const bool interactive=nativeWorkshopMenu_&&nativeWorkshopMenu_->active();
    if(interactive||nativeCoveHud_->content().menu||nativeCoveHud_->needsContentUpdate()) {
        auto content=nativeCoveHudContent();
        content.rightAligned=!asset->workshopOpen;
        nativeCoveHud_->setContent(std::move(content));
    }
    return nativeCoveHud_->render(encoder,target,gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight());
}
#endif

bool Application::coveAssetsDraining() const noexcept {
    const auto* asset = salvageLocalSession_ ? salvageLocalSession_->asset.get() : nullptr;
    const auto* shapeResources=physicsWorld_?physicsWorld_->authoredShapeResources():nullptr;
    const bool boatDraining=asset && asset->boat && asset->leaving
        && (!asset->boatRoots->allRetired() || !asset->sceneryRetired || !asset->environmentRetired
            || (asset->harbor&&asset->harbor->stage()!=game::expedition::CoveHarborRuntime::Stage::Drained)
            || (asset->cargo && !asset->cargoRetired) || (shapeResources && shapeResources->stats().pendingOperations!=0)
            || asset->boatEventsThrough<physicsWorld_->tickFrontier().submitted);
    return asset && asset->leaving
        && (asset->status != render::SalvageFixtureStatus::Drained || boatDraining);
}

std::string Application::salvagePreviewJson() const {
    using Preview = game::expedition::SalvagePreview;
    const auto phase = salvagePreview_ ? salvagePreview_->phase() : Preview::Phase::Empty;
    const bool failed = salvagePreviewFailed_ || phase == Preview::Phase::Failed;
    const auto* asset = salvageLocalSession_ ? salvageLocalSession_->asset.get() : nullptr;
    const bool assetDraining = coveAssetsDraining();
    const bool assetReady = !asset || (!asset->leaving && asset->fixture.stats().active.generation != 0
        && asset->status != render::SalvageFixtureStatus::ViewsValidating);
    std::ostringstream json;
    json << "{\"active\":" << (config_.salvagePreviewEnabled && initialized_ && (phase != Preview::Phase::Empty || assetDraining) ? "true" : "false")
         << ",\"ready\":" << (phase == Preview::Phase::Ready && assetReady && !failed && (!coveResume_ || coveResume_->ready) ? "true" : "false")
         << ",\"busy\":" << ((salvagePreview_ && salvagePreview_->busy()) || assetDraining || (asset && !asset->leaving && !assetReady) ? "true" : "false")
         << ",\"failed\":" << (failed ? "true" : "false")
         << ",\"bodies\":" << (salvagePreview_ ? salvagePreview_->bodyCount() : 0u)
         << ",\"resets\":" << (salvagePreview_ ? salvagePreview_->resetCount() : 0u)
         << ",\"controller\":\"" << (asset && asset->player ? "cove-player" : controllerModeToString(controllerMode_)) << "\""
         << ",\"terrainSurface\":\"" << (config_.legoTerrainEnabled ? "lego" : "triangles") << "\""
         << ",\"mouseCaptured\":" << (input_ && input_->isMouseCaptured() ? "true" : "false")
         << ",\"origin\":";
    if (salvagePreview_) {
        const auto origin = salvagePreview_->origin();
        json << '[' << origin.x << ',' << origin.y << ',' << origin.z << ']';
    } else json << "null";
#if defined(VOXY_NATIVE)
    if(nativeCoveHud_){
        const auto& hud=nativeCoveHud_->layout();
        json<<",\"nativeHud\":{\"enabled\":"<<(nativeCoveHud_->initialized()?"true":"false")
            <<",\"lastEncodedQuads\":"<<nativeCoveHud_->lastEncodedQuads()
            <<",\"uploads\":"<<nativeCoveHud_->uploadCount()<<",\"bodyPixels\":"<<hud.bodyPixels
            <<",\"truncated\":"<<(hud.truncated?"true":"false")
            <<",\"panel\":["<<hud.panel.x<<','<<hud.panel.y<<','<<hud.panel.z<<','<<hud.panel.w<<']';
        const auto& content=nativeCoveHud_->content();
        const auto string=[&](std::string_view value){
            json<<'"';
            constexpr char hex[]="0123456789abcdef";
            for(const char byte:value){
                const auto c=static_cast<unsigned char>(byte);
                if(c=='"'||c=='\\')json<<'\\'<<static_cast<char>(c);
                else if(c<32)json<<"\\u00"<<hex[c>>4]<<hex[c&15];
                else json<<static_cast<char>(c);
            }
            json<<'"';
        };
        json<<",\"objective\":";string(content.objective);
        json<<",\"title\":";string(content.title);
        json<<",\"selected\":";string(content.selected);
        json<<",\"status\":";string(content.status);
        json<<",\"hints\":[";
        for(size_t i=0;i<content.hints.size();++i){if(i)json<<',';string(content.hints[i]);}
        json<<"]}";
    }
#endif
    if(salvageLocalSession_ && salvageLocalSession_->saveContext){
        constexpr std::string_view hex="0123456789abcdef";
        json<<",\"world\":\"";
        for(auto byte:salvageLocalSession_->saveContext->identity.world.bytes)json<<hex[byte>>4]<<hex[byte&15];
        json<<'"';
    }
    if(coveResume_)json<<",\"restore\":{\"phase\":\""<<(coveResume_->ready?"ready":coveResume_->awaitingCommit?"awaiting-storage":"restoring")
        <<"\",\"baseTick\":\""<<coveResume_->tick<<"\"}";
    json << ",\"camera\":";
    if (camera_) {
        const auto local = camera_->position();
        const auto sector = camera_->worldSector();
        json << "{\"local\":[" << local.x << ',' << local.y << ',' << local.z
             << "],\"sector\":[" << sector.x << ',' << sector.y << ',' << sector.z
             << "],\"yaw\":" << camera_->yaw() << ",\"pitch\":" << camera_->pitch();
        const auto matrix=camera_->projectionMatrix()*camera_->viewMatrix();json<<",\"viewProjection\":[";
        for(int column=0;column<4;++column)for(int row=0;row<4;++row){if(column||row)json<<',';json<<matrix[column][row];}
        json<<"]}";
    } else json << "null";
    if (asset && asset->player) {
        const auto& player = *asset->player;
        const auto feet = player.feet();
        json << ",\"player\":{\"mode\":\"" << game::expedition::CovePlayer::modeName(player.mode())
             << "\",\"interaction\":\"" << game::expedition::CovePlayer::interactionName(player.interaction())
             << "\",\"feet\":[" << feet.x << ',' << feet.y << ',' << feet.z
             << "],\"tick\":\"" << player.tick() << "\",\"interactions\":\"" << player.interactions()
             << "\",\"collisionBoxes\":" << player.collisionBoxes()
             << ",\"rootKey\":\"" << player.state().root.counter << '"'
             << ",\"onBoat\":" << (player.onBoat() ? "true" : "false") << '}';
        using Pause=SalvageLocalSessionState::AssetPreview::Pause;
        using Rescue=SalvageLocalSessionState::AssetPreview::Rescue;
        const auto cutTarget=asset->cutTarget();
        const bool cutPending=salvageLocalSession_->launchRequest.has_value();
        const bool canCut=cutTarget&&salvageLocalSession_->storageHostReady&&salvageLocalSession_->executionBound
            &&salvageLocalSession_->session->admissionOpen()&&!salvageLocalSession_->session->hasPending()
            &&!asset->launch&&!salvageLocalSession_->pendingControl&&asset->status==render::SalvageFixtureStatus::Active;
        json<<",\"cutter\":{\"canCut\":"<<(canCut?"true":"false")<<",\"pending\":"<<(cutPending?"true":"false")
            <<",\"weld\":\""<<(cutTarget?cutTarget->weld.counter:0)<<"\",\"label\":\""<<(cutTarget?asset->cutLabel(*cutTarget):"")
            <<"\",\"sections\":"<<asset->boat->roots().size()<<",\"cutWelds\":"
            <<std::count_if(asset->boat->build().connections.begin(),asset->boat->build().connections.end(),[](const auto& link){return !link.enabled;})<<'}';
        const char* rescuePhase=asset->rescue==Rescue::None?"idle":asset->rescue==Rescue::Requested?"stopping"
            :asset->rescue==Rescue::Releasing?"releasing":asset->rescue==Rescue::Moving?"returning":"saving";
        json<<",\"rescue\":{\"phase\":\""<<rescuePhase<<"\",\"pending\":"<<(asset->rescue!=Rescue::None?"true":"false")
            <<",\"completed\":\""<<asset->rescues<<"\",\"savePending\":"<<(asset->rescue==Rescue::Saving?"true":"false")<<'}';
        const char* phaseName=asset->pause==Pause::Running?"running":asset->pause==Pause::Requested?"requested"
            :asset->pause==Pause::Draining?"draining":"paused";
        const bool canPause=asset->pause==Pause::Running && !asset->workshopOpen && !asset->launch
            && !asset->cargoSecuring && !asset->leaving && !asset->towAction
            && asset->rescue==Rescue::None && !asset->checkpointPending
            && salvageLocalSession_->executionBound && !salvageLocalSession_->session->hasPending()
            && !salvageLocalSession_->pendingControl && asset->status==render::SalvageFixtureStatus::Active;
        json<<",\"pause\":{\"phase\":\""<<phaseName<<"\",\"canPause\":"<<(canPause?"true":"false")
            <<",\"tick\":\""<<asset->pauseTick<<"\",\"waterTick\":\""<<asset->waterClock.tick()<<"\",\"waterTime\":"<<asset->waterTime<<'}';
    }
    if(asset && asset->player) {
        const bool canOpen=asset->practicePhase==SalvageLocalSessionState::AssetPreview::PracticePhase::None&&!asset->player->onBoat()
            &&(!asset->harbor||asset->harbor->state().mode==game::expedition::CoveHarborLiftMode::Detached)
            && glm::length(asset->player->feet()-asset->content->registry.navigation->spawn)<=3;
#if defined(VOXY_NATIVE)
        if(nativeWorkshopMenu_) {
            const auto string=[&](std::string_view value){
                json<<'"';for(char byte:value) {
                    const auto c=static_cast<unsigned char>(byte);
                    if(c=='"'||c=='\\')json<<'\\'<<static_cast<char>(c);
                    else if(c<32)json<<' ';else json<<static_cast<char>(c);
                }json<<'"';
            };
            const auto& menu=*nativeWorkshopMenu_;const auto& view=menu.menuContent();const auto& library=menu.library();
            json<<",\"nativeMenu\":{\"open\":"<<(menu.active()?"true":"false")<<",\"page\":";string(menu.pageName());
            json<<",\"title\":";string(view.title);json<<",\"selected\":"<<view.selected<<",\"rows\":[";
            for(size_t i=0;i<view.rows.size();++i){if(i)json<<',';json<<"{\"label\":";string(view.rows[i].label);json<<",\"enabled\":"<<(view.rows[i].enabled?"true":"false")<<'}';}
            json<<"],\"naming\":"<<(view.naming?"true":"false")<<",\"name\":";string(view.name);
            json<<",\"key\":"<<view.key<<",\"keyboardFocus\":"<<(view.keyboardFocus?"true":"false")
                <<",\"library\":{\"ready\":"<<(library.ready()?"true":"false")<<",\"busy\":"<<(library.busy()?"true":"false")
                <<",\"generation\":\""<<library.generation()<<"\",\"root\":";string(library.root().string());
            json<<",\"selectedId\":\""<<library.selectedId()<<"\",\"rows\":[";
            for(size_t i=0;i<library.rows().size();++i){const auto& row=library.rows()[i];if(i)json<<',';
                json<<"{\"id\":\""<<row.id<<"\",\"revision\":\""<<row.revision<<"\",\"name\":";string(row.name);
                json<<",\"backupAvailable\":"<<(row.backupAvailable?"true":"false")<<'}';}
            json<<"],\"imports\":[";for(size_t i=0;i<library.imports().size();++i){if(i)json<<',';string(library.imports()[i]);}
            json<<"],\"lastPath\":";string(library.lastPath().string());json<<",\"message\":";string(library.message());json<<"}}";
        }
#endif
        const auto cameraSettings=asset->characterCamera.settings();
        const auto& pose=asset->characterCamera.pose();
        const bool cameraAvailable=!asset->workshopOpen&&!asset->leaving&&!asset->launch&&!asset->checkpointPending
            &&!salvageLocalSession_->storageRevoked&&!salvageLocalSession_->launchRequest&&!salvageLocalSession_->pendingControl
            &&asset->status==render::SalvageFixtureStatus::Active&&!salvagePreview_->busy()
            &&asset->rescue==SalvageLocalSessionState::AssetPreview::Rescue::None
            &&!(asset->harbor&&asset->harbor->installationPending());
        json<<",\"characterCamera\":{\"available\":"<<(cameraAvailable?"true":"false")
            <<",\"mode\":\""<<(cameraSettings.mode==game::expedition::CoveCamera::Mode::Chase?"chase":"orbit")
            <<"\",\"distance\":"<<asset->characterCamera.userDistance()<<",\"actualDistance\":"<<pose.distance
            <<",\"reducedMotion\":"<<(cameraSettings.reducedMotion?"true":"false")
            <<",\"frameLoad\":"<<(cameraSettings.frameLoad?"true":"false")
            <<",\"valid\":"<<(asset->characterViewReady?"true":"false")
            <<",\"held\":"<<(!asset->characterViewReady&&asset->characterPresentation.valid?"true":"false")
            <<",\"geometryTick\":\""<<pose.geometryTick<<"\",\"presentedTick\":\""<<asset->characterPresentation.tick
            <<"\",\"yaw\":"<<pose.yaw<<",\"elevation\":"<<pose.elevation
            <<",\"eye\":["<<pose.eye.x<<','<<pose.eye.y<<','<<pose.eye.z<<"]}";
        const auto velocity=asset->player->worldVelocity();
        json<<",\"character\":{\"clip\":\""<<asset->character.name()<<"\",\"time\":"<<asset->character.time()
            <<",\"blend\":"<<asset->character.blend()<<",\"facingYaw\":"<<asset->player->facingYaw()
            <<",\"worldVelocity\":["<<velocity.x<<','<<velocity.y<<','<<velocity.z<<']'
            <<",\"draws\":"<<asset->fixture.stats().lastSubmittedRobotDraws<<'}';
        if(coveUi_){
            const auto& ui=*coveUi_;const auto& p=ui.preferences;
            const auto card=ui.tutorial.card(input_&&input_->gamepad().connected(),asset->pause==SalvageLocalSessionState::AssetPreview::Pause::Paused,asset->workshopOpen);
            bool tutorialEnabled=assetReady&&!failed&&!salvageLocalSession_->storageRevoked&&!asset->checkpointPending;
            if(card.action==340)tutorialEnabled=covePracticeCanBegin();
            if(card.action==350)tutorialEnabled=tutorialEnabled&&!asset->workshopOpen
                &&asset->practicePhase==SalvageLocalSessionState::AssetPreview::PracticePhase::None;
            nlohmann::json presentation={{"textScale",p.textScale},{"highContrast",p.highContrast},
                {"captionsEnabled",p.captionsEnabled},{"tutorialsEnabled",p.tutorialsEnabled},
                {"preferenceMessage",ui.preferenceStatus},{"locale",p.locale},{"caption",p.captionsEnabled?ui.caption:std::string{}},
                {"tutorial",{{"key",std::string(card.key)},{"title",std::string(card.title)},{"text",std::string(card.text)},
                    {"actionLabel",std::string(card.actionLabel)},{"action",card.action},{"step",card.number},{"number",card.number},
                    {"total",card.total},{"complete",card.complete},{"enabled",tutorialEnabled}}}};
            presentation["actions"]=nlohmann::json::array();
            for(const auto& action:game::expedition::coveActionList()){
                const auto& binding=p.bindings[static_cast<size_t>(action.action)];
                presentation["actions"].push_back({{"id",std::string(action.id)},{"label",std::string(action.label)},
                    {"contexts",action.contexts},{"keyLabel",game::expedition::coveKeyLabel(binding.key,binding.modifiers)},
                    {"padLabel",game::expedition::covePadLabel(binding.pad)}});
            }
            json<<",\"ui\":"<<presentation.dump();
            nlohmann::json places=nlohmann::json::array();
            const auto addPlace=[&](const char* id,const char* label,glm::dvec3 position){
                places.push_back({{"id",id},{"label",label},{"position",{position.x,position.y,position.z}}});
            };
            if(asset->content->registry.navigation){
                const auto& navigation=*asset->content->registry.navigation;
                addPlace("dock","Boarding dock",navigation.dockBoarding);
                if(navigation.delivery)addPlace("harbor","Delivery harbor",navigation.delivery->center);
            }
            if(asset->boatRoot().observedTick)addPlace("boat","Your boat",physics::worldPositionToAbsolute(asset->boatRoot().observed.position)-asset->origin);
            if(asset->cargoObservedTick&&!asset->cargoBanked)addPlace("generator","Sunken generator",physics::worldPositionToAbsolute(asset->cargoObserved.position)-asset->origin);
            json<<",\"landmarks\":"<<places.dump();
        }
        if(input_)json<<",\"gamepad\":{\"connected\":"<<(input_->gamepad().connected()?"true":"false")
            <<",\"armed\":"<<(input_->gamepad().armed()?"true":"false")
            <<",\"menuOwner\":"<<(coveUiOwnsInput()?"true":"false")<<'}';
        {
            using Phase=SalvageLocalSessionState::AssetPreview::PracticePhase;
            const char* practicePhaseName="none";
            switch(asset->practicePhase) {
                case Phase::None:break;
                case Phase::Capturing:practicePhaseName="capturing";break;
                case Phase::Entering:practicePhaseName="entering";break;
                case Phase::Running:practicePhaseName="running";break;
                case Phase::Returning:practicePhaseName="returning";break;
            }
            json<<",\"practice\":{\"active\":"<<(asset->practicePhase!=Phase::None?"true":"false")
                <<",\"phase\":\""<<practicePhaseName<<"\",\"canBegin\":"<<(covePracticeCanBegin()?"true":"false")
                <<",\"canReturn\":"<<(covePracticeCanReturn()?"true":"false")<<",\"message\":\"";
            for(const char byte:asset->practiceMessage) {
                const auto c=static_cast<unsigned char>(byte);
                if(c=='"'||c=='\\')json<<'\\'<<static_cast<char>(c);
                else if(c<32)json<<' ';else json<<static_cast<char>(c);
            }
            json<<"\",\"runs\":\""<<asset->practiceRuns<<"\",\"returns\":\""<<asset->practiceReturns<<"\"}";
        }
        json<<",\"workshop\":{\"open\":"<<(asset->workshopOpen?"true":"false")
            <<",\"canOpen\":"<<(canOpen?"true":"false")
            <<",\"displayOrigin\":["<<asset->origin.x<<','<<asset->origin.y+4<<','<<asset->origin.z<<']';
        const auto& workshopCamera=asset->workshopCamera;const auto viewTarget=workshopCamera.target();
        const auto visible=workshopCamera.rectangle();
        json<<",\"camera\":{\"target\":["<<viewTarget.x<<','<<viewTarget.y<<','<<viewTarget.z
            <<"],\"distance\":"<<workshopCamera.distance()<<",\"yaw\":"<<workshopCamera.yaw()
            <<",\"elevation\":"<<workshopCamera.elevation()<<",\"framePending\":"<<(asset->workshopFrameRequest?"true":"false")
            <<",\"rectangle\":["<<visible.x<<','<<visible.y<<','<<visible.z<<','<<visible.w<<"]}";
        if(asset->workshop) {
            const auto& w=*asset->workshop;const auto p=w.preview().registry.placements[w.selected()].placement;
            const auto settings=w.selectedSettings();
            json<<",\"configurable\":"<<(w.configurable()?"true":"false")
                <<",\"hasOutputLimit\":"<<(w.hasOutputLimit()?"true":"false")<<",\"canReverse\":"<<(w.canReverse()?"true":"false")
                <<",\"settings\":{\"enabled\":"<<(settings.enabled?"true":"false")<<",\"limitPercent\":"<<settings.limitPermille/10
                <<",\"reversed\":"<<(settings.reversed?"true":"false")<<'}';
            json<<",\"selected\":"<<w.selected()<<",\"name\":\""<<w.selectedName()<<"\",\"valid\":"<<(w.valid()?"true":"false")
                <<",\"changed\":"<<(w.changed()?"true":"false")<<",\"undo\":"<<w.undoCount()<<",\"revision\":\""<<w.revision()
                <<"\",\"massKg\":"<<w.massKg()<<",\"parts\":"<<w.preview().registry.navigation->boatPlacements.size()
                <<",\"placement\":["<<p.translation.x<<','<<p.translation.y<<','<<p.translation.z<<']'
                <<",\"rotation\":"<<static_cast<unsigned>(p.rotation.value)<<",\"message\":\""<<w.message()<<'"';
            json<<",\"selectedCount\":"<<w.selectedParts().size()<<",\"selectedParts\":[";
            for(size_t i=0;i<w.selectedParts().size();++i){if(i)json<<',';json<<w.selectedParts()[i];}
            json<<"],\"redoCount\":"<<w.redoCount()<<",\"problemCode\":"<<static_cast<unsigned>(w.placementIssue().problem)
                <<",\"problemPlacement\":";
            if(w.placementIssue().placement)json<<*w.placementIssue().placement;else json<<"null";
        }
        const auto history=salvageLocalSession_->session->history();
        const bool pending=salvageLocalSession_->session->hasPending() || bool(asset->launch)
            || asset->practicePhase!=SalvageLocalSessionState::AssetPreview::PracticePhase::None;
        const bool kept=asset->workshop && (!asset->workshop->changed() || asset->workshop->brickToolActive());
        const bool same=asset->workshop && asset->workshop->matchesDesign(asset->acceptedScene());
        const auto owned=salvageLocalSession_->session->snapshot();const auto stock=owned.inventory;
        const auto quote=asset->workshop?game::expedition::quoteCoveDesign(asset->workshop->design(),*asset->boat,*salvageLocalSession_->catalog,owned.storedParts)
            :std::optional<game::expedition::CoveDesignCost>{};
        const bool affordable=quote && quote->affordable(stock);
        if(asset->workshop) {
            const auto* stored=game::expedition::availableCoveStoredPart(asset->workshop->design(),*asset->boat,owned.storedParts,asset->workshop->catalogDefinition());
            const auto price=stored?game::construction::ResourceAmounts{}:asset->workshop->catalogCost();
            json<<",\"catalogName\":\""<<asset->workshop->catalogName()<<"\",\"catalogIndex\":"<<asset->workshop->catalogIndex()
                <<",\"catalogCount\":"<<asset->workshop->catalogCount()<<",\"canAdd\":"<<(!pending && asset->workshop->canAdd()?"true":"false")
                <<",\"partCost\":\""<<price.salvageMaterial<<"\",\"partMachinery\":\""<<price.specialMachinery<<'"';
        }
        if(asset->workshop) {
            const auto paint=asset->workshop->currentPaint();const auto paintIndex=asset->workshop->paintIndex();
            json<<",\"canPaint\":"<<(!pending&&asset->workshop->canPaint()?"true":"false")<<",\"paintIndex\":";
            if(paintIndex)json<<*paintIndex;else json<<"null";
            json<<",\"paintName\":\""<<(paintIndex?game::expedition::kBrickPaintPalette[*paintIndex].name:"Custom")
                <<"\",\"paint\":["<<unsigned(paint[0])<<','<<unsigned(paint[1])<<','<<unsigned(paint[2])<<','<<unsigned(paint[3])<<']'
                <<",\"brushPaint\":";
            if(const auto preference=asset->workshop->brushPaint())json<<'['<<unsigned((*preference)[0])<<','<<unsigned((*preference)[1])<<','<<unsigned((*preference)[2])<<','<<unsigned((*preference)[3])<<']';
            else json<<"null";
            json<<",\"catalog\":[";
            for(uint32_t i=0;i<asset->workshop->catalogCount();++i) {
                if(i)json<<',';
                const auto price=asset->workshop->catalogCostAt(i);
                json<<"{\"index\":"<<i<<",\"name\":\""<<asset->workshop->catalogNameAt(i)
                    <<"\",\"cost\":\""<<price.salvageMaterial<<"\"}";
            }
            size_t placedBricks=0;
            for(const auto slot:asset->workshop->design().registry.navigation->boatPlacements) {
                const auto& placed=asset->workshop->design().registry.placements[slot];
                if(!placed.prototype&&asset->workshop->design().bundles[placed.bundleIndex]->sidecar().part.nameKey.starts_with("salvage.part.brick_"))++placedBricks;
            }
            json<<"],\"brickTool\":"<<(asset->workshop->brickToolActive()?"true":"false")
                <<",\"canChooseBrick\":"<<(!pending&&asset->workshop->canChooseBrick()?"true":"false")
                <<",\"placedParts\":"<<asset->workshop->design().registry.navigation->boatPlacements.size()
                <<",\"placedBricks\":"<<placedBricks
                <<",\"pointerPlacement\":"<<(asset->workshopPointerPlacement?"true":"false")
                <<",\"pointerTarget\":"<<(asset->workshopPointerTarget?"true":"false");
        }
        json<<",\"canLoadBlueprint\":"<<(!pending&&asset->workshopOpen&&asset->workshop
            &&!asset->workshop->changed()&&!asset->workshop->brickToolActive()?"true":"false");
        json<<",\"storedParts\":"<<owned.storedParts.size()<<",\"savePending\":"<<(asset->workshopSavePending?"true":"false")
            <<",\"canRebuild\":"<<(!pending&&kept&&same&&salvageLocalSession_->storageHostReady&&!asset->towRope.valid()?"true":"false");
        json<<",\"recoveryDesigns\":"<<asset->recoveryDesigns.size()<<",\"recoverySelected\":"<<asset->selectedRecoveryDesign
            <<",\"canLoadRecovery\":"<<(!pending&&kept&&!asset->recoveryDesigns.empty()?"true":"false")
            <<",\"canRemoveRecovery\":"<<(!pending&&kept&&same&&!asset->recoveryDesigns.empty()
                &&std::none_of(asset->boat->build().connections.begin(),asset->boat->build().connections.end(),[](const auto& link){return !link.enabled;})
                &&salvageLocalSession_->storageHostReady&&salvageLocalSession_->executionBound?"true":"false")
            <<",\"recoveryDigests\":[";
        for(size_t i=0;i<asset->recoveryDesigns.size();++i){if(i)json<<',';json<<'"'<<core::sha256Hex(core::sha256(asset->recoveryDesigns[i]))<<'"';}json<<']';
        json<<",\"storedPartIds\":[";bool firstStored=true;
        for(const auto& part:owned.storedParts){if(!firstStored)json<<',';firstStored=false;json<<'"'<<part.id.counter<<'"';}json<<']';
        json<<",\"materials\":\""<<stock.salvageMaterial<<"\",\"machinery\":\""<<stock.specialMachinery<<'"'
            <<",\"affordable\":"<<(affordable?"true":"false");
        if(quote)json<<",\"charge\":\""<<quote->debit.salvageMaterial<<"\",\"refund\":\""<<quote->credit.salvageMaterial
            <<"\",\"machineryCharge\":\""<<quote->debit.specialMachinery<<"\",\"machineryRefund\":\""<<quote->credit.specialMachinery<<'"';
        json<<",\"pending\":"<<(pending?"true":"false")
            <<",\"canLaunch\":"<<(!pending && kept && !same && affordable?"true":"false")
            <<",\"canUndoLaunch\":"<<(!pending && kept && same && history.undo && history.undo->target.build==salvageLocalSession_->boatId?"true":"false")
            <<",\"canRedoLaunch\":"<<(!pending && kept && same && history.redo && history.redo->target.build==salvageLocalSession_->boatId?"true":"false")
            <<",\"launches\":"<<asset->launchCount<<",\"lastLaunchTick\":"<<asset->lastLaunchTick
            <<",\"launchMessage\":\"";
        for(const char character:asset->launchMessage) {
            const auto c=static_cast<unsigned char>(character);
            if(c=='"' || c=='\\')json<<'\\'<<static_cast<char>(c);
            else if(c<32)json<<' ';
            else json<<static_cast<char>(c);
        }
        json<<"\"}";
    }
    if (asset && asset->boat) {
        const auto ticks=physicsWorld_->tickFrontier();
        const auto p=physics::worldPositionToAbsolute(asset->boatRoot().observed.position)-asset->origin;
        const auto helm=asset->player->helmPoint();
        const auto q=asset->boatRoot().observed.orientation;
        json << ",\"boat\":{\"prepared\":true,\"active\":" << (asset->boatRoot().body.valid()?"true":"false")
             << ",\"observedTick\":" << asset->boatRoot().observedTick
             << ",\"position\":[" << p.x << ',' << p.y << ',' << p.z << ']'
             << ",\"speed\":" << glm::length(asset->boatRoot().observed.originVelocity)
             << ",\"orientation\":[" << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']'
             << ",\"helmPosition\":[" << helm.x << ',' << helm.y << ',' << helm.z << ']'
             << ",\"buildId\":\"" << asset->boat->build().id.counter << "\",\"topologyRevision\":\""
             << asset->boat->build().revision.value() << "\",\"parts\":" << asset->boat->parts().size()
             << ",\"eventsThrough\":\"" << asset->boatEventsThrough << "\",\"attachmentBreaks\":\"" << asset->boatAttachmentBreaks << '\"'
             << ",\"physicsTicks\":{\"supported\":" << (ticks.supported?"true":"false")
             << ",\"failed\":" << (ticks.failed?"true":"false")
             << ",\"incarnation\":\"" << ticks.incarnation << "\",\"base\":\"" << ticks.baseTick
             << "\",\"scheduled\":\"" << ticks.scheduled << "\",\"encoded\":\"" << ticks.encoded
             << "\",\"submitted\":\"" << ticks.submitted << "\",\"completed\":\"" << ticks.completed
             << "\",\"maximumInFlight\":" << ticks.maximumInFlightTicks << '}'
             << ",\"sceneryCollision\":" << (asset->sceneryBody.valid()?"true":"false")
             << ",\"sceneryProxies\":" << (asset->scenery?asset->scenery->sources().size():0)
             << ",\"environmentCollision\":" << (asset->environmentObserved?"true":"false")
             << ",\"environmentProxies\":" << asset->environmentObstacleCount
             << ",\"thrustLimitNewtons\":"<<asset->thrustLimit<<",\"steeringLimitRadians\":"<<asset->steeringLimit
             << ",\"thrustDirection\":["<<asset->thrustDirection.x<<','<<asset->thrustDirection.y<<','<<asset->thrustDirection.z<<']'
             << std::setprecision(17) << ",\"mechanisms\":{\"tick\":\""<<asset->mechanisms.tick()<<"\",\"incarnation\":\""<<asset->mechanisms.incarnation()
             <<"\",\"rotorRadians\":"<<asset->mechanisms.rotorRadians()<<",\"drumRadians\":"<<asset->mechanisms.drumRadians()
             <<",\"effectiveDrive\":"<<asset->mechanisms.effectiveDrive()<<",\"animatedParts\":"<<asset->mechanismPlacements
             <<",\"bodyIndex\":"<<asset->boatRoot().body.index<<",\"bodyGeneration\":"<<asset->boatRoot().body.generation
             <<",\"ropeTick\":\""<<(asset->mechanisms.ropeSample()?asset->mechanisms.ropeSample()->tick:0)
             <<"\",\"ropeIndex\":"<<(asset->mechanisms.ropeSample()?asset->mechanisms.ropeSample()->handle.index:0)
             <<",\"ropeGeneration\":"<<(asset->mechanisms.ropeSample()?asset->mechanisms.ropeSample()->handle.generation:0)
             <<",\"ropeLength\":"<<(asset->mechanisms.ropeSample()?asset->mechanisms.ropeSample()->length:0)<<'}'<<std::setprecision(6)
             <<",\"effects\":{\"epoch\":\""<<asset->effects.stats().epoch<<"\",\"tick\":\""<<asset->effects.stats().tick
             <<"\",\"active\":"<<asset->effects.stats().active<<",\"highWater\":"<<asset->effects.stats().highWater
             <<",\"emitted\":\""<<asset->effects.stats().emitted<<"\",\"dropped\":\""<<asset->effects.stats().dropped
             <<"\",\"rejected\":\""<<asset->effects.stats().rejected<<"\",\"skippedContacts\":\""<<asset->effectsSkippedContacts
             <<"\",\"wake\":\""<<asset->effects.stats().emittedByKind[0]<<"\",\"foam\":\""<<asset->effects.stats().emittedByKind[1]
             <<"\",\"splash\":\""<<asset->effects.stats().emittedByKind[2]<<"\",\"runoff\":\""<<asset->effects.stats().emittedByKind[3]
             <<"\",\"playerEntrySplashes\":\""<<asset->effects.stats().playerEntrySplashes
             <<"\",\"dust\":\""<<asset->effects.stats().emittedByKind[4]<<"\",\"encoded\":"<<asset->effectsPath.lastEncodedInstances()
             <<",\"ownerBytes\":"<<(asset->effectsPath.initialized()?render::CoveEffectsPath::residentBytes:0)<<'}'
             << ",\"massKg\":" << asset->boat->massKg()
             << ",\"displacementM3\":" << asset->boat->displacementCubicMetres()
             << ",\"paidPartIds\":[";
        bool firstPaid=true;
        for(const auto& part:asset->boat->build().parts)if(part.provenance.origin==game::construction::PartOrigin::Paid) {
            if(!firstPaid)json<<',';
            firstPaid=false;json<<'"'<<part.id.counter<<'"';
        }
        json<<"],\"rootCount\":"<<asset->boatRoots->roots().size()<<",\"controlPart\":\""
            <<(asset->boat->primaryRoot().helm?asset->boat->primaryRoot().helm->counter:0)
            <<"\",\"joinedTick\":\""<<asset->boatRoots->joinedTick()<<"\",\"roots\":[";
        bool firstRoot=true;
        for(const auto& root:asset->boatRoots->roots()) {
            if(!firstRoot)json<<',';
            firstRoot=false;
            const auto position=physics::worldPositionToAbsolute(root.observed.position)-asset->origin;
            json<<"{\"key\":\""<<root.key.counter<<"\",\"active\":"<<(root.body.valid()?"true":"false")
                <<",\"observedTick\":\""<<root.observedTick<<"\",\"position\":["<<position.x<<','<<position.y<<','<<position.z<<']'
                <<",\"orientation\":["<<root.observed.orientation.x<<','<<root.observed.orientation.y<<','<<root.observed.orientation.z<<','<<root.observed.orientation.w<<']'
                <<",\"velocity\":["<<root.observed.originVelocity.x<<','<<root.observed.originVelocity.y<<','<<root.observed.originVelocity.z<<']'
                <<",\"angularVelocity\":["<<root.observed.angularVelocity.x<<','<<root.observed.angularVelocity.y<<','<<root.observed.angularVelocity.z<<"]}";
        }
        json<<"]}";
    }
    if(asset && asset->cargo) {
        const auto p=physics::worldPositionToAbsolute(asset->cargoObserved.position)-asset->origin;
        const auto a=physics::worldPositionToAbsolute(asset->towRoot().observed.position)
            +glm::dvec3(asset->towRoot().observed.orientation*asset->towBoatPoint);
        const auto b=physics::worldPositionToAbsolute(asset->cargoObserved.position)
            +glm::dvec3(asset->cargoObserved.orientation*asset->towCargoPoint);
        const auto distance=glm::length(b-a);
        const bool operable=asset->towReelSpeed>0 && asset->player->onBoat()
            && asset->player->state().root==asset->towRoot().key && asset->cargoObservedTick && asset->towRoot().observedTick
            && !asset->cargoBanked && !asset->cargoSecuring && !salvageLocalSession_->session->hasPending();
        json << ",\"tow\":{\"name\":\"Salvage generator\",\"active\":" << (asset->cargoBody.valid()?"true":"false")
            << ",\"attached\":" << (asset->towRope.valid() && !asset->towBroken?"true":"false")
            << ",\"broken\":" << (asset->towBroken?"true":"false")
            << ",\"hasWinch\":" << (asset->towReelSpeed>0?"true":"false")
            << ",\"rootKey\":\"" << asset->towRoot().key.counter << '"'
            << ",\"operable\":" << (operable?"true":"false")
            << ",\"inRange\":" << (distance<=8?"true":"false")
            << ",\"confirmed\":" << (asset->boatEventsThrough>=asset->towChangedTick
                && (!asset->towRope.valid() || asset->towObservedTick>=asset->towChangedTick)?"true":"false")
            << ",\"motor\":" << asset->towMotor << ",\"distance\":" << distance
            << ",\"ropeObservedTick\":\"" << asset->towObservedTick << '"'
            << ",\"ropeLength\":";
        if(asset->towRope.valid() && asset->towObservedTick>=asset->towChangedTick)
            json << asset->towObserved.distance.targetLength;
        else json << "null";
        json
            << ",\"observedTick\":" << asset->cargoObservedTick
            << ",\"position\":[" << p.x << ',' << p.y << ',' << p.z << ']'
            << ",\"speed\":" << glm::length(asset->cargoObserved.originVelocity)
            << ",\"massKg\":" << asset->cargo->primaryMassRoot().mass.dryMassKg << '}';
    }
    if(salvageLocalSession_ && salvageLocalSession_->jobId.counter) {
        const auto& local=*salvageLocalSession_;const auto state=local.session->snapshot();
        const auto job=std::find_if(state.jobs.begin(),state.jobs.end(),[&](const auto& j){return j.id==local.jobId;});
        if(job!=state.jobs.end()) {
            bool deliverable=false;
            if(local.storageHostReady && !local.asset->checkpointPending
                && job->phase==game::expedition::JobPhase::Accepted && !state.cargo.empty())
                deliverable=local.preparation.eligible({state.cargo[0],*job,*job,{}});
            const auto& zone=*local.asset->content->registry.navigation->delivery;
            const auto p=physics::worldPositionToAbsolute(local.asset->cargoObserved.position)-local.asset->origin;
            json << ",\"job\":{\"phase\":\"" << (job->phase==game::expedition::JobPhase::Available?"available"
                :job->phase==game::expedition::JobPhase::Accepted?"accepted":"completed")
                << "\",\"pending\":" << (local.session->hasPending()?"true":"false")
                << ",\"canDeliver\":" << (deliverable?"true":"false")
                << ",\"harborDistance\":" << std::hypot(p.x-zone.center.x,p.z-zone.center.z)
                << ",\"secured\":" << (local.asset->cargoBanked?"true":"false")
                << ",\"savePending\":" << (local.asset->checkpointPending?"true":"false")
                << ",\"durable\":" << (local.asset->deliveryDurable?"true":"false")
                << ",\"message\":\"" << local.asset->jobMessage << "\"}";
        }
    }
    if(asset&&asset->harbor){
        const auto& lift=*asset->harbor;const auto state=lift.state();
        const bool atDock=asset->player&&!asset->player->onBoat()
            &&glm::length(asset->player->feet()-asset->content->registry.navigation->dockBoarding)<=4;
        const bool available=atDock&&asset->deliveryDurable&&!asset->checkpointPending&&!asset->workshopOpen
            &&!asset->leaving&&!asset->launch&&!salvagePreview_->busy()
            &&asset->pause==SalvageLocalSessionState::AssetPreview::Pause::Running;
        const bool ready=available&&lift.durable()&&!lift.busy()&&!lift.installationPending();
        std::string attachmentIssue;
        const bool canAttach=ready&&lift.rig()&&!lift.attachmentPending()
            &&state.mode==game::expedition::CoveHarborLiftMode::Detached;
        if(ready&&lift.rig()&&state.mode==game::expedition::CoveHarborLiftMode::Detached)
            (void)lift.rig()->attach(asset->boatRoot().observed,lift.motion(),asset->boatRoot().body,lift.body(),attachmentIssue);
        json << ",\"harbor\":{\"installed\":"<<(lift.installed()?"true":"false")
            <<",\"durable\":"<<(lift.durable()?"true":"false")
            <<",\"pending\":"<<(lift.installationPending()?"true":"false")
            <<",\"atDock\":"<<(atDock?"true":"false")
            <<",\"compatible\":"<<(lift.rig()?"true":"false")
            <<",\"attached\":"<<(state.mode!=game::expedition::CoveHarborLiftMode::Detached?"true":"false")
            <<",\"brokenMask\":"<<unsigned(state.brokenMask)
            <<",\"canInstall\":"<<(available&&!lift.installed()&&!lift.busy()&&salvageLocalSession_->storageHostReady?"true":"false")
            <<",\"canAttach\":"<<(canAttach?"true":"false")
            <<",\"attachmentPending\":"<<(lift.attachmentPending()?"true":"false")
            <<",\"canOperate\":"<<(ready&&state.mode==game::expedition::CoveHarborLiftMode::Attached?"true":"false")
            <<",\"canRelease\":"<<(available&&lift.durable()&&(lift.attachmentPending()||state.mode!=game::expedition::CoveHarborLiftMode::Detached)?"true":"false")
            <<",\"motor\":"<<lift.motor()<<",\"bodyObservedTick\":\""<<lift.observedTick()
            <<"\",\"stage\":"<<static_cast<unsigned>(lift.stage())
            <<",\"message\":\""<<lift.message()<<"\",\"attachmentIssue\":\""<<attachmentIssue
            <<"\",\"rigIssue\":\""<<lift.rigIssue()<<"\",\"lengths\":[";
        for(size_t i=0;i<state.lengths.size();++i){if(i)json<<',';json<<state.lengths[i];}
        json<<"],\"lineLoads\":[";
        for(size_t i=0;i<lift.observedRopes().size();++i){
            if(i)json<<',';
            const auto& rope=lift.observedRopes()[i];
            if(!rope.handle.valid()){json<<"null";continue;}
            json<<"{\"forceNewtons\":"<<rope.requiredForce<<",\"impulse\":"<<rope.requiredImpulse
                <<",\"distance\":"<<rope.measuredDistance<<",\"breakTick\":\""<<rope.breakTick<<"\"}";
        }
        json<<"]}";
    }
    json << ",\"session\":";
    if (salvageLocalSession_ && salvageLocalSession_->session) {
        const auto& session = *salvageLocalSession_->session;
        const auto state = session.snapshot();
        json << "{\"revision\":\"" << state.revision.value()
             << "\",\"tick\":\"" << state.tick.value()
             << "\",\"admissionOpen\":" << (session.admissionOpen() ? "true" : "false")
             << ",\"builds\":" << state.builds.size()
             << ",\"buildParts\":" << (state.builds.empty()?0:state.builds[0].parts.size())
             << ",\"buildConnections\":" << (state.builds.empty()?0:state.builds[0].connections.size())
             << ",\"cargo\":" << state.cargo.size() << ",\"jobs\":" << state.jobs.size()
             << ",\"inventory\":{\"salvageMaterial\":\"" << state.inventory.salvageMaterial
             << "\",\"specialMachinery\":\"" << state.inventory.specialMachinery << "\"}}";
    } else json << "null";
    if (salvageLocalSession_ && salvageLocalSession_->session) {
        // Public diagnostics only. Polling does not advance a reader cursor or
        // authority time, and no admission token is included in this JSON.
        const auto reader = salvageLocalSession_->session->events();
        const auto identity = reader.identity();
        const auto hex = [](const std::array<uint8_t, 16>& bytes) {
            constexpr char digits[] = "0123456789abcdef";
            std::string value; value.reserve(32);
            for (uint8_t byte : bytes) { value.push_back(digits[byte >> 4]); value.push_back(digits[byte & 15]); }
            return value;
        };
        json << ",\"observation\":{\"world\":\"" << hex(identity.world.bytes)
             << "\",\"incarnation\":\"" << hex(identity.incarnation.bytes)
             << "\",\"epoch\":\"" << identity.epoch.value()
             << "\",\"publicationLost\":" << (salvageLocalSession_->session->observationDiagnostics().publicationLost ? "true" : "false")
             << ",\"lanes\":[";
        constexpr std::array<game::expedition::EventLane, 3> lanes{game::expedition::EventLane::Domain,
            game::expedition::EventLane::Presentation, game::expedition::EventLane::Telemetry};
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (i != 0) json << ',';
            const auto stats = *reader.stats(lanes[i]);
            json << "{\"through\":\"" << stats.publishedThrough.value() << "\",\"retained\":" << stats.occupancy
                 << ",\"overwritten\":\"" << stats.overwritten.value << "\",\"invalid\":\"" << stats.invalidInput.value
                 << "\",\"exhausted\":" << (stats.exhausted ? "true" : "false") << '}';
        }
        json << "]}";
    }
    if (asset) {
        const auto stats = asset->fixture.stats();
        size_t presentationParts=0;
        for(size_t i=0;i<asset->content->presentationBundles.size();++i)
            presentationParts+=asset->content->presentationBundles[i]!=asset->content->bundles[i];
        json << ",\"assetFixture\":{\"status\":" << static_cast<int>(asset->status)
             << ",\"presentationParts\":" << presentationParts
             << ",\"sceneSunShadows\":" << (blitPath_ && blitPath_->didUseSceneSunShadows() ? "true" : "false")
             << ",\"generation\":\"" << stats.active.generation << "\",\"uploads\":" << stats.active.uniqueUploads
             << ",\"prototypeUploads\":" << stats.active.prototypeUploads
             << ",\"dockMarkingGpuBytes\":\"" << stats.active.dockMarkingGpuBytes << '"'
             << ",\"dockMarkingDraws\":" << stats.lastSubmittedDockMarkingDraws
             << ",\"sceneryDraws\":" << stats.lastSubmittedSceneryDraws
             << ",\"sceneryGpuBytes\":" << stats.active.sceneryGpuBytes
             << ",\"externalGpuReserve\":" << stats.active.reservedExternalGpuBytes
             << ",\"environmentGpuBytes\":\"" << stats.active.environmentGpuBytes
             << "\",\"environmentBakeCount\":" << stats.active.environmentBakeCount
             << ",\"environmentReady\":" << (stats.active.environmentReady ? "true" : "false")
             << ",\"gpuReservationBytes\":\"" << stats.active.reservedGpuBytes
             << "\",\"submittedSerial\":\"" << stats.active.lastSubmittedSerial
             << "\",\"completedSerial\":\"" << stats.active.lastCompletedSerial
             << "\",\"draws\":" << stats.lastSubmittedDraws
             << ",\"guides\":" << static_cast<int>(asset->guides)
             << ",\"opaqueSceneGpuBytes\":\"" << (blitPath_ ? blitPath_->opaqueSceneBytes() : 0) << '"'
             << ",\"guideBoxes\":" << stats.lastEncodedGuideBoxes
             << ",\"guideMeshGpuBytes\":" << (stats.active.generation ? render::inspectionGuideGpuBytes : 0)
             << ",\"forcedLod\":\"" << asset->forcedLod << "\",\"lods\":[";
        for (size_t i = 0; i < asset->selectedLods.size(); ++i) {
            if (i != 0) json << ',';
            if (asset->acceptedScene().registry.placements[i].prototype) json << "null";
            else json << '"' << asset->selectedLods[i] << '"';
        }
        json << "]";
        // A manual level is usable only when every installed cooked bundle
        // supplies that exact ID. Mixed fixtures may include a one-LOD stand.
        json << ",\"availableLods\":[\"0\"";
        for (uint64_t id = 1; id <= 3; ++id) {
            const bool available = std::all_of(asset->content->bundles.begin(), asset->content->bundles.end(),
                [id](const auto& bundle) {
                    return std::any_of(bundle->lods().begin(), bundle->lods().end(),
                        [id](const auto& lod) { return lod.id == id; });
                });
            if (available) json << ",\"" << id << '"';
        }
        json << ']';
        if (asset->content->assembly) json << ",\"assembly\":{\"parts\":"
            << asset->content->assembly->snapshot().parts.size() << ",\"connections\":"
            << asset->content->assembly->snapshot().connections.size() << '}';
        json << '}';
    }
    json << '}';
    return json.str();
}

void Application::updateSalvagePreview(float frameDeltaTime) {
    if (!salvagePreview_ || salvagePreviewFailed_) return;
    using Preview = game::expedition::SalvagePreview;
    const auto previousPhase = salvagePreview_->phase();
    const auto previousResets = salvagePreview_->resetCount();
    auto* asset = salvageLocalSession_ ? salvageLocalSession_->asset.get() : nullptr;
    if (asset) {
        using Pause=SalvageLocalSessionState::AssetPreview::Pause;
        if(asset->pause==Pause::Requested || asset->pause==Pause::Draining) {
            asset->pauseWaitSeconds+=std::min(double(frameDeltaTime),.25);
            if(asset->pauseWaitSeconds>10) {
                LOG_ERROR("Cove pause could not obtain complete physics evidence");
                salvagePreviewFailed_=true;requestExit();return;
            }
        }
#if defined(VOXY_WASM)
        // This property belongs to the separately compiled main page. Brackets
        // preserve its name under the WASM build's Closure optimization.
        if (EM_ASM_INT({ return globalThis['voxyDeviceLost'] ? 1 : 0; })) asset->deviceLost->store(true);
#endif
        if (asset->deviceLost->load(std::memory_order_acquire)) asset->fixture.notifyDeviceLost();
        if (asset->boat && !updateCoveBoat()) { salvagePreviewFailed_=true; requestExit(); return; }
        asset->status = asset->fixture.poll();
        std::string error;
        if (asset->viewsDirty && !asset->leaving && asset->status != render::SalvageFixtureStatus::ViewsValidating
            && asset->status != render::SalvageFixtureStatus::Fatal) {
            if (!asset->fixture.setSceneViews(blitPath_->getEnvironmentTextureView(),
                config_.salvageAssetFixtureWaterAnchor ? raycastPath_->getTerrainDepthCacheView()
                                                       : raycastPath_->getDepthOutputView(), error)) {
                LOG_ERROR("Asset fixture view update failed: {}", error);
                asset->fixture.notifyDeviceLost();
            } else asset->viewsDirty = false;
            asset->status = asset->fixture.poll();
        }
        if (asset->status == render::SalvageFixtureStatus::CandidateReady) {
            if (!asset->fixture.publishCandidate(error)) {
                LOG_ERROR("Asset fixture publication failed: {}", error);
                asset->fixture.notifyDeviceLost();
            }
            asset->status = asset->fixture.poll();
        }
        if (asset->status == render::SalvageFixtureStatus::Fatal
            || asset->status == render::SalvageFixtureStatus::CandidateRejected) {
            LOG_ERROR("Asset fixture failed: {}", asset->fixture.lastError());
            salvageLocalSession_->session->closeAdmission();
            salvagePreviewFailed_ = true;
            requestExit();
            return;
        }
    }
    if (salvageLocalSession_ && salvageLocalSession_->pendingControl
        && !salvageLocalSession_->session->executionInFlight()
        && (!asset || !asset->launch || !asset->launch->staged || asset->launch->published)
        && (!asset || *salvageLocalSession_->pendingControl == Preview::Action::Leave
            || asset->status != render::SalvageFixtureStatus::ViewsValidating)) {
        const auto action = *salvageLocalSession_->pendingControl;
        salvageLocalSession_->pendingControl.reset();
        if(asset && asset->pause!=SalvageLocalSessionState::AssetPreview::Pause::Running) {
            if(!physicsWorld_->setSchedulingPaused(false)) {salvagePreviewFailed_=true;requestExit();return;}
            asset->pause=SalvageLocalSessionState::AssetPreview::Pause::Running;asset->pauseTick=0;
        }
        if (action == Preview::Action::Leave) salvageLocalSession_->session->closeAdmission();
        if (asset) {
            std::string error;
            const bool accepted = action == Preview::Action::Leave
                ? asset->fixture.requestLeave(error) : asset->fixture.resetInstances(error);
            if (!accepted) {
                LOG_ERROR("Asset fixture control failed: {}", error);
                salvagePreviewFailed_ = true;
                requestExit();
                return;
            }
            if (action == Preview::Action::Leave) {
                asset->leaving = true;
                if(asset->harbor&&!asset->harbor->close(*physicsWorld_)){salvagePreviewFailed_=true;requestExit();return;}
                if(asset->towRope.valid()) {
                    if(!physicsWorld_->destroyAttachment(asset->towRope)) {salvagePreviewFailed_=true;requestExit();return;}
                    asset->towRope={};
                }
                if(asset->cargoBody.valid() && !physicsWorld_->destroyBody(asset->cargoBody)) {
                    LOG_ERROR("Could not remove the salvage load");salvagePreviewFailed_=true;requestExit();return;
                }
                if(asset->boatRoots)for(const auto& root:asset->boatRoots->roots())
                    if(root.body.valid() && !physicsWorld_->destroyBody(root.body)) {
                        LOG_ERROR("Could not remove a physical boat section");salvagePreviewFailed_=true;requestExit();return;
                    }
                if (asset->sceneryBody.valid() && !physicsWorld_->destroyBody(asset->sceneryBody)) {
                    LOG_ERROR("Could not remove cove scenery collision"); salvagePreviewFailed_=true; requestExit(); return;
                }
                if (asset->environmentBody.valid()) {
                    if(!physicsWorld_->destroyBody(asset->environmentBody)) {
                        LOG_ERROR("Could not remove harbor environment collision"); salvagePreviewFailed_=true; requestExit(); return;
                    }
                    asset->environmentRemovalTick=physicsWorld_->tickFrontier().scheduled+1;
                }
            }
            else { asset->forcedLod = 0; asset->guides = render::InspectionGuides::Off; }
            asset->status = asset->fixture.poll();
        }
        // Dispatch before observing retirement so Leave can supersede a Reset
        // even on the frame whose completed metadata would otherwise respawn it.
        if (!salvagePreview_->request(action)) {
            LOG_ERROR("Cove session control could not be dispatched; closing preview.");
            salvageLocalSession_->session->closeAdmission();
            salvagePreviewFailed_ = true;
            requestExit();
            return;
        }
    }
    while (auto completed = salvageRetirementReadback_.poll()) {
        salvagePreview_->observeRetirement(completed->tick, completed->bytes);
    }
    salvagePreview_->update();
    if (salvagePreview_->busy() || (salvageLocalSession_ && salvageLocalSession_->session->executionInFlight())
        || coveAssetsDraining()) {
        if (previousPhase == Preview::Phase::Ready && input_) input_->resetState();
        salvageRetirementSeconds_ += frameDeltaTime;
        if (salvageRetirementSeconds_ > 10.0f) {
            // A full command queue or failed mapping must not strand Reset.
            LOG_ERROR("Cove removal timed out; closing its physics world safely.");
            if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
            salvagePreviewFailed_ = true;
            requestExit();
        }
    } else {
        salvageRetirementSeconds_ = 0;
    }
    if (salvagePreview_->phase() == Preview::Phase::Failed) {
        LOG_ERROR("Cove preview failed: {}", salvagePreview_->error());
        if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
        salvagePreviewFailed_ = true;
        requestExit();
    }
    if (salvagePreview_->resetCount() != previousResets) resetSalvagePreviewView();
    if (previousPhase != Preview::Phase::Empty && salvagePreview_->phase() == Preview::Phase::Empty) {
        if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
        salvageRetirementReadback_.shutdown();
        salvageMetadataSource_.clear();
        salvageRetirementBytes_ = 0;
        if (input_) input_->resetState();
        if (preSalvageCamera_ && camera_) {
            const auto saved = *preSalvageCamera_;
            camera_->setWorldPosition(saved.sector, saved.position);
            camera_->setYaw(saved.yaw);
            camera_->setPitch(saved.pitch);
            setControllerMode(preSalvageController_);
            if (characterController_) characterController_->syncPhysicsPosition();
        }
        preSalvageCamera_.reset();
    }
    // Logical session retirement can precede the rendered scene and authored
    // physics owners. Keep pumping frames until the same complete drain used
    // by the host handoff and browser Leave has finished.
    if(coveUi_&&coveUi_->exitAfterLeave&&!salvagePreviewFailed_
        &&salvagePreview_->phase()==Preview::Phase::Empty&&!coveAssetsDraining())requestExit();
}

void Application::encodeSalvageRetirement(WGPUCommandEncoder encoder) {
    if (!salvagePreview_ || !physicsWorld_ || !gpuContext_ || salvagePreviewFailed_) return;
    const auto request = salvagePreview_->retirementSnapshot();
    if (!request) return;
    const auto metadataBuffer = salvageMetadataSource_.readableBuffer(request->bodyCount);
    if (!metadataBuffer) {
        LOG_ERROR("Cove removal has no valid GPU lifetime metadata; closing preview.");
        if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
        salvagePreviewFailed_ = true;
        requestExit();
        return;
    }
    const size_t bytes = size_t{request->bodyCount} * sizeof(glm::uvec4);
    if (salvageRetirementBytes_ < bytes) {
        salvageRetirementReadback_.shutdown();
        if (!salvageRetirementReadback_.initialize(gpuContext_->getDevice(), 1u, bytes)) {
            LOG_ERROR("Could not allocate cove removal readback; closing preview.");
            if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
            salvagePreviewFailed_ = true;
            requestExit();
            return;
        }
        salvageRetirementBytes_ = bytes;
    }
    if (salvageRetirementReadback_.nextAvailableSlot()) {
        if (!salvageRetirementReadback_.encodeCopy(encoder, metadataBuffer, 0u,
                bytes, request->revision, 0u, request->bodyCount)) {
            LOG_ERROR("Could not encode cove removal readback; closing preview.");
            if (salvageLocalSession_) salvageLocalSession_->session->closeAdmission();
            salvagePreviewFailed_ = true;
            requestExit();
        }
    }
}

std::function<double(double,double,double)> Application::coveGroundSupport(glm::dvec3 origin) const {
    if(!config_.legoTerrainEnabled||!heightmap_)return {};
    const terrain::lego::Surface surface{heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),config_.heightScale,config_.cellScale};
    return [surface,origin](double x,double z,double radius){
        return double(terrain::lego::supportHeight(surface,glm::vec2(float(x+origin.x),float(z+origin.z)),float(radius)))-origin.y;
    };
}

void Application::updateCoveCharacterView(float deltaTime) {
    auto* asset=salvageLocalSession_?salvageLocalSession_->asset.get():nullptr;
    if(!asset||!asset->player||!asset->robot||!camera_||!heightmap_||asset->leaving)return;
    using Rig=game::expedition::CoveCamera;
    using Player=game::expedition::CovePlayer;
    auto& player=*asset->player;
    if(asset->terrainBoundPlayer!=&player) {
        const terrain::lego::Surface surface{heightmap_->getData(),heightmap_->getWidth(),heightmap_->getHeight(),config_.heightScale,config_.cellScale};
        const auto origin=asset->origin;const bool lego=config_.legoTerrainEnabled;
        player.setTerrainSweep([surface,origin,lego](glm::dvec3 a,glm::dvec3 b,double radius){
            const auto hit=game::expedition::sweepCoveTerrainSphere(surface,a+origin,b+origin,radius,lego);
            return Player::SweepResult{hit.complete,hit.hit,hit.startOverlapped,hit.distance,hit.normal};
        });
        if(lego)player.setGroundSupport([surface,origin](double x,double z,double radius){
            return double(terrain::lego::supportHeight(surface,glm::vec2(float(x+origin.x),float(z+origin.z)),float(radius)))-origin.y;
        });
        if(!asset->character.restoreMode(player.mode(),player.state().verticalSpeed)) {
            LOG_ERROR("Cove character could not restore its movement pose");salvagePreviewFailed_=true;requestExit();return;
        }
        asset->terrainBoundPlayer=&player;asset->playerObservedTick=player.collisionTick();
        asset->characterDiscontinuity=true;
    }
    if(asset->workshopOpen)return;
    asset->characterViewReady=false;
    const auto hold=[&]{if(asset->characterPresentation.valid)*camera_=asset->characterPresentation.camera;};
    const auto tick=player.collisionTick();
    if(asset->cargo&&asset->cargoObservedTick!=tick){hold();return;}
    for(const auto& root:asset->boatRoots->roots())if(root.observedTick!=tick){hold();return;}
    camera_->setAspectRatio(gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight());
    Rig::Target target;
    target.anchor=player.feet()+glm::dvec3(0,1.2,0);target.facingYaw=player.facingYaw();target.geometryTick=tick;
    target.discontinuity=asset->characterDiscontinuity;
    target.chaseActive=asset->character.clip()==game::expedition::CoveCharacter::Clip::Walk||player.mode()==Player::Mode::Helm;
    if(asset->cargo&&!asset->cargoBanked&&asset->towRope.valid()&&!asset->towBroken) {
        const auto bounds=asset->cargo->shape().rootBounds();
        Rig::Bounds load{glm::dvec3(std::numeric_limits<double>::infinity()),glm::dvec3(-std::numeric_limits<double>::infinity())};
        for(unsigned corner=0;corner<8;++corner) {
            const glm::dvec3 p=glm::dvec3((corner&1)?bounds.maximum.x:bounds.minimum.x,
                (corner&2)?bounds.maximum.y:bounds.minimum.y,(corner&4)?bounds.maximum.z:bounds.minimum.z)*.02;
            const auto world=physics::worldPositionToAbsolute(asset->cargoObserved.position)-asset->origin
                +glm::dquat(asset->cargoObserved.orientation)*p;
            load.minimum=glm::min(load.minimum,world);load.maximum=glm::max(load.maximum,world);
        }
        target.load=load;
    }
    const Rig::Sweep sweep{&player,[](const void* context,glm::dvec3 a,glm::dvec3 b,double radius,uint64_t expected) noexcept {
        const auto hit=static_cast<const Player*>(context)->sweepSphere(a,b,radius,expected);
        return Rig::SweepResult{hit.complete,hit.hit,hit.startOverlapped,hit.distance,hit.normal};
    }};
    auto input=asset->characterCameraInput;asset->characterCameraInput={};
    input.active=input_&&input_->focused()&&!asset->menuConsumedFrame&&!asset->controllerMenuOwner;
    const auto status=asset->characterCamera.update(target,input,{camera_->fovY(),camera_->aspectRatio(),camera_->nearPlane()},sweep,double(deltaTime));
    const auto& pose=asset->characterCamera.pose();
    if(status==Rig::Result::InvalidInput||!pose.valid){hold();return;}
    asset->characterViewReady=true;asset->characterDiscontinuity=false;
    setCameraWorldPose(*camera_,asset->origin+pose.eye,asset->origin+pose.viewTarget);
}

void Application::updateCoveOnboarding() {
    if(!coveUi_||!salvageLocalSession_||!salvageLocalSession_->session||!salvageLocalSession_->asset)return;
    auto& ui=*coveUi_;const auto& local=*salvageLocalSession_;const auto& asset=*local.asset;
    if(!asset.player||!asset.boatRoots||asset.leaving)return;
    const auto tick=asset.player->collisionTick(),epoch=local.session->events().identity().epoch.value();
    const auto& player=asset.player->state();const auto boat=physics::worldPositionToAbsolute(asset.boatRoot().observed.position);
    const bool helm=asset.player->mode()==game::expedition::CovePlayer::Mode::Helm;
    if(ui.observedEpoch==epoch&&tick>ui.observedTick&&ui.observedTick&&player.interactions==ui.previousInteractions){
        const auto walked=glm::length(player.feet-ui.previousFeet),sailed=glm::length(boat-ui.previousBoat);
        if(!player.onBoat&&!ui.previousOnBoat&&walked<5)ui.walkedMetres+=walked;
        if(helm&&ui.previousHelm&&sailed<5)ui.sailedMetres+=sailed;
    }
    if(ui.observedEpoch&&ui.observedEpoch!=epoch){ui.caption.clear();ui.captionTick=0;}
    const int mode=static_cast<int>(asset.player->mode());
    const bool tow=asset.towRope.valid()&&!asset.towBroken;
    if(ui.observedEpoch==epoch&&ui.observedTick&&tick>=ui.observedTick){
        const auto caption=[&](const char* text){ui.caption=text;ui.captionTick=tick;};
        if(player.onBoat!=ui.previousOnBoat)caption(player.onBoat?"Stepped aboard.":"Left the boat.");
        if(mode!=ui.previousMode){
            if(asset.player->mode()==game::expedition::CovePlayer::Mode::Swimming)caption("In the water. Swim toward the boat or use Rescue.");
            else if(helm)caption("At the helm. Ready to sail.");
            else if(ui.previousHelm)caption("Left the helm.");
        }
        if(tow!=ui.previousTow)caption(tow?"Cable attached.":"Cable released.");
        if(asset.towBroken&&!ui.previousTowBroken)caption("Cable broke.");
        if(asset.launchCount>ui.previousLaunches)caption("Boat launched.");
        if(asset.practiceRuns>ui.previousTests)caption("Test sail started. No cost or rewards.");
        if(asset.practiceReturns>ui.previousReturns)caption("Test discarded. Workshop restored.");
        if(asset.deliveryDurable&&!ui.previousDelivered)caption("Delivery saved. Material reward received.");
    }
    if(ui.captionTick&&tick>ui.captionTick&&tick-ui.captionTick>480)ui.caption.clear();
    ui.previousMode=mode;ui.previousTow=tow;ui.previousTowBroken=asset.towBroken;
    ui.previousLaunches=asset.launchCount;ui.previousTests=asset.practiceRuns;ui.previousReturns=asset.practiceReturns;
    ui.previousDelivered=asset.deliveryDurable;
    ui.observedEpoch=epoch;ui.observedTick=tick;ui.previousFeet=player.feet;ui.previousBoat=boat;
    ui.previousOnBoat=player.onBoat;ui.previousHelm=helm;ui.previousInteractions=player.interactions;
    ui.tutorial.observe({epoch,tick,ui.walkedMetres,ui.sailedMetres,asset.workshopOpen,
        asset.workshop&&asset.workshop->undoCount()>0,asset.practiceRuns>0,asset.launchCount>0,player.onBoat,helm,ui.saveConfirmed});
}

void Application::updateCovePlayer(float deltaTime) {
    if((coveResume_ && !coveResume_->ready) || salvageLocalSession_->storageRevoked)return;
    auto& asset = *salvageLocalSession_->asset;
    // Input and locomotion share the completed root/cargo packet. Rendering
    // uses those same observed transforms; frame/RAF time cannot glue a jump
    // to a boat or advance the player beyond its available contact geometry.
    const auto observedTick=asset.player->collisionTick();
    double movementSeconds=0;
    if(asset.playerObservedTick&&observedTick>=asset.playerObservedTick)
        movementSeconds=double(std::min<uint64_t>(observedTick-asset.playerObservedTick,15))*game::expedition::CovePlayer::fixedStep;
    asset.playerObservedTick=observedTick;
    if(asset.cargo&&asset.cargoObservedTick==observedTick) {
        const auto bounds=asset.cargo->shape().rootBounds();
        const auto& a=bounds.minimum;const auto& b=bounds.maximum;
        const game::expedition::CovePlayer::SceneObstacle cargo{
            glm::dvec3(a.x,a.y,a.z)*.02,glm::dvec3(b.x,b.y,b.z)*.02,
            glm::translate(glm::dmat4(1),physics::worldPositionToAbsolute(asset.cargoObserved.position)-asset.origin)
                *glm::mat4_cast(glm::normalize(glm::dquat(asset.cargoObserved.orientation)))};
        if(!asset.player->setSceneObstacles(std::span(&cargo,1),observedTick))return;
    } else if(asset.cargo)movementSeconds=0;
    asset.rotorCommandBody={};asset.rotorCommandDrive=0;
    if (!camera_ || asset.leaving || salvageLocalSession_->pendingControl
        || asset.status != render::SalvageFixtureStatus::Active) return;
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    using A=game::expedition::CoveAction;
    using Context=game::expedition::CoveInputContext;
    if(!coveUi_)coveUi_=std::make_unique<CoveUiState>();
    auto& ui=*coveUi_;const auto& prefs=ui.preferences;
    (void)input_->setGamepadDeadzones(static_cast<float>(prefs.moveDeadzone),static_cast<float>(prefs.lookDeadzone));
    [[maybe_unused]] const auto pad=input_->gamepad();
    const bool priorOwner=coveUiOwnsInput();
    ui.input.tick(game::expedition::sampleCoveInput(*input_),priorOwner?Context::Menu
        :asset.workshopOpen&&asset.pause==Pause::Running?Context::Workshop:Context::World,prefs);
    const auto pressed=[&](A action){return ui.input.pressed(action);};
    const auto down=[&](A action){return ui.input.down(action);};
    const auto look=ui.input.look();
#if defined(VOXY_WASM)
    asset.menuConsumedFrame=false;
    const bool before=EM_ASM_INT({return typeof window['voxyControllerMenuActive']==='function'&&window['voxyControllerMenuActive']();})!=0;
    const bool cameraKey=!asset.workshopOpen&&(pressed(A::CameraMenu)||input_->wasKeyPressed(Key::F2));
    const bool pauseKey=pressed(A::Pause)||((!asset.workshopOpen||asset.pause!=Pause::Running)
        &&(input_->wasKeyPressed(Key::Escape)||pad.pressed(PadButton::Menu)));
    bool opened=false;
    if(!before&&pauseKey) {
        if(asset.pause!=Pause::Running||salvagePreviewAction(90))
            opened=EM_ASM_INT({return typeof window['voxyCoveMenu']==='function'&&window['voxyCoveMenu']('pause');})!=0;
    }else if(!before&&cameraKey)opened=EM_ASM_INT({
        return typeof window['voxyCoveCameraMenu']==='function'&&window['voxyCoveCameraMenu']();
    })!=0;
    const bool consumed=!opened&&EM_ASM_INT({
        if(typeof window['voxyControllerMenuInput']!=='function')return false;
        return window['voxyControllerMenuInput']({'up':!!$0,'down':!!$1,'left':!!$2,'right':!!$3,
            'confirm':!!$4,'back':!!$5,'menu':!!$6});
    },pad.navigation(0),pad.navigation(1),pad.navigation(2),pad.navigation(3),
        pad.pressed(PadButton::Confirm),pad.pressed(PadButton::Back),
        (before&&pad.pressed(PadButton::Menu))||pressed(A::ToolsMenu)
        ||(asset.workshopOpen&&asset.pause==Pause::Running&&(input_->wasKeyPressed(Key::F2)||pad.pressed(PadButton::Menu))))!=0;
    const bool after=EM_ASM_INT({return typeof window['voxyControllerMenuActive']==='function'&&window['voxyControllerMenuActive']();})!=0;
    if(before!=after||asset.controllerMenuOwner!=after)input_->resetState();
    asset.controllerMenuOwner=after;asset.menuConsumedFrame=opened||consumed||before||after;
#endif
    if(!asset.menuConsumedFrame&&!asset.controllerMenuOwner&&pressed(A::Save))(void)salvagePreviewAction(350);
    if(asset.pause!=Pause::Running) {
        asset.player->discardPendingInput();
#if defined(VOXY_NATIVE)
        const std::string prompt=std::string(asset.checkpointPending?"Cove paused | Progress awaiting save"
            :asset.pause==Pause::Paused?"Cove paused | P: Resume":"Cove | Pausing…")
            +(salvageSaveStatus_.empty()?"":" | "+salvageSaveStatus_);
        if(window_ && asset.playerPrompt!=prompt){asset.playerPrompt=prompt;window_->setTitle(prompt.c_str());}
#endif
        return;
    }
    if(asset.checkpointPending || salvageLocalSession_->launchRequest) {
        asset.player->discardPendingInput();
        // The committed replacement already retires the parent at its target
        // tick. Its handle is retained only for observation/cleanup; sending a
        // helm command to it is invalid. Prepared children start with no input.
        if(asset.launch&&asset.launch->staged&&!asset.launch->published)return;
        if(!asset.boat->primaryRoot().cells.empty()&&!physicsWorld_->setAuthoredHelm(asset.boatRoot().body,0,0)) {
            LOG_ERROR("Cove could not neutralize the current boat while awaiting a saved change");
            salvagePreviewFailed_=true;requestExit();return;
        }
        if(asset.towMotor!=0 && asset.towRope.valid() && !asset.towBroken) {
            if(!physicsWorld_->setAttachmentMotorSpeed(asset.towRope,0)) {
                salvagePreviewFailed_=true;requestExit();return;
            }
            asset.towMotor=0;asset.towChangedTick=physicsWorld_->tickFrontier().scheduled+1;
        }
        return;
    }
    const auto neutralize=[&] {
        asset.player->discardPendingInput();
        if(!asset.boat->primaryRoot().cells.empty()&&asset.boatRoot().body.valid())
            if(!physicsWorld_->setAuthoredHelm(asset.boatRoot().body,0,0)) {
                salvagePreviewFailed_=true;requestExit();
            }
        if(asset.towMotor!=0&&asset.towRope.valid()&&!asset.towBroken) {
            if(!physicsWorld_->setAttachmentMotorSpeed(asset.towRope,0)){salvagePreviewFailed_=true;requestExit();}
            asset.towMotor=0;asset.towChangedTick=physicsWorld_->tickFrontier().scheduled+1;
        }
        if(asset.harbor&&asset.harbor->motor()!=0)
            if(!asset.harbor->stop(*physicsWorld_)){salvagePreviewFailed_=true;requestExit();}
        asset.controllerMotor=false;asset.harborKeyboardMotor=false;
    };
    const auto menuFrame=[&] {
        neutralize();
        if(!asset.workshopOpen)return;
        auto& view=asset.workshopCamera;
        const auto& projection=camera_->projectionMatrix();
        glm::dvec4 rectangle=view.rectangle();
#if defined(VOXY_NATIVE)
        rectangle=render::coveHudWorkshopRectangle(gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight(),coveUi_?static_cast<float>(coveUi_->preferences.textScale):1);
#endif
        if(view.viewport({projection[0][0],projection[1][1]},rectangle)&&asset.workshopFrameRequest) {
            asset.workshopFrameWhole=asset.workshopFrameRequest==2;
            if(const auto bounds=asset.workshop->viewBounds(asset.workshopFrameWhole))(void)view.frame(*bounds);
            asset.workshopFrameRequest=0;
        }
        const auto origin=asset.origin+glm::dvec3(0,4,0);
        setCameraWorldPose(*camera_,origin+view.eye(),origin+view.viewTarget());
    };
    if(!input_->focused()){neutralize();return;}
#if defined(VOXY_NATIVE)
    if(asset.menuConsumedFrame){menuFrame();return;}
#endif
#if defined(VOXY_WASM)
    if(asset.menuConsumedFrame){menuFrame();return;}
#endif
    if(pressed(A::Workshop))
        if(salvagePreviewAction(60)){neutralize();return;}
    if(asset.workshopOpen) {
        bool padEdit=false;
        const std::pair<A,int> actions[]{
            {A::PreviousPart,61},{A::NextPart,62},{A::PartLeft,63},{A::PartRight,64},
            {A::PartForward,65},{A::PartBack,66},{A::PartUp,67},{A::PartDown,68},
            {A::RotateY,69},{A::RotateX,307},{A::RotateZ,308},{A::Snap,70},
            {A::Undo,72},{A::Redo,306},{A::Remove,74},{A::Launch,79},
            {A::UndoLaunch,80},{A::RedoLaunch,81},{A::NextType,83},{A::AddPart,84},
            {A::TogglePart,85},{A::LimitPart,86},{A::ReversePart,87},{A::RebuildStarter,88},
            {A::LoadRecovery,89},{A::NextRecovery,93},{A::RemoveRecovery,94},
            {A::FocusPart,97},{A::WholeBoat,98},{A::SelectAll,300},{A::OnlyPrimary,301},
            {A::Duplicate,302},{A::MirrorX,303},{A::MirrorZ,304},{A::Replace,305},{A::ToggleNext,309}};
        for(const auto& [binding,action]:actions)if(pressed(binding)){
            (void)salvagePreviewAction(action);padEdit=true;
        }
        if(!asset.workshopOpen)return;
        if(pressed(A::Cancel))(void)salvagePreviewAction(asset.workshop->brickToolActive()?96:73);
        if(input_->wasKeyPressed(Key::Escape)){
            (void)salvagePreviewAction(asset.workshop->brickToolActive()?96:60);
            if(!asset.workshopOpen)return;
        }
        if(pressed(A::Keep)) {
            if(asset.workshop->brickToolActive()) {
                const auto state=salvageLocalSession_->session->snapshot();
                const auto* stored=game::expedition::availableCoveStoredPart(asset.workshop->preview(),*asset.boat,
                    state.storedParts,asset.workshop->catalogDefinition());
                if(asset.workshop->placeBrickTool(stored))asset.launchMessage.clear();
            }else (void)salvagePreviewAction(71);
            padEdit=true;
        }
        if(padEdit||std::abs(look[0])>.001||std::abs(look[1])>.001) {
            asset.workshopPointerPlacement=false;asset.workshopPointerTarget=false;
        }
        if(pressed(A::Paint)&&asset.workshop->canPaint()) {
            const auto count=static_cast<uint32_t>(game::expedition::kBrickPaintPalette.size());
            const auto next=(asset.workshop->paintIndex().value_or(count-1)+1)%count;
            (void)salvagePreviewAction(200+static_cast<int>(next));
        }
        auto& view=asset.workshopCamera;
        if(down(A::OrbitLeft))view.orbit(-double(deltaTime),0);
        if(down(A::OrbitRight))view.orbit(double(deltaTime),0);
        if(down(A::ZoomIn))view.zoom(4*double(deltaTime));
        if(down(A::ZoomOut))view.zoom(-4*double(deltaTime));
        for(const auto& [binding,name]:std::array<std::pair<A,std::string_view>,3>{{
            {A::BrickOne,"Brick 1 x 2"},{A::BrickTwo,"Brick 2 x 2"},{A::BrickThree,"Brick 2 x 4"}}})
            if(pressed(binding))for(uint32_t i=0;i<asset.workshop->catalogCount();++i)
                if(asset.workshop->catalogNameAt(i)==name)(void)salvagePreviewAction(100+static_cast<int>(i));
        double pointerWidth=window_?window_->getWidth():0,pointerHeight=window_?window_->getHeight():0;
        glm::dvec4 visible{-.94,-.9,.94,.9};
#if defined(VOXY_NATIVE)
        visible=render::coveHudWorkshopRectangle(gpuContext_->getSwapchainWidth(),gpuContext_->getSwapchainHeight(),coveUi_?static_cast<float>(coveUi_->preferences.textScale):1);
#endif
#if defined(VOXY_WASM)
        // DOM pointer coordinates are CSS pixels, independent of display DPI.
        emscripten_get_element_css_size("#voxy-canvas",&pointerWidth,&pointerHeight);
        visible={-.94,-.9,.94,.9};
        const double panelLeft=EM_ASM_DOUBLE({
            const canvas=document.getElementById('voxy-canvas').getBoundingClientRect();
            const panel=document.getElementById('salvage-preview');
            if(!panel||panel.hidden)return 1;
            return (panel.getBoundingClientRect().left-canvas.left)/canvas.width;
        });
        if(panelLeft<.2) {
            const double panelTop=EM_ASM_DOUBLE({
                const canvas=document.getElementById('voxy-canvas').getBoundingClientRect();
                return (document.getElementById('salvage-preview').getBoundingClientRect().top-canvas.top)/canvas.height;
            });
            visible.y=std::clamp(1-2*panelTop+.04,-.8,.6);
        } else visible.z=std::clamp(2*panelLeft-1-.04,-.6,.94);
#endif
        const auto& projection=camera_->projectionMatrix();const glm::dvec2 scales{projection[0][0],projection[1][1]};
        const bool resized=view.projection()!=scales||view.rectangle()!=visible;
        if(view.viewport(scales,visible)&&(asset.workshopFrameRequest||resized)) {
            if(asset.workshopFrameRequest)asset.workshopFrameWhole=asset.workshopFrameRequest==2;
            const auto bounds=asset.workshop->viewBounds(asset.workshopFrameWhole);
            if(bounds&&!view.frame(*bounds))asset.launchMessage="This design is too wide to fit in one view.";
            asset.workshopFrameRequest=0;
        }
        if(down(A::PanMode))
            view.pan({-look[0]*320*double(deltaTime),look[1]*320*double(deltaTime)},pointerHeight);
        else if(down(A::ZoomMode))view.zoom(-look[1]*4*double(deltaTime));
        else view.orbit(-look[0]*double(deltaTime),look[1]*double(deltaTime));
        const auto gestureButton=[&](MouseButton button){return input_->isMouseButtonDown(button)
            ||input_->wasMouseButtonPressed(button)||input_->wasMouseButtonReleased(button);};
        const bool middle=gestureButton(MouseButton::Middle),orbiting=ui.input.orbitDrag()
            ||(!prefs.orbitToggle&&input_->wasMouseButtonReleased(MouseButton::Right));
        const bool shift=input_->isKeyDown(Key::LeftShift)||input_->isKeyDown(Key::RightShift)
            ||input_->wasKeyReleased(Key::LeftShift)||input_->wasKeyReleased(Key::RightShift);
        const bool panning=middle||(orbiting&&shift);
        const bool gesture=orbiting||panning;
        const auto delta=glm::dvec2(prefs.orbitToggle&&!middle?input_->mouseDelta()
            :input_->mouseDragDelta(middle?MouseButton::Middle:MouseButton::Right))*prefs.mouseSensitivity;
        if(panning)view.pan(delta,pointerHeight);
        else if(orbiting)view.orbit(-delta.x*.006*(prefs.invertX?-1:1),delta.y*.006*(prefs.invertY?-1:1));
        view.zoom(input_->scrollDelta());
        const auto displayOrigin=asset.origin+glm::dvec3(0,4,0);
        setCameraWorldPose(*camera_,displayOrigin+view.eye(),displayOrigin+view.viewTarget());
        const glm::dvec2 pointer(input_->mousePosition());
        asset.workshopPointerTarget=false;
        bool paletteClick=false;
#if defined(VOXY_NATIVE)
        if(!gesture&&input_->wasMouseButtonPressed(MouseButton::Left)&&pointerWidth>0&&pointerHeight>0
            &&pointer.y/pointerHeight>.78&&pointer.y/pointerHeight<.94) {
            uint32_t thumb=0;
            for(uint32_t i=0;i<asset.workshop->catalogCount()&&thumb<3;++i) {
                if(!asset.workshop->catalogNameAt(i).starts_with("Brick "))continue;
                const double x=.32+.18*thumb++;
                if(std::abs(pointer.x/pointerWidth-x)<.08){paletteClick=true;(void)salvagePreviewAction(100+static_cast<int>(i));break;}
            }
        }
#endif
        bool overWorkshopHud=false;
#if defined(VOXY_NATIVE)
        overWorkshopHud=pointerWidth>0&&2*pointer.x/pointerWidth-1<visible.x;
#endif
        if(!gesture&&!paletteClick&&!overWorkshopHud&&pointerWidth>0&&pointerHeight>0&&pointer.x>=0&&pointer.y>=0&&pointer.x<pointerWidth&&pointer.y<pointerHeight) {
            const glm::dvec4 clip{2*pointer.x/pointerWidth-1,1-2*pointer.y/pointerHeight,.5,1};
            const auto far=glm::inverse(glm::dmat4(camera_->projectionMatrix()*camera_->viewMatrix()))*clip;
            const auto cameraLocal=glm::dvec3(camera_->position());
            const auto direction=glm::normalize(glm::dvec3(far)/far.w-cameraLocal);
            const auto from=cameraLocal+glm::dvec3(camera_->worldSector())*double(physics::kWorldSectorSize)-asset.origin-glm::dvec3(0,4,0);
            const auto hit=asset.workshop->pick(from,direction,asset.workshopPointerPlacement);
            asset.workshopPointerTarget=hit.has_value();
            if(asset.workshopPointerPlacement&&hit && (asset.workshop->brickToolActive()||input_->mouseDelta()!=glm::vec2(0)||pressed(A::RotateY)||input_->wasMouseButtonPressed(MouseButton::Left)))
                asset.workshopPointerTarget=asset.workshop->aimAt(*hit);
            if(input_->wasMouseButtonPressed(MouseButton::Left)) {
                if(asset.workshopPointerPlacement) {
                    if(asset.workshopPointerTarget&&asset.workshop->valid()) {
                        if(asset.workshop->brickToolActive()) {
                            const auto state=salvageLocalSession_->session->snapshot();
                            // The pending brick will be kept first. Count it
                            // when finding stock for the following preview.
                            const auto* nextStored=game::expedition::availableCoveStoredPart(asset.workshop->preview(),*asset.boat,state.storedParts,asset.workshop->catalogDefinition());
                            if(asset.workshop->placeBrickTool(nextStored))asset.launchMessage.clear();
                            asset.workshopPointerPlacement=asset.workshop->brickToolActive();
                        } else (void)salvagePreviewAction(71);
                    }
                } else if(hit) {
                    if(shift)(void)asset.workshop->selectPart(hit->placement,game::expedition::CoveWorkshop::SelectionMode::Toggle);
                    else if(hit->placement==asset.workshop->selected())asset.workshopPointerPlacement=true;
                    else (void)asset.workshop->selectPart(hit->placement);
                }
            }
        }
#if defined(VOXY_NATIVE)
        const auto stock=salvageLocalSession_->session->snapshot().inventory;
        const auto cost=asset.workshop->catalogCost();
        const auto settings=asset.workshop->selectedSettings();
        const std::string configuration=asset.workshop->configurable()
            ?std::string(settings.enabled?"On":"Off")+(asset.workshop->hasOutputLimit()?" | Limit "+std::to_string(settings.limitPermille/10)+"%":"")
                +(asset.workshop->canReverse()?(settings.reversed?" | Reversed":" | Forward"):"")+" | X: On/Off | L: Limit | N: Reverse"
            :"No adjustable settings";
        const std::string prompt=(asset.workshop->brickToolActive()?"Build | Click: Place another | R: Rotate | Esc: Select | Enter: Launch | ":"Workshop | ")
            +std::string(asset.workshop->selectedName())+" | "+asset.workshop->message()
            +" | "+configuration+" | Stock: "+std::to_string(stock.salvageMaterial)+" | Drawer: "+std::string(asset.workshop->catalogName())
            +" ("+std::to_string(cost.salvageMaterial)+" material, "+std::to_string(cost.specialMachinery)+" machinery)"
            +" | Recovery design "+std::to_string(asset.recoveryDesigns.empty()?0:asset.selectedRecoveryDesign+1)+"/"+std::to_string(asset.recoveryDesigns.size())+" | K: Load recovery | J: Next recovery | F: Remove recovery"
            +" | 1: Brick 1x2 | 2: Brick 2x2 | 3: Brick 2x4 | G: Focus | M: Whole boat | Right-drag: Orbit | Shift+right-drag: Pan | Wheel: Zoom | Click: Select/place | Tab: Part | Arrows/Q/Z: Move | T: Snap | R: Rotate | E: Keep | U: Undo edit | Enter: Launch | I/O: Undo/Redo launch | C: Part type | V: Add | H: Rebuild starter (paid parts stored) | B: Close"
            +(salvageSaveStatus_.empty()?"":" | "+salvageSaveStatus_);
        if(window_ && asset.playerPrompt!=prompt){asset.playerPrompt=prompt;window_->setTitle(prompt.c_str());}
#endif
        return;
    }
    if (input_->wasMouseButtonPressed(MouseButton::Left) && !input_->isMouseCaptured()
        && !input_->wasKeyPressed(Key::Escape)) input_->captureMouse();
    if (input_->isMouseCaptured()) {
        const auto delta = input_->mouseDelta();
        asset.characterCameraInput.orbitRadians+=glm::dvec2(-delta.x*(prefs.invertX?-1:1),delta.y*(prefs.invertY?-1:1))
            *double(config_.cameraMouseSensitivity)*prefs.mouseSensitivity;
    }
    asset.characterCameraInput.orbitRadians+=glm::dvec2(-look[0],look[1])*1.8*double(std::min(deltaTime,.1f));
    asset.characterCameraInput.zoomSteps+=double(input_->scrollDelta());
    if(pressed(A::CameraMode))(void)salvagePreviewAction(320);
    if(pressed(A::Recenter))(void)salvagePreviewAction(321);
    if(pressed(A::FrameLoad))(void)salvagePreviewAction(322);
    if(pressed(A::ReducedMotion))(void)salvagePreviewAction(323);
    const auto movement=ui.input.movement();
    const double forwardInput=movement[1],rightInput=movement[0];
    const double movementYaw=asset.characterCamera.pose().yaw+asset.characterCameraInput.orbitRadians.x;
    const glm::dvec2 horizontal{-std::sin(movementYaw),-std::cos(movementYaw)};
    const glm::dvec2 sideways{std::cos(movementYaw),-std::sin(movementYaw)};
    if(pressed(A::Rescue)){(void)salvagePreviewAction(1);return;}
    if(pressed(A::Cut)) { (void)salvagePreviewAction(95);if(salvageLocalSession_->launchRequest)return; }
    if(pressed(A::Interact))(void)salvagePreviewAction(30);
    if(pressed(A::AcceptJob))(void)salvagePreviewAction(50);
    if(pressed(A::Deliver))(void)salvagePreviewAction(51);
    const bool dockLift=asset.harbor&&asset.harbor->durable()&&!asset.player->onBoat();
    if(pressed(A::PowerHarbor))(void)salvagePreviewAction(52);
    if(pressed(A::Hook))(void)salvagePreviewAction(dockLift?(asset.harbor->attachmentPending()||asset.harbor->state().mode!=game::expedition::CoveHarborLiftMode::Detached?57:53):40);
    if(pressed(A::ReelIn)) {
        if(salvagePreviewAction(dockLift?54:41))asset.controllerMotor=true;
    }
    if(pressed(A::PayOut)) {
        if(salvagePreviewAction(dockLift?55:42))asset.controllerMotor=true;
    }
    if(asset.controllerMotor&&!down(A::ReelIn)&&!down(A::PayOut)) {
        (void)salvagePreviewAction(43);(void)salvagePreviewAction(56);asset.controllerMotor=false;
    }
    const auto beforeState=asset.player->state();
    if(!asset.checkpointPending)asset.player->advance(movementSeconds, {horizontal * forwardInput + sideways * rightInput,
        pressed(A::Jump)});
    const auto afterState=asset.player->state();
    double gaitSpeed=0;
    if(movementSeconds>0&&beforeState.onBoat==afterState.onBoat&&beforeState.root==afterState.root
        &&beforeState.interactions==afterState.interactions)
        gaitSpeed=glm::length(glm::dvec2(afterState.feet.x-beforeState.feet.x,afterState.feet.z-beforeState.feet.z))/movementSeconds;
    asset.character.update(movementSeconds,asset.player->mode(),gaitSpeed,afterState.verticalSpeed,
        asset.towMotor!=0||(asset.harbor&&asset.harbor->motor()!=0));
    if (asset.boatRoot().body.valid()&&!asset.boat->primaryRoot().cells.empty()) {
        const bool helm=asset.player->mode()==game::expedition::CovePlayer::Mode::Helm
            && !salvageLocalSession_->session->hasPending()&&!(asset.harbor&&asset.harbor->hasRopes());
        if (!physicsWorld_->setAuthoredHelm(asset.boatRoot().body,helm?static_cast<float>(forwardInput):0,
                                          helm?static_cast<float>(rightInput):0)) {
            LOG_ERROR("Cove helm input refused"); salvagePreviewFailed_=true; requestExit(); return;
        }
        asset.rotorCommandBody=asset.boatRoot().body;
        if(helm && asset.boat->primaryRoot().propeller) {
            const auto* module=asset.boat->assembly().functions().module(*asset.boat->primaryRoot().propeller);
            if(module)asset.rotorCommandDrive=forwardInput
                *static_cast<double>(game::expedition::coveModuleOutput(*module))
                *(module->settings.reversed?-1.0:1.0);
        }
    }
    if(asset.harbor&&(asset.harbor->motor()!=0||asset.harbor->attachmentPending())&&(asset.player->onBoat()
        ||glm::length(asset.player->feet()-asset.content->registry.navigation->dockBoarding)>4)){
        if(!asset.harbor->stop(*physicsWorld_)){salvagePreviewFailed_=true;requestExit();return;}
    }
#if defined(VOXY_NATIVE)
    using Player = game::expedition::CovePlayer;
    const char* action = "Walk beside the boat to board";
    switch (asset.player->interaction()) {
        case Player::Interaction::Board: action = "E: Board boat"; break;
        case Player::Interaction::ReturnToDock: action = "E: Return to dock"; break;
        case Player::Interaction::UseHelm: action = "E: Use helm"; break;
        case Player::Interaction::LeaveHelm: action = "W/S: Throttle | A/D: Steer | E: Leave helm"; break;
        case Player::Interaction::None: break;
    }
    if (asset.player->mode() == Player::Mode::Swimming) action = "Swimming - R: Return to dock";
    const auto cutterTarget=asset.cutTarget();
    const std::string prompt = std::string("Cove | ") + action
        +(cutterTarget?" | C: Cut "+asset.cutLabel(*cutterTarget):"")
        +(asset.launchMessage.empty()?"":" | "+asset.launchMessage)
        + " | P: Pause"
        + (asset.deliveryDurable&&asset.harbor&&!asset.harbor->installed()?" | K: Power harbor lift":"")
        + (dockLift?" | F: Attach/release lift | Q/Z: Raise/lower":"")
        + (salvageLocalSession_->jobId.counter ? " | J: Take recovery job | H: Deliver" : "")
        + (asset.player->onBoat() && asset.cargo && !asset.cargoBanked ? (asset.towRope.valid() && !asset.towBroken
            ? " | F: Release tow | Q/Z: Reel/Pay out" : " | F: Hook salvage") : "")
        + (asset.player->mode()==Player::Mode::Helm ? " | R: Return" : " | WASD: Walk | Space: Jump | R: Return")
        +(asset.harbor&&!asset.harbor->message().empty()?" | "+std::string(asset.harbor->message()):"")
        +(salvageSaveStatus_.empty()?"":" | "+salvageSaveStatus_);
    if (asset.playerPrompt != prompt) {
        asset.playerPrompt = prompt;
        if (window_) window_->setTitle(prompt.c_str());
        LOG_INFO("{}", prompt);
    }
#endif
}

void Application::processInput(float deltaTime) {
    if (config_.salvagePreviewEnabled) return;
    if (config_.legoTerrainEnabled && input_ && input_->wasKeyPressed(Key::P)) {
        legoAction(legoPlaygroundActive_ ? 8 : 0);
    }
    if (legoPlaygroundActive_ && legoPlayground_ && camera_ && input_) {
        const glm::vec3 eye = camera_->position() + glm::vec3(camera_->worldSector()) * physics::kWorldSectorSize;
        legoPlayground_->preview(eye, camera_->forward());
        if (input_->wasKeyPressed(Key::R)) legoAction(7);
        if (input_->wasKeyPressed(Key::Backspace)) legoAction(3);
        if (input_->wasKeyPressed(Key::B)) legoAction(2);
        if (input_->wasKeyPressed(Key::Enter)
            || (input_->isMouseCaptured() && input_->wasMouseButtonPressed(MouseButton::Left))) legoAction(1);
        return;
    }
    processThrowableInput(deltaTime);
}

bool Application::legoAction(int action) {
    if (config_.salvagePreviewEnabled || !config_.legoTerrainEnabled || !camera_ || !heightmap_) return false;
    if (action == 0) {
        if (!legoPlayground_) {
            const bool world = heightmap_->getWidth() > terrain::lego::kMaximumStudySamples;
            glm::vec3 origin{world ? -1164.0f : -12.0f, -10000.0f, world ? 3424.0f : -32.0f};
            for (int z=-10; z<=10; ++z) for (int x=-10; x<=10; ++x)
                origin.y = std::max(origin.y, sampleTerrainHeight(origin.x+float(x),origin.z+float(z)));
            origin.y += .64f;
            auto playground = std::make_unique<game::LegoPlayground>();
            if (!physicsWorld_ || !playground->initialize(*physicsWorld_, origin)) return false;
            legoPlayground_ = std::move(playground);
        }
        legoPlaygroundActive_ = true;
        setControllerMode(ControllerMode::FreeFly);
        const auto origin=legoPlayground_->origin();
        setCameraWorldPose(*camera_, origin+glm::vec3(0,8,15), origin+glm::vec3(0,1,0));
        return true;
    }
    if (!legoPlayground_) return false;
    const glm::vec3 eye=camera_->position()+glm::vec3(camera_->worldSector())*physics::kWorldSectorSize;
    switch(action) {
    case 1: legoPlayground_->preview(eye,camera_->forward()); return legoPlayground_->place();
    case 2: return legoPlayground_->launch(eye,camera_->forward());
    case 3: legoPlayground_->reset(); return true;
    case 4: case 5: case 6: legoPlayground_->select(uint32_t(action-4)); return true;
    case 7: legoPlayground_->rotate(); return true;
    case 8: legoPlaygroundActive_=false; legoPlayground_->preview(glm::vec3(0),glm::vec3(0)); return true;
    case 9: {
        const auto state=legoPlayground_->state();
        if(state.phase!=1) return false;
        const auto aim=state.target-glm::vec3(0,1.1f,0);
        setCameraWorldPose(*camera_,aim+glm::vec3(0,0,8),aim+glm::vec3(0,.56f,0));
        return true;
    }
    default: return false;
    }
}

std::string Application::legoHudJson() const {
    const auto s=legoPlayground_ ? legoPlayground_->state() : game::LegoPlayground::State{};
    const auto o=legoPlayground_ ? legoPlayground_->origin() : glm::vec3(0);
    std::ostringstream json;
    json << "{\"active\":" << (legoPlaygroundActive_ ? "true" : "false")
         << ",\"grouped\":" << (legoMode_ ? "true" : "false")
         << ",\"bricks\":" << s.bricks << ",\"balls\":" << s.balls
         << ",\"awake\":" << s.awake << ",\"sleeping\":" << s.sleeping
         << ",\"sleepTransitions\":" << s.sleepTransitions << ",\"wakeTransitions\":" << s.wakeTransitions
         << ",\"levels\":" << s.levels << ",\"phase\":" << s.phase << ",\"clicks\":" << s.clicks
         << ",\"dust\":" << s.dust << ",\"impacts\":" << s.impacts
         << ",\"valid\":" << (s.previewValid ? "true" : "false")
         << ",\"origin\":[" << o.x << ',' << o.y << ',' << o.z << ']'
         << ",\"target\":[" << s.target.x << ',' << s.target.y << ',' << s.target.z << "]}";
    return json.str();
}

bool Application::spawnThrowable(
    physics::ThrowableShape shape, const glm::vec3& origin,
    const glm::vec3& direction, const glm::ivec3& sector) {
    if (config_.salvagePreviewEnabled || !physicsWorld_
        || static_cast<uint32_t>(shape)
            >= static_cast<uint32_t>(physics::ThrowableShape::Count)
        || !std::isfinite(origin.x) || !std::isfinite(origin.y)
        || !std::isfinite(origin.z) || !std::isfinite(direction.x)
        || !std::isfinite(direction.y) || !std::isfinite(direction.z)
        || glm::dot(direction, direction) < 1.0e-8f) {
        return false;
    }

    const uint32_t playgroundBodies=legoPlayground_ ? legoPlayground_->residentCount() : 0u;
    if (config_.legoTerrainEnabled && physicsWorld_->stats().residentBodies >= 32u+playgroundBodies) return false;
    const glm::vec3 normalizedDirection = glm::normalize(direction);
    glm::vec3 launchOrigin = origin;
    glm::ivec3 launchSector = sector;
    std::optional<double> cubeTriangleImpactX;
    if (config_.cubePyramidBodyCount != 0u) {
        // The complete wall needs a distant overview camera. Preserve the
        // ordinary 28 m/s projectile and its real collision response, but
        // move its starting point along the same sight line to a launch plane
        // 20 m before the wall. This avoids turning a normal click into a
        // 160 m/s cannon shot merely to cover the camera distance.
        constexpr double launchPlaneZ =
            static_cast<double>(kBrowserJourneyTargetZ) - 20.0;
        const physics::WorldPosition source =
            physics::canonicalWorldPosition(sector, glm::dvec3(origin));
        glm::dvec3 absolute =
            physics::worldPositionToAbsolute(source);
        const double directionZ =
            static_cast<double>(normalizedDirection.z);
        if (std::abs(directionZ) > 1.0e-6) {
            const double distance =
                (launchPlaneZ - absolute.z) / directionZ;
            if (distance > 0.0 && distance <= 512.0) {
                const glm::dvec3 candidate = absolute
                    + glm::dvec3(normalizedDirection) * distance;
                constexpr double wallHalfWidth = 180.0;
                constexpr double wallVerticalMargin = 200.0;
                if (std::abs(
                        candidate.x
                        - static_cast<double>(kBrowserJourneyTargetX))
                        <= wallHalfWidth
                    && std::abs(candidate.y - absolute.y)
                        <= wallVerticalMargin) {
                    const physics::WorldPosition launch =
                        physics::worldPositionFromAbsolute(candidate);
                    launchOrigin = launch.local;
                    launchSector = launch.sector;
                    cubeTriangleImpactX = candidate.x;
                }
            }
        }
    }
    if (cubeTriangleImpactX) {
        wakeCubeTriangleImpactColumns(*cubeTriangleImpactX);
    }

    constexpr float throwSpeed = 28.0f;
    if (physicsWorld_->backendType()
        == physics::BackendType::Box3DReference) {
        return physicsWorld_->throwBody(
            shape, launchOrigin, normalizedDirection * throwSpeed);
    }

    physics::BodySpawnDesc desc;
    desc.shape = shape;
    desc.bullet = config_.legoTerrainEnabled;
    desc.position = launchOrigin;
    desc.sector = launchSector;
    desc.linearVelocity = normalizedDirection * throwSpeed;
    desc.angularVelocity = {3.5f, 5.0f, 2.5f};
    desc.dimensions = physics::throwableShapeDimensions(shape);
    return physicsWorld_->spawnBody(desc).valid();
}

uint32_t Application::throwThrowableBatch(
    physics::ThrowableShape shape, uint32_t maximumBodies) {
    if (!camera_ || !physicsWorld_ || maximumBodies == 0u) return 0u;

    constexpr uint32_t maximumColumns = 16u;
    constexpr uint32_t maximumRows = 8u;
    constexpr uint32_t fullBatchSize = maximumColumns * maximumRows;
    const uint32_t batchSize = std::min(maximumBodies, config_.legoTerrainEnabled ? 8u : fullBatchSize);
    const uint32_t columns = std::min(batchSize, maximumColumns);
    const uint32_t rows = (batchSize + columns - 1u) / columns;
    const glm::vec3 direction = glm::normalize(camera_->forward());
    const glm::vec3 dimensions =
        physics::throwableShapeDimensions(shape);
    const float maximumDimension = std::max(
        dimensions.x, std::max(dimensions.y, dimensions.z));
    const float spacing = maximumDimension * 1.08f + 0.02f;
    const float halfWidth = 0.5f * static_cast<float>(columns - 1u)
                          * spacing + 0.5f * maximumDimension;
    const float halfHeight = 0.5f * static_cast<float>(rows - 1u)
                           * spacing + 0.5f * maximumDimension;
    const float tanHalfFov = std::max(
        std::tan(camera_->fovY() * 0.5f), 1.0e-3f);
    const float verticalDistance = halfHeight / tanHalfFov;
    const float horizontalDistance = halfWidth
        / (tanHalfFov * std::max(camera_->aspectRatio(), 1.0e-3f));
    const uint32_t batchLane =
        (physicsWorld_->stats().residentBodies / fullBatchSize) % 4u;
    const float batchDistance = std::max(
        2.2f, std::max(verticalDistance, horizontalDistance) + 0.5f)
        + static_cast<float>(batchLane) * spacing * 1.5f;
    const glm::vec3 batchCenter =
        camera_->position() + direction * batchDistance;
    const glm::vec3 cameraRight = glm::normalize(camera_->right());
    const glm::vec3 cameraUp = glm::normalize(camera_->up());
    const glm::ivec3 cameraSector = camera_->worldSector();

    uint32_t thrown = 0u;
    for (uint32_t index = 0u; index < batchSize; ++index) {
        const uint32_t column = index % columns;
        const uint32_t row = index / columns;
        const float x = (static_cast<float>(column)
            - 0.5f * static_cast<float>(columns - 1u)) * spacing;
        const float y = (static_cast<float>(row)
            - 0.5f * static_cast<float>(rows - 1u)) * spacing;
        const float coneX = x / std::max(halfWidth, 1.0e-3f);
        const float coneY = y / std::max(halfHeight, 1.0e-3f);
        glm::vec3 launchDirection = glm::normalize(
            direction + cameraRight * (coneX * 0.08f)
                      + cameraUp * (coneY * 0.08f));
        glm::vec3 spawnOrigin = batchCenter
                              + cameraRight * x + cameraUp * y;
        if (characterController_) {
            const float worldX = spawnOrigin.x
                + static_cast<float>(cameraSector.x)
                * physics::kWorldSectorSize;
            const float worldZ = spawnOrigin.z
                + static_cast<float>(cameraSector.z)
                * physics::kWorldSectorSize;
            const float terrainHeight =
                characterController_->sampleTerrainHeight(worldX, worldZ);
            const float clearance = dimensions.y * 0.5f + 0.05f;
            const float minimumY = terrainHeight
                - static_cast<float>(cameraSector.y)
                * physics::kWorldSectorSize
                + clearance;
            if (spawnOrigin.y < minimumY) {
                spawnOrigin.y = minimumY;
                const glm::vec3 terrainNormal = glm::normalize(
                    characterController_->sampleTerrainNormal(
                        worldX, worldZ));
                const float intoTerrain =
                    glm::dot(launchDirection, terrainNormal);
                if (intoTerrain < 0.0f) {
                    launchDirection = glm::normalize(
                        launchDirection - terrainNormal * intoTerrain
                        + terrainNormal * 0.1f);
                }
            }
        }
        thrown += spawnThrowable(
            shape, spawnOrigin, launchDirection, cameraSector) ? 1u : 0u;
    }
    return thrown;
}

void Application::processThrowableInput(float deltaTime) {
    if (config_.salvagePreviewEnabled) return;
    if (!input_ || !camera_ || !physicsWorld_) {
        return;
    }

    const float wheel = config_.legoTerrainEnabled ? 0.0f : input_->scrollDelta();
    if (wheel != 0.0f) {
        if (throwableWheelAccumulator_ * wheel < 0.0f) {
            throwableWheelAccumulator_ = 0.0f;
        }
        throwableWheelAccumulator_ += wheel;
    }

    constexpr float wheelStep = 1.0f;
    if (std::abs(throwableWheelAccumulator_) >= wheelStep) {
        constexpr uint32_t count = static_cast<uint32_t>(
            physics::PhysicsWorld::ThrowableShape::Count);
        if (throwableWheelAccumulator_ > 0.0f) {
            selectedThrowable_ = (selectedThrowable_ + 1u) % count;
        } else {
            selectedThrowable_ = (selectedThrowable_ + count - 1u) % count;
        }
        throwableWheelAccumulator_ = 0.0f;
        const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
            selectedThrowable_);
        LOG_INFO("Selected throwable: {}",
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    const bool mouseCaptured = input_->isMouseCaptured();
    const bool batchRequested = mouseCaptured
                             && input_->wasMouseButtonPressed(MouseButton::Right);
    const bool firing = mouseCaptured
                     && input_->isMouseButtonDown(MouseButton::Left);
    const bool singleRequested = config_.legoTerrainEnabled && input_->wasKeyPressed(Key::B);
    if (!batchRequested && !firing && !singleRequested) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const auto shape = static_cast<physics::PhysicsWorld::ThrowableShape>(
        selectedThrowable_);
    const glm::vec3 direction = glm::normalize(camera_->forward());
    const glm::vec3 origin = camera_->position() + direction * 2.2f;

    if (singleRequested) {
        if (!spawnThrowable(shape, origin, direction, camera_->worldSector())) {
            LOG_DEBUG("Single throwable request rejected");
        }
        return;
    }

    if (batchRequested) {
        constexpr uint32_t batchSize = 16u * 8u;
        const uint32_t thrown = throwThrowableBatch(shape, batchSize);
        LOG_INFO("Threw {} x {}", thrown,
                 physics::PhysicsWorld::throwableShapeName(shape));
    }

    if (!firing) {
        throwableCooldown_ = 0.0f;
        return;
    }

    const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
    throwableCooldown_ -= frameTime;
    const float throwInterval = config_.legoTerrainEnabled ? 0.3f : 1.0f / 100.0f;
    while (throwableCooldown_ <= 0.0f) {
        if (throwableBodyLimit_ != 0u
            && physicsWorld_->stats().residentBodies
                >= throwableBodyLimit_) {
            break;
        }
        if (spawnThrowable(
                shape, origin, direction, camera_->worldSector())) {
            LOG_INFO("Threw {}", physics::PhysicsWorld::throwableShapeName(shape));
        }
        throwableCooldown_ += throwInterval;
    }
}

void Application::handleKeyboardShortcuts() {
    if (!input_ || coveUiOwnsInput()) return;
    // Presentation pacing applies to every experience, including the Cove
    // and its workshop. Handle it before their gameplay-shortcut guards.
    if (input_->wasKeyPressed(Key::F9)) toggleUncappedFPS();
    // The playable Cove owns its remapped function keys; legacy debug views
    // must not fire alongside a movement/build action bound to those keys.
    if(salvageLocalSession_&&salvageLocalSession_->asset&&salvageLocalSession_->asset->player)return;
    if(salvageLocalSession_ && salvageLocalSession_->asset && salvageLocalSession_->asset->workshopOpen)return;

    // Escape - release mouse or exit
    if (input_->wasKeyPressed(Key::Escape)&&!(salvageLocalSession_&&salvageLocalSession_->asset&&salvageLocalSession_->asset->player)) {
        if (input_->isMouseCaptured()) {
            input_->releaseMouse();
        } else {
#if !defined(VOXY_WASM)
            requestExit();
#endif
        }
    }

    // F1 - toggle debug overlay
    if (input_->wasKeyPressed(Key::F1)) {
        getDebugOverlay().toggle();
    }

    // F2 - toggle wireframe mode (triangle path)
    if (input_->wasKeyPressed(Key::F2)&&!(salvageLocalSession_&&salvageLocalSession_->asset&&salvageLocalSession_->asset->player)) {
        toggleWireframe();
    }

    // F3 - toggle render path
    if (input_->wasKeyPressed(Key::F3) && !config_.legoTerrainEnabled) {
        toggleRenderPath();
    }

    // F4 - toggle depth visualization
    if (input_->wasKeyPressed(Key::F4)) {
        if (debugVisMode_ == DebugVisMode::Depth) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Depth);
        }
    }

    // F5 - toggle normal visualization
    if (input_->wasKeyPressed(Key::F5)) {
        if (debugVisMode_ == DebugVisMode::Normals) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::Normals);
        }
    }

    // F6 - toggle mip level heat map (raycast path)
    if (input_->wasKeyPressed(Key::F6)) {
        if (debugVisMode_ == DebugVisMode::MipLevels) {
            setDebugVisMode(DebugVisMode::Off);
        } else {
            setDebugVisMode(DebugVisMode::MipLevels);
        }
    }

    if (config_.salvagePreviewEnabled) {
        if(salvageLocalSession_&&salvageLocalSession_->asset&&salvageLocalSession_->asset->player)return;
        if (input_->wasKeyPressed(Key::R)) salvagePreviewAction(1);
        if (input_->wasKeyPressed(Key::Num0)) salvagePreviewAction(10);
        if (input_->wasKeyPressed(Key::Num1)) salvagePreviewAction(11);
        if (input_->wasKeyPressed(Key::Num2)) salvagePreviewAction(12);
        if (input_->wasKeyPressed(Key::Num3)) salvagePreviewAction(13);
        if (input_->wasKeyPressed(Key::G) && salvageLocalSession_ && salvageLocalSession_->asset)
            salvagePreviewAction(20 + (static_cast<int>(salvageLocalSession_->asset->guides) + 1) % 3);
        return;
    }

    // F7 - toggle benchmark mode
    if (!wreckwaterClientState_ && !motoSession_
        && input_->wasKeyPressed(Key::F7)) {
        toggleBenchmark();
    }
    
    // F8 - toggle controller mode (free-fly / character)
    if (!wreckwaterClientState_ && !motoSession_
        && !legoPlaygroundActive_ && input_->wasKeyPressed(Key::F8)) {
        toggleControllerMode();
    }

    // K - toggle Lego Mode (F10 is reserved by browser)
    if (input_->wasKeyPressed(Key::K)) {
        toggleLegoMode();
    }

    // Free-fly recording and teleport shortcuts must not move an
    // authority-following multiplayer camera.
    if (wreckwaterClientState_
        || (motoSession_ && motoSession_->isInitialized())) return;

    // R - Record camera position
    if (!legoPlaygroundActive_ && input_->wasKeyPressed(Key::R)) {
        if (camera_) {
            CameraState state;
            state.position = camera_->position();
            state.yaw = camera_->yaw();
            state.pitch = camera_->pitch();
            state.sector = camera_->worldSector();

            recordedPositions_.push_back(state);
            LOG_INFO("Recorded state [{}] : Pos({:.2f}, {:.2f}, {:.2f}) Yaw({:.2f}) Pitch({:.2f})",
                     recordedPositions_.size() - 1, state.position.x, state.position.y, state.position.z,
                     state.yaw, state.pitch);

            LOG_INFO("All recorded positions:");
            LOG_INFO("teleportTargets_ = {{");
            for (const auto& s : recordedPositions_) {
                LOG_INFO("    {{ {{ {:.2f}f, {:.2f}f, {:.2f}f }}, {:.4f}f, {:.4f}f }},",
                         s.position.x, s.position.y, s.position.z, s.yaw, s.pitch);
            }
            LOG_INFO("}};");
        }
    }

    // 1-9 - Teleport
    auto checkTeleport = [&](Key key, size_t index) {
        if (input_->wasKeyPressed(key)) {
            if (index < teleportTargets_.size()) {
                if (camera_) {
                    const auto& target = teleportTargets_[index];
                    camera_->setWorldPosition(target.sector, target.position);
                    camera_->setYaw(target.yaw);
                    camera_->setPitch(target.pitch);
                    LOG_INFO("Teleported to position {}: {:.2f}, {:.2f}, {:.2f}",
                             index + 1, target.position.x, target.position.y, target.position.z);
                }
            } else {
                LOG_WARN("No teleport target for index {} (defined: {})", index + 1, teleportTargets_.size());
            }
        }
    };

    checkTeleport(Key::Num1, 0);
    checkTeleport(Key::Num2, 1);
    checkTeleport(Key::Num3, 2);
    checkTeleport(Key::Num4, 3);
    checkTeleport(Key::Num5, 4);
    checkTeleport(Key::Num6, 5);
    checkTeleport(Key::Num7, 6);
    checkTeleport(Key::Num8, 7);
    checkTeleport(Key::Num9, 8);
}

bool Application::captureScreenshot(const std::string& filepath, CaptureFormat captureFormat) {
#if defined(VOXY_WASM)
    static_cast<void>(filepath);
    static_cast<void>(captureFormat);
    LOG_WARN("Screenshots are not supported on WebAssembly builds");
    return false;
#else
    if (!gpuContext_) return false;

    LOG_INFO("Capturing screenshot to: {}", filepath);

    WGPUDevice device = gpuContext_->getDevice();
    WGPUQueue queue = gpuContext_->getQueue();
    WGPUTexture sourceTexture = config_.benchmarkOnStartup
        ? benchmarkTargetTexture_ : gpuContext_->getCurrentTexture();

    if (!sourceTexture) {
        LOG_ERROR("No current texture to capture");
        return false;
    }

    uint32_t width = gpuContext_->getSwapchainWidth();
    uint32_t height = gpuContext_->getSwapchainHeight();

    const WGPUTextureFormat format = gpuContext_->getSwapchainFormat();
    const bool bgra = format == WGPUTextureFormat_BGRA8Unorm
                   || format == WGPUTextureFormat_BGRA8UnormSrgb;
    const bool rgba = format == WGPUTextureFormat_RGBA8Unorm
                   || format == WGPUTextureFormat_RGBA8UnormSrgb;
    if (!bgra && !rgba) {
        LOG_ERROR("Screenshot capture does not support texture format {}",
                  gpu::textureFormatToString(format));
        return false;
    }
    if (width == 0u || height == 0u
        || width > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        LOG_ERROR("Invalid screenshot dimensions: {}x{}", width, height);
        return false;
    }

    // Bytes per row must be a multiple of 256.
    constexpr uint64_t bytesPerPixel = 4u;
    constexpr uint64_t rowAlignment = 256u;
    const uint64_t unalignedBytesPerRow =
        static_cast<uint64_t>(width) * bytesPerPixel;
    const uint64_t alignedBytesPerRow =
        (unalignedBytesPerRow + rowAlignment - 1u)
        & ~(rowAlignment - 1u);
    const uint64_t bufferSize =
        alignedBytesPerRow * static_cast<uint64_t>(height);
    const uint64_t pixelBytes =
        static_cast<uint64_t>(width) * height * bytesPerPixel;
    if (alignedBytesPerRow > std::numeric_limits<uint32_t>::max()
        || bufferSize > std::numeric_limits<size_t>::max()
        || pixelBytes > std::numeric_limits<size_t>::max()) {
        LOG_ERROR("Screenshot dimensions overflow the readback layout");
        return false;
    }
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignedBytesPerRow);
    const size_t mappedSize = static_cast<size_t>(bufferSize);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = "screenshot_buffer";
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    if (!buffer) {
        LOG_ERROR("Failed to create screenshot readback buffer");
        return false;
    }

    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label = "screenshot_encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    if (!encoder) {
        LOG_ERROR("Failed to create screenshot command encoder");
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }

    WGPUImageCopyTexture src = {};
    src.texture = sourceTexture;
    src.origin = {0, 0, 0};

    WGPUImageCopyBuffer dst = {};
    dst.buffer = buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = height;

    WGPUExtent3D extent = {width, height, 1};

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);
    if (!cmd) {
        LOG_ERROR("Failed to finish screenshot command buffer");
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }

#if defined(VOXY_USE_DAWN)
    wgpuQueueSubmit(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, 0}; // unused for Dawn path
#else
    // Capture submission index so we can explicitly wait for completion.
    WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(queue, 1, &cmd);
    WGPUWrappedSubmissionIndex submission = {queue, submissionIndex};
#endif
    wgpuCommandBufferRelease(cmd);

    auto* mapState = new ScreenshotMapState;
    wgpuBufferMapAsync(
        buffer, WGPUMapMode_Read, 0, mappedSize,
        onScreenshotBufferMapped, mapState);

    // Wait for mapping
    // Process events to allow callbacks to fire
    // wgpu-native callbacks are typically synchronous, but Dawn requires polling
    constexpr int maxPollAttempts = 1000;
    int pollAttempt = 0;
    while (!mapState->done.load(std::memory_order_acquire)
           && pollAttempt < maxPollAttempts) {
#if defined(VOXY_USE_DAWN)
        WGPUInstance instance = gpuContext_->getInstance();
        wgpuInstanceProcessEvents(instance);
    #if defined(_WIN32)
        Sleep(1);
    #else
        usleep(1000);
    #endif
#else
        // Pump the wgpu-native device, waiting on our submission.
        wgpuDevicePoll(device, /*wait=*/true, &submission);
#endif

        pollAttempt++;
    }
    
    const bool mappingDone =
        mapState->done.load(std::memory_order_acquire);
    const bool mappingSucceeded = mappingDone
        && mapState->succeeded.load(std::memory_order_relaxed);
    releaseScreenshotMapState(mapState);
    if (!mappingDone) {
        LOG_ERROR("Buffer mapping timed out after {} poll attempts", maxPollAttempts);
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }
    if (!mappingSucceeded) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }

    // Read data
    const uint8_t* data = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(buffer, 0, mappedSize));
    if (!data) {
        LOG_ERROR("Failed to map buffer range");
        wgpuBufferUnmap(buffer);
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }

    // Convert to RGBA and remove row padding.
    std::vector<uint8_t> pngData(static_cast<size_t>(pixelBytes));
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t srcIndex = static_cast<size_t>(y) * bytesPerRow
                                  + static_cast<size_t>(x) * 4u;
            const size_t dstIndex =
                (static_cast<size_t>(y) * width + x) * 4u;
            pngData[dstIndex + 0] = data[srcIndex + (bgra ? 2u : 0u)];
            pngData[dstIndex + 1] = data[srcIndex + 1]; // G
            pngData[dstIndex + 2] = data[srcIndex + (bgra ? 0u : 2u)];
            pngData[dstIndex + 3] = data[srcIndex + 3]; // A
        }
    }

    wgpuBufferUnmap(buffer);
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);

    const int written = captureFormat == CaptureFormat::Jpeg
        ? stbi_write_jpg(filepath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pngData.data(), 90)
        : stbi_write_png(filepath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pngData.data(), static_cast<int>(width) * 4);
    if (written) {
        LOG_INFO("Saved screenshot to: {}", filepath);
        if (captureFormat == CaptureFormat::Png && config_.salvageAssetFixtureRegistry)
            LOG_INFO("Asset inspection capture state: {}", salvagePreviewJson());
    } else {
        LOG_ERROR("Failed to save screenshot to: {}", filepath);
    }
    return written != 0;
#endif
}

} // namespace voxy

namespace voxy {
bool Application::updateCoveBoat() {
    if(coveResume_ && coveResume_->awaitingCommit)return true;
    auto& asset=*salvageLocalSession_->asset;
    if(!asset.boatRoots || !asset.boatRoots->matches(*asset.boat))return false;
    if(asset.effectsPath.initialized()) {
        const auto frontier=physicsWorld_->tickFrontier();
        const bool rescuing=asset.rescue!=SalvageLocalSessionState::AssetPreview::Rescue::None;
        const auto revision=asset.boat->build().revision.value();
        if(frontier.incarnation&&(asset.effects.stats().epoch!=frontier.incarnation
            ||asset.effectsBuildRevision!=revision||(!asset.effectsRescuing&&rescuing)
            ||(asset.leaving&&!asset.effects.instances().empty()))) {
            const auto tick=asset.boatRoots->joinedTick();
            if(!asset.effects.reset(frontier.incarnation,tick?tick:frontier.baseTick))return false;
            asset.effectsImpactCount=0;asset.effectsImpactTick=0;
            asset.effectsBuildRevision=revision;
        }
        asset.effectsRescuing=rescuing;
    }
    auto* resources=physicsWorld_->authoredShapeResources();
    if (!resources) { LOG_ERROR("Cove physics stopped"); return false; }
    resources->poll();
    if (resources->stats().phase==physics::ShapeResourcePhase::Failed) {
        LOG_ERROR("Cove shape failure: {}",resources->failure()); return false;
    }
    if (!asset.leaving && resources->stats().phase==physics::ShapeResourcePhase::Ready) {
        if (!asset.sceneryShape.valid()) {
            auto shape=asset.scenery->shape(); physics::ShapeResourceError error;
            asset.sceneryShape=resources->upload(std::move(shape),error);
            if (!asset.sceneryShape.valid()) {
                if(error==physics::ShapeResourceError::Busy||error==physics::ShapeResourceError::NotReady)return true;
                LOG_ERROR("Cove scenery upload failed: {}",static_cast<int>(error)); return false;
            }
        }
        if(asset.environmentCollision && !asset.environmentShape.valid()) {
            auto shape=asset.environmentCollision->shape();physics::ShapeResourceError error;
            asset.environmentShape=resources->upload(std::move(shape),error);
            if(!asset.environmentShape.valid()){
                if(error==physics::ShapeResourceError::Busy||error==physics::ShapeResourceError::NotReady)return true;
                LOG_ERROR("Cove environment upload failed: {}",static_cast<int>(error));return false;
            }
        }
        for(size_t i=0;i<asset.boatRoots->roots().size();++i) {
            auto& root=asset.boatRoots->roots()[i];
            if(root.shape.valid())continue;
            auto shape=asset.boat->roots()[i].shape;physics::ShapeResourceError error;
            root.shape=resources->upload(std::move(shape),error);
            if(!root.shape.valid()){
                // Restored split boats plus both static scenery owners can
                // exceed one upload batch. Keep accepted handles and retry
                // the remaining shapes after the normal callback poll.
                if(error==physics::ShapeResourceError::Busy||error==physics::ShapeResourceError::NotReady)return true;
                LOG_ERROR("Cove section upload failed: {}",static_cast<int>(error));return false;
            }
        }
        if (!asset.sceneryBody.valid() && resources->state(asset.sceneryShape)==physics::ShapeResourceState::Ready
            && asset.status==render::SalvageFixtureStatus::Active) {
            physics::AuthoredBodySpawnDesc desc; desc.shape=asset.sceneryShape;
            desc.motionType=physics::AuthoredBodyMotionType::Static;
            desc.motion.position=physics::worldPositionFromAbsolute(asset.origin+asset.scenery->origin());
            const auto fixed=physicsWorld_->spawnAuthoredBody(desc);
            if(!fixed) { LOG_ERROR("Cove scenery admission failed: {}",static_cast<int>(fixed.error)); return false; }
            asset.sceneryBody=fixed.body;
        }
        if(asset.environmentCollision && !asset.environmentBody.valid()
            && resources->state(asset.environmentShape)==physics::ShapeResourceState::Ready
            && asset.status==render::SalvageFixtureStatus::Active) {
            physics::AuthoredBodySpawnDesc desc;desc.shape=asset.environmentShape;
            desc.motionType=physics::AuthoredBodyMotionType::Static;
            desc.motion.position=physics::worldPositionFromAbsolute(asset.origin);
            const auto fixed=physicsWorld_->spawnAuthoredBody(desc);
            if(!fixed){LOG_ERROR("Cove environment admission failed: {}",static_cast<int>(fixed.error));return false;}
            asset.environmentBody=fixed.body;
            asset.environmentAdmissionTick=physicsWorld_->tickFrontier().scheduled+1;
            asset.environmentRemovalTick=0;
        }
        if(!asset.boatRoots->allAdmitted() && asset.sceneryBody.valid()
            && (!asset.environmentCollision || asset.environmentBody.valid())
            && asset.status==render::SalvageFixtureStatus::Active
            && std::all_of(asset.boatRoots->roots().begin(),asset.boatRoots->roots().end(),
                [&](const auto& root){return resources->state(root.shape)==physics::ShapeResourceState::Ready;})) {
            if(!asset.restorePhysical && asset.boatRoots->roots().size()!=1)return false;
            for(size_t i=0;i<asset.boatRoots->roots().size();++i) {
                auto& owned=asset.boatRoots->roots()[i];if(owned.body.valid())continue;
                const auto anchor=asset.boat->assembly().mass().roots()[i].buildFromRoot.translation;
                auto spawn=asset.origin+glm::dvec3(anchor.x,anchor.y,anchor.z)*.02;
                if(i==asset.boat->primaryRootIndex()) {
                    const auto surface=waterSimulation_->samplePlacementHeight(glm::vec2(spawn.x,spawn.z),
                        waterPhaseSeconds(),rendererSettings_.waterWaveStrength);
                    if(!surface){LOG_ERROR("The cove berth water surface is unavailable.");return false;}
                    spawn.y=asset.origin.y+asset.boat->equilibriumRootHeight()+static_cast<double>(*surface);
                }
                owned.spawn.position=physics::worldPositionFromAbsolute(spawn);
                const auto initialMotion=asset.restorePhysical?owned.observed:owned.spawn;
                const auto spawned=physicsWorld_->spawnAuthoredBody({.shape=owned.shape,.motion=initialMotion});
                if(!spawned){LOG_ERROR("Cove section admission failed: {}",static_cast<int>(spawned.error));return false;}
                owned.body=spawned.body;owned.observed=initialMotion;
                owned.admissionTick=physicsWorld_->tickFrontier().scheduled+1;
                const auto& physical=asset.boat->roots()[i];
                if(!physical.cells.empty()&&physicsWorld_->configureAuthoredWaterBody(physical.water(owned.body))!=physics::AuthoredBodyError::None) {
                    LOG_ERROR("Cove section buoyancy setup failed");return false;
                }
            }
            const auto& primary=asset.boat->primaryRoot();if(!primary.helm)return false;
            const auto water=primary.water(asset.boatRoot().body);
            asset.thrustLimit=primary.cells.empty()?0:water.maximumThrustNewtons;asset.steeringLimit=water.maximumSteeringRadians;
            asset.thrustDirection=water.propellerDirection;
            std::string playerIssue;
            if(!asset.boatRoots->bindPlayer(*asset.player,*asset.boat,asset.origin,playerIssue)){LOG_ERROR("{}",playerIssue);return false;}
            LOG_INFO("Cove machine active: {} physical sections",asset.boatRoots->roots().size());
        }
    }
    if(asset.cargo && !asset.leaving && resources->stats().phase==physics::ShapeResourcePhase::Ready) {
        if(!asset.cargoShape.valid()) {
            auto shape=asset.cargo->shape();physics::ShapeResourceError error;
            asset.cargoShape=resources->upload(std::move(shape),error);
            if(!asset.cargoShape.valid()) {
                if(error==physics::ShapeResourceError::Busy||error==physics::ShapeResourceError::NotReady)return true;
                return false;
            }
        }
        if(!asset.cargoBody.valid() && asset.boatRoots->allAdmitted()
            && resources->state(asset.cargoShape)==physics::ShapeResourceState::Ready) {
            const auto base=asset.cargo->primaryMassRoot().buildFromRoot.translation;
            asset.cargoSpawn.position=physics::worldPositionFromAbsolute(asset.origin+glm::dvec3(base.x,base.y,base.z)*.02);
            const auto initialMotion=asset.restorePhysical?asset.cargoObserved:asset.cargoSpawn;
            const auto spawned=physicsWorld_->spawnAuthoredBody({.shape=asset.cargoShape,.motion=initialMotion,
                .motionType=asset.cargoBanked?physics::AuthoredBodyMotionType::Static:physics::AuthoredBodyMotionType::Dynamic});
            if(!spawned) return false;
            asset.cargoBody=spawned.body;asset.cargoObserved=initialMotion;
            if(!asset.cargoBanked && physicsWorld_->configureAuthoredWaterBody(asset.cargo->primaryRoot().water(asset.cargoBody))!=physics::AuthoredBodyError::None) return false;
            bool line=false,eye=false;
            const auto& functions=asset.boat->assembly().functions();
            for(size_t i=0;i<functions.modules().size();++i) {
                if(const auto* winch=std::get_if<game::construction::WinchModule>(&functions.modules()[i].parameters)) {
                    if(line) return false;
                    const auto root=asset.boat->rootForPart(functions.modules()[i].part);if(!root)return false;
                    asset.towRootIndex=*root;
                    asset.towDesc.minimumLength=static_cast<float>(winch->minimumLengthMetres);
                    asset.towDesc.maximumLength=static_cast<float>(winch->maximumLengthMetres);
                    asset.towDesc.maximumForce=static_cast<float>(winch->maximumForceNewtons);
                    asset.towReelSpeed=static_cast<float>(winch->reelSpeedMetresPerSecond)*game::expedition::coveModuleOutput(functions.modules()[i]);
                    for(const auto& frame:functions.moduleFrames(i)) if(frame.kind==game::construction::AssemblyFrameKind::TowLine) {
                        const auto p=frame.rootFromFrame.translation;asset.towBoatPoint=glm::vec3(p.x,p.y,p.z)*.02f;line=true;
                        asset.towDesc.breakForce=static_cast<float>(functions.sockets()[frame.socket].definition.strength.tensionNewtons);
                    }
                }
            }
            const auto& cargoFunctions=asset.cargo->assembly().functions();
            for(const auto& frame:cargoFunctions.frames()) if(frame.kind==game::construction::AssemblyFrameKind::TowEye) {
                if(eye) return false;
                const auto p=frame.rootFromFrame.translation;asset.towCargoPoint=glm::vec3(p.x,p.y,p.z)*.02f;eye=true;
                asset.towDesc.breakForce=std::min(asset.towDesc.breakForce,
                    static_cast<float>(cargoFunctions.sockets()[frame.socket].definition.strength.tensionNewtons));
            }
            physics::AuthoredFrameError error;
            const auto a=physics::AuthoredBodyFrame(asset.boat->roots()[asset.towRootIndex].shape).bodyPoint(asset.towBoatPoint,error);
            const auto b=physics::AuthoredBodyFrame(asset.cargo->shape()).bodyPoint(asset.towCargoPoint,error);
            if((!line && !asset.restorePhysical) || !eye || !a || !b) return false;
            asset.towDesc.bodyA=asset.towRoot().body;asset.towDesc.bodyB=asset.cargoBody;
            asset.towDesc.localAnchorA=*a;asset.towDesc.localAnchorB=*b;
            LOG_INFO("Salvage load active: {} kg, authored winch and tow eye",asset.cargo->primaryMassRoot().mass.dryMassKg);
        }
    }
    if(asset.harbor){
        using Harbor=game::expedition::CoveHarborRuntime;
        using Pause=SalvageLocalSessionState::AssetPreview::Pause;
        if(!asset.leaving&&!asset.harbor->configureBoat(*asset.boat))return false;
        bool cargoAdmissionTick=false;
        if(asset.harbor->stage()==Harbor::Stage::Requested&&asset.pause==Pause::Paused){
            const auto origin=asset.origin;
            const auto parking=asset.harbor->prepareCargoParking(*asset.cargo,*asset.boat,asset.boatRoot().observed,
                *asset.scenery,origin,asset.player->feet(),[this,origin](double x,double z){
                    return double(sampleTerrainHeight(float(x+origin.x),float(z+origin.z)))-origin.y;
                });
            if(!parking)asset.harbor->cancelInstallation("Clear space on the pier before powering the lift.");
            else if(asset.harbor->checkInstallationSpace(*asset.boat,asset.boatRoot().observed,*asset.cargo,*parking,
                    origin+asset.player->feet())){
                const auto placed=physicsWorld_->spawnAuthoredBody({.shape=asset.cargoShape,.motion=*parking,
                    .motionType=physics::AuthoredBodyMotionType::Static});
                if(!placed||!physicsWorld_->destroyBody(asset.cargoBody))return false;
                asset.cargoReplacedBody=asset.cargoBody;asset.cargoReplacementDeadTick=0;
                asset.cargoBody=placed.body;asset.cargoDeliveredPose=*parking;cargoAdmissionTick=true;
            }
        }
        bool needsTick=false;
        // A cargo replacement may already be draining its neutral tick while
        // the gantry upload finishes. Do not enqueue another body behind that
        // paused frontier: join the cargo tick first, then grant gantry admission.
        const bool waitForCargoTick=asset.harbor->installationPending()
            &&asset.harbor->stage()==Harbor::Stage::Uploading&&asset.pause!=Pause::Paused;
        if(!waitForCargoTick&&!asset.harbor->update(*physicsWorld_,asset.boatRoot().body,needsTick)){
            LOG_ERROR("Harbor: {}",asset.harbor->message());return false;
        }
        if((needsTick||cargoAdmissionTick)&&asset.pause==Pause::Paused){
            // Extend the fully drained pause for admission. The scheduler
            // rejects adding final ticks to an already paused world. Reopen
            // here and grant the single neutral tick below in this same call,
            // before ordinary simulation or player input can run.
            if(!physicsWorld_->setSchedulingPaused(false))return false;
            asset.pause=Pause::Requested;asset.pauseTick=0;asset.pauseWaitSeconds=0;
        }
    }
    if(coveResume_ && !coveResume_->staged && asset.boatRoots->allAdmitted() && asset.cargoBody.valid() && asset.sceneryBody.valid()
        &&(!asset.harbor||!asset.harbor->installed()||asset.harbor->body().valid())) {
        if(!asset.restorePhysical)return false;
        if(asset.restorePhysical->cargoState==game::expedition::CoveSavedCargoState::Towed){
            asset.towDesc.targetLength=asset.restorePhysical->ropeLength;asset.towDesc.motorSpeed=0;
            asset.towRope=physicsWorld_->createDistanceAttachment(asset.towDesc);
            if(!asset.towRope.valid())return false;
            asset.towChangedTick=coveResume_->tick+1;
        }
        // Admit the saved bodies and neutral rope in one bounded tick, then
        // join its actual completion evidence before making Resume available.
        coveResume_->staged=true;asset.pause=SalvageLocalSessionState::AssetPreview::Pause::Requested;
    }
    while (auto snapshot=physicsWorld_->pollDebugSnapshot()) {
        if(snapshot->confirmedIncarnation!=physicsWorld_->tickFrontier().incarnation) return false;
        asset.boatObservationPending=false;
        if(asset.harbor&&!asset.harbor->observe(*snapshot)){LOG_ERROR("Harbor observation: {}",asset.harbor->message());return false;}
        for (const auto& body:snapshot->bodies) {
            if(asset.launch && asset.launch->staged && !asset.launch->published) {
                auto& launch=*asset.launch;
                const auto parent=std::find_if(launch.parents.begin(),launch.parents.begin()+launch.parentCount,
                    [&](const auto& old){return old.body.index==body.handle.index;});
                if(parent!=launch.parents.begin()+launch.parentCount) {
                    if(!body.alive && snapshot->tick>=launch.executionTick)parent->deadTick=snapshot->tick;
                    else if(body.alive && (body.handle!=parent->body || body.authoredShape!=parent->shape))return false;
                    continue;
                }
                auto children=launch.roots->roots();
                const auto child=std::find_if(children.begin(),children.end(),[&](const auto& root){return root.body.index==body.handle.index;});
                if(child!=children.end()) {
                    if(!body.alive){if(snapshot->tick>=child->admissionTick)return false;continue;}
                    if(body.handle!=child->body || body.authoredShape!=child->shape)return false;
                    physics::AuthoredFrameError error;const auto index=static_cast<size_t>(child-children.begin());
                    const auto motion=physics::AuthoredBodyFrame(launch.boat->roots()[index].shape).rootMotion({
                        .centerPosition={body.sector,body.position},.orientation=body.orientation,
                        .centerVelocity=body.linearVelocity,.angularVelocity=body.angularVelocity},error);
                    if(!motion)return false;
                    child->observed=*motion;child->observedTick=snapshot->tick;
                    const auto anchor=launch.boat->assembly().mass().roots()[index].buildFromRoot.translation;
                    const auto rootPosition=physics::worldPositionToAbsolute(motion->position)-asset.origin;
                    const auto pose=glm::translate(glm::dmat4(1),rootPosition)
                        *glm::mat4_cast(glm::dquat(motion->orientation))
                        *glm::translate(glm::dmat4(1),-glm::dvec3(anchor.x,anchor.y,anchor.z)*.02);
                    const auto omega=glm::dvec3(motion->angularVelocity);
                    const auto originVelocity=glm::dvec3(motion->originVelocity)+glm::cross(omega,glm::dvec3(pose[3])-rootPosition);
                    if(!launch.player->setBoatRootMotion(child->key,pose,originVelocity,omega,snapshot->tick))return false;
                    continue;
                }
            }
            if (body.handle.index==asset.sceneryBody.index) {
                if (!body.alive && asset.leaving) {
                    asset.sceneryBody={}; asset.sceneryRetired=true;
                    if(resources->retire(asset.sceneryShape)!=physics::ShapeResourceError::None) return false;
                } else if(body.alive && (body.handle!=asset.sceneryBody || body.authoredShape!=asset.sceneryShape)) return false;
                continue;
            }
            if(asset.environmentBody.valid() && body.handle.index==asset.environmentBody.index) {
                // Spawn is queued for scheduled+1; a completed packet from
                // before that tick still describes the previous empty slot.
                // Leave likewise needs post-removal evidence before retiring.
                if(snapshot->tick<asset.environmentAdmissionTick)continue;
                const bool removalDue=asset.environmentRemovalTick
                    &&snapshot->tick>=asset.environmentRemovalTick;
                if(!body.alive && asset.leaving && removalDue) {
                    asset.environmentBody={};asset.environmentRetired=true;asset.environmentObserved=false;
                    asset.environmentAdmissionTick=0;asset.environmentRemovalTick=0;
                    if(resources->retire(asset.environmentShape)!=physics::ShapeResourceError::None)return false;
                } else if(body.alive) {
                    if(removalDue || body.handle!=asset.environmentBody || body.authoredShape!=asset.environmentShape) {
                        LOG_ERROR("Cove environment identity/removal mismatch at tick {}",snapshot->tick);return false;
                    }
                    asset.environmentObserved=true;
                } else {
                    LOG_ERROR("Cove environment missing at tick {}, admitted for {}",snapshot->tick,asset.environmentAdmissionTick);
                    return false;
                }
                continue;
            }
            if(asset.cargoReplacedBody.valid() && body.handle.index==asset.cargoReplacedBody.index) {
                if(!body.alive) {asset.cargoReplacementDeadTick=snapshot->tick;asset.cargoReplacedBody={};}
                else if(body.handle!=asset.cargoReplacedBody) return false;
                continue;
            }
            if(asset.cargo && body.handle.index==asset.cargoBody.index) {
                if(!body.alive && asset.leaving) {
                    asset.cargoBody={};asset.cargoRetired=true;
                    if(resources->retire(asset.cargoShape)!=physics::ShapeResourceError::None) return false;
                } else if(body.alive) {
                    if(body.handle!=asset.cargoBody || body.authoredShape!=asset.cargoShape) return false;
                    physics::AuthoredFrameError error;
                    const auto root=physics::AuthoredBodyFrame(asset.cargo->shape()).rootMotion({
                        .centerPosition={body.sector,body.position},.orientation=body.orientation,
                        .centerVelocity=body.linearVelocity,.angularVelocity=body.angularVelocity},error);
                    if(!root) return false;
                    asset.cargoObserved=*root;asset.cargoObservedTick=snapshot->tick;
                }
                continue;
            }
            auto roots=asset.boatRoots->roots();
            const auto found=std::find_if(roots.begin(),roots.end(),[&](const auto& root) {
                return root.body.valid() && body.handle.index==root.body.index;
            });
            if(found==roots.end())continue;
            auto& owned=*found;const auto index=static_cast<size_t>(found-roots.begin());
            if (!body.alive) {
                if(snapshot->tick<owned.admissionTick)continue;
                if(!asset.leaving)return false;
                owned.body={};owned.retired=true;
                if (resources->retire(owned.shape)!=physics::ShapeResourceError::None) return false;
                continue;
            }
            if (body.handle!=owned.body || body.authoredShape!=owned.shape) return false;
            physics::AuthoredFrameError error;
            const auto root=physics::AuthoredBodyFrame(asset.boat->roots()[index].shape).rootMotion({
                .centerPosition={body.sector,body.position},.orientation=body.orientation,
                .centerVelocity=body.linearVelocity,.angularVelocity=body.angularVelocity},error);
            if (!root) return false;
            owned.observed=*root;owned.observedTick=snapshot->tick;
            const auto anchor=asset.boat->assembly().mass().roots()[index].buildFromRoot.translation;
            const auto initial=glm::dvec3(anchor.x,anchor.y,anchor.z)*.02;
            const auto current=physics::worldPositionToAbsolute(root->position)-asset.origin;
            const auto pose=glm::translate(glm::dmat4(1),current)
                *glm::mat4_cast(glm::normalize(glm::dquat(root->orientation)))*glm::translate(glm::dmat4(1),-initial);
            const auto omega=glm::dvec3(root->angularVelocity);
            const auto velocity=glm::dvec3(root->originVelocity)+glm::cross(omega,glm::dvec3(pose[3])-current);
            if(!asset.player->setBoatRootMotion(owned.key,pose,velocity,omega,snapshot->tick))return false;
        }
        for(const auto& rope:snapshot->attachments) {
            if(asset.rescueRope.valid()&&rope.handle.index==asset.rescueRope.index
                &&snapshot->tick>=asset.rescueRopeTick){
                // Retired GPU slots report generation zero. Require actual
                // post-command dead evidence before forgetting this handle.
                if(!rope.alive&&!rope.broken)asset.rescueRope={};
                else if(rope.handle!=asset.rescueRope)return false;
                continue;
            }
            if(!asset.towRope.valid() || rope.handle.index!=asset.towRope.index
                || snapshot->tick<asset.towChangedTick) continue;
            const auto& desc=rope.distance;
            if(rope.handle!=asset.towRope || (!rope.alive && !rope.broken)
                || desc.bodyA!=asset.towRoot().body || desc.bodyB!=asset.cargoBody
                || !std::isfinite(desc.targetLength) || desc.targetLength<desc.minimumLength
                || desc.targetLength>desc.maximumLength || !std::isfinite(desc.motorSpeed)) return false;
            asset.towObserved=rope;asset.towObservedTick=snapshot->tick;
        }
    }
    while(auto events=physicsWorld_->pollEvents()) {
        const auto frontier=physicsWorld_->tickFrontier();
        if(events->confirmedIncarnation!=frontier.incarnation || events->overflow
            || events->tick!=asset.boatEventsThrough+1 || events->tick>frontier.completed) return false;
        asset.boatEventsThrough=events->tick;
        if(asset.harbor&&!asset.harbor->observe(*events)){LOG_ERROR("Harbor events: {}",asset.harbor->message());return false;}
        for(const auto& event:events->events) {
            if(event.type==physics::PhysicsEventType::AttachmentBreak) {
                ++asset.boatAttachmentBreaks;
                if(event.attachmentHandle()==asset.towRope) {asset.towBroken=true;asset.towMotor=0;}
            }
            if(event.type!=physics::PhysicsEventType::ContactHit||!asset.effectsPath.initialized())continue;
            using Effects=game::expedition::CoveEffects;
            const physics::AuthoredShape* shape=nullptr;const physics::AuthoredRootMotion* motion=nullptr;
            Effects::SourceId source{};bool sourceA=true;
            for(const bool side:{true,false}) {
                const auto body=side?event.bodyHandleA():event.bodyHandleB();
                for(size_t i=0;i<asset.boatRoots->roots().size();++i) {
                    const auto& owned=asset.boatRoots->roots()[i];
                    if(owned.body==body&&body.valid()&&owned.observedTick==event.tick){
                        source={owned.key,body,Effects::SourceKind::Boat};shape=&asset.boat->roots()[i].shape;
                        motion=&owned.observed;sourceA=side;break;
                    }
                }
                if(!shape&&asset.cargo&&body.valid()&&body==asset.cargoBody&&asset.cargoObservedTick==event.tick){
                    source={salvageLocalSession_->cargoId,body,Effects::SourceKind::Cargo};
                    shape=&asset.cargo->shape();motion=&asset.cargoObserved;sourceA=side;
                }
                if(shape)break;
            }
            if(!shape||!motion||!coveEffectsRunning(*salvageLocalSession_)
                ||event.tick!=asset.boatRoots->joinedTick()||event.tick!=asset.player->collisionTick()
                ||(asset.cargo&&asset.cargoObservedTick!=event.tick)
                ||!std::isfinite(event.impactSpeed)||event.impactSpeed<1||event.impactSpeed>300
                ||!std::isfinite(event.impulse)||event.impulse<.1f||event.impulse>1e12f
                ||!waterSimulation_||!waterSimulation_->isInitialized()) {
                countCoveEffectSkip(asset.effectsSkippedContacts);continue;
            }
            physics::AuthoredFrameError anchorError;
            const auto localPoint=physics::AuthoredBodyFrame(*shape).rootPoint(sourceA?event.localAnchorA:event.localAnchorB,anchorError);
            const auto normal=glm::dvec3(sourceA?-event.normalAtoB:event.normalAtoB);
            if(!localPoint||!std::isfinite(glm::length(normal))||std::abs(glm::length(normal)-1)>.001){
                countCoveEffectSkip(asset.effectsSkippedContacts);continue;
            }
            if(asset.effectsImpactTick!=event.tick) {
                countCoveEffectSkip(asset.effectsSkippedContacts,asset.effectsImpactCount);
                asset.effectsImpactCount=0;asset.effectsImpactTick=event.tick;
            }
            if(asset.effectsImpactCount==asset.effectsImpacts.size()){
                countCoveEffectSkip(asset.effectsSkippedContacts);continue;
            }
            const auto absolute=physics::worldPositionToAbsolute(motion->position)
                +glm::dquat(motion->orientation)*glm::dvec3(*localPoint);
            const auto& drive=asset.effectsDrives[event.tick%asset.effectsDrives.size()];
            const float phase=drive.epoch==events->confirmedIncarnation&&drive.tick==event.tick?drive.waterPhase:waterPhaseSeconds();
            const double waterHeight=double(rendererSettings_.waterHeight)+double(waterSimulation_->sampleSurface(
                glm::vec2(absolute.x,absolute.z),phase,rendererSettings_.waterWaveStrength).heightOffset)-asset.origin.y;
            asset.effectsImpacts[asset.effectsImpactCount++]={source,event.tick,
                sourceA?event.bodyHandleB():event.bodyHandleA(),sourceA?event.featureId:event.otherFeatureId,
                sourceA?event.otherFeatureId:event.featureId,absolute-asset.origin,normal,
                static_cast<double>(event.impactSpeed),static_cast<double>(event.impulse),waterHeight};
        }
    }
    if(physicsWorld_->tickFrontier().failed) return false;
    if(asset.harbor&&asset.harbor->attachmentPending()){
        const bool permitted=!asset.leaving&&!asset.workshopOpen&&!asset.player->onBoat()
            &&asset.pause==SalvageLocalSessionState::AssetPreview::Pause::Running
            &&glm::length(asset.player->feet()-asset.content->registry.navigation->dockBoarding)<=4;
        if(!permitted){if(!asset.harbor->stop(*physicsWorld_))return false;}
        else if(!asset.harbor->pollAttachment(*physicsWorld_,asset.boatRoot().body,asset.boatRoot().observed,asset.boatRoot().observedTick))return false;
    }
    if(asset.towAction && !asset.leaving) {
        const int action=std::exchange(asset.towAction,0);
        if(asset.player->onBoat() && asset.player->state().root==asset.towRoot().key && asset.towRoot().observedTick && asset.cargoObservedTick) {
            if(action==40) {
                const bool release=asset.towRope.valid() && !asset.towBroken;
                if(asset.towRope.valid()) {
                    if(!physicsWorld_->destroyAttachment(asset.towRope)) return false;
                    asset.towRope={};asset.towMotor=0;
                }
                if(!release) {
                    const auto a=physics::worldPositionToAbsolute(asset.towRoot().observed.position)+glm::dvec3(asset.towRoot().observed.orientation*asset.towBoatPoint);
                    const auto b=physics::worldPositionToAbsolute(asset.cargoObserved.position)+glm::dvec3(asset.cargoObserved.orientation*asset.towCargoPoint);
                    const double distance=glm::length(b-a);
                    if(distance<=8) {
                        asset.towDesc.targetLength=static_cast<float>(distance)+.1f;
                        asset.towDesc.motorSpeed=0;
                        asset.towRope=physicsWorld_->createDistanceAttachment(asset.towDesc);
                        if(!asset.towRope.valid()) return false;
                        asset.towBroken=false;
                    }
                }
            } else if(asset.towRope.valid() && !asset.towBroken) {
                asset.towMotor=action==41?asset.towReelSpeed:action==42?-asset.towReelSpeed:0;
                if(!physicsWorld_->setAttachmentMotorSpeed(asset.towRope,asset.towMotor)) return false;
            }
            asset.towChangedTick=physicsWorld_->tickFrontier().scheduled+1;
        }
    }
    if(asset.towMotor!=0 && (!asset.player->onBoat()||asset.player->state().root!=asset.towRoot().key)
        && asset.towRope.valid() && !asset.towBroken) {
        if(!physicsWorld_->setAttachmentMotorSpeed(asset.towRope,0)) return false;
        asset.towMotor=0;
    }
    if(!asset.leaving && !asset.practice && salvageLocalSession_->boatId.counter) {
        auto& local=*salvageLocalSession_;auto& session=*local.session;const auto frontier=physicsWorld_->tickFrontier();
        if(frontier.supported && !local.executionBound) {
            if(!session.bindExecution(frontier.incarnation,game::construction::SimulationTick{frontier.baseTick})) return false;
            local.executionBound=true;
        }
        if(local.executionBound) {
            (void)session.confirmExecution(frontier.incarnation,game::construction::SimulationTick{frontier.completed});
            if(!session.executionInFlight() && session.hasPending() && !local.pendingControl)
                (void)session.stageExecution(frontier.incarnation,game::construction::SimulationTick{frontier.scheduled+1});
            if(local.jobRequest && !session.hasPending()) {
                const auto receipt=session.receipt(local.caller,*local.jobRequest);
                if(receipt.state==game::expedition::ReceiptState::Committed) {
                    if(asset.cargoBanked) {
                        asset.checkpointPending=true;asset.checkpointDigest.clear();
                        asset.pause=SalvageLocalSessionState::AssetPreview::Pause::Requested;
                        asset.pauseTick=0;asset.pauseWaitSeconds=0;
                        asset.player->discardPendingInput();if(input_)input_->resetState();
                        asset.jobMessage="Secured. Saving the delivery…";
                    }else asset.jobMessage="Recover the generator. Reel it above the water and return to the harbor.";
                }else if(receipt.state==game::expedition::ReceiptState::Rejected) {
                    asset.checkpointPending=false;asset.checkpointDigest.clear();
                    asset.jobMessage="Bring the generator into the harbor area and slow down before delivering.";
                }
                local.jobRequest.reset();
            }
            if(local.launchRequest && !session.hasPending()) {
                const auto receipt=session.receipt(local.caller,*local.launchRequest);
                if(receipt.state==game::expedition::ReceiptState::Committed) {
                    const bool cut=asset.launch&&asset.launch->fracture.has_value();
                    asset.launchMessage=cut?"Weld cut. All sections remain yours.":"Boat launched. Board and sail.";
                    if(asset.launch&&(asset.launch->ownershipService||cut)) {
                        asset.workshopSavePending=true;asset.checkpointPending=true;asset.checkpointDigest.clear();
                        asset.pause=SalvageLocalSessionState::AssetPreview::Pause::Requested;
                        asset.pauseTick=0;asset.pauseWaitSeconds=0;asset.player->discardPendingInput();
                        asset.launchMessage=cut?"Saving the cut sections and protected design…":"Saving boat and owned parts…";asset.jobMessage=asset.launchMessage;
                    }
                    asset.workshopOpen=false;
                    if(!cut)setCameraWorldPose(*camera_,glm::vec3(asset.origin+asset.player->feet()
                        +glm::dvec3(0,game::expedition::CovePlayer::eyeHeight,0)),
                        glm::vec3(asset.origin+asset.acceptedScene().registry.navigation->lookTarget));
                    if(input_){input_->releaseMouse();input_->resetState();}
                }
                else if(asset.launchMessage=="Preparing boat…"||asset.launchMessage=="Preparing cut…")asset.launchMessage="Change refused. Your build is unchanged.";
                local.launchRequest.reset();
            }
            if(session.journalFaulted()) return false;
        }
    }
    if(!updateCovePractice())return false;
    if(asset.launch && (asset.launch->published || asset.launch->canceled)) {
        auto& launch=*asset.launch;
        bool gone=true;
        for(auto& root:launch.roots->roots()) {
            if(!root.retired) {
                if(root.body.valid() && !launch.published)return false;
                if(root.shape.valid() && resources->retire(root.shape)!=physics::ShapeResourceError::None)return false;
                root.body={};root.retired=true;
            }
            if(root.shape.valid() && resources->state(root.shape)!=physics::ShapeResourceState::Missing)gone=false;
        }
        if(gone)asset.launch.reset();
    }
    using Pause=SalvageLocalSessionState::AssetPreview::Pause;
    if(!asset.leaving && !salvageLocalSession_->pendingControl) {
        const auto frontier=physicsWorld_->tickFrontier();
        if(asset.pause==Pause::Requested && frontier.scheduled==frontier.completed) {
            // No earlier tick is still reading controls. Reserve one final
            // neutral tick, then stop all scheduling while its evidence drains.
            if(!asset.boat->primaryRoot().cells.empty()&&!physicsWorld_->setAuthoredHelm(asset.boatRoot().body,0,0))return false;
            if(asset.harbor&&!asset.harbor->stop(*physicsWorld_))return false;
            if(asset.towRope.valid() && !asset.towBroken) {
                if(!physicsWorld_->setAttachmentMotorSpeed(asset.towRope,0))return false;
                asset.towChangedTick=frontier.scheduled+1;
            }
            asset.towMotor=0;
            if(!physicsWorld_->setSchedulingPaused(true,1))return false;
            asset.pauseTick=frontier.scheduled+1;asset.pause=Pause::Draining;
        } else if(asset.pause==Pause::Draining && frontier.scheduled==asset.pauseTick
            && frontier.completed==asset.pauseTick && asset.boatEventsThrough==asset.pauseTick
            && asset.boatRoots->joinedTick()==asset.pauseTick
            && (!asset.cargo || asset.cargoObservedTick==asset.pauseTick)
            && !asset.cargoReplacedBody.valid()
            && !asset.rescueRope.valid()
            && (!asset.towRope.valid() || asset.towObservedTick==asset.pauseTick)
            && (!asset.harbor||asset.harbor->joined(asset.pauseTick))) {
            const auto& session=*salvageLocalSession_->session;
            if(session.hasPending() || session.executionInFlight()
                || (!asset.practice&&session.snapshot().tick.value()!=asset.pauseTick)){LOG_ERROR("Cove pause session join: snapshot {}, requested {}",session.snapshot().tick.value(),asset.pauseTick);return false;}
            if(asset.towRope.valid() && !asset.towBroken && asset.towObserved.distance.motorSpeed!=0)return false;
            asset.pause=Pause::Paused;
            if(coveResume_ && coveResume_->staged) {
                coveResume_->ready=true;
                if(asset.cargoBanked)asset.deliveryDurable=true;
            }
        }
    }
    if(asset.harbor&&asset.harbor->installationPending()&&asset.pause==Pause::Paused
        &&asset.harbor->joined(asset.pauseTick)&&asset.harbor->installed()){
        if(!asset.checkpointPending){asset.checkpointPending=true;asset.checkpointDigest.clear();asset.jobMessage="Saving the powered harbor lift…";}
    }
    using Rescue=SalvageLocalSessionState::AssetPreview::Rescue;
    if(asset.rescue!=Rescue::None&&asset.rescue!=Rescue::Saving&&asset.pause==Pause::Paused){
        const auto nextTick=[&](){
            const auto frontier=physicsWorld_->tickFrontier();
            if(frontier.scheduled!=frontier.completed||!physicsWorld_->setSchedulingPaused(false)
                ||!physicsWorld_->setSchedulingPaused(true,1))return false;
            asset.pause=Pause::Draining;asset.pauseTick=frontier.scheduled+1;asset.pauseWaitSeconds=0;
            return true;
        };
        if(asset.rescue==Rescue::Requested){
            if(asset.harbor&&!asset.harbor->release(*physicsWorld_))return false;
            if(asset.towRope.valid()){
                if(!physicsWorld_->destroyAttachment(asset.towRope))return false;
                asset.rescueRope=asset.towRope;asset.towRope={};
                asset.rescueRopeTick=physicsWorld_->tickFrontier().scheduled+1;
            }
            asset.towMotor=0;asset.towAction=0;
            asset.rescue=Rescue::Releasing;if(!nextTick())return false;
        }else if(asset.rescue==Rescue::Releasing){
            if(asset.rescueRope.valid()||(asset.harbor&&asset.harbor->hasRopes()))return false;
            const auto refuse=[&](std::string message){
                asset.rescue=Rescue::None;
                asset.jobMessage=std::move(message); // Stay paused; no section was moved.
            };
            const auto frontier=physicsWorld_->tickFrontier();std::string error;
            const auto base=asset.boat->primaryMassRoot().buildFromRoot.translation;
            const auto xz=glm::vec2(asset.origin.x+double(base.x)*.02,asset.origin.z+double(base.z)*.02);
            const auto surface=waterSimulation_->samplePlacementHeight(xz,waterPhaseSeconds(),rendererSettings_.waterWaveStrength);
            if(!surface){refuse("Recovery water surface unavailable. Resume and retry.");return true;}
            physics::AuthoredFrameError frameError;
            const auto origin=physics::translateAuthoredPosition({},asset.origin,frameError);
            const auto recovery=origin?asset.boatRoots->prepareRecovery(*asset.boat,*origin,
                static_cast<double>(*surface),asset.pauseTick,error):std::nullopt;
            if(!recovery){refuse(error.empty()?"Recovery berth unavailable. Resume and retry.":error);return true;}
            std::array<physics::PhysicsCommand,3*(game::expedition::CoveRigidRoots::maximumRoots+1)> commands;
            const auto rootCommands=recovery->bodyCommands();
            std::copy(rootCommands.begin(),rootCommands.end(),commands.begin());size_t count=rootCommands.size();
            const bool moveCargo=!asset.cargoBanked&&!asset.cargoSecuring;
            auto cargoTarget=asset.cargoSpawn;cargoTarget.originVelocity={};cargoTarget.angularVelocity={};
            if(moveCargo){
                if(!asset.cargoBody.valid()||asset.cargoObservedTick!=asset.pauseTick)return false;
                const auto cargo=physics::AuthoredBodyFrame(asset.cargo->shape()).bodyMotion(cargoTarget,frameError);
                if(!cargo){refuse("Cargo recovery position unavailable. Resume and retry.");return true;}
                auto& teleport=commands[count++];teleport.type=physics::PhysicsCommandType::Teleport;teleport.body=asset.cargoBody;
                teleport.sector=cargo->centerPosition.sector;teleport.a=glm::vec4(cargo->centerPosition.local,0);
                teleport.b={cargo->orientation.x,cargo->orientation.y,cargo->orientation.z,cargo->orientation.w};
                auto& velocity=commands[count++];velocity.type=physics::PhysicsCommandType::SetVelocity;velocity.body=asset.cargoBody;
                auto& angular=commands[count++];angular.type=physics::PhysicsCommandType::SetAngularVelocity;angular.body=asset.cargoBody;
            }
            const auto prepared=physicsWorld_->prepareMutationBatch({.bodyCommands={commands.data(),count},
                .joinedBoundary=physics::PhysicsMutationJoin{frontier.incarnation,asset.pauseTick}});
            if(!prepared){refuse("Recovery cannot move all parts yet. Resume and retry.");return true;}
            const auto committed=physicsWorld_->commitPrepared(prepared);
            if(!committed){(void)physicsWorld_->discardPrepared(prepared);refuse("Recovery was not applied. Resume and retry.");return true;}
            if(committed.targetTick!=asset.pauseTick+1||committed.bodyCommandCount!=count)return false;
            for(size_t i=0;i<recovery->count;++i)asset.boatRoots->roots()[i].spawn=recovery->targets[i];
            if(moveCargo)asset.cargoSpawn=cargoTarget;
            asset.towBroken=false;asset.towMotor=0;asset.towAction=0;
            // Publication cannot allocate or change ownership. The next joined
            // observation must confirm every existing body before saving.
            resetSalvagePreviewView();
            asset.rescue=Rescue::Moving;if(!nextTick())return false;
        }else{
            // Capture the actual integrated result, never the queued pose.
            const auto moved=[](const auto& actual,const auto& spawn){
                return glm::length(physics::worldPositionToAbsolute(actual.position)
                    -physics::worldPositionToAbsolute(spawn.position))<.25
                    && std::abs(glm::dot(actual.orientation,spawn.orientation))>.995f;
            };
            if(!std::all_of(asset.boatRoots->roots().begin(),asset.boatRoots->roots().end(),
                [&](const auto& root){return root.observedTick==asset.pauseTick&&moved(root.observed,root.spawn);})
                ||(!asset.cargoBanked&&(asset.cargoObservedTick!=asset.pauseTick
                    ||!moved(asset.cargoObserved,asset.cargoSpawn))))return false;
            asset.rescue=Rescue::Saving;asset.checkpointPending=true;asset.checkpointDigest.clear();
            asset.jobMessage="Boat recovered. Saving your parts and cargo…";
        }
    }
    uint32_t first=UINT32_MAX,last=0;
    asset.boatRoots->includeBodyRange(first,last);
    for(const auto body:{asset.sceneryBody,asset.environmentBody,asset.cargoBody,asset.cargoReplacedBody,
        asset.harbor?asset.harbor->body():physics::BodyHandle{}}) if(body.valid()) {
        first=std::min(first,body.index); last=std::max(last,body.index);
    }
    if(asset.launch) {
        asset.launch->roots->includeBodyRange(first,last);
        for(size_t i=0;i<asset.launch->parentCount;++i) {
            const auto body=asset.launch->parents[i].body;
            if(body.valid()){first=std::min(first,body.index);last=std::max(last,body.index);}
        }
    }
    // Camera/deck observation has one outstanding packet. Submitting one every
    // render frame can exhaust the bounded readback ring while a browser GPU
    // is still executing an earlier frame. Physics continues independently.
    if(last && !asset.boatObservationPending && asset.pause!=Pause::Paused) {
        uint32_t firstRope=UINT32_MAX,lastRope=0;
        if(asset.towRope.valid()){firstRope=asset.towRope.index;lastRope=asset.towRope.index;}
        if(asset.rescueRope.valid()){firstRope=std::min(firstRope,asset.rescueRope.index);lastRope=std::max(lastRope,asset.rescueRope.index);}
        if(asset.harbor)asset.harbor->includeAttachmentRange(firstRope,lastRope);
        physicsWorld_->requestDebugSnapshot({first,last-first+1,lastRope?firstRope:0,lastRope?lastRope-firstRope+1:0});
        asset.boatObservationPending=true;
    }
    if(asset.leaving)for(auto& root:asset.boatRoots->roots())
        if(!root.body.valid() && !root.retired) {
            if(root.shape.valid() && resources->retire(root.shape)!=physics::ShapeResourceError::None)return false;
            root.retired=true;
        }
    if(asset.leaving && asset.cargo && !asset.cargoBody.valid() && !asset.cargoRetired) {
        if(asset.cargoShape.valid() && resources->retire(asset.cargoShape)!=physics::ShapeResourceError::None) return false;
        asset.cargoRetired=true;
    }
    if (asset.leaving && !asset.sceneryBody.valid() && !asset.sceneryRetired) {
        if(asset.sceneryShape.valid() && resources->retire(asset.sceneryShape)!=physics::ShapeResourceError::None) return false;
        asset.sceneryRetired=true;
    }
    if(asset.leaving && !asset.environmentBody.valid() && !asset.environmentRetired) {
        if(asset.environmentShape.valid() && resources->retire(asset.environmentShape)!=physics::ShapeResourceError::None)return false;
        asset.environmentRetired=true;asset.environmentObserved=false;
    }
    if(asset.effectsPath.initialized()&&!coveEffectsRunning(*salvageLocalSession_)) {
        const auto tick=asset.boatRoots->joinedTick();
        if(tick&&(!asset.cargo||asset.cargoObservedTick==tick)&&asset.effects.stats().epoch) {
            // Freeze at service/paused ticks. Clearing source baselines means
            // neither hidden workshop motion nor Rescue becomes a fake splash.
            if(!asset.effects.observe({asset.effects.stats().epoch,tick,false,{},{}}))return false;
            countCoveEffectSkip(asset.effectsSkippedContacts,asset.effectsImpactCount);
            asset.effectsImpactCount=0;asset.effectsImpactTick=0;
        }
    }
    return true;
}
} // namespace voxy

namespace voxy {
void Application::adventureAction(int action,int value){if(adventure_)adventure_->action(action,value);}
std::string Application::adventureJson() const{return adventure_?adventure_->json():"{}";}
bool Application::adventureSnapshot(std::vector<std::byte>& bytes,std::string& error)const{return adventure_&&adventure_->snapshot(bytes,error);}
bool Application::adventureValidateSave(std::span<const std::byte> bytes)const {
    if(!adventure_)return false;
    game::adventure::AdventureState state;std::string error;
    return game::adventure::AdventureSaveCodec::decode(bytes,adventure_->state().world,adventure_->content(),state,error);
}
void Application::adventureSaveCompleted(std::string status){if(adventure_)adventure_->saveCompleted(std::move(status));}
}

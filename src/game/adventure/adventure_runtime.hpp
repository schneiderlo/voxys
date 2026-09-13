#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/expedition/cove_character.hpp"
#include "game/expedition/cove_camera.hpp"
#include "game/expedition/cove_input_preferences.hpp"
#include "render/mesh_path.hpp"
#include "render/cove_hud.hpp"
#include <filesystem>
#include <utility>

namespace voxy { class Input; class Camera; namespace platform {class NativeAdventureSaves;} }
namespace voxy::game::adventure {
// Application owns the terrain and frame. This adapter owns one accepted solo
// adventure; rendering and input never mutate inventory outside session commands.
class AdventureRuntime {
public:
    AdventureRuntime();
    ~AdventureRuntime();
    static bool stageWorld(std::string_view world,std::string_view archiveHex);
    bool initialize(terrain::lego::Surface,WGPUDevice,WGPUQueue,
        const std::filesystem::path& shaderDirectory,WGPUTextureFormat,std::string&);
    void update(double seconds,Input&,Camera&,uint32_t width,uint32_t height);
    bool render(WGPUCommandEncoder,WGPUTextureView color,WGPUTextureView depth,
        WGPUTextureView linearDepth,WGPUTextureView environment,WGPUTextureView rayDepth,
        const Camera&,const render::PrimitiveLighting&,uint32_t width,uint32_t height,
        render::SceneShadowConsumer);
    bool renderHud(WGPUCommandEncoder,WGPUTextureView,uint32_t,uint32_t);
    void action(int action,int value=0);
    std::string json() const;
    bool snapshot(std::vector<std::byte>&,std::string&) const;
    bool restore(std::span<const std::byte>,construction::WorldNamespace,std::string&);
    bool consumeSaveRequest() noexcept {return std::exchange(saveRequested_,false);}
    void saveCompleted(std::string status);
    const AdventureState& state() const {return session_->state();}
    const AdventureContent& content() const {return content_;}
private:
    enum class Menu {None,Main,Catalog,Chest};
    CommandStamp stamp() const;
    CandidateValidator validator() const;
    bool commit(std::optional<AdventureSession::PreparedChange>);
    void updateTarget(const Camera&,const Input&,uint32_t,uint32_t);
    void use();
    void menuRow(int);
    void recover();
    void refreshHud();
    uint64_t nearbyComponent() const;
    std::string interactionLabel() const;
    bool prepareGeometry(const AdventureState&,AdventureSpatialQueries&,std::string&) const;
    std::unique_ptr<AdventureSession> session_;
    AdventureContent content_;
    AdventureSpatialQueries queries_;
    AdventurePlayer player_;
    expedition::CoveCamera orbit_;
    expedition::CoveCharacter character_;
    std::shared_ptr<const assets::RigidAnimationAsset> robot_;
    render::MeshPath meshes_;
    render::CoveHudPath hud_;
    render::CoveHudContent hudContent_;
    uint64_t savedRevision_=0;
    mutable uint64_t savingRevision_=0;
#if defined(VOXY_NATIVE)
    std::unique_ptr<platform::NativeAdventureSaves> saves_;
#endif
    expedition::CoveInputPreferences preferences_;
    expedition::CoveInputRouter inputRouter_,buildRouter_;
    bool uiInputOwned_=false,pendingUiInputOwned_=false,uiOwnershipChanged_=false;
    bool starter_=false;
    uint64_t lastBlueprint_=0,observationSerial_=0;
    glm::dvec3 observedEye_{},observedTarget_{},observedOrigin_{};
    glm::dmat4 observedViewProjection_{1};
    uint32_t observedWidth_=0,observedHeight_=0;
    double observationSeconds_=0;
    glm::dvec2 observedPointer_{},observedAimPointer_{},lastAimPointer_{};
    glm::dvec3 observedRayFrom_{},observedRayTo_{};
    AdventureSpatialQueries::RayHit observedRayHit_{};
    bool padAim_=false,observedMouseCaptured_=false,observedPadConnected_=false;
    bool building_=false,previewValid_=false,hasTarget_=false,saveRequested_=false;
    bool discontinuity_=true;
    PieceKind selected_=PieceKind::Foundation;
    uint8_t yaw_=0;
    int heightSteps_=0,menuSelection_=0;
    Menu menu_=Menu::None;
    uint64_t chest_=0,targetPart_=0,lastPlaced_=0;
    PlacePart preview_{};
    std::string status_="Find a home site. Press B to build.",previewReason_,saveStatus_="Unsaved adventure";
    glm::dvec3 aimPoint_{};
    std::vector<std::pair<int,int>> pendingActions_;
};
}

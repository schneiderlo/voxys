#pragma once
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/building_blueprints.hpp"
#include "game/adventure/adventure_preferences.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/adventure_swim_animation.hpp"
#include "game/adventure/builder_motorbike.hpp"
#include "game/adventure/builder_cannon.hpp"
#include "game/adventure/brick_thrower.hpp"
#include "game/adventure/player_physics_proxy.hpp"
#include "game/adventure/imported_wall_physics.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "game/adventure/adventure_pointer.hpp"
#include "game/adventure/adventure_navigation.hpp"
#include "game/adventure/menu_intent.hpp"
#include "game/adventure/town_residents.hpp"
#include "game/adventure/village_layout.hpp"
#include "game/adventure/creative_scenery.hpp"
#include "game/adventure/adventure_combat.hpp"
#include "game/adventure/trail_sites.hpp"
#include "game/adventure/trail_presentation.hpp"
#include "game/expedition/cove_character.hpp"
#include "game/expedition/cove_camera.hpp"
#include "game/expedition/cove_input_preferences.hpp"
#include "render/mesh_path.hpp"
#include "render/adventure_hud.hpp"
#include <filesystem>
#include <utility>

namespace voxy { class Input; class Camera; namespace platform {class NativeAdventureSaves;} }
namespace voxy::game::adventure {
// Application owns the terrain and frame. This adapter owns one accepted solo
// adventure; rendering and input never mutate inventory outside session commands.
class CannonPhysicsScene;
class AdventureRuntime {
public:
    explicit AdventureRuntime(bool freeBuild=false);
    ~AdventureRuntime();
    static bool stageWorld(std::string_view world,std::string_view archiveHex);
    bool initialize(terrain::lego::Surface,WGPUDevice,WGPUQueue,
        const std::filesystem::path& shaderDirectory,WGPUTextureFormat,std::string&);
    void attachPhysics(physics::PhysicsWorld&);
    bool physicsWaiting() const noexcept;
    void update(double seconds,Input&,Camera&,uint32_t width,uint32_t height,glm::dvec2 logicalPointerExtent);
    bool render(WGPUCommandEncoder,WGPUTextureView color,WGPUTextureView depth,
        WGPUTextureView linearDepth,WGPUTextureView environment,WGPUTextureView rayDepth,
        const Camera&,const render::PrimitiveLighting&,uint32_t width,uint32_t height,
        render::SceneShadowConsumer);
    bool renderHud(WGPUCommandEncoder,WGPUTextureView,uint32_t,uint32_t);
    void action(int action,int value=0);
    std::string preferencesAction(int action,std::string_view text);
    std::string json() const;
    bool snapshot(std::vector<std::byte>&,std::string&) const;
    bool restore(std::span<const std::byte>,construction::WorldNamespace,std::string&);
    bool consumeSaveRequest() noexcept {return std::exchange(saveRequested_,false);}
    void saveCompleted(std::string status);
    bool isPaused() const noexcept { return menu_ != Menu::None; }
    const AdventureState& state() const {return session_->state();}
    const AdventureContent& content() const {return content_;}
    const AdventureSpatialQueries& spatialQueries() const noexcept {return queries_;}
    std::span<const uint32_t> thrownBrickBodyIds() const noexcept {return brickThrower_.bodyIds();}
private:
    enum class Menu {None,Main,Catalog,Chest,Dialogue,Workbench,Journal,Bag,Settings,Controls,CombatBinding,BindingChoice,GuideTopics,Guide,Colours};
    enum class MenuOperation {Close,Catalog,Save,Recover,TextScale,Contrast,Motion,Starter,Journal,
        EquipUtility,CompassTarget,SelectPiece,AcceptQuest,CompleteQuest,CraftHammer,CraftCompass,Transfer,EquipTool,Bag,CraftStaff,AcceptTrailQuest,CompleteTrailQuest,SelectQuest,BuildRecipe,
        Settings,Controls,ChooseCombat,BindingDevice,SetBinding,InvertX,InvertY,OrbitToggle,MouseSensitivity,PadSensitivity,MoveDeadzone,LookDeadzone,ResetPreferences,RetryPreferences,
        GuideTopics,GuideTopic,GuideExit,Paint,Motorbike,Cannon,Undo,Rotate,Raise,Lower,Remove,Colours,FinishBuilding};
    struct MenuCommand {MenuOperation operation;uint64_t argument=0;TransferItems transfer{};};
    struct PendingAction {
        int action=0,value=0;uint32_t menuToken=0;
        uint64_t door=0,doorRevision=0;bool doorOpen=false;
        uint64_t removePart=0;bool removeScenery=false;
    };
    PendingAction observedAction(int action,int value,uint32_t menuToken) const;
    CommandStamp stamp() const;
    CandidateValidator validator() const;
    CandidateValidator npcValidator(uint8_t npc,bool requireHome=false) const;
    bool commit(std::optional<AdventureSession::PreparedChange>);
    void updateTarget(const Camera&,const Input&,uint32_t,uint32_t);
    void use(bool allowDoor=true);
    void useDoor(uint64_t component,bool open,uint64_t expectedRevision);
    void menuRow(int);
    void activateMenuIntent(int);
    bool closeCurrentMode();
    std::string_view mode() const;
    void recover();
    void toggleMotorbike();
    void toggleCannon();
    void toggleColours();
    void resetQuickSlots();
    void selectQuickSlot(size_t);
    void rememberQuickSlot();
    void undoLastPlacement();
    void fireCannon();
    void throwBricks(uint32_t count);
    void updateCannonPhysics();
    void updateImportedWall();
    void changeImportedWall(bool rebuild, bool removeSupport = false);
    bool wallLocked() const noexcept;
    bool cannonVisible() const noexcept;
    void appendCannon(const AdventureState&,std::vector<AdventureSpatialQueries::Solid>&,
        std::vector<AdventureSpatialQueries::Solid>&,bool) const;
    void talk(uint8_t npc);
    void equipCompass(uint8_t slot);
    void craftAtBench(bool compass);
    bool prepareActors(const AdventureState&,const AdventureSpatialQueries&,AdventureSpatialQueries&,AdventureEncounters&,std::string&) const;
    void advanceCombat(double seconds,AdventureCombat::Input);
    uint8_t nearbyLoot() const;
    uint8_t nearbyDiscovery() const;
    bool nearbyRelay() const;
    void cycleCompass();
    void openJournal();
    void openGuide(bool topics,AdventureGuideTopic topic=AdventureGuideTopic::Movement);
    void selectBlueprint(BlueprintKind);
    bool applyPreferences(const AdventurePreferences&,bool force=false);
    std::string combatLabel() const;
    void refreshHud();
    void refreshNavigation();
    uint64_t nearbyComponent() const;
    std::string interactionLabel() const;
    bool prepareGeometry(const AdventureState&,AdventureSpatialQueries&,TownResidents&,VillageLayout&,TrailSites&,CreativeScenery&,std::string&,bool preserveInstalled=true,bool validate=true) const;
    std::unique_ptr<AdventureSession> session_;
    AdventureContent content_;
    AdventureSpatialQueries queries_;
    AdventureSpatialQueries walkQueries_;
    AdventureEncounters encounters_;
    AdventureCombat combat_;
    double combatSeconds_=0;
    bool pendingAttack_=false,pendingDodge_=false,pendingJump_=false;
    TownResidents town_;
    VillageLayout village_;
    CreativeScenery scenery_;
    // Installed set geometry is derived scenery, never player-owned save data.
    // Rendering follows its accepted collision packet, including suppression
    // when a saved build or actor already occupies the plot.
    std::optional<glm::dvec3> blacksmithFeet_;
    bool blacksmithVisible() const noexcept;
    void appendBlacksmith(const AdventureState&,std::vector<AdventureSpatialQueries::Solid>&,
        std::vector<AdventureSpatialQueries::Solid>&,bool preserveInstalled) const;
    physics::PhysicsWorld* physics_=nullptr; // Borrowed from Application, which outlives this runtime.
    std::unique_ptr<CannonPhysicsScene> cannonPhysics_;
    BuilderCannon cannon_;
    BrickThrower brickThrower_;
    PlayerPhysicsProxy playerPhysics_;
    std::optional<ImportedAssemblySource> wallSource_;
    std::unique_ptr<ImportedWallPhysics> wall_;
    uint64_t wallGeometryRevision_=0;
    uint64_t cannonEventsThrough_=0,cannonImpacts_=0;
    uint64_t cannonContacts_=0;
    uint32_t cannonContactFeature_=0,cannonContactBody_=0;
    float cannonContactSpeed_=0,cannonContactImpulse_=0;
    bool cannonEventsReady_=false;
    void updateCannonImpacts();
    bool wallQueryFailure_=false;
    std::optional<glm::dvec3> cannonFeet_;
    bool usingCannon_=false,inspectWall_=false;
    glm::dvec3 wallInspectionPoint_{};
    uint64_t staticGeometryEpoch_=1,cannonObservedEpoch_=0,cannonGeometryEpoch_=1,cannonPreparedEpoch_=0,cannonFailedEpoch_=0;
    std::vector<AdventureSpatialQueries::Solid> cannonRegion_;
    std::string cannonPhysicsError_;
    TrailSites trailSites_;
    // Changes only with accepted static geometry, not every combat/HUD tick.
    FirstHomeReadiness fieldHome_;
    AdventurePlayer player_;
    BuilderMotorbike motorbike_;
    bool riding_=false;
    expedition::CoveCamera orbit_;
    expedition::CoveCharacter character_;
    AdventureSwimAnimation swimAnimation_;
    std::array<expedition::CoveCharacter,3> residentCharacters_;
    std::array<expedition::CoveCharacter,kAdventureEncounterCount> enemyCharacters_;
    std::array<double,3> residentFacing_{};
    double residentRetrySeconds_=0;
    glm::dvec3 residentRetryPlayer_{};
    std::shared_ptr<const assets::RigidAnimationAsset> robot_;
    std::shared_ptr<const assets::RigidAnimationAsset> raider_;
    std::array<std::shared_ptr<const assets::RigidAnimationAsset>,3> residentAssets_;
    render::MeshPath meshes_;
    struct ForestDraw { CreativeProp prop; render::MeshDrawInstance instance; };
    struct ForestTile { glm::dvec3 minimum,maximum; uint32_t first,count; };
    std::vector<ForestDraw> forestDraws_;
    std::vector<ForestTile> forestTiles_;
    std::vector<uint32_t> forestSelection_;
    uint64_t forestDrawEpoch_=0;
    std::shared_ptr<const std::vector<CreativeProp>> forestDrawSource_;
    std::vector<uint64_t> forestAdmissionMask_;
    glm::dvec3 forestDrawOrigin_{};
    uint32_t forestVisited_=0,forestVisible_=0;
    double forestSelectionMs_=0;
    render::AdventureHudPath hud_;
    render::AdventureHudContent hudContent_;
    render::AdventureHudNavigation hudNavigation_;
    AdventureNavigationCache navigationCache_;
    uint64_t navigationCountEpoch_=0;
    uint32_t hudWidth_=0,hudHeight_=0;
    float hudPixelScale_=1;
    double saveFeedbackSeconds_=0,interactionFeedbackSeconds_=0;
    std::optional<render::AdventureHudHit> hudHover_;
    glm::dvec2 hudHoverPointer_{};
    bool hudHoverFromMouse_=true;
    uint64_t savedRevision_=0;
    bool migrationDirty_=false;
    CompassTarget compassTarget_=CompassTarget::Relay;
    uint8_t journalQuest_=1;
    mutable uint64_t savingRevision_=0;
#if defined(VOXY_NATIVE)
    std::unique_ptr<platform::NativeAdventureSaves> saves_;
#endif
    AdventurePreferences preferences_;
    expedition::CoveInputPreferences routingPreferences_;
    AdventureCombatInputRouter combatInput_;
    uint64_t preferencesRevision_=1;
    CombatAction bindingAction_=CombatAction::Attack;
    uint8_t bindingDevice_=0;
    std::filesystem::path preferencesPath_;
    std::string preferencesStatus_="Default settings.";
    expedition::CoveInputRouter inputRouter_,buildRouter_;
    bool uiInputOwned_=false,pendingUiInputOwned_=false,uiOwnershipChanged_=false;
    bool domUiAttached_=false,hasWorldAim_=false,hudPointerOwned_=false;
    glm::dvec2 worldAimNdc_{};
    MenuIntents menuIntents_;
    std::vector<MenuCommand> menuCommands_;
    std::string menuTitle_,menuText_,menuStatus_;
    BlueprintKind blueprint_=BlueprintKind::None;
    uint64_t lastBlueprint_=0,observationSerial_=0,interactionStatusSerial_=0;
    glm::dvec3 observedEye_{},observedTarget_{},observedOrigin_{};
    glm::dmat4 observedViewProjection_{1};
    uint32_t observedWidth_=0,observedHeight_=0;
    double observationSeconds_=0;
    glm::dvec2 observedPointer_{},observedAimPointer_{},lastAimPointer_{};
    std::optional<AdventurePointer> framePointer_;
    glm::dvec3 observedRayFrom_{},observedRayTo_{};
    AdventureSpatialQueries::RayHit observedRayHit_{};
    bool padAim_=false,observedMouseCaptured_=false,observedPadConnected_=false;
    bool freeBuild_=false;
    bool building_=false,previewValid_=false,hasTarget_=false,saveRequested_=false;
    bool discontinuity_=true;
    PieceKind selected_=PieceKind::Foundation;
    uint8_t yaw_=0;
    uint32_t selectedPaint_=0;
    struct QuickSlot {PieceKind kind=PieceKind::Brick2x4;uint32_t paint=0;};
    std::array<QuickSlot,6> quickSlots_{};
    size_t activeQuickSlot_=0;
    float brickScroll_=0;
    uint8_t catalogCategory_=0;
    int heightSteps_=0,menuSelection_=0;
    Menu menu_=Menu::None;
    Menu guideReturnMenu_=Menu::None;
    int guideReturnSelection_=0;
    AdventureGuideTopic guideTopic_=AdventureGuideTopic::Movement;
    bool guideGamepad_=false;
    uint64_t chest_=0,targetPart_=0,lastPlaced_=0;
    uint64_t bench_=0;
    uint8_t dialogueNpc_=0;
    PlacePart preview_{};
    struct PreviewKey {
        construction::WorldNamespace world{};
        uint64_t epoch=0,revision=0,sequence=0,geometryRevision=0,structure=0;
        PieceKind kind=PieceKind::Foundation;
        GridPosition position{};
        uint8_t yaw=0;
        uint32_t paint=0;
        bool operator==(const PreviewKey&) const = default;
    };
    struct PreviewResult {PreviewKey key;bool valid=false;std::string reason;};
    // Only the discarded speculative result is cached. Real edits validate anew.
    std::optional<PreviewResult> previewResult_;
    struct StructureJson {
        construction::WorldNamespace world{};
        uint64_t epoch=0,geometryRevision=0;
        std::string bytes;
    };
    mutable std::optional<StructureJson> structureJson_;
    struct AimRayKey {
        construction::WorldNamespace world{};
        uint64_t epoch=0,geometryRevision=0;
        std::array<uint64_t,7> rayBits{};
        bool operator==(const AimRayKey&) const = default;
    };
    struct AimRayResult {AimRayKey key;AdventureSpatialQueries::RayHit hit;};
    std::optional<AimRayResult> aimRayResult_;
    std::string status_="Find a home site. Press B to build.",previewReason_,saveStatus_="Unsaved adventure",saveFailure_;
    glm::dvec3 aimPoint_{};
    std::vector<PendingAction> pendingActions_;
};
}

#pragma once

#include "game/assets/fixture_registry.hpp"
#include <functional>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::expedition {

// Bounded local character movement over admitted scenery and rigid sections.
// Supported feet use the section frame; airborne feet and velocity are world
// independent. The character never applies an impulse to an owned craft.
class CovePlayer {
public:
    enum class Mode { Walking, Airborne, Swimming, Helm };
    enum class Interaction { None, Board, ReturnToDock, UseHelm, LeaveHelm };
    struct Input { glm::dvec2 movement{0}; bool jump = false; };
    // Semantic resume: feet use authored boat coordinates while aboard, and
    // cove-scene coordinates otherwise. No GPU handles or pending input.
    struct State {
        glm::dvec3 feet{0};
        double verticalSpeed = 0;
        uint64_t tick = 0, interactions = 0;
        Mode mode = Mode::Walking;
        bool onBoat = false;
        construction::DurableId root{}; // The ridden rigid section, never a GPU handle.
        uint32_t locomotionVersion=0; // Zero migrates the original vertical-only state.
        glm::dvec3 worldVelocity{0};
        double facingYaw=0; // Canonical -Z forward, normalized to [-pi,pi].
    };
    using Ground = std::function<double(double, double)>;
    using GroundSupport = std::function<double(double,double,double)>;
    void setGroundSupport(GroundSupport query) {groundSupport_=std::move(query);}
    static constexpr double eyeHeight = 1.55;
    static constexpr double radius = .3;
    static constexpr double height = 1.7;
    static constexpr double fixedStep = 1.0 / 60.0;

    [[nodiscard]] bool initialize(const assets::LoadedAssetFixture& scene, Ground ground,
                                   std::string& error, std::span<const uint32_t> boatSlots = {}, GroundSupport support = {});
    struct StaticObstacle {glm::dvec3 minimum{},maximum{};};
    // Optional harbor obstacles, in Cove coordinates. Replaces at most eleven
    // base solids while preserving the separately admitted environment.
    [[nodiscard]] bool setStaticObstacles(std::span<const StaticObstacle>) noexcept;
    // Replaces at most forty-eight environment solids and preserves the base
    // set. Invalid input leaves every movement/camera obstacle unchanged.
    [[nodiscard]] bool setEnvironmentObstacles(std::span<const StaticObstacle>) noexcept;
    struct SceneObstacle {glm::dvec3 minimum{},maximum{};glm::dmat4 sceneFromObstacle{1};};
    // Complete, stably ordered moving scenery packet (currently recovered cargo).
    // It joins the accepted boat packet; stale geometry is never used by a sweep.
    [[nodiscard]] bool setSceneObstacles(std::span<const SceneObstacle>,uint64_t observedTick) noexcept;
    struct BoatRoot {
        construction::DurableId key{}; glm::dmat4 sceneFromBoat{1};
        glm::dvec3 originVelocity{0},angularVelocity{0};
        uint64_t observedTick=0;
    };
    struct BoatPart { uint32_t placement=0; construction::DurableId part{},root{}; };
    // Complete immutable part ownership and independently moving section poses.
    // Call during preparation; invalid bindings preserve the existing player.
    [[nodiscard]] bool bindBoatRoots(std::span<const BoatRoot>,std::span<const BoatPart>,construction::DurableId helmRoot) noexcept;
    [[nodiscard]] bool setBoatRootTransform(construction::DurableId,const glm::dmat4&) noexcept;
    [[nodiscard]] bool setBoatRootMotion(construction::DurableId,const glm::dmat4&,
        glm::dvec3 originVelocity,glm::dvec3 angularVelocity,uint64_t observedTick) noexcept;
    [[nodiscard]] uint64_t collisionTick() const noexcept {return geometryTick_;}
    struct SweepResult {
        bool complete=false,hit=false,startOverlapped=false;
        double distance=0;
        glm::dvec3 normal{0};
    };
    using TerrainSweep=std::function<SweepResult(glm::dvec3,glm::dvec3,double)>;
    void setTerrainSweep(TerrainSweep query) {terrainSweep_=std::move(query);}
    [[nodiscard]] SweepResult sweepSphere(glm::dvec3 start,glm::dvec3 end,
        double sphereRadius,uint64_t expectedTick=0) const noexcept;
    [[nodiscard]] construction::DurableId supportingPart() const noexcept;
    void reset() noexcept;
    void discardPendingInput() noexcept { accumulator_=0;pendingJump_=pendingInteraction_=false;advancedGeometryTick_=geometryTick_; }
    [[nodiscard]] State state() const noexcept;
    // Initialize the accepted scene and apply its restored boat transform
    // first. Invalid states preserve the entire player, including queued input.
    // Successful resume clears fractional catch-up and unexecuted controls.
    [[nodiscard]] bool restore(const State&);
    void advance(double seconds, Input input);
    [[nodiscard]] bool requestInteraction() noexcept;
    [[nodiscard]] Interaction interaction() const noexcept;
    [[nodiscard]] glm::dvec3 feet() const noexcept {
        return onBoat_ ? glm::dvec3(riderTransform() * glm::dvec4(feet_,1)) : feet_;
    }
    void setBoatTransform(const glm::dmat4& transform) noexcept;
    [[nodiscard]] glm::dvec3 helmPoint() const noexcept { return glm::dvec3(sceneFromBoat_*glm::dvec4(navigation_.helmStanding,1)); }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] uint64_t tick() const noexcept { return tick_; }
    [[nodiscard]] uint64_t interactions() const noexcept { return interactions_; }
    [[nodiscard]] size_t collisionBoxes() const noexcept { return boxes_.size()+staticObstacleCount_+sceneObstacleCount_; }
    [[nodiscard]] bool onBoat() const noexcept { return onBoat_; }
    [[nodiscard]] glm::dvec3 worldVelocity() const noexcept {return worldVelocity_;}
    [[nodiscard]] double facingYaw() const noexcept {return facingYaw_;}
    [[nodiscard]] glm::dvec3 supportNormal() const noexcept {return supportNormal_;}
    [[nodiscard]] static const char* modeName(Mode mode) noexcept;
    [[nodiscard]] static const char* interactionName(Interaction action) noexcept;

private:
    struct Box { glm::dvec3 min{}, max{}; bool boat = false; uint32_t placement=0; uint8_t root=0; construction::DurableId part{}; };
    [[nodiscard]] bool collisionApplies(const Box&) const noexcept;
    [[nodiscard]] const glm::dmat4& riderTransform() const noexcept { return rootCount_?roots_[riderRoot_].sceneFromBoat:sceneFromBoat_; }
    [[nodiscard]] const glm::dmat4& boardingTransform() const noexcept { return rootCount_?roots_[boardingRoot_].sceneFromBoat:sceneFromBoat_; }
    [[nodiscard]] bool clear(glm::dvec3 feet) const noexcept;
    [[nodiscard]] bool supported(glm::dvec3 feet) const;
    [[nodiscard]] bool boatSupport(glm::dvec3 feet) const noexcept;
    [[nodiscard]] double groundHeight(glm::dvec3 feet) const;
    void step(Input input);
    struct Contact {double gap=0;glm::dvec3 normal{0};const Box* box=nullptr;};
    [[nodiscard]] Contact nearestContact(glm::dvec3 worldFeet) const noexcept;
    [[nodiscard]] glm::dvec3 pointVelocity(uint8_t root,glm::dvec3 worldPoint) const noexcept;
    [[nodiscard]] bool sceneClear(glm::dvec3 worldFeet) const noexcept;
    [[nodiscard]] glm::dvec3 moveScene(glm::dvec3 start,glm::dvec3 displacement) const noexcept;
    [[nodiscard]] bool settleSupport(glm::dvec3& worldFeet,double distance,bool allowSnap,
        double maximumSupportHeight=std::numeric_limits<double>::infinity());
    void leaveSupport(glm::dvec3 worldFeet,glm::dvec3 relativeVelocity={0,0,0}) noexcept;
    std::vector<Box> boxes_;
    std::array<StaticObstacle,59> staticObstacles_{};
    size_t staticObstacleCount_=0,baseObstacleCount_=0;
    std::array<SceneObstacle,4> sceneObstacles_{},previousSceneObstacles_{};
    size_t sceneObstacleCount_=0,previousSceneObstacleCount_=0;
    uint64_t sceneObstacleTick_=0;
    bool sceneObstaclePacketBound_=false;
    assets::CoveNavigation navigation_{};
    Ground ground_;
    GroundSupport groundSupport_;
    TerrainSweep terrainSweep_;
    glm::dvec3 feet_{0};
    double verticalSpeed_ = 0, accumulator_ = 0;
    glm::dvec3 worldVelocity_{0},supportNormal_{0,1,0};
    double facingYaw_=0;
    uint64_t tick_ = 0, interactions_ = 0;
    Mode mode_ = Mode::Walking;
    bool onBoat_ = false, pendingJump_ = false, pendingInteraction_ = false;
    bool dynamicBoat_ = false;
    glm::dmat4 sceneFromBoat_{1};
    std::array<BoatRoot,32> roots_{};
    std::array<BoatRoot,32> pendingRoots_{};
    std::array<BoatRoot,32> previousRoots_{};
    uint64_t geometryTick_=0,pendingGeometryTick_=0;
    uint64_t advancedGeometryTick_=0;
    uint32_t pendingRootMask_=0;
    size_t rootCount_=0;
    uint8_t riderRoot_=0,helmRoot_=0,boardingRoot_=0;
};
} // namespace voxy::game::expedition

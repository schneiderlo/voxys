#pragma once

#include "game/assets/fixture_registry.hpp"
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::expedition {

// Local kinematic walking. On the starter craft, collision and walking remain
// deck-local and a bounded observed rigid pose carries the player. Full dynamic
// character reactions, jumps with inherited velocity and camera collision are ACT-03.
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
    };
    using Ground = std::function<double(double, double)>;
    static constexpr double eyeHeight = 1.55;
    static constexpr double radius = .3;
    static constexpr double height = 1.7;
    static constexpr double fixedStep = 1.0 / 60.0;

    [[nodiscard]] bool initialize(const assets::LoadedAssetFixture& scene, Ground ground,
                                   std::string& error, std::span<const uint32_t> boatSlots = {});
    struct StaticObstacle {glm::dvec3 minimum{},maximum{};};
    // Optional installed scenery, in cove coordinates. Replaces the complete
    // bounded set without allocating or changing boat-local collision.
    [[nodiscard]] bool setStaticObstacles(std::span<const StaticObstacle>) noexcept;
    struct BoatRoot { construction::DurableId key{}; glm::dmat4 sceneFromBoat{1}; };
    struct BoatPart { uint32_t placement=0; construction::DurableId part{},root{}; };
    // Complete immutable part ownership and independently moving section poses.
    // Call during preparation; invalid bindings preserve the existing player.
    [[nodiscard]] bool bindBoatRoots(std::span<const BoatRoot>,std::span<const BoatPart>,construction::DurableId helmRoot) noexcept;
    [[nodiscard]] bool setBoatRootTransform(construction::DurableId,const glm::dmat4&) noexcept;
    [[nodiscard]] construction::DurableId supportingPart() const noexcept;
    void reset() noexcept;
    void discardPendingInput() noexcept { accumulator_=0;pendingJump_=pendingInteraction_=false; }
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
    [[nodiscard]] size_t collisionBoxes() const noexcept { return boxes_.size()+staticObstacleCount_; }
    [[nodiscard]] bool onBoat() const noexcept { return onBoat_; }
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
    [[nodiscard]] double moveAxis(glm::dvec3 start, int axis, double distance) const noexcept;
    void step(Input input);
    std::vector<Box> boxes_;
    std::array<StaticObstacle,11> staticObstacles_{};
    size_t staticObstacleCount_=0;
    assets::CoveNavigation navigation_{};
    Ground ground_;
    glm::dvec3 feet_{0};
    double verticalSpeed_ = 0, accumulator_ = 0;
    uint64_t tick_ = 0, interactions_ = 0;
    Mode mode_ = Mode::Walking;
    bool onBoat_ = false, pendingJump_ = false, pendingInteraction_ = false;
    bool dynamicBoat_ = false;
    glm::dmat4 sceneFromBoat_{1};
    std::array<BoatRoot,32> roots_{};
    size_t rootCount_=0;
    uint8_t riderRoot_=0,helmRoot_=0,boardingRoot_=0;
};
} // namespace voxy::game::expedition

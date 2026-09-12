#pragma once

#include "game/expedition/cove_boat.hpp"
#include "physics/authored_shape_resources.hpp"

#include <array>
#include <cassert>

namespace voxy::game::construction { class AssemblyFracturePlan; }

namespace voxy::game::expedition {
class CovePlayer;

// One runtime owner for every physical root of an accepted cove build. GPU
// handles are temporary bindings; durable keys and the build revision select
// roots. The application drains bodies/shapes before destroying this owner.
// It must also retain the corresponding immutable CoveBoatAssembly.
class CoveRigidRoots {
public:
    static constexpr size_t maximumRoots = 32;
    struct Root {
        construction::DurableId key{};
        physics::ShapeHandle shape{};
        physics::BodyHandle body{};
        physics::AuthoredRootMotion spawn{}, observed{};
        uint64_t admissionTick = 0, observedTick = 0;
        bool retired = false;
    };

    [[nodiscard]] static std::unique_ptr<CoveRigidRoots> prepare(
        const CoveBoatAssembly&, std::string& error);
    CoveRigidRoots(const CoveRigidRoots&) = delete;
    CoveRigidRoots& operator=(const CoveRigidRoots&) = delete;

    // Retry only unsubmitted prepared shapes after the caller polls resources.
    // None certifies every shape is Ready; Busy/NotReady retain preparation.
    // Every accepted handle stays owned here, including on terminal failure;
    // the caller cancels/drains it through the existing retirement path.
    // No body admission, polling, staging, allocation or payload copies occur.
    [[nodiscard]] physics::ShapeResourceError prepareShapes(
        physics::IAuthoredShapeResources&, std::span<physics::AuthoredShape>) noexcept;

    [[nodiscard]] bool matches(const CoveBoatAssembly&) const noexcept;
    [[nodiscard]] std::span<Root> roots() noexcept { return {roots_.data(), count_}; }
    [[nodiscard]] std::span<const Root> roots() const noexcept { return {roots_.data(), count_}; }
    [[nodiscard]] Root& primary() noexcept { assert(primary_ < count_); return roots_[primary_]; }
    [[nodiscard]] const Root& primary() const noexcept { assert(primary_ < count_); return roots_[primary_]; }
    [[nodiscard]] construction::DurableId build() const noexcept { return build_; }
    [[nodiscard]] construction::TopologyRevision revision() const noexcept { return revision_; }
    [[nodiscard]] std::optional<size_t> indexForKey(construction::DurableId) const noexcept;
    [[nodiscard]] std::optional<size_t> indexForPart(
        const CoveBoatAssembly&, construction::DurableId) const noexcept;
    // Only a common complete observation can become an archive/mutation input.
    // Incarnation/fence/event certification remains the application boundary.
    [[nodiscard]] uint64_t joinedTick() const noexcept;
    [[nodiscard]] bool allAdmitted() const noexcept;
    [[nodiscard]] bool allRetired() const noexcept;
    void includeBodyRange(uint32_t& first, uint32_t& last) const noexcept;
    // Bind collision/rider ownership and every observed root pose to a prepared
    // local player. Does not invent a completed tick or activate physics.
    [[nodiscard]] bool bindPlayer(CovePlayer&,const CoveBoatAssembly&,glm::dvec3 origin,std::string& error) const;

    static constexpr double cutterReachMetres=2;
    struct CutTarget { construction::DurableId weld{},partA{},partB{}; double distance=0; };
    // Physical socket positions from a common observation. An exact selection
    // rechecks that weld at staging instead of silently choosing another one.
    [[nodiscard]] std::optional<CutTarget> reachableWeld(const CoveBoatAssembly&,
        glm::dvec3 absoluteHand,std::optional<construction::DurableId> exact={}) const noexcept;
    // Prepared destination poses must already inherit the source's motion.
    // The rider follows their supporting part; losing the helm leaves Walking.
    [[nodiscard]] bool transferCutPlayer(CovePlayer& destination,const CoveBoatAssembly&,
        const CovePlayer& source,glm::dvec3 origin,std::string& error) const;

    struct Recovery {
        std::array<physics::AuthoredRootMotion,maximumRoots> targets{};
        std::array<physics::PhysicsCommand,3*maximumRoots> commands{};
        size_t count=0;
        uint64_t sourceTick=0;
        [[nodiscard]] std::span<const physics::PhysicsCommand> bodyCommands() const noexcept {return {commands.data(),3*count};}
    };
    // Prepare the complete upright accepted layout at the home water datum.
    // Every section receives its own mass-frame teleport and neutral velocity.
    // Source observations must be joined; no owner state or GPU queue changes.
    // The caller releases cables and reserves/commits this entire command set
    // (plus eligible cargo) before publishing targets or acknowledging a save.
    [[nodiscard]] std::optional<Recovery> prepareRecovery(const CoveBoatAssembly&,
        const physics::WorldPosition& sceneOrigin,double waterHeightOffset,
        uint64_t sourceTick,std::string& error) const;

    // Stage every child's inherited origin motion from one joined parent set.
    // Failure leaves all destination motions unchanged. Does not admit bodies,
    // retire parents, authorize cuts or certify GPU fences/events for the caller.
    [[nodiscard]] bool inheritFractureMotion(const CoveBoatAssembly& destination,
        const construction::AssemblyFracturePlan&, const CoveBoatAssembly& source,
        const CoveRigidRoots& sourceRoots, uint64_t completedTick, std::string& error);

private:
    CoveRigidRoots() = default;
    construction::DurableId build_{};
    construction::TopologyRevision revision_{};
    std::array<Root, maximumRoots> roots_{};
    size_t count_ = 0, primary_ = 0;
};

} // namespace voxy::game::expedition

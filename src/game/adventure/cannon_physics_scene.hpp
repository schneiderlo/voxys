#pragma once

#include "game/adventure/spatial_queries.hpp"
#include "physics/authored_body_frame.hpp"
#include "physics/authored_shape_resources.hpp"
#include "physics/physics_world.hpp"

#include <optional>
#include <string>
#include <vector>

namespace voxy::game::adventure {

// Session-owned bridge for the complete accepted cannon collision region.
// Application still owns the one PhysicsWorld. Call update before scheduling
// or opening a GPU submission. The owner must stop firing and retire existing
// shots before requesting a new revision. No partial packet is ever ready.
class CannonPhysicsScene {
public:
    static constexpr size_t maximumPartitions = 96;
    static constexpr size_t maximumBoxesPerPartition = 128;
    // Complete scene ceiling, independent of the world's GPU allocation. The
    // scene pool below reserves old + replacement plus a separately owned wall.
    static constexpr uint32_t maximumCells = 12000;
    static constexpr uint32_t maximumFaces = 72000;
    static constexpr uint32_t maximumNodes = 24000;
    [[nodiscard]] static physics::ShapeResourceLimits resourceLimits() noexcept;
    struct Partition {
        glm::dvec3 origin{}; // Absolute root origin; geometry is local.
        physics::AuthoredShape shape;
        glm::quat orientation{1,0,0,0};
        bool dynamic = false;
        glm::vec3 originVelocity{}, angularVelocity{}; // World-axis root motion, not COM motion.
    };
    struct Packet {
        uint64_t revision = 0;
        size_t solidCount = 0;
        physics::ShapeResourceCost cost{};
        std::vector<Partition> partitions;
        // Ownership transfers only when prepareAuthored succeeds. Retired in
        // the same joined mutation as old roots, or during explicit cleanup.
        std::optional<physics::BodyHandle> consumeBody;
    };
    enum class Progress { Waiting, Ready, Failed };

    // CPU-only, complete and deterministic. Positions are made local before
    // quantization to the existing .02-unit authored lattice. Boundaries expand
    // by less than one tick; gaps wider than two ticks remain open. Sources are
    // one-based input indices, not invented LDraw part identities.
    [[nodiscard]] static std::optional<Packet> compile(
        std::span<const AdventureSpatialQueries::Solid>, glm::dvec3 sceneOrigin,
        uint64_t revision, std::string& error);
    // Refuses stale/invalid revisions without changing the old accepted scene.
    // A transition already submitted must complete before another is prepared.
    [[nodiscard]] bool prepare(std::span<const AdventureSpatialQueries::Solid>,
        glm::dvec3 sceneOrigin, uint64_t revision, std::string& error);
    // Already compiled imported sections use the same complete reservation,
    // replacement and retirement protocol. Initial dynamic motion is converted
    // from root to COM/principal frame once by typed backend admission.
    [[nodiscard]] bool prepareAuthored(Packet, std::string& error);
    [[nodiscard]] Progress update(physics::PhysicsWorld&, std::string& error);
    [[nodiscard]] bool ready(uint64_t revision) const noexcept;
    // Pause new scheduling while this is true so outstanding GPU work can join.
    // Once false and waitingForExecution true, resume ticks to execute admission.
    [[nodiscard]] bool needsQuiescentBoundary() const noexcept {
        return !executionTick_ && (!failed_ || clearing_)
            && (packet_.has_value() || (clearing_ && (!live_.empty() || retirementBody_.valid())));
    }
    [[nodiscard]] bool waitingForExecution() const noexcept { return executionTick_ != 0; }
    [[nodiscard]] uint64_t acceptedRevision() const noexcept { return acceptedRevision_; }
    [[nodiscard]] size_t bodyCount() const noexcept { return live_.size(); }
    [[nodiscard]] physics::BodyHandle body(size_t index) const noexcept {
        return index<live_.size()?live_[index].body:physics::BodyHandle{};
    }
    [[nodiscard]] physics::BodyHandle pendingBody(size_t index) const noexcept {
        return index<next_.size()?next_[index].body:physics::BodyHandle{};
    }
    [[nodiscard]] uint64_t pendingExecutionTick() const noexcept { return clearingSubmitted_?0:executionTick_; }
    [[nodiscard]] uint64_t pendingRevision() const noexcept { return packet_ ? packet_->revision : 0; }

    // Stop firing first. Repeated update calls plus ordinary GPU ticks finish
    // retirement without blocking. Keep this owner and world alive until empty().
    void requestClear() noexcept { clearing_ = true; }
    [[nodiscard]] bool empty() const noexcept;
    // Only after the owning PhysicsWorld has shut down/device-abandoned its pool.
    void abandonAfterWorldShutdown() noexcept;
private:
    struct Owned { physics::ShapeHandle shape{}; physics::BodyHandle body{}; };
    [[nodiscard]] bool retireShapes(physics::IAuthoredShapeResources&,
        std::vector<Owned>&, std::string& error);
    std::optional<Packet> packet_;
    std::vector<Owned> live_, next_;
    physics::BodyHandle retirementBody_{};
    uint64_t acceptedRevision_ = 0, executionTick_ = 0, incarnation_ = 0;
    bool clearing_ = false, failed_ = false, clearingSubmitted_ = false;
};

} // namespace voxy::game::adventure

#pragma once

#include "physics/physics_types.hpp"
#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace voxy::physics { class PhysicsWorld; }

namespace voxy::game::expedition {

// A disposable authored scene, with no inventory, rewards, or GameSession.
// The caller owns the physics world and must outlive this fixture owner.
class SalvagePreview {
public:
    static constexpr uint32_t MaximumBodies = 64u;
    static constexpr uint32_t PreviewBodyBudget = 1024u;
    enum class Phase { Empty, Ready, Removing, AwaitingRetirement, Failed };
    enum class Action { Reset = 1, Leave = 2 };
    enum class Scenery { Cove, Inspection };
    struct BodyAccess {
        std::function<physics::BodyHandle(const physics::BodySpawnDesc&)> spawn;
        std::function<bool(physics::BodyHandle)> destroy;
    };
    struct SnapshotRequest { uint64_t revision; uint32_t bodyCount; };

    SalvagePreview() = default;
    ~SalvagePreview();
    SalvagePreview(const SalvagePreview&) = delete;
    SalvagePreview& operator=(const SalvagePreview&) = delete;

    [[nodiscard]] bool initialize(physics::PhysicsWorld& world, glm::vec3 origin,
                                  Scenery scenery = Scenery::Cove);
    // Narrow body port enables failure/ownership tests without a GPU.
    [[nodiscard]] bool initialize(BodyAccess access, glm::vec3 origin,
                                  Scenery scenery = Scenery::Cove);
    [[nodiscard]] bool request(Action action);
    void update();
    // Copy real metadata AFTER physics in the frame command buffer. Only a
    // completed mapping may be passed back; encodedTick is not completion.
    [[nodiscard]] std::optional<SnapshotRequest> retirementSnapshot() const;
    void observeRetirement(uint64_t revision, std::span<const std::byte> metadata);
    // Best-effort owner teardown, also valid before any GPU submission. A false
    // result requires the enclosing PhysicsWorld owner to tear down its world.
    [[nodiscard]] bool shutdown();

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] uint32_t bodyCount() const noexcept { return count_; }
    [[nodiscard]] uint32_t resetCount() const noexcept { return resets_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] glm::vec3 origin() const noexcept { return origin_; }
    [[nodiscard]] std::string_view error() const noexcept { return error_; }
    [[nodiscard]] bool busy() const noexcept {
        return phase_ == Phase::Removing || phase_ == Phase::AwaitingRetirement;
    }
    [[nodiscard]] std::span<const uint32_t> legoBodyIds() const noexcept {
        return std::span(legoIds_).first(legoCount_);
    }

private:
    [[nodiscard]] bool spawnScene();
    void completeRemoval();
    BodyAccess access_;
    Scenery scenery_ = Scenery::Cove;
    glm::vec3 origin_{0};
    std::array<physics::BodyHandle, MaximumBodies> handles_{};
    std::array<bool, MaximumBodies> destroyAccepted_{};
    std::array<uint32_t, MaximumBodies> legoIds_{};
    uint32_t count_ = 0, legoCount_ = 0, resets_ = 0;
    uint64_t revision_ = 0;
    Phase phase_ = Phase::Empty;
    std::optional<Action> pending_;
    bool leaveAfterRetirement_ = false;
    std::string_view error_;
};

} // namespace voxy::game::expedition

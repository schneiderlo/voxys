#pragma once

#include "game/adventure/cannon_physics_scene.hpp"
#include "game/adventure/imported_assembly.hpp"

namespace voxy::game::adventure {

// Source-aware manual cuts and certified projectile impacts. GPU debris is
// frozen only after sleeping poses are certified, before publishing walking geometry.
// Owns a separate body set: scenery revisions cannot respawn fallen parts.
class ImportedWallPhysics {
public:
    struct Binding {
        uint64_t sourceId = 0;
        uint32_t meshNode = 0;
        glm::dmat4 localMatrix{1}; // Mesh -> authored root; renderer applies live body once.
        physics::BodyHandle body{};
    };
    struct Cell { uint64_t sourceId=0; glm::dvec3 minimum{},maximum{}; };
    enum class Phase { Empty, Installing, Intact, Releasing, Falling, Joining, Certifying, Freezing, Settled, Resetting, Failed, Recovering, Clearing };
    [[nodiscard]] bool initialize(const ImportedAssemblySource&, glm::dvec3 origin,
        glm::quat orientation, std::string& error);
    // Optional manual support removal is limited to a current, validated
    // supported part. Renderer bindings and collision omit the same source ID.
    [[nodiscard]] bool release(std::string& error, std::optional<uint64_t> removePart = {});
    // Caller supplies a certified ContactHit and transfers projectile ownership
    // only on success. localPoint uses the event's COM/principal body frame.
    struct Impact {
        uint64_t geometryRevision = 0;
        physics::BodyHandle target{}, projectile{};
        uint32_t targetFeature = 0;
        glm::vec3 targetLocalPoint{}, direction{};
        double normalImpulse = 0, closingSpeed = 0, projectileMass = 0, projectileEnergy = 0;
    };
    struct ImpactStats {
        uint64_t count = 0, sourceId = 0;
        size_t releasedParts = 0;
        double energyBudget = 0, addedEnergy = 0, rotationalEnergy = 0;
        glm::dvec3 worldPoint{}, worldDirection{};
    };
    [[nodiscard]] std::optional<uint64_t> contactPart(physics::BodyHandle, uint32_t feature) const noexcept;
    [[nodiscard]] bool impact(const Impact&, std::string& error);
    [[nodiscard]] const ImpactStats& impactStats() const noexcept { return impactStats_; }
    // Certified source-pivot movement, independent of collision proxy changes.
    [[nodiscard]] double maximumPartDisplacement() const noexcept;
    [[nodiscard]] bool reset(std::string& error);
    [[nodiscard]] bool update(physics::PhysicsWorld&, std::string& error);
    void requestClear() noexcept;
    [[nodiscard]] bool empty() const noexcept { return phase_==Phase::Empty; }
    [[nodiscard]] bool active() const noexcept { return original_.has_value(); }
    [[nodiscard]] bool ready() const noexcept { return phase_==Phase::Intact||phase_==Phase::Settled; }
    [[nodiscard]] bool released() const noexcept { return changed_; }
    [[nodiscard]] bool busy() const noexcept { return active()&&!ready(); }
    [[nodiscard]] bool needsQuiescentBoundary() const noexcept;
    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] std::span<const Binding> bindings() const noexcept { return bindings_; }
    // The render pass follows physics encoding. Its view may use newly admitted
    // handles before CPU completion, while gameplay/query publication still waits.
    [[nodiscard]] std::span<const Binding> bindingsForEncodedTick(uint64_t tick) const noexcept;
    // Only accepted intact/settled cells, never an old snapshot of moving bodies.
    [[nodiscard]] std::span<const Cell> initialSolids() const noexcept { return initialCells_; }
    [[nodiscard]] std::span<const Cell> settledSolids() const noexcept { return ready()?std::span<const Cell>(cells_):std::span<const Cell>{}; }
    [[nodiscard]] uint64_t geometryRevision() const noexcept { return publishedRevision_; }
    [[nodiscard]] const std::string& message() const noexcept { return failure_; }
    void abandonAfterWorldShutdown() noexcept;
private:
    [[nodiscard]] bool stage(ImportedAssembly, std::vector<physics::AuthoredRootMotion>,
        std::vector<bool>, Phase, std::string&, std::optional<physics::BodyHandle> consumeBody = {});
    [[nodiscard]] std::vector<physics::AuthoredRootMotion> initialMotion(const ImportedAssembly&) const;
    void publish();
    void cachePendingBindings();
    [[nodiscard]] bool observe(const physics::DebugSnapshot&, bool exact, const physics::PhysicsTickFrontier&,
        std::vector<physics::AuthoredRootMotion>&) const;
    void requestObservation(physics::PhysicsWorld&);
    CannonPhysicsScene scene_;
    std::optional<ImportedAssembly> original_, accepted_, pending_;
    glm::dvec3 origin_{};
    glm::quat orientation_{1,0,0,0};
    std::vector<physics::AuthoredRootMotion> motions_, pendingMotions_;
    std::vector<bool> dynamic_, pendingDynamic_;
    std::vector<Binding> bindings_,pendingBindings_;
    std::vector<Cell> cells_,initialCells_;
    Phase phase_=Phase::Empty;
    uint64_t revision_=0,publishedRevision_=0,releaseTick_=0,certificationTick_=0,pendingBindingTick_=0;
    bool changed_=false,observationPending_=false,sceneFailed_=false;
    std::string failure_;
    ImpactStats impactStats_{};
};
} // namespace voxy::game::adventure

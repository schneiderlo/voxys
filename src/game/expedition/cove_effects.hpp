#pragma once

#include "game/construction/construction_types.hpp"
#include "physics/physics_types.hpp"
#include <array>
#include <cstdint>
#include <span>

namespace voxy::game::expedition {

// Ephemeral, bounded presentation only. Inputs carry completed observations;
// this owner cannot issue a command or change a part, cargo, inventory or save.
class CoveEffects {
public:
    static constexpr size_t maximumInstances=512, maximumSources=40, maximumImpacts=32;
    static constexpr uint64_t maximumTickGap=15;
    enum class Kind : uint32_t { Wake=0, Foam=1, Splash=2, Drain=3, Dust=4 };
    enum class SourceKind : uint8_t { Boat, Cargo, Player };
    struct SourceId {
        construction::DurableId owner{};
        physics::BodyHandle body{}; // Player alone has no physics body.
        SourceKind kind=SourceKind::Boat;
        [[nodiscard]] bool operator==(const SourceId&) const = default;
    };
    struct MotionSource {
        SourceId id{};
        glm::dvec3 point{},pointVelocity{}; // Cove scene metres/world-axis m/s.
        double waterHeight=0,footprintRadius=.5;
        // The point is an authored surface/keel sample, not a guessed COM.
        // Water heights are the explicitly approximate cosmetic CPU sampler.
        bool waterContact=false;
        double effectiveDrive=0;
        bool hasPropeller=false;
        glm::dvec3 propellerPoint{},propellerVelocity{};
        double propellerWaterHeight=0;
    };
    struct Impact {
        SourceId source{};
        uint64_t tick=0; // Must equal Inputs.tick and have its exact paired pose.
        physics::BodyHandle otherBody{}; // Zero is the terrain; preserves distinct contacts.
        uint32_t feature=0,otherFeature=0;
        glm::dvec3 point{},normal{0,1,0};
        double speed=0,impulse=0,waterHeight=0;
    };
    struct Inputs {
        uint64_t epoch=0,tick=0; // Physics incarnation and completed coherent tick.
        bool running=true; // False freezes age/emission and rebases observations.
        std::span<const MotionSource> sources{};
        std::span<const Impact> impacts{};
    };
    // Four vec4s are also the GPU instance ABI. Position is scene-local.
    // sizeLife = full width, full height, lifetime seconds, world-Y angle.
    struct alignas(16) Instance {
        glm::vec4 positionKind{},velocityAge{},sizeLife{},color{};
        [[nodiscard]] bool operator==(const Instance&) const = default;
    };
    struct Stats {
        uint64_t epoch=0,tick=0,emitted=0,dropped=0,rejected=0,discontinuities=0;
        uint64_t playerEntrySplashes=0; // Actual admitted sprites from player water crossings only.
        std::array<uint64_t,5> emittedByKind{};
        uint32_t active=0,highWater=0;
    };
    [[nodiscard]] bool reset(uint64_t epoch,uint64_t tick) noexcept;
    // Invalid packets refuse atomically apart from the rejection counter.
    // Duplicate ticks never replay events. Gaps >15 ticks clear transient
    // state and establish a new baseline; they do not emit a catch-up burst.
    [[nodiscard]] bool observe(const Inputs&) noexcept;
    [[nodiscard]] std::span<const Instance> instances() const noexcept {return {instances_.data(),count_};}
    [[nodiscard]] const Stats& stats() const noexcept {return stats_;}
private:
    struct Baseline { MotionSource source{}; double wakeCredit=0,foamCredit=0; };
    std::array<Instance,maximumInstances> instances_{};
    std::array<float,maximumInstances> waterAtBirth_{};
    std::array<Baseline,maximumSources> baselines_{};
    size_t count_=0,baselineCount_=0;
    uint32_t random_=0x43564658u;
    construction::WorldNamespace world_{};
    bool worldBound_=false;
    Stats stats_{};
    [[nodiscard]] float random() noexcept;
    void emit(Kind,glm::dvec3 point,glm::dvec3 velocity,double waterHeight,
              float width,float height,float lifetime,float angle,glm::vec4 color) noexcept;
    void burst(Kind,glm::dvec3 point,glm::dvec3 velocity,glm::dvec3 normal,
               double waterHeight,uint32_t count,double strength) noexcept;
    void age(double seconds) noexcept;
};
static_assert(sizeof(CoveEffects::Instance)==64);

} // namespace voxy::game::expedition

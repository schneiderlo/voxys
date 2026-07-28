#pragma once

#include "physics/physics_types.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec3.hpp>

namespace voxy::physics {

// The first GPU milestone deliberately keeps character interaction off the
// GPU critical path. Nearby dynamic bodies can be added through an async
// mirror later; no policy is allowed to trigger a whole-world sync readback.
enum class NearbyDynamicBodyPolicy : uint8_t {
    TerrainOnly,
    AsyncQueryMirror,
    CpuAuthoritativeSet,
};

struct CharacterTerrainSample {
    float height = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t featureId = 0;
    bool valid = false;
};

class CpuCapsuleMoverWorld {
public:
    struct Config {
        uint32_t maximumCharacters = 64;
        uint32_t maximumPlanes = 4;
        uint32_t maximumCastSamples = 32;
        uint32_t bisectionIterations = 8;
        float skin = 0.005f;
        NearbyDynamicBodyPolicy nearbyDynamicPolicy =
            NearbyDynamicBodyPolicy::TerrainOnly;
    };

    CpuCapsuleMoverWorld();
    ~CpuCapsuleMoverWorld();
    CpuCapsuleMoverWorld(const CpuCapsuleMoverWorld&) = delete;
    CpuCapsuleMoverWorld& operator=(const CpuCapsuleMoverWorld&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool initialize(const Config& config);
    void shutdown();

    [[nodiscard]] bool setTerrain(std::span<const uint16_t> samples,
                                  uint32_t width, uint32_t height,
                                  float heightScale, float cellScale);
    void clearTerrain();
    [[nodiscard]] bool hasTerrain() const noexcept;

    [[nodiscard]] CharacterHandle createCharacter(
        const glm::vec3& feetPosition, const CharacterSettings& settings);
    [[nodiscard]] CharacterHandle createCharacter(
        const WorldPosition& feetPosition, const CharacterSettings& settings);
    void destroyCharacter(CharacterHandle handle);
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle, const glm::vec3& feetPosition);
    [[nodiscard]] bool setCharacterPosition(
        CharacterHandle handle, const WorldPosition& feetPosition);
    [[nodiscard]] CharacterMotion moveCharacter(
        CharacterHandle handle,
        const glm::vec3& desiredHorizontalVelocity,
        bool jump, float jumpSpeed, float gravity, float terminalVelocity,
        float deltaTime);

    [[nodiscard]] CharacterTerrainSample sampleTerrain(
        float worldX, float worldZ) const noexcept;
    [[nodiscard]] NearbyDynamicBodyPolicy nearbyDynamicPolicy()
        const noexcept { return config_.nearbyDynamicPolicy; }

private:
    struct CharacterSlot;
    struct CapsuleClearance;
    struct CastHit;

    [[nodiscard]] CharacterSlot* find(CharacterHandle handle) noexcept;
    [[nodiscard]] const CharacterSlot* find(
        CharacterHandle handle) const noexcept;
    [[nodiscard]] CapsuleClearance capsuleClearance(
        const glm::vec3& feetPosition, const glm::ivec3& referenceSector,
        float radius) const noexcept;
    [[nodiscard]] CastHit castCapsule(
        const glm::vec3& start, const glm::vec3& translation,
        const glm::ivec3& referenceSector, float radius) const noexcept;
    [[nodiscard]] CharacterTerrainSample sampleTerrainInFrame(
        float localX, float localZ,
        const glm::ivec3& referenceSector) const noexcept;

    Config config_{};
    std::vector<uint16_t> terrainSamples_;
    uint32_t terrainWidth_ = 0;
    uint32_t terrainHeight_ = 0;
    float terrainHeightScale_ = 0.0f;
    float terrainCellScale_ = 0.0f;
    std::vector<CharacterSlot> characters_;
    std::vector<uint32_t> freeCharacterSlots_;
    bool initialized_ = false;
};

} // namespace voxy::physics

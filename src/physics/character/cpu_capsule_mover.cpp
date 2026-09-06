#include "physics/character/cpu_capsule_mover.hpp"

#include "physics/terrain_topology.hpp"
#include "terrain/lego_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace voxy::physics {
namespace {

bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

// Direct mover users rely on coarse, half-second deterministic steps in tools
// and tests. Keep that supported while preventing unbounded displacement from
// malformed frame input.
constexpr float kMaximumFrameTime = 1.0f;
constexpr float kMaximumCharacterRate = 10'000.0f;
constexpr float kMaximumTerrainScale = 1.0e6f;
constexpr uint32_t kMaximumCharacters = 65'535u;
constexpr uint32_t kMaximumTerrainExtent = 8'192u;

bool validPolicy(NearbyDynamicBodyPolicy policy) noexcept {
    return policy == NearbyDynamicBodyPolicy::TerrainOnly
        || policy == NearbyDynamicBodyPolicy::AsyncQueryMirror
        || policy == NearbyDynamicBodyPolicy::CpuAuthoritativeSet;
}

} // namespace

struct CpuCapsuleMoverWorld::CharacterSlot {
    glm::vec3 position{0.0f};
    glm::ivec3 sector{0};
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    CharacterSettings settings{};
    uint16_t generation = 0;
    bool active = false;
    bool grounded = false;
    bool steep = false;
};

struct CpuCapsuleMoverWorld::CapsuleClearance {
    float distance = std::numeric_limits<float>::max();
    float requiredFeetHeight = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t featureId = 0;
    bool valid = false;
};

struct CpuCapsuleMoverWorld::CastHit {
    float fraction = 1.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t featureId = 0;
    bool hit = false;
};

CpuCapsuleMoverWorld::CpuCapsuleMoverWorld() = default;
CpuCapsuleMoverWorld::~CpuCapsuleMoverWorld() = default;

bool CpuCapsuleMoverWorld::initialize() { return initialize(Config{}); }

bool CpuCapsuleMoverWorld::initialize(const Config& config) {
    if (config.maximumCharacters == 0 || config.maximumPlanes == 0
        || config.maximumCharacters > kMaximumCharacters
        || config.maximumPlanes > 8 || config.maximumCastSamples == 0
        || config.maximumCastSamples > 64 || config.bisectionIterations == 0
        || config.bisectionIterations > 16 || !std::isfinite(config.skin)
        || config.skin < 0.0f || config.skin > 1.0f
        || !validPolicy(config.nearbyDynamicPolicy)) {
        return false;
    }
    std::vector<CharacterSlot> replacement(config.maximumCharacters);
    shutdown();
    config_ = config;
    characters_ = std::move(replacement);
    initialized_ = true;
    return true;
}

void CpuCapsuleMoverWorld::shutdown() {
    characters_.clear();
    freeCharacterSlots_.clear();
    terrainSamples_.clear();
    terrainWidth_ = 0;
    terrainHeight_ = 0;
    terrainHeightScale_ = 0.0f;
    terrainCellScale_ = 0.0f;
    config_ = {};
    initialized_ = false;
}

bool CpuCapsuleMoverWorld::setTerrain(
    std::span<const uint16_t> samples, uint32_t width, uint32_t height,
    float heightScale, float cellScale, bool lego) {
    if (!initialized_ || width < 2u || height < 2u
        || width > kMaximumTerrainExtent
        || height > kMaximumTerrainExtent
        || !std::isfinite(heightScale) || heightScale <= 0.0f
        || heightScale > kMaximumTerrainScale
        || !std::isfinite(cellScale) || cellScale <= 0.0f
        || cellScale > kMaximumTerrainScale) {
        return false;
    }
    const size_t count = size_t{width} * height;
    if (samples.size() < count) return false;
    const auto selected = samples.first(count);
    std::vector<uint16_t> replacement(selected.begin(), selected.end());
    terrainSamples_ = std::move(replacement);
    terrainWidth_ = width;
    terrainHeight_ = height;
    terrainHeightScale_ = heightScale;
    terrainCellScale_ = cellScale;
    legoTerrain_ = lego;
    return true;
}

void CpuCapsuleMoverWorld::clearTerrain() {
    terrainSamples_.clear();
    terrainWidth_ = 0;
    terrainHeight_ = 0;
    terrainHeightScale_ = 0.0f;
    terrainCellScale_ = 0.0f;
    for (auto& character : characters_) {
        character.grounded = false;
        character.steep = false;
    }
}

bool CpuCapsuleMoverWorld::hasTerrain() const noexcept {
    return terrainWidth_ >= 2 && terrainHeight_ >= 2
        && terrainSamples_.size() == size_t{terrainWidth_} * terrainHeight_;
}

CharacterTerrainSample CpuCapsuleMoverWorld::sampleTerrain(
    float worldX, float worldZ) const noexcept {
    CharacterTerrainSample result;
    if (!hasTerrain() || !std::isfinite(worldX) || !std::isfinite(worldZ))
        return result;
    const glm::vec2 origin = terrain_topology::centeredOrigin(
        terrainWidth_, terrainHeight_, terrainCellScale_);
    const glm::vec2 local =
        (glm::vec2(worldX, worldZ) + origin) / terrainCellScale_;
    const glm::vec2 maximum(
        static_cast<float>(terrainWidth_ - 1),
        static_cast<float>(terrainHeight_ - 1));
    if (glm::any(glm::lessThan(local, glm::vec2(0.0f)))
        || glm::any(glm::greaterThan(local, maximum))) return result;
    const uint32_t cellX = std::min(
        static_cast<uint32_t>(std::floor(local.x)), terrainWidth_ - 2);
    const uint32_t cellZ = std::min(
        static_cast<uint32_t>(std::floor(local.y)), terrainHeight_ - 2);
    const glm::vec2 fraction = glm::clamp(
        local - glm::vec2(float(cellX), float(cellZ)),
        glm::vec2(0.0f), glm::vec2(1.0f));
    const auto heightAt = [this](uint32_t x, uint32_t z) {
        return terrain_topology::worldHeight(
            static_cast<float>(terrainSamples_[size_t{z} * terrainWidth_ + x]),
            terrainHeightScale_);
    };
    const float topLeft = heightAt(cellX, cellZ);
    const float topRight = heightAt(cellX + 1u, cellZ);
    const float bottomLeft = heightAt(cellX, cellZ + 1u);
    const float bottomRight = heightAt(cellX + 1u, cellZ + 1u);
    if (legoTerrain_) {
        const terrain::lego::Surface surface{terrainSamples_, terrainWidth_, terrainHeight_,
            terrainHeightScale_, terrainCellScale_};
        result.height = surface.heightAt(worldX, worldZ);
        result.normal = {0, 1, 0};
        result.featureId = (cellZ * terrainWidth_ + cellX) * 8u + 1u;
        result.valid = true;
        return result;
    }
    glm::vec2 gradient(0.0f);
    uint32_t triangle = 0;
    if (fraction.y >= fraction.x) {
        result.height = topLeft
            + fraction.x * (bottomRight - bottomLeft)
            + fraction.y * (bottomLeft - topLeft);
        gradient = {bottomRight - bottomLeft, bottomLeft - topLeft};
    } else {
        triangle = 1;
        result.height = topLeft
            + fraction.x * (topRight - topLeft)
            + fraction.y * (bottomRight - topRight);
        gradient = {topRight - topLeft, bottomRight - topRight};
    }
    gradient /= terrainCellScale_;
    result.normal = glm::normalize(glm::vec3(-gradient.x, 1.0f, -gradient.y));
    result.featureId = (cellZ * (terrainWidth_ - 1u) + cellX) * 2u
        + triangle;
    result.valid = true;
    return result;
}

CharacterTerrainSample CpuCapsuleMoverWorld::sampleTerrainInFrame(
    float localX, float localZ,
    const glm::ivec3& referenceSector) const noexcept {
    CharacterTerrainSample result;
    if (!hasTerrain() || !std::isfinite(localX) || !std::isfinite(localZ))
        return result;

    // Terrain is currently anchored in sector zero. Reject distant sectors
    // before converting any delta to f32; nearby terrain math remains in its
    // compact local frame.
    const WorldPosition horizontal = canonicalWorldPosition(
        referenceSector, glm::dvec3(localX, 0.0, localZ));
    const double halfWidth = 0.5 * static_cast<double>(terrainWidth_ - 1u)
                           * static_cast<double>(terrainCellScale_);
    const double halfDepth = 0.5 * static_cast<double>(terrainHeight_ - 1u)
                           * static_cast<double>(terrainCellScale_);
    const int64_t maximumHorizontalSectors = static_cast<int64_t>(std::ceil(
        std::max(halfWidth, halfDepth)
            / static_cast<double>(kWorldSectorSize))) + 1;
    const int64_t maximumVerticalSectors = static_cast<int64_t>(std::ceil(
        static_cast<double>(terrainHeightScale_)
            / static_cast<double>(kWorldSectorSize))) + 1;
    const int64_t sectorX = horizontal.sector.x;
    const int64_t sectorZ = horizontal.sector.z;
    const int64_t sectorY = referenceSector.y;
    if (std::abs(sectorX) > maximumHorizontalSectors
        || std::abs(sectorZ) > maximumHorizontalSectors
        || std::abs(sectorY) > maximumVerticalSectors) {
        return result;
    }

    const float terrainX = horizontal.local.x
        + static_cast<float>(sectorX) * kWorldSectorSize;
    const float terrainZ = horizontal.local.z
        + static_cast<float>(sectorZ) * kWorldSectorSize;
    result = sampleTerrain(terrainX, terrainZ);
    if (result.valid) {
        result.height -= static_cast<float>(sectorY) * kWorldSectorSize;
    }
    return result;
}

CpuCapsuleMoverWorld::CapsuleClearance
CpuCapsuleMoverWorld::capsuleClearance(
    const glm::vec3& feetPosition, const glm::ivec3& referenceSector,
    float radius) const noexcept {
    CapsuleClearance result;
    radius = std::max(radius, 1e-4f);
    if (legoTerrain_) {
        const terrain::lego::Surface surface{terrainSamples_, terrainWidth_, terrainHeight_,
            terrainHeightScale_, terrainCellScale_};
        // Reject distant sectors before converting to the terrain's float frame.
        if (std::abs(int64_t{referenceSector.x}) > 64 || std::abs(int64_t{referenceSector.z}) > 64
            || std::abs(int64_t{referenceSector.y}) > 64) return result;
        const glm::vec3 world = feetPosition + glm::vec3(referenceSector) * kWorldSectorSize;
        const auto contact = terrain::lego::sphereContact(surface, world + glm::vec3(0, radius, 0), radius);
        result.distance = contact.distance;
        result.normal = contact.normal;
        result.featureId = contact.feature;
        result.valid = contact.valid;
        result.requiredFeetHeight = terrain::lego::supportHeight(surface, {world.x, world.z}, radius)
            - float(referenceSector.y) * kWorldSectorSize;
        return result;
    }
    constexpr std::array<glm::vec2, 9> normalizedOffsets{{
        {0.0f, 0.0f},
        {0.5f, 0.0f}, {-0.5f, 0.0f},
        {0.0f, 0.5f}, {0.0f, -0.5f},
        {0.35355339f, 0.35355339f},
        {-0.35355339f, 0.35355339f},
        {0.35355339f, -0.35355339f},
        {-0.35355339f, -0.35355339f},
    }};
    for (uint32_t index = 0; index < normalizedOffsets.size(); ++index) {
        const glm::vec2 offset = normalizedOffsets[index] * radius;
        const CharacterTerrainSample surface = sampleTerrainInFrame(
            feetPosition.x + offset.x, feetPosition.z + offset.y,
            referenceSector);
        if (!surface.valid) continue;
        const float radialSquared = glm::dot(offset, offset);
        const float sphereSurfaceOffset = radius
            - std::sqrt(std::max(radius * radius - radialSquared, 0.0f));
        const float required = surface.height - sphereSurfaceOffset;
        const float clearance = feetPosition.y - required;
        const uint32_t feature = surface.featureId * 16u + index;
        if (!result.valid || clearance < result.distance
            || (clearance == result.distance && feature < result.featureId)) {
            result.distance = clearance;
            result.requiredFeetHeight = required;
            result.normal = surface.normal;
            result.featureId = feature;
            result.valid = true;
        }
    }
    return result;
}

CpuCapsuleMoverWorld::CastHit CpuCapsuleMoverWorld::castCapsule(
    const glm::vec3& start, const glm::vec3& translation,
    const glm::ivec3& referenceSector, float radius) const noexcept {
    CastHit result;
    const float horizontalDistance = glm::length(glm::vec2(
        translation.x, translation.z));
    const float sampleSpan = std::max(
        std::min(terrainCellScale_, std::max(radius, 0.05f)) * 0.25f,
        0.01f);
    const float verticalSpan = std::max(radius * 0.25f, 0.01f);
    const uint32_t requested = std::max(1u, static_cast<uint32_t>(std::ceil(
        horizontalDistance / sampleSpan
        + std::abs(translation.y) / verticalSpan)));
    const uint32_t steps = std::min(requested, config_.maximumCastSamples);
    CapsuleClearance previous = capsuleClearance(
        start, referenceSector, radius);
    if (previous.valid
        && (previous.distance < -config_.skin
            || (previous.distance <= config_.skin
                && glm::dot(translation, previous.normal) < -1e-7f))) {
        result.fraction = 0.0f;
        result.normal = previous.normal;
        result.featureId = previous.featureId;
        result.hit = true;
        return result;
    }
    float previousFraction = 0.0f;
    for (uint32_t step = 1; step <= steps; ++step) {
        const float fraction = static_cast<float>(step)
                             / static_cast<float>(steps);
        CapsuleClearance sample = capsuleClearance(
            start + fraction * translation, referenceSector, radius);
        if (sample.valid && sample.distance <= config_.skin) {
            float lower = previousFraction;
            float upper = fraction;
            CapsuleClearance hit = sample;
            for (uint32_t iteration = 0;
                 iteration < config_.bisectionIterations; ++iteration) {
                const float middle = 0.5f * (lower + upper);
                const CapsuleClearance candidate = capsuleClearance(
                    start + middle * translation, referenceSector, radius);
                if (candidate.valid && candidate.distance <= config_.skin) {
                    upper = middle;
                    hit = candidate;
                } else {
                    lower = middle;
                }
            }
            result.fraction = upper;
            result.normal = hit.normal;
            result.featureId = hit.featureId;
            result.hit = true;
            return result;
        }
        previousFraction = fraction;
        previous = sample;
    }
    return result;
}

CharacterHandle CpuCapsuleMoverWorld::createCharacter(
    const glm::vec3& feetPosition, const CharacterSettings& requested) {
    if (!initialized_ || !isRepresentableAbsolutePosition(feetPosition))
        return InvalidCharacter;
    return createCharacter(
        worldPositionFromAbsolute(glm::dvec3(feetPosition)), requested);
}

CharacterHandle CpuCapsuleMoverWorld::createCharacter(
    const WorldPosition& feetPosition, const CharacterSettings& requested) {
    if (!initialized_ || !isValidWorldPosition(feetPosition))
        return InvalidCharacter;
    const CharacterSettings settings = sanitizeCharacterSettings(requested);
    uint32_t slotIndex = kMaximumCharacterSlots;
    if (!freeCharacterSlots_.empty()) {
        slotIndex = freeCharacterSlots_.front();
        freeCharacterSlots_.erase(freeCharacterSlots_.begin());
    } else {
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            if (!characters_[index].active
                && characters_[index].generation
                    != std::numeric_limits<uint16_t>::max()) {
                slotIndex = index;
                break;
            }
        }
    }
    const CharacterHandle handle = slotIndex < characters_.size()
        ? makeCharacterHandle(slotIndex, characters_[slotIndex].generation)
        : InvalidCharacter;
    if (handle == InvalidCharacter) return handle;
    auto& slot = characters_[slotIndex];
    const uint16_t generation = slot.generation;
    slot = {};
    slot.generation = generation;
    slot.position = feetPosition.local;
    slot.sector = feetPosition.sector;
    slot.settings = settings;
    slot.active = true;
    const CapsuleClearance clearance = capsuleClearance(
        slot.position, slot.sector, settings.radius);
    if (clearance.valid && clearance.distance <= config_.skin) {
        slot.position.y = clearance.requiredFeetHeight;
        slot.groundNormal = clearance.normal;
        const float minimumNormal = std::cos(glm::radians(
            settings.maxSlopeAngleDegrees));
        slot.grounded = clearance.normal.y >= minimumNormal;
        slot.steep = !slot.grounded;
    }
    const WorldPosition canonical = canonicalWorldPosition(
        slot.sector, glm::dvec3(slot.position));
    slot.sector = canonical.sector;
    slot.position = canonical.local;
    return handle;
}

void CpuCapsuleMoverWorld::destroyCharacter(CharacterHandle handle) {
    CharacterSlot* slot = find(handle);
    if (!slot) return;
    const uint32_t slotIndex = characterHandleSlot(handle);
    const uint16_t generation = slot->generation;
    *slot = {};
    slot->generation = generation;
    if (generation == std::numeric_limits<uint16_t>::max()) return;
    ++slot->generation;
    const auto insertion = std::lower_bound(
        freeCharacterSlots_.begin(), freeCharacterSlots_.end(), slotIndex);
    if (insertion == freeCharacterSlots_.end() || *insertion != slotIndex)
        freeCharacterSlots_.insert(insertion, slotIndex);
}

bool CpuCapsuleMoverWorld::setCharacterPosition(
    CharacterHandle handle, const glm::vec3& feetPosition) {
    if (!isRepresentableAbsolutePosition(feetPosition)) return false;
    return setCharacterPosition(
        handle, worldPositionFromAbsolute(glm::dvec3(feetPosition)));
}

bool CpuCapsuleMoverWorld::setCharacterPosition(
    CharacterHandle handle, const WorldPosition& feetPosition) {
    CharacterSlot* slot = find(handle);
    if (!slot || !isValidWorldPosition(feetPosition)) return false;
    slot->position = feetPosition.local;
    slot->sector = feetPosition.sector;
    slot->velocity = glm::vec3(0.0f);
    slot->grounded = false;
    slot->steep = false;
    return true;
}

CharacterMotion CpuCapsuleMoverWorld::moveCharacter(
    CharacterHandle handle, const glm::vec3& desiredHorizontalVelocity,
    bool jump, float jumpSpeed, float gravity, float terminalVelocity,
    float deltaTime) {
    CharacterMotion result;
    CharacterSlot* slot = find(handle);
    if (!slot) return result;
    const float frameTime = std::clamp(
        finiteOr(deltaTime, 0.0f), 0.0f, kMaximumFrameTime);
    if (frameTime <= 0.0f) {
        return {slot->position, slot->velocity, slot->groundNormal,
                slot->grounded, slot->steep, slot->sector};
    }
    const glm::vec3 desired = finiteVector(desiredHorizontalVelocity)
        ? glm::clamp(
            desiredHorizontalVelocity,
            glm::vec3(-kMaximumCharacterRate),
            glm::vec3(kMaximumCharacterRate))
        : glm::vec3(0.0f);
    gravity = std::clamp(
        finiteOr(gravity, 0.0f), 0.0f, kMaximumCharacterRate);
    terminalVelocity = std::clamp(
        finiteOr(terminalVelocity, 0.0f),
        0.0f, kMaximumCharacterRate);
    jumpSpeed = std::clamp(
        finiteOr(jumpSpeed, 0.0f), 0.0f, kMaximumCharacterRate);
    slot->velocity.x = desired.x;
    slot->velocity.z = desired.z;
    if (jump && slot->grounded) {
        slot->velocity.y = jumpSpeed;
        slot->grounded = false;
    } else if (slot->grounded) {
        slot->velocity.y = 0.0f;
    } else {
        slot->velocity.y = std::max(
            slot->velocity.y - gravity * frameTime, -terminalVelocity);
    }

    const float minimumGroundNormal = std::cos(glm::radians(
        slot->settings.maxSlopeAngleDegrees));
    bool encounteredSteep = slot->steep;
    glm::vec3 translation = slot->velocity * frameTime;
    const glm::vec3 horizontalTarget(
        slot->position.x + translation.x,
        slot->position.y,
        slot->position.z + translation.z);
    const CapsuleClearance targetGround = capsuleClearance(
        horizontalTarget, slot->sector, slot->settings.radius);
    if (slot->grounded && !jump && targetGround.valid
        && (legoTerrain_ || targetGround.normal.y >= minimumGroundNormal)) {
        const float rise = targetGround.requiredFeetHeight - slot->position.y;
        // A short vertical face is a step, even though its contact normal is
        // horizontal. Check the raised forward path before placing the feet on
        // exact hemisphere support; tall cliffs still obstruct that path.
        const bool stepPathClear = !legoTerrain_ || !castCapsule(
            slot->position + glm::vec3(0, slot->settings.stepUp + 2 * config_.skin, 0),
            glm::vec3(translation.x, 0, translation.z), slot->sector,
            slot->settings.radius).hit;
        if (rise <= slot->settings.stepUp + config_.skin
            && rise >= -slot->settings.stepDown - config_.skin && stepPathClear) {
            slot->position = horizontalTarget;
            slot->position.y = targetGround.requiredFeetHeight;
            slot->groundNormal = legoTerrain_
                ? capsuleClearance(slot->position, slot->sector, slot->settings.radius).normal
                : targetGround.normal;
            slot->grounded = true;
            slot->steep = false;
            const WorldPosition canonical = canonicalWorldPosition(
                slot->sector, glm::dvec3(slot->position));
            slot->sector = canonical.sector;
            slot->position = canonical.local;
            return {slot->position, slot->velocity, slot->groundNormal,
                    true, false, slot->sector};
        }
    }

    std::array<std::pair<uint32_t, glm::vec3>, 8> planes{};
    uint32_t planeCount = 0;
    for (uint32_t iteration = 0;
         iteration < config_.maximumPlanes; ++iteration) {
        if (glm::dot(translation, translation) < 1e-12f) break;
        const CastHit hit = castCapsule(
            slot->position, translation, slot->sector,
            slot->settings.radius);
        if (!hit.hit) {
            slot->position += translation;
            translation = glm::vec3(0.0f);
            break;
        }
        const float translationLength = glm::length(translation);
        if (hit.normal.y < minimumGroundNormal) {
            encounteredSteep = true;
            slot->groundNormal = hit.normal;
        }
        const float safeFraction = std::max(
            hit.fraction - config_.skin / std::max(translationLength, 1e-6f),
            0.0f);
        slot->position += translation * safeFraction;
        bool duplicate = false;
        for (uint32_t plane = 0; plane < planeCount; ++plane)
            duplicate = duplicate || planes[plane].first == hit.featureId;
        if (!duplicate && planeCount < planes.size()) {
            uint32_t insertion = planeCount;
            while (insertion != 0u
                && planes[insertion - 1u].first > hit.featureId) {
                planes[insertion] = planes[insertion - 1u];
                --insertion;
            }
            planes[insertion] = {hit.featureId, hit.normal};
            ++planeCount;
        }
        glm::vec3 remainder = translation * (1.0f - safeFraction);
        for (uint32_t plane = 0; plane < planeCount; ++plane) {
            const float intoPlane = glm::dot(remainder, planes[plane].second);
            if (intoPlane < 0.0f)
                remainder -= intoPlane * planes[plane].second;
            const float velocityInto = glm::dot(
                slot->velocity, planes[plane].second);
            if (velocityInto < 0.0f)
                slot->velocity -= velocityInto * planes[plane].second;
        }
        translation = remainder;
    }

    const CapsuleClearance finalGround = capsuleClearance(
        slot->position, slot->sector, slot->settings.radius);
    slot->grounded = false;
    slot->steep = false;
    if (finalGround.valid) {
        if (finalGround.distance < -config_.skin) {
            const float correction = -finalGround.distance + config_.skin;
            slot->position += finalGround.normal
                * (legoTerrain_ ? correction : correction / std::max(finalGround.normal.y, 0.1f));
        }
        const float distance = slot->position.y
                             - finalGround.requiredFeetHeight;
        if (slot->velocity.y <= 0.0f
            && distance <= slot->settings.stepDown + config_.skin) {
            slot->groundNormal = finalGround.normal;
            if (finalGround.normal.y >= minimumGroundNormal) {
                slot->position.y = finalGround.requiredFeetHeight;
                slot->velocity.y = 0.0f;
                slot->grounded = true;
            } else {
                slot->steep = true;
            }
        }
    }
    if (!slot->grounded && encounteredSteep) slot->steep = true;
    const WorldPosition canonical = canonicalWorldPosition(
        slot->sector, glm::dvec3(slot->position));
    slot->sector = canonical.sector;
    slot->position = canonical.local;
    return {slot->position, slot->velocity, slot->groundNormal,
            slot->grounded, slot->steep, slot->sector};
}

CpuCapsuleMoverWorld::CharacterSlot* CpuCapsuleMoverWorld::find(
    CharacterHandle handle) noexcept {
    const uint32_t slotIndex = characterHandleSlot(handle);
    if (slotIndex >= characters_.size()) return nullptr;
    auto& slot = characters_[slotIndex];
    return slot.active
            && slot.generation == characterHandleGeneration(handle)
        ? &slot
        : nullptr;
}

const CpuCapsuleMoverWorld::CharacterSlot* CpuCapsuleMoverWorld::find(
    CharacterHandle handle) const noexcept {
    const uint32_t slotIndex = characterHandleSlot(handle);
    if (slotIndex >= characters_.size()) return nullptr;
    const auto& slot = characters_[slotIndex];
    return slot.active
            && slot.generation == characterHandleGeneration(handle)
        ? &slot
        : nullptr;
}

} // namespace voxy::physics

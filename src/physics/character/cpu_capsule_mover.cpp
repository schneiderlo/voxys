#include "physics/character/cpu_capsule_mover.hpp"

#include "physics/terrain_topology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace voxy::physics {
namespace {

bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // namespace

struct CpuCapsuleMoverWorld::CharacterSlot {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    CharacterSettings settings{};
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
    shutdown();
    if (config.maximumCharacters == 0 || config.maximumPlanes == 0
        || config.maximumPlanes > 8 || config.maximumCastSamples == 0
        || config.maximumCastSamples > 64 || config.bisectionIterations == 0
        || config.bisectionIterations > 16 || !std::isfinite(config.skin)
        || config.skin < 0.0f) {
        return false;
    }
    config_ = config;
    characters_.resize(config_.maximumCharacters);
    initialized_ = true;
    return true;
}

void CpuCapsuleMoverWorld::shutdown() {
    characters_.clear();
    freeCharacters_.clear();
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
    float heightScale, float cellScale) {
    const size_t count = size_t{width} * height;
    if (!initialized_ || width < 2 || height < 2 || samples.size() < count
        || !std::isfinite(heightScale) || heightScale <= 0.0f
        || !std::isfinite(cellScale) || cellScale <= 0.0f) {
        return false;
    }
    const auto selected = samples.first(count);
    terrainSamples_.assign(selected.begin(), selected.end());
    terrainWidth_ = width;
    terrainHeight_ = height;
    terrainHeightScale_ = heightScale;
    terrainCellScale_ = cellScale;
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

CpuCapsuleMoverWorld::CapsuleClearance
CpuCapsuleMoverWorld::capsuleClearance(
    const glm::vec3& feetPosition, float radius) const noexcept {
    CapsuleClearance result;
    radius = std::max(radius, 1e-4f);
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
        const CharacterTerrainSample surface = sampleTerrain(
            feetPosition.x + offset.x, feetPosition.z + offset.y);
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
    float radius) const noexcept {
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
    CapsuleClearance previous = capsuleClearance(start, radius);
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
            start + fraction * translation, radius);
        if (sample.valid && sample.distance <= config_.skin) {
            float lower = previousFraction;
            float upper = fraction;
            CapsuleClearance hit = sample;
            for (uint32_t iteration = 0;
                 iteration < config_.bisectionIterations; ++iteration) {
                const float middle = 0.5f * (lower + upper);
                const CapsuleClearance candidate = capsuleClearance(
                    start + middle * translation, radius);
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
    if (!initialized_ || !finiteVector(feetPosition))
        return InvalidCharacter;
    CharacterSettings settings = requested;
    settings.radius = std::max(settings.radius, 0.01f);
    settings.height = std::max(settings.height, 2.0f * settings.radius);
    settings.maxSlopeAngleDegrees = std::clamp(
        settings.maxSlopeAngleDegrees, 0.0f, 89.9f);
    settings.stepUp = std::max(settings.stepUp, 0.0f);
    settings.stepDown = std::max(settings.stepDown, 0.0f);
    CharacterHandle handle = InvalidCharacter;
    if (!freeCharacters_.empty()) {
        handle = freeCharacters_.front();
        freeCharacters_.erase(freeCharacters_.begin());
    } else {
        for (uint32_t index = 0; index < characters_.size(); ++index) {
            if (!characters_[index].active) {
                handle = index + 1u;
                break;
            }
        }
    }
    if (handle == InvalidCharacter) return handle;
    auto& slot = characters_[handle - 1u];
    slot = {};
    slot.position = feetPosition;
    slot.settings = settings;
    slot.active = true;
    const CapsuleClearance clearance = capsuleClearance(
        feetPosition, settings.radius);
    if (clearance.valid && clearance.distance <= config_.skin) {
        slot.position.y = clearance.requiredFeetHeight;
        slot.groundNormal = clearance.normal;
        const float minimumNormal = std::cos(glm::radians(
            settings.maxSlopeAngleDegrees));
        slot.grounded = clearance.normal.y >= minimumNormal;
        slot.steep = !slot.grounded;
    }
    return handle;
}

void CpuCapsuleMoverWorld::destroyCharacter(CharacterHandle handle) {
    CharacterSlot* slot = find(handle);
    if (!slot) return;
    *slot = {};
    const auto insertion = std::lower_bound(
        freeCharacters_.begin(), freeCharacters_.end(), handle);
    if (insertion == freeCharacters_.end() || *insertion != handle)
        freeCharacters_.insert(insertion, handle);
}

bool CpuCapsuleMoverWorld::setCharacterPosition(
    CharacterHandle handle, const glm::vec3& feetPosition) {
    CharacterSlot* slot = find(handle);
    if (!slot || !finiteVector(feetPosition)) return false;
    slot->position = feetPosition;
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
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f
        || !finiteVector(desiredHorizontalVelocity)) {
        return {slot->position, slot->velocity, slot->groundNormal,
                slot->grounded, slot->steep};
    }
    gravity = std::max(gravity, 0.0f);
    terminalVelocity = std::max(terminalVelocity, 0.0f);
    slot->velocity.x = desiredHorizontalVelocity.x;
    slot->velocity.z = desiredHorizontalVelocity.z;
    if (jump && slot->grounded) {
        slot->velocity.y = std::max(jumpSpeed, 0.0f);
        slot->grounded = false;
    } else if (slot->grounded) {
        slot->velocity.y = 0.0f;
    } else {
        slot->velocity.y = std::max(
            slot->velocity.y - gravity * deltaTime, -terminalVelocity);
    }

    const float minimumGroundNormal = std::cos(glm::radians(
        slot->settings.maxSlopeAngleDegrees));
    bool encounteredSteep = slot->steep;
    glm::vec3 translation = slot->velocity * deltaTime;
    const glm::vec3 horizontalTarget(
        slot->position.x + translation.x,
        slot->position.y,
        slot->position.z + translation.z);
    const CapsuleClearance targetGround = capsuleClearance(
        horizontalTarget, slot->settings.radius);
    if (slot->grounded && !jump && targetGround.valid
        && targetGround.normal.y >= minimumGroundNormal) {
        const float rise = targetGround.requiredFeetHeight - slot->position.y;
        if (rise <= slot->settings.stepUp + config_.skin
            && rise >= -slot->settings.stepDown - config_.skin) {
            slot->position = horizontalTarget;
            slot->position.y = targetGround.requiredFeetHeight;
            slot->groundNormal = targetGround.normal;
            slot->grounded = true;
            slot->steep = false;
            return {slot->position, slot->velocity, slot->groundNormal,
                    true, false};
        }
    }

    std::array<std::pair<uint32_t, glm::vec3>, 8> planes{};
    uint32_t planeCount = 0;
    for (uint32_t iteration = 0;
         iteration < config_.maximumPlanes; ++iteration) {
        if (glm::dot(translation, translation) < 1e-12f) break;
        const CastHit hit = castCapsule(
            slot->position, translation, slot->settings.radius);
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
            planes[planeCount++] = {hit.featureId, hit.normal};
            std::sort(planes.begin(),
                planes.begin() + static_cast<std::ptrdiff_t>(planeCount),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });
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
        slot->position, slot->settings.radius);
    slot->grounded = false;
    slot->steep = false;
    if (finalGround.valid) {
        if (finalGround.distance < -config_.skin) {
            const float correction = -finalGround.distance + config_.skin;
            slot->position += finalGround.normal
                * (correction / std::max(finalGround.normal.y, 0.1f));
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
    return {slot->position, slot->velocity, slot->groundNormal,
            slot->grounded, slot->steep};
}

CpuCapsuleMoverWorld::CharacterSlot* CpuCapsuleMoverWorld::find(
    CharacterHandle handle) noexcept {
    if (handle == InvalidCharacter || handle > characters_.size()) return nullptr;
    auto& slot = characters_[handle - 1u];
    return slot.active ? &slot : nullptr;
}

const CpuCapsuleMoverWorld::CharacterSlot* CpuCapsuleMoverWorld::find(
    CharacterHandle handle) const noexcept {
    if (handle == InvalidCharacter || handle > characters_.size()) return nullptr;
    const auto& slot = characters_[handle - 1u];
    return slot.active ? &slot : nullptr;
}

} // namespace voxy::physics

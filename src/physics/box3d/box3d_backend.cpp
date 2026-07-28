#include "physics/box3d/box3d_backend.hpp"

#include "core/log.hpp"
#include "physics/box3d/box3d_conversions.hpp"
#include "physics/terrain_topology.hpp"

#include <box3d/box3d.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

using box3d_conversion::toGlmPosition;
using box3d_conversion::toGlmVector;
using box3d_conversion::toPosition;
using box3d_conversion::toVector;

constexpr float kFixedTimeStep = 1.0f / 60.0f;
constexpr uint32_t kInternalSubsteps = 4;
constexpr uint32_t kMaxCatchUpTicks = 8;
constexpr float kMaximumFrameTime =
    kFixedTimeStep * static_cast<float>(kMaxCatchUpTicks);
constexpr float kWaterSampleBand = 8.0f;
constexpr float kWaterLinearDrag = 0.55f;
constexpr float kWaterAngularDrag = 0.08f;
constexpr float kGravityMagnitude = 9.81f;
constexpr int kCylinderSides = 16;
constexpr int kMoverPlaneCapacity = 16;
constexpr int kMoverIterations = 5;
constexpr float kMaximumCharacterRate = 10'000.0f;
constexpr float kMaximumTerrainScale = 1.0e6f;
constexpr uint32_t kMaximumTerrainExtent = 8'192u;
constexpr uint32_t kMaximumBodyCapacity = 1'000'000u;
constexpr uint32_t kMaximumPairCapacity = 1'048'576u;
constexpr uint32_t kMaximumContactCapacity = 1'048'576u;

[[nodiscard]] bool finiteVector(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float throwableBuoyancy(ThrowableShape shape) noexcept {
    switch (shape) {
        case ThrowableShape::Sphere: return 1.08f;
        case ThrowableShape::Cube: return 0.94f;
        case ThrowableShape::Box: return 1.18f;
        case ThrowableShape::Capsule: return 1.10f;
        case ThrowableShape::Cylinder: return 0.98f;
        case ThrowableShape::Count: break;
    }
    return 1.0f;
}

[[nodiscard]] glm::vec3 normalizedWaterNormal(
    const WaterSurfaceSample& sample) noexcept {
    glm::vec3 normal{-sample.slope.x, 1.0f, -sample.slope.y};
    const float lengthSquared = normal.x * normal.x + normal.y * normal.y
        + normal.z * normal.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return normal / std::sqrt(lengthSquared);
}

struct MoverPlaneCollector {
    std::array<b3CollisionPlane, kMoverPlaneCapacity> planes{};
    int count = 0;
};

bool collectMoverPlanes(b3ShapeId, const b3PlaneResult* results,
                        int resultCount, void* context) {
    auto& collector = *static_cast<MoverPlaneCollector*>(context);
    for (int index = 0;
         index < resultCount && collector.count < kMoverPlaneCapacity;
         ++index) {
        collector.planes[static_cast<size_t>(collector.count)] = {
            .plane = results[index].plane,
            .pushLimit = FLT_MAX,
            .push = 0.0f,
            .clipVelocity = true,
        };
        ++collector.count;
    }
    return true;
}

struct GroundProbe {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool hit = false;
    bool walkable = false;
};

} // namespace

class Box3DReferenceBackend::Impl {
public:
    struct DynamicSlot {
        b3BodyId body = b3_nullBodyId;
        ThrowableShape shape = ThrowableShape::Sphere;
        glm::vec3 dimensions{1.0f};
        mutable glm::vec3 cachedPosition{0.0f};
        mutable glm::quat cachedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        mutable bool cacheValid = false;
        mutable bool snapshotDirty = true;
    };

    struct CharacterSlot {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
        CharacterSettings settings;
        bool grounded = false;
        bool onSteepGround = false;
    };

    [[nodiscard]] CharacterSlot* findCharacter(CharacterHandle handle) {
        const uint32_t index = characterHandleSlot(handle);
        if (index >= characters.size()
            || index >= characterGenerations.size()
            || characterGenerations[index]
                != characterHandleGeneration(handle)) {
            return nullptr;
        }
        return characters[index].get();
    }

    [[nodiscard]] const CharacterSlot* findCharacter(
        CharacterHandle handle) const {
        const uint32_t index = characterHandleSlot(handle);
        if (index >= characters.size()
            || index >= characterGenerations.size()
            || characterGenerations[index]
                != characterHandleGeneration(handle)) {
            return nullptr;
        }
        return characters[index].get();
    }

    [[nodiscard]] b3Capsule characterCapsule(
        const CharacterSettings& settings) const noexcept {
        return {
            .center1 = {0.0f, settings.radius, 0.0f},
            .center2 = {0.0f, settings.height - settings.radius, 0.0f},
            .radius = settings.radius,
        };
    }

    [[nodiscard]] GroundProbe probeGround(
        const CharacterSlot& character, float extraDistance = 0.0f) const {
        GroundProbe result;
        if (!b3World_IsValid(world)) {
            return result;
        }

        constexpr float kRayStartLift = 0.05f;
        const float distance = std::max(character.settings.stepDown, 0.05f)
            + kRayStartLift + std::max(extraDistance, 0.0f);
        const glm::vec3 origin = character.position
            + glm::vec3(0.0f, kRayStartLift, 0.0f);
        const b3RayResult hit = b3World_CastRayClosest(
            world, toPosition(origin), {0.0f, -distance, 0.0f},
            b3DefaultQueryFilter());
        if (!hit.hit) {
            return result;
        }

        result.hit = true;
        result.point = toGlmPosition(hit.point);
        result.normal = toGlmVector(hit.normal);
        const float slopeCosine = std::cos(
            character.settings.maxSlopeAngleDegrees
            * std::numbers::pi_v<float> / 180.0f);
        result.walkable = result.normal.y >= slopeCosine;
        return result;
    }

    void refreshCharacterGround(CharacterSlot& character,
                                bool allowSnap) const {
        const GroundProbe probe = probeGround(character);
        character.grounded = false;
        character.onSteepGround = probe.hit && !probe.walkable;
        character.groundNormal = probe.hit
            ? probe.normal : glm::vec3(0.0f, 1.0f, 0.0f);
        if (probe.walkable && allowSnap) {
            const float fall = character.position.y - probe.point.y;
            const float maximumSnap = std::max(
                character.settings.stepDown, 0.05f) + 0.051f;
            if (fall >= -0.051f && fall <= maximumSnap) {
                character.position.y = probe.point.y;
                character.velocity.y = 0.0f;
                character.grounded = true;
            }
        }
    }

    void moveCapsule(CharacterSlot& character,
                     const glm::vec3& translation) const {
        const b3Capsule mover = characterCapsule(character.settings);
        const glm::vec3 target = character.position + translation;
        const b3QueryFilter filter = b3DefaultQueryFilter();

        for (int iteration = 0; iteration < kMoverIterations; ++iteration) {
            MoverPlaneCollector collector;
            b3World_CollideMover(
                world, toPosition(character.position), &mover, filter,
                collectMoverPlanes, &collector);

            const glm::vec3 desiredDelta = target - character.position;
            b3PlaneSolverResult solved = b3SolvePlanes(
                toVector(desiredDelta), collector.planes.data(),
                collector.count);
            glm::vec3 delta = toGlmVector(solved.delta);
            if (!finiteVector(delta)) {
                break;
            }

            const float fraction = b3World_CastMover(
                world, toPosition(character.position), &mover,
                toVector(delta), filter, nullptr, nullptr);
            delta *= std::clamp(fraction, 0.0f, 1.0f);
            character.position += delta;
            if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z
                < 1.0e-8f) {
                break;
            }
        }
    }

    void collectBodyMoveEvents() {
        const b3BodyEvents events = b3World_GetBodyEvents(world);
        for (int eventIndex = 0; eventIndex < events.moveCount;
             ++eventIndex) {
            const b3BodyMoveEvent& event = events.moveEvents[eventIndex];
            const uintptr_t encoded = reinterpret_cast<uintptr_t>(
                event.userData);
            if (encoded == 0 || encoded > dynamicBodies.size()) {
                continue;
            }
            DynamicSlot& slot = dynamicBodies[encoded - 1u];
            if (!B3_ID_EQUALS(slot.body, event.bodyId)) {
                continue;
            }
            slot.cachedPosition = toGlmPosition(event.transform.p);
            slot.cachedRotation = box3d_conversion::toGlm(event.transform.q);
            slot.cacheValid = true;
            slot.snapshotDirty = true;
        }
    }

    void applyWaterForces(float stepTime) {
        if (!waterEnabled) {
            return;
        }

        waterTime = std::fmod(waterTime + stepTime, 4096.0f);
        for (const DynamicSlot& slot : dynamicBodies) {
            if (!b3Body_IsValid(slot.body)) {
                continue;
            }

            const glm::vec3 position = toGlmPosition(
                b3Body_GetPosition(slot.body));
            if (position.y - waterHeight >= kWaterSampleBand) {
                continue;
            }

            WaterSurfaceSample water;
            if (waterSurfaceSampler
                && std::abs(position.y - waterHeight) < kWaterSampleBand) {
                water = waterSurfaceSampler(
                    {position.x, position.z}, waterTime);
            }
            if (!std::isfinite(water.heightOffset)
                || !std::isfinite(water.slope.x)
                || !std::isfinite(water.slope.y)
                || !finiteVector(water.velocity)) {
                water = {};
            }

            const float halfHeight = 0.5f * slot.dimensions.y;
            const float surfaceHeight = waterHeight + water.heightOffset;
            const float submergedFraction = std::clamp(
                (surfaceHeight - (position.y - halfHeight))
                    / std::max(2.0f * halfHeight, 0.001f),
                0.0f, 1.0f);
            if (submergedFraction <= 0.0f) {
                continue;
            }

            const float mass = b3Body_GetMass(slot.body);
            const glm::vec3 velocity = toGlmVector(
                b3Body_GetLinearVelocity(slot.body));
            const glm::vec3 buoyancy = normalizedWaterNormal(water)
                * (mass * kGravityMagnitude * throwableBuoyancy(slot.shape)
                   * submergedFraction);
            const glm::vec3 drag = (water.velocity - velocity)
                * (mass * kWaterLinearDrag * submergedFraction);
            b3Body_ApplyForceToCenter(
                slot.body, toVector(buoyancy + drag), true);

            const glm::vec3 angularVelocity = toGlmVector(
                b3Body_GetAngularVelocity(slot.body));
            const float angularScale = std::max(
                0.0f, 1.0f - kWaterAngularDrag * submergedFraction
                    * stepTime);
            b3Body_SetAngularVelocity(
                slot.body, toVector(angularVelocity * angularScale));
        }
    }

    void clearTerrain() {
        if (b3Body_IsValid(terrainBody)) {
            b3DestroyBody(terrainBody);
        }
        terrainBody = b3_nullBodyId;
        terrainShape = b3_nullShapeId;
        if (terrainData != nullptr) {
            b3DestroyHeightField(terrainData);
            terrainData = nullptr;
        }
        terrainWidth = 0;
        terrainHeight = 0;
    }

    PhysicsInitContext initContext;
    b3WorldId world = b3_nullWorldId;
    b3BodyId terrainBody = b3_nullBodyId;
    b3ShapeId terrainShape = b3_nullShapeId;
    b3HeightFieldData* terrainData = nullptr;
    uint32_t terrainWidth = 0;
    uint32_t terrainHeight = 0;
    std::vector<DynamicSlot> dynamicBodies;
    std::vector<std::unique_ptr<CharacterSlot>> characters;
    std::vector<uint16_t> characterGenerations;
    WaterSurfaceSampler waterSurfaceSampler;
    float waterHeight = 0.0f;
    float waterTime = 0.0f;
    double accumulator = 0.0;
    bool waterEnabled = false;
    bool bodyCapacityOverflow = false;
    mutable DynamicBodyReadStats lastReadStats;
    PhysicsStepStats lastStepStats;
};

Box3DReferenceBackend::Box3DReferenceBackend()
    : impl_(std::make_unique<Impl>()) {}

Box3DReferenceBackend::~Box3DReferenceBackend() {
    shutdown();
}

Box3DReferenceBackend::Box3DReferenceBackend(
    Box3DReferenceBackend&&) noexcept = default;

Box3DReferenceBackend& Box3DReferenceBackend::operator=(
    Box3DReferenceBackend&&) noexcept = default;

bool Box3DReferenceBackend::initialize(const PhysicsInitContext& context) {
    if (isInitialized()) {
        return true;
    }
    if (context.requestedBackend != BackendType::Box3DReference) {
        LOG_ERROR("Box3D backend received initialization for '{}'",
                  backendTypeName(context.requestedBackend));
        return false;
    }
    if (context.maxBodies == 0
        || context.maxBodies > kMaximumBodyCapacity
        || context.maxPairs == 0
        || context.maxPairs > kMaximumPairCapacity
        || context.maxContacts == 0
        || context.maxContacts > kMaximumContactCapacity) {
        LOG_ERROR("Box3D backend capacities are invalid or unbounded");
        return false;
    }

    impl_->initContext = context;
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = {0.0f, -kGravityMagnitude, 0.0f};
    worldDef.workerCount = std::clamp(context.box3dWorkerThreads, 1u, 32u);
    // A second static slot lets setTerrain build a complete replacement while
    // the working terrain remains live.
    worldDef.capacity.staticBodyCount = 2;
    worldDef.capacity.staticShapeCount = 2;
    worldDef.capacity.dynamicBodyCount = static_cast<int>(
        std::min<uint32_t>(context.maxBodies,
                           static_cast<uint32_t>(std::numeric_limits<int>::max())));
    worldDef.capacity.dynamicShapeCount = worldDef.capacity.dynamicBodyCount;
    worldDef.capacity.contactCount = static_cast<int>(
        std::min<uint32_t>(context.maxContacts,
                           static_cast<uint32_t>(std::numeric_limits<int>::max())));
    impl_->world = b3CreateWorld(&worldDef);
    if (!b3World_IsValid(impl_->world)) {
        LOG_ERROR("Failed to create Box3D reference world");
        impl_->world = b3_nullWorldId;
        return false;
    }
    impl_->dynamicBodies.reserve(context.maxBodies);
    return true;
}

void Box3DReferenceBackend::shutdown() {
    if (!impl_) {
        return;
    }
    if (b3World_IsValid(impl_->world)) {
        impl_->clearTerrain();
        b3DestroyWorld(impl_->world);
    } else if (impl_->terrainData != nullptr) {
        b3DestroyHeightField(impl_->terrainData);
        impl_->terrainData = nullptr;
    }
    impl_->world = b3_nullWorldId;
    impl_->dynamicBodies.clear();
    impl_->characters.clear();
    impl_->characterGenerations.clear();
    impl_->waterSurfaceSampler = {};
    impl_->lastReadStats = {};
    impl_->lastStepStats = {};
    impl_->accumulator = 0.0;
    impl_->waterTime = 0.0f;
    impl_->waterEnabled = false;
    impl_->bodyCapacityOverflow = false;
}

bool Box3DReferenceBackend::isInitialized() const noexcept {
    return impl_ && b3World_IsValid(impl_->world);
}

BackendType Box3DReferenceBackend::type() const noexcept {
    return BackendType::Box3DReference;
}

BackendCapabilities Box3DReferenceBackend::capabilities() const noexcept {
    return {
        .gpuResidentState = false,
        .directRenderView = false,
        .synchronousCharacter = true,
        .deterministicFloat = true,
        .lockstep = false,
        .bodyBodyContacts = true,
        .continuousCollision = true,
        .asynchronousQueries = false,
        .eventReadback = false,
    };
}

bool Box3DReferenceBackend::setTerrain(
    std::span<const uint16_t> samples, uint32_t width, uint32_t height,
    float heightScale, float cellScale) {
    if (!isInitialized() || width < 2 || height < 2
        || width > kMaximumTerrainExtent
        || height > kMaximumTerrainExtent
        || !std::isfinite(heightScale) || heightScale <= 0.0f
        || heightScale > kMaximumTerrainScale
        || !std::isfinite(cellScale) || cellScale <= 0.0f
        || cellScale > kMaximumTerrainScale
        || samples.size() != static_cast<size_t>(width) * height
        || width > static_cast<uint32_t>(std::numeric_limits<int>::max())
        || height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const box3d_conversion::CanonicalHeightFieldLayout layout{
        width, height};
    std::vector<float> rotatedHeights(samples.size());
    for (uint32_t localZ = 0; localZ < layout.localCountZ(); ++localZ) {
        for (uint32_t localX = 0; localX < layout.localCountX(); ++localX) {
            const size_t localIndex = static_cast<size_t>(localZ)
                * layout.localCountX() + localX;
            rotatedHeights[localIndex] = terrain_topology::worldHeight(
                static_cast<float>(samples[layout.sourceIndex(
                    localX, localZ)]), heightScale);
        }
    }

    b3HeightFieldDef heightFieldDef{};
    heightFieldDef.heights = rotatedHeights.data();
    heightFieldDef.scale = {cellScale, 1.0f, cellScale};
    heightFieldDef.countX = static_cast<int>(layout.localCountX());
    heightFieldDef.countZ = static_cast<int>(layout.localCountZ());
    heightFieldDef.globalMinimumHeight = -heightScale;
    heightFieldDef.globalMaximumHeight = heightScale;
    heightFieldDef.clockwiseWinding = false;
    b3HeightFieldData* replacementData =
        b3CreateHeightField(&heightFieldDef);
    if (replacementData == nullptr) {
        LOG_ERROR("Box3D failed to cook {}x{} terrain", width, height);
        return false;
    }

    const glm::vec2 origin = terrain_topology::centeredOrigin(
        width, height, cellScale);
    constexpr float kSinCos45 = 0.7071067811865475244f;
    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.position = {-origin.x, 0.0f, origin.y};
    bodyDef.rotation = {{0.0f, kSinCos45, 0.0f}, kSinCos45};
    bodyDef.name = "voxys_canonical_terrain";
    const b3BodyId replacementBody = b3CreateBody(impl_->world, &bodyDef);
    if (!b3Body_IsValid(replacementBody)) {
        b3DestroyHeightField(replacementData);
        LOG_ERROR("Box3D failed to create terrain body");
        return false;
    }

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.baseMaterial.friction = 0.8f;
    const b3ShapeId replacementShape = b3CreateHeightFieldShape(
        replacementBody, &shapeDef, replacementData);
    if (!b3Shape_IsValid(replacementShape)) {
        b3DestroyBody(replacementBody);
        b3DestroyHeightField(replacementData);
        LOG_ERROR("Box3D failed to create terrain shape");
        return false;
    }

    impl_->clearTerrain();
    impl_->terrainData = replacementData;
    impl_->terrainBody = replacementBody;
    impl_->terrainShape = replacementShape;
    impl_->terrainWidth = width;
    impl_->terrainHeight = height;
    return true;
}

void Box3DReferenceBackend::clearTerrain() {
    if (impl_ && isInitialized()) {
        impl_->clearTerrain();
    }
}

bool Box3DReferenceBackend::hasTerrain() const noexcept {
    return isInitialized() && impl_->terrainData != nullptr
        && b3Body_IsValid(impl_->terrainBody)
        && b3Shape_IsValid(impl_->terrainShape);
}

void Box3DReferenceBackend::setWaterPlane(float height, bool enabled) {
    if (!impl_) {
        return;
    }
    impl_->waterHeight = height;
    impl_->waterEnabled = enabled && std::isfinite(height);
}

void Box3DReferenceBackend::setWaterSurfaceSampler(
    WaterSurfaceSampler sampler) {
    if (impl_) {
        impl_->waterSurfaceSampler = std::move(sampler);
    }
}

CharacterHandle Box3DReferenceBackend::createCharacter(
    const glm::vec3& feetPosition,
    const CharacterSettings& requestedSettings) {
    if (!isInitialized()
        || !isRepresentableAbsolutePosition(feetPosition)) {
        return InvalidCharacter;
    }

    size_t targetIndex = impl_->characters.size();
    for (size_t index = 0; index < impl_->characters.size(); ++index) {
        if (!impl_->characters[index]
            && impl_->characterGenerations[index]
                != std::numeric_limits<uint16_t>::max()) {
            targetIndex = index;
            break;
        }
    }
    if (targetIndex == impl_->characters.size()) {
        if (impl_->characters.size() >= kMaximumCharacterSlots) {
            return InvalidCharacter;
        }
        // Keep the two parallel arrays consistent even if growth throws.
        impl_->characters.reserve(impl_->characters.size() + 1u);
        impl_->characterGenerations.reserve(
            impl_->characterGenerations.size() + 1u);
    }

    auto slot = std::make_unique<Impl::CharacterSlot>();
    slot->position = feetPosition;
    slot->settings = sanitizeCharacterSettings(requestedSettings);
    impl_->refreshCharacterGround(*slot, true);

    if (targetIndex < impl_->characters.size()) {
        impl_->characters[targetIndex] = std::move(slot);
        return makeCharacterHandle(
            static_cast<uint32_t>(targetIndex),
            impl_->characterGenerations[targetIndex]);
    }
    impl_->characters.push_back(std::move(slot));
    impl_->characterGenerations.push_back(0u);
    return makeCharacterHandle(
        static_cast<uint32_t>(impl_->characters.size() - 1u), 0u);
}

void Box3DReferenceBackend::destroyCharacter(CharacterHandle handle) {
    if (!impl_ || !impl_->findCharacter(handle)) return;
    const uint32_t index = characterHandleSlot(handle);
    impl_->characters[index].reset();
    auto& generation = impl_->characterGenerations[index];
    if (generation != std::numeric_limits<uint16_t>::max()) ++generation;
}

bool Box3DReferenceBackend::setCharacterPosition(
    CharacterHandle handle, const glm::vec3& feetPosition) {
    if (!isInitialized()
        || !isRepresentableAbsolutePosition(feetPosition)) {
        return false;
    }
    Impl::CharacterSlot* character = impl_->findCharacter(handle);
    if (character == nullptr) {
        return false;
    }
    character->position = feetPosition;
    character->velocity = {0.0f, 0.0f, 0.0f};
    impl_->refreshCharacterGround(*character, true);
    return true;
}

CharacterMotion Box3DReferenceBackend::moveCharacter(
    CharacterHandle handle, const glm::vec3& desiredHorizontalVelocity,
    bool jump, float jumpSpeed, float gravity, float terminalVelocity,
    float deltaTime) {
    CharacterMotion result;
    if (!isInitialized()) {
        return result;
    }
    Impl::CharacterSlot* character = impl_->findCharacter(handle);
    if (character == nullptr) {
        return result;
    }

    const float frameTime = std::clamp(
        finiteOr(deltaTime, 0.0f), 0.0f, kMaximumFrameTime);
    const int substepCount = std::max(
        1, static_cast<int>(std::ceil(frameTime / kFixedTimeStep)));
    const float substepTime = frameTime / static_cast<float>(substepCount);
    gravity = std::clamp(
        finiteOr(gravity, 0.0f), 0.0f, kMaximumCharacterRate);
    terminalVelocity = std::clamp(
        finiteOr(terminalVelocity, 0.0f),
        0.0f, kMaximumCharacterRate);
    jumpSpeed = std::clamp(
        finiteOr(jumpSpeed, 0.0f), 0.0f, kMaximumCharacterRate);
    const glm::vec3 desired = finiteVector(desiredHorizontalVelocity)
        ? glm::clamp(
            desiredHorizontalVelocity,
            glm::vec3(-kMaximumCharacterRate),
            glm::vec3(kMaximumCharacterRate))
        : glm::vec3(0.0f);

    for (int substep = 0;
         substep < substepCount && substepTime > 0.0f; ++substep) {
        impl_->refreshCharacterGround(*character,
                                      character->velocity.y <= 0.1f);
        character->velocity.x = desired.x;
        character->velocity.z = desired.z;
        const bool shouldJump = jump && substep == 0 && character->grounded;
        if (shouldJump) {
            character->velocity.y = jumpSpeed;
            character->grounded = false;
        } else if (character->grounded && character->velocity.y <= 0.1f) {
            character->velocity.y = 0.0f;
        } else {
            character->velocity.y = std::max(
                character->velocity.y - gravity * substepTime,
                -terminalVelocity);
        }

        const glm::vec3 start = character->position;
        impl_->moveCapsule(*character,
                           character->velocity * substepTime);
        if (!shouldJump && character->velocity.y <= 0.0f) {
            impl_->refreshCharacterGround(*character, true);
        } else {
            character->grounded = false;
            character->onSteepGround = false;
            character->groundNormal = {0.0f, 1.0f, 0.0f};
        }

        if (substepTime > 0.0f && !character->grounded) {
            const glm::vec3 actualVelocity =
                (character->position - start) / substepTime;
            character->velocity.x = actualVelocity.x;
            character->velocity.z = actualVelocity.z;
            if (actualVelocity.y < character->velocity.y) {
                character->velocity.y = actualVelocity.y;
            }
        }
    }

    result.position = character->position;
    result.velocity = character->velocity;
    result.groundNormal = character->groundNormal;
    result.grounded = character->grounded;
    result.onSteepGround = character->onSteepGround;
    return result;
}

bool Box3DReferenceBackend::throwBody(
    ThrowableShape shape, const glm::vec3& position,
    const glm::vec3& velocity) {
    if (!isInitialized() || shape >= ThrowableShape::Count
        || !finiteVector(position) || !finiteVector(velocity)) {
        return false;
    }
    if (impl_->dynamicBodies.size() >= impl_->initContext.maxBodies) {
        impl_->bodyCapacityOverflow = true;
        return false;
    }

    const size_t slotIndex = impl_->dynamicBodies.size();
    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.type = b3_dynamicBody;
    bodyDef.position = toPosition(position);
    bodyDef.linearVelocity = toVector(velocity);
    bodyDef.angularVelocity = {3.5f, 5.0f, 2.5f};
    bodyDef.userData = reinterpret_cast<void*>(slotIndex + 1u);
    bodyDef.name = throwableShapeName(shape);
    const b3BodyId body = b3CreateBody(impl_->world, &bodyDef);
    if (!b3Body_IsValid(body)) {
        return false;
    }

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.baseMaterial.friction = 0.65f;
    shapeDef.baseMaterial.restitution =
        shape == ThrowableShape::Sphere ? 0.55f : 0.25f;
    b3ShapeId shapeId = b3_nullShapeId;
    switch (shape) {
        case ThrowableShape::Sphere: {
            const b3Sphere sphere{{0.0f, 0.0f, 0.0f}, 0.6f};
            shapeId = b3CreateSphereShape(body, &shapeDef, &sphere);
            break;
        }
        case ThrowableShape::Cube: {
            const b3BoxHull box = b3MakeCubeHull(0.55f);
            shapeId = b3CreateHullShape(body, &shapeDef, &box.base);
            break;
        }
        case ThrowableShape::Box: {
            const b3BoxHull box = b3MakeBoxHull(0.9f, 0.4f, 0.5f);
            shapeId = b3CreateHullShape(body, &shapeDef, &box.base);
            break;
        }
        case ThrowableShape::Capsule: {
            const b3Capsule capsule{
                {0.0f, -0.55f, 0.0f},
                {0.0f, 0.55f, 0.0f},
                0.35f,
            };
            shapeId = b3CreateCapsuleShape(body, &shapeDef, &capsule);
            break;
        }
        case ThrowableShape::Cylinder: {
            b3HullData* cylinder = b3CreateCylinder(
                1.1f, 0.45f, 0.0f, kCylinderSides);
            if (cylinder != nullptr) {
                shapeId = b3CreateHullShape(
                    body, &shapeDef, cylinder);
                b3DestroyHull(cylinder);
            }
            break;
        }
        case ThrowableShape::Count:
            break;
    }

    if (!b3Shape_IsValid(shapeId)) {
        b3DestroyBody(body);
        return false;
    }
    impl_->dynamicBodies.push_back({
        .body = body,
        .shape = shape,
        .dimensions = throwableShapeDimensions(shape),
    });
    return true;
}

void Box3DReferenceBackend::stepCpu(float deltaTime) {
    using Clock = std::chrono::steady_clock;
    const auto totalStart = Clock::now();
    if (impl_) {
        impl_->lastStepStats = {};
    }
    if (!isInitialized()) {
        return;
    }

    const float frameTime = std::clamp(
        finiteOr(deltaTime, 0.0f), 0.0f, kMaximumFrameTime);
    impl_->accumulator = std::min(
        impl_->accumulator + static_cast<double>(frameTime),
        static_cast<double>(kMaximumFrameTime));

    uint32_t tickCount = 0;
    while (impl_->accumulator + 1.0e-12
               >= static_cast<double>(kFixedTimeStep)
           && tickCount < kMaxCatchUpTicks) {
        if (impl_->waterEnabled) {
            const auto waterStart = Clock::now();
            impl_->applyWaterForces(kFixedTimeStep);
            impl_->lastStepStats.waterMs +=
                std::chrono::duration<double, std::milli>(
                    Clock::now() - waterStart).count();
        }

        const auto simulationStart = Clock::now();
        b3World_Step(impl_->world, kFixedTimeStep,
                     static_cast<int>(kInternalSubsteps));
        impl_->lastStepStats.simulationMs +=
            std::chrono::duration<double, std::milli>(
                Clock::now() - simulationStart).count();
        impl_->collectBodyMoveEvents();
        impl_->accumulator -= static_cast<double>(kFixedTimeStep);
        ++tickCount;
    }
    impl_->lastStepStats.substepCount = tickCount * kInternalSubsteps;
    impl_->lastStepStats.totalMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - totalStart).count();
}

std::vector<DynamicBodySnapshot> Box3DReferenceBackend::dynamicBodies(
    size_t additionalCapacity) const {
    std::vector<DynamicBodySnapshot> result;
    if (impl_) {
        impl_->lastReadStats = {};
    }
    if (!isInitialized()) {
        return result;
    }

    impl_->lastReadStats.bodyCount = impl_->dynamicBodies.size();
    result.reserve(impl_->dynamicBodies.size() + additionalCapacity);
    for (const Impl::DynamicSlot& slot : impl_->dynamicBodies) {
        if (!b3Body_IsValid(slot.body)) {
            continue;
        }
        const bool awake = b3Body_IsAwake(slot.body);
        const bool mustRead = awake || slot.snapshotDirty
            || !slot.cacheValid;
        if (mustRead) {
            slot.cachedPosition = toGlmPosition(
                b3Body_GetPosition(slot.body));
            slot.cachedRotation = box3d_conversion::toGlm(
                b3Body_GetRotation(slot.body));
            slot.cacheValid = true;
            slot.snapshotDirty = awake;
            ++impl_->lastReadStats.lockedBodyCount;
        } else {
            ++impl_->lastReadStats.cachedBodyCount;
        }
        result.push_back({
            .shape = slot.shape,
            .position = slot.cachedPosition,
            .rotation = slot.cachedRotation,
            .dimensions = slot.dimensions,
            .active = awake,
        });
    }
    return result;
}

DynamicBodyReadStats
Box3DReferenceBackend::lastDynamicBodyReadStats() const noexcept {
    return impl_ ? impl_->lastReadStats : DynamicBodyReadStats{};
}

PhysicsStats Box3DReferenceBackend::stats() const noexcept {
    PhysicsStats result;
    result.backend = BackendType::Box3DReference;
    result.arithmeticMode = PhysicsArithmeticMode::FastFloat;
    if (!isInitialized()) {
        return result;
    }

    const b3Counters counters = b3World_GetCounters(impl_->world);
    result.residentBodies = static_cast<uint32_t>(impl_->dynamicBodies.size());
    result.activeBodies = std::min(
        result.residentBodies,
        static_cast<uint32_t>(std::max(
            b3World_GetAwakeBodyCount(impl_->world), 0)));
    result.sleepingBodies = result.residentBodies - result.activeBodies;
    result.bodyCapacity = impl_->initContext.maxBodies;
    result.pairCapacity = impl_->initContext.maxPairs;
    result.contactCapacity = impl_->initContext.maxContacts;
    result.manifoldCapacity = impl_->initContext.maxManifolds;
    result.workerConcurrency = static_cast<uint32_t>(
        std::max(b3World_GetWorkerCount(impl_->world), 1));
    result.estimatedPersistentBytes = static_cast<size_t>(
        std::max(counters.byteCount, 0));
    result.scratchBytes = static_cast<size_t>(
        std::max(counters.arenaCapacity, 0));
    result.bodyCapacityOverflow = impl_->bodyCapacityOverflow;
    result.contactCapacityOverflow = counters.contactCount
        > static_cast<int>(impl_->initContext.maxContacts);
    result.residentBodyUsage = {
        result.residentBodies, result.bodyCapacity, result.residentBodies,
        result.bodyCapacityOverflow};
    result.activeBodyUsage = {
        result.activeBodies, impl_->initContext.maxActiveBodies,
        result.activeBodies, false};
    result.commandUsage.capacity = impl_->initContext.gpu.commandCapacity;
    result.uniquePairUsage.capacity = result.pairCapacity;
    result.contactUsage = {
        static_cast<uint32_t>(std::max(counters.contactCount, 0)),
        result.contactCapacity,
        static_cast<uint32_t>(std::max(counters.contactCount, 0)),
        result.contactCapacityOverflow};
    result.manifoldUsage.capacity = result.manifoldCapacity;
    result.visibleBodyUsage.capacity = result.bodyCapacity;
    return result;
}

PhysicsStepStats Box3DReferenceBackend::lastStepStats() const noexcept {
    return impl_ ? impl_->lastStepStats : PhysicsStepStats{};
}

} // namespace voxy::physics

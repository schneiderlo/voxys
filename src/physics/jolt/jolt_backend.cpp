#include "physics/jolt/jolt_backend.hpp"

#include "core/log.hpp"
#include "physics/terrain_topology.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#if !defined(__EMSCRIPTEN__)
#include <Jolt/Core/JobSystemThreadPool.h>
#endif
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace voxy::physics {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving{0};
constexpr JPH::BroadPhaseLayer Moving{1};
constexpr uint32_t Count = 2;
} // namespace BroadPhaseLayers

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::Count;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::NonMoving
            ? BroadPhaseLayers::NonMoving
            : BroadPhaseLayers::Moving;
    }
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer,
                       JPH::BroadPhaseLayer broadPhaseLayer) const override {
        if (layer == Layers::NonMoving) {
            return broadPhaseLayer == BroadPhaseLayers::Moving;
        }
        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first,
                       JPH::ObjectLayer second) const override {
        if (first == Layers::NonMoving) {
            return second == Layers::Moving;
        }
        return true;
    }
};

std::mutex gRuntimeMutex;
uint32_t gRuntimeUsers = 0;

void joltTrace(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    std::vsnprintf(buffer, sizeof(buffer), format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(args);
    LOG_DEBUG("Jolt: {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool joltAssert(const char* expression, const char* message,
                const char* file, JPH::uint line) {
    LOG_ERROR("Jolt assertion at {}:{}: {} ({})", file, line, expression,
              message != nullptr ? message : "no message");
    return true;
}
#endif

void retainJoltRuntime() {
    std::scoped_lock lock(gRuntimeMutex);
    if (gRuntimeUsers++ != 0) {
        return;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTrace;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = joltAssert;
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void releaseJoltRuntime() {
    std::scoped_lock lock(gRuntimeMutex);
    if (gRuntimeUsers == 0 || --gRuntimeUsers != 0) {
        return;
    }

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    JPH::Trace = nullptr;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = nullptr;
#endif
}

constexpr uint32_t kTileSampleCount = 256;
constexpr uint32_t kTileCellCount = kTileSampleCount - 1;
constexpr int32_t kTileRadius = 1;
constexpr uint32_t kMaxBodies = 16384;
constexpr size_t kTempAllocatorBytes = 16u * 1024u * 1024u;
constexpr float kMaxFrameTime = 8.0f / 60.0f;
constexpr float kMaxSubstep = 1.0f / 60.0f;
constexpr float kWaterWaveSampleBand = 8.0f;

float throwableBuoyancy(JoltBackend::ThrowableShape shape) {
    using Shape = JoltBackend::ThrowableShape;
    switch (shape) {
        case Shape::Sphere: return 1.08f;
        case Shape::Cube: return 0.94f;
        case Shape::Box: return 1.18f;
        case Shape::Capsule: return 1.10f;
        case Shape::Cylinder: return 0.98f;
        case Shape::Count: break;
    }
    return 1.0f;
}

uint64_t tileKey(int32_t x, int32_t z) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u)
         | static_cast<uint32_t>(z);
}

glm::vec3 toGlmVector(JPH::Vec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

glm::vec3 toGlmPosition(JPH::RVec3Arg value) {
    return {value.GetX(), value.GetY(), value.GetZ()};
}

JPH::RVec3 toJoltPosition(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}

} // namespace

class JoltBackend::Impl : public JPH::BodyActivationListener {
public:
    struct Terrain {
        std::span<const uint16_t> samples;
        uint32_t width = 0;
        uint32_t height = 0;
        float heightScale = 1.0f;
        float cellScale = 1.0f;
        std::unordered_map<uint64_t, JPH::BodyID> bodies;

        [[nodiscard]] bool valid() const noexcept {
            return width >= 2 && height >= 2 && cellScale > 0.0f
                && samples.size() == static_cast<size_t>(width) * height;
        }
    };

    struct CharacterSlot {
        JPH::Ref<JPH::CharacterVirtual> character;
        CharacterSettings settings;
    };

    struct DynamicSlot {
        JPH::BodyID body;
        ThrowableShape shape = ThrowableShape::Sphere;
        glm::vec3 dimensions{1.0f};
        mutable glm::vec3 cachedPosition{0.0f};
        mutable glm::quat cachedRotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    void OnBodyActivated(const JPH::BodyID& bodyID, JPH::uint64) override {
        const uint32_t index = bodyID.GetIndex();
        if (index < snapshotTransformDirty.size()) {
            snapshotTransformDirty[index].store(true,
                                                std::memory_order_relaxed);
        }
    }

    void OnBodyDeactivated(const JPH::BodyID& bodyID, JPH::uint64) override {
        OnBodyActivated(bodyID, 0);
    }

    ~Impl() { shutdown(); }

    bool initialize(const PhysicsInitContext& context) {
        if (system) {
            return true;
        }

        if (context.requestedBackend != BackendType::JoltLegacy ||
            context.maxBodies == 0 || context.maxBodies > kMaxBodies ||
            context.maxPairs == 0 || context.maxContacts == 0) {
            LOG_ERROR(
                "Invalid Jolt physics capacities: bodies={} (max {}), pairs={}, "
                "contacts={}",
                context.maxBodies, kMaxBodies, context.maxPairs,
                context.maxContacts);
            return false;
        }

        retainJoltRuntime();
        runtimeRetained = true;

        initContext = context;

        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kTempAllocatorBytes);
#if defined(__EMSCRIPTEN__)
        if (context.joltJobSystem == JoltJobSystemMode::ThreadPool) {
            LOG_ERROR("The Jolt thread-pool baseline is unavailable in this WASM build");
            shutdown();
            return false;
        }
#else
        if (context.joltJobSystem == JoltJobSystemMode::ThreadPool) {
            const int workerThreads = context.joltWorkerThreads == 0
                ? -1
                : static_cast<int>(context.joltWorkerThreads);
            jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);
        }
#endif
        if (!jobSystem) {
            jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(
                JPH::cMaxPhysicsJobs);
        }
        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(context.maxBodies, 0, context.maxPairs,
                     context.maxContacts,
                     broadPhaseInterface,
                     objectVsBroadPhaseFilter, objectLayerPairFilter);
        system->SetBodyActivationListener(this);
        return true;
    }

    void shutdown() {
        if (!system && !runtimeRetained) {
            return;
        }

        if (system) system->SetBodyActivationListener(nullptr);
        characters.clear();
        clearDynamicBodies();
        clearTerrain();
        system.reset();
        jobSystem.reset();
        tempAllocator.reset();

        if (runtimeRetained) {
            releaseJoltRuntime();
            runtimeRetained = false;
        }
    }

    void clearDynamicBodies() {
        if (system) {
            auto& bodyInterface = system->GetBodyInterface();
            for (const DynamicSlot& slot : dynamicBodies) {
                bodyInterface.RemoveBody(slot.body);
                bodyInterface.DestroyBody(slot.body);
            }
        }
        dynamicBodies.clear();
        waterBodyIDs.clear();
        waterPositions.clear();
        lastDynamicBodyReadStats = {};
    }

    void clearTerrain() {
        if (system) {
            auto& bodyInterface = system->GetBodyInterface();
            for (const auto& [key, body] : terrain.bodies) {
                (void)key;
                bodyInterface.RemoveBody(body);
                bodyInterface.DestroyBody(body);
            }
        }
        terrain = {};
    }

    bool createTerrainTile(int32_t tileX, int32_t tileZ) {
        if (!terrain.valid() || !system) {
            return false;
        }

        std::vector<float> heights(
            static_cast<size_t>(kTileSampleCount) * kTileSampleCount,
            JPH::HeightFieldShapeConstants::cNoCollisionValue);

        const uint32_t startX = static_cast<uint32_t>(tileX) * kTileCellCount;
        const uint32_t startZ = static_cast<uint32_t>(tileZ) * kTileCellCount;
        bool hasCollision = false;

        for (uint32_t z = 0; z < kTileSampleCount; ++z) {
            const uint32_t sourceZ = startZ + z;
            if (sourceZ >= terrain.height) {
                break;
            }
            for (uint32_t x = 0; x < kTileSampleCount; ++x) {
                const uint32_t sourceX = startX + x;
                if (sourceX >= terrain.width) {
                    break;
                }
                heights[static_cast<size_t>(z) * kTileSampleCount + x] =
                    terrain_topology::normalizedHeight(
                        static_cast<float>(terrain.samples[
                            static_cast<size_t>(sourceZ) * terrain.width +
                            sourceX]));
                hasCollision = true;
            }
        }

        if (!hasCollision) {
            return false;
        }

        const glm::vec2 terrainOrigin = terrain_topology::centeredOrigin(
            terrain.width, terrain.height, terrain.cellScale);
        const JPH::Vec3 offset{
            -terrainOrigin.x + static_cast<float>(startX) * terrain.cellScale,
            -terrain.heightScale,
            -terrainOrigin.y + static_cast<float>(startZ) * terrain.cellScale};
        const JPH::Vec3 scale{
            terrain.cellScale, 2.0f * terrain.heightScale, terrain.cellScale};

        JPH::HeightFieldShapeSettings settings(
            heights.data(), offset, scale, kTileSampleCount);
        settings.mBlockSize = 4;
        settings.mBitsPerSample = 8;
        auto shapeResult = settings.Create();
        if (shapeResult.HasError()) {
            LOG_ERROR("Jolt terrain tile ({}, {}) failed: {}", tileX, tileZ,
                      shapeResult.GetError().c_str());
            return false;
        }

        const JPH::BodyCreationSettings bodySettings(
            shapeResult.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
            JPH::EMotionType::Static, Layers::NonMoving);
        const JPH::BodyID body = system->GetBodyInterface().CreateAndAddBody(
            bodySettings, JPH::EActivation::DontActivate);
        if (body.IsInvalid()) {
            LOG_ERROR("Jolt could not allocate terrain tile ({}, {})", tileX, tileZ);
            return false;
        }

        terrain.bodies.emplace(tileKey(tileX, tileZ), body);
        return true;
    }

    void streamTerrainAt(const glm::vec3& position) {
        if (!terrain.valid() || !system) {
            return;
        }

        const glm::vec2 terrainOrigin = terrain_topology::centeredOrigin(
            terrain.width, terrain.height, terrain.cellScale);
        const int32_t maxTileX = static_cast<int32_t>((terrain.width - 2)
                                                     / kTileCellCount);
        const int32_t maxTileZ = static_cast<int32_t>((terrain.height - 2)
                                                     / kTileCellCount);
        const int32_t centerX = std::clamp(
            static_cast<int32_t>(std::floor(
                (position.x + terrainOrigin.x) /
                terrain.cellScale / kTileCellCount)),
            0, maxTileX);
        const int32_t centerZ = std::clamp(
            static_cast<int32_t>(std::floor(
                (position.z + terrainOrigin.y) /
                terrain.cellScale / kTileCellCount)),
            0, maxTileZ);

        std::unordered_set<uint64_t> wanted;
        for (int32_t z = std::max(0, centerZ - kTileRadius);
             z <= std::min(maxTileZ, centerZ + kTileRadius); ++z) {
            for (int32_t x = std::max(0, centerX - kTileRadius);
                 x <= std::min(maxTileX, centerX + kTileRadius); ++x) {
                const uint64_t key = tileKey(x, z);
                wanted.insert(key);
                if (!terrain.bodies.contains(key)) {
                    createTerrainTile(x, z);
                }
            }
        }

        auto& bodyInterface = system->GetBodyInterface();
        for (auto it = terrain.bodies.begin(); it != terrain.bodies.end();) {
            if (wanted.contains(it->first)) {
                ++it;
                continue;
            }
            bodyInterface.RemoveBody(it->second);
            bodyInterface.DestroyBody(it->second);
            it = terrain.bodies.erase(it);
        }
    }

    CharacterSlot* findCharacter(CharacterHandle handle) {
        if (handle == InvalidCharacter || handle > characters.size()) {
            return nullptr;
        }
        return characters[handle - 1].get();
    }

    void refreshContacts(CharacterSlot& slot) {
        slot.character->RefreshContacts(
            system->GetDefaultBroadPhaseLayerFilter(Layers::Moving),
            system->GetDefaultLayerFilter(Layers::Moving), {}, {},
            *tempAllocator);
    }

    BroadPhaseLayerInterface broadPhaseInterface;
    ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter;
    ObjectLayerPairFilter objectLayerPairFilter;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystem> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> system;
    Terrain terrain;
    std::vector<std::unique_ptr<CharacterSlot>> characters;
    std::vector<DynamicSlot> dynamicBodies;
    std::array<std::atomic_bool, kMaxBodies> snapshotTransformDirty{};
    mutable DynamicBodyReadStats lastDynamicBodyReadStats;
    std::vector<JPH::BodyID> waterBodyIDs;
    std::vector<JPH::RVec3> waterPositions;
    float waterHeight = 0.0f;
    float waterTime = 0.0f;
    bool waterEnabled = false;
    WaterSurfaceSampler waterSurfaceSampler;
    PhysicsInitContext initContext;
    PhysicsStepStats lastStepStats;
    bool runtimeRetained = false;
};

JoltBackend::JoltBackend() : impl_(std::make_unique<Impl>()) {}
JoltBackend::~JoltBackend() = default;
JoltBackend::JoltBackend(JoltBackend&&) noexcept = default;
JoltBackend& JoltBackend::operator=(JoltBackend&&) noexcept = default;

bool JoltBackend::initialize(const PhysicsInitContext& context) {
    return impl_ && impl_->initialize(context);
}

void JoltBackend::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

bool JoltBackend::isInitialized() const noexcept {
    return impl_ && impl_->system != nullptr;
}

BackendType JoltBackend::type() const noexcept {
    return BackendType::JoltLegacy;
}

BackendCapabilities JoltBackend::capabilities() const noexcept {
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

bool JoltBackend::setTerrain(std::span<const uint16_t> samples,
                              uint32_t width, uint32_t height,
                              float heightScale, float cellScale) {
    if (!isInitialized() || width < 2 || height < 2 || heightScale <= 0.0f
        || cellScale <= 0.0f
        || samples.size() != static_cast<size_t>(width) * height) {
        return false;
    }

    impl_->clearTerrain();
    impl_->terrain.samples = samples;
    impl_->terrain.width = width;
    impl_->terrain.height = height;
    impl_->terrain.heightScale = heightScale;
    impl_->terrain.cellScale = cellScale;

    for (const auto& slot : impl_->characters) {
        if (slot) {
            impl_->streamTerrainAt(toGlmPosition(slot->character->GetPosition()));
            impl_->refreshContacts(*slot);
            break;
        }
    }
    return true;
}

void JoltBackend::clearTerrain() {
    if (impl_) {
        impl_->clearTerrain();
    }
}

bool JoltBackend::hasTerrain() const noexcept {
    return impl_ && impl_->terrain.valid();
}

void JoltBackend::setWaterPlane(float height, bool enabled) {
    if (!impl_) return;
    impl_->waterHeight = height;
    impl_->waterEnabled = enabled && std::isfinite(height);
}

void JoltBackend::setWaterSurfaceSampler(WaterSurfaceSampler sampler) {
    if (impl_) impl_->waterSurfaceSampler = std::move(sampler);
}

JoltBackend::CharacterHandle JoltBackend::createCharacter(
    const glm::vec3& feetPosition, const CharacterSettings& requestedSettings) {
    if (!isInitialized()) {
        return InvalidCharacter;
    }

    CharacterSettings settings = requestedSettings;
    settings.radius = std::max(settings.radius, 0.05f);
    settings.height = std::max(settings.height, 2.0f * settings.radius + 0.02f);
    settings.maxSlopeAngleDegrees = std::clamp(
        settings.maxSlopeAngleDegrees, 0.0f, 89.0f);
    settings.stepUp = std::max(settings.stepUp, 0.0f);
    settings.stepDown = std::max(settings.stepDown, 0.0f);

    impl_->streamTerrainAt(feetPosition);

    const float cylinderHalfHeight = 0.5f * settings.height - settings.radius;
    const JPH::RotatedTranslatedShapeSettings shapeSettings(
        JPH::Vec3(0.0f, 0.5f * settings.height, 0.0f),
        JPH::Quat::sIdentity(),
        new JPH::CapsuleShape(cylinderHalfHeight, settings.radius));
    auto shapeResult = shapeSettings.Create();
    if (shapeResult.HasError()) {
        LOG_ERROR("Jolt character shape failed: {}", shapeResult.GetError().c_str());
        return InvalidCharacter;
    }

    JPH::CharacterVirtualSettings characterSettings;
    characterSettings.mShape = shapeResult.Get();
    characterSettings.mMaxSlopeAngle = JPH::DegreesToRadians(
        settings.maxSlopeAngleDegrees);
    characterSettings.mSupportingVolume = JPH::Plane(
        JPH::Vec3::sAxisY(), -settings.radius);
    characterSettings.mEnhancedInternalEdgeRemoval = true;

    auto slot = std::make_unique<Impl::CharacterSlot>();
    slot->settings = settings;
    slot->character = new JPH::CharacterVirtual(
        &characterSettings, toJoltPosition(feetPosition), JPH::Quat::sIdentity(),
        0, impl_->system.get());
    impl_->refreshContacts(*slot);

    for (size_t i = 0; i < impl_->characters.size(); ++i) {
        if (!impl_->characters[i]) {
            impl_->characters[i] = std::move(slot);
            return static_cast<CharacterHandle>(i + 1);
        }
    }
    impl_->characters.push_back(std::move(slot));
    return static_cast<CharacterHandle>(impl_->characters.size());
}

void JoltBackend::destroyCharacter(CharacterHandle handle) {
    if (!impl_ || handle == InvalidCharacter || handle > impl_->characters.size()) {
        return;
    }
    impl_->characters[handle - 1].reset();
}

bool JoltBackend::setCharacterPosition(CharacterHandle handle,
                                        const glm::vec3& feetPosition) {
    if (!isInitialized()) {
        return false;
    }
    auto* slot = impl_->findCharacter(handle);
    if (!slot) {
        return false;
    }

    impl_->streamTerrainAt(feetPosition);
    slot->character->SetPosition(toJoltPosition(feetPosition));
    slot->character->SetLinearVelocity(JPH::Vec3::sZero());
    impl_->refreshContacts(*slot);
    return true;
}

JoltBackend::CharacterMotion JoltBackend::moveCharacter(
    CharacterHandle handle, const glm::vec3& desiredHorizontalVelocity,
    bool jump, float jumpSpeed, float gravity, float terminalVelocity,
    float deltaTime) {
    CharacterMotion result;
    if (!isInitialized()) {
        return result;
    }
    auto* slot = impl_->findCharacter(handle);
    if (!slot) {
        return result;
    }

    auto& character = *slot->character;
    const float frameTime = std::clamp(deltaTime, 0.0f, kMaxFrameTime);
    const int substeps = std::max(1, static_cast<int>(std::ceil(frameTime / kMaxSubstep)));
    const float stepTime = frameTime / static_cast<float>(substeps);
    gravity = std::max(gravity, 0.0f);
    terminalVelocity = std::max(terminalVelocity, 0.0f);

    for (int step = 0; step < substeps && stepTime > 0.0f; ++step) {
        impl_->streamTerrainAt(toGlmPosition(character.GetPosition()));
        character.UpdateGroundVelocity();

        const JPH::Vec3 up = character.GetUp();
        const JPH::Vec3 oldVelocity = character.GetLinearVelocity();
        const JPH::Vec3 verticalVelocity = oldVelocity.Dot(up) * up;
        const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
        const bool movingTowardsGround =
            verticalVelocity.GetY() - groundVelocity.GetY() < 0.1f;
        const bool supported =
            character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround
            && !character.IsSlopeTooSteep(character.GetGroundNormal());

        JPH::Vec3 velocity = supported && movingTowardsGround
            ? groundVelocity
            : verticalVelocity;
        JPH::Vec3 horizontal = character.CancelVelocityTowardsSteepSlopes(
            JPH::Vec3(desiredHorizontalVelocity.x, 0.0f,
                      desiredHorizontalVelocity.z));
        velocity += horizontal;

        if (jump && step == 0 && supported && movingTowardsGround) {
            velocity += std::max(jumpSpeed, 0.0f) * up;
        } else {
            velocity -= gravity * stepTime * up;
        }
        if (velocity.GetY() < -terminalVelocity) {
            velocity.SetY(-terminalVelocity);
        }
        character.SetLinearVelocity(velocity);

        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mWalkStairsStepUp = slot->settings.stepUp * up;
        updateSettings.mStickToFloorStepDown = -slot->settings.stepDown * up;
        character.ExtendedUpdate(
            stepTime, -gravity * up, updateSettings,
            impl_->system->GetDefaultBroadPhaseLayerFilter(Layers::Moving),
            impl_->system->GetDefaultLayerFilter(Layers::Moving), {}, {},
            *impl_->tempAllocator);
    }

    result.position = toGlmPosition(character.GetPosition());
    result.velocity = toGlmVector(character.GetLinearVelocity());
    const auto groundState = character.GetGroundState();
    result.grounded = groundState == JPH::CharacterBase::EGroundState::OnGround;
    result.onSteepGround =
        groundState == JPH::CharacterBase::EGroundState::OnSteepGround;
    const JPH::Vec3 normal = character.GetGroundNormal();
    result.groundNormal = normal.IsNearZero() ? glm::vec3(0.0f, 1.0f, 0.0f)
                                               : toGlmVector(normal.Normalized());
    return result;
}

bool JoltBackend::throwBody(ThrowableShape shape, const glm::vec3& position,
                             const glm::vec3& velocity) {
    if (!isInitialized() || shape >= ThrowableShape::Count) {
        return false;
    }

    impl_->streamTerrainAt(position);

    JPH::RefConst<JPH::Shape> bodyShape;
    const glm::vec3 dimensions = throwableShapeDimensions(shape);
    switch (shape) {
        case ThrowableShape::Sphere:
            bodyShape = new JPH::SphereShape(0.6f);
            break;
        case ThrowableShape::Cube:
            bodyShape = new JPH::BoxShape(JPH::Vec3::sReplicate(0.55f));
            break;
        case ThrowableShape::Box:
            bodyShape = new JPH::BoxShape(JPH::Vec3(0.9f, 0.4f, 0.5f));
            break;
        case ThrowableShape::Capsule:
            bodyShape = new JPH::CapsuleShape(0.55f, 0.35f);
            break;
        case ThrowableShape::Cylinder:
            bodyShape = new JPH::CylinderShape(0.55f, 0.45f);
            break;
        case ThrowableShape::Count:
            return false;
    }

    JPH::BodyCreationSettings settings(
        bodyShape, toJoltPosition(position), JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mFriction = 0.65f;
    settings.mRestitution = shape == ThrowableShape::Sphere ? 0.55f : 0.25f;
    const JPH::BodyID body = impl_->system->GetBodyInterface().CreateAndAddBody(
        settings, JPH::EActivation::Activate);
    if (body.IsInvalid()) {
        return false;
    }

    auto& bodyInterface = impl_->system->GetBodyInterface();
    bodyInterface.SetLinearVelocity(body, JPH::Vec3(velocity.x, velocity.y, velocity.z));
    bodyInterface.SetAngularVelocity(body, JPH::Vec3(3.5f, 5.0f, 2.5f));
    impl_->dynamicBodies.push_back({body, shape, dimensions});
    impl_->snapshotTransformDirty[body.GetIndex()].store(
        true, std::memory_order_relaxed);
    impl_->waterBodyIDs.push_back(body);
    return true;
}

BodyHandle JoltBackend::spawnBody(const BodySpawnDesc& requested) {
    if (!isInitialized() || requested.shape >= ThrowableShape::Count
        || impl_->dynamicBodies.size() >= impl_->initContext.maxBodies) {
        return {};
    }

    BodySpawnDesc desc = requested;
    if (!std::isfinite(desc.dimensions.x)
        || !std::isfinite(desc.dimensions.y)
        || !std::isfinite(desc.dimensions.z)
        || glm::any(glm::lessThanEqual(desc.dimensions, glm::vec3(0.0f)))) {
        desc.dimensions = throwableShapeDimensions(desc.shape);
    }

    const WorldPosition worldPosition = canonicalWorldPosition(
        desc.sector, glm::dvec3(desc.position));
    if (!isValidWorldPosition(worldPosition)) return {};
    const glm::dvec3 absolute = worldPositionToAbsolute(worldPosition);
    const glm::vec3 position(absolute);
    if (!std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z)) {
        return {};
    }
    impl_->streamTerrainAt(position);

    JPH::RefConst<JPH::Shape> bodyShape;
    switch (desc.shape) {
        case ThrowableShape::Sphere:
            bodyShape = new JPH::SphereShape(0.5f * desc.dimensions.x);
            break;
        case ThrowableShape::Cube:
        case ThrowableShape::Box:
            bodyShape = new JPH::BoxShape(JPH::Vec3(
                0.5f * desc.dimensions.x,
                0.5f * desc.dimensions.y,
                0.5f * desc.dimensions.z));
            break;
        case ThrowableShape::Capsule: {
            const float radius = 0.5f * desc.dimensions.x;
            const float halfHeight = 0.5f * desc.dimensions.y - radius;
            if (!(halfHeight > 0.0f)) return {};
            bodyShape = new JPH::CapsuleShape(halfHeight, radius);
            break;
        }
        case ThrowableShape::Cylinder:
            bodyShape = new JPH::CylinderShape(
                0.5f * desc.dimensions.y, 0.5f * desc.dimensions.x);
            break;
        case ThrowableShape::Count:
            return {};
    }

    float orientationLength = glm::length(desc.orientation);
    if (!std::isfinite(orientationLength) || orientationLength <= 1e-6f) {
        desc.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
        desc.orientation /= orientationLength;
    }
    const JPH::Quat rotation(
        desc.orientation.x, desc.orientation.y,
        desc.orientation.z, desc.orientation.w);
    JPH::BodyCreationSettings settings(
        bodyShape, toJoltPosition(position), rotation,
        JPH::EMotionType::Dynamic, Layers::Moving);
    settings.mFriction = 0.65f;
    settings.mRestitution =
        desc.shape == ThrowableShape::Sphere ? 0.55f : 0.25f;
    settings.mMotionQuality = desc.bullet
        ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass =
        1.0f / std::max(desc.inverseMass, 1e-6f);

    auto& bodyInterface = impl_->system->GetBodyInterface();
    const JPH::BodyID body = bodyInterface.CreateAndAddBody(
        settings, JPH::EActivation::Activate);
    if (body.IsInvalid()) return {};

    bodyInterface.SetLinearVelocity(body, JPH::Vec3(
        desc.linearVelocity.x, desc.linearVelocity.y,
        desc.linearVelocity.z));
    bodyInterface.SetAngularVelocity(body, JPH::Vec3(
        desc.angularVelocity.x, desc.angularVelocity.y,
        desc.angularVelocity.z));
    impl_->dynamicBodies.push_back({
        body, desc.shape, desc.dimensions, position, desc.orientation});
    impl_->snapshotTransformDirty[body.GetIndex()].store(
        true, std::memory_order_relaxed);
    impl_->waterBodyIDs.push_back(body);
    return {
        static_cast<uint32_t>(impl_->dynamicBodies.size()),
        0u,
    };
}

void JoltBackend::stepCpu(float deltaTime) {
    using Clock = std::chrono::steady_clock;
    const auto stepStart = Clock::now();
    impl_->lastStepStats = {};
    if (!isInitialized()) {
        return;
    }
    const float frameTime = std::clamp(deltaTime, 0.0f, kMaxFrameTime);
    if (frameTime <= 0.0f) {
        return;
    }
    const int substeps = std::max(
        1, static_cast<int>(std::ceil(frameTime / kMaxSubstep)));
    impl_->lastStepStats.substepCount = static_cast<uint32_t>(substeps);
    const float stepTime = frameTime / static_cast<float>(substeps);
    for (int step = 0; step < substeps; ++step) {
        if (impl_->waterEnabled) {
            const auto waterStart = Clock::now();
            impl_->waterTime = std::fmod(impl_->waterTime + stepTime, 4096.0f);
            auto& bodyInterface = impl_->system->GetBodyInterface();
            impl_->waterPositions.resize(impl_->waterBodyIDs.size());
            if (!impl_->waterBodyIDs.empty()) {
                // Snapshot every position, then release all shared locks before
                // ApplyBuoyancyImpulse takes an exclusive body lock below.
                const JPH::BodyLockMultiRead lock(
                    impl_->system->GetBodyLockInterface(),
                    impl_->waterBodyIDs.data(),
                    static_cast<int>(impl_->waterBodyIDs.size()));
                for (size_t index = 0; index < impl_->waterBodyIDs.size(); ++index) {
                    const JPH::Body* body = lock.GetBody(static_cast<int>(index));
                    impl_->waterPositions[index] = body != nullptr
                        ? body->GetPosition()
                        : JPH::RVec3::sZero();
                }
            }
            for (size_t index = 0; index < impl_->dynamicBodies.size(); ++index) {
                const Impl::DynamicSlot& slot = impl_->dynamicBodies[index];
                const JPH::RVec3 bodyPosition = impl_->waterPositions[index];
                // Outside the wave-sampling band the surface is the flat water
                // plane. Every throwable is less than 1.2 m from its centre,
                // so Jolt would calculate zero submerged volume here.
                if (bodyPosition.GetY() - impl_->waterHeight >=
                    kWaterWaveSampleBand) {
                    continue;
                }
                WaterSurfaceSample water;
                // Far above or below the interface, a local wave sample cannot
                // affect submerged volume. Avoid spectral work for those bodies.
                if (impl_->waterSurfaceSampler &&
                    std::abs(bodyPosition.GetY() - impl_->waterHeight) <
                        kWaterWaveSampleBand) {
                    water = impl_->waterSurfaceSampler(
                        glm::vec2(bodyPosition.GetX(), bodyPosition.GetZ()),
                        impl_->waterTime);
                }
                if (!std::isfinite(water.heightOffset) ||
                    !std::isfinite(water.slope.x) || !std::isfinite(water.slope.y) ||
                    !std::isfinite(water.velocity.x) ||
                    !std::isfinite(water.velocity.y) ||
                    !std::isfinite(water.velocity.z)) {
                    water = {};
                }
                const glm::vec3 normal = glm::normalize(
                    glm::vec3(-water.slope.x, 1.0f, -water.slope.y));
                const JPH::RVec3 surface(
                    bodyPosition.GetX(), impl_->waterHeight + water.heightOffset,
                    bodyPosition.GetZ());
                bodyInterface.ApplyBuoyancyImpulse(
                    slot.body, surface, JPH::Vec3(normal.x, normal.y, normal.z),
                    throwableBuoyancy(slot.shape), 0.55f, 0.08f,
                    JPH::Vec3(water.velocity.x, water.velocity.y, water.velocity.z),
                    impl_->system->GetGravity(), stepTime);
            }
            impl_->lastStepStats.waterMs +=
                std::chrono::duration<double, std::milli>(
                    Clock::now() - waterStart).count();
        }
        const auto simulationStart = Clock::now();
        impl_->system->Update(stepTime, 1, impl_->tempAllocator.get(),
                              impl_->jobSystem.get());
        impl_->lastStepStats.simulationMs +=
            std::chrono::duration<double, std::milli>(
                Clock::now() - simulationStart).count();
    }
    impl_->lastStepStats.totalMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - stepStart).count();
}

std::vector<JoltBackend::DynamicBodySnapshot> JoltBackend::dynamicBodies(
    size_t additionalCapacity) const {
    std::vector<DynamicBodySnapshot> result;
    if (impl_) impl_->lastDynamicBodyReadStats = {};
    if (!isInitialized()) {
        return result;
    }
    impl_->lastDynamicBodyReadStats.bodyCount = impl_->dynamicBodies.size();
    result.reserve(impl_->dynamicBodies.size() + additionalCapacity);
    if (impl_->dynamicBodies.empty()) {
        return result;
    }

    std::vector<JPH::BodyID> bodyIDs;
    bodyIDs.reserve(impl_->dynamicBodies.size());
    for (const Impl::DynamicSlot& slot : impl_->dynamicBodies) {
        bodyIDs.push_back(slot.body);
    }

    const uint32_t activeBodyCount =
        impl_->system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
    const bool allBodiesActive =
        activeBodyCount == impl_->dynamicBodies.size();
    if (allBodiesActive) {
        const JPH::BodyLockMultiRead lock(
            impl_->system->GetBodyLockInterface(), bodyIDs.data(),
            static_cast<int>(bodyIDs.size()));
        for (size_t index = 0; index < impl_->dynamicBodies.size(); ++index) {
            const Impl::DynamicSlot& slot = impl_->dynamicBodies[index];
            const JPH::Body* body = lock.GetBody(static_cast<int>(index));
            const JPH::RVec3 position = body != nullptr
                ? body->GetPosition()
                : JPH::RVec3::sZero();
            const JPH::Quat rotation = body != nullptr
                ? body->GetRotation()
                : JPH::Quat::sIdentity();
            result.push_back({
                slot.shape,
                toGlmPosition(position),
                glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(),
                          rotation.GetZ()),
                slot.dimensions,
                body != nullptr});
        }
        impl_->lastDynamicBodyReadStats.lockedBodyCount = bodyIDs.size();
        return result;
    }

    std::array<uint8_t, kMaxBodies> activeBodyIndices;
    activeBodyIndices.fill(0);
    if (activeBodyCount != 0) {
        JPH::BodyIDVector activeBodies;
        impl_->system->GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);
        for (const JPH::BodyID bodyID : activeBodies) {
            activeBodyIndices[bodyID.GetIndex()] = 1;
        }
    }

    result.resize(impl_->dynamicBodies.size());
    bodyIDs.clear();
    for (size_t index = 0; index < impl_->dynamicBodies.size(); ++index) {
        const Impl::DynamicSlot& slot = impl_->dynamicBodies[index];
        auto& snapshot = result[index];
        snapshot.shape = slot.shape;
        snapshot.dimensions = slot.dimensions;
        snapshot.active = activeBodyCount != 0
            && activeBodyIndices[slot.body.GetIndex()] != 0;
        const bool transformDirty =
            impl_->snapshotTransformDirty[slot.body.GetIndex()].load(
                std::memory_order_relaxed);
        if (snapshot.active || transformDirty) {
            if (!snapshot.active) {
                activeBodyIndices[slot.body.GetIndex()] = 2;
            }
            bodyIDs.push_back(slot.body);
        } else {
            snapshot.position = slot.cachedPosition;
            snapshot.rotation = slot.cachedRotation;
        }
    }

    if (!bodyIDs.empty()) {
        const JPH::BodyLockMultiRead lock(
            impl_->system->GetBodyLockInterface(), bodyIDs.data(),
            static_cast<int>(bodyIDs.size()));
        int readIndex = 0;
        for (size_t index = 0; index < impl_->dynamicBodies.size(); ++index) {
            const Impl::DynamicSlot& slot = impl_->dynamicBodies[index];
            auto& snapshot = result[index];
            if (activeBodyIndices[slot.body.GetIndex()] == 0) continue;

            const JPH::Body* body = lock.GetBody(readIndex++);
            const JPH::RVec3 position = body != nullptr
                ? body->GetPosition()
                : JPH::RVec3::sZero();
            const JPH::Quat rotation = body != nullptr
                ? body->GetRotation()
                : JPH::Quat::sIdentity();
            snapshot.position = toGlmPosition(position);
            snapshot.rotation = glm::quat(
                rotation.GetW(), rotation.GetX(), rotation.GetY(),
                rotation.GetZ());
            snapshot.active &= body != nullptr;
            if (!snapshot.active) {
                slot.cachedPosition = snapshot.position;
                slot.cachedRotation = snapshot.rotation;
                impl_->snapshotTransformDirty[slot.body.GetIndex()].store(
                    false, std::memory_order_relaxed);
            }
        }
    }
    impl_->lastDynamicBodyReadStats.lockedBodyCount = bodyIDs.size();
    impl_->lastDynamicBodyReadStats.cachedBodyCount =
        result.size() - bodyIDs.size();
    return result;
}

JoltBackend::DynamicBodyReadStats
JoltBackend::lastDynamicBodyReadStats() const noexcept {
    return impl_ ? impl_->lastDynamicBodyReadStats : DynamicBodyReadStats{};
}

PhysicsStats JoltBackend::stats() const noexcept {
    PhysicsStats result;
    result.backend = BackendType::JoltLegacy;
    result.arithmeticMode = PhysicsArithmeticMode::FastFloat;
    if (!isInitialized()) {
        return result;
    }

    result.residentBodies = static_cast<uint32_t>(impl_->dynamicBodies.size());
    result.activeBodies = impl_->system->GetNumActiveBodies(
        JPH::EBodyType::RigidBody);
    result.sleepingBodies = result.residentBodies -
        std::min(result.residentBodies, result.activeBodies);
    result.bodyCapacity = impl_->initContext.maxBodies;
    result.pairCapacity = impl_->initContext.maxPairs;
    result.contactCapacity = impl_->initContext.maxContacts;
    result.manifoldCapacity = impl_->initContext.maxManifolds;
    result.workerConcurrency = static_cast<uint32_t>(
        std::max(impl_->jobSystem->GetMaxConcurrency(), 1));
    result.estimatedPersistentBytes =
        static_cast<size_t>(impl_->initContext.maxBodies) *
        (sizeof(Impl::DynamicSlot) + sizeof(std::atomic_bool) +
         sizeof(JPH::BodyID) + sizeof(JPH::RVec3));
    result.scratchBytes = kTempAllocatorBytes;
    result.bodyCapacityOverflow =
        result.residentBodies > result.bodyCapacity;
    result.residentBodyUsage = {
        result.residentBodies, result.bodyCapacity, result.residentBodies,
        result.bodyCapacityOverflow};
    result.activeBodyUsage = {
        result.activeBodies, impl_->initContext.maxActiveBodies,
        result.activeBodies, false};
    result.commandUsage.capacity = impl_->initContext.gpu.commandCapacity;
    result.uniquePairUsage.capacity = result.pairCapacity;
    result.contactUsage.capacity = result.contactCapacity;
    result.manifoldUsage.capacity = result.manifoldCapacity;
    result.visibleBodyUsage.capacity = result.bodyCapacity;
    return result;
}

PhysicsStepStats JoltBackend::lastStepStats() const noexcept {
    return impl_ ? impl_->lastStepStats : PhysicsStepStats{};
}

} // namespace voxy::physics

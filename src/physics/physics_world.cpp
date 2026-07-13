#include "physics/physics_world.hpp"

#include "core/log.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
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
constexpr float kMaxFrameTime = 8.0f / 60.0f;
constexpr float kMaxSubstep = 1.0f / 60.0f;

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

class PhysicsWorld::Impl {
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

    ~Impl() { shutdown(); }

    bool initialize() {
        if (system) {
            return true;
        }

        retainJoltRuntime();
        runtimeRetained = true;

        tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
        jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(4096, 0, 8192, 4096, broadPhaseInterface,
                     objectVsBroadPhaseFilter, objectLayerPairFilter);
        return true;
    }

    void shutdown() {
        if (!system && !runtimeRetained) {
            return;
        }

        characters.clear();
        clearTerrain();
        system.reset();
        jobSystem.reset();
        tempAllocator.reset();

        if (runtimeRetained) {
            releaseJoltRuntime();
            runtimeRetained = false;
        }
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
                    static_cast<float>(terrain.samples[
                        static_cast<size_t>(sourceZ) * terrain.width + sourceX])
                    / 65535.0f;
                hasCollision = true;
            }
        }

        if (!hasCollision) {
            return false;
        }

        const float originX = -0.5f * static_cast<float>(terrain.width - 1)
                            * terrain.cellScale;
        const float originZ = -0.5f * static_cast<float>(terrain.height - 1)
                            * terrain.cellScale;
        const JPH::Vec3 offset{
            originX + static_cast<float>(startX) * terrain.cellScale,
            -terrain.heightScale,
            originZ + static_cast<float>(startZ) * terrain.cellScale};
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

        const float originX = -0.5f * static_cast<float>(terrain.width - 1)
                            * terrain.cellScale;
        const float originZ = -0.5f * static_cast<float>(terrain.height - 1)
                            * terrain.cellScale;
        const int32_t maxTileX = static_cast<int32_t>((terrain.width - 2)
                                                     / kTileCellCount);
        const int32_t maxTileZ = static_cast<int32_t>((terrain.height - 2)
                                                     / kTileCellCount);
        const int32_t centerX = std::clamp(
            static_cast<int32_t>(std::floor(
                (position.x - originX) / terrain.cellScale / kTileCellCount)),
            0, maxTileX);
        const int32_t centerZ = std::clamp(
            static_cast<int32_t>(std::floor(
                (position.z - originZ) / terrain.cellScale / kTileCellCount)),
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
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;
    std::unique_ptr<JPH::PhysicsSystem> system;
    Terrain terrain;
    std::vector<std::unique_ptr<CharacterSlot>> characters;
    bool runtimeRetained = false;
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

bool PhysicsWorld::initialize() {
    return impl_ && impl_->initialize();
}

void PhysicsWorld::shutdown() {
    if (impl_) {
        impl_->shutdown();
    }
}

bool PhysicsWorld::isInitialized() const noexcept {
    return impl_ && impl_->system != nullptr;
}

bool PhysicsWorld::setTerrain(std::span<const uint16_t> samples,
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

void PhysicsWorld::clearTerrain() {
    if (impl_) {
        impl_->clearTerrain();
    }
}

bool PhysicsWorld::hasTerrain() const noexcept {
    return impl_ && impl_->terrain.valid();
}

PhysicsWorld::CharacterHandle PhysicsWorld::createCharacter(
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

void PhysicsWorld::destroyCharacter(CharacterHandle handle) {
    if (!impl_ || handle == InvalidCharacter || handle > impl_->characters.size()) {
        return;
    }
    impl_->characters[handle - 1].reset();
}

bool PhysicsWorld::setCharacterPosition(CharacterHandle handle,
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

PhysicsWorld::CharacterMotion PhysicsWorld::moveCharacter(
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
        impl_->system->Update(stepTime, 1, impl_->tempAllocator.get(),
                              impl_->jobSystem.get());
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

} // namespace voxy::physics

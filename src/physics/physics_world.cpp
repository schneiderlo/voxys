#include "physics/physics_world.hpp"

#include "core/log.hpp"
#include "physics/physics_backend_factory.hpp"
#include "physics/physics_backend.hpp"

#include <utility>

namespace voxy::physics {

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

bool PhysicsWorld::initialize() {
    return initialize(PhysicsInitContext{});
}

bool PhysicsWorld::initialize(const PhysicsInitContext& context) {
    if (backend_ && backend_->isInitialized()) {
        if (backend_->type() == context.requestedBackend
            || (context.allowCpuFallback
                && context.requestedBackend == BackendType::WebGpuSoft
                && backend_->type() == BackendType::Box3DReference)) {
            return true;
        }
        LOG_ERROR("Cannot switch an initialized physics world from '{}' to '{}'",
                  backendTypeName(backend_->type()),
                  backendTypeName(context.requestedBackend));
        return false;
    }

    auto backend = detail::createPhysicsBackend(
        context.requestedBackend);
    if (backend && backend->initialize(context)) {
        backend_ = std::move(backend);
        return true;
    }

    if (!context.allowCpuFallback
        || context.requestedBackend != BackendType::WebGpuSoft) return false;

    PhysicsInitContext fallbackContext = context;
    fallbackContext.requestedBackend = BackendType::Box3DReference;
    fallbackContext.device = nullptr;
    fallbackContext.queue = nullptr;
    auto fallback = detail::createPhysicsBackend(
        fallbackContext.requestedBackend);
    if (!fallback || !fallback->initialize(fallbackContext)) {
        LOG_ERROR("WebGPU physics failed and Box3D CPU fallback could not initialize");
        return false;
    }
    LOG_WARN("WebGPU physics unavailable; using explicit Box3D CPU fallback");
    backend_ = std::move(fallback);
    return true;
}

void PhysicsWorld::shutdown() {
    if (backend_) {
        backend_->shutdown();
    }
}

bool PhysicsWorld::isInitialized() const noexcept {
    return backend_ && backend_->isInitialized();
}

BackendType PhysicsWorld::backendType() const noexcept {
    return backend_ ? backend_->type() : BackendType::JoltLegacy;
}

BackendCapabilities PhysicsWorld::capabilities() const noexcept {
    return backend_ ? backend_->capabilities() : BackendCapabilities{};
}

PhysicsStats PhysicsWorld::stats() const noexcept {
    return backend_ ? backend_->stats() : PhysicsStats{};
}

PhysicsStepStats PhysicsWorld::lastStepStats() const noexcept {
    return backend_ ? backend_->lastStepStats() : PhysicsStepStats{};
}

uint64_t PhysicsWorld::encodedTick() const noexcept {
    return backend_ ? backend_->encodedTick() : 0u;
}

bool PhysicsWorld::setTerrain(std::span<const uint16_t> samples,
                              uint32_t width, uint32_t height,
                              float heightScale, float cellScale) {
    return backend_ && backend_->setTerrain(
        samples, width, height, heightScale, cellScale);
}

void PhysicsWorld::setTerrainGpuResources(
    const TerrainGpuResources& resources) {
    if (backend_) backend_->setTerrainGpuResources(resources);
}

void PhysicsWorld::clearTerrain() {
    if (backend_) backend_->clearTerrain();
}

bool PhysicsWorld::hasTerrain() const noexcept {
    return backend_ && backend_->hasTerrain();
}

void PhysicsWorld::setWaterPlane(float height, bool enabled) {
    if (backend_) backend_->setWaterPlane(height, enabled);
}

void PhysicsWorld::setWaterSurfaceSampler(WaterSurfaceSampler sampler) {
    if (backend_) backend_->setWaterSurfaceSampler(std::move(sampler));
}

void PhysicsWorld::setWaterGpuResources(const WaterGpuResources& resources) {
    if (backend_) backend_->setWaterGpuResources(resources);
}

PhysicsWorld::CharacterHandle PhysicsWorld::createCharacter(
    const glm::vec3& feetPosition, const CharacterSettings& settings) {
    return backend_
        ? backend_->createCharacter(feetPosition, settings)
        : InvalidCharacter;
}

PhysicsWorld::CharacterHandle PhysicsWorld::createCharacter(
    const WorldPosition& feetPosition, const CharacterSettings& settings) {
    return backend_
        ? backend_->createCharacter(feetPosition, settings)
        : InvalidCharacter;
}

void PhysicsWorld::destroyCharacter(CharacterHandle handle) {
    if (backend_) backend_->destroyCharacter(handle);
}

bool PhysicsWorld::setCharacterPosition(CharacterHandle handle,
                                        const glm::vec3& feetPosition) {
    return backend_ && backend_->setCharacterPosition(handle, feetPosition);
}

bool PhysicsWorld::setCharacterPosition(
    CharacterHandle handle, const WorldPosition& feetPosition) {
    return backend_
        && backend_->setCharacterPosition(handle, feetPosition);
}

PhysicsWorld::CharacterMotion PhysicsWorld::moveCharacter(
    CharacterHandle handle, const glm::vec3& desiredHorizontalVelocity,
    bool jump, float jumpSpeed, float gravity, float terminalVelocity,
    float deltaTime) {
    return backend_
        ? backend_->moveCharacter(handle, desiredHorizontalVelocity, jump,
                                  jumpSpeed, gravity, terminalVelocity,
                                  deltaTime)
        : CharacterMotion{};
}

bool PhysicsWorld::throwBody(ThrowableShape shape,
                             const glm::vec3& position,
                             const glm::vec3& velocity) {
    return backend_ && backend_->throwBody(shape, position, velocity);
}

BodyHandle PhysicsWorld::spawnBody(const BodySpawnDesc& desc) {
    return backend_ ? backend_->spawnBody(desc) : BodyHandle{};
}

bool PhysicsWorld::destroyBody(BodyHandle handle) {
    return backend_ && backend_->destroyBody(handle);
}

void PhysicsWorld::enqueue(std::span<const PhysicsCommand> commands) {
    if (backend_) backend_->enqueue(commands);
}

AttachmentHandle PhysicsWorld::createDistanceAttachment(
    const DistanceAttachmentDesc& desc) {
    return backend_
        ? backend_->createDistanceAttachment(desc)
        : AttachmentHandle{};
}

bool PhysicsWorld::destroyAttachment(AttachmentHandle handle) {
    return backend_ && backend_->destroyAttachment(handle);
}

bool PhysicsWorld::setAttachmentTargetLength(
    AttachmentHandle handle, float targetLength) {
    return backend_
        && backend_->setAttachmentTargetLength(handle, targetLength);
}

bool PhysicsWorld::setAttachmentMotorSpeed(
    AttachmentHandle handle, float motorSpeed) {
    return backend_
        && backend_->setAttachmentMotorSpeed(handle, motorSpeed);
}

PreparedPhysicsMutation PhysicsWorld::prepareMutationBatch(
    const PhysicsMutationBatch& batch) noexcept {
    return backend_
        ? backend_->prepareMutationBatch(batch)
        : PreparedPhysicsMutation{};
}

PhysicsMutationResult PhysicsWorld::commitPrepared(
    const PreparedPhysicsMutation& prepared) noexcept {
    return backend_
        ? backend_->commitPrepared(prepared)
        : PhysicsMutationResult{};
}

bool PhysicsWorld::discardPrepared(
    const PreparedPhysicsMutation& prepared) noexcept {
    return backend_ && backend_->discardPrepared(prepared);
}

void PhysicsWorld::update(float deltaTime) {
    if (backend_) backend_->stepCpu(deltaTime);
}

bool PhysicsWorld::scheduleFixedTicks(uint32_t tickCount) {
    return backend_ && backend_->scheduleFixedTicks(tickCount);
}

void PhysicsWorld::encodeGpuStep(WGPUCommandEncoder encoder) {
    if (backend_) backend_->encodeGpuStep(encoder);
}

PhysicsEncodeReport PhysicsWorld::encodeGpuStepChecked(
    WGPUCommandEncoder encoder) {
    return backend_
        ? backend_->encodeGpuStepChecked(encoder)
        : PhysicsEncodeReport{};
}

bool PhysicsWorld::submitQueries(
    std::span<const PhysicsQueryRequest> requests, uint64_t resultTick) {
    return backend_ && backend_->submitQueries(requests, resultTick);
}

std::optional<PhysicsQueryBatch> PhysicsWorld::pollQueryResults() {
    return backend_ ? backend_->pollQueryResults() : std::nullopt;
}

bool PhysicsWorld::setEventReadbackEnabled(bool enabled) {
    return backend_ && backend_->setEventReadbackEnabled(enabled);
}

std::optional<PhysicsEventBatch> PhysicsWorld::pollEvents() {
    return backend_ ? backend_->pollEvents() : std::nullopt;
}

std::optional<PhysicsGpuStageTiming> PhysicsWorld::pollGpuStageTimings() {
    return backend_ ? backend_->pollGpuStageTimings() : std::nullopt;
}

PhysicsRenderView PhysicsWorld::renderView() const {
    return backend_ ? backend_->renderView() : PhysicsRenderView{};
}

void PhysicsWorld::requestDebugSnapshot(DebugSnapshotRequest request) {
    if (backend_) backend_->requestDebugSnapshot(request);
}

std::optional<DebugSnapshot> PhysicsWorld::pollDebugSnapshot() {
    return backend_ ? backend_->pollDebugSnapshot() : std::nullopt;
}

std::vector<PhysicsWorld::DynamicBodySnapshot> PhysicsWorld::dynamicBodies(
    size_t additionalCapacity) const {
    return backend_
        ? backend_->dynamicBodies(additionalCapacity)
        : std::vector<DynamicBodySnapshot>{};
}

PhysicsWorld::DynamicBodyReadStats
PhysicsWorld::lastDynamicBodyReadStats() const noexcept {
    return backend_
        ? backend_->lastDynamicBodyReadStats()
        : DynamicBodyReadStats{};
}

const char* PhysicsWorld::throwableShapeName(ThrowableShape shape) noexcept {
    return physics::throwableShapeName(shape);
}

glm::vec3 PhysicsWorld::throwableShapeDimensions(
    ThrowableShape shape) noexcept {
    return physics::throwableShapeDimensions(shape);
}

} // namespace voxy::physics

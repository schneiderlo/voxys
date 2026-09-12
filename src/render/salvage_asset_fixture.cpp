#include "render/salvage_asset_fixture.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <glm/gtx/quaternion.hpp>

namespace voxy::render {
namespace {
using Bundle = game::assets::CookedPartBundle;
using Lod = game::assets::AdmittedPartLod;

// Callback records contain CPU data only. Publishing status with release
// makes the bounded message visible to poll's acquire load, including when a
// spontaneous callback runs on another thread or before registration returns.
struct Completion {
    std::atomic<int> status{0}; // pending=0, success=1, failure=2
    std::array<char, 256> message{};
};
struct CallbackPayload { std::shared_ptr<Completion> completion; };

void complete(void* userdata, bool success, std::string_view message) noexcept {
    std::unique_ptr<CallbackPayload> payload(static_cast<CallbackPayload*>(userdata));
    auto& state = *payload->completion;
    const size_t count = std::min(message.size(), state.message.size() - 1u);
    if (count != 0u) std::memcpy(state.message.data(), message.data(), count);
    state.message[count] = '\0';
    state.status.store(success ? 1 : 2, std::memory_order_release);
    // No WebGPU calls, GPU owners or their destructors on this callback path.
}

#if defined(VOXY_WASM)
std::string_view viewOf(WGPUStringView message) noexcept {
    if (!message.data) return {};
    return {message.data, message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length};
}
void retainDevice(WGPUDevice device) { wgpuDeviceAddRef(device); }
void retainQueue(WGPUQueue queue) { wgpuQueueAddRef(queue); }
void retainView(WGPUTextureView view) { if (view) wgpuTextureViewAddRef(view); }
void popScope(WGPUDevice device, CallbackPayload* payload) noexcept {
    WGPUPopErrorScopeCallbackInfo info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = payload;
    info.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type,
                       WGPUStringView message, void* userdata, void*) {
        complete(userdata, status == WGPUPopErrorScopeStatus_Success
            && type == WGPUErrorType_NoError, viewOf(message));
    };
    static_cast<void>(wgpuDevicePopErrorScope(device, info));
}
void queueFence(WGPUQueue queue, CallbackPayload* payload) noexcept {
    WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = payload;
    info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView message,
                       void* userdata, void*) {
        complete(userdata, status == WGPUQueueWorkDoneStatus_Success, viewOf(message));
    };
    static_cast<void>(wgpuQueueOnSubmittedWorkDone(queue, info));
}
#else
void retainDevice(WGPUDevice device) { wgpuDeviceReference(device); }
void retainQueue(WGPUQueue queue) { wgpuQueueReference(queue); }
void retainView(WGPUTextureView view) { if (view) wgpuTextureViewReference(view); }
void popScope(WGPUDevice device, CallbackPayload* payload) noexcept {
    wgpuDevicePopErrorScope(device, [](WGPUErrorType type, const char* message, void* userdata) {
        complete(userdata, type == WGPUErrorType_NoError, message ? message : "");
    }, payload);
}
void queueFence(WGPUQueue queue, CallbackPayload* payload) noexcept {
    wgpuQueueOnSubmittedWorkDone(queue, [](WGPUQueueWorkDoneStatus status, void* userdata) {
        complete(userdata, status == WGPUQueueWorkDoneStatus_Success,
            status == WGPUQueueWorkDoneStatus_Success ? "" : "queue completion failed");
    }, payload);
}
#endif

#if defined(VOXY_WASM)
constexpr size_t scopeCount = 3;
#else
// Pinned wgpu-native v22.1.0.5 aborts on Internal despite declaring the enum
// in webgpu.h. Its implementation supports only Validation and OutOfMemory.
// Device loss belongs to the platform callback/queue failure path.
constexpr size_t scopeCount = 2;
#endif
using ScopeRecords = std::array<std::shared_ptr<Completion>, scopeCount>;
struct Scopes {
    WGPUDevice device;
    ScopeRecords records{};
    std::array<std::unique_ptr<CallbackPayload>, scopeCount> payloads{};
    bool open = false;
    explicit Scopes(WGPUDevice value) : device(value) {
        // Finish all allocations BEFORE pushing any scope.
        for (size_t i = 0; i < records.size(); ++i) {
            records[i] = std::make_shared<Completion>();
            payloads[i] = std::make_unique<CallbackPayload>(CallbackPayload{records[i]});
        }
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_OutOfMemory);
#if defined(VOXY_WASM)
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Internal);
#endif
        open = true;
    }
    void close() noexcept {
        if (!open) return;
        open = false;
        for (size_t i = records.size(); i > 0; --i) popScope(device, payloads[i-1u].release());
    }
    ~Scopes() { close(); }
};

int scopeStatus(const ScopeRecords& records, std::string& error) {
    bool pending = false;
    for (const auto& record : records) {
        if (!record) continue;
        const int status = record->status.load(std::memory_order_acquire);
        if (status == 2) {
            error = record->message[0] ? record->message.data() : "GPU error scope failed";
            return 2;
        }
        pending |= status == 0;
    }
    return pending ? 0 : 1;
}
uint32_t pendingScopes(const ScopeRecords& records) {
    uint32_t count = 0;
    for (const auto& record : records) if (record && record->status.load(std::memory_order_acquire) == 0) ++count;
    return count;
}

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool finite(const glm::mat4& value) {
    for (glm::length_t c = 0; c < 4; ++c) for (glm::length_t r = 0; r < 4; ++r) {
        if (!std::isfinite(value[c][r])) return false;
    }
    return true;
}
bool validFrame(const SalvageFixtureFrame& frame) {
    const auto& light = frame.lighting;
    const float length = glm::dot(light.direction, light.direction);
    return (frame.guides == InspectionGuides::Off || frame.guides == InspectionGuides::Dimensions
            || frame.guides == InspectionGuides::Sockets)
        && frame.width > 0 && frame.height > 0 && frame.width <= 8192 && frame.height <= 8192
        && finite(frame.view) && finite(frame.projection) && finite(frame.projection * frame.view)
        && finite(frame.cameraPosition) && finite(frame.shadowFrameWorldOrigin)
        && finite(light.direction) && std::isfinite(length)
        && length > std::numeric_limits<float>::min()
        && finite(light.sunColor) && std::isfinite(light.sunIntensity)
        && finite(light.ambientColor) && std::isfinite(light.ambientIntensity)
        && finite(light.fogColor) && std::isfinite(light.fogDensity) && std::isfinite(light.exposure);
}

struct Owner {
    uint64_t generation = 0;
    uint64_t assetBytes = 0;
    uint64_t reservedBytes = SalvageAssetFixture::fixedGpuReservationBytes;
    std::vector<std::shared_ptr<const Bundle>> bundles{};
    struct Mapping { uint32_t bundle = 0; uint64_t lod = 0; uint32_t upload = 0; const Lod* source = nullptr; };
    std::vector<Mapping> mappings{};
    std::vector<const Lod*> uploads{};
    struct Prototype {
        game::construction::PartDefinition part;
        moto::VmeshData mesh;
        game::assets::RigidPrefab prefab;
    };
    std::vector<Prototype> prototypes{};
    MeshPath path{};
    MeshPath guidePath{}; // Same owner/fences; separate buffers prevent queued-write aliasing.
    ScopeRecords scopes{};
    std::shared_ptr<Completion> fence = std::make_shared<Completion>();
    bool fencePending = false;
    bool uploadComplete = false;
    bool rejected = false;
    bool encoded = false;
    uint64_t submitted = 0;
    uint64_t completed = 0;
    // Discard flushes queued writes without counting a submitted scene frame.
    uint64_t work = 0;
    uint64_t completedWork = 0;
    uint64_t fenceWork = 0;
    uint64_t fenceSubmitted = 0;

    bool retired() const { return uploadComplete && !fencePending && !encoded && completedWork >= work; }
    ~Owner() {
        if (retired()) {path.shutdown();guidePath.shutdown();}
        else {path.releaseHandles();guidePath.releaseHandles();}
    }
    SalvageFixtureOwnerStats stats() const {
        return {.generation = generation, .assetGpuBytes = assetBytes, .reservedGpuBytes = reservedBytes,
            .lastSubmittedSerial = submitted, .lastCompletedSerial = completed,
            .uniqueUploads = static_cast<uint32_t>(uploads.size()),
            .prototypeUploads = static_cast<uint32_t>(prototypes.size()),
            .pendingCallbacks = pendingScopes(scopes) + (fencePending ? 1u : 0u),
            .environmentGpuBytes = path.environmentLightingBytes(),
            .environmentBakeCount = path.environmentBakeCount(),
            .environmentReady = path.environmentLightingReady()};
    }
};
} // namespace

struct SalvageAssetFixture::Impl {
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureView environment = nullptr;
    WGPUTextureView rayDepth = nullptr;
    SalvageFixtureConfig config{};
    std::unique_ptr<Owner> active{}, candidate{}, retiring{};
    ScopeRecords viewScopes{};
    bool viewsPending = false;
    bool leaving = false;
    bool fatal = false;
    bool candidateRejected = false;
    std::string error{};
    uint64_t nextGeneration = 1;
    uint64_t nextSerial = 1;
    SalvageFixtureTicket unresolved{};
    uint32_t encodedDraws = 0;
    uint32_t submittedDraws = 0;
    uint32_t guideBoxes = 0;

    ~Impl() {
        candidate.reset(); retiring.reset(); active.reset();
        if (environment) wgpuTextureViewRelease(environment);
        if (rayDepth) wgpuTextureViewRelease(rayDepth);
        if (queue) wgpuQueueRelease(queue);
        if (device) wgpuDeviceRelease(device);
    }
    bool fail(std::string& output, std::string_view message) { output = message; return false; }
    bool boundary(std::string& output) {
        if (fatal) return fail(output, "fixture is in terminal GPU failure: " + error);
        if (unresolved.serial != 0) return fail(output, "encoded frame needs submission or discard acknowledgment");
        if (leaving) return fail(output, "fixture Leave is draining; initialize a fresh fixture after drain");
        if (viewsPending) return fail(output, "scene view validation is pending");
        return true;
    }
    void armFence(Owner& owner, std::unique_ptr<CallbackPayload> payload) {
        owner.fence->status.store(0, std::memory_order_relaxed);
        owner.fenceWork = owner.work;
        owner.fenceSubmitted = owner.submitted;
        owner.fencePending = true;
        queueFence(queue, payload.release());
    }
    void pollOwner(Owner& owner) {
        if (scopeStatus(owner.scopes, error) == 2) owner.rejected = true;
        if (owner.fencePending) {
            const int status = owner.fence->status.load(std::memory_order_acquire);
            if (status == 2) {
                fatal = true;
                error = owner.fence->message[0] ? owner.fence->message.data() : "GPU queue completion failed";
                return;
            }
            if (status == 1) {
                owner.fencePending = false;
                owner.uploadComplete = true;
                owner.completedWork = owner.fenceWork;
                owner.completed = owner.fenceSubmitted;
            }
        }
        if (!owner.fencePending && owner.completedWork < owner.work) {
            // An allocation failure here leaves the work horizon intact and
            // safely retryable; no completion or frame submission is invented.
            auto payload = std::make_unique<CallbackPayload>(CallbackPayload{owner.fence});
            armFence(owner, std::move(payload));
        }
    }
};

SalvageAssetFixture::SalvageAssetFixture() = default;
SalvageAssetFixture::~SalvageAssetFixture() = default;

bool SalvageAssetFixture::init(WGPUDevice device, WGPUQueue queue,
    const SalvageFixtureConfig& config, std::string& error) {
    if (impl_) { error = "fixture already initialized; drain/shutdown before re-entry"; return false; }
    const uint64_t fixed = fixedGpuReservationBytes
        + (config.filteredEnvironment ? MeshPath::filteredEnvironmentReservationBytes : 0u)
        + (config.sunShadows ? MeshPath::sunShadowReservationBytes : 0u);
    if (!device || !queue || config.maximumOwnerGpuBytes <= fixed
        || config.maximumOwnerGpuBytes > 16ull * 1024ull * 1024ull
        || config.maximumResidentGpuBytes < config.maximumOwnerGpuBytes
        || config.maximumResidentGpuBytes > 48ull * 1024ull * 1024ull) {
        error = "invalid fixture handles or hard resource ceilings";
        return false;
    }
    auto pending = std::make_unique<Impl>();
    pending->config = config;
    pending->nextGeneration = nextGeneration_;
    pending->nextSerial = nextSerial_;
    retainDevice(device); retainQueue(queue);
    pending->device = device; pending->queue = queue;
    impl_ = std::move(pending);
    error.clear();
    return true;
}

bool SalvageAssetFixture::beginCandidate(
    std::span<const std::shared_ptr<const Bundle>> bundles, std::string& error,
    std::span<const game::construction::PartDefinition> prototypes) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (!state.boundary(error)) return false;
    if (state.candidate) return state.fail(error, "candidate residency is already occupied");
    if (bundles.empty() || bundles.size() > maximumBundles)
        return state.fail(error, "fixture requires one to twelve admitted bundles");
    if (state.nextGeneration == std::numeric_limits<uint64_t>::max())
        return state.fail(error, "fixture generation counter exhausted");
    auto owner = std::make_unique<Owner>();
    if (state.config.filteredEnvironment) owner->reservedBytes += MeshPath::filteredEnvironmentReservationBytes;
    if (state.config.sunShadows) owner->reservedBytes += MeshPath::sunShadowReservationBytes;
    if (prototypes.size() > maximumPrototypes) return state.fail(error,"prototype definition ceiling exceeded");
    owner->prototypes.reserve(prototypes.size());
    for (const auto& definition : prototypes) {
        if (std::any_of(owner->prototypes.begin(),owner->prototypes.end(),[&](const auto& p){return p.part.key==definition.key;}))
            return state.fail(error,"duplicate prototype definition key");
        Owner::Prototype prototype; prototype.part=definition;
        if (!makePrototypeFixtureMesh(definition,prototype.mesh,prototype.prefab,error)) return false;
        if (prototype.prefab.counts.gpuBytes > state.config.maximumOwnerGpuBytes-owner->reservedBytes)
            return state.fail(error,"per-owner prototype GPU reservation exceeded");
        owner->assetBytes += prototype.prefab.counts.gpuBytes;
        owner->reservedBytes += prototype.prefab.counts.gpuBytes;
        owner->prototypes.push_back(std::move(prototype));
    }
    owner->bundles.assign(bundles.begin(), bundles.end());
    for (size_t bundleIndex = 0; bundleIndex < bundles.size(); ++bundleIndex) {
        if (!bundles[bundleIndex]) return state.fail(error, "null admitted bundle");
        for (const auto& lod : bundles[bundleIndex]->lods()) {
            size_t upload = 0;
            for (; upload < owner->uploads.size(); ++upload) {
                if (owner->uploads[upload]->asset == lod.asset) {
                    if (owner->uploads[upload]->vmeshSha256 != lod.vmeshSha256)
                        return state.fail(error, "one visual content key resolves to conflicting VMESH bytes");
                    break;
                }
            }
            if (upload == owner->uploads.size()) {
                if (upload == maximumUniqueAssets) return state.fail(error, "unique asset ceiling exceeded");
                if (lod.prefab.counts.gpuBytes > state.config.maximumOwnerGpuBytes - owner->reservedBytes)
                    return state.fail(error, "per-owner GPU reservation exceeded");
                owner->uploads.push_back(&lod);
                owner->assetBytes += lod.prefab.counts.gpuBytes;
                owner->reservedBytes += lod.prefab.counts.gpuBytes;
            }
            owner->mappings.push_back({static_cast<uint32_t>(bundleIndex), lod.id,
                static_cast<uint32_t>(upload), &lod});
        }
    }
    if (owner->uploads.empty()) return state.fail(error, "bundle has no admitted LOD assets");
    const uint64_t resident = (state.active ? state.active->reservedBytes : 0)
        + (state.retiring ? state.retiring->reservedBytes : 0);
    if (owner->reservedBytes > state.config.maximumResidentGpuBytes - resident)
        return state.fail(error, "combined active/candidate/retiring GPU reservation exceeded");
    // The shared helper is owned and fenced with this candidate. It is already
    // covered by fixedGpuReservationBytes; never masquerades as authored LODs.
    const auto guideMesh = inspectionGuideMesh();
    game::assets::RigidPrefab guidePrefab;
    if (!game::assets::prepareRigidPrefab(guideMesh, {}, {}, guidePrefab, error)) return false;
    if (guidePrefab.counts.gpuBytes != inspectionGuideGpuBytes)
        return state.fail(error, "guide mesh exceeds its fixed GPU reservation");
    owner->generation = state.nextGeneration++;
    auto payload = std::make_unique<CallbackPayload>(CallbackPayload{owner->fence});
    Scopes scopes(state.device);
    owner->scopes = scopes.records;
    MeshPathConfig config;
    config.shaderPath = state.config.shaderPath;
    config.colorFormat = state.config.colorFormat;
    config.depthFormat = state.config.depthFormat;
    config.frontFace = WGPUFrontFace_CW;
    config.maxInstances = maximumExpandedDraws;
    config.maxDrawsPerFrame = maximumExpandedDraws;
    config.linearHdrOutput = state.config.linearHdrOutput;
    config.filteredEnvironment = state.config.filteredEnvironment;
    config.sunShadows = state.config.sunShadows;
    bool valid = owner->path.init(state.device, state.queue, config);
    if (valid) valid = owner->path.setSceneTextures(state.environment, state.rayDepth);
    if (valid) for (const auto* lod : owner->uploads) {
        if (!owner->path.loadMeshData(lod->mesh)) { valid = false; break; }
    }
    if (valid) for (const auto& prototype : owner->prototypes) {
        if (!owner->path.loadMeshData(prototype.mesh)) {valid=false;break;}
    }
    // Opaque cable shares the small helper cube, with ordinary model depth.
    if(valid) valid=owner->path.loadMeshData(guideMesh);
    config.depthOverlay = true;
    config.filteredEnvironment = false;
    config.sunShadows = false;
    if (valid) valid = owner->guidePath.init(state.device,state.queue,config);
    if (valid) valid = owner->guidePath.loadMeshData(guideMesh);
    scopes.close();
    // Flush upload writes even if initialization failed or no frame is drawn.
    wgpuQueueSubmit(state.queue, 0, nullptr);
    state.armFence(*owner, std::move(payload));
    owner->rejected = !valid;
    state.candidate = std::move(owner);
    state.candidateRejected = !valid;
    if (!valid) {
        state.error = "candidate GPU initialization/upload failed";
        return state.fail(error, state.error);
    }
    state.error.clear(); error.clear();
    return true;
}

SalvageFixtureStatus SalvageAssetFixture::poll() {
    if (!impl_) return SalvageFixtureStatus::Uninitialized;
    auto& state = *impl_;
    if (state.fatal) return SalvageFixtureStatus::Fatal;
    if (state.viewsPending) {
        const int status = scopeStatus(state.viewScopes, state.error);
        if (status == 2) state.fatal = true;
        if (status == 1) { state.viewsPending = false; state.viewScopes = {}; }
    }
    for (auto* owner : {state.active.get(), state.candidate.get(), state.retiring.get()}) {
        if (owner) state.pollOwner(*owner);
    }
    if (state.fatal) return SalvageFixtureStatus::Fatal;
    if (state.retiring && state.retiring->retired()) state.retiring.reset();
    if (state.candidate && state.candidate->rejected) {
        state.candidateRejected = true;
        if (state.candidate->retired() && pendingScopes(state.candidate->scopes) == 0) state.candidate.reset();
    }
    if (state.leaving) {
        if (state.candidate && state.candidate->retired() && pendingScopes(state.candidate->scopes) == 0) state.candidate.reset();
        if (state.active && state.active->retired()) state.active.reset();
        return !state.active && !state.candidate && !state.retiring && !state.viewsPending
            ? SalvageFixtureStatus::Drained : SalvageFixtureStatus::Leaving;
    }
    if (state.viewsPending) return SalvageFixtureStatus::ViewsValidating;
    if (state.candidate) {
        if (state.candidate->rejected) return SalvageFixtureStatus::CandidateRejected;
        if (state.candidate->uploadComplete && pendingScopes(state.candidate->scopes) == 0)
            return SalvageFixtureStatus::CandidateReady;
        return SalvageFixtureStatus::CandidateValidating;
    }
    if (state.candidateRejected) return SalvageFixtureStatus::CandidateRejected;
    return state.active ? SalvageFixtureStatus::Active : SalvageFixtureStatus::Idle;
}

bool SalvageAssetFixture::publishCandidate(std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    const auto status = poll();
    if (!state.boundary(error)) return false;
    if (status != SalvageFixtureStatus::CandidateReady)
        return state.fail(error, "candidate has not completed upload and validation");
    if (state.retiring) return state.fail(error, "previous generation is still retiring");
    if (state.active && !state.active->retired()) state.retiring = std::move(state.active);
    else state.active.reset();
    state.active = std::move(state.candidate);
    state.encodedDraws = 0; state.submittedDraws = 0;
    state.guideBoxes = 0;
    error.clear();
    return true;
}

bool SalvageAssetFixture::setSceneViews(WGPUTextureView environment,
    WGPUTextureView rayDepth, std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (!state.boundary(error)) return false;
    if (state.environment == environment && state.rayDepth == rayDepth) { error.clear(); return true; }
    Scopes scopes(state.device);
    state.viewScopes = scopes.records;
    state.viewsPending = true;
    retainView(environment); retainView(rayDepth);
    // Bind groups retain their old dependencies if submitted work still uses
    // them. The retiring generation is never rebound or drawn again.
    const auto oldEnvironment = std::exchange(state.environment, environment);
    const auto oldDepth = std::exchange(state.rayDepth, rayDepth);
    if (oldEnvironment) wgpuTextureViewRelease(oldEnvironment);
    if (oldDepth) wgpuTextureViewRelease(oldDepth);
    try {
        for (auto* owner : {state.active.get(), state.candidate.get()}) {
            if (owner && !owner->rejected && !owner->path.setSceneTextures(environment, rayDepth)) {
                state.fatal = true;
                state.error = "scene view binding failed";
            }
        }
    } catch (...) {
        state.fatal = true;
        throw; // Scopes are still balanced; caller performs exceptional shutdown.
    }
    if (state.fatal) return state.fail(error, state.error);
    error.clear();
    return true;
}

bool SalvageAssetFixture::encode(WGPUCommandEncoder encoder, WGPUTextureView color,
    WGPUTextureView depth, std::span<const SalvageFixturePlacement> placements,
    const SalvageFixtureFrame& frame, SalvageFixtureTicket& output, std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (!state.boundary(error)) return false;
    if (!state.active) return state.fail(error, "no active fixture generation");
    if (!encoder || !color || !depth || !validFrame(frame)
        || state.config.linearHdrOutput != (frame.linearDepthOutput != nullptr)
        || (state.config.linearHdrOutput && frame.guides != InspectionGuides::Off)
        || (frame.useRayDepth && !state.rayDepth)) return state.fail(error, "invalid fixture frame or target views");
    if (placements.size() > maximumPlacements) return state.fail(error, "fixture placement ceiling exceeded");
    if (state.nextSerial == std::numeric_limits<uint64_t>::max()) return state.fail(error, "frame serial counter exhausted");
    auto& owner = *state.active;
    std::vector<MeshDrawInstance> instances;
    std::vector<MeshDrawInstance> guideInstances;
    instances.reserve(maximumExpandedDraws);
    guideInstances.reserve(maximumExpandedDraws);
    uint32_t expandedDraws = 0;
    uint32_t authoredInstances = 0;
    uint32_t guideBoxes = 0;
    std::vector<game::assets::RigidPrefabDraw> placed;
    std::vector<InspectionGuideBox> guides;
    for (const auto& placement : placements) {
        for(int channel=0;channel<4;++channel)if(!std::isfinite(placement.tint[channel])
            || placement.tint[channel]<0 || placement.tint[channel]>1)return state.fail(error,"invalid placement tint");
        const game::assets::RigidPrefab* selectedPrefab=nullptr;
        const game::construction::PartDefinition* definition=nullptr;
        uint32_t upload=0;
        if (placement.prototype) {
            if (placement.bundleIndex >= owner.prototypes.size() || placement.lodId!=0)
                return state.fail(error,"placement references a missing prototype or nonzero prototype LOD");
            const auto& prototype=owner.prototypes[placement.bundleIndex];
            selectedPrefab=&prototype.prefab;definition=&prototype.part;
            upload=static_cast<uint32_t>(owner.uploads.size())+placement.bundleIndex;
        } else {
            const auto found = std::find_if(owner.mappings.begin(), owner.mappings.end(), [&](const auto& mapping) {
                return mapping.bundle == placement.bundleIndex && mapping.lod == placement.lodId;
            });
            if (found == owner.mappings.end()) return state.fail(error, "placement references a missing bundle or stable LOD ID");
            selectedPrefab=&found->source->prefab;definition=&owner.bundles[placement.bundleIndex]->sidecar().part;
            upload=found->upload;
        }
        const auto& prefab = *selectedPrefab;
        if (prefab.counts.expandedDraws > maximumExpandedDraws - expandedDraws)
            return state.fail(error, "expanded draw ceiling exceeded");
        if (prefab.counts.meshInstances > maximumMeshInstances - authoredInstances)
            return state.fail(error, "mesh instance ceiling exceeded");
        if (!game::assets::placeRigidPrefab(prefab, placement.cameraRelativeRoot, placement.placement,
            maximumMeshInstances - authoredInstances, placed, error)) return false;
        expandedDraws += prefab.counts.expandedDraws;
        authoredInstances += prefab.counts.meshInstances;
        for (const auto& draw : placed) instances.push_back({.assetIndex = upload,
            .meshIndex = draw.meshIndex, .modelMatrix = draw.modelMatrix, .tintColor = placement.tint,
            .physicsBody = placement.physicsBody, .castsSunShadow = placement.castsSunShadow});
        // One ruler stand avoids duplicating labels/scales. Socket inspection
        // covers every placement; reject the whole frame if its budget cannot
        // represent every socket instead of silently dropping late guides.
        if (frame.guides == InspectionGuides::Sockets
            || (frame.guides == InspectionGuides::Dimensions && &placement == placements.data())) {
            if (!makeInspectionGuides(*definition,
                prefab.canonicalBounds, placement.cameraRelativeRoot, placement.placement,
                frame.guides, maximumExpandedDraws - guideInstances.size(),
                guides, error, placement.selectedSockets)) return false;
            for (const auto& guide : guides) guideInstances.push_back({.assetIndex = 0,
                .meshIndex = 0, .modelMatrix = guide.model, .tintColor = guide.color});
            const auto count = static_cast<uint32_t>(guides.size());
            guideBoxes += count;
        }
    }
    // Establish ownership BEFORE render can enqueue writes. If allocation
    // throws below, output still identifies the encoder the caller must discard.
    if(frame.harborStructure.size()>10||frame.harborCables.size()>4)
        return state.fail(error,"harbor presentation capacity exceeded");
    const auto helperAsset=static_cast<uint32_t>(owner.uploads.size()+owner.prototypes.size());
    for(const auto& solid:frame.harborStructure){
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)
            if(!std::isfinite(solid.model[column][row]))return state.fail(error,"invalid harbor transform");
        for(int channel=0;channel<4;++channel)if(!std::isfinite(solid.color[channel])||solid.color[channel]<0||solid.color[channel]>1)
            return state.fail(error,"invalid harbor color");
        if(solid.color.a!=1||expandedDraws>=maximumExpandedDraws||instances.size()>=maximumExpandedDraws)
            return state.fail(error,"harbor opaque draw capacity exceeded");
        ++expandedDraws;
        instances.push_back({.assetIndex=helperAsset,.meshIndex=0,.modelMatrix=solid.model,.tintColor=solid.color});
    }
    const auto addCable=[&](const std::array<glm::vec3,2>& endpoints){
        const auto a=endpoints[0],b=endpoints[1];const auto delta=b-a;const float length=glm::length(delta);
        if(!std::isfinite(length)||length>64||!std::isfinite(glm::length(a))
            ||instances.size()>=maximumExpandedDraws||expandedDraws>=maximumExpandedDraws)
            return state.fail(error,"invalid cable endpoints or draw capacity");
        if(length>.001f){
            const auto model=glm::translate(glm::mat4(1),(a+b)*.5f)
                *glm::mat4_cast(glm::rotation(glm::vec3(0,1,0),delta/length))
                *glm::scale(glm::mat4(1),glm::vec3(.028f,length,.028f));
            instances.push_back({.assetIndex=helperAsset,.meshIndex=0,.modelMatrix=model,.tintColor={.32f,.17f,.045f,1}});
            ++expandedDraws;
        }
        return true;
    };
    if(frame.towCable&&!addCable(*frame.towCable))return false;
    for(const auto& cable:frame.harborCables)if(!addCable(cable))return false;
    state.unresolved = {owner.generation, state.nextSerial++};
    output = state.unresolved;
    owner.encoded = true;
    state.encodedDraws = 0;
    if (!owner.path.setAuthoredBodyView(frame.physics,frame.worldCamera))
        return state.fail(error,"authored body render binding failed");
    if (!owner.path.encodeEnvironmentLighting(encoder))
        return state.fail(error, "environment filter encoding failed; discard the open frame ticket");
    owner.path.clearInstances();
    for (const auto& instance : instances) owner.path.addInstance(instance);
    if (!owner.path.render(encoder, color, depth, frame.view, frame.projection,
        frame.cameraPosition, frame.lighting, frame.width, frame.height, frame.useRayDepth, frame.linearDepthOutput,
        frame.beforeColor, frame.shadowFrameWorldOrigin))
        return state.fail(error, "model drawing failed; discard the open frame ticket");
    owner.guidePath.clearInstances();
    for (const auto& instance : guideInstances) owner.guidePath.addInstance(instance);
    // Draw after all opaque/blended models, with separate instance/uniform
    // storage. Reusing path.render here would overwrite pending model draws.
    if (!owner.guidePath.render(encoder,color,depth,frame.view,frame.projection,
        frame.cameraPosition,frame.lighting,frame.width,frame.height,false,frame.linearDepthOutput))
        return state.fail(error, "guide drawing failed; discard the open frame ticket");
    state.encodedDraws = owner.path.lastSubmittedDrawCount()+owner.guidePath.lastSubmittedDrawCount();
    state.guideBoxes = guideBoxes;
    error.clear();
    return true;
}

bool SalvageAssetFixture::submitted(SalvageFixtureTicket ticket, std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (ticket.serial == 0 || ticket != state.unresolved || !state.active)
        return state.fail(error, "wrong or already acknowledged frame ticket");
    auto& owner = *state.active;
    owner.submitted = ticket.serial;
    owner.work = ticket.serial;
    owner.encoded = false;
    owner.path.acknowledgeEnvironmentSubmission();
    state.unresolved = {};
    state.submittedDraws = state.encodedDraws;
    error.clear();
    return true; // poll registers a bounded fence; acknowledgment cannot allocate.
}

bool SalvageAssetFixture::discarded(SalvageFixtureTicket ticket, std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (ticket.serial == 0 || ticket != state.unresolved || !state.active)
        return state.fail(error, "wrong or already acknowledged frame ticket");
    // MeshPath's queue writes exist independently of the abandoned encoder.
    // Flush and retire those writes without claiming submission of its draws.
    wgpuQueueSubmit(state.queue, 0, nullptr);
    state.active->work = ticket.serial;
    state.active->encoded = false;
    state.active->path.discardEnvironmentEncoding();
    state.active->path.clearInstances();
    state.active->guidePath.clearInstances();
    state.unresolved = {};
    state.encodedDraws = 0;
    state.guideBoxes = 0;
    error.clear();
    return true;
}

bool SalvageAssetFixture::resetInstances(std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    if (!impl_->boundary(error)) return false;
    if (impl_->active) {impl_->active->path.clearInstances();impl_->active->guidePath.clearInstances();}
    impl_->encodedDraws = 0;
    impl_->guideBoxes = 0;
    error.clear();
    return true;
}

bool SalvageAssetFixture::requestLeave(std::string& error) {
    if (!impl_) { error = "fixture is not initialized"; return false; }
    auto& state = *impl_;
    if (state.fatal) return state.fail(error, "terminal GPU failure requires shutdown");
    if (state.unresolved.serial != 0) return state.fail(error, "encoded frame needs acknowledgment before Leave");
    state.leaving = true;
    if (state.active) {state.active->path.clearInstances();state.active->guidePath.clearInstances();}
    state.encodedDraws = 0;
    state.guideBoxes = 0;
    error.clear();
    return true;
}

void SalvageAssetFixture::notifyDeviceLost() noexcept {
    if (impl_) impl_->fatal = true;
}
void SalvageAssetFixture::shutdown() {
    if (impl_) {
        nextGeneration_ = impl_->nextGeneration;
        nextSerial_ = impl_->nextSerial;
    }
    impl_.reset();
}
SalvageFixtureStats SalvageAssetFixture::stats() const {
    SalvageFixtureStats result;
    if (!impl_) return result;
    if (impl_->active) result.active = impl_->active->stats();
    if (impl_->candidate) result.candidate = impl_->candidate->stats();
    if (impl_->retiring) result.retiring = impl_->retiring->stats();
    result.unresolved = impl_->unresolved;
    result.lastEncodedDraws = impl_->encodedDraws;
    result.lastEncodedGuideBoxes = impl_->guideBoxes;
    result.lastSubmittedDraws = impl_->submittedDraws;
    result.pendingViewCallbacks = pendingScopes(impl_->viewScopes);
    return result;
}
std::string_view SalvageAssetFixture::lastError() const {
    if (!impl_) return {};
    if (impl_->fatal && impl_->error.empty()) return "platform reported device loss or terminal GPU failure";
    return impl_->error;
}

} // namespace voxy::render

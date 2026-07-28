// ═══════════════════════════════════════════════════════════════════════════════
// gpu_timer.cpp - GPU Timing Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/gpu_timer.hpp"
#include "core/log.hpp"

#include <cstring>
#include <utility>

namespace voxy::perf {
namespace {

enum MapState : uint32_t {
    Mapping,
    Ready,
    Failed,
};

struct MapPayload {
    std::shared_ptr<std::atomic<uint32_t>> state;
};

#if !defined(VOXY_WASM)
void mapCallback(WGPUBufferMapAsyncStatus status, void* userdata) {
    std::unique_ptr<MapPayload> payload(
        static_cast<MapPayload*>(userdata));
    payload->state->store(
        status == WGPUBufferMapAsyncStatus_Success ? Ready : Failed,
        std::memory_order_release);
}
#endif

void destroyBuffer(WGPUBuffer& buffer) {
    if (!buffer) return;
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
    buffer = nullptr;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// GPUTimer Implementation
// ─────────────────────────────────────────────────────────────────────────────

GPUTimer::~GPUTimer() {
    shutdown();
}

GPUTimer::GPUTimer(GPUTimer&& other) noexcept
    : device_(other.device_)
    , querySet_(other.querySet_)
    , resolveBuffer_(other.resolveBuffer_)
    , readbackBuffer_(other.readbackBuffer_)
    , labels_(std::move(other.labels_))
    , resolvedLabels_(std::move(other.resolvedLabels_))
    , queryIndex_(other.queryIndex_)
    , resolvedQueryCount_(other.resolvedQueryCount_)
    , timestampPeriod_(other.timestampPeriod_)
    , supported_(other.supported_)
    , pendingReadback_(other.pendingReadback_)
    , mappingStarted_(other.mappingStarted_)
    , mappingState_(std::move(other.mappingState_))
    , lastResults_(std::move(other.lastResults_))
{
    other.device_ = nullptr;
    other.querySet_ = nullptr;
    other.resolveBuffer_ = nullptr;
    other.readbackBuffer_ = nullptr;
    other.supported_ = false;
    other.pendingReadback_ = false;
    other.mappingStarted_ = false;
    other.queryIndex_ = 0;
    other.resolvedQueryCount_ = 0;
}

GPUTimer& GPUTimer::operator=(GPUTimer&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        querySet_ = other.querySet_;
        resolveBuffer_ = other.resolveBuffer_;
        readbackBuffer_ = other.readbackBuffer_;
        labels_ = std::move(other.labels_);
        resolvedLabels_ = std::move(other.resolvedLabels_);
        queryIndex_ = other.queryIndex_;
        resolvedQueryCount_ = other.resolvedQueryCount_;
        timestampPeriod_ = other.timestampPeriod_;
        supported_ = other.supported_;
        pendingReadback_ = other.pendingReadback_;
        mappingStarted_ = other.mappingStarted_;
        mappingState_ = std::move(other.mappingState_);
        lastResults_ = std::move(other.lastResults_);
        
        other.device_ = nullptr;
        other.querySet_ = nullptr;
        other.resolveBuffer_ = nullptr;
        other.readbackBuffer_ = nullptr;
        other.supported_ = false;
        other.pendingReadback_ = false;
        other.mappingStarted_ = false;
        other.queryIndex_ = 0;
        other.resolvedQueryCount_ = 0;
    }
    return *this;
}

bool GPUTimer::init(WGPUDevice device) {
    if (!device) {
        LOG_ERROR("GPUTimer::init: device is null");
        return false;
    }
    
#if defined(VOXY_WASM)
    // Timestamp queries are generally not supported in WebGPU WASM yet
    LOG_INFO("GPUTimer: Timestamp queries not supported in WASM build");
    return false;
#else
    if (!wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery)) {
        LOG_INFO("GPUTimer: Timestamp queries not enabled on this device");
        return false;
    }

    GPUTimer replacement;
    replacement.device_ = device;
    WGPUQuerySetDescriptor querySetDesc{};
    querySetDesc.label = "gpu_timer_query_set";
    querySetDesc.type = WGPUQueryType_Timestamp;
    querySetDesc.count = MAX_QUERIES;
    replacement.querySet_ =
        wgpuDeviceCreateQuerySet(device, &querySetDesc);
    if (!replacement.querySet_) {
        LOG_INFO("GPUTimer: Timestamp queries not supported on this device");
        return false;
    }
    
    // Create resolve buffer (for timestamp values)
    uint64_t resolveBufferSize = MAX_QUERIES * sizeof(uint64_t);
    WGPUBufferDescriptor resolveDesc{};
    resolveDesc.label = "gpu_timer_resolve_buffer";
    resolveDesc.size = resolveBufferSize;
    resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    
    replacement.resolveBuffer_ = wgpuDeviceCreateBuffer(device, &resolveDesc);
    if (!replacement.resolveBuffer_) {
        LOG_ERROR("GPUTimer: Failed to create resolve buffer");
        return false;
    }
    
    // Create readback buffer (for CPU access)
    WGPUBufferDescriptor readbackDesc{};
    readbackDesc.label = "gpu_timer_readback_buffer";
    readbackDesc.size = resolveBufferSize;
    readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    
    replacement.readbackBuffer_ =
        wgpuDeviceCreateBuffer(device, &readbackDesc);
    if (!replacement.readbackBuffer_) {
        LOG_ERROR("GPUTimer: Failed to create readback buffer");
        return false;
    }
    
    // Get timestamp period (nanoseconds per tick)
    // Note: This API may vary between implementations
    replacement.timestampPeriod_ = 1;
    replacement.labels_.reserve(MAX_QUERIES);
    replacement.resolvedLabels_.reserve(MAX_QUERIES);
    replacement.supported_ = true;
    *this = std::move(replacement);
    
    LOG_INFO("GPUTimer initialized with {} query slots", MAX_QUERIES);
    return true;
#endif
}

void GPUTimer::shutdown() {
    if (mappingStarted_ && readbackBuffer_
        && (!mappingState_
            || mappingState_->load(std::memory_order_acquire) != Failed))
        wgpuBufferUnmap(readbackBuffer_);
    if (readbackBuffer_) {
        destroyBuffer(readbackBuffer_);
    }
    if (resolveBuffer_) {
        destroyBuffer(resolveBuffer_);
    }
    if (querySet_) {
        wgpuQuerySetRelease(querySet_);
        querySet_ = nullptr;
    }
    
    device_ = nullptr;
    supported_ = false;
    labels_.clear();
    resolvedLabels_.clear();
    queryIndex_ = 0;
    resolvedQueryCount_ = 0;
    pendingReadback_ = false;
    mappingStarted_ = false;
    mappingState_.reset();
    lastResults_ = {};
}

bool GPUTimer::beginFrame() {
    if (!supported_ || pendingReadback_) return false;
    queryIndex_ = 0;
    labels_.clear();
    return true;
}

bool GPUTimer::writeTimestamp(
    WGPUCommandEncoder encoder, const char* label) {
    if (!supported_ || pendingReadback_ || !encoder || !label
        || queryIndex_ >= MAX_QUERIES) return false;
    labels_.emplace_back(label);
    wgpuCommandEncoderWriteTimestamp(encoder, querySet_, queryIndex_);
    queryIndex_++;
    return true;
}

bool GPUTimer::resolve(WGPUCommandEncoder encoder) {
    if (!supported_ || pendingReadback_ || !encoder || queryIndex_ == 0)
        return false;
    std::vector<std::string> labels = labels_;
    
    wgpuCommandEncoderResolveQuerySet(encoder, querySet_, 0, queryIndex_, 
                                       resolveBuffer_, 0);
    
    // Copy to readback buffer
    wgpuCommandEncoderCopyBufferToBuffer(encoder, resolveBuffer_, 0,
                                          readbackBuffer_, 0,
                                          queryIndex_ * sizeof(uint64_t));
    resolvedLabels_ = std::move(labels);
    resolvedQueryCount_ = queryIndex_;
    pendingReadback_ = true;
    return true;
}

GPUTimingResult GPUTimer::readResults() {
    GPUTimingResult result;
    result.valid = false;
    
    if (!supported_ || !pendingReadback_) {
        return result;
    }
    
#if defined(VOXY_WASM)
    return result;
#else
    const size_t byteCount =
        size_t{resolvedQueryCount_} * sizeof(uint64_t);
    if (!mappingStarted_) {
        mappingState_ =
            std::make_shared<std::atomic<uint32_t>>(Mapping);
        auto* payload = new MapPayload{mappingState_};
        wgpuBufferMapAsync(
            readbackBuffer_, WGPUMapMode_Read, 0, byteCount,
            mapCallback, payload);
        mappingStarted_ = true;
        return result;
    }
    const uint32_t state =
        mappingState_->load(std::memory_order_acquire);
    if (state == Mapping) return result;
    if (state == Failed) {
        pendingReadback_ = false;
        mappingStarted_ = false;
        mappingState_.reset();
        return result;
    }

    const void* mapped =
        wgpuBufferGetConstMappedRange(readbackBuffer_, 0, byteCount);
    if (!mapped || resolvedLabels_.size() != resolvedQueryCount_) {
        wgpuBufferUnmap(readbackBuffer_);
        pendingReadback_ = false;
        mappingStarted_ = false;
        mappingState_.reset();
        return result;
    }
    std::vector<uint64_t> ticks(resolvedQueryCount_);
    std::memcpy(ticks.data(), mapped, byteCount);
    wgpuBufferUnmap(readbackBuffer_);
    pendingReadback_ = false;
    mappingStarted_ = false;
    mappingState_.reset();

    result.timestamps.reserve(resolvedQueryCount_);
    const uint64_t first = ticks.front();
    for (size_t index = 0; index < ticks.size(); ++index) {
        if (ticks[index] < first) return {};
        result.timestamps.push_back(GPUTimestamp{
            .label = resolvedLabels_[index],
            .timeMs = static_cast<double>(ticks[index] - first)
                    * static_cast<double>(timestampPeriod_) * 1.0e-6,
        });
    }
    result.totalMs = result.timestamps.back().timeMs;
    result.valid = true;
    lastResults_ = result;
    return result;
#endif
}

} // namespace voxy::perf


// ═══════════════════════════════════════════════════════════════════════════════
// gpu_timer.cpp - GPU Timing Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "perf/gpu_timer.hpp"
#include "core/log.hpp"

namespace voxy::perf {

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
    , queryIndex_(other.queryIndex_)
    , timestampPeriod_(other.timestampPeriod_)
    , supported_(other.supported_)
    , pendingReadback_(other.pendingReadback_)
    , lastResults_(std::move(other.lastResults_))
{
    other.device_ = nullptr;
    other.querySet_ = nullptr;
    other.resolveBuffer_ = nullptr;
    other.readbackBuffer_ = nullptr;
    other.supported_ = false;
}

GPUTimer& GPUTimer::operator=(GPUTimer&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        querySet_ = other.querySet_;
        resolveBuffer_ = other.resolveBuffer_;
        readbackBuffer_ = other.readbackBuffer_;
        labels_ = std::move(other.labels_);
        queryIndex_ = other.queryIndex_;
        timestampPeriod_ = other.timestampPeriod_;
        supported_ = other.supported_;
        pendingReadback_ = other.pendingReadback_;
        lastResults_ = std::move(other.lastResults_);
        
        other.device_ = nullptr;
        other.querySet_ = nullptr;
        other.resolveBuffer_ = nullptr;
        other.readbackBuffer_ = nullptr;
        other.supported_ = false;
    }
    return *this;
}

bool GPUTimer::init(WGPUDevice device) {
    if (!device) {
        LOG_ERROR("GPUTimer::init: device is null");
        return false;
    }
    
    device_ = device;
    
    // Check if timestamp queries are supported
    // Note: This is a simplified check. In production, you'd query device features.
    // For now, we'll try to create the query set and see if it succeeds.
    
#if defined(VOXY_WASM)
    // Timestamp queries are generally not supported in WebGPU WASM yet
    LOG_INFO("GPUTimer: Timestamp queries not supported in WASM build");
    supported_ = false;
    return false;
#else
    // Try to create a timestamp query set
    WGPUQuerySetDescriptor querySetDesc{};
    querySetDesc.label = "gpu_timer_query_set";
    querySetDesc.type = WGPUQueryType_Timestamp;
    querySetDesc.count = MAX_QUERIES;
    
    querySet_ = wgpuDeviceCreateQuerySet(device, &querySetDesc);
    
    if (!querySet_) {
        LOG_INFO("GPUTimer: Timestamp queries not supported on this device");
        supported_ = false;
        return false;
    }
    
    // Create resolve buffer (for timestamp values)
    uint64_t resolveBufferSize = MAX_QUERIES * sizeof(uint64_t);
    WGPUBufferDescriptor resolveDesc{};
    resolveDesc.label = "gpu_timer_resolve_buffer";
    resolveDesc.size = resolveBufferSize;
    resolveDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    
    resolveBuffer_ = wgpuDeviceCreateBuffer(device, &resolveDesc);
    if (!resolveBuffer_) {
        LOG_ERROR("GPUTimer: Failed to create resolve buffer");
        shutdown();
        return false;
    }
    
    // Create readback buffer (for CPU access)
    WGPUBufferDescriptor readbackDesc{};
    readbackDesc.label = "gpu_timer_readback_buffer";
    readbackDesc.size = resolveBufferSize;
    readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    
    readbackBuffer_ = wgpuDeviceCreateBuffer(device, &readbackDesc);
    if (!readbackBuffer_) {
        LOG_ERROR("GPUTimer: Failed to create readback buffer");
        shutdown();
        return false;
    }
    
    // Get timestamp period (nanoseconds per tick)
    // Note: This API may vary between implementations
    timestampPeriod_ = 1;  // Default to 1ns if not available
    
    labels_.reserve(MAX_QUERIES);
    supported_ = true;
    
    LOG_INFO("GPUTimer initialized with {} query slots", MAX_QUERIES);
    return true;
#endif
}

void GPUTimer::shutdown() {
    if (readbackBuffer_) {
        wgpuBufferRelease(readbackBuffer_);
        readbackBuffer_ = nullptr;
    }
    if (resolveBuffer_) {
        wgpuBufferRelease(resolveBuffer_);
        resolveBuffer_ = nullptr;
    }
    if (querySet_) {
        wgpuQuerySetRelease(querySet_);
        querySet_ = nullptr;
    }
    
    device_ = nullptr;
    supported_ = false;
    labels_.clear();
}

void GPUTimer::beginFrame() {
    queryIndex_ = 0;
    labels_.clear();
}

void GPUTimer::writeTimestamp(WGPUCommandEncoder encoder, const char* label) {
    if (!supported_ || !encoder || queryIndex_ >= MAX_QUERIES) {
        return;
    }
    
    wgpuCommandEncoderWriteTimestamp(encoder, querySet_, queryIndex_);
    labels_.emplace_back(label);
    queryIndex_++;
}

void GPUTimer::resolve(WGPUCommandEncoder encoder) {
    if (!supported_ || !encoder || queryIndex_ == 0) {
        return;
    }
    
    wgpuCommandEncoderResolveQuerySet(encoder, querySet_, 0, queryIndex_, 
                                       resolveBuffer_, 0);
    
    // Copy to readback buffer
    wgpuCommandEncoderCopyBufferToBuffer(encoder, resolveBuffer_, 0,
                                          readbackBuffer_, 0,
                                          queryIndex_ * sizeof(uint64_t));
    
    pendingReadback_ = true;
}

GPUTimingResult GPUTimer::readResults() {
    GPUTimingResult result;
    result.valid = false;
    
    if (!supported_ || !pendingReadback_) {
        return result;
    }
    
    // Note: In a real implementation, you'd use async buffer mapping.
    // This is a simplified synchronous version that may block.
    
    // For now, we'll skip actual GPU timing readback as it requires
    // complex async handling. The framework is in place for future use.
    
    pendingReadback_ = false;
    
    // Return last known results
    return lastResults_;
}

} // namespace voxy::perf




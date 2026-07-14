#include "physics/gpu/debug_readback_ring.hpp"

#include "gpu/resources.hpp"

#include <algorithm>
#include <cstring>

namespace voxy::physics {

DebugReadbackRing::~DebugReadbackRing() { shutdown(); }

bool DebugReadbackRing::initialize(WGPUDevice device, uint32_t slotCount,
                                   size_t slotBytes) {
    shutdown();
    if (!device || slotCount == 0 || slotBytes == 0) return false;
    device_ = device;
    slotBytes_ = slotBytes;
    slots_.resize(slotCount);
    for (auto& slot : slots_) {
        const gpu::BufferDesc desc{
            .label = "physics_debug_readback",
            .size = slotBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        };
        slot.buffer = gpu::createBuffer(device_, desc);
        if (!slot.buffer) {
            shutdown();
            return false;
        }
    }
    return true;
}

void DebugReadbackRing::shutdown() {
    for (auto& slot : slots_) {
        if (!slot.buffer) continue;
        if (slot.state == State::Ready) wgpuBufferUnmap(slot.buffer);
        wgpuBufferDestroy(slot.buffer);
        wgpuBufferRelease(slot.buffer);
    }
    slots_.clear();
    device_ = nullptr;
    slotBytes_ = 0;
    nextSlot_ = 0;
}

bool DebugReadbackRing::encodeCopy(WGPUCommandEncoder encoder,
                                   WGPUBuffer source, uint64_t sourceOffset,
                                   uint64_t byteCount, uint64_t tick,
                                   uint32_t firstBody, uint32_t bodyCount) {
    if (!encoder || !source || byteCount == 0 || byteCount > slotBytes_)
        return false;
    for (size_t attempt = 0; attempt < slots_.size(); ++attempt) {
        const size_t index = (nextSlot_ + attempt) % slots_.size();
        auto& slot = slots_[index];
        if (slot.state != State::Idle) continue;
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, source, sourceOffset, slot.buffer, 0, byteCount);
        slot.tick = tick;
        slot.firstBody = firstBody;
        slot.bodyCount = bodyCount;
        slot.byteCount = static_cast<size_t>(byteCount);
        slot.state = State::CopyEncoded;
        nextSlot_ = (index + 1) % slots_.size();
        return true;
    }
    return false;
}

#if defined(VOXY_WASM)
void DebugReadbackRing::mapCallback(WGPUMapAsyncStatus status,
                                    WGPUStringView, void* userdata,
                                    void*) {
    auto& slot = *static_cast<Slot*>(userdata);
    slot.state = status == WGPUMapAsyncStatus_Success
        ? State::Ready : State::Failed;
}
#else
void DebugReadbackRing::mapCallback(WGPUBufferMapAsyncStatus status,
                                    void* userdata) {
    auto& slot = *static_cast<Slot*>(userdata);
    slot.state = status == WGPUBufferMapAsyncStatus_Success
        ? State::Ready : State::Failed;
}
#endif

std::optional<RawDebugReadback> DebugReadbackRing::poll() {
    for (auto& slot : slots_) {
        if (slot.state == State::CopyEncoded) {
            slot.state = State::Mapping;
#if defined(VOXY_WASM)
            WGPUBufferMapCallbackInfo callbackInfo =
                WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
            callbackInfo.callback = mapCallback;
            callbackInfo.userdata1 = &slot;
            static_cast<void>(wgpuBufferMapAsync(
                slot.buffer, WGPUMapMode_Read, 0, slot.byteCount,
                callbackInfo));
#else
            wgpuBufferMapAsync(slot.buffer, WGPUMapMode_Read, 0,
                               slot.byteCount, mapCallback, &slot);
#endif
        }
    }
    for (auto& slot : slots_) {
        if (slot.state == State::Failed) {
            slot.state = State::Idle;
            continue;
        }
        if (slot.state != State::Ready) continue;
        RawDebugReadback result;
        result.tick = slot.tick;
        result.firstBody = slot.firstBody;
        result.bodyCount = slot.bodyCount;
        result.bytes.resize(slot.byteCount);
        const void* mapped = wgpuBufferGetConstMappedRange(
            slot.buffer, 0, slot.byteCount);
        if (mapped) std::memcpy(result.bytes.data(), mapped, slot.byteCount);
        wgpuBufferUnmap(slot.buffer);
        slot.state = State::Idle;
        return result;
    }
    return std::nullopt;
}

} // namespace voxy::physics

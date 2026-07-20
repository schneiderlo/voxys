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
        const State mappingState = slot.mapping
            ? slot.mapping->state.load(std::memory_order_acquire)
            : slot.state;
        if (slot.state == State::Mapping || slot.state == State::Ready
            || mappingState == State::Mapping
            || mappingState == State::Ready) {
            // Unmap also cancels an outstanding map request. The callback owns
            // only its heap payload/shared state, so an asynchronous Abort
            // cannot touch this ring after shutdown.
            wgpuBufferUnmap(slot.buffer);
        }
        wgpuBufferDestroy(slot.buffer);
        wgpuBufferRelease(slot.buffer);
    }
    slots_.clear();
    device_ = nullptr;
    slotBytes_ = 0;
    nextSlot_ = 0;
    nextSequence_ = 1;
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
        slot.sequence = nextSequence_++;
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
    std::unique_ptr<CallbackPayload> payload(
        static_cast<CallbackPayload*>(userdata));
    payload->mapping->state.store(
        status == WGPUMapAsyncStatus_Success
            ? State::Ready : State::Failed,
        std::memory_order_release);
}
#else
void DebugReadbackRing::mapCallback(WGPUBufferMapAsyncStatus status,
                                    void* userdata) {
    std::unique_ptr<CallbackPayload> payload(
        static_cast<CallbackPayload*>(userdata));
    payload->mapping->state.store(
        status == WGPUBufferMapAsyncStatus_Success
            ? State::Ready : State::Failed,
        std::memory_order_release);
}
#endif

std::optional<RawDebugReadback> DebugReadbackRing::poll() {
    for (auto& slot : slots_) {
        if (slot.state == State::CopyEncoded) {
            slot.state = State::Mapping;
            slot.mapping = std::make_shared<MappingState>();
            auto* payload = new CallbackPayload{slot.mapping};
#if defined(VOXY_WASM)
            WGPUBufferMapCallbackInfo callbackInfo =
                WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
            callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
            callbackInfo.callback = mapCallback;
            callbackInfo.userdata1 = payload;
            static_cast<void>(wgpuBufferMapAsync(
                slot.buffer, WGPUMapMode_Read, 0, slot.byteCount,
                callbackInfo));
#else
            wgpuBufferMapAsync(slot.buffer, WGPUMapMode_Read, 0,
                               slot.byteCount, mapCallback, payload);
#endif
        }
    }

    for (auto& slot : slots_) {
        if (slot.state != State::Mapping || !slot.mapping) continue;
        const State mapped = slot.mapping->state.load(std::memory_order_acquire);
        if (mapped == State::Ready || mapped == State::Failed) {
            slot.state = mapped;
            slot.mapping.reset();
        }
    }

    while (true) {
        Slot* oldest = nullptr;
        for (auto& slot : slots_) {
            if (slot.state == State::Idle) continue;
            if (!oldest || slot.sequence < oldest->sequence) oldest = &slot;
        }
        if (!oldest) return std::nullopt;
        if (oldest->state == State::Failed) {
            oldest->state = State::Idle;
            continue;
        }
        if (oldest->state != State::Ready) return std::nullopt;

        RawDebugReadback result;
        result.tick = oldest->tick;
        result.firstBody = oldest->firstBody;
        result.bodyCount = oldest->bodyCount;
        result.bytes.resize(oldest->byteCount);
        const void* mapped = wgpuBufferGetConstMappedRange(
            oldest->buffer, 0, oldest->byteCount);
        if (!mapped) {
            wgpuBufferUnmap(oldest->buffer);
            oldest->state = State::Idle;
            return std::nullopt;
        }
        std::memcpy(result.bytes.data(), mapped, oldest->byteCount);
        wgpuBufferUnmap(oldest->buffer);
        oldest->state = State::Idle;
        return result;
    }
}

} // namespace voxy::physics

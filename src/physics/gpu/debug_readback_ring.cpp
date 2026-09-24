#include "physics/gpu/debug_readback_ring.hpp"

#include "gpu/resources.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace voxy::physics {

DebugReadbackRing::~DebugReadbackRing() { shutdown(); }

bool DebugReadbackRing::initialize(WGPUDevice device, uint32_t slotCount,
                                   size_t slotBytes) {
    if (!device || slotCount == 0 || slotCount > kMaximumSlots
        || slotBytes == 0
        || slotBytes > std::numeric_limits<size_t>::max() / slotCount) {
        return false;
    }
    std::vector<Slot> replacement(slotCount);
    for (auto& slot : replacement) {
        const gpu::BufferDesc desc{
            .label = "physics_debug_readback",
            .size = slotBytes,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        };
        slot.buffer = gpu::createBuffer(device, desc);
        if (!slot.buffer) {
            for (auto& created : replacement) {
                if (!created.buffer) continue;
                wgpuBufferDestroy(created.buffer);
                wgpuBufferRelease(created.buffer);
                created.buffer = nullptr;
            }
            return false;
        }
    }
    shutdown();
    device_ = device;
    slotBytes_ = slotBytes;
    slots_ = std::move(replacement);
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
    failedReadbacks_ = 0;
}

uint32_t DebugReadbackRing::availableSlots() const noexcept {
    return static_cast<uint32_t>(std::count_if(slots_.begin(),slots_.end(),
        [](const Slot& slot){return slot.state==State::Idle;}));
}

std::optional<size_t> DebugReadbackRing::nextAvailableSlot() const noexcept {
    for (size_t attempt = 0; attempt < slots_.size(); ++attempt) {
        const size_t index = (nextSlot_ + attempt) % slots_.size();
        if (slots_[index].state == State::Idle) return index;
    }
    return std::nullopt;
}

bool DebugReadbackRing::encodeCopy(WGPUCommandEncoder encoder,
                                   WGPUBuffer source, uint64_t sourceOffset,
                                   uint64_t byteCount, uint64_t tick,
                                   uint32_t firstBody, uint32_t bodyCount,
                                   std::optional<size_t> slotIndex,
                                   uint64_t submissionSerial) {
    const DebugReadbackCopy copy{source,sourceOffset,byteCount};
    return encodeCopies(encoder,std::span(&copy,1),tick,{firstBody,bodyCount},slotIndex,submissionSerial);
}

bool DebugReadbackRing::encodeCopies(WGPUCommandEncoder encoder,
    std::span<const DebugReadbackCopy> copies, uint64_t tick, DebugReadbackRange range,
    std::optional<size_t> slotIndex, uint64_t submissionSerial) {
    if (!encoder || copies.empty()) return false;
    uint64_t byteCount=0;
    for (const auto& copy:copies) {
        if (!copy.source || !copy.bytes || copy.bytes>slotBytes_-byteCount) return false;
        const uint64_t sourceBytes=wgpuBufferGetSize(copy.source);
        if (!(wgpuBufferGetUsage(copy.source)&WGPUBufferUsage_CopySrc)
            || copy.offset%4 || copy.bytes%4 || copy.offset>sourceBytes
            || copy.bytes>sourceBytes-copy.offset) return false;
        byteCount+=copy.bytes;
    }
    const auto available = slotIndex ? slotIndex : nextAvailableSlot();
    if (available && *available < slots_.size()
        && slots_[*available].state == State::Idle) {
        const size_t index = *available;
        auto& slot = slots_[index];
        uint64_t destinationOffset=0;
        for (const auto& copy:copies) {
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder,copy.source,copy.offset,slot.buffer,destinationOffset,copy.bytes);
            destinationOffset+=copy.bytes;
        }
        slot.tick = tick;
        slot.submissionSerial=submissionSerial;
        slot.sequence = nextSequence_++;
        slot.firstBody = range.firstBody;
        slot.bodyCount = range.bodyCount;
        slot.firstAttachment = range.firstAttachment;
        slot.attachmentCount = range.attachmentCount;
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

std::optional<RawDebugReadback> DebugReadbackRing::poll(
    std::optional<CountedReadbackLayout> counted) {
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
            if(failedReadbacks_!=UINT64_MAX) ++failedReadbacks_;
            oldest->state = State::Idle;
            continue;
        }
        if (oldest->state != State::Ready) return std::nullopt;

        RawDebugReadback result;
        result.tick = oldest->tick;
        result.submissionSerial=oldest->submissionSerial;
        result.firstBody = oldest->firstBody;
        result.bodyCount = oldest->bodyCount;
        result.firstAttachment = oldest->firstAttachment;
        result.attachmentCount = oldest->attachmentCount;
        const auto fail = [&]() -> std::optional<RawDebugReadback> {
            if (failedReadbacks_ != UINT64_MAX) ++failedReadbacks_;
            wgpuBufferUnmap(oldest->buffer);
            oldest->state = State::Idle;
            return std::nullopt;
        };
        // Mapped ranges must be non-overlapping, with 8-byte offsets and
        // 4-byte sizes. Bound GPU-provided counts before multiplying them.
        if (counted && (counted->headerBytes < sizeof(uint32_t)
            || counted->headerBytes % 8u != 0u
            || counted->headerBytes > oldest->byteCount
            || counted->recordBytes == 0u || counted->recordBytes % 4u != 0u
            || (oldest->byteCount - counted->headerBytes) % counted->recordBytes != 0u)) {
            return fail();
        }
        const size_t prefixBytes = counted ? counted->headerBytes : oldest->byteCount;
        result.bytes.resize(prefixBytes);
        const void* mapped = wgpuBufferGetConstMappedRange(
            oldest->buffer, 0, prefixBytes);
        if (!mapped) return fail();
        std::memcpy(result.bytes.data(), mapped, prefixBytes);
        if (counted) {
            uint32_t count = 0;
            std::memcpy(&count, result.bytes.data(), sizeof(count));
            const size_t capacity = (oldest->byteCount - prefixBytes) / counted->recordBytes;
            const size_t payloadBytes = std::min(size_t{count}, capacity) * counted->recordBytes;
            // Keep the original count/overflow header, including corrupt counts,
            // so the packet consumer retains its existing validation contract.
            if (payloadBytes != 0u) {
                const void* payload = wgpuBufferGetConstMappedRange(
                    oldest->buffer, prefixBytes, payloadBytes);
                if (!payload) return fail();
                result.bytes.resize(prefixBytes + payloadBytes);
                std::memcpy(result.bytes.data() + prefixBytes, payload, payloadBytes);
            }
        }
        wgpuBufferUnmap(oldest->buffer);
        oldest->state = State::Idle;
        return result;
    }
}

} // namespace voxy::physics

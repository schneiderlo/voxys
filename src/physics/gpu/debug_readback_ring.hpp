#pragma once

#include "gpu/webgpu_compat.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voxy::physics {

struct RawDebugReadback {
    uint64_t tick = 0;
    uint32_t firstBody = 0;
    uint32_t bodyCount = 0;
    std::vector<std::byte> bytes;
    uint64_t submissionSerial = 0;
    uint32_t firstAttachment = 0, attachmentCount = 0;
};

struct DebugReadbackCopy {
    WGPUBuffer source = nullptr;
    uint64_t offset = 0, bytes = 0;
};

struct DebugReadbackRange {
    uint32_t firstBody = 0, bodyCount = 0;
    uint32_t firstAttachment = 0, attachmentCount = 0;
};

class DebugReadbackRing {
public:
    static constexpr uint32_t kMaximumSlots = 64u;
    DebugReadbackRing() = default;
    ~DebugReadbackRing();

    DebugReadbackRing(const DebugReadbackRing&) = delete;
    DebugReadbackRing& operator=(const DebugReadbackRing&) = delete;

    [[nodiscard]] bool initialize(WGPUDevice device, uint32_t slotCount,
                                  size_t slotBytes);
    void shutdown();

    // The slot encodeCopy will use, unless the ring changes first. Allows
    // callers to keep per-copy GPU parameters alive for the same lifetime.
    [[nodiscard]] std::optional<size_t> nextAvailableSlot() const noexcept;
    [[nodiscard]] uint32_t availableSlots() const noexcept;
    [[nodiscard]] uint64_t failedReadbacks() const noexcept { return failedReadbacks_; }
    // An explicit slot must still be idle; no other slot is substituted.
    [[nodiscard]] bool encodeCopy(WGPUCommandEncoder encoder,
                                  WGPUBuffer source, uint64_t sourceOffset,
                                  uint64_t byteCount, uint64_t tick,
                                  uint32_t firstBody, uint32_t bodyCount,
                                  std::optional<size_t> slotIndex = std::nullopt,
                                  uint64_t submissionSerial = 0);
    // Validate every source before encoding any copy. Concatenate the ranges
    // into one slot/map so bodies and constraints cannot arrive independently.
    [[nodiscard]] bool encodeCopies(WGPUCommandEncoder encoder,
        std::span<const DebugReadbackCopy> copies, uint64_t tick,
        DebugReadbackRange range, std::optional<size_t> slotIndex = std::nullopt,
        uint64_t submissionSerial = 0);
    [[nodiscard]] std::optional<RawDebugReadback> poll();
    [[nodiscard]] size_t allocatedBytes() const noexcept {
        return slots_.size() * slotBytes_;
    }

private:
    enum class State : uint8_t { Idle, CopyEncoded, Mapping, Ready, Failed };
    struct MappingState {
        std::atomic<State> state{State::Mapping};
    };
    struct CallbackPayload {
        std::shared_ptr<MappingState> mapping;
    };
    struct Slot {
        WGPUBuffer buffer = nullptr;
        State state = State::Idle;
        std::shared_ptr<MappingState> mapping;
        uint64_t sequence = 0;
        uint64_t tick = 0;
        uint32_t firstBody = 0;
        uint32_t bodyCount = 0;
        uint32_t firstAttachment = 0, attachmentCount = 0;
        size_t byteCount = 0;
        uint64_t submissionSerial = 0;
    };

#if defined(VOXY_WASM)
    static void mapCallback(WGPUMapAsyncStatus status, WGPUStringView message,
                            void* userdata1, void* userdata2);
#else
    static void mapCallback(WGPUBufferMapAsyncStatus status, void* userdata);
#endif

    WGPUDevice device_ = nullptr;
    size_t slotBytes_ = 0;
    size_t nextSlot_ = 0;
    uint64_t nextSequence_ = 1;
    uint64_t failedReadbacks_ = 0;
    std::vector<Slot> slots_;
};

} // namespace voxy::physics

#pragma once

#include "gpu/webgpu_compat.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace voxy::physics {

struct RawDebugReadback {
    uint64_t tick = 0;
    uint32_t firstBody = 0;
    uint32_t bodyCount = 0;
    std::vector<std::byte> bytes;
};

class DebugReadbackRing {
public:
    DebugReadbackRing() = default;
    ~DebugReadbackRing();

    DebugReadbackRing(const DebugReadbackRing&) = delete;
    DebugReadbackRing& operator=(const DebugReadbackRing&) = delete;

    [[nodiscard]] bool initialize(WGPUDevice device, uint32_t slotCount,
                                  size_t slotBytes);
    void shutdown();

    [[nodiscard]] bool encodeCopy(WGPUCommandEncoder encoder,
                                  WGPUBuffer source, uint64_t sourceOffset,
                                  uint64_t byteCount, uint64_t tick,
                                  uint32_t firstBody, uint32_t bodyCount);
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
        size_t byteCount = 0;
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
    std::vector<Slot> slots_;
};

} // namespace voxy::physics

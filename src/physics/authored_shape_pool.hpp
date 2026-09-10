#pragma once

#include "physics/authored_shape.hpp"
#include "physics/shape_handle.hpp"

namespace voxy::physics {

struct AuthoredShapePoolLimits {
    uint32_t slots = 256; // Live plus retiring, not just currently drawn bodies.
    uint32_t cells = 16384;
    uint32_t faces = 98304;
    uint32_t nodes = 32768;
    uint64_t bytes = 16 * 1024 * 1024;
    // A smaller ceiling can enforce a conservative session lifetime. Never wrap.
    uint32_t generationsPerSlot = UINT32_MAX;
};
enum class ShapePoolError : uint8_t {
    None, InvalidProfile, Allocation, InvalidShape, Capacity, GenerationExhausted,
    InvalidHandle, Retiring, ReferenceOverflow, NoReference, InvalidSubmission,
};
struct ShapePoolStats {
    ShapeResourceCost charged{};
    uint32_t resident = 0, retiring = 0, exhausted = 0;
    uint64_t references = 0;
    uint64_t submitted = 0, completed = 0;
    [[nodiscard]] bool operator==(const ShapePoolStats&) const = default;
};

// Single owner, CPU resource lifetime ledger. Creation allocates its fixed slot
// table. Insert takes already prepared storage; all subsequent operations are
// allocation-free. No operation uploads, submits or waits on a GPU by itself.
// The backend must call submitted BEFORE handing that submission to its queue,
// and completed ONLY from certified completion of that same queue/incarnation.
// If encoding/submission fails after recording use, conservatively retain it
// until real completion or destroy the entire drained/device-lost pool.
class AuthoredShapePool {
public:
    [[nodiscard]] static std::optional<AuthoredShapePool> create(
        uint64_t uniquePoolIdentity, ShapePoolError&, AuthoredShapePoolLimits = {});
    AuthoredShapePool(const AuthoredShapePool&) = delete;
    AuthoredShapePool& operator=(const AuthoredShapePool&) = delete;
    AuthoredShapePool(AuthoredShapePool&&) noexcept;
    AuthoredShapePool& operator=(AuthoredShapePool&&) = delete;

    // On refusal neither the pool nor the caller's rvalue shape is changed.
    [[nodiscard]] ShapeHandle insert(AuthoredShape&&, ShapePoolError&) noexcept;
    // Borrow valid until a collecting mutation. Retain before keeping it across
    // mutations/async readback. A retained retiring shape remains readable.
    [[nodiscard]] const AuthoredShape* get(ShapeHandle) const noexcept;
    [[nodiscard]] ShapePoolError retain(ShapeHandle) noexcept;
    [[nodiscard]] ShapePoolError release(ShapeHandle) noexcept;
    // Freeze future retain/submission admission, keeping old readers and GPU
    // references alive. Idempotent while still retiring; stale after collection.
    [[nodiscard]] ShapePoolError retire(ShapeHandle) noexcept;
    // Monotonic serial, all handles resident; entire call validates before any
    // mutation. Duplicates are harmless. Empty spans track unrelated queue work.
    [[nodiscard]] ShapePoolError submitted(uint64_t serial, std::span<const ShapeHandle>) noexcept;
    [[nodiscard]] ShapePoolError completed(uint64_t serial) noexcept;
    [[nodiscard]] ShapePoolStats stats() const noexcept { return stats_; }
    [[nodiscard]] uint64_t identity() const noexcept { return identity_; }
    [[nodiscard]] size_t slotStorageBytes() const noexcept { return slots_.capacity()*sizeof(Slot); }
private:
    enum class State : uint8_t { Free, Resident, Retiring, Exhausted };
    struct Slot {
        std::optional<AuthoredShape> shape{};
        uint64_t lastSubmission = 0;
        uint32_t references = 0;
        uint32_t generation = 1;
        State state = State::Free;
    };
    AuthoredShapePool(uint64_t identity, AuthoredShapePoolLimits limits) : identity_(identity), limits_(limits) {}
    [[nodiscard]] Slot* slot(ShapeHandle) noexcept;
    [[nodiscard]] const Slot* slot(ShapeHandle) const noexcept;
    void collect(Slot&) noexcept;
    uint64_t identity_ = 0;
    AuthoredShapePoolLimits limits_{};
    std::vector<Slot> slots_{};
    ShapePoolStats stats_{};
};
} // namespace voxy::physics

#include "physics/authored_shape_pool.hpp"

#include <new>
#include <type_traits>

namespace voxy::physics {
static_assert(std::is_nothrow_move_constructible_v<AuthoredShape>);

std::optional<AuthoredShapePool> AuthoredShapePool::create(uint64_t identity,
    ShapePoolError& error, AuthoredShapePoolLimits limits) {
    if (identity==0 || limits.slots==0 || limits.slots>512 || limits.cells==0 || limits.cells>65536
        || limits.faces==0 || limits.faces>393216 || limits.nodes==0 || limits.nodes>131072
        || limits.bytes==0 || limits.bytes>32*1024*1024 || limits.generationsPerSlot==0) {
        error=ShapePoolError::InvalidProfile; return std::nullopt;
    }
    AuthoredShapePool result{identity,limits};
    try { result.slots_.resize(limits.slots); }
    catch (const std::bad_alloc&) { error=ShapePoolError::Allocation; return std::nullopt; }
    error=ShapePoolError::None; return result;
}
AuthoredShapePool::AuthoredShapePool(AuthoredShapePool&& other) noexcept
    : identity_(std::exchange(other.identity_,0)),limits_(other.limits_),slots_(std::move(other.slots_)),
      stats_(std::exchange(other.stats_,{})) {}

const AuthoredShapePool::Slot* AuthoredShapePool::slot(ShapeHandle handle) const noexcept {
    if (!handle.valid() || handle.pool!=identity_ || handle.index>slots_.size()) return nullptr;
    const auto& entry=slots_[handle.index-1];
    return entry.generation==handle.generation && entry.shape ? &entry : nullptr;
}
AuthoredShapePool::Slot* AuthoredShapePool::slot(ShapeHandle handle) noexcept {
    if (!handle.valid() || handle.pool!=identity_ || handle.index>slots_.size()) return nullptr;
    auto& entry=slots_[handle.index-1];
    return entry.generation==handle.generation && entry.shape ? &entry : nullptr;
}
ShapeHandle AuthoredShapePool::insert(AuthoredShape&& shape, ShapePoolError& error) noexcept {
    const auto refuse=[&](ShapePoolError reason) { error=reason; return ShapeHandle{}; };
    if (identity_==0) return refuse(ShapePoolError::InvalidProfile);
    const auto cost=shape.cost();
    if (cost.cells==0 || cost.faces==0 || cost.nodes==0) return refuse(ShapePoolError::InvalidShape);
    if (cost.cells>limits_.cells-stats_.charged.cells || cost.faces>limits_.faces-stats_.charged.faces
        || cost.nodes>limits_.nodes-stats_.charged.nodes || cost.bytes>limits_.bytes-stats_.charged.bytes)
        return refuse(ShapePoolError::Capacity);
    for (size_t i=0; i<slots_.size(); ++i) {
        auto& entry=slots_[i];
        if (entry.state!=State::Free) continue;
        entry.shape.emplace(std::move(shape)); entry.state=State::Resident;
        entry.lastSubmission=0; entry.references=0;
        stats_.charged.cells+=cost.cells; stats_.charged.faces+=cost.faces;
        stats_.charged.nodes+=cost.nodes; stats_.charged.bytes+=cost.bytes; ++stats_.resident;
        error=ShapePoolError::None;
        return {static_cast<uint32_t>(i+1),entry.generation,identity_};
    }
    return refuse(stats_.exhausted==slots_.size() ? ShapePoolError::GenerationExhausted : ShapePoolError::Capacity);
}
const AuthoredShape* AuthoredShapePool::get(ShapeHandle handle) const noexcept {
    const auto* entry=slot(handle); return entry ? &*entry->shape : nullptr;
}
ShapePoolError AuthoredShapePool::retain(ShapeHandle handle) noexcept {
    auto* entry=slot(handle);
    if (!entry) return ShapePoolError::InvalidHandle;
    if (entry->state!=State::Resident) return ShapePoolError::Retiring;
    if (entry->references==UINT32_MAX) return ShapePoolError::ReferenceOverflow;
    ++entry->references; ++stats_.references; return ShapePoolError::None;
}
ShapePoolError AuthoredShapePool::release(ShapeHandle handle) noexcept {
    auto* entry=slot(handle);
    if (!entry) return ShapePoolError::InvalidHandle;
    if (entry->references==0) return ShapePoolError::NoReference;
    --entry->references; --stats_.references; collect(*entry); return ShapePoolError::None;
}
ShapePoolError AuthoredShapePool::retire(ShapeHandle handle) noexcept {
    auto* entry=slot(handle);
    if (!entry) return ShapePoolError::InvalidHandle;
    if (entry->state==State::Resident) { entry->state=State::Retiring; --stats_.resident; ++stats_.retiring; }
    collect(*entry); return ShapePoolError::None;
}
ShapePoolError AuthoredShapePool::submitted(uint64_t serial,std::span<const ShapeHandle> handles) noexcept {
    if (identity_==0 || serial<=stats_.submitted) return ShapePoolError::InvalidSubmission;
    // Callers deduplicate body references to the at-most-512 distinct shapes.
    if (handles.size()>512) return ShapePoolError::Capacity;
    for (auto handle:handles) {
        const auto* entry=slot(handle);
        if (!entry) return ShapePoolError::InvalidHandle;
        if (entry->state!=State::Resident) return ShapePoolError::Retiring;
    }
    for (auto handle:handles) slot(handle)->lastSubmission=serial;
    stats_.submitted=serial; return ShapePoolError::None;
}
ShapePoolError AuthoredShapePool::completed(uint64_t serial) noexcept {
    if (identity_==0 || serial<stats_.completed || serial>stats_.submitted) return ShapePoolError::InvalidSubmission;
    stats_.completed=serial;
    for (auto& entry:slots_) collect(entry);
    return ShapePoolError::None;
}
void AuthoredShapePool::collect(Slot& entry) noexcept {
    if (entry.state!=State::Retiring || entry.references!=0 || entry.lastSubmission>stats_.completed) return;
    const auto cost=entry.shape->cost();
    stats_.charged.cells-=cost.cells; stats_.charged.faces-=cost.faces;
    stats_.charged.nodes-=cost.nodes; stats_.charged.bytes-=cost.bytes;
    --stats_.retiring; entry.shape.reset();
    if (entry.generation==limits_.generationsPerSlot) { entry.state=State::Exhausted; ++stats_.exhausted; }
    else { ++entry.generation; entry.state=State::Free; }
}
} // namespace voxy::physics

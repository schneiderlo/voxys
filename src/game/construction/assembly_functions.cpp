#include "game/construction/assembly_functions.hpp"

#include <algorithm>
#include <new>

namespace voxy::game::construction {
namespace {
size_t frameCount(const PartModule& parameters) noexcept {
    return std::visit([](const auto& value) -> size_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PropellerModule>) return 3;
        else if constexpr (std::is_same_v<T, EngineModule> || std::is_same_v<T, HelmModule>
            || std::is_same_v<T, WinchModule> || std::is_same_v<T, TowEyeModule> || std::is_same_v<T, CargoCradleModule>) return 2;
        else {
            static_assert(std::is_same_v<T, StructureModule> || std::is_same_v<T, FlotationModule>
                || std::is_same_v<T, BraceModule> || std::is_same_v<T, BallastModule> || std::is_same_v<T, RepairModule>);
            return 1;
        }
    }, parameters);
}
}

uint32_t AssemblyFunctionPlan::socketIndex(SocketEndpoint endpoint) const noexcept {
    const auto found = std::lower_bound(sockets_.begin(), sockets_.end(), endpoint,
        [](const AssemblySocketBinding& item, SocketEndpoint key) { return item.endpoint < key; });
    return found != sockets_.end() && found->endpoint == endpoint
        ? static_cast<uint32_t>(found - sockets_.begin()) : kNoAssemblySocket;
}
const AssemblySocketBinding* AssemblyFunctionPlan::socket(SocketEndpoint endpoint) const noexcept {
    const auto index = socketIndex(endpoint);
    return index != kNoAssemblySocket ? &sockets_[index] : nullptr;
}
const AssemblyModuleBinding* AssemblyFunctionPlan::module(DurableId part) const noexcept {
    const auto found = std::lower_bound(modules_.begin(), modules_.end(), part,
        [](const AssemblyModuleBinding& item, DurableId key) { return item.part < key; });
    return found != modules_.end() && found->part == part ? &*found : nullptr;
}
std::span<const AssemblyFunctionFrame> AssemblyFunctionPlan::moduleFrames(size_t index) const noexcept {
    if (index >= modules_.size()) return {};
    const auto& item = modules_[index];
    return std::span<const AssemblyFunctionFrame>{frames_}.subspan(item.firstFrame, item.frameCount);
}

std::optional<AssemblyFunctionPlan> AssemblyFunctionPlan::compile(
    const BuildSnapshot& input, const PartCatalog& catalog, AssemblyFunctionIssue& issue,
    AssemblyMassLimits massLimits, BoxUnionLimits collisionLimits, BoxCoverageLimits coverageLimits, AssemblyFunctionLimits limits) {
    const auto refuse = [&](AssemblyFunctionError error, DurableId object = {}) -> std::optional<AssemblyFunctionPlan> {
        issue = {{}, error, object}; return std::nullopt;
    };
    if (limits.modules > kMaximumBuildParts || limits.sockets > kMaximumBuildSocketRecords
        || limits.connections > kMaximumBuildConnections || limits.frames > kMaximumAssemblyFunctionFrames)
        return refuse(AssemblyFunctionError::InvalidProfile);
    if (input.parts.size() > limits.modules || input.connections.size() > limits.connections)
        return refuse(AssemblyFunctionError::Capacity, input.id);
    try {
        AssemblyBuoyancyIssue buoyancyIssue;
        auto buoyancy = AssemblyBuoyancyPlan::compile(input, catalog, buoyancyIssue, massLimits, collisionLimits, coverageLimits);
        if (!buoyancy) { issue = {buoyancyIssue, AssemblyFunctionError::None, {}}; return std::nullopt; }
        size_t sockets = 0, frames = 0;
        for (const auto& part : buoyancy->collisionPlan().massPlan().parts()) {
            const auto& definition = *catalog.lookup(part.definition).definition;
            const auto count = frameCount(definition.module);
            if (definition.sockets.size() > limits.sockets - sockets || count > limits.frames - frames)
                return refuse(AssemblyFunctionError::Capacity, part.part);
            sockets += definition.sockets.size(); frames += count;
        }
        // Geometry compilation already validated the exact input/catalog. Sort
        // bounded borrowed pointers for this invocation only, not a second build.
        std::array<const PartInstance*, kMaximumBuildParts> ordered{};
        for (size_t i = 0; i < input.parts.size(); ++i) ordered[i] = &input.parts[i];
        const auto parts = std::span{ordered}.first(input.parts.size());
        std::sort(parts.begin(), parts.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
        AssemblyFunctionPlan result{std::move(*buoyancy)};
        result.modules_.reserve(parts.size()); result.sockets_.reserve(sockets); result.frames_.reserve(frames);
        result.connections_.reserve(input.connections.size());
        for (size_t i = 0; i < parts.size(); ++i) {
            const auto& source = *parts[i]; const auto& massPart = result.massPlan().parts()[i];
            const auto& definition = *catalog.lookup(source.definition).definition;
            const auto moduleIndex = static_cast<uint32_t>(i);
            AssemblyModuleBinding module{source.id, massPart.root, definition.module, source.settings, source.health, definition.strength,
                static_cast<uint32_t>(result.sockets_.size()), static_cast<uint32_t>(definition.sockets.size()),
                static_cast<uint32_t>(result.frames_.size()), static_cast<uint32_t>(frameCount(definition.module))};
            for (const auto& socket : definition.sockets) {
                const auto frame = compose(massPart.rootFromPart, socket.frame);
                if (!frame) return refuse(AssemblyFunctionError::InvalidFrame, source.id);
                result.sockets_.push_back({{source.id, socket.id}, massPart.root, socket, *frame, 0});
            }
            result.frames_.push_back({moduleIndex, AssemblyFrameKind::PartOrigin, massPart.rootFromPart, kNoAssemblySocket});
            const auto addFrame = [&](AssemblyFrameKind kind, GridTransform local) {
                const auto frame = compose(massPart.rootFromPart, local);
                if (!frame) return false;
                result.frames_.push_back({moduleIndex, kind, *frame, kNoAssemblySocket}); return true;
            };
            const auto addSocket = [&](AssemblyFrameKind kind, SocketId id) {
                const auto index = result.socketIndex({source.id, id});
                if (index == kNoAssemblySocket) return false;
                result.frames_.push_back({moduleIndex, kind, result.sockets_[index].rootFromSocket, index}); return true;
            };
            const bool valid = std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, EngineModule>) return addSocket(AssemblyFrameKind::DriveShaft, value.shaft);
                else if constexpr (std::is_same_v<T, PropellerModule>) return addSocket(AssemblyFrameKind::DriveShaft, value.shaft)
                    && addFrame(AssemblyFrameKind::Thrust, value.forceFrame); // Local -Z, never the socket's outward +Y.
                else if constexpr (std::is_same_v<T, HelmModule>) return addFrame(AssemblyFrameKind::Operator, value.operatorFrame);
                else if constexpr (std::is_same_v<T, WinchModule>) return addSocket(AssemblyFrameKind::TowLine, value.line);
                else if constexpr (std::is_same_v<T, TowEyeModule>) return addSocket(AssemblyFrameKind::TowEye, value.eye);
                else if constexpr (std::is_same_v<T, CargoCradleModule>) return addSocket(AssemblyFrameKind::CargoLatch, value.latch);
                else {
                    static_assert(std::is_same_v<T, StructureModule> || std::is_same_v<T, FlotationModule>
                        || std::is_same_v<T, BraceModule> || std::is_same_v<T, BallastModule> || std::is_same_v<T, RepairModule>);
                    return true; // Repair's reach is centered on the part origin in profile 1.
                }
            }, definition.module);
            if (!valid) return refuse(AssemblyFunctionError::InvalidFrame, source.id);
            result.modules_.push_back(std::move(module));
        }
        for (auto connection : input.connections) {
            if (connection.b < connection.a) std::swap(connection.a, connection.b);
            const auto a = result.socketIndex(connection.a), b = result.socketIndex(connection.b);
            if (a == kNoAssemblySocket || b == kNoAssemblySocket) return refuse(AssemblyFunctionError::InvalidFrame, connection.id);
            ++result.sockets_[a].usedSlots; ++result.sockets_[b].usedSlots; // Validated capacities include disabled links.
            result.connections_.push_back({connection, a, b});
        }
        std::sort(result.connections_.begin(), result.connections_.end(),
            [](const auto& a, const auto& b) { return a.definition.id < b.definition.id; });
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(AssemblyFunctionError::Capacity, input.id); }
}
} // namespace voxy::game::construction

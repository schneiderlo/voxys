#pragma once

#include "gameplay/gameplay_types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace voxy::gameplay {

enum AssemblyNodeFlag : uint32_t {
    AssemblyNodeSealed = 1u << 0u,
    AssemblyNodeFoundation = 1u << 1u,
};

struct AssemblyNode {
    uint32_t id = 0;
    std::array<int32_t, 3> localPositionQ12{};
    int32_t massQ16 = 0;
    int32_t displacedVolumeQ16 = 0;
    uint32_t flags = AssemblyNodeSealed;
};

struct AssemblyEdge {
    uint32_t id = 0;
    uint32_t nodeA = 0;
    uint32_t nodeB = 0;
    int32_t healthQ16 = 0;
    uint32_t material = 0;
};

struct AssemblyDamageCommand {
    uint64_t tick = 0;
    uint64_t sequence = 0;
    uint32_t source = 0;
    uint32_t edgeId = 0;
    int32_t damageQ16 = 0;

    [[nodiscard]] bool operator==(
        const AssemblyDamageCommand&) const = default;
};

[[nodiscard]] bool assemblyDamageCommandLess(
    const AssemblyDamageCommand& lhs,
    const AssemblyDamageCommand& rhs) noexcept;

struct AssemblyComponent {
    uint32_t rootNode = 0;
    std::vector<uint32_t> nodeIds;
    std::vector<uint32_t> activeEdgeIds;
    int32_t massQ16 = 0;
    int32_t displacedVolumeQ16 = 0;
    std::array<int32_t, 3> centerOfMassQ12{};
    bool containsKeel = false;
    bool significant = false;
    bool tinyDebris = false;
};

struct AssemblyFractureEvent {
    uint64_t tick = 0;
    std::vector<uint32_t> brokenEdgeIds;
    std::vector<uint32_t> componentRoots;
    uint32_t stateHash = 0;
};

class StructuralAssembly {
public:
    struct Config {
        uint32_t maximumNodes = kMaximumAssemblyNodes;
        uint32_t maximumEdges = kMaximumAssemblyEdges;
        uint32_t keelNodeId = 1;
        int32_t significantMassQ16 = 2 * kScalarOne;
    };

    StructuralAssembly();
    explicit StructuralAssembly(Config config);

    [[nodiscard]] bool initialize(
        std::span<const AssemblyNode> nodes,
        std::span<const AssemblyEdge> edges);
    [[nodiscard]] bool queueDamage(const AssemblyDamageCommand& command);
    [[nodiscard]] bool step(uint64_t tick);

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] uint64_t currentTick() const noexcept { return currentTick_; }
    [[nodiscard]] uint32_t stateHash() const noexcept { return stateHash_; }
    [[nodiscard]] std::span<const AssemblyNode> nodes() const noexcept {
        return nodes_;
    }
    [[nodiscard]] std::span<const AssemblyEdge> edges() const noexcept {
        return edges_;
    }
    [[nodiscard]] std::span<const AssemblyComponent> components() const noexcept {
        return components_;
    }
    [[nodiscard]] std::span<const AssemblyFractureEvent>
    fractureEvents() const noexcept { return fractureEvents_; }
    [[nodiscard]] std::span<const uint32_t> lastBrokenEdges() const noexcept {
        return lastBrokenEdges_;
    }
    [[nodiscard]] std::span<const AssemblyDamageCommand>
    commandRecording() const noexcept { return commandRecording_; }

    [[nodiscard]] const AssemblyNode* node(uint32_t id) const noexcept;
    [[nodiscard]] const AssemblyEdge* edge(uint32_t id) const noexcept;
    [[nodiscard]] const AssemblyComponent* component(
        uint32_t rootNode) const noexcept;

private:
    [[nodiscard]] bool validate() const noexcept;
    [[nodiscard]] bool rebuildComponents();
    void updateStateHash() noexcept;

    Config config_{};
    std::vector<AssemblyNode> nodes_;
    std::vector<AssemblyEdge> edges_;
    std::vector<AssemblyDamageCommand> pendingDamage_;
    std::vector<AssemblyDamageCommand> commandRecording_;
    std::vector<AssemblyComponent> components_;
    std::vector<AssemblyFractureEvent> fractureEvents_;
    std::vector<uint32_t> lastBrokenEdges_;
    uint64_t currentTick_ = 0;
    uint32_t stateHash_ = 0;
    bool initialized_ = false;
};

} // namespace voxy::gameplay

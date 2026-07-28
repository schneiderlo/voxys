#include "gameplay/structural_assembly.hpp"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::gameplay {
namespace {

bool validNode(const AssemblyNode& node) noexcept {
    if (node.id == 0u || node.massQ16 <= 0
        || node.massQ16 > kMaximumNodeMassQ16
        || node.displacedVolumeQ16 < 0
        || node.displacedVolumeQ16 > kMaximumNodeVolumeQ16) return false;
    return std::all_of(
        node.localPositionQ12.begin(), node.localPositionQ12.end(),
        [](int32_t value) {
            return value >= -kMaximumLocalPositionQ12
                && value <= kMaximumLocalPositionQ12;
        });
}

} // namespace

bool assemblyDamageCommandLess(
    const AssemblyDamageCommand& lhs,
    const AssemblyDamageCommand& rhs) noexcept {
    return std::tie(lhs.tick, lhs.sequence, lhs.source, lhs.edgeId,
                    lhs.damageQ16)
        < std::tie(rhs.tick, rhs.sequence, rhs.source, rhs.edgeId,
                   rhs.damageQ16);
}

StructuralAssembly::StructuralAssembly() : StructuralAssembly(Config{}) {}

StructuralAssembly::StructuralAssembly(Config config) : config_(config) {}

bool StructuralAssembly::initialize(
    std::span<const AssemblyNode> nodes,
    std::span<const AssemblyEdge> edges) {
    StructuralAssembly replacement(config_);
    replacement.nodes_.assign(nodes.begin(), nodes.end());
    replacement.edges_.assign(edges.begin(), edges.end());
    std::sort(replacement.nodes_.begin(), replacement.nodes_.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    std::sort(replacement.edges_.begin(), replacement.edges_.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.id < rhs.id; });
    if (!replacement.validate() || !replacement.rebuildComponents())
        return false;
    replacement.initialized_ = true;
    replacement.updateStateHash();
    *this = std::move(replacement);
    return true;
}

bool StructuralAssembly::validate() const noexcept {
    if (nodes_.empty() || nodes_.size() > config_.maximumNodes
        || edges_.size() > config_.maximumEdges
        || config_.maximumNodes == 0u || config_.maximumEdges == 0u
        || config_.maximumNodes > kMaximumAssemblyNodes
        || config_.maximumEdges > kMaximumAssemblyEdges
        || config_.keelNodeId == 0u || config_.significantMassQ16 <= 0)
        return false;
    if (!std::all_of(nodes_.begin(), nodes_.end(), validNode)) return false;
    if (std::adjacent_find(nodes_.begin(), nodes_.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.id == rhs.id; })
        != nodes_.end()) return false;
    if (node(config_.keelNodeId) == nullptr) return false;

    int64_t totalMass = 0;
    int64_t totalVolume = 0;
    for (const auto& value : nodes_) {
        totalMass += value.massQ16;
        totalVolume += value.displacedVolumeQ16;
    }
    if (totalMass > std::numeric_limits<int32_t>::max()
        || totalVolume > std::numeric_limits<int32_t>::max()) return false;

    uint32_t previousEdge = 0u;
    for (const auto& value : edges_) {
        if (value.id == 0u || value.id == previousEdge || value.nodeA == value.nodeB
            || value.healthQ16 <= 0 || value.healthQ16 > kMaximumImpactQ16
            || node(value.nodeA) == nullptr || node(value.nodeB) == nullptr)
            return false;
        previousEdge = value.id;
    }
    return true;
}

const AssemblyNode* StructuralAssembly::node(uint32_t id) const noexcept {
    const auto iterator = std::lower_bound(
        nodes_.begin(), nodes_.end(), id,
        [](const AssemblyNode& value, uint32_t needle) {
            return value.id < needle;
        });
    return iterator != nodes_.end() && iterator->id == id ? &*iterator : nullptr;
}

const AssemblyEdge* StructuralAssembly::edge(uint32_t id) const noexcept {
    const auto iterator = std::lower_bound(
        edges_.begin(), edges_.end(), id,
        [](const AssemblyEdge& value, uint32_t needle) {
            return value.id < needle;
        });
    return iterator != edges_.end() && iterator->id == id ? &*iterator : nullptr;
}

const AssemblyComponent* StructuralAssembly::component(
    uint32_t rootNode) const noexcept {
    const auto iterator = std::lower_bound(
        components_.begin(), components_.end(), rootNode,
        [](const AssemblyComponent& value, uint32_t needle) {
            return value.rootNode < needle;
        });
    return iterator != components_.end() && iterator->rootNode == rootNode
        ? &*iterator : nullptr;
}

bool StructuralAssembly::queueDamage(const AssemblyDamageCommand& command) {
    if (!initialized_ || command.sequence == 0u || command.source == 0u)
        return false;
    const auto duplicate = std::find_if(
        commandRecording_.begin(), commandRecording_.end(),
        [&command](const auto& existing) {
            return existing.source == command.source
                && existing.sequence == command.sequence;
        });
    if (duplicate != commandRecording_.end()) return *duplicate == command;
    const AssemblyEdge* target = edge(command.edgeId);
    if (command.tick <= currentTick_ || command.damageQ16 <= 0
        || command.damageQ16 > kMaximumImpactQ16
        || target == nullptr || target->healthQ16 <= 0) return false;

    const auto insertPending = std::upper_bound(
        pendingDamage_.begin(), pendingDamage_.end(), command,
        [](const auto& value, const auto& existing) {
            return assemblyDamageCommandLess(value, existing);
        });
    pendingDamage_.insert(insertPending, command);
    const auto insertRecording = std::upper_bound(
        commandRecording_.begin(), commandRecording_.end(), command,
        [](const auto& value, const auto& existing) {
            return assemblyDamageCommandLess(value, existing);
        });
    commandRecording_.insert(insertRecording, command);
    return true;
}

bool StructuralAssembly::rebuildComponents() {
    std::vector<uint32_t> parent(nodes_.size());
    for (size_t index = 0; index < nodes_.size(); ++index)
        parent[index] = static_cast<uint32_t>(index);

    const auto nodeIndex = [this](uint32_t id) -> uint32_t {
        const auto iterator = std::lower_bound(
            nodes_.begin(), nodes_.end(), id,
            [](const AssemblyNode& value, uint32_t needle) {
                return value.id < needle;
            });
        return static_cast<uint32_t>(iterator - nodes_.begin());
    };
    const auto findRoot = [&parent](uint32_t start) {
        uint32_t root = start;
        while (parent[root] != root) root = parent[root];
        uint32_t cursor = start;
        while (parent[cursor] != cursor) {
            const uint32_t next = parent[cursor];
            parent[cursor] = root;
            cursor = next;
        }
        return root;
    };
    for (const auto& value : edges_) {
        if (value.healthQ16 <= 0) continue;
        const uint32_t a = findRoot(nodeIndex(value.nodeA));
        const uint32_t b = findRoot(nodeIndex(value.nodeB));
        if (a == b) continue;
        const uint32_t root = nodes_[a].id < nodes_[b].id ? a : b;
        const uint32_t child = root == a ? b : a;
        parent[child] = root;
    }
    for (uint32_t index = 0; index < parent.size(); ++index)
        parent[index] = findRoot(index);

    components_.clear();
    for (size_t index = 0; index < nodes_.size(); ++index) {
        const uint32_t rootNode = nodes_[parent[index]].id;
        auto iterator = std::lower_bound(
            components_.begin(), components_.end(), rootNode,
            [](const AssemblyComponent& value, uint32_t needle) {
                return value.rootNode < needle;
            });
        if (iterator == components_.end() || iterator->rootNode != rootNode) {
            AssemblyComponent component;
            component.rootNode = rootNode;
            iterator = components_.insert(iterator, std::move(component));
        }
        iterator->nodeIds.push_back(nodes_[index].id);
    }

    for (const auto& value : edges_) {
        if (value.healthQ16 <= 0) continue;
        const uint32_t rootNode = nodes_[parent[nodeIndex(value.nodeA)]].id;
        const auto componentIterator = std::lower_bound(
            components_.begin(), components_.end(), rootNode,
            [](const AssemblyComponent& componentValue, uint32_t needle) {
                return componentValue.rootNode < needle;
            });
        if (componentIterator == components_.end()
            || componentIterator->rootNode != rootNode) return false;
        componentIterator->activeEdgeIds.push_back(value.id);
    }

    for (auto& value : components_) {
        int64_t mass = 0;
        int64_t volume = 0;
        std::array<int64_t, 3> weightedPosition{};
        for (const uint32_t id : value.nodeIds) {
            const AssemblyNode* valueNode = node(id);
            if (valueNode == nullptr) return false;
            mass += valueNode->massQ16;
            volume += valueNode->displacedVolumeQ16;
            for (size_t axis = 0; axis < weightedPosition.size(); ++axis) {
                weightedPosition[axis] += int64_t{valueNode->massQ16}
                    * int64_t{valueNode->localPositionQ12[axis]};
            }
        }
        if (mass <= 0 || mass > std::numeric_limits<int32_t>::max()
            || volume > std::numeric_limits<int32_t>::max()) return false;
        value.massQ16 = static_cast<int32_t>(mass);
        value.displacedVolumeQ16 = static_cast<int32_t>(volume);
        for (size_t axis = 0; axis < weightedPosition.size(); ++axis) {
            value.centerOfMassQ12[axis] = saturateI32(
                roundedDivide(weightedPosition[axis], mass));
        }
        value.containsKeel = std::binary_search(
            value.nodeIds.begin(), value.nodeIds.end(), config_.keelNodeId);
        value.significant = !value.containsKeel
            && value.massQ16 >= config_.significantMassQ16;
        value.tinyDebris = !value.containsKeel && !value.significant;
    }
    return true;
}

bool StructuralAssembly::step(uint64_t tick) {
    if (!initialized_ || tick != currentTick_ + 1u) return false;
    lastBrokenEdges_.clear();
    const auto end = std::upper_bound(
        pendingDamage_.begin(), pendingDamage_.end(), tick,
        [](uint64_t value, const AssemblyDamageCommand& command) {
            return value < command.tick;
        });
    for (auto iterator = pendingDamage_.begin(); iterator != end; ++iterator) {
        auto edgeIterator = std::lower_bound(
            edges_.begin(), edges_.end(), iterator->edgeId,
            [](const AssemblyEdge& value, uint32_t needle) {
                return value.id < needle;
            });
        if (edgeIterator == edges_.end() || edgeIterator->id != iterator->edgeId)
            return false;
        if (edgeIterator->healthQ16 <= 0) continue;
        edgeIterator->healthQ16 = std::max(
            0, saturateI32(int64_t{edgeIterator->healthQ16}
                           - iterator->damageQ16));
        if (edgeIterator->healthQ16 == 0)
            lastBrokenEdges_.push_back(edgeIterator->id);
    }
    pendingDamage_.erase(pendingDamage_.begin(), end);
    std::sort(lastBrokenEdges_.begin(), lastBrokenEdges_.end());
    currentTick_ = tick;
    if (!lastBrokenEdges_.empty() && !rebuildComponents()) return false;
    updateStateHash();
    if (!lastBrokenEdges_.empty()) {
        AssemblyFractureEvent event;
        event.tick = tick;
        event.brokenEdgeIds = lastBrokenEdges_;
        event.stateHash = stateHash_;
        event.componentRoots.reserve(components_.size());
        for (const auto& value : components_)
            event.componentRoots.push_back(value.rootNode);
        fractureEvents_.push_back(std::move(event));
    }
    return true;
}

void StructuralAssembly::updateStateHash() noexcept {
    uint32_t hash = gameplayHashWord(kGameplayHashOffset,
                                     kGameplaySchemaVersion);
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_));
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_ >> 32u));
    hash = gameplayHashWord(hash, config_.keelNodeId);
    hash = gameplayHashI32(hash, config_.significantMassQ16);
    for (const auto& value : nodes_) {
        hash = gameplayHashWord(hash, value.id);
        for (const int32_t position : value.localPositionQ12)
            hash = gameplayHashI32(hash, position);
        hash = gameplayHashI32(hash, value.massQ16);
        hash = gameplayHashI32(hash, value.displacedVolumeQ16);
        hash = gameplayHashWord(hash, value.flags);
    }
    for (const auto& value : edges_) {
        hash = gameplayHashWord(hash, value.id);
        hash = gameplayHashWord(hash, value.nodeA);
        hash = gameplayHashWord(hash, value.nodeB);
        hash = gameplayHashI32(hash, value.healthQ16);
        hash = gameplayHashWord(hash, value.material);
    }
    for (const auto& value : components_) {
        hash = gameplayHashWord(hash, value.rootNode);
        hash = gameplayHashI32(hash, value.massQ16);
        hash = gameplayHashI32(hash, value.displacedVolumeQ16);
        for (const int32_t position : value.centerOfMassQ12)
            hash = gameplayHashI32(hash, position);
        hash = gameplayHashWord(hash, value.containsKeel ? 1u : 0u);
        hash = gameplayHashWord(hash, value.significant ? 1u : 0u);
        hash = gameplayHashWord(hash, value.tinyDebris ? 1u : 0u);
    }
    stateHash_ = hash;
}

} // namespace voxy::gameplay

#include "gameplay/vertical_slices.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::gameplay {
namespace {

constexpr int32_t q16(int32_t whole, int32_t numerator = 0,
                      int32_t denominator = 1) noexcept {
    return whole * kScalarOne
        + static_cast<int32_t>(roundedDivide(
            int64_t{numerator} * kScalarOne, denominator));
}

constexpr int32_t q12(int32_t whole, int32_t numerator = 0,
                      int32_t denominator = 1) noexcept {
    return whole * kPositionOne
        + static_cast<int32_t>(roundedDivide(
            int64_t{numerator} * kPositionOne, denominator));
}

std::array<AssemblyNode, 6> demolitionNodes() {
    return {{
        {1u, {0, q12(0), 0}, q16(10), 0, AssemblyNodeFoundation},
        {2u, {0, q12(1), 0}, q16(4), 0, 0u},
        {3u, {0, q12(2), 0}, q16(3), 0, 0u},
        {4u, {0, q12(3), 0}, q16(3), 0, 0u},
        {5u, {q12(1), q12(2), 0}, q16(2), 0, 0u},
        {6u, {-q12(1), q12(2), 0}, q16(2), 0, 0u},
    }};
}

std::array<AssemblyEdge, 5> demolitionEdges() {
    return {{
        {100u, 1u, 2u, q16(4), 1u},
        {101u, 2u, 3u, q16(4), 1u},
        {102u, 3u, 4u, q16(3), 1u},
        {103u, 3u, 5u, q16(2), 2u},
        {104u, 3u, 6u, q16(2), 2u},
    }};
}

std::array<AssemblyNode, 6> wreckwaterNodes() {
    return {{
        {1u, {0, -q12(0, 1, 2), 0}, q16(4), q16(6), AssemblyNodeSealed},
        {2u, {-q12(1), -q12(0, 1, 4), 0}, q16(2), q16(3), AssemblyNodeSealed},
        {3u, {q12(1), -q12(0, 1, 4), 0}, q16(2), q16(3), AssemblyNodeSealed},
        {4u, {0, 0, q12(2)}, q16(2, 1, 2), q16(3, 1, 2), AssemblyNodeSealed},
        {5u, {0, 0, -q12(2)}, q16(2, 1, 2), q16(3, 1, 2), AssemblyNodeSealed},
        {6u, {0, q12(1, 1, 2), 0}, q16(0, 1, 4), q16(0, 1, 8), 0u},
    }};
}

std::array<AssemblyEdge, 9> wreckwaterEdges() {
    return {{
        {10u, 1u, 2u, q16(3), 1u},
        {11u, 1u, 3u, q16(3), 1u},
        {12u, 1u, 4u, q16(3), 2u},
        {13u, 1u, 5u, q16(3), 2u},
        {14u, 2u, 4u, q16(2), 1u},
        {15u, 3u, 4u, q16(2), 1u},
        {16u, 2u, 5u, q16(2), 1u},
        {17u, 3u, 5u, q16(2), 1u},
        {18u, 1u, 6u, q16(1), 3u},
    }};
}

template <typename Fragment>
const Fragment* priorFragmentFor(
    std::span<const Fragment> fragments, uint32_t nodeId) noexcept {
    const auto iterator = std::find_if(
        fragments.begin(), fragments.end(), [nodeId](const Fragment& value) {
            return std::binary_search(
                value.nodeIds.begin(), value.nodeIds.end(), nodeId);
        });
    return iterator == fragments.end() ? nullptr : &*iterator;
}

int32_t transportWindQ16(uint64_t tick) noexcept {
    const uint32_t phase = static_cast<uint32_t>(tick % 120u);
    const uint32_t rising = phase <= 60u ? phase : 120u - phase;
    return saturateI32(-int64_t{kScalarOne}
        + roundedDivide(int64_t{rising} * 2 * kScalarOne, 60));
}

uint32_t productIndex(ProductCandidate candidate) noexcept {
    return static_cast<uint32_t>(candidate);
}

} // namespace

DemolitionLeagueSlice::DemolitionLeagueSlice()
    : DemolitionLeagueSlice(Config{}) {}

DemolitionLeagueSlice::DemolitionLeagueSlice(Config config)
    : config_(config), building_(StructuralAssembly::Config{
          .maximumNodes = 16,
          .maximumEdges = 32,
          .keelNodeId = 1,
          .significantMassQ16 = config.significantMassQ16,
      }) {}

bool DemolitionLeagueSlice::initialize() {
    initialized_ = false;
    if (config_.roundTicks == 0u
        || config_.roundTicks
            > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
        || config_.significantMassQ16 <= 0)
        return false;
    const auto nodes = demolitionNodes();
    const auto edges = demolitionEdges();
    if (!building_.initialize(nodes, edges)) return false;
    fragments_.clear();
    scoredNodes_.clear();
    currentTick_ = 0;
    scoreQ16_ = 0;
    stateHash_ = 0;
    telemetry_ = {};
    initialized_ = true;
    updateHash();
    return true;
}

bool DemolitionLeagueSlice::submitImpact(
    const AssemblyDamageCommand& impact) {
    if (!initialized_ || roundComplete() || !building_.queueDamage(impact)) {
        ++telemetry_.rejectedImpacts;
        return false;
    }
    ++telemetry_.acceptedImpacts;
    return true;
}

void DemolitionLeagueSlice::synchronizeFragments() {
    const std::vector<DemolitionFragmentState> prior = std::move(fragments_);
    fragments_.clear();
    for (const auto& component : building_.components()) {
        if (component.containsKeel) continue;
        DemolitionFragmentState next;
        next.rootNode = component.rootNode;
        next.nodeIds = component.nodeIds;
        if (const auto* old = priorFragmentFor<DemolitionFragmentState>(
                prior, component.nodeIds.front()); old != nullptr) {
            next.verticalOffsetQ12 = old->verticalOffsetQ12;
            next.verticalVelocityQ16 = old->verticalVelocityQ16;
            next.grounded = old->grounded;
        }
        fragments_.push_back(std::move(next));
    }
}

void DemolitionLeagueSlice::awardNewDetachments() {
    int64_t newMass = 0;
    for (const auto& component : building_.components()) {
        if (component.containsKeel) continue;
        for (const uint32_t nodeId : component.nodeIds) {
            const auto scored = std::lower_bound(
                scoredNodes_.begin(), scoredNodes_.end(), nodeId);
            if (scored != scoredNodes_.end() && *scored == nodeId) continue;
            const AssemblyNode* value = building_.node(nodeId);
            if (value != nullptr) newMass += value->massQ16;
            scoredNodes_.insert(scored, nodeId);
        }
    }
    if (newMass == 0) return;
    const int32_t timeBonus = saturateI32(int64_t{kScalarOne}
        + roundedDivide(
            static_cast<int64_t>(config_.roundTicks - currentTick_)
                * kScalarOne,
            static_cast<int64_t>(config_.roundTicks)));
    scoreQ16_ = saturateI32(int64_t{scoreQ16_}
        + roundedDivide(newMass * timeBonus, kScalarOne));
}

void DemolitionLeagueSlice::integrateFragments() {
    for (auto& fragment : fragments_) {
        const AssemblyComponent* component = building_.component(fragment.rootNode);
        if (component == nullptr || fragment.grounded) continue;
        fragment.verticalVelocityQ16 = saturateI32(
            int64_t{fragment.verticalVelocityQ16}
            - roundedDivide(kGravityQ16, kGameplayTickRateHz));
        fragment.verticalOffsetQ12 = saturateI32(
            int64_t{fragment.verticalOffsetQ12}
            + roundedDivide(fragment.verticalVelocityQ16,
                            int64_t{kGameplayTickRateHz} * 16));
        const int64_t centerHeight = int64_t{component->centerOfMassQ12[1]}
            + fragment.verticalOffsetQ12;
        if (centerHeight <= 0) {
            fragment.verticalOffsetQ12 = -component->centerOfMassQ12[1];
            fragment.verticalVelocityQ16 = 0;
            fragment.grounded = true;
        }
    }
}

bool DemolitionLeagueSlice::step() {
    if (!initialized_ || roundComplete()) return false;
    const uint64_t nextTick = currentTick_ + 1u;
    if (!building_.step(nextTick)) return false;
    currentTick_ = nextTick;
    if (!building_.lastBrokenEdges().empty()) {
        ++telemetry_.fractureEvents;
        synchronizeFragments();
        awardNewDetachments();
    }
    integrateFragments();
    telemetry_.detachedBodies = static_cast<uint32_t>(fragments_.size());
    updateHash();
    return true;
}

void DemolitionLeagueSlice::updateHash() noexcept {
    uint32_t hash = gameplayHashWord(kGameplayHashOffset,
                                     kGameplaySchemaVersion);
    hash = gameplayHashWord(hash, building_.stateHash());
    hash = gameplayHashWord(hash, static_cast<uint32_t>(config_.roundTicks));
    hash = gameplayHashWord(hash,
                            static_cast<uint32_t>(config_.roundTicks >> 32u));
    hash = gameplayHashI32(hash, config_.significantMassQ16);
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_));
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_ >> 32u));
    hash = gameplayHashI32(hash, scoreQ16_);
    for (const auto& fragment : fragments_) {
        hash = gameplayHashWord(hash, fragment.rootNode);
        hash = gameplayHashI32(hash, fragment.verticalOffsetQ12);
        hash = gameplayHashI32(hash, fragment.verticalVelocityQ16);
        hash = gameplayHashWord(hash, fragment.grounded ? 1u : 0u);
    }
    stateHash_ = hash;
    telemetry_.stateHash = hash;
}

bool deadweightInputLess(
    const DeadweightInput& lhs, const DeadweightInput& rhs) noexcept {
    return std::tie(lhs.tick, lhs.clientId, lhs.sequence, lhs.pullXQ16,
                    lhs.pullZQ16, lhs.flags)
        < std::tie(rhs.tick, rhs.clientId, rhs.sequence, rhs.pullXQ16,
                   rhs.pullZQ16, rhs.flags);
}

DeadweightSlice::DeadweightSlice() : DeadweightSlice(Config{}) {}

DeadweightSlice::DeadweightSlice(Config config) : config_(config) {}

bool DeadweightSlice::initialize() {
    initialized_ = false;
    if (config_.roundTicks == 0u || config_.inputFutureWindow == 0u
        || config_.historyTicks == 0u || config_.cargoMassQ16 <= 0
        || config_.winchForceQ16 <= 0 || config_.objectiveZQ12 <= 0
        || config_.deliveryHalfWidthQ12 <= 0
        || config_.lossHalfWidthQ12 <= config_.deliveryHalfWidthQ12)
        return false;
    pending_.clear();
    recording_.clear();
    history_.clear();
    state_ = {};
    telemetry_ = {};
    initialized_ = true;
    updateHash();
    storeHistory();
    return true;
}

bool DeadweightSlice::validate(const DeadweightInput& input) const noexcept {
    if (!initialized_ || state_.outcome != TransportOutcome::Running
        || input.tick <= state_.tick
        || input.tick > state_.tick + config_.inputFutureWindow
        || input.sequence == 0u || (input.clientId != 1u && input.clientId != 2u)
        || input.pullXQ16 < -kScalarOne || input.pullXQ16 > kScalarOne
        || input.pullZQ16 < -kScalarOne || input.pullZQ16 > kScalarOne
        || (input.flags & ~TransportInputBrake) != 0u) return false;
    const int64_t effort = (input.pullXQ16 < 0
        ? -int64_t{input.pullXQ16} : int64_t{input.pullXQ16})
        + (input.pullZQ16 < 0
            ? -int64_t{input.pullZQ16} : int64_t{input.pullZQ16});
    return effort <= int64_t{kScalarOne} + kScalarOne / 2;
}

bool DeadweightSlice::submitInput(const DeadweightInput& input) {
    const auto sameSequence = std::find_if(
        recording_.begin(), recording_.end(), [&input](const auto& existing) {
            return existing.clientId == input.clientId
                && existing.sequence == input.sequence;
        });
    if (sameSequence != recording_.end()) {
        ++telemetry_.duplicateInputs;
        if (*sameSequence == input) return true;
        ++telemetry_.rejectedInputs;
        return false;
    }
    if (!validate(input)) {
        ++telemetry_.rejectedInputs;
        return false;
    }
    if (std::any_of(pending_.begin(), pending_.end(),
            [&input](const auto& existing) {
                return existing.clientId == input.clientId
                    && existing.tick == input.tick;
            })) {
        ++telemetry_.rejectedInputs;
        return false;
    }
    const auto pendingPosition = std::upper_bound(
        pending_.begin(), pending_.end(), input,
        [](const auto& value, const auto& existing) {
            return deadweightInputLess(value, existing);
        });
    pending_.insert(pendingPosition, input);
    const auto recordingPosition = std::upper_bound(
        recording_.begin(), recording_.end(), input,
        [](const auto& value, const auto& existing) {
            return deadweightInputLess(value, existing);
        });
    recording_.insert(recordingPosition, input);
    ++telemetry_.acceptedInputs;
    return true;
}

bool DeadweightSlice::submitRedundantInputs(
    std::span<const DeadweightInput> inputs) {
    bool allAccepted = true;
    for (const auto& input : inputs)
        allAccepted = submitInput(input) && allAccepted;
    return allAccepted;
}

void DeadweightSlice::storeHistory() {
    if (history_.size() == config_.historyTicks) {
        history_.erase(history_.begin());
        ++telemetry_.historyEvictions;
    }
    history_.push_back(state_);
}

bool DeadweightSlice::step() {
    if (!initialized_ || state_.outcome != TransportOutcome::Running)
        return false;
    const uint64_t tick = state_.tick + 1u;
    std::array<std::optional<DeadweightInput>, 2> inputs;
    const auto end = std::upper_bound(
        pending_.begin(), pending_.end(), tick,
        [](uint64_t value, const DeadweightInput& input) {
            return value < input.tick;
        });
    for (auto iterator = pending_.begin(); iterator != end; ++iterator) {
        if (iterator->tick != tick) continue;
        inputs.at(iterator->clientId - 1u) = *iterator;
    }
    pending_.erase(pending_.begin(), end);

    int32_t forceXQ16 = 0;
    int32_t forceZQ16 = 0;
    bool braking = false;
    for (const auto& input : inputs) {
        if (!input.has_value()) {
            ++telemetry_.missingClientTicks;
            continue;
        }
        forceXQ16 = saturateI32(int64_t{forceXQ16}
            + multiplyQ16(input->pullXQ16, config_.winchForceQ16));
        forceZQ16 = saturateI32(int64_t{forceZQ16}
            + multiplyQ16(input->pullZQ16, config_.winchForceQ16));
        braking = braking || (input->flags & TransportInputBrake) != 0u;
    }

    const int32_t z = state_.cargoPositionQ12[1];
    const bool inMud = z >= 3 * kPositionOne && z < 5 * kPositionOne;
    const bool inWind = z >= 6 * kPositionOne && z < 9 * kPositionOne;
    if (inWind) {
        forceXQ16 = saturateI32(int64_t{forceXQ16}
            + multiplyQ16(transportWindQ16(tick), 2 * kScalarOne));
    }
    if (inMud || inWind) ++telemetry_.hazardTicks;

    const int32_t accelerationXQ16 = ratioQ16(
        forceXQ16, config_.cargoMassQ16);
    const int32_t accelerationZQ16 = ratioQ16(
        forceZQ16, config_.cargoMassQ16);
    state_.cargoVelocityQ16[0] = saturateI32(
        int64_t{state_.cargoVelocityQ16[0]}
        + roundedDivide(accelerationXQ16, kGameplayTickRateHz));
    state_.cargoVelocityQ16[1] = saturateI32(
        int64_t{state_.cargoVelocityQ16[1]}
        + roundedDivide(accelerationZQ16, kGameplayTickRateHz));

    int32_t dampingQ16 = inMud ? q16(0, 15, 16) : q16(0, 255, 256);
    if (braking) dampingQ16 = q16(0, 7, 8);
    for (auto& velocity : state_.cargoVelocityQ16)
        velocity = multiplyQ16(velocity, dampingQ16);

    const int32_t firstPull = inputs[0].has_value() ? inputs[0]->pullZQ16 : 0;
    const int32_t secondPull = inputs[1].has_value() ? inputs[1]->pullZQ16 : 0;
    const int32_t yawAcceleration = multiplyQ16(
        saturateI32(int64_t{firstPull} - secondPull), kScalarOne / 2);
    state_.cargoYawVelocityQ16 = multiplyQ16(
        saturateI32(int64_t{state_.cargoYawVelocityQ16}
            + roundedDivide(yawAcceleration, kGameplayTickRateHz)),
        q16(0, 63, 64));
    state_.cargoYawQ16 = saturateI32(int64_t{state_.cargoYawQ16}
        + roundedDivide(state_.cargoYawVelocityQ16, kGameplayTickRateHz));

    for (size_t axis = 0; axis < state_.cargoPositionQ12.size(); ++axis) {
        state_.cargoPositionQ12[axis] = saturateI32(
            int64_t{state_.cargoPositionQ12[axis]}
            + roundedDivide(state_.cargoVelocityQ16[axis],
                            int64_t{kGameplayTickRateHz} * 16));
    }
    state_.tick = tick;

    const int64_t absoluteX = state_.cargoPositionQ12[0] < 0
        ? -int64_t{state_.cargoPositionQ12[0]}
        : int64_t{state_.cargoPositionQ12[0]};
    if (absoluteX > config_.lossHalfWidthQ12) {
        state_.outcome = TransportOutcome::Lost;
    } else if (state_.cargoPositionQ12[1] >= config_.objectiveZQ12
               && absoluteX <= config_.deliveryHalfWidthQ12) {
        state_.outcome = TransportOutcome::Delivered;
    } else if (tick >= config_.roundTicks) {
        state_.outcome = TransportOutcome::TimedOut;
    }
    updateHash();
    storeHistory();
    return true;
}

void DeadweightSlice::updateHash() noexcept {
    uint32_t hash = gameplayHashWord(kGameplayHashOffset,
                                     kGameplaySchemaVersion);
    hash = gameplayHashWord(hash, static_cast<uint32_t>(state_.tick));
    hash = gameplayHashWord(hash, static_cast<uint32_t>(state_.tick >> 32u));
    hash = gameplayHashWord(hash, static_cast<uint32_t>(config_.roundTicks));
    hash = gameplayHashWord(hash,
                            static_cast<uint32_t>(config_.roundTicks >> 32u));
    hash = gameplayHashWord(hash, config_.inputFutureWindow);
    hash = gameplayHashWord(hash, config_.historyTicks);
    hash = gameplayHashI32(hash, config_.cargoMassQ16);
    hash = gameplayHashI32(hash, config_.winchForceQ16);
    hash = gameplayHashI32(hash, config_.objectiveZQ12);
    hash = gameplayHashI32(hash, config_.deliveryHalfWidthQ12);
    hash = gameplayHashI32(hash, config_.lossHalfWidthQ12);
    for (const int32_t value : state_.cargoPositionQ12)
        hash = gameplayHashI32(hash, value);
    for (const int32_t value : state_.cargoVelocityQ16)
        hash = gameplayHashI32(hash, value);
    hash = gameplayHashI32(hash, state_.cargoYawQ16);
    hash = gameplayHashI32(hash, state_.cargoYawVelocityQ16);
    hash = gameplayHashWord(hash, static_cast<uint32_t>(state_.outcome));
    state_.stateHash = hash;
}

WreckwaterSlice::WreckwaterSlice() : WreckwaterSlice(Config{}) {}

WreckwaterSlice::WreckwaterSlice(Config config)
    : config_(config), water_(config.water),
      assembly_(StructuralAssembly::Config{
          .maximumNodes = 32,
          .maximumEdges = 64,
          .keelNodeId = 1,
          .significantMassQ16 = config.significantMassQ16,
      }) {}

bool WreckwaterSlice::initialize() {
    initialized_ = false;
    if (config_.significantMassQ16 <= 0
        || config_.floodRatePerTickQ16 <= 0
        || config_.floodRatePerTickQ16 > kScalarOne
        || config_.dragCoefficientQ16 < 0 || !water_.valid()) return false;
    const auto nodes = wreckwaterNodes();
    const auto edges = wreckwaterEdges();
    if (!assembly_.initialize(nodes, edges)) return false;
    fragments_.clear();
    flooding_.clear();
    currentTick_ = 0;
    stateHash_ = 0;
    telemetry_ = {};
    initialized_ = true;
    synchronizeFragments();
    updateHash();
    return true;
}

bool WreckwaterSlice::submitImpact(const AssemblyDamageCommand& impact) {
    if (!initialized_ || !assembly_.queueDamage(impact)) {
        ++telemetry_.rejectedImpacts;
        return false;
    }
    ++telemetry_.acceptedImpacts;
    return true;
}

void WreckwaterSlice::addBreach(uint32_t nodeId) {
    const AssemblyNode* valueNode = assembly_.node(nodeId);
    if (valueNode == nullptr
        || (valueNode->flags & AssemblyNodeSealed) == 0u) return;
    const auto iterator = std::lower_bound(
        flooding_.begin(), flooding_.end(), nodeId,
        [](const FloodedCompartment& value, uint32_t needle) {
            return value.nodeId < needle;
        });
    if (iterator == flooding_.end() || iterator->nodeId != nodeId)
        flooding_.insert(iterator, FloodedCompartment{.nodeId = nodeId});
}

int32_t WreckwaterSlice::floodedFraction(uint32_t nodeId) const noexcept {
    const auto iterator = std::lower_bound(
        flooding_.begin(), flooding_.end(), nodeId,
        [](const FloodedCompartment& value, uint32_t needle) {
            return value.nodeId < needle;
        });
    return iterator != flooding_.end() && iterator->nodeId == nodeId
        ? iterator->floodedFractionQ16 : 0;
}

void WreckwaterSlice::advanceFlooding() {
    for (auto& value : flooding_) {
        value.floodedFractionQ16 = std::min(
            kScalarOne, saturateI32(int64_t{value.floodedFractionQ16}
                + config_.floodRatePerTickQ16));
        ++telemetry_.floodedNodeTicks;
    }
}

void WreckwaterSlice::synchronizeFragments() {
    const std::vector<HullFragmentState> prior = std::move(fragments_);
    fragments_.clear();
    telemetry_.significantFragments = 0;
    telemetry_.tinyDebrisFragments = 0;
    for (const auto& component : assembly_.components()) {
        HullFragmentState next;
        next.rootNode = component.rootNode;
        next.nodeIds = component.nodeIds;
        if (const auto* old = priorFragmentFor<HullFragmentState>(
                prior, component.nodeIds.front()); old != nullptr) {
            next.verticalOffsetQ12 = old->verticalOffsetQ12;
            next.verticalVelocityQ16 = old->verticalVelocityQ16;
            next.buoyancy = old->buoyancy;
        }
        fragments_.push_back(std::move(next));
        if (component.significant) ++telemetry_.significantFragments;
        if (component.tinyDebris) ++telemetry_.tinyDebrisFragments;
    }
}

void WreckwaterSlice::integrateFragments() {
    for (auto& fragment : fragments_) {
        const AssemblyComponent* component = assembly_.component(fragment.rootNode);
        if (component == nullptr) continue;
        std::vector<BuoyancyPointQ> points;
        points.reserve(component->nodeIds.size());
        for (const uint32_t nodeId : component->nodeIds) {
            const AssemblyNode* value = assembly_.node(nodeId);
            if (value == nullptr) continue;
            points.push_back(BuoyancyPointQ{
                .localPositionQ12 = value->localPositionQ12,
                .displacedVolumeQ16 = value->displacedVolumeQ16,
                .floodedFractionQ16 = floodedFraction(nodeId),
            });
        }
        fragment.buoyancy = evaluateBuoyancy(
            points,
            BuoyancyInputQ{
                .worldVerticalOffsetQ12 = fragment.verticalOffsetQ12,
                .verticalVelocityQ16 = fragment.verticalVelocityQ16,
                .massQ16 = component->massQ16,
                .dragCoefficientQ16 = config_.dragCoefficientQ16,
            },
            water_, currentTick_);
        fragment.verticalVelocityQ16 = std::clamp(
            saturateI32(int64_t{fragment.verticalVelocityQ16}
                + roundedDivide(fragment.buoyancy.accelerationQ16,
                                kGameplayTickRateHz)),
            -20 * kScalarOne, 20 * kScalarOne);
        fragment.verticalOffsetQ12 = std::clamp(
            saturateI32(int64_t{fragment.verticalOffsetQ12}
                + roundedDivide(fragment.verticalVelocityQ16,
                                int64_t{kGameplayTickRateHz} * 16)),
            -64 * kPositionOne, 64 * kPositionOne);
    }
}

bool WreckwaterSlice::step() {
    if (!initialized_) return false;
    const uint64_t nextTick = currentTick_ + 1u;
    if (!assembly_.step(nextTick)) return false;
    currentTick_ = nextTick;
    if (!assembly_.lastBrokenEdges().empty()) {
        ++telemetry_.fractureEvents;
        for (const uint32_t edgeId : assembly_.lastBrokenEdges()) {
            const AssemblyEdge* value = assembly_.edge(edgeId);
            if (value == nullptr) return false;
            if (value->material != 3u) {
                addBreach(value->nodeA);
                addBreach(value->nodeB);
            }
        }
        synchronizeFragments();
    }
    advanceFlooding();
    integrateFragments();
    updateHash();
    return true;
}

void WreckwaterSlice::updateHash() noexcept {
    uint32_t hash = gameplayHashWord(kGameplayHashOffset,
                                     kGameplaySchemaVersion);
    hash = gameplayHashWord(hash, assembly_.stateHash());
    hash = gameplayHashWord(hash, water_.configHash());
    hash = gameplayHashI32(hash, config_.significantMassQ16);
    hash = gameplayHashI32(hash, config_.floodRatePerTickQ16);
    hash = gameplayHashI32(hash, config_.dragCoefficientQ16);
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_));
    hash = gameplayHashWord(hash, static_cast<uint32_t>(currentTick_ >> 32u));
    for (const auto& fragment : fragments_) {
        hash = gameplayHashWord(hash, fragment.rootNode);
        hash = gameplayHashI32(hash, fragment.verticalOffsetQ12);
        hash = gameplayHashI32(hash, fragment.verticalVelocityQ16);
        hash = gameplayHashI32(hash, fragment.buoyancy.accelerationQ16);
        hash = gameplayHashI32(hash, fragment.buoyancy.submergedVolumeQ16);
    }
    for (const auto& value : flooding_) {
        hash = gameplayHashWord(hash, value.nodeId);
        hash = gameplayHashI32(hash, value.floodedFractionQ16);
    }
    stateHash_ = hash;
    telemetry_.stateHash = hash;
}

PlaytestLedger::PlaytestLedger() : PlaytestLedger(Config{}) {}

PlaytestLedger::PlaytestLedger(Config config) : config_(config) {
    config_.minimumSessionsPerCandidate = std::max(
        config_.minimumSessionsPerCandidate, 1u);
    config_.minimumDurationMinutes = std::max(
        config_.minimumDurationMinutes, 1u);
    config_.maximumCrashRatePercent = std::min(
        config_.maximumCrashRatePercent, 100u);
    config_.minimumWinningMargin = std::max(
        config_.minimumWinningMargin, 0);
}

bool PlaytestLedger::record(const PlaytestObservation& observation) {
    if (!observation.humanVerified || observation.sessionId == 0u
        || productIndex(observation.candidate) >= 3u
        || observation.participants == 0u || observation.participants > 64u
        || observation.durationMinutes == 0u
        || observation.durationMinutes > 720u
        || observation.funRating == 0u || observation.funRating > 5u
        || observation.blockerCount > 100u
        || observation.supportMinutes > 10'000u) return false;
    const auto iterator = std::lower_bound(
        observations_.begin(), observations_.end(), observation.sessionId,
        [](const PlaytestObservation& value, uint64_t sessionId) {
            return value.sessionId < sessionId;
        });
    if (iterator != observations_.end()
        && iterator->sessionId == observation.sessionId) return false;
    observations_.insert(iterator, observation);
    return true;
}

ProductDecision PlaytestLedger::decision() const noexcept {
    ProductDecision result;
    std::array<int64_t, 3> scoreSums{};
    std::array<uint32_t, 3> crashes{};
    for (const auto& observation : observations_) {
        if (observation.durationMinutes < config_.minimumDurationMinutes)
            continue;
        const uint32_t index = productIndex(observation.candidate);
        ++result.qualifyingSessions[index];
        crashes[index] += observation.crashed ? 1u : 0u;
        scoreSums[index] += int64_t{observation.funRating} * 100
            + (observation.completed ? 50 : 0)
            - int64_t{observation.blockerCount} * 50
            - int64_t{observation.supportMinutes} * 2
            - (observation.crashed ? 250 : 0);
    }
    if (std::any_of(
            result.qualifyingSessions.begin(),
            result.qualifyingSessions.end(),
            [this](uint32_t count) {
                return count < config_.minimumSessionsPerCandidate;
            })) return result;

    std::array<bool, 3> operational{};
    uint32_t operationalCount = 0;
    for (size_t index = 0; index < result.evidenceScores.size(); ++index) {
        result.evidenceScores[index] = saturateI32(roundedDivide(
            scoreSums[index], result.qualifyingSessions[index]));
        const int64_t crashPercent = roundedDivide(
            int64_t{crashes[index]} * 100,
            result.qualifyingSessions[index]);
        operational[index] = crashPercent
            <= static_cast<int64_t>(config_.maximumCrashRatePercent);
        operationalCount += operational[index] ? 1u : 0u;
    }
    if (operationalCount == 0u) {
        result.status = ProductDecisionStatus::OperationalFailure;
        return result;
    }

    std::optional<size_t> winner;
    std::optional<size_t> runnerUp;
    for (size_t index = 0; index < operational.size(); ++index) {
        if (!operational[index]) continue;
        if (!winner.has_value()
            || result.evidenceScores[index]
                > result.evidenceScores[*winner]) {
            runnerUp = winner;
            winner = index;
        } else if (!runnerUp.has_value()
                   || result.evidenceScores[index]
                       > result.evidenceScores[*runnerUp]) {
            runnerUp = index;
        }
    }
    if (!winner.has_value()) {
        result.status = ProductDecisionStatus::OperationalFailure;
        return result;
    }
    if (runnerUp.has_value()
        && int64_t{result.evidenceScores[*winner]}
            - result.evidenceScores[*runnerUp]
            < config_.minimumWinningMargin) {
        result.status = ProductDecisionStatus::Tie;
        return result;
    }
    result.status = ProductDecisionStatus::Ready;
    result.candidate = static_cast<ProductCandidate>(*winner);
    return result;
}

} // namespace voxy::gameplay

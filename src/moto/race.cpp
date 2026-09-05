#include "moto/race.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace voxy::moto {
namespace {

[[nodiscard]] bool finite(glm::vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] float lengthSquared(glm::vec3 value) noexcept {
    return glm::dot(value, value);
}

[[nodiscard]] glm::vec3 horizontal(glm::vec3 value) noexcept {
    value.y = 0.0f;
    return value;
}

[[nodiscard]] bool segmentCrossesCheckpoint(
    glm::vec3 from, glm::vec3 to, const RaceCheckpoint& checkpoint) noexcept {
    const float fromPlane = glm::dot(from - checkpoint.center,
                                     checkpoint.forward);
    const float toPlane = glm::dot(to - checkpoint.center,
                                   checkpoint.forward);
    // Count the segment that reaches the plane. Requiring a gap on both
    // sides loses crossings split across ticks at (or very near) the plane.
    if (fromPlane >= 0.0f || toPlane < 0.0f) return false;

    const float denominator = fromPlane - toPlane;
    const float t = fromPlane / denominator;
    if (t < 0.0f || t > 1.0f) return false;

    const glm::vec3 crossing = from + (to - from) * t;
    const glm::vec3 offset = crossing - checkpoint.center;
    const glm::vec3 right{-checkpoint.forward.z, 0.0f,
                           checkpoint.forward.x};
    return std::abs(glm::dot(offset, right)) <= checkpoint.halfWidth
        && std::abs(offset.y) <= checkpoint.halfHeight;
}

[[nodiscard]] uint32_t trickScore(RaceTrick trick) noexcept {
    switch (trick) {
    case RaceTrick::Whip: return 500u;
    case RaceTrick::NoHander: return 750u;
    case RaceTrick::NacNac: return 1'000u;
    case RaceTrick::Superman: return 1'500u;
    case RaceTrick::Backflip: return 2'500u;
    case RaceTrick::Frontflip: return 3'000u;
    case RaceTrick::DoubleBackflip: return 6'500u;
    case RaceTrick::BarrelRoll: return 2'000u;
    }
    return 0u;
}

void rejectTrick(RaceRiderState& state) noexcept {
    if (state.rejectedTrickEvents != std::numeric_limits<uint32_t>::max()) {
        ++state.rejectedTrickEvents;
    }
}

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

} // namespace

bool buildDirectedRaceCheckpoints(
    std::span<const glm::vec2> route, float halfWidth, float halfHeight,
    std::vector<RaceCheckpoint>* checkpoints, std::string* error) {
    if (checkpoints == nullptr || !std::isfinite(halfWidth)
        || !std::isfinite(halfHeight) || halfWidth < 1.0f
        || halfWidth > 100.0f || halfHeight < 1.0f
        || halfHeight > 100.0f) {
        setError(error, "invalid race gate destination or dimensions");
        return false;
    }

    size_t pointCount = route.size();
    if (pointCount > 1u) {
        const glm::vec2 closure = route.front() - route.back();
        if (glm::dot(closure, closure) < 1.0e-4f) --pointCount;
    }
    if (pointCount < 3u || pointCount > kMaximumRaceCheckpoints) {
        setError(error, "race route requires 3..64 unique points");
        return false;
    }

    std::vector<RaceCheckpoint> result;
    result.reserve(pointCount);
    for (size_t index = 0u; index < pointCount; ++index) {
        const glm::vec2 previous = route[
            index == 0u ? pointCount - 1u : index - 1u];
        const glm::vec2 current = route[index];
        const glm::vec2 next = route[(index + 1u) % pointCount];
        if (!std::isfinite(current.x) || !std::isfinite(current.y)) {
            setError(error, "race route contains a non-finite point");
            return false;
        }
        glm::vec2 direction = next - previous;
        const float directionLength2 = glm::dot(direction, direction);
        if (!std::isfinite(directionLength2)
            || directionLength2 < 1.0e-4f) {
            setError(error, "race route contains a degenerate gate");
            return false;
        }
        direction /= std::sqrt(directionLength2);
        result.push_back({
            .center = {current.x, 0.0f, current.y},
            .forward = {direction.x, 0.0f, direction.y},
            .halfWidth = halfWidth,
            .halfHeight = halfHeight,
        });
    }

    *checkpoints = std::move(result);
    if (error != nullptr) error->clear();
    return true;
}

bool buildCircuitGridPose(std::span<const RaceCheckpoint> checkpoints,
                          float distanceBehindGate, CircuitGridPose* pose,
                          std::string* error) {
    if (pose == nullptr || checkpoints.empty()
        || !std::isfinite(distanceBehindGate)
        || distanceBehindGate < 2.0f || distanceBehindGate > 100.0f) {
        setError(error, "invalid circuit grid request");
        return false;
    }
    const RaceCheckpoint& start = checkpoints.front();
    if (!finite(start.center) || !finite(start.forward)) {
        setError(error, "invalid circuit start gate");
        return false;
    }
    const glm::vec3 flatForward = horizontal(start.forward);
    const float forwardLength2 = lengthSquared(flatForward);
    if (forwardLength2 < 1.0e-6f) {
        setError(error, "circuit start gate has no horizontal direction");
        return false;
    }
    const glm::vec3 forward = flatForward / std::sqrt(forwardLength2);
    pose->position = start.center - forward * distanceBehindGate;
    pose->yaw = std::atan2(forward.x, forward.z);
    if (error != nullptr) error->clear();
    return true;
}

RaceConfig makeUntimedPracticeRaceConfig() noexcept {
    RaceConfig config;
    config.mode = RaceMode::Freeride;
    config.countdownTicks = 0u;
    config.durationTicks = 0u;
    return config;
}

bool isCircuitGridLocked(RaceMode mode, RacePhase phase) noexcept {
    return mode == RaceMode::Circuit && phase == RacePhase::Countdown;
}

MotoRaceApplicationPolicy evaluateMotoRaceApplicationPolicy(
    bool circuitPressed, bool resetPressed, RaceMode mode,
    RacePhase phase, uint32_t countdownTicksRemaining) noexcept {
    MotoRaceApplicationPolicy policy;
    if (circuitPressed
        || (resetPressed && mode == RaceMode::Circuit)) {
        policy.action = MotoRaceControlAction::StartCircuit;
    } else if (resetPressed) {
        policy.action = MotoRaceControlAction::ResetPractice;
    }
    policy.gridOwned = isCircuitGridLocked(mode, phase);
    policy.hudRaceActive = mode == RaceMode::Circuit;
    if (policy.hudRaceActive) {
        policy.hudPhase = phase;
        policy.hudCountdownTicks = phase == RacePhase::Countdown
            ? countdownTicksRemaining : 0u;
    }
    return policy;
}

bool RaceSession::configure(const RaceConfig& config,
                            std::span<const RaceCheckpoint> checkpoints,
                            std::string* error) {
    if ((config.mode == RaceMode::Circuit && checkpoints.size() < 2u)
        || checkpoints.size() > kMaximumRaceCheckpoints) {
        setError(error, "circuit requires 2..64 checkpoints");
        return false;
    }
    if (config.tickRate == 0u || config.tickRate > 240u
        || (config.mode == RaceMode::Circuit && config.durationTicks == 0u)
        || config.lapCount == 0u
        || config.lapCount > 99u || config.maximumPlayers == 0u
        || config.maximumPlayers > kMaximumRacePlayers
        || !std::isfinite(config.maximumTravelPerTick)
        || config.maximumTravelPerTick <= 0.0f
        || config.maximumTrickEventsPerFrame == 0u
        || config.maximumTrickEventsPerFrame > 64u
        || config.minimumTrickLandingIntervalTicks == 0u
        || config.minimumTrickLandingIntervalTicks > 3'600u) {
        setError(error, "invalid race configuration");
        return false;
    }
    std::vector<RaceCheckpoint> canonical;
    canonical.reserve(checkpoints.size());
    for (const RaceCheckpoint& checkpoint : checkpoints) {
        const glm::vec3 flatForward = horizontal(checkpoint.forward);
        const float forwardLength2 = lengthSquared(flatForward);
        if (!finite(checkpoint.center) || !finite(checkpoint.forward)
            || std::abs(checkpoint.forward.y) > 0.01f
            || !std::isfinite(checkpoint.halfWidth)
            || !std::isfinite(checkpoint.halfHeight)
            || checkpoint.halfWidth < 1.0f || checkpoint.halfWidth > 100.0f
            || checkpoint.halfHeight < 1.0f || checkpoint.halfHeight > 100.0f
            || forwardLength2 < 1.0e-6f) {
            setError(error, "invalid checkpoint");
            return false;
        }
        RaceCheckpoint normalized = checkpoint;
        normalized.forward = flatForward / std::sqrt(forwardLength2);
        canonical.push_back(normalized);
    }

    // Check every pair, not only neighbours. Overlapping gates can otherwise
    // let one movement collect multiple nearby course locations over time.
    for (size_t first = 0; first < canonical.size(); ++first) {
        for (size_t second = first + 1u; second < canonical.size(); ++second) {
            const glm::vec3 delta = horizontal(canonical[second].center
                                               - canonical[first].center);
            const float minimumDistance = canonical[first].halfWidth
                + canonical[second].halfWidth + 1.0f;
            if (lengthSquared(delta) < minimumDistance * minimumDistance) {
                setError(error, "checkpoint gates overlap");
                return false;
            }
        }
    }

    // Gate normals must follow the authored course. A reversed or sideways
    // gate would create an impossible lap or reward travel in the wrong lane.
    for (size_t index = 0; index < canonical.size(); ++index) {
        const size_t previous = index == 0u ? canonical.size() - 1u : index - 1u;
        const size_t next = (index + 1u) % canonical.size();
        glm::vec3 courseDirection = canonical.size() == 2u
            ? canonical[next].center - canonical[index].center
            : canonical[next].center - canonical[previous].center;
        courseDirection = horizontal(courseDirection);
        const float courseLength2 = lengthSquared(courseDirection);
        if (courseLength2 < 1.0e-6f
            || glm::dot(canonical[index].forward,
                        courseDirection / std::sqrt(courseLength2)) < 0.5f) {
            setError(error, "checkpoint direction does not follow course");
            return false;
        }
    }

    config_ = config;
    checkpoints_ = std::move(canonical);
    configured_ = true;
    reset();
    if (error != nullptr) error->clear();
    return true;
}

void RaceSession::reset() noexcept {
    for (RaceRiderState& rider : riders_) rider = {};
    phase_ = RacePhase::Lobby;
    tick_ = 0u;
    countdownStartTick_ = 0u;
    runningStartTick_ = 0u;
    finishers_ = 0u;
}

RaceRiderState* RaceSession::mutableRider(uint16_t player) noexcept {
    if (player == kInvalidRacePlayer) return nullptr;
    for (RaceRiderState& rider : riders_) {
        if (rider.joined && rider.player == player) return &rider;
    }
    return nullptr;
}

const RaceRiderState* RaceSession::rider(uint16_t player) const noexcept {
    if (player == kInvalidRacePlayer) return nullptr;
    for (const RaceRiderState& rider : riders_) {
        if (rider.joined && rider.player == player) return &rider;
    }
    return nullptr;
}

bool RaceSession::join(uint16_t player) noexcept {
    if (!configured_ || phase_ != RacePhase::Lobby
        || player == kInvalidRacePlayer) return false;
    if (rider(player) != nullptr) return true;

    uint32_t joined = 0u;
    RaceRiderState* free = nullptr;
    for (RaceRiderState& candidate : riders_) {
        if (candidate.joined) {
            ++joined;
        } else if (free == nullptr) {
            free = &candidate;
        }
    }
    if (joined >= config_.maximumPlayers || free == nullptr) return false;
    *free = {};
    free->player = player;
    free->joined = true;
    return true;
}

bool RaceSession::leave(uint16_t player) noexcept {
    RaceRiderState* state = mutableRider(player);
    if (state == nullptr) return false;
    *state = {};
    return true;
}

bool RaceSession::start() noexcept {
    if (!configured_ || phase_ != RacePhase::Lobby) return false;
    const bool anyPlayer = std::any_of(
        riders_.begin(), riders_.end(),
        [](const RaceRiderState& rider) { return rider.joined; });
    if (!anyPlayer) return false;
    phase_ = config_.countdownTicks == 0u
        ? RacePhase::Running : RacePhase::Countdown;
    countdownStartTick_ = tick_;
    if (phase_ == RacePhase::Running) runningStartTick_ = tick_;
    return true;
}

uint64_t RaceSession::runningTick() const noexcept {
    if (phase_ == RacePhase::Lobby || phase_ == RacePhase::Countdown) return 0u;
    return tick_ >= runningStartTick_ ? tick_ - runningStartTick_ : 0u;
}

uint32_t RaceSession::countdownTicksRemaining() const noexcept {
    if (phase_ != RacePhase::Countdown) return 0u;
    const uint64_t elapsed = tick_ - countdownStartTick_;
    return elapsed >= config_.countdownTicks
        ? 0u : static_cast<uint32_t>(config_.countdownTicks - elapsed);
}

void RaceSession::acceptFrame(RaceRiderState& state,
                              const RaceRiderFrame& frame) noexcept {
    if (!finite(frame.position)) {
        if (state.rejectedFrames != std::numeric_limits<uint32_t>::max()) {
            ++state.rejectedFrames;
        }
        return;
    }

    const bool discontinuity = frame.discontinuity || !state.positionValid
        || lengthSquared(frame.position - state.position)
            > config_.maximumTravelPerTick * config_.maximumTravelPerTick;
    state.previousPosition = discontinuity ? frame.position : state.position;
    state.position = frame.position;
    state.positionValid = true;

    if (phase_ != RacePhase::Running || state.finished) return;

    const size_t acceptedEventLimit = std::min(
        frame.trickEvents.size(),
        static_cast<size_t>(config_.maximumTrickEventsPerFrame));
    for (size_t index = 0; index < acceptedEventLimit; ++index) {
        const RaceTrickEvent& event = frame.trickEvents[index];
        const uint32_t score = trickScore(event.trick);
        const uint32_t trickIndex = static_cast<uint32_t>(event.trick);
        const uint32_t trickMask = trickIndex < 32u
            ? 1u << trickIndex : 0u;
        const bool firstLanding = state.lastLandingIdentity == 0u;
        const bool sameLanding = !firstLanding
            && event.landingIdentity == state.lastLandingIdentity;
        const bool nextLanding = firstLanding
            ? event.landingIdentity == 1u
            : state.lastLandingIdentity
                    != std::numeric_limits<uint64_t>::max()
                && event.landingIdentity == state.lastLandingIdentity + 1u;
        const bool landingTimingValid = sameLanding
            ? event.authorityTick == state.lastLandingTick
            : nextLanding && event.authorityTick >= state.lastLandingTick
                    + (firstLanding
                           ? 0u
                           : config_.minimumTrickLandingIntervalTicks);
        if (event.sequence == 0u
            || state.lastTrickSequence == std::numeric_limits<uint64_t>::max()
            || event.sequence != state.lastTrickSequence + 1u
            || event.authorityTick != tick_
            || score == 0u || trickMask == 0u
            || (!sameLanding && !nextLanding)
            || !landingTimingValid
            || (sameLanding
                && (state.acceptedLandingTrickMask & trickMask) != 0u)) {
            rejectTrick(state);
            continue;
        }
        state.lastTrickSequence = event.sequence;
        if (!sameLanding) {
            state.lastLandingIdentity = event.landingIdentity;
            state.lastLandingTick = event.authorityTick;
            state.acceptedLandingTrickMask = 0u;
        }
        state.acceptedLandingTrickMask |= trickMask;
        state.score += std::min(
            std::numeric_limits<uint64_t>::max() - state.score,
            static_cast<uint64_t>(score));
    }
    for (size_t index = acceptedEventLimit;
         index < frame.trickEvents.size(); ++index) {
        rejectTrick(state);
    }

    if (config_.mode != RaceMode::Circuit || discontinuity
        || checkpoints_.empty()) return;
    const RaceCheckpoint& next = checkpoints_[state.nextCheckpoint];
    if (!segmentCrossesCheckpoint(
            state.previousPosition, state.position, next)) return;

    ++state.nextCheckpoint;
    if (state.nextCheckpoint != checkpoints_.size()) return;
    state.nextCheckpoint = 0u;
    ++state.completedLaps;
    if (state.completedLaps < config_.lapCount) return;

    state.finished = true;
    state.didNotFinish = false;
    state.finishTick = runningTick();
    state.finishPlace = ++finishers_;
}

void RaceSession::finishExpiredSession() noexcept {
    phase_ = RacePhase::Finished;
    for (RaceRiderState& rider : riders_) {
        if (!rider.joined || rider.finished) continue;
        rider.finished = true;
        rider.didNotFinish = config_.mode == RaceMode::Circuit;
        rider.finishTick = config_.durationTicks;
    }
}

void RaceSession::step(std::span<const RaceRiderFrame> frames) noexcept {
    if (!configured_ || phase_ == RacePhase::Finished) return;
    ++tick_;

    if (phase_ == RacePhase::Countdown
        && tick_ - countdownStartTick_ >= config_.countdownTicks) {
        phase_ = RacePhase::Running;
        runningStartTick_ = tick_;
    }

    std::array<bool, kMaximumRacePlayers> consumed{};
    for (const RaceRiderFrame& frame : frames) {
        RaceRiderState* state = mutableRider(frame.player);
        if (state == nullptr) continue;
        const size_t index = static_cast<size_t>(state - riders_.data());
        if (consumed[index]) {
            if (state->rejectedFrames != std::numeric_limits<uint32_t>::max()) {
                ++state->rejectedFrames;
            }
            continue;
        }
        consumed[index] = true;
        acceptFrame(*state, frame);
    }

    if (phase_ != RacePhase::Running) return;
    if (config_.durationTicks != 0u
        && runningTick() >= config_.durationTicks) {
        finishExpiredSession();
        return;
    }

    bool any = false;
    bool allFinished = true;
    for (const RaceRiderState& rider : riders_) {
        if (!rider.joined) continue;
        any = true;
        allFinished = allFinished && rider.finished;
    }
    if (any && allFinished) phase_ = RacePhase::Finished;
}

std::vector<uint16_t> RaceSession::standings() const {
    std::vector<const RaceRiderState*> ordered;
    ordered.reserve(config_.maximumPlayers);
    for (const RaceRiderState& rider : riders_) {
        if (rider.joined) ordered.push_back(&rider);
    }
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [this](const RaceRiderState* a, const RaceRiderState* b) {
            if (config_.mode == RaceMode::Freeride) {
                if (a->score != b->score) return a->score > b->score;
            } else {
                const bool aPlaced = a->finishPlace != 0u;
                const bool bPlaced = b->finishPlace != 0u;
                if (aPlaced != bPlaced) return aPlaced;
                if (aPlaced && a->finishPlace != b->finishPlace) {
                    return a->finishPlace < b->finishPlace;
                }
                if (a->completedLaps != b->completedLaps) {
                    return a->completedLaps > b->completedLaps;
                }
                if (a->nextCheckpoint != b->nextCheckpoint) {
                    return a->nextCheckpoint > b->nextCheckpoint;
                }
            }
            return a->player < b->player;
        });

    std::vector<uint16_t> result;
    result.reserve(ordered.size());
    for (const RaceRiderState* rider : ordered) result.push_back(rider->player);
    return result;
}

} // namespace voxy::moto

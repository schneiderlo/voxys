#include "network/replication.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <tuple>
#include <utility>

namespace voxy::network {
namespace {

constexpr uint32_t kSnapshotSchemaVersion = 1;
constexpr uint32_t kMaximumSnapshotBodies = 1'048'576;
constexpr uint64_t kSnapshotHeaderBytes = 52u;
constexpr uint32_t kFnvOffset = 2'166'136'261u;
constexpr uint32_t kFnvPrime = 16'777'619u;

bool bodyAlive(const physics::deterministic::LockstepBody& body) noexcept {
    return (body.identity[2] & physics::deterministic::LockstepBodyAlive) != 0u;
}

uint32_t hashWord(uint32_t hash, uint32_t word) noexcept {
    return (hash ^ word) * kFnvPrime;
}

class Writer {
public:
    void u32(uint32_t value) {
        for (uint32_t shift = 0; shift < 32u; shift += 8u)
            bytes_.push_back(std::byte{static_cast<uint8_t>(value >> shift)});
    }
    void i32(int32_t value) { u32(std::bit_cast<uint32_t>(value)); }
    void u64(uint64_t value) {
        u32(static_cast<uint32_t>(value));
        u32(static_cast<uint32_t>(value >> 32u));
    }
    [[nodiscard]] std::vector<std::byte>& bytes() noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    bool u32(uint32_t& value) {
        if (remaining() < 4u) return fail("truncated u32");
        value = 0;
        for (uint32_t byte = 0; byte < 4u; ++byte) {
            value |= std::to_integer<uint32_t>(bytes_[offset_ + byte])
                << (byte * 8u);
        }
        offset_ += 4u;
        return true;
    }
    bool i32(int32_t& value) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        value = std::bit_cast<int32_t>(bits);
        return true;
    }
    bool u64(uint64_t& value) {
        uint32_t low = 0;
        uint32_t high = 0;
        if (!u32(low) || !u32(high)) return false;
        value = uint64_t{low} | (uint64_t{high} << 32u);
        return true;
    }
    [[nodiscard]] size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    bool fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }
    std::span<const std::byte> bytes_;
    size_t offset_ = 0;
    std::string error_;
};

void writeBody(Writer& writer,
               const physics::deterministic::LockstepBody& body) {
    for (uint32_t value : body.identity) writer.u32(value);
    for (int32_t value : body.sectorRadius) writer.i32(value);
    for (int32_t value : body.positionInvMass) writer.i32(value);
    for (int32_t value : body.linearVelocity) writer.i32(value);
}

bool readBody(Reader& reader, physics::deterministic::LockstepBody& body) {
    for (uint32_t& value : body.identity) if (!reader.u32(value)) return false;
    for (int32_t& value : body.sectorRadius) if (!reader.i32(value)) return false;
    for (int32_t& value : body.positionInvMass) if (!reader.i32(value)) return false;
    for (int32_t& value : body.linearVelocity) if (!reader.i32(value)) return false;
    return true;
}

bool sameBody(const physics::deterministic::LockstepBody& lhs,
              const physics::deterministic::LockstepBody& rhs) noexcept {
    return std::bit_cast<std::array<uint32_t, 16>>(lhs)
        == std::bit_cast<std::array<uint32_t, 16>>(rhs);
}

void canonicalizeBodies(
    std::vector<physics::deterministic::LockstepBody>& bodies) {
    std::erase_if(bodies, [](const auto& body) { return !bodyAlive(body); });
    std::stable_sort(bodies.begin(), bodies.end(),
        [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.identity[0], lhs.identity[1])
                 < std::tie(rhs.identity[0], rhs.identity[1]);
        });
    bodies.erase(std::unique(
        bodies.begin(), bodies.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.identity[0] == rhs.identity[0];
        }), bodies.end());
}

bool canonicalBodies(
    std::span<const physics::deterministic::LockstepBody> bodies) noexcept {
    for (size_t index = 0; index < bodies.size(); ++index) {
        if (!bodyAlive(bodies[index]) || bodies[index].sectorRadius[3] <= 0
            || (index != 0u
                && bodies[index - 1u].identity[0]
                    >= bodies[index].identity[0])) {
            return false;
        }
    }
    return true;
}

bool canonicalRemovals(std::span<const uint32_t> removals) noexcept {
    return std::is_sorted(removals.begin(), removals.end())
        && std::adjacent_find(removals.begin(), removals.end())
            == removals.end();
}

bool snapshotWireSize(
    size_t bodyCount, size_t removedCount, uint64_t& bytes) noexcept {
    if (bodyCount > kMaximumSnapshotBodies
        || removedCount > kMaximumSnapshotBodies) {
        return false;
    }
    bytes = kSnapshotHeaderBytes
        + uint64_t{bodyCount}
            * sizeof(physics::deterministic::LockstepBody)
        + uint64_t{removedCount} * sizeof(uint32_t);
    return bytes <= kMaximumReliableFrameBytes
        - kNetworkPacketOverheadBytes;
}

bool validSnapshotShape(const AuthoritativeSnapshot& snapshot) noexcept {
    uint64_t wireBytes = 0u;
    if (snapshot.islandId == 0u || snapshot.authorityEpoch == 0u
        || !snapshotWireSize(
            snapshot.bodies.size(), snapshot.removedBodyIds.size(), wireBytes)
        || !canonicalBodies(snapshot.bodies)
        || !canonicalRemovals(snapshot.removedBodyIds)) {
        return false;
    }
    if (snapshot.full) {
        return snapshot.baselineTick == 0u
            && snapshot.removedBodyIds.empty();
    }
    if (snapshot.baselineTick >= snapshot.tick) return false;
    for (const auto& body : snapshot.bodies) {
        if (std::binary_search(snapshot.removedBodyIds.begin(),
                               snapshot.removedBodyIds.end(),
                               body.identity[0])) {
            return false;
        }
    }
    return true;
}

bool sameSnapshotState(
    const AuthoritativeSnapshot& lhs,
    const AuthoritativeSnapshot& rhs) noexcept {
    return lhs.stateHash == rhs.stateHash
        && lhs.bodies.size() == rhs.bodies.size()
        && std::equal(lhs.bodies.begin(), lhs.bodies.end(),
                      rhs.bodies.begin(), sameBody);
}

int64_t floorDivide(int64_t numerator, int64_t denominator) noexcept {
    int64_t quotient = numerator / denominator;
    if (numerator < 0 && numerator % denominator != 0) --quotient;
    return quotient;
}

int32_t saturateI32(int64_t value) noexcept {
    return static_cast<int32_t>(std::clamp(
        value, int64_t{std::numeric_limits<int32_t>::min()},
        int64_t{std::numeric_limits<int32_t>::max()}));
}

} // namespace

InputRedundancyBuffer::InputRedundancyBuffer(uint32_t historyCapacity)
    : historyCapacity_(std::max(historyCapacity, 1u)) {}

bool InputRedundancyBuffer::push(const InputFrame& frame) {
    if (frame.tick <= acknowledgedTick_) return false;
    auto iterator = std::lower_bound(
        frames_.begin(), frames_.end(), frame.tick,
        [](const InputFrame& value, uint64_t tick) { return value.tick < tick; });
    if (iterator != frames_.end() && iterator->tick == frame.tick) {
        if (iterator->sequence >= frame.sequence) return false;
        *iterator = frame;
    } else {
        frames_.insert(iterator, frame);
    }
    if (frames_.size() > historyCapacity_)
        frames_.erase(frames_.begin(),
                      frames_.begin() + static_cast<std::ptrdiff_t>(
                          frames_.size() - historyCapacity_));
    return true;
}

std::vector<InputFrame> InputRedundancyBuffer::bundle(
    uint32_t frameCount) const {
    const size_t count = std::min<size_t>(
        std::min(frameCount, kMaximumInputFramesPerBundle), frames_.size());
    return {frames_.end() - static_cast<std::ptrdiff_t>(count), frames_.end()};
}

std::vector<InputFrame> InputRedundancyBuffer::ingest(
    std::span<const InputFrame> redundantFrames) {
    if (redundantFrames.size() > kMaximumInputFramesPerBundle) return {};
    std::vector<InputFrame> accepted;
    accepted.reserve(redundantFrames.size());
    for (const auto& frame : redundantFrames) {
        if (push(frame)) accepted.push_back(frame);
    }
    std::stable_sort(accepted.begin(), accepted.end(),
        [](const InputFrame& lhs, const InputFrame& rhs) {
            return std::tie(lhs.tick, lhs.sequence)
                 < std::tie(rhs.tick, rhs.sequence);
        });
    return accepted;
}

void InputRedundancyBuffer::acknowledge(uint64_t tick) {
    acknowledgedTick_ = std::max(acknowledgedTick_, tick);
    std::erase_if(frames_, [this](const InputFrame& frame) {
        return frame.tick <= acknowledgedTick_;
    });
}

std::vector<std::byte> InputRedundancyBuffer::encode(
    std::span<const InputFrame> input) {
    std::vector<InputFrame> frames;
    frames.reserve(std::min<size_t>(
        input.size(), kMaximumInputFramesPerBundle));
    for (const auto& frame : input) {
        const auto iterator = std::upper_bound(
            frames.begin(), frames.end(), frame,
            [](const InputFrame& value, const InputFrame& existing) {
                return std::tie(value.tick, value.sequence)
                    < std::tie(existing.tick, existing.sequence);
            });
        frames.insert(iterator, frame);
        if (frames.size() > kMaximumInputFramesPerBundle)
            frames.erase(frames.begin());
    }
    Writer writer;
    writer.u32(static_cast<uint32_t>(frames.size()));
    for (const auto& frame : frames) {
        writer.u64(frame.tick);
        writer.u64(frame.sequence);
        writer.u32(frame.buttons);
        writer.i32(frame.moveXQ16);
        writer.i32(frame.moveZQ16);
        writer.i32(frame.lookXQ16);
        writer.i32(frame.lookYQ16);
    }
    return std::move(writer.bytes());
}

InputBundleReadResult InputRedundancyBuffer::decode(
    std::span<const std::byte> bytes) {
    InputBundleReadResult result;
    Reader reader(bytes);
    uint32_t count = 0;
    if (!reader.u32(count)) {
        result.error = reader.error();
        return result;
    }
    constexpr uint32_t encodedFrameBytes = 36;
    if (count > kMaximumInputFramesPerBundle
        || uint64_t{count} * encodedFrameBytes != reader.remaining()) {
        result.error = "invalid input redundancy bundle";
        return result;
    }
    std::vector<InputFrame> frames(count);
    for (auto& frame : frames) {
        if (!reader.u64(frame.tick) || !reader.u64(frame.sequence)
            || !reader.u32(frame.buttons) || !reader.i32(frame.moveXQ16)
            || !reader.i32(frame.moveZQ16) || !reader.i32(frame.lookXQ16)
            || !reader.i32(frame.lookYQ16)) {
            result.error = reader.error();
            return result;
        }
    }
    std::stable_sort(frames.begin(), frames.end(),
        [](const InputFrame& lhs, const InputFrame& rhs) {
            return std::tie(lhs.tick, lhs.sequence)
                 < std::tie(rhs.tick, rhs.sequence);
        });
    result.frames = std::move(frames);
    return result;
}

TickSynchronizer::TickSynchronizer() : TickSynchronizer(Config{}) {}

TickSynchronizer::TickSynchronizer(Config config) : config_(config) {
    if (config_.tickRateHz == 0) config_.tickRateHz = 60;
    if (config_.maximumSamples == 0) config_.maximumSamples = 1;
}

bool TickSynchronizer::observe(const TickSyncSample& sample) {
    if (sample.clientReceiveMicros < sample.clientSendMicros) return false;
    const uint64_t rtt = sample.clientReceiveMicros - sample.clientSendMicros;
    if (rtt > 10'000'000u) return false;
    if (samples_ >= config_.maximumSamples) {
        samples_ = 0;
        minimumRttMicros_ = 0;
    }
    const bool haveMinimum = samples_ != 0u;
    ++samples_;
    if (haveMinimum && rtt > minimumRttMicros_) return true;
    minimumRttMicros_ = rtt;
    const uint64_t oneWayTicks =
        (rtt * config_.tickRateHz + 1'000'000u) / 2'000'000u;
    const uint64_t projectedServer = sample.serverTick
        > std::numeric_limits<uint64_t>::max() - oneWayTicks
        ? std::numeric_limits<uint64_t>::max()
        : sample.serverTick + oneWayTicks;
    if (projectedServer >= sample.localReceiveTick) {
        const uint64_t positive = projectedServer - sample.localReceiveTick;
        offsetTicks_ = positive > uint64_t{std::numeric_limits<int64_t>::max()}
            ? std::numeric_limits<int64_t>::max()
            : static_cast<int64_t>(positive);
    } else {
        const uint64_t negative = sample.localReceiveTick - projectedServer;
        offsetTicks_ = negative > uint64_t{std::numeric_limits<int64_t>::max()}
            ? std::numeric_limits<int64_t>::min()
            : -static_cast<int64_t>(negative);
    }
    return true;
}

uint64_t TickSynchronizer::estimatedServerTick(uint64_t localTick) const {
    if (offsetTicks_ >= 0) {
        const uint64_t offset = static_cast<uint64_t>(offsetTicks_);
        return localTick > std::numeric_limits<uint64_t>::max() - offset
            ? std::numeric_limits<uint64_t>::max() : localTick + offset;
    }
    const uint64_t magnitude = static_cast<uint64_t>(-(offsetTicks_ + 1)) + 1u;
    return magnitude > localTick ? 0u : localTick - magnitude;
}

uint64_t TickSynchronizer::recommendedInputTick(uint64_t localTick) const {
    const uint64_t server = estimatedServerTick(localTick);
    return server > std::numeric_limits<uint64_t>::max()
                    - config_.inputLeadTicks
        ? std::numeric_limits<uint64_t>::max()
        : server + config_.inputLeadTicks;
}

uint32_t snapshotStateHash(const AuthoritativeSnapshot& source) {
    AuthoritativeSnapshot snapshot = source;
    canonicalizeBodies(snapshot.bodies);
    uint32_t hash = hashWord(kFnvOffset, static_cast<uint32_t>(snapshot.tick));
    hash = hashWord(hash, static_cast<uint32_t>(snapshot.tick >> 32u));
    hash = hashWord(hash, static_cast<uint32_t>(snapshot.islandId));
    hash = hashWord(hash, static_cast<uint32_t>(snapshot.islandId >> 32u));
    hash = hashWord(hash, snapshot.authorityEpoch);
    for (const auto& body : snapshot.bodies) {
        const auto words = std::bit_cast<std::array<uint32_t, 16>>(body);
        for (uint32_t word : words) hash = hashWord(hash, word);
    }
    return hash;
}

bool isCanonicalAuthoritativeSnapshot(
    const AuthoritativeSnapshot& snapshot) {
    return validSnapshotShape(snapshot)
        && (!snapshot.full
            || snapshotStateHash(snapshot) == snapshot.stateHash);
}

std::vector<std::byte> SnapshotCodec::encode(
    const AuthoritativeSnapshot& source) {
    uint64_t sourceBytes = 0u;
    if (!snapshotWireSize(
            source.bodies.size(), source.removedBodyIds.size(), sourceBytes)) {
        return {};
    }
    AuthoritativeSnapshot snapshot = source;
    canonicalizeBodies(snapshot.bodies);
    std::sort(snapshot.removedBodyIds.begin(), snapshot.removedBodyIds.end());
    snapshot.removedBodyIds.erase(std::unique(snapshot.removedBodyIds.begin(),
                                              snapshot.removedBodyIds.end()),
                                  snapshot.removedBodyIds.end());
    if (snapshot.full) {
        snapshot.baselineTick = 0u;
        snapshot.removedBodyIds.clear();
        snapshot.stateHash = snapshotStateHash(snapshot);
    } else {
        std::erase_if(snapshot.removedBodyIds, [&snapshot](uint32_t bodyId) {
            const auto changed = std::lower_bound(
                snapshot.bodies.begin(), snapshot.bodies.end(), bodyId,
                [](const auto& body, uint32_t id) {
                    return body.identity[0] < id;
                });
            return changed != snapshot.bodies.end()
                && changed->identity[0] == bodyId;
        });
    }
    uint64_t wireBytes = 0u;
    if (!validSnapshotShape(snapshot)
        || !snapshotWireSize(snapshot.bodies.size(),
                             snapshot.removedBodyIds.size(), wireBytes)) {
        return {};
    }
    Writer writer;
    writer.u32(kSnapshotSchemaVersion);
    writer.u32(snapshot.full ? 1u : 0u);
    writer.u64(snapshot.tick);
    writer.u64(snapshot.islandId);
    writer.u32(snapshot.authorityEpoch);
    writer.u32(0u);
    writer.u64(snapshot.baselineTick);
    writer.u32(snapshot.stateHash);
    writer.u32(static_cast<uint32_t>(snapshot.bodies.size()));
    writer.u32(static_cast<uint32_t>(snapshot.removedBodyIds.size()));
    for (const auto& body : snapshot.bodies) writeBody(writer, body);
    for (uint32_t body : snapshot.removedBodyIds) writer.u32(body);
    return std::move(writer.bytes());
}

SnapshotReadResult SnapshotCodec::decode(std::span<const std::byte> bytes) {
    SnapshotReadResult result;
    Reader reader(bytes);
    uint32_t schema = 0;
    uint32_t flags = 0;
    uint32_t reserved = 0;
    uint32_t bodyCount = 0;
    uint32_t removedCount = 0;
    AuthoritativeSnapshot snapshot;
    if (!reader.u32(schema) || !reader.u32(flags)
        || !reader.u64(snapshot.tick) || !reader.u64(snapshot.islandId)
        || !reader.u32(snapshot.authorityEpoch) || !reader.u32(reserved)
        || !reader.u64(snapshot.baselineTick)
        || !reader.u32(snapshot.stateHash) || !reader.u32(bodyCount)
        || !reader.u32(removedCount)) {
        result.error = reader.error();
        return result;
    }
    if (schema != kSnapshotSchemaVersion || flags > 1u || reserved != 0u) {
        result.error = "invalid snapshot header";
        return result;
    }
    const uint64_t required = uint64_t{bodyCount}
            * sizeof(physics::deterministic::LockstepBody)
        + uint64_t{removedCount} * sizeof(uint32_t);
    uint64_t wireBytes = 0u;
    if (!snapshotWireSize(bodyCount, removedCount, wireBytes)
        || required != reader.remaining()) {
        result.error = "invalid snapshot element counts";
        return result;
    }
    snapshot.full = flags != 0u;
    snapshot.bodies.resize(bodyCount);
    snapshot.removedBodyIds.resize(removedCount);
    for (auto& body : snapshot.bodies) {
        if (!readBody(reader, body)) {
            result.error = reader.error();
            return result;
        }
    }
    for (uint32_t& body : snapshot.removedBodyIds) {
        if (!reader.u32(body)) {
            result.error = reader.error();
            return result;
        }
    }
    if (!validSnapshotShape(snapshot)) {
        result.error = "snapshot payload is not canonical";
        return result;
    }
    if (snapshot.full && snapshotStateHash(snapshot) != snapshot.stateHash) {
        result.error = "snapshot state hash mismatch";
        return result;
    }
    result.snapshot = std::move(snapshot);
    return result;
}

SnapshotHistory::SnapshotHistory(uint32_t capacity)
    : capacity_(std::max(capacity, 1u)) {}

bool SnapshotHistory::store(AuthoritativeSnapshot snapshot) {
    uint64_t wireBytes = 0u;
    if (!snapshot.full || snapshot.islandId == 0u
        || snapshot.authorityEpoch == 0u || snapshot.baselineTick != 0u
        || !snapshot.removedBodyIds.empty()
        || !snapshotWireSize(snapshot.bodies.size(), 0u, wireBytes)) {
        return false;
    }
    canonicalizeBodies(snapshot.bodies);
    snapshot.stateHash = snapshotStateHash(snapshot);
    if (!validSnapshotShape(snapshot)) return false;
    auto existing = std::find_if(snapshots_.begin(), snapshots_.end(),
        [&snapshot](const auto& value) {
            return value.islandId == snapshot.islandId
                && value.authorityEpoch == snapshot.authorityEpoch
                && value.tick == snapshot.tick;
        });
    if (existing != snapshots_.end())
        return sameSnapshotState(*existing, snapshot);
    snapshots_.push_back(std::move(snapshot));
    std::stable_sort(snapshots_.begin(), snapshots_.end(),
        [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.tick, lhs.islandId, lhs.authorityEpoch)
                 < std::tie(rhs.tick, rhs.islandId, rhs.authorityEpoch);
        });
    if (snapshots_.size() > capacity_)
        snapshots_.erase(snapshots_.begin(),
                         snapshots_.begin() + static_cast<std::ptrdiff_t>(
                             snapshots_.size() - capacity_));
    return true;
}

std::optional<AuthoritativeSnapshot> SnapshotHistory::find(
    uint64_t islandId, uint32_t epoch, uint64_t tick) const {
    auto iterator = std::find_if(snapshots_.begin(), snapshots_.end(),
        [=](const auto& value) {
            return value.islandId == islandId
                && value.authorityEpoch == epoch && value.tick == tick;
        });
    return iterator == snapshots_.end()
        ? std::nullopt : std::optional<AuthoritativeSnapshot>(*iterator);
}

std::optional<AuthoritativeSnapshot> SnapshotHistory::deltaFrom(
    const AuthoritativeSnapshot& source, uint64_t acknowledgedTick) const {
    uint64_t sourceBytes = 0u;
    if (!source.full || source.islandId == 0u
        || source.authorityEpoch == 0u || source.baselineTick != 0u
        || !source.removedBodyIds.empty()
        || acknowledgedTick >= source.tick
        || !snapshotWireSize(
            source.bodies.size(), 0u, sourceBytes)) {
        return std::nullopt;
    }
    const auto baseline = find(source.islandId, source.authorityEpoch,
                               acknowledgedTick);
    if (!baseline.has_value()) return std::nullopt;
    AuthoritativeSnapshot current = source;
    canonicalizeBodies(current.bodies);
    current.stateHash = snapshotStateHash(current);
    AuthoritativeSnapshot delta;
    delta.tick = current.tick;
    delta.islandId = current.islandId;
    delta.authorityEpoch = current.authorityEpoch;
    delta.baselineTick = baseline->tick;
    delta.full = false;
    delta.stateHash = current.stateHash;
    for (const auto& body : current.bodies) {
        auto old = std::lower_bound(
            baseline->bodies.begin(), baseline->bodies.end(), body.identity[0],
            [](const auto& value, uint32_t id) { return value.identity[0] < id; });
        if (old == baseline->bodies.end() || old->identity[0] != body.identity[0]
            || !sameBody(*old, body)) delta.bodies.push_back(body);
    }
    for (const auto& body : baseline->bodies) {
        auto now = std::lower_bound(
            current.bodies.begin(), current.bodies.end(), body.identity[0],
            [](const auto& value, uint32_t id) { return value.identity[0] < id; });
        if (now == current.bodies.end() || now->identity[0] != body.identity[0])
            delta.removedBodyIds.push_back(body.identity[0]);
    }
    return validSnapshotShape(delta)
        ? std::optional<AuthoritativeSnapshot>(std::move(delta))
        : std::nullopt;
}

std::optional<AuthoritativeSnapshot> SnapshotHistory::applyDelta(
    const AuthoritativeSnapshot& delta) const {
    if (delta.full) {
        return validSnapshotShape(delta)
                && snapshotStateHash(delta) == delta.stateHash
            ? std::optional<AuthoritativeSnapshot>(delta) : std::nullopt;
    }
    if (!validSnapshotShape(delta)) return std::nullopt;
    auto result = find(delta.islandId, delta.authorityEpoch,
                       delta.baselineTick);
    if (!result.has_value()) return std::nullopt;
    result->tick = delta.tick;
    result->baselineTick = 0u;
    result->stateHash = delta.stateHash;
    for (uint32_t removed : delta.removedBodyIds) {
        std::erase_if(result->bodies, [removed](const auto& body) {
            return body.identity[0] == removed;
        });
    }
    for (const auto& changed : delta.bodies) {
        auto existing = std::lower_bound(
            result->bodies.begin(), result->bodies.end(), changed.identity[0],
            [](const auto& body, uint32_t id) { return body.identity[0] < id; });
        if (existing != result->bodies.end()
            && existing->identity[0] == changed.identity[0]) *existing = changed;
        else result->bodies.insert(existing, changed);
    }
    result->full = true;
    canonicalizeBodies(result->bodies);
    uint64_t wireBytes = 0u;
    if (!snapshotWireSize(result->bodies.size(), 0u, wireBytes)
        || !validSnapshotShape(*result)
        || snapshotStateHash(*result) != delta.stateHash) {
        return std::nullopt;
    }
    return result;
}

void SnapshotAckTracker::acknowledge(uint32_t clientId, uint64_t tick) {
    auto iterator = std::lower_bound(
        entries_.begin(), entries_.end(), clientId,
        [](const Entry& value, uint32_t id) { return value.clientId < id; });
    if (iterator != entries_.end() && iterator->clientId == clientId)
        iterator->tick = std::max(iterator->tick, tick);
    else entries_.insert(iterator, Entry{clientId, tick});
}

void SnapshotAckTracker::reset(uint32_t clientId) {
    std::erase_if(entries_, [clientId](const Entry& entry) {
        return entry.clientId == clientId;
    });
}

std::optional<uint64_t> SnapshotAckTracker::acknowledgedTick(
    uint32_t clientId) const noexcept {
    auto iterator = std::lower_bound(
        entries_.begin(), entries_.end(), clientId,
        [](const Entry& value, uint32_t id) { return value.clientId < id; });
    return iterator != entries_.end() && iterator->clientId == clientId
        ? std::optional<uint64_t>(iterator->tick) : std::nullopt;
}

InterestGrid::InterestGrid() : InterestGrid(Config{}) {}

InterestGrid::InterestGrid(Config config) : config_(config) {
    if (config_.cellSizeQ12 <= 0)
        config_.cellSizeQ12 = 32 * physics::deterministic::kLockstepPositionOne;
    config_.maximumEntries = std::max(config_.maximumEntries, 1u);
    config_.maximumQueryBodies = std::max(config_.maximumQueryBodies, 1u);
}

InterestCell InterestGrid::cellFor(
    const physics::deterministic::LockstepBody& body) const noexcept {
    InterestCell result;
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        const int64_t position = int64_t{body.sectorRadius[axis]}
                * physics::deterministic::kLockstepSectorSize
            + body.positionInvMass[axis];
        result.coordinate[axis] = saturateI32(
            floorDivide(position, config_.cellSizeQ12));
    }
    return result;
}

bool InterestGrid::rebuild(
    std::span<const physics::deterministic::LockstepBody> bodies) {
    std::vector<Entry> replacement;
    replacement.reserve(std::min<size_t>(
        bodies.size(), config_.maximumEntries));
    uint64_t count = 0;
    for (const auto& body : bodies) {
        if (!bodyAlive(body)) continue;
        ++count;
        if (replacement.size() < config_.maximumEntries)
            replacement.push_back({cellFor(body), body.identity[0]});
    }
    highWater_ = std::max(highWater_, static_cast<uint32_t>(
        std::min<uint64_t>(count, std::numeric_limits<uint32_t>::max())));
    overflowed_ = count > config_.maximumEntries;
    std::stable_sort(replacement.begin(), replacement.end(),
        [](const Entry& lhs, const Entry& rhs) {
            return std::tie(lhs.cell.coordinate, lhs.bodyId)
                 < std::tie(rhs.cell.coordinate, rhs.bodyId);
        });
    entries_ = std::move(replacement);
    return !overflowed_;
}

InterestQueryResult InterestGrid::query(
    InterestCell center, uint32_t radiusCells) const {
    InterestQueryResult result;
    const uint32_t radius = std::min(radiusCells, 64u);
    result.overflow = overflowed_ || radius != radiusCells;
    for (const auto& entry : entries_) {
        bool inside = true;
        for (uint32_t axis = 0; axis < 3u; ++axis) {
            const int64_t delta = int64_t{entry.cell.coordinate[axis]}
                - center.coordinate[axis];
            if (delta < -int64_t{radius} || delta > int64_t{radius}) {
                inside = false;
                break;
            }
        }
        if (!inside) continue;
        if (result.bodyIds.size() >= config_.maximumQueryBodies) {
            result.overflow = true;
            continue;
        }
        result.bodyIds.push_back(entry.bodyId);
    }
    std::sort(result.bodyIds.begin(), result.bodyIds.end());
    return result;
}

bool AuthorityTable::assign(const IslandAuthority& authority) {
    if (authority.islandId == 0u || authority.epoch == 0u) return false;
    auto iterator = std::lower_bound(
        entries_.begin(), entries_.end(), authority.islandId,
        [](const IslandAuthority& value, uint64_t id) {
            return value.islandId < id;
        });
    if (iterator != entries_.end() && iterator->islandId == authority.islandId) {
        if (authority.epoch < iterator->epoch) return false;
        if (authority.epoch == iterator->epoch) {
            return authority.workerId == iterator->workerId
                && authority.startTick == iterator->startTick
                && authority.checkpointHash == iterator->checkpointHash;
        }
        if (authority.startTick <= iterator->startTick) return false;
        *iterator = authority;
    } else {
        entries_.insert(iterator, authority);
    }
    return true;
}

std::optional<IslandAuthority> AuthorityTable::find(uint64_t islandId) const {
    auto iterator = std::lower_bound(
        entries_.begin(), entries_.end(), islandId,
        [](const IslandAuthority& value, uint64_t id) {
            return value.islandId < id;
        });
    return iterator != entries_.end() && iterator->islandId == islandId
        ? std::optional<IslandAuthority>(*iterator) : std::nullopt;
}

bool AuthorityTable::accepts(
    uint64_t islandId, uint32_t epoch, uint64_t tick) const noexcept {
    const auto authority = find(islandId);
    return authority.has_value() && authority->epoch == epoch
        && tick >= authority->startTick;
}

std::optional<IslandAuthority> AuthorityTable::advanceEpoch(
    uint64_t islandId, uint32_t destinationWorker, uint64_t startTick,
    std::array<uint64_t, 2> checkpointHash) {
    const auto current = find(islandId);
    if (!current.has_value()
        || current->epoch == std::numeric_limits<uint32_t>::max()
        || startTick <= current->startTick) return std::nullopt;
    IslandAuthority next{
        .islandId = islandId,
        .epoch = current->epoch + 1u,
        .workerId = destinationWorker,
        .startTick = startTick,
        .checkpointHash = checkpointHash,
    };
    if (!assign(next)) return std::nullopt;
    return next;
}

bool PredictionBubble::initialize(
    const Config& config, uint64_t islandId, uint32_t authorityEpoch,
    uint32_t controlledBody,
    std::span<const physics::deterministic::LockstepBody> bodies,
    uint64_t initialTick) {
    if (islandId == 0u || authorityEpoch == 0u
        || config.historyTicks == 0u
        || config.maximumPredictedBodies == 0u
        || initialTick > std::numeric_limits<uint32_t>::max()
        || controlledBody >= config.world.bodyCapacity) {
        return false;
    }
    physics::deterministic::LockstepWorld replacement;
    if (!replacement.initialize(config.world)
        || !replacement.setBodies(bodies)
        || controlledBody >= replacement.bodies().size()
        || !bodyAlive(replacement.bodies()[controlledBody])
        || replacement.bodies()[controlledBody].identity[0]
            != controlledBody) {
        return false;
    }
    const size_t liveBodies = static_cast<size_t>(std::count_if(
        replacement.bodies().begin(), replacement.bodies().end(),
        [](const auto& body) { return bodyAlive(body); }));
    if (liveBodies > config.maximumPredictedBodies) return false;
    config_ = config;
    world_ = std::move(replacement);
    islandId_ = islandId;
    authorityEpoch_ = authorityEpoch;
    controlledBody_ = controlledBody;
    currentTick_ = initialTick;
    predictedBodies_.clear();
    predictedBodies_.reserve(liveBodies);
    for (const auto& body : world_.bodies()) {
        if (bodyAlive(body))
            predictedBodies_.push_back(body.identity[0]);
    }
    std::sort(predictedBodies_.begin(), predictedBodies_.end());
    boundaryGhosts_.clear();
    history_.clear();
    inputs_.clear();
    recordedCommands_.clear();
    correctionEvents_.clear();
    telemetry_ = {};
    storeHistory(initialTick);
    return true;
}

bool PredictionBubble::setMembership(
    std::span<const uint32_t> predictedBodies,
    std::span<const uint32_t> boundaryGhosts) {
    std::vector<uint32_t> predicted(predictedBodies.begin(),
                                    predictedBodies.end());
    std::vector<uint32_t> ghosts(boundaryGhosts.begin(), boundaryGhosts.end());
    std::sort(predicted.begin(), predicted.end());
    predicted.erase(std::unique(predicted.begin(), predicted.end()),
                    predicted.end());
    std::sort(ghosts.begin(), ghosts.end());
    ghosts.erase(std::unique(ghosts.begin(), ghosts.end()), ghosts.end());
    std::erase_if(ghosts, [&predicted](uint32_t body) {
        return std::binary_search(predicted.begin(), predicted.end(), body);
    });
    if (predicted.size() > config_.maximumPredictedBodies
        || !std::binary_search(predicted.begin(), predicted.end(),
                               controlledBody_)
        || std::any_of(predicted.begin(), predicted.end(), [this](uint32_t id) {
            return id >= world_.bodies().size()
                || !bodyAlive(world_.bodies()[id]);
        })
        || std::any_of(ghosts.begin(), ghosts.end(), [this](uint32_t id) {
            return id >= world_.bodies().size()
                || !bodyAlive(world_.bodies()[id]);
        })
        || std::any_of(
            world_.bodies().begin(), world_.bodies().end(),
            [&predicted, &ghosts](const auto& body) {
                return bodyAlive(body)
                    && !std::binary_search(
                        predicted.begin(), predicted.end(),
                        body.identity[0])
                    && !std::binary_search(
                        ghosts.begin(), ghosts.end(),
                        body.identity[0]);
        })) {
        return false;
    }
    predictedBodies_ = std::move(predicted);
    boundaryGhosts_ = std::move(ghosts);
    return true;
}

void PredictionBubble::storeHistory(uint64_t tick) {
    AuthoritativeSnapshot state = snapshot();
    state.tick = tick;
    state.stateHash = snapshotStateHash(state);
    history_.push_back({tick, state.stateHash,
                        std::vector<physics::deterministic::LockstepBody>(
                            world_.bodies().begin(), world_.bodies().end())});
    if (history_.size() > config_.historyTicks)
        history_.erase(history_.begin(),
                       history_.begin() + static_cast<std::ptrdiff_t>(
                           history_.size() - config_.historyTicks));
    telemetry_.historyHighWater = std::max(
        telemetry_.historyHighWater, static_cast<uint32_t>(history_.size()));
}

void PredictionBubble::recordCommand(
    const physics::deterministic::CanonicalReplayCommand& command) {
    auto iterator = std::upper_bound(
        recordedCommands_.begin(), recordedCommands_.end(), command,
        [](const auto& value, const auto& existing) {
            return physics::deterministic::canonicalReplayCommandLess(
                value, existing);
        });
    recordedCommands_.insert(iterator, command);
}

bool PredictionBubble::predict(
    uint64_t tick,
    std::span<const physics::deterministic::CanonicalReplayCommand> source) {
    if (tick != currentTick_ + 1u
        || tick > std::numeric_limits<uint32_t>::max()
        || source.size() > kMaximumCommandsPerPacket) return false;
    std::vector<physics::deterministic::CanonicalReplayCommand> commands(
        source.begin(), source.end());
    std::stable_sort(commands.begin(), commands.end(),
                     physics::deterministic::canonicalReplayCommandLess);
    if (std::any_of(commands.begin(), commands.end(),
                    [this, tick](const auto& command) {
                        return command.tick != tick
                            || !std::binary_search(
                                predictedBodies_.begin(),
                                predictedBodies_.end(),
                                command.body);
                    })) {
        return false;
    }
    auto replacement = world_;
    for (const auto& command : commands) {
        if (!physics::deterministic::applyCanonicalReplayCommand(
                replacement, command)) {
            return false;
        }
    }
    static_cast<void>(replacement.step(static_cast<uint32_t>(tick)));
    world_ = std::move(replacement);
    for (const auto& command : commands) {
        inputs_.push_back(command);
        recordCommand(command);
    }
    currentTick_ = tick;
    storeHistory(tick);
    return true;
}

std::vector<physics::deterministic::LockstepBody>
PredictionBubble::expandSnapshot(
    const AuthoritativeSnapshot& snapshot) const {
    std::vector<physics::deterministic::LockstepBody> result(
        config_.world.bodyCapacity);
    for (const auto& body : snapshot.bodies) {
        if (body.identity[0] < result.size()) result[body.identity[0]] = body;
    }
    return result;
}

PredictionBubble::ReconcileResult PredictionBubble::reconcile(
    const AuthoritativeSnapshot& authoritative) {
    ReconcileResult result;
    if (!authoritative.full || !validSnapshotShape(authoritative)
        || authoritative.islandId != islandId_
        || authoritative.authorityEpoch != authorityEpoch_
        || authoritative.tick > currentTick_
        || authoritative.tick > std::numeric_limits<uint32_t>::max()
        || snapshotStateHash(authoritative) != authoritative.stateHash
        || std::any_of(authoritative.bodies.begin(),
                       authoritative.bodies.end(), [this](const auto& body) {
            return body.identity[0] >= config_.world.bodyCapacity;
        })) {
        ++telemetry_.staleSnapshots;
        return result;
    }
    result.accepted = true;
    const auto local = std::find_if(history_.begin(), history_.end(),
        [&authoritative](const HistoryEntry& entry) {
            return entry.tick == authoritative.tick;
        });
    if (local != history_.end() && local->hash == authoritative.stateHash) {
        result.matched = true;
        result.finalHash = snapshot().stateHash;
        ++telemetry_.confirmedTicks;
        std::erase_if(history_, [&authoritative](const HistoryEntry& entry) {
            return entry.tick < authoritative.tick;
        });
        std::erase_if(inputs_, [&authoritative](const auto& command) {
            return command.tick <= authoritative.tick;
        });
        return result;
    }

    const uint64_t targetTick = currentTick_;
    const auto restored = expandSnapshot(authoritative);
    if (!world_.setBodies(restored)) {
        result.accepted = false;
        return result;
    }
    for (const auto& body : authoritative.bodies) {
        physics::deterministic::CanonicalReplayCommand correction;
        correction.tick = authoritative.tick;
        correction.sequence = (authoritative.tick << 32u) | body.identity[0];
        correction.type = physics::deterministic::ReplayCommandType::Correction;
        correction.body = body.identity[0];
        correction.generation = body.identity[1];
        correction.payload[0] = body.sectorRadius[0];
        correction.payload[1] = body.sectorRadius[1];
        correction.payload[2] = body.sectorRadius[2];
        correction.payload[4] = body.positionInvMass[0];
        correction.payload[5] = body.positionInvMass[1];
        correction.payload[6] = body.positionInvMass[2];
        correction.payload[8] = body.linearVelocity[0];
        correction.payload[9] = body.linearVelocity[1];
        correction.payload[10] = body.linearVelocity[2];
        correctionEvents_.push_back(correction);
        recordCommand(correction);
    }
    telemetry_.correctionEvents += authoritative.bodies.size();
    std::erase_if(history_, [&authoritative](const HistoryEntry& entry) {
        return entry.tick >= authoritative.tick;
    });
    currentTick_ = authoritative.tick;
    storeHistory(currentTick_);

    std::stable_sort(inputs_.begin(), inputs_.end(),
                     physics::deterministic::canonicalReplayCommandLess);
    size_t commandIndex = 0;
    while (commandIndex < inputs_.size()
           && inputs_[commandIndex].tick <= authoritative.tick) ++commandIndex;
    std::vector<physics::deterministic::CanonicalReplayCommand>
        retainedInputs;
    retainedInputs.reserve(inputs_.size() - commandIndex);
    for (uint64_t tick = authoritative.tick + 1u;
         tick <= targetTick; ++tick) {
        while (commandIndex < inputs_.size()
               && inputs_[commandIndex].tick == tick) {
            if (physics::deterministic::applyCanonicalReplayCommand(
                    world_, inputs_[commandIndex])) {
                retainedInputs.push_back(inputs_[commandIndex]);
            }
            ++commandIndex;
        }
        static_cast<void>(world_.step(static_cast<uint32_t>(tick)));
        currentTick_ = tick;
        storeHistory(tick);
        ++result.replayedTicks;
    }
    inputs_ = std::move(retainedInputs);
    result.rolledBack = true;
    result.finalHash = snapshot().stateHash;
    ++telemetry_.rollbacks;
    telemetry_.replayedTicks += result.replayedTicks;
    return result;
}

AuthoritativeSnapshot PredictionBubble::snapshot() const {
    AuthoritativeSnapshot result;
    result.tick = currentTick_;
    result.islandId = islandId_;
    result.authorityEpoch = authorityEpoch_;
    result.full = true;
    for (const auto& body : world_.bodies()) {
        if (bodyAlive(body)) result.bodies.push_back(body);
    }
    result.stateHash = snapshotStateHash(result);
    return result;
}

} // namespace voxy::network

#include "client/wreckwater_client_probe.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstddef>
#include <limits>

namespace voxy::client {
namespace {

enum OptionBit : uint32_t {
    ServerBit = 1u << 0u,
    PortBit = 1u << 1u,
    PeerBit = 1u << 2u,
    KeyBit = 1u << 3u,
    SessionBit = 1u << 4u,
    MatchBit = 1u << 5u,
    WorldBit = 1u << 6u,
    WorldEpochBit = 1u << 7u,
    AuthorityEpochBit = 1u << 8u,
    MaximumTicksBit = 1u << 9u,
    ReconnectBit = 1u << 10u,
    ChaosSeedBit = 1u << 11u,
};

[[nodiscard]] uint32_t optionBit(std::string_view option) noexcept {
    if (option == "--server") return ServerBit;
    if (option == "--port") return PortBit;
    if (option == "--peer") return PeerBit;
    if (option == "--key") return KeyBit;
    if (option == "--session") return SessionBit;
    if (option == "--match") return MatchBit;
    if (option == "--world") return WorldBit;
    if (option == "--world-epoch") return WorldEpochBit;
    if (option == "--authority-epoch") return AuthorityEpochBit;
    if (option == "--max-ticks") return MaximumTicksBit;
    if (option == "--reconnect-at") return ReconnectBit;
    if (option == "--chaos-seed") return ChaosSeedBit;
    return 0u;
}

template <typename Integer>
[[nodiscard]] bool parseInteger(
    std::string_view text, Integer& value) noexcept {
    if (text.empty()) return false;
    Integer parsed = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{}
        || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] bool parseAuthenticationKey(
    std::string_view text,
    network::NativeTcpAuthenticationKey& key) noexcept {
    if (text.size()
        != network::kNativeTcpAuthenticationKeyBytes * 2u) {
        return false;
    }
    network::NativeTcpAuthenticationKey decoded{};
    bool nonzero = false;
    for (size_t index = 0u; index < decoded.size(); ++index) {
        const int high = hexDigit(text[index * 2u]);
        const int low = hexDigit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        const uint8_t byte = static_cast<uint8_t>(
            static_cast<uint32_t>(high) * 16u
            + static_cast<uint32_t>(low));
        decoded[index] = std::byte{byte};
        nonzero = nonzero || byte != 0u;
    }
    if (!nonzero) return false;
    key = decoded;
    return true;
}

[[nodiscard]] bool incrementTick(
    uint64_t current, uint64_t& next) noexcept {
    if (current == std::numeric_limits<uint64_t>::max()) return false;
    next = current + 1u;
    return true;
}

[[nodiscard]] const network::WreckwaterEntityState* cargoEntity(
    const network::WreckwaterClientSample& sample) noexcept {
    for (uint32_t index = 0u; index < sample.entityCount; ++index) {
        const network::WreckwaterEntityState& entity =
            sample.entities[index].authoritativeState;
        if (entity.kind == network::WreckwaterEntityKind::Cargo
            && entity.cargo.cargoId != 0u
            && entity.cargo.generation != 0u
            && entity.cargo.revision != 0u) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] bool alternatingRevisionTurn(
    uint32_t revision, uint32_t firstRevision,
    bool firstTurn) noexcept {
    if (revision < firstRevision) return false;
    const uint32_t delta = revision - firstRevision;
    if (delta % 3u != 0u) return false;
    const bool evenCycle = (delta / 3u) % 2u == 0u;
    return evenCycle == firstTurn;
}

[[nodiscard]] const network::WreckwaterCharacterState*
characterForPlayer(
    const network::WreckwaterClientSample& sample,
    uint64_t playerId) noexcept {
    for (uint32_t index = 0u; index < sample.characterCount; ++index) {
        const network::WreckwaterCharacterState& character =
            sample.characters[index].authoritativeState;
        if (character.playerId == playerId) return &character;
    }
    return nullptr;
}

void hashByte(uint64_t& hash, uint8_t value) noexcept {
    constexpr uint64_t kFnvPrime = 1'099'511'628'211ull;
    hash ^= value;
    hash *= kFnvPrime;
}

void hashU32(uint64_t& hash, uint32_t value) noexcept {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hashByte(
            hash,
            static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void hashU64(uint64_t& hash, uint64_t value) noexcept {
    for (uint32_t shift = 0u; shift < 64u; shift += 8u) {
        hashByte(
            hash,
            static_cast<uint8_t>((value >> shift) & 0xffu));
    }
}

void hashFloat(uint64_t& hash, float value) noexcept {
    hashU32(hash, std::bit_cast<uint32_t>(value));
}

void hashVector(
    uint64_t& hash, const network::WreckwaterVec3& value) noexcept {
    hashFloat(hash, value.x);
    hashFloat(hash, value.y);
    hashFloat(hash, value.z);
}

void hashCharacter(
    uint64_t& hash,
    const network::WreckwaterCharacterState& character) noexcept {
    hashU32(hash, character.characterHandle);
    hashU32(hash, character.stateFlags);
    hashU64(hash, character.playerId);
    hashU32(hash, character.connectionGeneration);
    for (const int32_t sector : character.sector) {
        hashU32(hash, static_cast<uint32_t>(sector));
    }
    hashVector(hash, character.localFeetPosition);
    hashVector(hash, character.worldVelocity);
    hashU32(hash, character.skiffId);
    hashU32(hash, character.skiffGeneration);
    hashVector(hash, character.skiffLocalFeetPosition);
    hashVector(hash, character.skiffLocalVelocity);
    hashU64(
        hash, character.lastAppliedCharacterInputSequence);
}

} // namespace

const char* wreckwaterClientProbeOptionErrorName(
    WreckwaterClientProbeOptionError error) noexcept {
    switch (error) {
        case WreckwaterClientProbeOptionError::None:
            return "none";
        case WreckwaterClientProbeOptionError::UnknownOption:
            return "unknown option";
        case WreckwaterClientProbeOptionError::MissingValue:
            return "missing option value";
        case WreckwaterClientProbeOptionError::DuplicateOption:
            return "duplicate option";
        case WreckwaterClientProbeOptionError::InvalidServerAddress:
            return "invalid server address";
        case WreckwaterClientProbeOptionError::InvalidPort:
            return "invalid server port";
        case WreckwaterClientProbeOptionError::InvalidPeerId:
            return "invalid peer ID";
        case WreckwaterClientProbeOptionError::InvalidAuthenticationKey:
            return "invalid authentication key";
        case WreckwaterClientProbeOptionError::MissingPeerId:
            return "missing peer ID";
        case WreckwaterClientProbeOptionError::MissingAuthenticationKey:
            return "missing authentication key";
        case WreckwaterClientProbeOptionError::InvalidIdentity:
            return "invalid session identity";
        case WreckwaterClientProbeOptionError::InvalidMaximumPumpTicks:
            return "invalid maximum tick count";
        case WreckwaterClientProbeOptionError::InvalidReconnectTick:
            return "invalid reconnect tick";
        case WreckwaterClientProbeOptionError::InvalidChaosSeed:
            return "invalid chaos seed";
    }
    return "unknown";
}

WreckwaterClientProbeOptionResult
parseWreckwaterClientProbeOptions(
    std::span<const std::string_view> arguments,
    WreckwaterClientProbeOptions& options) {
    options = {};
    uint32_t seen = 0u;
    for (size_t index = 0u; index < arguments.size(); ++index) {
        const std::string_view option = arguments[index];
        if (option == "--help" || option == "-h") {
            options.help = true;
            return {};
        }
        const uint32_t bit = optionBit(option);
        if (bit == 0u) {
            return {
                .error =
                    WreckwaterClientProbeOptionError::UnknownOption,
            };
        }
        if ((seen & bit) != 0u) {
            return {
                .error =
                    WreckwaterClientProbeOptionError::DuplicateOption,
            };
        }
        seen |= bit;
        if (index + 1u >= arguments.size()) {
            return {
                .error =
                    WreckwaterClientProbeOptionError::MissingValue,
            };
        }
        const std::string_view value = arguments[++index];
        if (bit == ServerBit) {
            if (value.empty()) {
                return {
                    .error = WreckwaterClientProbeOptionError::
                        InvalidServerAddress,
                };
            }
            options.serverAddress = value;
            continue;
        }
        if (bit == PortBit) {
            uint32_t port = 0u;
            if (!parseInteger(value, port) || port == 0u
                || port
                    > std::numeric_limits<uint16_t>::max()) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidPort,
                };
            }
            options.serverPort = static_cast<uint16_t>(port);
            continue;
        }
        if (bit == PeerBit) {
            if (!parseInteger(value, options.peerId)
                || options.peerId == 0u
                || options.peerId > kWreckwaterProbePeerCount) {
                return {
                    .error = WreckwaterClientProbeOptionError::
                        InvalidPeerId,
                };
            }
            continue;
        }
        if (bit == KeyBit) {
            if (!parseAuthenticationKey(
                    value, options.authenticationKey)) {
                return {
                    .error = WreckwaterClientProbeOptionError::
                        InvalidAuthenticationKey,
                };
            }
            continue;
        }
        if (bit == SessionBit) {
            if (!parseInteger(value, options.sessionId)
                || options.sessionId == 0u) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidIdentity,
                };
            }
            continue;
        }
        if (bit == MatchBit) {
            if (!parseInteger(value, options.matchId)
                || options.matchId == 0u) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidIdentity,
                };
            }
            continue;
        }
        if (bit == WorldBit) {
            if (!parseInteger(value, options.worldId)
                || options.worldId == 0u) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidIdentity,
                };
            }
            continue;
        }
        if (bit == WorldEpochBit) {
            if (!parseInteger(value, options.worldEpoch)
                || options.worldEpoch == 0u) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidIdentity,
                };
            }
            continue;
        }
        if (bit == AuthorityEpochBit) {
            if (!parseInteger(value, options.authorityEpoch)
                || options.authorityEpoch == 0u) {
                return {
                    .error =
                        WreckwaterClientProbeOptionError::InvalidIdentity,
                };
            }
            continue;
        }
        if (bit == MaximumTicksBit) {
            if (!parseInteger(value, options.maximumPumpTicks)
                || options.maximumPumpTicks == 0u
                || options.maximumPumpTicks
                    > kWreckwaterProbeMaximumPumpTicks) {
                return {
                    .error = WreckwaterClientProbeOptionError::
                        InvalidMaximumPumpTicks,
                };
            }
            continue;
        }
        if (bit == ChaosSeedBit) {
            if (!parseInteger(value, options.chaosSeed)) {
                return {
                    .error = WreckwaterClientProbeOptionError::
                        InvalidChaosSeed,
                };
            }
            continue;
        }
        if (!parseInteger(value, options.reconnectAtPumpTick)) {
            return {
                .error = WreckwaterClientProbeOptionError::
                    InvalidReconnectTick,
            };
        }
    }

    if ((seen & PeerBit) == 0u) {
        return {
            .error =
                WreckwaterClientProbeOptionError::MissingPeerId,
        };
    }
    if ((seen & KeyBit) == 0u) {
        return {
            .error = WreckwaterClientProbeOptionError::
                MissingAuthenticationKey,
        };
    }
    if (options.reconnectAtPumpTick >= options.maximumPumpTicks
        && options.reconnectAtPumpTick != 0u) {
        return {
            .error =
                WreckwaterClientProbeOptionError::InvalidReconnectTick,
        };
    }
    return {};
}

bool wreckwaterProbeIsHelmPeer(uint32_t peerId) noexcept {
    return peerId == 1u || peerId == 3u;
}

bool wreckwaterProbeIsDeckPeer(uint32_t peerId) noexcept {
    return peerId == 2u || peerId == 4u;
}

bool WreckwaterClientProbeSnapshotLogGate::shouldLog(
    const network::WreckwaterClientSample& sample) noexcept {
    std::optional<network::WreckwaterCargoLogicalState> cargo;
    if (const network::WreckwaterEntityState* value =
            cargoEntity(sample);
        value != nullptr) {
        cargo = value->cargo;
    }
    const bool heartbeat =
        initialized_
        && sample.authoritative.applicationTick >= applicationTick_
        && sample.authoritative.applicationTick - applicationTick_
            >= kWreckwaterProbeSnapshotHeartbeatTicks;
    const bool logicalTransition =
        !initialized_
        || sample.authoritative.phase != phase_
        || sample.authoritative.outcome != outcome_
        || sample.authoritative.winner != winner_
        || sample.authoritative.crewOneScore != crewOneScore_
        || sample.authoritative.crewTwoScore != crewTwoScore_
        || cargo != cargo_;
    if (!logicalTransition && !heartbeat) return false;

    applicationTick_ = sample.authoritative.applicationTick;
    phase_ = sample.authoritative.phase;
    outcome_ = sample.authoritative.outcome;
    winner_ = sample.authoritative.winner;
    crewOneScore_ = sample.authoritative.crewOneScore;
    crewTwoScore_ = sample.authoritative.crewTwoScore;
    cargo_ = cargo;
    initialized_ = true;
    return true;
}

WreckwaterClientProbeScript::WreckwaterClientProbeScript(
    uint32_t peerId) noexcept
    : peerId_(peerId),
      initialized_(
          wreckwaterProbeIsHelmPeer(peerId)
          || wreckwaterProbeIsDeckPeer(peerId)) {}

std::optional<WreckwaterClientProbeAction>
WreckwaterClientProbeScript::plan(
    const network::WreckwaterClientSample& sample) noexcept {
    if (!initialized_) return std::nullopt;
    if (pending_.has_value()) return pending_;
    if (sample.authoritative.snapshotSequence == 0u
        || sample.authoritative.applicationTick == 0u) {
        return std::nullopt;
    }
    if (sample.authoritative.phase != network::WreckwaterPhase::Live
        && sample.authoritative.phase
            != network::WreckwaterPhase::Overtime) {
        return std::nullopt;
    }
    pending_ = wreckwaterProbeIsHelmPeer(peerId_)
        ? planHelm(sample) : planDeck(sample);
    return pending_;
}

std::optional<WreckwaterClientProbeAction>
WreckwaterClientProbeScript::planHelm(
    const network::WreckwaterClientSample& sample) noexcept {
    const network::WreckwaterEntityState* cargo =
        cargoEntity(sample);
    if (cargo != nullptr
        && (cargo->cargo.disposition
                == network::WreckwaterCargoDisposition::Banked
            || cargo->cargo.disposition
                == network::WreckwaterCargoDisposition::Lost)) {
        // The scripted objective is complete. Quiescing helm traffic
        // leaves a bounded quiet tail before the authority closes its
        // final loopback TCP snapshot.
        return std::nullopt;
    }

    uint64_t requestedTick = 0u;
    if (!incrementTick(
            lastSuccessfulRequestTick_, requestedTick)) {
        return std::nullopt;
    }
    if (requestedTick <= sample.authoritative.applicationTick
        && !incrementTick(
            sample.authoritative.applicationTick,
            requestedTick)) {
        return std::nullopt;
    }
    const uint64_t maximumTick =
        sample.authoritative.applicationTick
            > std::numeric_limits<uint64_t>::max()
                - kWreckwaterProbeMaximumActionLeadTicks
        ? std::numeric_limits<uint64_t>::max()
        : sample.authoritative.applicationTick
            + kWreckwaterProbeMaximumActionLeadTicks;
    if (requestedTick > maximumTick) return std::nullopt;

    const uint64_t throttlePhase = (requestedTick / 30u) % 4u;
    const int16_t throttle =
        throttlePhase == 0u || throttlePhase == 3u
        ? int16_t{2'048} : int16_t{-2'048};
    const bool positiveSteering =
        ((requestedTick / 45u) % 2u == 0u)
        == (peerId_ == 1u);
    return WreckwaterClientProbeAction{
        .action = network::WreckwaterAction::Helm,
        .requestedApplicationTick = requestedTick,
        .helmThrottleQ15 = throttle,
        .helmSteeringQ15 = positiveSteering
            ? int16_t{1'024} : int16_t{-1'024},
        .sourceSnapshotSequence =
            sample.authoritative.snapshotSequence,
    };
}

std::optional<WreckwaterClientProbeAction>
WreckwaterClientProbeScript::planDeck(
    const network::WreckwaterClientSample& sample) noexcept {
    const uint64_t snapshotSequence =
        sample.authoritative.snapshotSequence;
    if (snapshotSequence
        <= lastEvaluatedDeckSnapshotSequence_) {
        return std::nullopt;
    }
    lastEvaluatedDeckSnapshotSequence_ = snapshotSequence;

    const network::WreckwaterEntityState* cargo =
        cargoEntity(sample);
    if (cargo == nullptr) return std::nullopt;
    const bool sameCargoRevision =
        cargo->cargo.generation == lastCargoAttemptGeneration_
        && cargo->cargo.revision == lastCargoAttemptRevision_;
    if (sameCargoRevision
        && (snapshotSequence < lastCargoAttemptSnapshotSequence_
            || snapshotSequence
                    - lastCargoAttemptSnapshotSequence_
                < kWreckwaterProbeCargoRetrySnapshots)) {
        return std::nullopt;
    }

    std::optional<network::WreckwaterAction> action;
    if (peerId_ == 2u) {
        if (cargo->cargo.disposition
            == network::WreckwaterCargoDisposition::Free) {
            action = network::WreckwaterAction::Tow;
        } else if (
            cargo->cargo.disposition
                == network::WreckwaterCargoDisposition::Towed
            && cargo->cargo.ownerCrew
                == network::WreckwaterCrew::CrewOne
            && alternatingRevisionTurn(
                cargo->cargo.revision, 2u, false)) {
            action = network::WreckwaterAction::Bank;
        } else if (
            cargo->cargo.disposition
                == network::WreckwaterCargoDisposition::Towed
            && cargo->cargo.ownerCrew
                == network::WreckwaterCrew::CrewTwo
            && alternatingRevisionTurn(
                cargo->cargo.revision, 3u, true)) {
            action = network::WreckwaterAction::Cut;
        }
    } else if (
        cargo->cargo.disposition
            == network::WreckwaterCargoDisposition::Towed
        && cargo->cargo.ownerCrew
            == network::WreckwaterCrew::CrewOne
        && alternatingRevisionTurn(
            cargo->cargo.revision, 2u, true)) {
        action = network::WreckwaterAction::Steal;
    } else if (
        cargo->cargo.disposition
            == network::WreckwaterCargoDisposition::Towed
        && cargo->cargo.ownerCrew
            == network::WreckwaterCrew::CrewTwo
        && alternatingRevisionTurn(
            cargo->cargo.revision, 3u, false)) {
        action = network::WreckwaterAction::Bank;
    }
    if (!action.has_value()) return std::nullopt;

    uint64_t requestedTick = 0u;
    if (!incrementTick(
            lastSuccessfulRequestTick_, requestedTick)) {
        return std::nullopt;
    }
    if (requestedTick <= sample.authoritative.applicationTick
        && !incrementTick(
            sample.authoritative.applicationTick,
            requestedTick)) {
        return std::nullopt;
    }
    return WreckwaterClientProbeAction{
        .action = *action,
        .requestedApplicationTick = requestedTick,
        .cargoId = cargo->cargo.cargoId,
        .cargoGeneration = cargo->cargo.generation,
        .observedCargoRevision = cargo->cargo.revision,
        .sourceSnapshotSequence = snapshotSequence,
    };
}

bool WreckwaterClientProbeScript::markSent(
    const WreckwaterClientProbeAction& action) noexcept {
    if (!pending_.has_value() || *pending_ != action
        || action.requestedApplicationTick
            <= lastSuccessfulRequestTick_) {
        return false;
    }
    lastSuccessfulRequestTick_ =
        action.requestedApplicationTick;
    if (action.action != network::WreckwaterAction::Helm) {
        lastCargoAttemptSnapshotSequence_ =
            action.sourceSnapshotSequence;
        lastCargoAttemptGeneration_ = action.cargoGeneration;
        lastCargoAttemptRevision_ =
            action.observedCargoRevision;
    }
    pending_.reset();
    return true;
}

bool wreckwaterClientProbeHasExactCharacterRoster(
    const network::WreckwaterClientSample& sample) noexcept {
    if (sample.characterCount != kWreckwaterProbePeerCount) {
        return false;
    }
    std::array<bool, kWreckwaterProbePeerCount> players{};
    for (uint32_t index = 0u;
         index < sample.characterCount; ++index) {
        const network::WreckwaterCharacterState& character =
            sample.characters[index].authoritativeState;
        if (!network::isCanonicalWreckwaterCharacterState(character)
            || character.characterHandle == 0u
            || character.playerId == 0u
            || character.playerId > kWreckwaterProbePeerCount
            || character.connectionGeneration == 0u
            || (character.stateFlags
                & network::kWreckwaterCharacterStateActiveFlag)
                == 0u) {
            return false;
        }
        const size_t playerIndex = character.playerId - 1u;
        if (players[playerIndex]) return false;
        players[playerIndex] = true;
    }
    return std::all_of(
        players.begin(), players.end(),
        [](bool present) { return present; });
}

uint64_t wreckwaterClientProbeCharacterRosterHash(
    const network::WreckwaterClientSample& sample) noexcept {
    if (!wreckwaterClientProbeHasExactCharacterRoster(sample)) {
        return 0u;
    }
    uint64_t hash = 14'695'981'039'346'656'037ull;
    hashU32(hash, sample.characterCount);
    for (uint64_t playerId = 1u;
         playerId <= kWreckwaterProbePeerCount; ++playerId) {
        const network::WreckwaterCharacterState* character =
            characterForPlayer(sample, playerId);
        if (character == nullptr) return 0u;
        hashCharacter(hash, *character);
    }
    return hash != 0u ? hash : 1u;
}

WreckwaterClientProbeCharacterScript::
    WreckwaterClientProbeCharacterScript(uint32_t peerId) noexcept
    : peerId_(peerId),
      initialized_(
          peerId != 0u && peerId <= kWreckwaterProbePeerCount) {}

std::optional<WreckwaterClientProbeCharacterInput>
WreckwaterClientProbeCharacterScript::plan(
    const network::WreckwaterClientSample& sample) noexcept {
    if (!initialized_) return std::nullopt;
    if (pending_.has_value()) return pending_;
    if (sample.authoritative.snapshotSequence == 0u
        || sample.authoritative.physicsEvidenceTick == 0u
        || sample.authoritative.snapshotSequence
            <= lastEvaluatedSnapshotSequence_) {
        return std::nullopt;
    }
    lastEvaluatedSnapshotSequence_ =
        sample.authoritative.snapshotSequence;
    // Start during warmup. The simulated proof skiffs can become steep
    // enough to shed passengers before Live; waiting would certify only
    // swimming input rather than deck-local movement.
    if (sample.authoritative.phase
        == network::WreckwaterPhase::Finished) {
        return std::nullopt;
    }
    if (!wreckwaterClientProbeHasExactCharacterRoster(sample)) {
        return std::nullopt;
    }
    const network::WreckwaterCharacterState* local =
        characterForPlayer(sample, peerId_);
    if (local == nullptr
        || (local->stateFlags
            & network::kWreckwaterCharacterStateConnectedFlag)
            == 0u) {
        return std::nullopt;
    }
    if (sample.authoritative.physicsEvidenceTick
        > network::kWreckwaterMaximumApplicationTick
            - kWreckwaterProbeCharacterTargetLeadTicks) {
        return std::nullopt;
    }
    const uint64_t requestedTick =
        sample.authoritative.physicsEvidenceTick
        + kWreckwaterProbeCharacterTargetLeadTicks;
    if (requestedTick <= lastSuccessfulRequestTick_) {
        return std::nullopt;
    }

    int16_t moveX = 0;
    int16_t moveZ = 0;
    switch (peerId_) {
        case 1u:
            moveX = kWreckwaterProbeCharacterMoveQ15;
            break;
        case 2u:
            moveX = -kWreckwaterProbeCharacterMoveQ15;
            break;
        case 3u:
            moveZ = kWreckwaterProbeCharacterMoveQ15;
            break;
        case 4u:
            moveZ = -kWreckwaterProbeCharacterMoveQ15;
            break;
        default:
            return std::nullopt;
    }
    pending_ = WreckwaterClientProbeCharacterInput{
        .requestedApplicationTick = requestedTick,
        .moveXQ15 = moveX,
        .moveZQ15 = moveZ,
        .sourceSnapshotSequence =
            sample.authoritative.snapshotSequence,
        .sourceEvidenceTick =
            sample.authoritative.physicsEvidenceTick,
        .sourceConnectionGeneration =
            local->connectionGeneration,
    };
    return pending_;
}

bool WreckwaterClientProbeCharacterScript::markSent(
    const WreckwaterClientProbeCharacterInput& input) noexcept {
    if (!pending_.has_value() || *pending_ != input
        || input.requestedApplicationTick
            <= lastSuccessfulRequestTick_) {
        return false;
    }
    lastSuccessfulRequestTick_ =
        input.requestedApplicationTick;
    pending_.reset();
    return true;
}

WreckwaterClientProbeFinalSnapshot
wreckwaterClientProbeFinalSnapshot(
    const network::WreckwaterClientSample& sample) noexcept {
    return {
        .sequence = sample.authoritative.snapshotSequence,
        .applicationTick = sample.authoritative.applicationTick,
        .evidenceTick =
            sample.authoritative.physicsEvidenceTick,
        .phase = sample.authoritative.phase,
        .outcome = sample.authoritative.outcome,
        .winner = sample.authoritative.winner,
        .crewOneScore = sample.authoritative.crewOneScore,
        .crewTwoScore = sample.authoritative.crewTwoScore,
        .stateHash = sample.authoritative.matchStateHash,
        .eventHash = sample.authoritative.eventStreamHash,
        .serializedHash =
            sample.authoritative.serializedByteHash,
    };
}

const char* wreckwaterProbeActionName(
    network::WreckwaterAction action) noexcept {
    switch (action) {
        case network::WreckwaterAction::Tow: return "tow";
        case network::WreckwaterAction::Cut: return "cut";
        case network::WreckwaterAction::Steal: return "steal";
        case network::WreckwaterAction::Bank: return "bank";
        case network::WreckwaterAction::Helm: return "helm";
    }
    return "unknown";
}

const char* wreckwaterProbePhaseName(
    network::WreckwaterPhase phase) noexcept {
    switch (phase) {
        case network::WreckwaterPhase::Warmup: return "warmup";
        case network::WreckwaterPhase::Live: return "live";
        case network::WreckwaterPhase::Overtime: return "overtime";
        case network::WreckwaterPhase::Finished: return "finished";
    }
    return "unknown";
}

const char* wreckwaterProbeOutcomeName(
    network::WreckwaterOutcomeType outcome) noexcept {
    switch (outcome) {
        case network::WreckwaterOutcomeType::Undecided:
            return "undecided";
        case network::WreckwaterOutcomeType::CrewVictory:
            return "crew_victory";
        case network::WreckwaterOutcomeType::Tie:
            return "tie";
    }
    return "unknown";
}

const char* wreckwaterProbeCrewName(
    network::WreckwaterCrew crew) noexcept {
    switch (crew) {
        case network::WreckwaterCrew::None: return "none";
        case network::WreckwaterCrew::CrewOne: return "crew1";
        case network::WreckwaterCrew::CrewTwo: return "crew2";
    }
    return "unknown";
}

} // namespace voxy::client

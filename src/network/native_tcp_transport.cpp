#include "network/native_tcp_transport.hpp"

#include "network/protocol.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string_view>
#include <utility>

#if !defined(__EMSCRIPTEN__) \
    && (defined(__linux__) || defined(__APPLE__) || defined(__unix__))
#define VOXY_HAS_POSIX_TCP_TRANSPORT 1
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#else
#define VOXY_HAS_POSIX_TCP_TRANSPORT 0
#endif

namespace voxy::network {

#if VOXY_HAS_POSIX_TCP_TRANSPORT
namespace {

constexpr uint32_t kWireMagic = 0x31545856u; // "VXT1", little endian.
constexpr uint16_t kWireVersion = kNativeTcpWireVersion;
constexpr size_t kWireHeaderBytes = 12u;
constexpr size_t kHelloPayloadBytes =
    sizeof(uint32_t) + kNativeTcpAuthenticationKeyBytes
    + kNativeTcpContentDigestBytes;
constexpr size_t kAcceptPayloadBytes =
    sizeof(uint32_t) + sizeof(uint64_t)
    + kNativeTcpContentDigestBytes;
constexpr size_t kHelloKeyOffset = sizeof(uint32_t);
constexpr size_t kHelloDigestOffset =
    kHelloKeyOffset + kNativeTcpAuthenticationKeyBytes;
constexpr size_t kAcceptDigestOffset =
    sizeof(uint32_t) + sizeof(uint64_t);

enum class WireKind : uint8_t {
    ClientHello = 1u,
    ServerAccepted = 2u,
    Data = 3u,
};

enum class ParseDisposition {
    Consumed,
    ConsumedStop,
    Blocked,
    Fatal,
};

enum class ConnectionPhase {
    AwaitingHello,
    AwaitingAccepted,
    AwaitingAdmission,
    Authenticated,
};

struct WireHeader {
    WireKind kind = WireKind::Data;
    uint8_t delivery = 0u;
    uint32_t payloadBytes = 0u;
};

struct QueuedWrite {
    std::vector<std::byte> bytes;
    size_t offset = 0u;
    bool dataFrame = false;
    bool latestRealtime = false;
};

struct SocketConnection {
    int fd = -1;
    uint64_t serial = 0u;
    uint32_t peerId = 0u;
    ConnectionPhase phase = ConnectionPhase::AwaitingHello;
    uint32_t handshakeServiceCalls = 0u;
    std::vector<std::byte> receiveBuffer;
    size_t receiveOffset = 0u;
    std::deque<QueuedWrite> writes;
    size_t queuedWriteBytes = 0u;
    uint32_t queuedInboundFrames = 0u;
    size_t queuedInboundBytes = 0u;
    bool disconnectEventReserved = false;
};

struct InboundFrame {
    MultiplayerTransportFrame frame;
};

void setError(std::string* output, std::string_view message) {
    if (output != nullptr) *output = message;
}

void setCreateStatus(
    NativeTcpClientCreateStatus* output,
    NativeTcpClientCreateStatus status) noexcept {
    if (output != nullptr) *output = status;
}

[[nodiscard]] bool privateIpv4Address(
    const uint8_t* bytes) noexcept {
    return bytes[0] == 127u
        || bytes[0] == 10u
        || (bytes[0] == 172u
            && bytes[1] >= 16u && bytes[1] <= 31u)
        || (bytes[0] == 192u && bytes[1] == 168u)
        || (bytes[0] == 169u && bytes[1] == 254u);
}

[[nodiscard]] bool ipv4MappedIpv6Address(
    const uint8_t* bytes) noexcept {
    for (size_t index = 0u; index < 10u; ++index) {
        if (bytes[index] != 0u) return false;
    }
    return bytes[10] == 0xffu && bytes[11] == 0xffu;
}

[[nodiscard]] NativeTcpIpAddressClass classifySockaddr(
    const sockaddr* address, socklen_t addressBytes) noexcept {
    if (address == nullptr) {
        return NativeTcpIpAddressClass::Invalid;
    }
    if (address->sa_family == AF_INET) {
        if (addressBytes < sizeof(sockaddr_in)) {
            return NativeTcpIpAddressClass::Invalid;
        }
        const auto* ipv4 =
            reinterpret_cast<const sockaddr_in*>(address);
        const auto* bytes = reinterpret_cast<const uint8_t*>(
            &ipv4->sin_addr);
        return privateIpv4Address(bytes)
            ? NativeTcpIpAddressClass::Private
            : NativeTcpIpAddressClass::Public;
    }
    if (address->sa_family == AF_INET6) {
        if (addressBytes < sizeof(sockaddr_in6)) {
            return NativeTcpIpAddressClass::Invalid;
        }
        const auto* ipv6 =
            reinterpret_cast<const sockaddr_in6*>(address);
        const auto* bytes = reinterpret_cast<const uint8_t*>(
            &ipv6->sin6_addr);
        bool loopback = true;
        for (size_t index = 0u; index < 15u; ++index) {
            loopback = loopback && bytes[index] == 0u;
        }
        loopback = loopback && bytes[15] == 1u;
        const bool uniqueLocal = (bytes[0] & 0xfeu) == 0xfcu;
        const bool linkLocal =
            bytes[0] == 0xfeu
            && (bytes[1] & 0xc0u) == 0x80u;
        const bool mappedPrivate =
            ipv4MappedIpv6Address(bytes)
            && privateIpv4Address(bytes + 12u);
        return loopback || uniqueLocal || linkLocal || mappedPrivate
            ? NativeTcpIpAddressClass::Private
            : NativeTcpIpAddressClass::Public;
    }
    return NativeTcpIpAddressClass::Invalid;
}

void appendU16(std::vector<std::byte>& output, uint16_t value) {
    output.push_back(std::byte{static_cast<uint8_t>(value)});
    output.push_back(
        std::byte{static_cast<uint8_t>(value >> 8u)});
}

void appendU32(std::vector<std::byte>& output, uint32_t value) {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        output.push_back(
            std::byte{static_cast<uint8_t>(value >> shift)});
    }
}

void appendU64(std::vector<std::byte>& output, uint64_t value) {
    for (uint32_t shift = 0u; shift < 64u; shift += 8u) {
        output.push_back(
            std::byte{static_cast<uint8_t>(value >> shift)});
    }
}

bool readU16(
    std::span<const std::byte> bytes, size_t offset,
    uint16_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 2u)
        return false;
    value = static_cast<uint16_t>(
        std::to_integer<uint16_t>(bytes[offset])
        | (std::to_integer<uint16_t>(bytes[offset + 1u]) << 8u));
    return true;
}

bool readU32(
    std::span<const std::byte> bytes, size_t offset,
    uint32_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4u)
        return false;
    value = 0u;
    for (uint32_t byte = 0u; byte < 4u; ++byte) {
        value |= std::to_integer<uint32_t>(bytes[offset + byte])
            << (byte * 8u);
    }
    return true;
}

bool readU64(
    std::span<const std::byte> bytes, size_t offset,
    uint64_t& value) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 8u)
        return false;
    value = 0u;
    for (uint32_t byte = 0u; byte < 8u; ++byte) {
        value |= std::to_integer<uint64_t>(bytes[offset + byte])
            << (byte * 8u);
    }
    return true;
}

std::vector<std::byte> makeWireFrame(
    WireKind kind, uint8_t delivery,
    std::span<const std::byte> payload) {
    std::vector<std::byte> output;
    output.reserve(kWireHeaderBytes + payload.size());
    appendU32(output, kWireMagic);
    appendU16(output, kWireVersion);
    output.push_back(std::byte{static_cast<uint8_t>(kind)});
    output.push_back(std::byte{delivery});
    appendU32(output, static_cast<uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::byte> makeHello(
    const NativeTcpPeerCredential& credential,
    const NativeTcpContentDigest& contentDigest) {
    std::vector<std::byte> payload;
    payload.reserve(kHelloPayloadBytes);
    appendU32(payload, credential.peerId);
    payload.insert(
        payload.end(), credential.authenticationKey.begin(),
        credential.authenticationKey.end());
    payload.insert(
        payload.end(), contentDigest.begin(), contentDigest.end());
    return makeWireFrame(WireKind::ClientHello, 0u, payload);
}

std::vector<std::byte> makeAccepted(
    uint32_t peerId, uint64_t connectionSerial,
    const NativeTcpContentDigest& contentDigest) {
    std::vector<std::byte> payload;
    payload.reserve(kAcceptPayloadBytes);
    appendU32(payload, peerId);
    appendU64(payload, connectionSerial);
    payload.insert(
        payload.end(), contentDigest.begin(), contentDigest.end());
    return makeWireFrame(WireKind::ServerAccepted, 0u, payload);
}

uint8_t wireDelivery(DeliveryClass delivery) noexcept {
    return static_cast<uint8_t>(delivery);
}

std::optional<DeliveryClass> deliveryFromWire(uint8_t value) noexcept {
    switch (static_cast<DeliveryClass>(value)) {
        case DeliveryClass::Realtime:
            return DeliveryClass::Realtime;
        case DeliveryClass::ReliableEvent:
            return DeliveryClass::ReliableEvent;
        case DeliveryClass::ReliableControl:
            return DeliveryClass::ReliableControl;
    }
    return std::nullopt;
}

template <size_t Size>
bool bytesAreNonzero(
    const std::array<std::byte, Size>& bytes) noexcept {
    uint8_t aggregate = 0u;
    for (const std::byte value : bytes)
        aggregate |= std::to_integer<uint8_t>(value);
    return aggregate != 0u;
}

template <size_t Size>
bool constantTimeBytesEqual(
    const std::array<std::byte, Size>& lhs,
    const std::array<std::byte, Size>& rhs) noexcept {
    uint8_t difference = 0u;
    for (size_t byte = 0u; byte < lhs.size(); ++byte) {
        difference |= static_cast<uint8_t>(
            std::to_integer<uint8_t>(lhs[byte])
            ^ std::to_integer<uint8_t>(rhs[byte]));
    }
    return difference == 0u;
}

bool validLimits(
    const NativeTcpTransportLimits& limits,
    std::string* error) {
    if (limits.maximumPeers == 0u
        || limits.maximumPeers > 4'096u
        || limits.maximumPendingConnections == 0u
        || limits.maximumPendingConnections > 4'096u) {
        setError(error, "invalid native TCP peer limits");
        return false;
    }
    if (limits.maximumFrameBytes == 0u
        || limits.maximumFrameBytes > kMaximumReliableFrameBytes
        || limits.maximumFrameBytes
            > std::numeric_limits<uint32_t>::max()) {
        setError(error, "invalid native TCP frame limit");
        return false;
    }
    if (limits.maximumQueuedFramesPerPeer == 0u
        || limits.maximumQueuedBytesPerPeer
            < kWireHeaderBytes + kHelloPayloadBytes
        || limits.maximumQueuedInboundFramesPerPeer == 0u
        || limits.maximumQueuedInboundBytesPerPeer
            < limits.maximumFrameBytes
        || limits.maximumInboundFrames == 0u
        || limits.maximumInboundBytes < limits.maximumFrameBytes
        || static_cast<uint64_t>(
               limits.maximumLifecycleEvents)
            < 2u * static_cast<uint64_t>(limits.maximumPeers)
                + 1u) {
        setError(error, "invalid native TCP queue limits");
        return false;
    }
    if (limits.maximumAcceptsPerService == 0u
        || limits.maximumFramesPerPeerPerService == 0u
        || limits.maximumHandshakeServiceCalls == 0u
        || limits.maximumReadBytesPerPeerPerService == 0u
        || limits.maximumWriteBytesPerPeerPerService == 0u) {
        setError(error, "invalid native TCP service budgets");
        return false;
    }
    return true;
}

bool validPayloadSize(
    DeliveryClass delivery, size_t bytes,
    const NativeTcpTransportLimits& limits) noexcept {
    if (bytes == 0u || bytes > limits.maximumFrameBytes)
        return false;
    switch (delivery) {
        case DeliveryClass::Realtime:
            return bytes <= kConservativeRealtimeMtu;
        case DeliveryClass::ReliableEvent:
        case DeliveryClass::ReliableControl:
            return bytes <= kMaximumReliableFrameBytes;
    }
    return false;
}

void closeFd(int& fd) noexcept {
    if (fd >= 0) {
        static_cast<void>(::close(fd));
        fd = -1;
    }
}

bool configureSocket(int fd) noexcept {
    const int oldFlags = ::fcntl(fd, F_GETFL, 0);
    if (oldFlags < 0
        || ::fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) < 0) {
        return false;
    }
    const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0
        || ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
        return false;
    }
    const int enabled = 1;
    if (::setsockopt(
            fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
            static_cast<socklen_t>(sizeof(enabled))) < 0) {
        return false;
    }
#if defined(SO_NOSIGPIPE)
    if (::setsockopt(
            fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
            static_cast<socklen_t>(sizeof(enabled))) < 0) {
        return false;
    }
#endif
    return true;
}

int noSignalSendFlags() noexcept {
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

bool wouldBlock(int error) noexcept {
#if EAGAIN == EWOULDBLOCK
    return error == EAGAIN;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

bool queueWrite(
    SocketConnection& connection, std::vector<std::byte> bytes,
    bool dataFrame, const NativeTcpTransportLimits& limits,
    NativeTcpTransportTelemetry& telemetry,
    bool latestRealtime = false) {
    if (bytes.empty()
        || connection.writes.size()
            >= limits.maximumQueuedFramesPerPeer
        || bytes.size() > limits.maximumQueuedBytesPerPeer
        || connection.queuedWriteBytes
            > limits.maximumQueuedBytesPerPeer - bytes.size()) {
        ++telemetry.outboundBackpressure;
        return false;
    }
    connection.queuedWriteBytes += bytes.size();
    ++telemetry.queuedOutboundFrames;
    telemetry.queuedOutboundBytes += bytes.size();
    connection.writes.push_back(
        {std::move(bytes), 0u, dataFrame, latestRealtime});
    return true;
}

void discardSupersededRealtimeWrites(
    SocketConnection& connection,
    NativeTcpTransportTelemetry& telemetry) noexcept {
    for (auto iterator = connection.writes.begin();
         iterator != connection.writes.end();) {
        if (!iterator->latestRealtime || iterator->offset != 0u) {
            ++iterator;
            continue;
        }
        const size_t bytes = iterator->bytes.size();
        connection.queuedWriteBytes -= bytes;
        --telemetry.queuedOutboundFrames;
        telemetry.queuedOutboundBytes -= bytes;
        ++telemetry.supersededOutboundFrames;
        telemetry.supersededOutboundBytes += bytes;
        iterator = connection.writes.erase(iterator);
    }
}

void discardWrites(
    SocketConnection& connection,
    NativeTcpTransportTelemetry& telemetry) noexcept {
    const auto frameCount = connection.writes.size();
    telemetry.queuedOutboundFrames =
        telemetry.queuedOutboundFrames >= frameCount
        ? telemetry.queuedOutboundFrames - frameCount : 0u;
    const uint64_t byteCount =
        static_cast<uint64_t>(connection.queuedWriteBytes);
    telemetry.queuedOutboundBytes =
        telemetry.queuedOutboundBytes >= byteCount
        ? telemetry.queuedOutboundBytes - byteCount : 0u;
    connection.writes.clear();
    connection.queuedWriteBytes = 0u;
}

void discardConnection(
    SocketConnection& connection,
    NativeTcpTransportTelemetry& telemetry) noexcept {
    discardWrites(connection, telemetry);
    closeFd(connection.fd);
    connection.receiveBuffer.clear();
    connection.receiveOffset = 0u;
    connection.queuedInboundFrames = 0u;
    connection.queuedInboundBytes = 0u;
}

bool flushWrites(
    SocketConnection& connection,
    const NativeTcpTransportLimits& limits,
    NativeTcpTransportTelemetry& telemetry) {
    size_t budget = limits.maximumWriteBytesPerPeerPerService;
    while (budget > 0u && !connection.writes.empty()) {
        QueuedWrite& write = connection.writes.front();
        const size_t remaining = write.bytes.size() - write.offset;
        const size_t attempt = std::min(remaining, budget);
        const ssize_t sent = ::send(
            connection.fd, write.bytes.data() + write.offset,
            attempt, noSignalSendFlags());
        if (sent < 0) {
            if (wouldBlock(errno)) {
                ++telemetry.sendWouldBlock;
                return true;
            }
            ++telemetry.socketErrors;
            return false;
        }
        if (sent == 0) {
            ++telemetry.socketErrors;
            return false;
        }
        const size_t sentBytes = static_cast<size_t>(sent);
        write.offset += sentBytes;
        budget -= sentBytes;
        telemetry.sentWireBytes += sentBytes;
        if (write.offset != write.bytes.size()) {
            ++telemetry.partialWrites;
            continue;
        }
        const size_t completedBytes = write.bytes.size();
        const bool completedDataFrame = write.dataFrame;
        connection.queuedWriteBytes -= completedBytes;
        --telemetry.queuedOutboundFrames;
        telemetry.queuedOutboundBytes -= completedBytes;
        connection.writes.pop_front();
        if (completedDataFrame) ++telemetry.sentFrames;
    }
    return true;
}

void compactReceiveBuffer(SocketConnection& connection) {
    if (connection.receiveOffset == 0u) return;
    if (connection.receiveOffset == connection.receiveBuffer.size()) {
        connection.receiveBuffer.clear();
        connection.receiveOffset = 0u;
        return;
    }
    if (connection.receiveOffset
        >= connection.receiveBuffer.size() / 2u) {
        connection.receiveBuffer.erase(
            connection.receiveBuffer.begin(),
            connection.receiveBuffer.begin()
                + static_cast<std::ptrdiff_t>(
                    connection.receiveOffset));
        connection.receiveOffset = 0u;
    }
}

bool decodeWireHeader(
    std::span<const std::byte> bytes, WireHeader& header,
    NativeTcpTransportTelemetry& telemetry,
    const NativeTcpTransportLimits& limits) {
    uint32_t magic = 0u;
    uint16_t version = 0u;
    uint32_t payloadBytes = 0u;
    if (!readU32(bytes, 0u, magic)
        || !readU16(bytes, 4u, version)
        || !readU32(bytes, 8u, payloadBytes)) {
        ++telemetry.malformedFrames;
        return false;
    }
    if (magic != kWireMagic || version != kWireVersion) {
        ++telemetry.malformedFrames;
        return false;
    }
    const uint8_t kindValue =
        std::to_integer<uint8_t>(bytes[6u]);
    const uint8_t delivery =
        std::to_integer<uint8_t>(bytes[7u]);
    if (kindValue < static_cast<uint8_t>(WireKind::ClientHello)
        || kindValue > static_cast<uint8_t>(WireKind::Data)) {
        ++telemetry.malformedFrames;
        return false;
    }
    header.kind = static_cast<WireKind>(kindValue);
    header.delivery = delivery;
    header.payloadBytes = payloadBytes;

    if (header.kind == WireKind::ClientHello) {
        if (delivery != 0u
            || payloadBytes != kHelloPayloadBytes) {
            ++telemetry.malformedFrames;
            return false;
        }
        return true;
    }
    if (header.kind == WireKind::ServerAccepted) {
        if (delivery != 0u
            || payloadBytes != kAcceptPayloadBytes) {
            ++telemetry.malformedFrames;
            return false;
        }
        return true;
    }
    const auto decodedDelivery = deliveryFromWire(delivery);
    if (!decodedDelivery.has_value() || payloadBytes == 0u) {
        ++telemetry.malformedFrames;
        return false;
    }
    if (payloadBytes > limits.maximumFrameBytes
        || (*decodedDelivery == DeliveryClass::Realtime
            && payloadBytes > kConservativeRealtimeMtu)
        || payloadBytes > kMaximumReliableFrameBytes) {
        ++telemetry.oversizedFrames;
        return false;
    }
    return true;
}

template <typename FrameHandler>
bool receiveFrames(
    SocketConnection& connection,
    const NativeTcpTransportLimits& limits,
    NativeTcpTransportTelemetry& telemetry,
    FrameHandler&& handler) {
    uint32_t processedFrames = 0u;
    bool parserBlocked = false;
    bool parserStopped = false;

    const auto parseBuffered = [&]() -> bool {
        parserBlocked = false;
        parserStopped = false;
        while (processedFrames
               < limits.maximumFramesPerPeerPerService) {
            const size_t available =
                connection.receiveBuffer.size()
                - connection.receiveOffset;
            if (available < kWireHeaderBytes) return true;
            const auto unconsumed = std::span<const std::byte>(
                connection.receiveBuffer.data()
                    + connection.receiveOffset,
                available);
            WireHeader header;
            if (!decodeWireHeader(
                    unconsumed.first(kWireHeaderBytes), header,
                    telemetry, limits)) {
                return false;
            }
            const size_t totalBytes =
                kWireHeaderBytes
                + static_cast<size_t>(header.payloadBytes);
            if (available < totalBytes) return true;
            const auto payload = unconsumed.subspan(
                kWireHeaderBytes,
                static_cast<size_t>(header.payloadBytes));
            const ParseDisposition disposition =
                handler(header, payload);
            if (disposition == ParseDisposition::Fatal)
                return false;
            if (disposition == ParseDisposition::Blocked) {
                ++telemetry.inboundBackpressure;
                parserBlocked = true;
                return true;
            }
            connection.receiveOffset += totalBytes;
            ++processedFrames;
            compactReceiveBuffer(connection);
            if (disposition == ParseDisposition::ConsumedStop) {
                parserStopped = true;
                return true;
            }
        }
        return true;
    };

    if (!parseBuffered()) return false;
    if (parserBlocked || parserStopped
        || processedFrames
            >= limits.maximumFramesPerPeerPerService) {
        return true;
    }

    size_t readBudget = limits.maximumReadBytesPerPeerPerService;
    constexpr size_t kReadChunkBytes = 16u * 1024u;
    while (readBudget > 0u
           && processedFrames
               < limits.maximumFramesPerPeerPerService) {
        compactReceiveBuffer(connection);
        const size_t buffered =
            connection.receiveBuffer.size()
            - connection.receiveOffset;
        const size_t maximumBuffered =
            std::max(
                limits.maximumFrameBytes,
                kHelloPayloadBytes)
            + kWireHeaderBytes;
        if (buffered >= maximumBuffered) {
            ++telemetry.malformedFrames;
            return false;
        }
        const size_t availableCapacity =
            maximumBuffered - buffered;
        const size_t attempt = std::min(
            {readBudget, availableCapacity, kReadChunkBytes});
        std::array<std::byte, kReadChunkBytes> scratch{};
        const ssize_t received = ::recv(
            connection.fd, scratch.data(), attempt, 0);
        if (received < 0) {
            if (wouldBlock(errno)) {
                ++telemetry.receiveWouldBlock;
                return true;
            }
            ++telemetry.socketErrors;
            return false;
        }
        if (received == 0) return false;
        const size_t receivedBytes =
            static_cast<size_t>(received);
        connection.receiveBuffer.insert(
            connection.receiveBuffer.end(), scratch.begin(),
            scratch.begin()
                + static_cast<std::ptrdiff_t>(receivedBytes));
        readBudget -= receivedBytes;
        telemetry.receivedWireBytes += receivedBytes;
        if (!parseBuffered()) return false;
        if (parserBlocked || parserStopped) return true;
    }
    return true;
}

bool createListeningSocket(
    const NativeTcpServerConfig& config, int& outputFd,
    uint16_t& outputPort, std::string* error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;
    const std::string service = std::to_string(config.port);
    addrinfo* addresses = nullptr;
    const int addressResult = ::getaddrinfo(
        config.bindAddress.c_str(), service.c_str(),
        &hints, &addresses);
    if (addressResult != 0 || addresses == nullptr) {
        setError(error, "native TCP bind address resolution failed");
        return false;
    }

    int listener = -1;
    for (const addrinfo* address = addresses;
         address != nullptr; address = address->ai_next) {
        listener = ::socket(
            address->ai_family, address->ai_socktype,
            address->ai_protocol);
        if (listener < 0) continue;
        const int enabled = 1;
        static_cast<void>(::setsockopt(
            listener, SOL_SOCKET, SO_REUSEADDR, &enabled,
            static_cast<socklen_t>(sizeof(enabled))));
        const int oldFlags = ::fcntl(listener, F_GETFL, 0);
        const int descriptorFlags = ::fcntl(listener, F_GETFD, 0);
        if (oldFlags < 0 || descriptorFlags < 0
            || ::fcntl(
                   listener, F_SETFL,
                   oldFlags | O_NONBLOCK) < 0
            || ::fcntl(
                   listener, F_SETFD,
                   descriptorFlags | FD_CLOEXEC) < 0
            || ::bind(
                   listener, address->ai_addr,
                   address->ai_addrlen) < 0
            || ::listen(listener, config.listenBacklog) < 0) {
            closeFd(listener);
            continue;
        }
        break;
    }
    ::freeaddrinfo(addresses);
    if (listener < 0) {
        setError(error, "native TCP listen failed");
        return false;
    }

    sockaddr_storage localAddress{};
    socklen_t localLength =
        static_cast<socklen_t>(sizeof(localAddress));
    if (::getsockname(
            listener,
            reinterpret_cast<sockaddr*>(&localAddress),
            &localLength) < 0) {
        closeFd(listener);
        setError(error, "native TCP local address query failed");
        return false;
    }
    uint16_t port = 0u;
    if (localAddress.ss_family == AF_INET) {
        const auto* address =
            reinterpret_cast<const sockaddr_in*>(&localAddress);
        port = ntohs(address->sin_port);
    } else if (localAddress.ss_family == AF_INET6) {
        const auto* address =
            reinterpret_cast<const sockaddr_in6*>(&localAddress);
        port = ntohs(address->sin6_port);
    }
    if (port == 0u) {
        closeFd(listener);
        setError(error, "native TCP listener has no port");
        return false;
    }
    outputFd = listener;
    outputPort = port;
    return true;
}

bool createClientSocket(
    const NativeTcpClientConfig& config, int& outputFd,
    bool& connectedImmediately, std::string* error,
    NativeTcpClientCreateStatus* status) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;
    const std::string service =
        std::to_string(config.serverPort);
    addrinfo* addresses = nullptr;
    const int addressResult = ::getaddrinfo(
        config.serverAddress.c_str(), service.c_str(),
        &hints, &addresses);
    if (addressResult != 0 || addresses == nullptr) {
        setError(error, "native TCP server address resolution failed");
        setCreateStatus(
            status,
            NativeTcpClientCreateStatus::AddressResolutionFailed);
        return false;
    }

    int socketFd = -1;
    bool immediate = false;
    bool privateCandidateFound = false;
    for (const addrinfo* address = addresses;
         address != nullptr; address = address->ai_next) {
        if (config.requirePrivateServerAddress) {
            const NativeTcpIpAddressClass addressClass =
                classifySockaddr(
                    address->ai_addr, address->ai_addrlen);
            if (addressClass
                != NativeTcpIpAddressClass::Private) {
                continue;
            }
            privateCandidateFound = true;
        }
        socketFd = ::socket(
            address->ai_family, address->ai_socktype,
            address->ai_protocol);
        if (socketFd < 0) continue;
        if (!configureSocket(socketFd)) {
            closeFd(socketFd);
            continue;
        }
        if (::connect(
                socketFd, address->ai_addr,
                address->ai_addrlen) == 0) {
            immediate = true;
            break;
        }
        if (errno == EINPROGRESS) break;
        closeFd(socketFd);
    }
    ::freeaddrinfo(addresses);
    if (socketFd < 0) {
        if (config.requirePrivateServerAddress
            && !privateCandidateFound) {
            setError(
                error,
                "native TCP private-address policy rejected every "
                "resolved server address");
            setCreateStatus(
                status,
                NativeTcpClientCreateStatus::
                    PrivateAddressPolicyRejected);
        } else {
            setError(error, "native TCP connect failed");
            setCreateStatus(
                status,
                NativeTcpClientCreateStatus::ConnectFailed);
        }
        return false;
    }
    outputFd = socketFd;
    connectedImmediately = immediate;
    return true;
}

} // namespace

NativeTcpIpAddressClass classifyNativeTcpIpAddress(
    std::string_view address) noexcept {
    if (address.empty()
        || address.find('\0') != std::string_view::npos) {
        return NativeTcpIpAddressClass::Invalid;
    }
    const std::string terminated(address);
    sockaddr_in ipv4{};
    ipv4.sin_family = AF_INET;
    if (::inet_pton(
            AF_INET, terminated.c_str(),
            &ipv4.sin_addr) == 1) {
        return classifySockaddr(
            reinterpret_cast<const sockaddr*>(&ipv4),
            static_cast<socklen_t>(sizeof(ipv4)));
    }
    sockaddr_in6 ipv6{};
    ipv6.sin6_family = AF_INET6;
    if (::inet_pton(
            AF_INET6, terminated.c_str(),
            &ipv6.sin6_addr) == 1) {
        return classifySockaddr(
            reinterpret_cast<const sockaddr*>(&ipv6),
            static_cast<socklen_t>(sizeof(ipv6)));
    }
    return NativeTcpIpAddressClass::Invalid;
}

const char* nativeTcpClientCreateStatusName(
    NativeTcpClientCreateStatus status) noexcept {
    switch (status) {
        case NativeTcpClientCreateStatus::Created:
            return "created";
        case NativeTcpClientCreateStatus::InvalidConfiguration:
            return "invalid configuration";
        case NativeTcpClientCreateStatus::AddressResolutionFailed:
            return "address resolution failed";
        case NativeTcpClientCreateStatus::
                PrivateAddressPolicyRejected:
            return "private-address policy rejected";
        case NativeTcpClientCreateStatus::ConnectFailed:
            return "connect failed";
        case NativeTcpClientCreateStatus::
                AuthenticationQueueFailed:
            return "authentication queue failed";
        case NativeTcpClientCreateStatus::TransportUnavailable:
            return "transport unavailable";
    }
    return "unknown";
}

namespace {

bool connectCompleted(int fd, bool& failed) noexcept {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = POLLOUT;
    const int result = ::poll(&descriptor, 1u, 0);
    if (result == 0) return false;
    if (result < 0) {
        if (errno == EINTR) return false;
        failed = true;
        return false;
    }
    int socketError = 0;
    socklen_t errorLength =
        static_cast<socklen_t>(sizeof(socketError));
    if (::getsockopt(
            fd, SOL_SOCKET, SO_ERROR, &socketError,
            &errorLength) < 0
        || socketError != 0) {
        failed = true;
        return false;
    }
    return true;
}

} // namespace

struct NativeTcpServerTransport::State {
    NativeTcpServerConfig config;
    int listener = -1;
    uint16_t boundPort = 0u;
    uint64_t nextConnectionSerial = 1u;
    uint64_t reservedLifecycleEvents = 0u;
    bool connectionSerialsExhausted = false;
    bool closed = false;
    std::vector<SocketConnection> pending;
    std::vector<SocketConnection> active;
    std::vector<uint32_t> authenticatedBefore;
    std::deque<InboundFrame> inbound;
    NativeTcpTransportTelemetry telemetry;

    const NativeTcpPeerCredential* credential(
        uint32_t peerId) const noexcept {
        const auto found = std::lower_bound(
            config.peers.begin(), config.peers.end(), peerId,
            [](const NativeTcpPeerCredential& candidate,
               uint32_t id) {
                return candidate.peerId < id;
            });
        return found != config.peers.end()
                && found->peerId == peerId
            ? &*found : nullptr;
    }

    SocketConnection* connection(uint32_t peerId) noexcept {
        const auto found = std::lower_bound(
            active.begin(), active.end(), peerId,
            [](const SocketConnection& candidate, uint32_t id) {
                return candidate.peerId < id;
            });
        return found != active.end() && found->peerId == peerId
            ? &*found : nullptr;
    }

    [[nodiscard]] bool canReserveConnectionLifecycle() const noexcept {
        const uint64_t used =
            telemetry.queuedLifecycleEvents
            + reservedLifecycleEvents;
        return used <= config.limits.maximumLifecycleEvents
            && config.limits.maximumLifecycleEvents - used >= 2u;
    }

    void queueLifecycle(
        MultiplayerTransportFrameType type, uint32_t peerId,
        uint64_t serial) {
        MultiplayerTransportFrame frame;
        frame.peerId = peerId;
        frame.delivery = DeliveryClass::ReliableControl;
        frame.type = type;
        frame.connectionSerial = serial;
        inbound.push_back({std::move(frame)});
        ++telemetry.queuedLifecycleEvents;
    }

    void publishConnected(SocketConnection& connection) {
        queueLifecycle(
            MultiplayerTransportFrameType::Connected,
            connection.peerId, connection.serial);
        connection.disconnectEventReserved = true;
        ++reservedLifecycleEvents;
    }

    void publishConnectionRequest(SocketConnection& connection) {
        queueLifecycle(
            MultiplayerTransportFrameType::ConnectionRequested,
            connection.peerId, connection.serial);
        connection.disconnectEventReserved = true;
        ++reservedLifecycleEvents;
    }

    void releaseDisconnectReservation(SocketConnection& connection) noexcept {
        if (!connection.disconnectEventReserved) return;
        connection.disconnectEventReserved = false;
        if (reservedLifecycleEvents != 0u) --reservedLifecycleEvents;
    }

    void cancelConnectionRequest(SocketConnection& connection) noexcept {
        for (auto iterator = inbound.begin(); iterator != inbound.end();) {
            if (iterator->frame.type
                    != MultiplayerTransportFrameType::ConnectionRequested
                || iterator->frame.peerId != connection.peerId
                || iterator->frame.connectionSerial != connection.serial) {
                ++iterator;
                continue;
            }
            if (telemetry.queuedLifecycleEvents != 0u)
                --telemetry.queuedLifecycleEvents;
            iterator = inbound.erase(iterator);
        }
        releaseDisconnectReservation(connection);
    }

    void publishDisconnected(SocketConnection& connection) {
        if (!connection.disconnectEventReserved
            || reservedLifecycleEvents == 0u) {
            ++telemetry.lifecycleBackpressure;
            return;
        }
        connection.disconnectEventReserved = false;
        --reservedLifecycleEvents;
        queueLifecycle(
            MultiplayerTransportFrameType::Disconnected,
            connection.peerId, connection.serial);
    }

    void removeInboundDataForPeer(uint32_t peerId) {
        for (auto iterator = inbound.begin();
             iterator != inbound.end();) {
            if (!iterator->frame.isData()
                || iterator->frame.peerId != peerId) {
                ++iterator;
                continue;
            }
            --telemetry.queuedInboundFrames;
            telemetry.queuedInboundBytes -=
                iterator->frame.bytes.size();
            iterator = inbound.erase(iterator);
        }
    }

    void disconnectActive(size_t index) {
        publishDisconnected(active[index]);
        discardConnection(active[index], telemetry);
        active.erase(
            active.begin() + static_cast<std::ptrdiff_t>(index));
        ++telemetry.disconnectedPeers;
        telemetry.connectedPeers =
            static_cast<uint32_t>(active.size());
    }

    bool queueInbound(
        SocketConnection& connection,
        DeliveryClass delivery,
        std::span<const std::byte> payload) {
        if (connection.queuedInboundFrames
                >= config.limits
                    .maximumQueuedInboundFramesPerPeer
            || payload.size()
                > config.limits.maximumQueuedInboundBytesPerPeer
            || connection.queuedInboundBytes
                > config.limits
                    .maximumQueuedInboundBytesPerPeer
                    - payload.size()
            || telemetry.queuedInboundFrames
                >= config.limits.maximumInboundFrames
            || payload.size() > config.limits.maximumInboundBytes
            || telemetry.queuedInboundBytes
                > config.limits.maximumInboundBytes
                    - payload.size()) {
            return false;
        }
        MultiplayerTransportFrame frame;
        frame.peerId = connection.peerId;
        frame.delivery = delivery;
        frame.bytes.assign(payload.begin(), payload.end());
        frame.connectionSerial = connection.serial;
        inbound.push_back({std::move(frame)});
        ++connection.queuedInboundFrames;
        connection.queuedInboundBytes += payload.size();
        ++telemetry.receivedFrames;
        ++telemetry.queuedInboundFrames;
        telemetry.queuedInboundBytes += payload.size();
        return true;
    }

    ParseDisposition handleFrame(
        SocketConnection& connection, const WireHeader& header,
        std::span<const std::byte> payload) {
        if (connection.phase == ConnectionPhase::AwaitingHello) {
            if (header.kind != WireKind::ClientHello) {
                ++telemetry.malformedFrames;
                return ParseDisposition::Fatal;
            }
            uint32_t peerId = 0u;
            if (!readU32(payload, 0u, peerId)) {
                ++telemetry.malformedFrames;
                return ParseDisposition::Fatal;
            }
            NativeTcpAuthenticationKey suppliedKey{};
            std::copy_n(
                payload.begin()
                    + static_cast<std::ptrdiff_t>(kHelloKeyOffset),
                kNativeTcpAuthenticationKeyBytes,
                suppliedKey.begin());
            NativeTcpContentDigest suppliedDigest{};
            std::copy_n(
                payload.begin()
                    + static_cast<std::ptrdiff_t>(
                        kHelloDigestOffset),
                kNativeTcpContentDigestBytes,
                suppliedDigest.begin());
            const NativeTcpPeerCredential* expected =
                credential(peerId);
            const NativeTcpAuthenticationKey zeroKey{};
            const NativeTcpAuthenticationKey& comparison =
                expected != nullptr
                ? expected->authenticationKey : zeroKey;
            const bool credentialMatches =
                expected != nullptr
                && constantTimeBytesEqual(
                    suppliedKey, comparison);
            const bool contentMatches =
                constantTimeBytesEqual(
                    suppliedDigest,
                    config.expectedContentDigest);
            if (!credentialMatches)
                ++telemetry.authenticationFailures;
            if (!contentMatches)
                ++telemetry.contentDigestMismatches;
            if (!credentialMatches || !contentMatches) {
                return ParseDisposition::Fatal;
            }
            connection.peerId = peerId;
            if (config.requireExplicitAdmission) {
                if (!canReserveConnectionLifecycle()) {
                    ++telemetry.lifecycleBackpressure;
                    return ParseDisposition::Fatal;
                }
                connection.phase = ConnectionPhase::AwaitingAdmission;
                publishConnectionRequest(connection);
            } else {
                connection.phase = ConnectionPhase::Authenticated;
                if (!queueWrite(
                        connection,
                        makeAccepted(
                            peerId, connection.serial,
                            config.expectedContentDigest),
                        false,
                        config.limits, telemetry)) {
                    return ParseDisposition::Fatal;
                }
            }
            // Promote before parsing a coalesced Data frame so Connected is
            // always observable first.
            return ParseDisposition::ConsumedStop;
        }
        if (connection.phase != ConnectionPhase::Authenticated
            || header.kind != WireKind::Data) {
            ++telemetry.malformedFrames;
            return ParseDisposition::Fatal;
        }
        const auto delivery =
            deliveryFromWire(header.delivery);
        if (!delivery.has_value()) {
            ++telemetry.malformedFrames;
            return ParseDisposition::Fatal;
        }
        return queueInbound(connection, *delivery, payload)
            ? ParseDisposition::Consumed
            : ParseDisposition::Blocked;
    }

    void promotePending(size_t& index) {
        SocketConnection connectionToPromote =
            std::move(pending[index]);
        pending.erase(
            pending.begin() + static_cast<std::ptrdiff_t>(index));
        const uint32_t promotedPeerId =
            connectionToPromote.peerId;

        if (!canReserveConnectionLifecycle()) {
            ++telemetry.lifecycleBackpressure;
            ++telemetry.rejectedSockets;
            discardConnection(connectionToPromote, telemetry);
            return;
        }

        const auto prior = std::lower_bound(
            authenticatedBefore.begin(),
            authenticatedBefore.end(), promotedPeerId);
        const bool authenticatedPreviously =
            prior != authenticatedBefore.end()
            && *prior == promotedPeerId;
        if (authenticatedPreviously)
            removeInboundDataForPeer(promotedPeerId);

        auto activePosition = std::lower_bound(
            active.begin(), active.end(),
            connectionToPromote.peerId,
            [](const SocketConnection& candidate, uint32_t id) {
                return candidate.peerId < id;
            });
        if (activePosition != active.end()
            && activePosition->peerId
                == connectionToPromote.peerId) {
            publishDisconnected(*activePosition);
            discardConnection(*activePosition, telemetry);
            ++telemetry.disconnectedPeers;
            *activePosition = std::move(connectionToPromote);
        } else {
            active.insert(
                activePosition, std::move(connectionToPromote));
        }

        SocketConnection* promoted =
            connection(promotedPeerId);
        if (promoted == nullptr) {
            ++telemetry.lifecycleBackpressure;
            return;
        }
        publishConnected(*promoted);

        if (authenticatedPreviously) {
            ++telemetry.reconnects;
        } else {
            authenticatedBefore.insert(prior, promoted->peerId);
        }
        ++telemetry.authenticatedConnections;
        telemetry.connectedPeers =
            static_cast<uint32_t>(active.size());
        telemetry.peakConnectedPeers = std::max<uint64_t>(
            telemetry.peakConnectedPeers,
            active.size());
    }

    [[nodiscard]] bool acceptPending(
        uint32_t peerId, uint64_t serial) {
        const auto candidate = std::find_if(
            pending.begin(), pending.end(),
            [peerId, serial](const SocketConnection& connection) {
                return connection.phase == ConnectionPhase::AwaitingAdmission
                    && connection.peerId == peerId
                    && connection.serial == serial;
            });
        if (candidate == pending.end()) return false;
        if (!queueWrite(
                *candidate,
                makeAccepted(peerId, serial, config.expectedContentDigest),
                false, config.limits, telemetry)) {
            return false;
        }

        SocketConnection accepted = std::move(*candidate);
        pending.erase(candidate);
        accepted.phase = ConnectionPhase::Authenticated;
        removeInboundDataForPeer(peerId);

        auto activePosition = std::lower_bound(
            active.begin(), active.end(), peerId,
            [](const SocketConnection& connection, uint32_t id) {
                return connection.peerId < id;
            });
        const bool replacing = activePosition != active.end()
            && activePosition->peerId == peerId;
        if (replacing) {
            // Authority has atomically accepted the new lifetime. Retire the
            // old socket without publishing a contradictory intermediate
            // disconnect to the already-committed authority session.
            releaseDisconnectReservation(*activePosition);
            discardConnection(*activePosition, telemetry);
            *activePosition = std::move(accepted);
            ++telemetry.disconnectedPeers;
            ++telemetry.reconnects;
        } else {
            active.insert(activePosition, std::move(accepted));
        }
        const auto prior = std::lower_bound(
            authenticatedBefore.begin(), authenticatedBefore.end(), peerId);
        if (prior == authenticatedBefore.end() || *prior != peerId)
            authenticatedBefore.insert(prior, peerId);
        ++telemetry.authenticatedConnections;
        telemetry.connectedPeers = static_cast<uint32_t>(active.size());
        telemetry.peakConnectedPeers = std::max<uint64_t>(
            telemetry.peakConnectedPeers, active.size());
        return true;
    }

    void rejectPending(uint32_t peerId, uint64_t serial) noexcept {
        const auto candidate = std::find_if(
            pending.begin(), pending.end(),
            [peerId, serial](const SocketConnection& connection) {
                return connection.phase == ConnectionPhase::AwaitingAdmission
                    && connection.peerId == peerId
                    && connection.serial == serial;
            });
        if (candidate == pending.end()) return;
        releaseDisconnectReservation(*candidate);
        discardConnection(*candidate, telemetry);
        pending.erase(candidate);
        ++telemetry.rejectedSockets;
    }

    void acceptSockets() {
        for (uint32_t accepted = 0u;
             accepted < config.limits.maximumAcceptsPerService;
             ++accepted) {
            sockaddr_storage remoteAddress{};
            socklen_t remoteLength =
                static_cast<socklen_t>(sizeof(remoteAddress));
#if defined(__linux__)
            int fd = ::accept4(
                listener,
                reinterpret_cast<sockaddr*>(&remoteAddress),
                &remoteLength, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
            int fd = ::accept(
                listener,
                reinterpret_cast<sockaddr*>(&remoteAddress),
                &remoteLength);
#endif
            if (fd < 0) {
                if (wouldBlock(errno)) return;
                if (errno == EINTR) continue;
                ++telemetry.socketErrors;
                return;
            }
            ++telemetry.acceptedSockets;
#if !defined(__linux__)
            if (!configureSocket(fd)) {
                closeFd(fd);
                ++telemetry.rejectedSockets;
                continue;
            }
#else
            const int enabled = 1;
            if (::setsockopt(
                    fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
                    static_cast<socklen_t>(sizeof(enabled))) < 0) {
                closeFd(fd);
                ++telemetry.rejectedSockets;
                continue;
            }
#if defined(SO_NOSIGPIPE)
            static_cast<void>(::setsockopt(
                fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                static_cast<socklen_t>(sizeof(enabled))));
#endif
#endif
            if (pending.size()
                    >= config.limits.maximumPendingConnections
                || connectionSerialsExhausted) {
                closeFd(fd);
                ++telemetry.rejectedSockets;
                if (connectionSerialsExhausted)
                    ++telemetry.serialExhaustions;
                continue;
            }
            SocketConnection connection;
            connection.fd = fd;
            connection.serial = nextConnectionSerial;
            if (nextConnectionSerial
                == std::numeric_limits<uint64_t>::max()) {
                connectionSerialsExhausted = true;
            } else {
                ++nextConnectionSerial;
            }
            connection.phase = ConnectionPhase::AwaitingHello;
            pending.push_back(std::move(connection));
        }
    }

    void service() {
        if (closed || listener < 0) return;
        acceptSockets();

        for (size_t index = 0u; index < pending.size();) {
            SocketConnection& connection = pending[index];
            ++connection.handshakeServiceCalls;
            const bool alive =
                connection.handshakeServiceCalls
                    <= config.limits.maximumHandshakeServiceCalls
                && flushWrites(
                    connection, config.limits, telemetry)
                && receiveFrames(
                    connection, config.limits, telemetry,
                    [this, &connection](
                        const WireHeader& header,
                        std::span<const std::byte> payload) {
                        return handleFrame(
                            connection, header, payload);
                    });
            if (!alive) {
                if (connection.phase == ConnectionPhase::AwaitingAdmission)
                    cancelConnectionRequest(connection);
                discardConnection(connection, telemetry);
                pending.erase(
                    pending.begin()
                        + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            if (connection.phase
                == ConnectionPhase::Authenticated) {
                promotePending(index);
                continue;
            }
            ++index;
        }

        for (size_t index = 0u; index < active.size();) {
            SocketConnection& connection = active[index];
            const bool alive =
                flushWrites(
                    connection, config.limits, telemetry)
                && receiveFrames(
                    connection, config.limits, telemetry,
                    [this, &connection](
                        const WireHeader& header,
                        std::span<const std::byte> payload) {
                        return handleFrame(
                            connection, header, payload);
                    });
            if (!alive) {
                disconnectActive(index);
                continue;
            }
            ++index;
        }
    }

    void close() {
        if (closed) return;
        closed = true;
        closeFd(listener);
        for (auto& connection : pending)
            discardConnection(connection, telemetry);
        for (auto& connection : active) {
            discardConnection(connection, telemetry);
            ++telemetry.disconnectedPeers;
        }
        pending.clear();
        active.clear();
        inbound.clear();
        reservedLifecycleEvents = 0u;
        telemetry.queuedInboundFrames = 0u;
        telemetry.queuedInboundBytes = 0u;
        telemetry.queuedLifecycleEvents = 0u;
        telemetry.connectedPeers = 0u;
    }
};

NativeTcpServerTransport::NativeTcpServerTransport(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}

NativeTcpServerTransport::~NativeTcpServerTransport() { close(); }

std::unique_ptr<NativeTcpServerTransport>
NativeTcpServerTransport::create(
    NativeTcpServerConfig config, std::string* error) {
    if (error != nullptr) error->clear();
    if (!validLimits(config.limits, error)
        || config.bindAddress.empty()
        || config.listenBacklog <= 0
        || config.listenBacklog > 4'096
        || config.firstConnectionSerial == 0u
        || !bytesAreNonzero(config.expectedContentDigest)
        || config.peers.empty()
        || config.peers.size() > config.limits.maximumPeers) {
        if (error != nullptr && error->empty())
            *error = "invalid native TCP server configuration";
        return nullptr;
    }
    std::sort(
        config.peers.begin(), config.peers.end(),
        [](const NativeTcpPeerCredential& lhs,
           const NativeTcpPeerCredential& rhs) {
            return lhs.peerId < rhs.peerId;
        });
    for (size_t peer = 0u; peer < config.peers.size(); ++peer) {
        if (config.peers[peer].peerId == 0u
            || !bytesAreNonzero(
                config.peers[peer].authenticationKey)
            || (peer > 0u
                && config.peers[peer - 1u].peerId
                    == config.peers[peer].peerId)) {
            setError(
                error,
                "invalid native TCP peer credential set");
            return nullptr;
        }
    }
    std::vector<NativeTcpAuthenticationKey> uniqueKeys;
    uniqueKeys.reserve(config.peers.size());
    for (const auto& peer : config.peers)
        uniqueKeys.push_back(peer.authenticationKey);
    std::sort(uniqueKeys.begin(), uniqueKeys.end());
    if (std::adjacent_find(
            uniqueKeys.begin(), uniqueKeys.end())
        != uniqueKeys.end()) {
        setError(
            error,
            "native TCP peer keys must be unique");
        return nullptr;
    }

    auto state = std::make_unique<State>();
    state->config = std::move(config);
    state->nextConnectionSerial =
        state->config.firstConnectionSerial;
    if (!createListeningSocket(
            state->config, state->listener,
            state->boundPort, error)) {
        return nullptr;
    }
    if (error != nullptr) error->clear();
    return std::unique_ptr<NativeTcpServerTransport>(
        new NativeTcpServerTransport(std::move(state)));
}

void NativeTcpServerTransport::service() {
    if (state_) state_->service();
}

bool NativeTcpServerTransport::send(
    uint32_t peerId, DeliveryClass delivery,
    std::span<const std::byte> bytes) {
    if (!state_ || state_->closed
        || !validPayloadSize(delivery, bytes.size(),
                            state_->config.limits)) {
        if (state_) ++state_->telemetry.outboundBackpressure;
        return false;
    }
    SocketConnection* connection =
        state_->connection(peerId);
    if (connection == nullptr) return false;
    return queueWrite(
        *connection,
        makeWireFrame(
            WireKind::Data, wireDelivery(delivery), bytes),
        true, state_->config.limits, state_->telemetry);
}

bool NativeTcpServerTransport::sendLatestRealtime(
    uint32_t peerId, uint64_t connectionSerial,
    std::span<const std::byte> bytes) {
    if (!state_ || state_->closed || connectionSerial == 0u
        || !validPayloadSize(
            DeliveryClass::Realtime, bytes.size(), state_->config.limits)) {
        if (state_) ++state_->telemetry.outboundBackpressure;
        return false;
    }
    SocketConnection* connection = state_->connection(peerId);
    if (connection == nullptr
        || connection->serial != connectionSerial) return false;
    discardSupersededRealtimeWrites(*connection, state_->telemetry);
    return queueWrite(
        *connection,
        makeWireFrame(
            WireKind::Data, wireDelivery(DeliveryClass::Realtime), bytes),
        true, state_->config.limits, state_->telemetry, true);
}

bool NativeTcpServerTransport::acceptConnection(
    uint32_t peerId, uint64_t connectionSerial) {
    return state_ && !state_->closed
        && state_->acceptPending(peerId, connectionSerial);
}

void NativeTcpServerTransport::rejectConnection(
    uint32_t peerId, uint64_t connectionSerial) {
    if (state_ && !state_->closed)
        state_->rejectPending(peerId, connectionSerial);
}

bool NativeTcpServerTransport::send(
    uint32_t peerId, uint64_t connectionSerial,
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    if (!state_ || state_->closed || connectionSerial == 0u
        || !validPayloadSize(
            delivery, bytes.size(), state_->config.limits)) {
        if (state_) ++state_->telemetry.outboundBackpressure;
        return false;
    }
    SocketConnection* connection =
        state_->connection(peerId);
    if (connection == nullptr
        || connection->serial != connectionSerial) {
        return false;
    }
    return queueWrite(
        *connection,
        makeWireFrame(
            WireKind::Data, wireDelivery(delivery), bytes),
        true, state_->config.limits, state_->telemetry);
}

std::optional<MultiplayerTransportFrame>
NativeTcpServerTransport::poll() {
    if (!state_ || state_->closed) return std::nullopt;
    if (state_->inbound.empty()) return std::nullopt;
    InboundFrame entry = std::move(state_->inbound.front());
    state_->inbound.pop_front();
    if (entry.frame.isData()) {
        const auto connection = std::find_if(
            state_->active.begin(), state_->active.end(),
            [&entry](const SocketConnection& candidate) {
                return candidate.serial
                    == entry.frame.connectionSerial;
            });
        if (connection != state_->active.end()) {
            if (connection->queuedInboundFrames > 0u)
                --connection->queuedInboundFrames;
            connection->queuedInboundBytes =
                connection->queuedInboundBytes
                        >= entry.frame.bytes.size()
                ? connection->queuedInboundBytes
                    - entry.frame.bytes.size()
                : 0u;
        }
        --state_->telemetry.queuedInboundFrames;
        state_->telemetry.queuedInboundBytes -=
            entry.frame.bytes.size();
    } else {
        --state_->telemetry.queuedLifecycleEvents;
    }
    return std::move(entry.frame);
}

void NativeTcpServerTransport::close() {
    if (state_) state_->close();
}

uint16_t NativeTcpServerTransport::listeningPort() const noexcept {
    return state_ ? state_->boundPort : 0u;
}

bool NativeTcpServerTransport::connected(
    uint32_t peerId) const noexcept {
    return state_ && !state_->closed
        && state_->connection(peerId) != nullptr;
}

uint64_t NativeTcpServerTransport::connectionSerial(
    uint32_t peerId) const noexcept {
    if (!state_ || state_->closed) return 0u;
    const SocketConnection* connection =
        state_->connection(peerId);
    return connection != nullptr ? connection->serial : 0u;
}

std::vector<uint32_t>
NativeTcpServerTransport::connectedPeerIds() const {
    std::vector<uint32_t> peers;
    if (!state_ || state_->closed) return peers;
    peers.reserve(state_->active.size());
    for (const auto& connection : state_->active)
        peers.push_back(connection.peerId);
    return peers;
}

const NativeTcpTransportTelemetry&
NativeTcpServerTransport::telemetry() const noexcept {
    static const NativeTcpTransportTelemetry empty{};
    return state_ ? state_->telemetry : empty;
}

struct NativeTcpClientTransport::State {
    NativeTcpClientConfig config;
    SocketConnection connection;
    NativeTcpClientState state = NativeTcpClientState::Connecting;
    bool closed = false;
    uint32_t connectServiceCalls = 0u;
    uint64_t reservedLifecycleEvents = 0u;
    std::deque<InboundFrame> inbound;
    NativeTcpTransportTelemetry telemetry;

    void queueLifecycle(MultiplayerTransportFrameType type) {
        MultiplayerTransportFrame frame;
        frame.peerId = 0u;
        frame.delivery = DeliveryClass::ReliableControl;
        frame.type = type;
        frame.connectionSerial = connection.serial;
        inbound.push_back({std::move(frame)});
        ++telemetry.queuedLifecycleEvents;
    }

    [[nodiscard]] bool publishConnected() {
        const uint64_t used =
            telemetry.queuedLifecycleEvents
            + reservedLifecycleEvents;
        if (used > config.limits.maximumLifecycleEvents
            || config.limits.maximumLifecycleEvents - used < 2u) {
            ++telemetry.lifecycleBackpressure;
            return false;
        }
        queueLifecycle(MultiplayerTransportFrameType::Connected);
        connection.disconnectEventReserved = true;
        ++reservedLifecycleEvents;
        return true;
    }

    void publishDisconnected() {
        if (!connection.disconnectEventReserved
            || reservedLifecycleEvents == 0u) {
            ++telemetry.lifecycleBackpressure;
            return;
        }
        connection.disconnectEventReserved = false;
        --reservedLifecycleEvents;
        queueLifecycle(MultiplayerTransportFrameType::Disconnected);
    }

    void fail() {
        if (state == NativeTcpClientState::Connected) {
            publishDisconnected();
            ++telemetry.disconnectedPeers;
        }
        discardConnection(connection, telemetry);
        telemetry.connectedPeers = 0u;
        state = NativeTcpClientState::Failed;
    }

    bool beginAuthentication() {
        connection.phase = ConnectionPhase::AwaitingAccepted;
        state = NativeTcpClientState::Authenticating;
        return queueWrite(
            connection,
            makeHello(
                config.credential,
                config.expectedContentDigest),
            false,
            config.limits, telemetry);
    }

    bool queueInbound(
        DeliveryClass delivery,
        std::span<const std::byte> payload) {
        if (connection.queuedInboundFrames
                >= config.limits
                    .maximumQueuedInboundFramesPerPeer
            || payload.size()
                > config.limits.maximumQueuedInboundBytesPerPeer
            || connection.queuedInboundBytes
                > config.limits
                    .maximumQueuedInboundBytesPerPeer
                    - payload.size()
            || telemetry.queuedInboundFrames
                >= config.limits.maximumInboundFrames
            || payload.size() > config.limits.maximumInboundBytes
            || telemetry.queuedInboundBytes
                > config.limits.maximumInboundBytes
                    - payload.size()) {
            return false;
        }
        MultiplayerTransportFrame frame;
        frame.peerId = 0u;
        frame.delivery = delivery;
        frame.bytes.assign(payload.begin(), payload.end());
        frame.connectionSerial = connection.serial;
        inbound.push_back({std::move(frame)});
        ++connection.queuedInboundFrames;
        connection.queuedInboundBytes += payload.size();
        ++telemetry.receivedFrames;
        ++telemetry.queuedInboundFrames;
        telemetry.queuedInboundBytes += payload.size();
        return true;
    }

    ParseDisposition handleFrame(
        const WireHeader& header,
        std::span<const std::byte> payload) {
        if (state == NativeTcpClientState::Authenticating) {
            if (header.kind != WireKind::ServerAccepted) {
                ++telemetry.malformedFrames;
                return ParseDisposition::Fatal;
            }
            uint32_t acceptedPeer = 0u;
            uint64_t acceptedSerial = 0u;
            if (!readU32(payload, 0u, acceptedPeer)
                || !readU64(
                    payload, sizeof(uint32_t), acceptedSerial)) {
                ++telemetry.authenticationFailures;
                return ParseDisposition::Fatal;
            }
            NativeTcpContentDigest acceptedDigest{};
            std::copy_n(
                payload.begin()
                    + static_cast<std::ptrdiff_t>(
                        kAcceptDigestOffset),
                kNativeTcpContentDigestBytes,
                acceptedDigest.begin());
            const bool acceptanceMatches =
                acceptedPeer == config.credential.peerId
                && acceptedSerial != 0u;
            const bool contentMatches =
                constantTimeBytesEqual(
                    acceptedDigest,
                    config.expectedContentDigest);
            if (!acceptanceMatches)
                ++telemetry.authenticationFailures;
            if (!contentMatches)
                ++telemetry.contentDigestMismatches;
            if (!acceptanceMatches || !contentMatches)
                return ParseDisposition::Fatal;
            connection.serial = acceptedSerial;
            if (!publishConnected())
                return ParseDisposition::Fatal;
            connection.phase = ConnectionPhase::Authenticated;
            state = NativeTcpClientState::Connected;
            ++telemetry.authenticatedConnections;
            telemetry.connectedPeers = 1u;
            telemetry.peakConnectedPeers = 1u;
            return ParseDisposition::Consumed;
        }
        if (state != NativeTcpClientState::Connected
            || header.kind != WireKind::Data) {
            ++telemetry.malformedFrames;
            return ParseDisposition::Fatal;
        }
        const auto delivery =
            deliveryFromWire(header.delivery);
        if (!delivery.has_value()) {
            ++telemetry.malformedFrames;
            return ParseDisposition::Fatal;
        }
        return queueInbound(*delivery, payload)
            ? ParseDisposition::Consumed
            : ParseDisposition::Blocked;
    }

    void service() {
        if (closed || state == NativeTcpClientState::Disconnected
            || state == NativeTcpClientState::Failed) {
            return;
        }
        ++connectServiceCalls;
        if (connectServiceCalls
            > config.limits.maximumHandshakeServiceCalls) {
            fail();
            return;
        }
        if (state == NativeTcpClientState::Connecting) {
            bool failed = false;
            if (!connectCompleted(connection.fd, failed)) {
                if (failed) {
                    ++telemetry.socketErrors;
                    fail();
                }
                return;
            }
            if (!beginAuthentication()) {
                fail();
                return;
            }
        }
        const bool alive =
            flushWrites(connection, config.limits, telemetry)
            && receiveFrames(
                connection, config.limits, telemetry,
                [this](
                    const WireHeader& header,
                    std::span<const std::byte> payload) {
                    return handleFrame(header, payload);
                });
        if (!alive) {
            fail();
            return;
        }
        if (state == NativeTcpClientState::Connected)
            connectServiceCalls = 0u;
    }

    void close() {
        if (closed) return;
        closed = true;
        if (state == NativeTcpClientState::Connected)
            ++telemetry.disconnectedPeers;
        discardConnection(connection, telemetry);
        inbound.clear();
        reservedLifecycleEvents = 0u;
        telemetry.queuedInboundFrames = 0u;
        telemetry.queuedInboundBytes = 0u;
        telemetry.queuedLifecycleEvents = 0u;
        telemetry.connectedPeers = 0u;
        state = NativeTcpClientState::Disconnected;
    }
};

NativeTcpClientTransport::NativeTcpClientTransport(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}

NativeTcpClientTransport::~NativeTcpClientTransport() { close(); }

std::unique_ptr<NativeTcpClientTransport>
NativeTcpClientTransport::create(
    NativeTcpClientConfig config, std::string* error,
    NativeTcpClientCreateStatus* status) {
    if (error != nullptr) error->clear();
    setCreateStatus(
        status,
        NativeTcpClientCreateStatus::InvalidConfiguration);
    if (!validLimits(config.limits, error)
        || config.serverAddress.empty()
        || config.serverPort == 0u
        || config.credential.peerId == 0u
        || !bytesAreNonzero(config.expectedContentDigest)
        || !bytesAreNonzero(
            config.credential.authenticationKey)) {
        if (error != nullptr && error->empty())
            *error = "invalid native TCP client configuration";
        return nullptr;
    }
    auto state = std::make_unique<State>();
    state->config = std::move(config);
    state->connection.serial = 0u;
    state->connection.phase = ConnectionPhase::AwaitingAccepted;
    bool connectedImmediately = false;
    if (!createClientSocket(
            state->config, state->connection.fd,
            connectedImmediately, error, status)) {
        return nullptr;
    }
    if (connectedImmediately && !state->beginAuthentication()) {
        state->close();
        setError(error, "native TCP authentication queue failed");
        setCreateStatus(
            status,
            NativeTcpClientCreateStatus::
                AuthenticationQueueFailed);
        return nullptr;
    }
    if (error != nullptr) error->clear();
    setCreateStatus(
        status, NativeTcpClientCreateStatus::Created);
    return std::unique_ptr<NativeTcpClientTransport>(
        new NativeTcpClientTransport(std::move(state)));
}

void NativeTcpClientTransport::service() {
    if (state_) state_->service();
}

bool NativeTcpClientTransport::send(
    uint32_t peerId, DeliveryClass delivery,
    std::span<const std::byte> bytes) {
    if (!state_ || state_->closed || peerId != 0u
        || state_->state != NativeTcpClientState::Connected
        || !validPayloadSize(
            delivery, bytes.size(), state_->config.limits)) {
        if (state_) ++state_->telemetry.outboundBackpressure;
        return false;
    }
    return queueWrite(
        state_->connection,
        makeWireFrame(
            WireKind::Data, wireDelivery(delivery), bytes),
        true, state_->config.limits, state_->telemetry);
}

bool NativeTcpClientTransport::send(
    uint32_t peerId, uint64_t connectionSerial,
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    if (!state_ || connectionSerial == 0u
        || connectionSerial != state_->connection.serial) {
        return false;
    }
    return send(peerId, delivery, bytes);
}

std::optional<MultiplayerTransportFrame>
NativeTcpClientTransport::poll() {
    if (!state_ || state_->closed) return std::nullopt;
    if (state_->inbound.empty()) return std::nullopt;
    InboundFrame entry = std::move(state_->inbound.front());
    state_->inbound.pop_front();
    if (entry.frame.isData()) {
        if (state_->connection.queuedInboundFrames > 0u)
            --state_->connection.queuedInboundFrames;
        state_->connection.queuedInboundBytes =
            state_->connection.queuedInboundBytes
                    >= entry.frame.bytes.size()
            ? state_->connection.queuedInboundBytes
                - entry.frame.bytes.size()
            : 0u;
        --state_->telemetry.queuedInboundFrames;
        state_->telemetry.queuedInboundBytes -=
            entry.frame.bytes.size();
    } else {
        --state_->telemetry.queuedLifecycleEvents;
    }
    return std::move(entry.frame);
}

void NativeTcpClientTransport::close() {
    if (state_) state_->close();
}

NativeTcpClientState
NativeTcpClientTransport::state() const noexcept {
    return state_ ? state_->state
                  : NativeTcpClientState::Disconnected;
}

uint64_t NativeTcpClientTransport::connectionSerial() const noexcept {
    return state_
            && state_->state == NativeTcpClientState::Connected
        ? state_->connection.serial : 0u;
}

const NativeTcpTransportTelemetry&
NativeTcpClientTransport::telemetry() const noexcept {
    static const NativeTcpTransportTelemetry empty{};
    return state_ ? state_->telemetry : empty;
}

#else

NativeTcpIpAddressClass classifyNativeTcpIpAddress(
    std::string_view) noexcept {
    return NativeTcpIpAddressClass::Invalid;
}

const char* nativeTcpClientCreateStatusName(
    NativeTcpClientCreateStatus status) noexcept {
    switch (status) {
        case NativeTcpClientCreateStatus::Created:
            return "created";
        case NativeTcpClientCreateStatus::InvalidConfiguration:
            return "invalid configuration";
        case NativeTcpClientCreateStatus::AddressResolutionFailed:
            return "address resolution failed";
        case NativeTcpClientCreateStatus::
                PrivateAddressPolicyRejected:
            return "private-address policy rejected";
        case NativeTcpClientCreateStatus::ConnectFailed:
            return "connect failed";
        case NativeTcpClientCreateStatus::
                AuthenticationQueueFailed:
            return "authentication queue failed";
        case NativeTcpClientCreateStatus::TransportUnavailable:
            return "transport unavailable";
    }
    return "unknown";
}

struct NativeTcpServerTransport::State {
    NativeTcpTransportTelemetry telemetry;
};

struct NativeTcpClientTransport::State {
    NativeTcpClientState state = NativeTcpClientState::Disconnected;
    NativeTcpTransportTelemetry telemetry;
};

NativeTcpServerTransport::NativeTcpServerTransport(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}
NativeTcpServerTransport::~NativeTcpServerTransport() = default;
std::unique_ptr<NativeTcpServerTransport>
NativeTcpServerTransport::create(
    NativeTcpServerConfig, std::string* error) {
    if (error != nullptr)
        *error = "native TCP transport is unavailable";
    return nullptr;
}
void NativeTcpServerTransport::service() {}
bool NativeTcpServerTransport::send(
    uint32_t, DeliveryClass, std::span<const std::byte>) {
    return false;
}
bool NativeTcpServerTransport::send(
    uint32_t, uint64_t, DeliveryClass,
    std::span<const std::byte>) {
    return false;
}
bool NativeTcpServerTransport::sendLatestRealtime(
    uint32_t, uint64_t, std::span<const std::byte>) {
    return false;
}
bool NativeTcpServerTransport::acceptConnection(uint32_t, uint64_t) {
    return false;
}
void NativeTcpServerTransport::rejectConnection(uint32_t, uint64_t) {}
std::optional<MultiplayerTransportFrame>
NativeTcpServerTransport::poll() {
    return std::nullopt;
}
void NativeTcpServerTransport::close() {}
uint16_t NativeTcpServerTransport::listeningPort() const noexcept {
    return 0u;
}
bool NativeTcpServerTransport::connected(uint32_t) const noexcept {
    return false;
}
uint64_t NativeTcpServerTransport::connectionSerial(
    uint32_t) const noexcept {
    return 0u;
}
std::vector<uint32_t>
NativeTcpServerTransport::connectedPeerIds() const {
    return {};
}
const NativeTcpTransportTelemetry&
NativeTcpServerTransport::telemetry() const noexcept {
    static const NativeTcpTransportTelemetry empty{};
    return state_ ? state_->telemetry : empty;
}

NativeTcpClientTransport::NativeTcpClientTransport(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}
NativeTcpClientTransport::~NativeTcpClientTransport() = default;
std::unique_ptr<NativeTcpClientTransport>
NativeTcpClientTransport::create(
    NativeTcpClientConfig, std::string* error,
    NativeTcpClientCreateStatus* status) {
    if (error != nullptr)
        *error = "native TCP transport is unavailable";
    if (status != nullptr) {
        *status =
            NativeTcpClientCreateStatus::TransportUnavailable;
    }
    return nullptr;
}
void NativeTcpClientTransport::service() {}
bool NativeTcpClientTransport::send(
    uint32_t, DeliveryClass, std::span<const std::byte>) {
    return false;
}
bool NativeTcpClientTransport::send(
    uint32_t, uint64_t, DeliveryClass,
    std::span<const std::byte>) {
    return false;
}
std::optional<MultiplayerTransportFrame>
NativeTcpClientTransport::poll() {
    return std::nullopt;
}
void NativeTcpClientTransport::close() {}
NativeTcpClientState
NativeTcpClientTransport::state() const noexcept {
    return state_ ? state_->state
                  : NativeTcpClientState::Disconnected;
}
uint64_t
NativeTcpClientTransport::connectionSerial() const noexcept {
    return 0u;
}
const NativeTcpTransportTelemetry&
NativeTcpClientTransport::telemetry() const noexcept {
    static const NativeTcpTransportTelemetry empty{};
    return state_ ? state_->telemetry : empty;
}

#endif

} // namespace voxy::network

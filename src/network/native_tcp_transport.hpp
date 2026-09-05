#pragma once

#include "network/session_transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::network {

// NativeTcpMultiplayerTransport is a bounded, nonblocking TCP fallback for
// local/native sessions. TCP makes every delivery class reliable and ordered.
// DeliveryClass remains in the frame so MultiplayerSession keeps independent
// packet sequence/ack windows, but this adapter cannot provide datagram loss or
// unordered delivery semantics. The bootstrap key is sent over the TCP stream;
// use this directly only on loopback/private development networks. Internet
// deployments require an authenticated encrypted tunnel, TLS/mTLS, or QUIC.
inline constexpr size_t kNativeTcpAuthenticationKeyBytes = 32u;
inline constexpr size_t kNativeTcpContentDigestBytes = 32u;
inline constexpr uint16_t kNativeTcpWireVersion = 3u;
using NativeTcpAuthenticationKey =
    std::array<std::byte, kNativeTcpAuthenticationKeyBytes>;
using NativeTcpContentDigest =
    std::array<std::byte, kNativeTcpContentDigestBytes>;

struct NativeTcpPeerCredential {
    uint32_t peerId = 0;
    NativeTcpAuthenticationKey authenticationKey{};
};

struct NativeTcpTransportLimits {
    uint32_t maximumPeers = 64;
    uint32_t maximumPendingConnections = 64;
    size_t maximumFrameBytes = 256u * 1024u;

    uint32_t maximumQueuedFramesPerPeer = 256;
    size_t maximumQueuedBytesPerPeer = 2u * 1024u * 1024u;
    uint32_t maximumQueuedInboundFramesPerPeer = 256;
    size_t maximumQueuedInboundBytesPerPeer = 1u * 1024u * 1024u;
    uint32_t maximumInboundFrames = 1'024;
    size_t maximumInboundBytes = 4u * 1024u * 1024u;
    // Includes queued lifecycle events and one guaranteed future
    // Disconnected event reserved by every authenticated connection.
    uint32_t maximumLifecycleEvents = 256;

    uint32_t maximumAcceptsPerService = 16;
    uint32_t maximumFramesPerPeerPerService = 64;
    uint32_t maximumHandshakeServiceCalls = 600;
    size_t maximumReadBytesPerPeerPerService = 64u * 1024u;
    size_t maximumWriteBytesPerPeerPerService = 64u * 1024u;
};

struct NativeTcpServerConfig {
    std::string bindAddress = "127.0.0.1";
    uint16_t port = 0;
    int listenBacklog = 64;
    NativeTcpTransportLimits limits{};
    std::vector<NativeTcpPeerCredential> peers;
    // Unkeyed build-content identity. It is checked in full before a peer can
    // enter the authenticated/Connected lifecycle.
    NativeTcpContentDigest expectedContentDigest{};
    // Primarily useful when restoring a server-owned serial namespace and for
    // deterministic boundary tests. Serials are monotonic for this transport
    // lifetime and fail-stop rather than wrapping through zero.
    uint64_t firstConnectionSerial = 1u;
    // Safe default: authenticated replacement sockets remain pending until
    // the authority accepts their exact server-issued serial.
    bool requireExplicitAdmission = true;
};

struct NativeTcpClientConfig {
    std::string serverAddress = "127.0.0.1";
    uint16_t serverPort = 0;
    NativeTcpPeerCredential credential{};
    // The client sends this expectation and independently requires the server
    // to return the same full digest before publishing Connected.
    NativeTcpContentDigest expectedContentDigest{};
    // Resolve once, then filter the exact sockaddr candidates passed to
    // connect(). Public candidates are never opened and therefore can never
    // receive the plaintext bootstrap credential.
    bool requirePrivateServerAddress = false;
    NativeTcpTransportLimits limits{
        .maximumPeers = 1,
        .maximumPendingConnections = 1,
    };
};

enum class NativeTcpIpAddressClass : uint32_t {
    Invalid = 0u,
    Private = 1u,
    Public = 2u,
};

// Numeric-address classifier used by tests and diagnostics. Hostname policy
// is enforced later against each resolved sockaddr, not against this parser.
[[nodiscard]] NativeTcpIpAddressClass classifyNativeTcpIpAddress(
    std::string_view address) noexcept;

enum class NativeTcpClientCreateStatus : uint32_t {
    Created = 0u,
    InvalidConfiguration,
    AddressResolutionFailed,
    PrivateAddressPolicyRejected,
    ConnectFailed,
    AuthenticationQueueFailed,
    TransportUnavailable,
};

[[nodiscard]] const char* nativeTcpClientCreateStatusName(
    NativeTcpClientCreateStatus status) noexcept;

enum class NativeTcpClientState : uint32_t {
    Disconnected = 0,
    Connecting = 1,
    Authenticating = 2,
    Connected = 3,
    Failed = 4,
};

struct NativeTcpTransportTelemetry {
    uint64_t acceptedSockets = 0;
    uint64_t rejectedSockets = 0;
    uint64_t authenticatedConnections = 0;
    uint64_t authenticationFailures = 0;
    uint64_t contentDigestMismatches = 0;
    uint64_t reconnects = 0;
    uint64_t disconnectedPeers = 0;
    uint64_t malformedFrames = 0;
    uint64_t oversizedFrames = 0;
    uint64_t outboundBackpressure = 0;
    uint64_t supersededOutboundFrames = 0;
    uint64_t supersededOutboundBytes = 0;
    uint64_t inboundBackpressure = 0;
    uint64_t sendWouldBlock = 0;
    uint64_t receiveWouldBlock = 0;
    uint64_t partialWrites = 0;
    uint64_t sentFrames = 0;
    uint64_t receivedFrames = 0;
    uint64_t sentWireBytes = 0;
    uint64_t receivedWireBytes = 0;
    uint64_t socketErrors = 0;
    uint64_t lifecycleBackpressure = 0;
    uint64_t serialExhaustions = 0;
    uint64_t peakConnectedPeers = 0;

    uint64_t queuedOutboundFrames = 0;
    uint64_t queuedOutboundBytes = 0;
    uint64_t queuedInboundFrames = 0;
    uint64_t queuedInboundBytes = 0;
    uint64_t queuedLifecycleEvents = 0;
    uint32_t connectedPeers = 0;
};

class NativeTcpServerTransport final : public IMultiplayerTransport {
public:
    ~NativeTcpServerTransport() override;

    NativeTcpServerTransport(const NativeTcpServerTransport&) = delete;
    NativeTcpServerTransport& operator=(
        const NativeTcpServerTransport&) = delete;

    [[nodiscard]] static std::unique_ptr<NativeTcpServerTransport> create(
        NativeTcpServerConfig config, std::string* error = nullptr);

    // Performs bounded, zero-timeout socket progress. It never waits for a
    // peer and is safe to call once per simulation tick.
    void service() override;

    [[nodiscard]] bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool sendLatestRealtime(
        uint32_t peerId, uint64_t connectionSerial,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool acceptConnection(
        uint32_t peerId, uint64_t connectionSerial) override;
    void rejectConnection(
        uint32_t peerId, uint64_t connectionSerial) override;
    [[nodiscard]] std::optional<MultiplayerTransportFrame> poll() override;
    void close() override;

    [[nodiscard]] uint16_t listeningPort() const noexcept;
    [[nodiscard]] bool connected(uint32_t peerId) const noexcept;
    [[nodiscard]] uint64_t connectionSerial(
        uint32_t peerId) const noexcept;
    [[nodiscard]] std::vector<uint32_t> connectedPeerIds() const;
    [[nodiscard]] const NativeTcpTransportTelemetry& telemetry()
        const noexcept;

private:
    struct State;
    explicit NativeTcpServerTransport(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

class NativeTcpClientTransport final : public IMultiplayerTransport {
public:
    ~NativeTcpClientTransport() override;

    NativeTcpClientTransport(const NativeTcpClientTransport&) = delete;
    NativeTcpClientTransport& operator=(
        const NativeTcpClientTransport&) = delete;

    [[nodiscard]] static std::unique_ptr<NativeTcpClientTransport> create(
        NativeTcpClientConfig config, std::string* error = nullptr,
        NativeTcpClientCreateStatus* status = nullptr);

    // Performs bounded, zero-timeout socket progress. A successful create may
    // still be Connecting; Connected means the server accepted the peer key
    // and this plaintext handshake reported the expected build-content digest
    // in both directions.
    void service() override;

    [[nodiscard]] bool send(
        uint32_t peerId, DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] bool send(
        uint32_t peerId, uint64_t connectionSerial,
        DeliveryClass delivery,
        std::span<const std::byte> bytes) override;
    [[nodiscard]] std::optional<MultiplayerTransportFrame> poll() override;
    void close() override;

    [[nodiscard]] NativeTcpClientState state() const noexcept;
    [[nodiscard]] bool connected() const noexcept {
        return state() == NativeTcpClientState::Connected;
    }
    [[nodiscard]] uint64_t connectionSerial() const noexcept;
    [[nodiscard]] const NativeTcpTransportTelemetry& telemetry()
        const noexcept;

private:
    struct State;
    explicit NativeTcpClientTransport(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace voxy::network

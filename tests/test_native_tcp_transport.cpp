#include "network/native_tcp_transport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__) \
    && (defined(__linux__) || defined(__APPLE__) || defined(__unix__))
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace voxy::network {
namespace {

#if !defined(__EMSCRIPTEN__) \
    && (defined(__linux__) || defined(__APPLE__) || defined(__unix__))

constexpr uint32_t kTestWireMagic = 0x31545856u;
constexpr uint16_t kTestWireVersion = kNativeTcpWireVersion;

NativeTcpAuthenticationKey testKey(uint32_t peerId) {
    NativeTcpAuthenticationKey key{};
    for (size_t byte = 0u; byte < key.size(); ++byte) {
        key[byte] = std::byte{static_cast<uint8_t>(
            (peerId * 37u
             + static_cast<uint32_t>(byte) * 13u)
            % 251u + 1u)};
    }
    return key;
}

NativeTcpPeerCredential testCredential(uint32_t peerId) {
    return {
        .peerId = peerId,
        .authenticationKey = testKey(peerId),
    };
}

NativeTcpContentDigest testContentDigest(uint8_t variant = 0u) {
    NativeTcpContentDigest digest{};
    for (size_t byte = 0u; byte < digest.size(); ++byte) {
        digest[byte] = std::byte{static_cast<uint8_t>(
            0x41u + variant
            + static_cast<uint8_t>(byte * 5u))};
    }
    return digest;
}

NativeTcpTransportLimits testLimits() {
    NativeTcpTransportLimits limits;
    limits.maximumPeers = 8u;
    limits.maximumPendingConnections = 8u;
    limits.maximumFrameBytes = 4u * 1024u;
    limits.maximumQueuedFramesPerPeer = 32u;
    limits.maximumQueuedBytesPerPeer = 64u * 1024u;
    limits.maximumQueuedInboundFramesPerPeer = 32u;
    limits.maximumQueuedInboundBytesPerPeer = 16u * 1024u;
    limits.maximumInboundFrames = 128u;
    limits.maximumInboundBytes = 128u * 1024u;
    limits.maximumAcceptsPerService = 8u;
    limits.maximumFramesPerPeerPerService = 32u;
    limits.maximumHandshakeServiceCalls = 10'000u;
    limits.maximumReadBytesPerPeerPerService = 64u * 1024u;
    limits.maximumWriteBytesPerPeerPerService = 64u * 1024u;
    return limits;
}

NativeTcpServerConfig serverConfig(
    std::span<const uint32_t> peerIds,
    NativeTcpTransportLimits limits = testLimits()) {
    NativeTcpServerConfig config;
    config.bindAddress = "127.0.0.1";
    config.port = 0u;
    config.limits = limits;
    config.expectedContentDigest = testContentDigest();
    for (const uint32_t peerId : peerIds)
        config.peers.push_back(testCredential(peerId));
    return config;
}

NativeTcpClientConfig clientConfig(
    uint16_t port, uint32_t peerId,
    NativeTcpTransportLimits limits = testLimits()) {
    limits.maximumPeers = 1u;
    limits.maximumPendingConnections = 1u;
    NativeTcpClientConfig config;
    config.serverAddress = "127.0.0.1";
    config.serverPort = port;
    config.credential = testCredential(peerId);
    config.expectedContentDigest = testContentDigest();
    config.limits = limits;
    return config;
}

std::vector<std::byte> payload(
    uint8_t first, size_t size = 8u) {
    std::vector<std::byte> output(size);
    for (size_t byte = 0u; byte < size; ++byte) {
        output[byte] = std::byte{static_cast<uint8_t>(
            first + static_cast<uint8_t>(byte))};
    }
    return output;
}

template <typename Predicate>
bool serviceUntil(
    NativeTcpServerTransport& server,
    std::span<std::unique_ptr<NativeTcpClientTransport>> clients,
    Predicate&& predicate, uint32_t attempts = 50'000u) {
    for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
        for (auto& client : clients) client->service();
        server.service();
        if (predicate()) return true;
        std::this_thread::yield();
    }
    return false;
}

template <typename Predicate>
bool serviceServerUntil(
    NativeTcpServerTransport& server, Predicate&& predicate,
    uint32_t attempts = 50'000u) {
    for (uint32_t attempt = 0u; attempt < attempts; ++attempt) {
        server.service();
        if (predicate()) return true;
        std::this_thread::yield();
    }
    return false;
}

template <typename Transport>
std::optional<MultiplayerTransportFrame> pollData(
    Transport& transport) {
    while (auto frame = transport.poll()) {
        if (frame->isData()) return frame;
    }
    return std::nullopt;
}

template <typename Transport>
std::vector<MultiplayerTransportFrame> drainFrames(
    Transport& transport) {
    std::vector<MultiplayerTransportFrame> frames;
    while (auto frame = transport.poll())
        frames.push_back(std::move(*frame));
    return frames;
}

void appendTestU16(
    std::vector<std::byte>& output, uint16_t value) {
    output.push_back(std::byte{static_cast<uint8_t>(value)});
    output.push_back(
        std::byte{static_cast<uint8_t>(value >> 8u)});
}

void appendTestU32(
    std::vector<std::byte>& output, uint32_t value) {
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        output.push_back(
            std::byte{static_cast<uint8_t>(value >> shift)});
    }
}

void appendTestU64(
    std::vector<std::byte>& output, uint64_t value) {
    for (uint32_t shift = 0u; shift < 64u; shift += 8u) {
        output.push_back(
            std::byte{static_cast<uint8_t>(value >> shift)});
    }
}

std::vector<std::byte> rawFrame(
    uint8_t kind, uint8_t delivery,
    std::span<const std::byte> body,
    uint32_t advertisedBytes = 0u) {
    std::vector<std::byte> output;
    output.reserve(12u + body.size());
    appendTestU32(output, kTestWireMagic);
    appendTestU16(output, kTestWireVersion);
    output.push_back(std::byte{kind});
    output.push_back(std::byte{delivery});
    appendTestU32(
        output,
        advertisedBytes == 0u
            ? static_cast<uint32_t>(body.size())
            : advertisedBytes);
    output.insert(output.end(), body.begin(), body.end());
    return output;
}

std::vector<std::byte> rawHello(
    const NativeTcpPeerCredential& credential,
    NativeTcpContentDigest contentDigest = testContentDigest()) {
    std::vector<std::byte> body;
    appendTestU32(body, credential.peerId);
    body.insert(
        body.end(), credential.authenticationKey.begin(),
        credential.authenticationKey.end());
    body.insert(
        body.end(), contentDigest.begin(), contentDigest.end());
    return rawFrame(1u, 0u, body);
}

std::vector<std::byte> rawAccepted(
    uint32_t peerId, uint64_t connectionSerial,
    const NativeTcpContentDigest& contentDigest) {
    std::vector<std::byte> body;
    appendTestU32(body, peerId);
    appendTestU64(body, connectionSerial);
    body.insert(
        body.end(), contentDigest.begin(), contentDigest.end());
    return rawFrame(2u, 0u, body);
}

std::vector<std::byte> rawData(
    DeliveryClass delivery,
    std::span<const std::byte> body) {
    return rawFrame(
        3u, static_cast<uint8_t>(delivery), body);
}

class ScopedSocket {
public:
    explicit ScopedSocket(int fd = -1) : fd_(fd) {}
    ~ScopedSocket() {
        if (fd_ >= 0) static_cast<void>(::close(fd_));
    }
    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;
    ScopedSocket(ScopedSocket&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}
    ScopedSocket& operator=(ScopedSocket&& other) noexcept {
        if (this == &other) return *this;
        if (fd_ >= 0) static_cast<void>(::close(fd_));
        fd_ = std::exchange(other.fd_, -1);
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

ScopedSocket rawConnect(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return ScopedSocket{};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(
            AF_INET, "127.0.0.1",
            &address.sin_addr) != 1
        || ::connect(
            fd, reinterpret_cast<const sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))) < 0) {
        static_cast<void>(::close(fd));
        return ScopedSocket{};
    }
    return ScopedSocket(fd);
}

ScopedSocket rawListen(uint16_t& port) {
    port = 0u;
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return ScopedSocket{};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(0u);
    if (::inet_pton(
            AF_INET, "127.0.0.1",
            &address.sin_addr) != 1
        || ::bind(
            fd, reinterpret_cast<const sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))) < 0
        || ::listen(fd, 1) < 0) {
        static_cast<void>(::close(fd));
        return ScopedSocket{};
    }
    socklen_t addressBytes =
        static_cast<socklen_t>(sizeof(address));
    if (::getsockname(
            fd, reinterpret_cast<sockaddr*>(&address),
            &addressBytes) < 0) {
        static_cast<void>(::close(fd));
        return ScopedSocket{};
    }
    port = ntohs(address.sin_port);
    return ScopedSocket(fd);
}

bool rawSendAll(
    int fd, std::span<const std::byte> bytes) {
    size_t offset = 0u;
    while (offset < bytes.size()) {
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t sent = ::send(
            fd, bytes.data() + offset,
            bytes.size() - offset, flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (sent == 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

TEST(NativeTcpTransportTest,
     PrivateAddressClassifierCoversProductRanges) {
    const std::array privateAddresses{
        "127.0.0.1",
        "127.255.255.255",
        "10.0.0.1",
        "172.16.0.1",
        "172.31.255.255",
        "192.168.42.1",
        "169.254.1.1",
        "::1",
        "fc00::1",
        "fdff::1",
        "fe80::1",
        "febf::1",
        "::ffff:127.0.0.1",
        "::ffff:192.168.1.1",
    };
    for (const std::string_view address : privateAddresses) {
        EXPECT_EQ(
            classifyNativeTcpIpAddress(address),
            NativeTcpIpAddressClass::Private)
            << address;
    }

    const std::array publicAddresses{
        "0.0.0.0",
        "8.8.8.8",
        "172.15.255.255",
        "172.32.0.0",
        "192.169.0.1",
        "169.253.255.255",
        "224.0.0.1",
        "::",
        "2001:4860:4860::8888",
        "fe7f::1",
        "fec0::1",
        "::ffff:8.8.8.8",
    };
    for (const std::string_view address : publicAddresses) {
        EXPECT_EQ(
            classifyNativeTcpIpAddress(address),
            NativeTcpIpAddressClass::Public)
            << address;
    }
    EXPECT_EQ(
        classifyNativeTcpIpAddress("localhost"),
        NativeTcpIpAddressClass::Invalid);
    EXPECT_EQ(
        classifyNativeTcpIpAddress("not-an-address"),
        NativeTcpIpAddressClass::Invalid);
}

TEST(NativeTcpTransportTest,
     PrivatePolicyRejectsPublicCandidateBeforeCredentialQueue) {
    NativeTcpClientConfig config = clientConfig(9u, 1u);
    config.serverAddress = "192.0.2.1";
    config.requirePrivateServerAddress = true;
    NativeTcpClientCreateStatus status =
        NativeTcpClientCreateStatus::Created;
    std::string error;
    auto client = NativeTcpClientTransport::create(
        std::move(config), &error, &status);
    EXPECT_EQ(client, nullptr);
    EXPECT_EQ(
        status,
        NativeTcpClientCreateStatus::
            PrivateAddressPolicyRejected);
    EXPECT_EQ(
        nativeTcpClientCreateStatusName(status),
        std::string_view{"private-address policy rejected"});
    EXPECT_FALSE(error.empty());
}

TEST(NativeTcpTransportTest,
     PrivatePolicyAllowsResolvedLoopbackHandshake) {
    constexpr uint32_t peerId = 1u;
    std::string error;
    auto server = NativeTcpServerTransport::create(
        serverConfig(std::span(&peerId, 1u)), &error);
    ASSERT_NE(server, nullptr) << error;

    auto config = clientConfig(
        server->listeningPort(), peerId);
    config.requirePrivateServerAddress = true;
    NativeTcpClientCreateStatus status =
        NativeTcpClientCreateStatus::InvalidConfiguration;
    auto client = NativeTcpClientTransport::create(
        std::move(config), &error, &status);
    ASSERT_NE(client, nullptr) << error;
    EXPECT_EQ(status, NativeTcpClientCreateStatus::Created);

    std::array<
        std::unique_ptr<NativeTcpClientTransport>, 1u>
        clients{std::move(client)};
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected();
        }));
}

TEST(NativeTcpTransportTest, FourAuthenticatedClientsRoundTripAllLanes) {
    const std::array<uint32_t, 4> peerIds{4u, 2u, 1u, 3u};
    std::string error;
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds), &error);
    ASSERT_NE(server, nullptr) << error;

    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        auto client = NativeTcpClientTransport::create(
            clientConfig(server->listeningPort(), peerId), &error);
        ASSERT_NE(client, nullptr) << error;
        clients.push_back(std::move(client));
    }
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return server->connectedPeerIds()
                    == std::vector<uint32_t>{1u, 2u, 3u, 4u}
                && std::all_of(
                    clients.begin(), clients.end(),
                    [](const auto& client) {
                        return client->connected();
                    });
        }));

    const auto serverLifecycle = drainFrames(*server);
    ASSERT_EQ(serverLifecycle.size(), 4u);
    std::array<uint64_t, 5> serialByPeer{};
    for (const auto& frame : serverLifecycle) {
        EXPECT_EQ(
            frame.type,
            MultiplayerTransportFrameType::Connected);
        ASSERT_GE(frame.peerId, 1u);
        ASSERT_LE(frame.peerId, 4u);
        EXPECT_NE(frame.connectionSerial, 0u);
        EXPECT_TRUE(frame.bytes.empty());
        serialByPeer[frame.peerId] = frame.connectionSerial;
    }
    for (size_t index = 0u; index < clients.size(); ++index) {
        const auto lifecycle = drainFrames(*clients[index]);
        ASSERT_EQ(lifecycle.size(), 1u);
        EXPECT_EQ(
            lifecycle[0].type,
            MultiplayerTransportFrameType::Connected);
        EXPECT_EQ(lifecycle[0].peerId, 0u);
        EXPECT_EQ(
            lifecycle[0].connectionSerial,
            serialByPeer[index + 1u]);
        EXPECT_EQ(
            clients[index]->connectionSerial(),
            serialByPeer[index + 1u]);
        EXPECT_EQ(
            server->connectionSerial(
                static_cast<uint32_t>(index + 1u)),
            serialByPeer[index + 1u]);
    }

    const std::array lanes{
        DeliveryClass::Realtime,
        DeliveryClass::ReliableEvent,
        DeliveryClass::ReliableControl,
        DeliveryClass::Realtime,
    };
    for (size_t reverse = clients.size(); reverse > 0u; --reverse) {
        const size_t index = reverse - 1u;
        IMultiplayerTransport* endpoint = clients[index].get();
        const auto bytes = payload(
            static_cast<uint8_t>(0x10u + index));
        ASSERT_TRUE(endpoint->send(0u, lanes[index], bytes));
    }

    std::vector<MultiplayerTransportFrame> received;
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            while (auto frame = server->poll())
                received.push_back(std::move(*frame));
            return received.size() == clients.size();
        }));
    std::sort(
        received.begin(), received.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.peerId < rhs.peerId;
        });
    for (size_t index = 0u; index < received.size(); ++index) {
        EXPECT_EQ(received[index].peerId, index + 1u);
        EXPECT_EQ(
            received[index].connectionSerial,
            serialByPeer[index + 1u]);
        EXPECT_TRUE(received[index].isData());
        EXPECT_EQ(received[index].delivery, lanes[index]);
        EXPECT_EQ(
            received[index].bytes,
            payload(static_cast<uint8_t>(0x10u + index)));
        ASSERT_TRUE(server->send(
            received[index].peerId,
            received[index].connectionSerial,
            DeliveryClass::ReliableControl,
            received[index].bytes));
    }

    std::vector<bool> replied(clients.size(), false);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            for (size_t index = 0u; index < clients.size(); ++index) {
                if (replied[index]) continue;
                auto frame = clients[index]->poll();
                if (!frame.has_value()) continue;
                EXPECT_EQ(frame->peerId, 0u);
                EXPECT_TRUE(frame->isData());
                EXPECT_EQ(
                    frame->connectionSerial,
                    serialByPeer[index + 1u]);
                EXPECT_EQ(
                    frame->delivery,
                    DeliveryClass::ReliableControl);
                EXPECT_EQ(
                    frame->bytes,
                    payload(static_cast<uint8_t>(0x10u + index)));
                replied[index] = true;
            }
            return std::all_of(
                replied.begin(), replied.end(),
                [](bool value) { return value; });
        }));

    EXPECT_EQ(server->telemetry().peakConnectedPeers, 4u);
    EXPECT_EQ(server->telemetry().authenticationFailures, 0u);
    EXPECT_EQ(server->telemetry().contentDigestMismatches, 0u);
    EXPECT_EQ(server->telemetry().receivedFrames, 4u);
}

TEST(NativeTcpTransportTest, DecodesFragmentedAndCoalescedFrames) {
    const std::array<uint32_t, 2> peerIds{1u, 2u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds));
    ASSERT_NE(server, nullptr);

    ScopedSocket fragmented =
        rawConnect(server->listeningPort());
    ASSERT_GE(fragmented.get(), 0);
    std::vector<std::byte> fragmentedStream =
        rawHello(testCredential(1u));
    const auto firstPayload = payload(0x31u, 19u);
    const auto firstData =
        rawData(DeliveryClass::Realtime, firstPayload);
    fragmentedStream.insert(
        fragmentedStream.end(),
        firstData.begin(), firstData.end());
    for (const std::byte byte : fragmentedStream) {
        const std::array one{byte};
        ASSERT_TRUE(rawSendAll(fragmented.get(), one));
        server->service();
    }
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() { return server->connected(1u); }));

    ScopedSocket coalesced =
        rawConnect(server->listeningPort());
    ASSERT_GE(coalesced.get(), 0);
    std::vector<std::byte> coalescedStream =
        rawHello(testCredential(2u));
    const auto secondPayload = payload(0x52u, 23u);
    const auto thirdPayload = payload(0x73u, 29u);
    const auto secondData =
        rawData(DeliveryClass::ReliableEvent, secondPayload);
    const auto thirdData =
        rawData(DeliveryClass::ReliableControl, thirdPayload);
    coalescedStream.insert(
        coalescedStream.end(),
        secondData.begin(), secondData.end());
    coalescedStream.insert(
        coalescedStream.end(),
        thirdData.begin(), thirdData.end());
    ASSERT_TRUE(rawSendAll(coalesced.get(), coalescedStream));

    std::vector<MultiplayerTransportFrame> received;
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            while (auto frame = server->poll())
                received.push_back(std::move(*frame));
            return server->connected(2u)
                && received.size() == 5u;
        }));
    ASSERT_EQ(received.size(), 5u);
    // Cross-peer service order is intentionally unspecified. Within each TCP
    // connection, lifecycle and data ordering is strict.
    std::stable_sort(
        received.begin(), received.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.peerId < rhs.peerId;
        });
    EXPECT_EQ(
        received[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(received[0].peerId, 1u);
    EXPECT_NE(received[0].connectionSerial, 0u);
    EXPECT_TRUE(received[1].isData());
    EXPECT_EQ(received[1].peerId, 1u);
    EXPECT_EQ(
        received[1].connectionSerial,
        received[0].connectionSerial);
    EXPECT_EQ(received[1].bytes, firstPayload);
    EXPECT_EQ(
        received[2].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(received[2].peerId, 2u);
    EXPECT_GT(
        received[2].connectionSerial,
        received[0].connectionSerial);
    EXPECT_TRUE(received[3].isData());
    EXPECT_EQ(received[3].peerId, 2u);
    EXPECT_EQ(
        received[3].delivery,
        DeliveryClass::ReliableEvent);
    EXPECT_EQ(
        received[3].connectionSerial,
        received[2].connectionSerial);
    EXPECT_EQ(received[3].bytes, secondPayload);
    EXPECT_TRUE(received[4].isData());
    EXPECT_EQ(received[4].peerId, 2u);
    EXPECT_EQ(
        received[4].delivery,
        DeliveryClass::ReliableControl);
    EXPECT_EQ(received[4].bytes, thirdPayload);
}

TEST(
    NativeTcpTransportTest,
    MultiRecvHelloStopsBeforeDataAndPublishesConnectedFirst) {
    NativeTcpTransportLimits limits = testLimits();
    limits.maximumFrameBytes = 24u * 1024u;
    limits.maximumQueuedInboundBytesPerPeer = 32u * 1024u;
    limits.maximumInboundBytes = 64u * 1024u;
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds, limits));
    ASSERT_NE(server, nullptr);

    ScopedSocket socket = rawConnect(server->listeningPort());
    ASSERT_GE(socket.get(), 0);
    const std::vector<std::byte> body =
        payload(0x29u, 20u * 1024u);
    std::vector<std::byte> stream =
        rawHello(testCredential(1u));
    const std::vector<std::byte> data =
        rawData(DeliveryClass::ReliableControl, body);
    stream.insert(stream.end(), data.begin(), data.end());
    ASSERT_GT(stream.size(), 16u * 1024u);
    ASSERT_TRUE(rawSendAll(socket.get(), stream));

    std::vector<MultiplayerTransportFrame> received;
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            while (auto frame = server->poll())
                received.push_back(std::move(*frame));
            return received.size() >= 2u;
        }));
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(
        received[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(received[0].peerId, 1u);
    EXPECT_NE(received[0].connectionSerial, 0u);
    EXPECT_TRUE(received[1].isData());
    EXPECT_EQ(received[1].peerId, 1u);
    EXPECT_EQ(
        received[1].connectionSerial,
        received[0].connectionSerial);
    EXPECT_EQ(received[1].bytes, body);
}

TEST(NativeTcpTransportTest, RejectsBadCredentialsMalformedAndOversizedInput) {
    const std::array<uint32_t, 2> duplicateKeyIds{10u, 11u};
    auto duplicateKeyConfig = serverConfig(duplicateKeyIds);
    duplicateKeyConfig.peers[1u].authenticationKey =
        duplicateKeyConfig.peers[0u].authenticationKey;
    std::string duplicateError;
    EXPECT_EQ(
        NativeTcpServerTransport::create(
            std::move(duplicateKeyConfig), &duplicateError),
        nullptr);
    EXPECT_FALSE(duplicateError.empty());

    NativeTcpTransportLimits limits = testLimits();
    limits.maximumFrameBytes = 64u;
    limits.maximumInboundBytes = 256u;
    const std::array<uint32_t, 3> peerIds{1u, 2u, 3u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds, limits));
    ASSERT_NE(server, nullptr);

    ScopedSocket oversized =
        rawConnect(server->listeningPort());
    ASSERT_GE(oversized.get(), 0);
    ASSERT_TRUE(rawSendAll(
        oversized.get(), rawHello(testCredential(1u))));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() { return server->connected(1u); }));
    const std::array<std::byte, 0> noBody{};
    const auto oversizedHeader = rawFrame(
        3u,
        static_cast<uint8_t>(
            DeliveryClass::ReliableControl),
        noBody, 65u);
    ASSERT_TRUE(rawSendAll(
        oversized.get(), oversizedHeader));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return !server->connected(1u)
                && server->telemetry().oversizedFrames == 1u;
        }));

    ScopedSocket malformed =
        rawConnect(server->listeningPort());
    ASSERT_GE(malformed.get(), 0);
    ASSERT_TRUE(rawSendAll(
        malformed.get(), rawHello(testCredential(2u))));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() { return server->connected(2u); }));
    auto badHeader = rawData(
        DeliveryClass::Realtime, payload(0x80u));
    badHeader[0] = std::byte{0u};
    ASSERT_TRUE(rawSendAll(malformed.get(), badHeader));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return !server->connected(2u)
                && server->telemetry().malformedFrames >= 1u;
        }));

    const uint64_t malformedBeforeVersion =
        server->telemetry().malformedFrames;
    ScopedSocket legacyVersion =
        rawConnect(server->listeningPort());
    ASSERT_GE(legacyVersion.get(), 0);
    auto legacyHello = rawHello(testCredential(1u));
    const uint16_t legacyWireVersion =
        static_cast<uint16_t>(kTestWireVersion - 1u);
    legacyHello[4u] =
        std::byte{static_cast<uint8_t>(legacyWireVersion)};
    legacyHello[5u] =
        std::byte{static_cast<uint8_t>(
            legacyWireVersion >> 8u)};
    ASSERT_TRUE(rawSendAll(legacyVersion.get(), legacyHello));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return server->telemetry().malformedFrames
                == malformedBeforeVersion + 1u;
        }));
    EXPECT_FALSE(server->connected(1u));

    ScopedSocket unauthenticated =
        rawConnect(server->listeningPort());
    ASSERT_GE(unauthenticated.get(), 0);
    auto wrongCredential = testCredential(3u);
    wrongCredential.authenticationKey[7u] ^= std::byte{0x5au};
    ASSERT_TRUE(rawSendAll(
        unauthenticated.get(), rawHello(wrongCredential)));
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return server->telemetry().authenticationFailures
                == 1u;
        }));
    EXPECT_FALSE(server->connected(3u));
    EXPECT_EQ(server->telemetry().connectedPeers, 0u);
}

TEST(NativeTcpTransportTest,
     ServerRejectsContentMismatchBeforeConnectedOrRosterAdmission) {
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds));
    ASSERT_NE(server, nullptr);

    NativeTcpClientConfig mismatched =
        clientConfig(server->listeningPort(), 1u);
    mismatched.expectedContentDigest[17u] ^= std::byte{0x80u};
    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    clients.push_back(
        NativeTcpClientTransport::create(std::move(mismatched)));
    ASSERT_NE(clients[0], nullptr);

    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->state()
                    == NativeTcpClientState::Failed
                && server->telemetry().contentDigestMismatches
                    == 1u;
        }));
    EXPECT_FALSE(server->connected(1u));
    EXPECT_TRUE(server->connectedPeerIds().empty());
    EXPECT_EQ(server->telemetry().connectedPeers, 0u);
    EXPECT_EQ(server->telemetry().authenticatedConnections, 0u);
    EXPECT_EQ(server->telemetry().authenticationFailures, 0u);
    EXPECT_TRUE(drainFrames(*server).empty());
    EXPECT_TRUE(drainFrames(*clients[0]).empty());
}

TEST(NativeTcpTransportTest,
     ClientRejectsServerContentMismatchBeforeConnected) {
    uint16_t port = 0u;
    ScopedSocket listener = rawListen(port);
    ASSERT_GE(listener.get(), 0);
    ASSERT_NE(port, 0u);

    auto client = NativeTcpClientTransport::create(
        clientConfig(port, 1u));
    ASSERT_NE(client, nullptr);
    NativeTcpContentDigest wrongDigest = testContentDigest();
    wrongDigest[31u] ^= std::byte{0x01u};
    std::atomic<bool> responseSent = false;
    std::thread rawServer([&]() {
        int accepted = -1;
        do {
            accepted = ::accept(listener.get(), nullptr, nullptr);
        } while (accepted < 0 && errno == EINTR);
        ScopedSocket connection(accepted);
        if (accepted < 0) return;
        const auto response =
            rawAccepted(1u, 73u, wrongDigest);
        responseSent.store(
            rawSendAll(connection.get(), response),
            std::memory_order_relaxed);
    });

    for (uint32_t attempt = 0u;
         attempt < 50'000u
         && client->state() != NativeTcpClientState::Failed;
         ++attempt) {
        client->service();
        std::this_thread::yield();
    }
    rawServer.join();

    EXPECT_TRUE(responseSent.load(std::memory_order_relaxed));
    EXPECT_EQ(
        client->state(), NativeTcpClientState::Failed);
    EXPECT_FALSE(client->connected());
    EXPECT_EQ(client->connectionSerial(), 0u);
    EXPECT_EQ(client->telemetry().connectedPeers, 0u);
    EXPECT_EQ(client->telemetry().authenticatedConnections, 0u);
    EXPECT_EQ(client->telemetry().authenticationFailures, 0u);
    EXPECT_EQ(client->telemetry().contentDigestMismatches, 1u);
    EXPECT_TRUE(drainFrames(*client).empty());
}

TEST(NativeTcpTransportTest, RejectsZeroExpectedContentDigest) {
    const std::array<uint32_t, 1> peerIds{1u};
    NativeTcpServerConfig invalidServer = serverConfig(peerIds);
    invalidServer.expectedContentDigest = {};
    std::string error;
    EXPECT_EQ(
        NativeTcpServerTransport::create(
            std::move(invalidServer), &error),
        nullptr);
    EXPECT_FALSE(error.empty());

    NativeTcpClientConfig invalidClient = clientConfig(1u, 1u);
    invalidClient.expectedContentDigest = {};
    error.clear();
    EXPECT_EQ(
        NativeTcpClientTransport::create(
            std::move(invalidClient), &error),
        nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(NativeTcpTransportTest, AppliesOutboundAndInboundBackpressure) {
    NativeTcpTransportLimits limits = testLimits();
    limits.maximumFrameBytes = 64u;
    limits.maximumQueuedFramesPerPeer = 2u;
    limits.maximumQueuedBytesPerPeer = 80u;
    limits.maximumQueuedInboundFramesPerPeer = 1u;
    limits.maximumQueuedInboundBytesPerPeer = 64u;
    limits.maximumInboundFrames = 1u;
    limits.maximumInboundBytes = 64u;
    limits.maximumWriteBytesPerPeerPerService = 3u;
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds, limits));
    ASSERT_NE(server, nullptr);
    auto client = NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u, limits));
    ASSERT_NE(client, nullptr);
    std::array<std::unique_ptr<NativeTcpClientTransport>, 1>
        clients{std::move(client)};
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected()
                && server->connected(1u);
        }));

    const auto first = payload(0x21u, 16u);
    const auto second = payload(0x41u, 16u);
    const auto third = payload(0x61u, 16u);
    ASSERT_TRUE(clients[0]->send(
        0u, DeliveryClass::ReliableEvent, first));
    ASSERT_TRUE(clients[0]->send(
        0u, DeliveryClass::ReliableEvent, second));
    EXPECT_FALSE(clients[0]->send(
        0u, DeliveryClass::ReliableEvent, third));
    EXPECT_GE(
        clients[0]->telemetry().outboundBackpressure, 1u);

    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return server->telemetry().inboundBackpressure > 0u;
        }));
    std::vector<MultiplayerTransportFrame> received;
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            if (auto frame = pollData(*server))
                received.push_back(std::move(*frame));
            return received.size() == 2u;
        }));
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].bytes, first);
    EXPECT_EQ(received[1].bytes, second);
    EXPECT_GT(clients[0]->telemetry().partialWrites, 0u);
    EXPECT_GT(server->telemetry().inboundBackpressure, 0u);
    EXPECT_EQ(server->telemetry().queuedInboundFrames, 0u);
}

TEST(NativeTcpTransportTest, ReconnectsSamePeerAndReleasesSocketsOnClose) {
    const std::array<uint32_t, 1> peerIds{1u};
    NativeTcpTransportLimits limits = testLimits();
    limits.maximumWriteBytesPerPeerPerService = 1u;
    NativeTcpServerConfig initialConfig =
        serverConfig(peerIds, limits);
    auto server = NativeTcpServerTransport::create(initialConfig);
    ASSERT_NE(server, nullptr);
    const uint16_t port = server->listeningPort();

    auto first = NativeTcpClientTransport::create(
        clientConfig(port, 1u, limits));
    ASSERT_NE(first, nullptr);
    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    clients.push_back(std::move(first));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected()
                && server->connected(1u);
        }));
    const uint64_t oldSerial = server->connectionSerial(1u);
    ASSERT_NE(oldSerial, 0u);
    ASSERT_EQ(clients[0]->connectionSerial(), oldSerial);
    const auto initialServerEvents = drainFrames(*server);
    const auto initialClientEvents = drainFrames(*clients[0]);
    ASSERT_EQ(initialServerEvents.size(), 1u);
    ASSERT_EQ(initialClientEvents.size(), 1u);
    EXPECT_EQ(
        initialServerEvents[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(
        initialClientEvents[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(
        initialServerEvents[0].connectionSerial, oldSerial);
    EXPECT_EQ(
        initialClientEvents[0].connectionSerial, oldSerial);

    const auto before = payload(0x11u);
    ASSERT_TRUE(clients[0]->send(
        0u, oldSerial, DeliveryClass::Realtime, before));
    std::optional<MultiplayerTransportFrame> beforeFrame;
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            beforeFrame = pollData(*server);
            return beforeFrame.has_value();
    }));
    EXPECT_EQ(beforeFrame->bytes, before);
    EXPECT_EQ(beforeFrame->connectionSerial, oldSerial);

    const auto stale = payload(0x51u);
    ASSERT_TRUE(clients[0]->send(
        0u, DeliveryClass::ReliableEvent, stale));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return server->telemetry().queuedInboundFrames == 1u;
        }));

    const auto staleOutbound = payload(0x71u, 2u * 1024u);
    ASSERT_TRUE(server->send(
        1u, oldSerial, DeliveryClass::ReliableControl,
        staleOutbound));
    ASSERT_GT(server->telemetry().queuedOutboundBytes, 0u);

    auto reconnecting = NativeTcpClientTransport::create(
        clientConfig(port, 1u, limits));
    ASSERT_NE(reconnecting, nullptr);
    clients.push_back(std::move(reconnecting));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[1]->connected()
                && server->connected(1u)
                && server->telemetry().reconnects == 1u
                && server->telemetry().queuedInboundFrames == 0u;
        }));
    const uint64_t newSerial = server->connectionSerial(1u);
    ASSERT_GT(newSerial, oldSerial);
    ASSERT_EQ(clients[1]->connectionSerial(), newSerial);
    EXPECT_FALSE(clients[0]->connected());
    EXPECT_EQ(server->telemetry().reconnects, 1u);
    EXPECT_EQ(server->telemetry().connectedPeers, 1u);
    EXPECT_EQ(server->telemetry().queuedInboundFrames, 0u);

    const auto replacementEvents = drainFrames(*server);
    ASSERT_EQ(replacementEvents.size(), 2u);
    EXPECT_EQ(
        replacementEvents[0].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(replacementEvents[0].peerId, 1u);
    EXPECT_EQ(
        replacementEvents[0].connectionSerial, oldSerial);
    EXPECT_EQ(
        replacementEvents[1].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(replacementEvents[1].peerId, 1u);
    EXPECT_EQ(
        replacementEvents[1].connectionSerial, newSerial);

    const auto oldClientEvents = drainFrames(*clients[0]);
    ASSERT_EQ(oldClientEvents.size(), 1u);
    EXPECT_EQ(
        oldClientEvents[0].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(
        oldClientEvents[0].connectionSerial, oldSerial);
    const auto newClientEvents = drainFrames(*clients[1]);
    ASSERT_EQ(newClientEvents.size(), 1u);
    EXPECT_EQ(
        newClientEvents[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(
        newClientEvents[0].connectionSerial, newSerial);

    EXPECT_FALSE(server->send(
        1u, oldSerial, DeliveryClass::ReliableControl, before));
    EXPECT_FALSE(clients[1]->send(
        0u, oldSerial, DeliveryClass::ReliableControl, before));

    const auto after = payload(0x91u);
    ASSERT_TRUE(clients[1]->send(
        0u, newSerial, DeliveryClass::ReliableControl, after));
    std::optional<MultiplayerTransportFrame> afterFrame;
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            afterFrame = pollData(*server);
            return afterFrame.has_value();
        }));
    EXPECT_EQ(afterFrame->peerId, 1u);
    EXPECT_EQ(afterFrame->bytes, after);
    EXPECT_EQ(afterFrame->connectionSerial, newSerial);

    ASSERT_TRUE(server->send(
        1u, newSerial, DeliveryClass::ReliableControl, after));
    EXPECT_GT(server->telemetry().queuedOutboundBytes, 0u);
    clients[1]->close();
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return !server->connected(1u);
        }));
    EXPECT_EQ(server->telemetry().queuedOutboundBytes, 0u);
    const auto finalEvents = drainFrames(*server);
    ASSERT_EQ(finalEvents.size(), 1u);
    EXPECT_EQ(
        finalEvents[0].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(finalEvents[0].connectionSerial, newSerial);
    server->close();
    EXPECT_EQ(server->telemetry().connectedPeers, 0u);
    EXPECT_EQ(server->telemetry().queuedOutboundBytes, 0u);
    EXPECT_EQ(server->telemetry().queuedInboundBytes, 0u);

    initialConfig.port = port;
    auto replacement =
        NativeTcpServerTransport::create(initialConfig);
    ASSERT_NE(replacement, nullptr);
    EXPECT_EQ(replacement->listeningPort(), port);
    replacement->close();
}

TEST(NativeTcpTransportTest, ConnectionSerialExhaustionFailsStop) {
    const std::array<uint32_t, 3> peerIds{1u, 2u, 3u};
    NativeTcpServerConfig config = serverConfig(peerIds);
    config.firstConnectionSerial =
        std::numeric_limits<uint64_t>::max() - 1u;
    auto server = NativeTcpServerTransport::create(config);
    ASSERT_NE(server, nullptr);

    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    for (uint32_t peerId = 1u; peerId <= 2u; ++peerId) {
        auto client = NativeTcpClientTransport::create(
            clientConfig(server->listeningPort(), peerId));
        ASSERT_NE(client, nullptr);
        clients.push_back(std::move(client));
        ASSERT_TRUE(serviceUntil(
            *server, clients, [&]() {
                return clients.back()->connected()
                    && server->connected(peerId);
            }));
    }
    EXPECT_EQ(
        server->connectionSerial(1u),
        std::numeric_limits<uint64_t>::max() - 1u);
    EXPECT_EQ(
        server->connectionSerial(2u),
        std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(
        clients[0]->connectionSerial(),
        server->connectionSerial(1u));
    EXPECT_EQ(
        clients[1]->connectionSerial(),
        server->connectionSerial(2u));

    auto rejected = NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 3u));
    ASSERT_NE(rejected, nullptr);
    clients.push_back(std::move(rejected));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[2]->state()
                    == NativeTcpClientState::Failed
                && server->telemetry().serialExhaustions > 0u;
        }));
    EXPECT_FALSE(server->connected(3u));
    EXPECT_EQ(server->connectionSerial(3u), 0u);

    config.firstConnectionSerial = 0u;
    std::string error;
    EXPECT_EQ(
        NativeTcpServerTransport::create(config, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST(NativeTcpTransportTest,
     LifecycleCapacityRejectsBeforeReplacingActivePeer) {
    NativeTcpTransportLimits limits = testLimits();
    limits.maximumPeers = 1u;
    limits.maximumLifecycleEvents = 3u;
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds, limits));
    ASSERT_NE(server, nullptr);

    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u, limits)));
    ASSERT_NE(clients[0], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected()
                && server->connected(1u);
        }));
    const uint64_t firstSerial =
        server->connectionSerial(1u);

    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u, limits)));
    ASSERT_NE(clients[1], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[1]->state()
                    == NativeTcpClientState::Failed
                && server->telemetry().lifecycleBackpressure == 1u;
        }));
    EXPECT_TRUE(server->connected(1u));
    EXPECT_EQ(server->connectionSerial(1u), firstSerial);
    EXPECT_EQ(server->telemetry().reconnects, 0u);

    const auto initialEvents = drainFrames(*server);
    ASSERT_EQ(initialEvents.size(), 1u);
    EXPECT_EQ(
        initialEvents[0].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(initialEvents[0].connectionSerial, firstSerial);

    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u, limits)));
    ASSERT_NE(clients[2], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[2]->connected()
                && server->telemetry().reconnects == 1u;
        }));
    const uint64_t replacementSerial =
        server->connectionSerial(1u);
    EXPECT_GT(replacementSerial, firstSerial);
    const auto replacementEvents = drainFrames(*server);
    ASSERT_EQ(replacementEvents.size(), 2u);
    EXPECT_EQ(
        replacementEvents[0].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(
        replacementEvents[0].connectionSerial, firstSerial);
    EXPECT_EQ(
        replacementEvents[1].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(
        replacementEvents[1].connectionSerial,
        replacementSerial);
}

TEST(NativeTcpTransportTest,
     NormalDisconnectRetainsDecodedDataBeforeLifecycleEvent) {
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds));
    ASSERT_NE(server, nullptr);
    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u)));
    ASSERT_NE(clients[0], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected()
                && server->connected(1u);
        }));
    const uint64_t serial = server->connectionSerial(1u);
    static_cast<void>(drainFrames(*server));
    static_cast<void>(drainFrames(*clients[0]));

    const auto finalData = payload(0xb1u, 31u);
    ASSERT_TRUE(clients[0]->send(
        0u, serial, DeliveryClass::ReliableEvent, finalData));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return server->telemetry().queuedInboundFrames == 1u;
        }));
    clients[0]->close();
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return !server->connected(1u);
        }));

    const auto frames = drainFrames(*server);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_TRUE(frames[0].isData());
    EXPECT_EQ(frames[0].bytes, finalData);
    EXPECT_EQ(frames[0].connectionSerial, serial);
    EXPECT_EQ(
        frames[1].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(frames[1].connectionSerial, serial);
}

TEST(NativeTcpTransportTest,
     ReconnectAfterDisconnectPurgesUnconsumedOldData) {
    const std::array<uint32_t, 1> peerIds{1u};
    auto server = NativeTcpServerTransport::create(
        serverConfig(peerIds));
    ASSERT_NE(server, nullptr);
    std::vector<std::unique_ptr<NativeTcpClientTransport>> clients;
    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u)));
    ASSERT_NE(clients[0], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[0]->connected()
                && server->connected(1u);
        }));
    const uint64_t oldSerial = server->connectionSerial(1u);
    static_cast<void>(drainFrames(*server));
    static_cast<void>(drainFrames(*clients[0]));

    const auto stale = payload(0xd1u, 27u);
    ASSERT_TRUE(clients[0]->send(
        0u, oldSerial, DeliveryClass::ReliableEvent, stale));
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return server->telemetry().queuedInboundFrames == 1u;
        }));
    clients[0]->close();
    ASSERT_TRUE(serviceServerUntil(
        *server, [&]() {
            return !server->connected(1u);
        }));
    ASSERT_EQ(server->telemetry().queuedInboundFrames, 1u);

    clients.push_back(NativeTcpClientTransport::create(
        clientConfig(server->listeningPort(), 1u)));
    ASSERT_NE(clients[1], nullptr);
    ASSERT_TRUE(serviceUntil(
        *server, clients, [&]() {
            return clients[1]->connected()
                && server->telemetry().reconnects == 1u;
        }));
    const uint64_t newSerial = server->connectionSerial(1u);
    ASSERT_GT(newSerial, oldSerial);
    EXPECT_EQ(server->telemetry().queuedInboundFrames, 0u);

    const auto frames = drainFrames(*server);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(
        frames[0].type,
        MultiplayerTransportFrameType::Disconnected);
    EXPECT_EQ(frames[0].connectionSerial, oldSerial);
    EXPECT_EQ(
        frames[1].type,
        MultiplayerTransportFrameType::Connected);
    EXPECT_EQ(frames[1].connectionSerial, newSerial);
}

#else

TEST(NativeTcpTransportTest, UnsupportedPlatformIsExplicit) {
    std::string error;
    EXPECT_EQ(
        NativeTcpServerTransport::create({}, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

#endif

} // namespace
} // namespace voxy::network

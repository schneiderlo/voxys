#pragma once

#include "network/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::network {

enum class TransportKind : uint32_t {
    None = 0,
    WebTransport = 1,
    WebRtcDataChannel = 2,
    Loopback = 3,
};

enum class TransportState : uint32_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Failed = 3,
};

struct TransportFrame {
    DeliveryClass delivery = DeliveryClass::Realtime;
    std::vector<std::byte> bytes;
};

struct TransportCallbacks {
    std::function<bool()> available;
    std::function<bool(std::string_view)> connect;
    // Channel 0 is realtime. WebRTC additionally uses channel 1 for reliable
    // unordered events and channel 2 for ordered reliable control.
    std::function<bool(uint32_t, std::span<const std::byte>)> send;
    std::function<std::optional<TransportFrame>()> poll;
    std::function<void()> close;
};

class ITransportEndpoint {
public:
    virtual ~ITransportEndpoint() = default;
    [[nodiscard]] virtual TransportKind kind() const noexcept = 0;
    [[nodiscard]] virtual TransportState state() const noexcept = 0;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual bool connect(std::string_view url) = 0;
    [[nodiscard]] virtual bool send(
        DeliveryClass delivery, std::span<const std::byte> bytes) = 0;
    [[nodiscard]] virtual std::optional<TransportFrame> poll() = 0;
    virtual void close() = 0;
};

class WebTransportEndpoint final : public ITransportEndpoint {
public:
    explicit WebTransportEndpoint(TransportCallbacks callbacks);
    [[nodiscard]] TransportKind kind() const noexcept override;
    [[nodiscard]] TransportState state() const noexcept override;
    [[nodiscard]] bool available() const override;
    [[nodiscard]] bool connect(std::string_view url) override;
    [[nodiscard]] bool send(
        DeliveryClass delivery, std::span<const std::byte> bytes) override;
    [[nodiscard]] std::optional<TransportFrame> poll() override;
    void close() override;

private:
    TransportCallbacks callbacks_;
    TransportState state_ = TransportState::Disconnected;
};

class WebRtcDataChannelEndpoint final : public ITransportEndpoint {
public:
    explicit WebRtcDataChannelEndpoint(TransportCallbacks callbacks);
    [[nodiscard]] TransportKind kind() const noexcept override;
    [[nodiscard]] TransportState state() const noexcept override;
    [[nodiscard]] bool available() const override;
    [[nodiscard]] bool connect(std::string_view url) override;
    [[nodiscard]] bool send(
        DeliveryClass delivery, std::span<const std::byte> bytes) override;
    [[nodiscard]] std::optional<TransportFrame> poll() override;
    void close() override;

private:
    TransportCallbacks callbacks_;
    TransportState state_ = TransportState::Disconnected;
};

struct GatewayTelemetry {
    uint64_t connectAttempts = 0;
    uint64_t primaryFailures = 0;
    uint64_t fallbackConnections = 0;
    uint64_t failovers = 0;
    uint64_t sentFrames = 0;
    uint64_t receivedFrames = 0;
    uint64_t rejectedFrames = 0;
    uint64_t sentBytes = 0;
    TransportKind active = TransportKind::None;
};

class RealtimeGateway {
public:
    RealtimeGateway(std::unique_ptr<ITransportEndpoint> primary,
                    std::unique_ptr<ITransportEndpoint> fallback);

    [[nodiscard]] bool connect(std::string url);
    [[nodiscard]] bool send(
        DeliveryClass delivery, std::span<const std::byte> bytes);
    [[nodiscard]] std::optional<TransportFrame> poll();
    [[nodiscard]] bool failover();
    void close();

    [[nodiscard]] TransportKind activeKind() const noexcept;
    [[nodiscard]] TransportState state() const noexcept;
    [[nodiscard]] const GatewayTelemetry& telemetry() const noexcept {
        return telemetry_;
    }

private:
    [[nodiscard]] bool connectEndpoint(ITransportEndpoint* endpoint);

    std::unique_ptr<ITransportEndpoint> primary_;
    std::unique_ptr<ITransportEndpoint> fallback_;
    ITransportEndpoint* active_ = nullptr;
    std::string url_;
    GatewayTelemetry telemetry_{};
};

} // namespace voxy::network

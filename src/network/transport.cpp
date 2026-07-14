#include "network/transport.hpp"

#include <utility>

namespace voxy::network {
namespace {

uint32_t webTransportChannel(DeliveryClass delivery) noexcept {
    return delivery == DeliveryClass::Realtime ? 0u : 1u;
}

uint32_t webRtcChannel(DeliveryClass delivery) noexcept {
    switch (delivery) {
        case DeliveryClass::Realtime: return 0u;
        case DeliveryClass::ReliableEvent: return 1u;
        case DeliveryClass::ReliableControl: return 2u;
    }
    return 2u;
}

bool validFrameSize(DeliveryClass delivery, size_t bytes) noexcept {
    if (bytes == 0u) return false;
    if (delivery == DeliveryClass::Realtime)
        return bytes <= kConservativeRealtimeMtu;
    return bytes <= 16u * 1024u * 1024u;
}

template <typename Endpoint>
bool connectCallbacks(Endpoint& endpoint, TransportCallbacks& callbacks,
                      TransportState& state, std::string_view url) {
    if (!callbacks.available || !callbacks.connect
        || !callbacks.available()) {
        state = TransportState::Failed;
        return false;
    }
    state = TransportState::Connecting;
    if (!callbacks.connect(url)) {
        state = TransportState::Failed;
        return false;
    }
    state = TransportState::Connected;
    static_cast<void>(endpoint);
    return true;
}

bool sendCallbacks(TransportCallbacks& callbacks, TransportState& state,
                   uint32_t channel, DeliveryClass delivery,
                   std::span<const std::byte> bytes) {
    if (state != TransportState::Connected || !callbacks.send
        || !validFrameSize(delivery, bytes.size())) return false;
    if (!callbacks.send(channel, bytes)) {
        state = TransportState::Failed;
        return false;
    }
    return true;
}

std::optional<TransportFrame> pollCallbacks(
    TransportCallbacks& callbacks, TransportState state) {
    if (state != TransportState::Connected || !callbacks.poll)
        return std::nullopt;
    return callbacks.poll();
}

void closeCallbacks(TransportCallbacks& callbacks, TransportState& state) {
    if (callbacks.close) callbacks.close();
    state = TransportState::Disconnected;
}

} // namespace

WebTransportEndpoint::WebTransportEndpoint(TransportCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

TransportKind WebTransportEndpoint::kind() const noexcept {
    return TransportKind::WebTransport;
}
TransportState WebTransportEndpoint::state() const noexcept { return state_; }
bool WebTransportEndpoint::available() const {
    return callbacks_.available && callbacks_.available();
}
bool WebTransportEndpoint::connect(std::string_view url) {
    return connectCallbacks(*this, callbacks_, state_, url);
}
bool WebTransportEndpoint::send(
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    return sendCallbacks(callbacks_, state_, webTransportChannel(delivery),
                         delivery, bytes);
}
std::optional<TransportFrame> WebTransportEndpoint::poll() {
    return pollCallbacks(callbacks_, state_);
}
void WebTransportEndpoint::close() { closeCallbacks(callbacks_, state_); }

WebRtcDataChannelEndpoint::WebRtcDataChannelEndpoint(
    TransportCallbacks callbacks) : callbacks_(std::move(callbacks)) {}

TransportKind WebRtcDataChannelEndpoint::kind() const noexcept {
    return TransportKind::WebRtcDataChannel;
}
TransportState WebRtcDataChannelEndpoint::state() const noexcept {
    return state_;
}
bool WebRtcDataChannelEndpoint::available() const {
    return callbacks_.available && callbacks_.available();
}
bool WebRtcDataChannelEndpoint::connect(std::string_view url) {
    return connectCallbacks(*this, callbacks_, state_, url);
}
bool WebRtcDataChannelEndpoint::send(
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    return sendCallbacks(callbacks_, state_, webRtcChannel(delivery),
                         delivery, bytes);
}
std::optional<TransportFrame> WebRtcDataChannelEndpoint::poll() {
    return pollCallbacks(callbacks_, state_);
}
void WebRtcDataChannelEndpoint::close() {
    closeCallbacks(callbacks_, state_);
}

RealtimeGateway::RealtimeGateway(
    std::unique_ptr<ITransportEndpoint> primary,
    std::unique_ptr<ITransportEndpoint> fallback)
    : primary_(std::move(primary)), fallback_(std::move(fallback)) {}

bool RealtimeGateway::connectEndpoint(ITransportEndpoint* endpoint) {
    if (endpoint == nullptr || !endpoint->available()) return false;
    ++telemetry_.connectAttempts;
    if (!endpoint->connect(url_)) return false;
    active_ = endpoint;
    telemetry_.active = endpoint->kind();
    return true;
}

bool RealtimeGateway::connect(std::string url) {
    close();
    url_ = std::move(url);
    if (connectEndpoint(primary_.get())) return true;
    ++telemetry_.primaryFailures;
    if (connectEndpoint(fallback_.get())) {
        ++telemetry_.fallbackConnections;
        return true;
    }
    return false;
}

bool RealtimeGateway::send(
    DeliveryClass delivery, std::span<const std::byte> bytes) {
    if (active_ == nullptr || !validFrameSize(delivery, bytes.size())) {
        ++telemetry_.rejectedFrames;
        return false;
    }
    if (!active_->send(delivery, bytes)) {
        ++telemetry_.rejectedFrames;
        if (active_ == primary_.get() && failover())
            return send(delivery, bytes);
        return false;
    }
    ++telemetry_.sentFrames;
    telemetry_.sentBytes += bytes.size();
    return true;
}

std::optional<TransportFrame> RealtimeGateway::poll() {
    if (active_ == nullptr) return std::nullopt;
    auto frame = active_->poll();
    if (frame.has_value()) ++telemetry_.receivedFrames;
    return frame;
}

bool RealtimeGateway::failover() {
    if (active_ != primary_.get()) return false;
    primary_->close();
    active_ = nullptr;
    telemetry_.active = TransportKind::None;
    if (!connectEndpoint(fallback_.get())) return false;
    ++telemetry_.fallbackConnections;
    ++telemetry_.failovers;
    return true;
}

void RealtimeGateway::close() {
    if (primary_) primary_->close();
    if (fallback_) fallback_->close();
    active_ = nullptr;
    telemetry_.active = TransportKind::None;
}

TransportKind RealtimeGateway::activeKind() const noexcept {
    return active_ != nullptr ? active_->kind() : TransportKind::None;
}

TransportState RealtimeGateway::state() const noexcept {
    return active_ != nullptr
        ? active_->state() : TransportState::Disconnected;
}

} // namespace voxy::network

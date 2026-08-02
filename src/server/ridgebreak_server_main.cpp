#include "network/native_tcp_transport.hpp"
#include "server/ridgebreak_server_runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

namespace {

volatile std::sig_atomic_t gStopRequested = 0;

extern "C" void requestStop(int) { gStopRequested = 1; }

struct Options {
    std::string bindAddress = "127.0.0.1";
    uint16_t port = 7778u;
    uint64_t maximumTicks = 0u;
    std::array<voxy::network::NativeTcpAuthenticationKey, 4u> keys{};
    std::array<bool, 4u> keyPresent{};
    voxy::network::NativeTcpContentDigest contentDigest{};
    bool digestPresent = false;
    bool help = false;
};

void usage(const char* executable) {
    std::cerr
        << "Usage: " << executable
        << " [--bind ADDRESS] [--port PORT] [--max-ticks COUNT]"
        << " --digest HEX64"
        << " --key1 HEX64 --key2 HEX64 --key3 HEX64 --key4 HEX64\n"
        << "This development server uses authenticated plaintext TCP.\n";
}

int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <size_t Size>
bool parseHex(std::string_view text, std::array<std::byte, Size>& output) {
    if (text.size() != Size * 2u) return false;
    for (size_t index = 0u; index < Size; ++index) {
        const int high = hexDigit(text[index * 2u]);
        const int low = hexDigit(text[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output[index] = std::byte{
            static_cast<uint8_t>((high << 4) | low)};
    }
    return true;
}

template <typename Integer>
bool parseInteger(std::string_view text, Integer& output) {
    Integer value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{}
        || result.ptr != text.data() + text.size()) return false;
    output = value;
    return true;
}

bool parseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            return true;
        }
        if (index + 1 >= argc) return false;
        const std::string_view value(argv[++index]);
        if (argument == "--bind") {
            if (value.empty()) return false;
            options.bindAddress = value;
        } else if (argument == "--port") {
            uint32_t port = 0u;
            if (!parseInteger(value, port)
                || port > std::numeric_limits<uint16_t>::max()) return false;
            options.port = static_cast<uint16_t>(port);
        } else if (argument == "--max-ticks") {
            if (!parseInteger(value, options.maximumTicks)) return false;
        } else if (argument == "--digest") {
            if (options.digestPresent
                || !parseHex(value, options.contentDigest)) return false;
            options.digestPresent = true;
        } else if (argument.size() == 6u
                   && argument.substr(0u, 5u) == "--key"
                   && argument[5] >= '1' && argument[5] <= '4') {
            const size_t key = static_cast<size_t>(argument[5] - '1');
            if (options.keyPresent[key]
                || !parseHex(value, options.keys[key])) return false;
            options.keyPresent[key] = true;
        } else {
            return false;
        }
    }
    return options.digestPresent
        && std::all_of(options.keyPresent.begin(), options.keyPresent.end(),
                       [](bool present) { return present; });
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        usage(argv[0]);
        return 1;
    }
    if (options.help) {
        usage(argv[0]);
        return 0;
    }

    std::cerr
        << "WARNING: ridgebreak_server uses authenticated plaintext TCP. "
        << "Use loopback/private development networks only.\n";
    voxy::network::NativeTcpServerConfig transportConfig;
    transportConfig.bindAddress = options.bindAddress;
    transportConfig.port = options.port;
    transportConfig.limits.maximumPeers = 4u;
    transportConfig.limits.maximumPendingConnections = 8u;
    transportConfig.limits.maximumFrameBytes =
        voxy::network::kConservativeRealtimeMtu;
    transportConfig.expectedContentDigest = options.contentDigest;
    for (uint32_t peerId = 1u; peerId <= 4u; ++peerId) {
        transportConfig.peers.push_back({
            .peerId = peerId,
            .authenticationKey = options.keys[peerId - 1u],
        });
    }
    std::string error;
    auto transport = voxy::network::NativeTcpServerTransport::create(
        std::move(transportConfig), &error);
    if (transport == nullptr) {
        std::cerr << "Failed to create RIDGEBREAK transport: " << error << '\n';
        return 2;
    }
    const uint16_t port = transport->listeningPort();

    voxy::server::RidgebreakServerRuntime runtime;
    voxy::server::RidgebreakServerRuntime::Config runtimeConfig;
    if (!runtime.initialize(runtimeConfig, std::move(transport))) {
        std::cerr << "Failed to initialize RIDGEBREAK authority runtime.\n";
        return 3;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::cerr << "RIDGEBREAK_SERVER_READY port=" << port << " tick_hz=60\n";
    using Clock = std::chrono::steady_clock;
    constexpr auto tickDuration = std::chrono::nanoseconds{16'666'667};
    auto nextTick = Clock::now();
    while (gStopRequested == 0
           && (options.maximumTicks == 0u
               || runtime.authority().tick() < options.maximumTicks)) {
        const auto tick = runtime.tickOnce();
        (void)tick;
        if (runtime.faulted()) {
            std::cerr << "RIDGEBREAK_SERVER_FAULT reason="
                      << static_cast<uint32_t>(runtime.fault())
                      << " registration="
                      << static_cast<uint32_t>(runtime.lastRegistrationError())
                      << '\n';
            runtime.close();
            return 4;
        }
        nextTick += tickDuration;
        std::this_thread::sleep_until(nextTick);
    }
    const auto& telemetry = runtime.telemetry();
    std::cerr << "RIDGEBREAK_SERVER_STOP tick=" << runtime.authority().tick()
              << " frames=" << telemetry.framesPolled
              << " inputs=" << telemetry.inputFramesAccepted
              << " rejected=" << telemetry.inputFramesRejected
              << " snapshots=" << telemetry.snapshotsSent << '\n';
    runtime.close();
    return 0;
}

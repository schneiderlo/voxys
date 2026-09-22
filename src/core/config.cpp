// ═══════════════════════════════════════════════════════════════════════════════
// config.cpp - Configuration System Implementation (C++20)
// ═══════════════════════════════════════════════════════════════════════════════

#include "config.hpp"
#include "log.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <charconv>
#include <cmath>
#include <format>

namespace voxy::config {

GameModeResolution resolveGameMode(const Config& config) noexcept {
    GameModeResolution result;
    if (config.game.mode) {
        const auto& mode = *config.game.mode;
        if (mode == "terrain") result.mode = GameMode::Terrain;
        else if (mode == "ridgebreak") result.mode = GameMode::Ridgebreak;
        else if (mode == "lego-shore") result.mode = GameMode::LegoShore;
        else if (mode == "lego-world") result.mode = GameMode::LegoWorld;
        else if (mode == "salvage") result.mode = GameMode::Salvage;
        else if (mode == "adventure") result.mode = GameMode::Adventure;
        else if (mode == "free-build") result.mode = GameMode::FreeBuild;
        else result.status = GameModeStatus::UnknownMode;
    } else if (config.window.title == "RIDGEBREAK") {
        result.mode = GameMode::Ridgebreak;
    } else if (config.window.title == "LEGO SHORE") {
        result.mode = GameMode::LegoShore;
    } else if (config.window.title == "LEGO WORLD") {
        result.mode = GameMode::LegoWorld;
    }
    if (result.ready() && (result.mode == GameMode::Salvage || result.mode == GameMode::Adventure || result.mode == GameMode::FreeBuild)
        && validateWreckwaterClientConfig(config.wreckwaterClient)
            != WreckwaterClientConfigStatus::Disabled) {
        result.status = GameModeStatus::ConflictingBootstrap;
    }
    if (result.ready() && config.game.assetFixtureRegistry
        && (result.mode != GameMode::Salvage || config.game.assetFixtureRegistry->empty()
            || config.game.assetFixtureRegistry->size() > 4096
            || config.game.assetFixtureRegistry->find('\0') != std::string::npos)) {
        result.status = GameModeStatus::InvalidAssetFixture;
    }
    if (result.ready() && config.game.assetFixtureCatalog
        && (!config.game.assetFixtureRegistry || config.game.assetFixtureCatalog->empty()
            || config.game.assetFixtureCatalog->size()>4096
            || config.game.assetFixtureCatalog->find('\0')!=std::string::npos))
        result.status=GameModeStatus::InvalidAssetFixture;
    if (result.ready() && ((config.game.assetFixtureGuides != "off"
            && config.game.assetFixtureGuides != "dimensions" && config.game.assetFixtureGuides != "sockets")
        || (config.game.assetFixtureGuides != "off" && !config.game.assetFixtureRegistry)))
        result.status = GameModeStatus::InvalidAssetFixture;
    if (result.ready() && ((config.game.assetFixtureLod != "auto"
            && config.game.assetFixtureLod != "near" && config.game.assetFixtureLod != "middle"
            && config.game.assetFixtureLod != "far")
        || (config.game.assetFixtureLod != "auto" && !config.game.assetFixtureRegistry)))
        result.status = GameModeStatus::InvalidAssetFixture;
    if (result.ready() && ((config.game.assetFixtureLighting != "legacy"
            && config.game.assetFixtureLighting != "filtered")
        || (config.game.assetFixtureLighting != "legacy" && !config.game.assetFixtureRegistry)))
        result.status = GameModeStatus::InvalidAssetFixture;
    if (result.ready() && ((config.game.assetFixtureAnchor != "inspection"
            && config.game.assetFixtureAnchor != "water")
        || (config.game.assetFixtureAnchor != "inspection" && !config.game.assetFixtureRegistry)))
        result.status = GameModeStatus::InvalidAssetFixture;
    return result;
}

const char* gameModeStatusName(GameModeStatus status) noexcept {
    switch (status) {
    case GameModeStatus::Ready: return "ready";
    case GameModeStatus::UnknownMode: return "unknown game mode";
    case GameModeStatus::ConflictingBootstrap: return "game mode conflicts with WRECKWATER bootstrap";
    case GameModeStatus::InvalidAssetFixture: return "asset fixture requires salvage mode and a valid installed registry path";
    }
    return "invalid game mode status";
}

// ─────────────────────────────────────────────────────────────────────────────
// Global State
// ─────────────────────────────────────────────────────────────────────────────

namespace {

Config globalConfig;
bool initialized = false;

bool validBroadPhaseCellSize(float cellSize) noexcept {
    constexpr float worldSectorSize = 256.0f;
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) return false;
    const float cellsPerSector = worldSectorSize / cellSize;
    const float rounded = std::round(cellsPerSector);
    return rounded >= 1.0f && rounded <= 2'097'152.0f
        && std::abs(cellsPerSector - rounded) <= 1e-5f;
}

template<typename T>
[[nodiscard]] bool parseExactUnsigned(
    std::string_view value, T& output) noexcept {
    static_assert(std::is_unsigned_v<T>);
    T candidate = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), candidate);
    if (value.empty() || error != std::errc{}
        || end != value.data() + value.size()) {
        return false;
    }
    output = candidate;
    return true;
}

[[nodiscard]] int hexDigit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

void scrubString(std::string& value) noexcept {
    volatile char* bytes = value.data();
    for (size_t index = 0u; index < value.size(); ++index) {
        bytes[index] = '\0';
    }
    value.clear();
}

void scrubCString(char* value) noexcept {
    if (value == nullptr) return;
    volatile char* bytes = value;
    while (*bytes != '\0') {
        *bytes = '\0';
        ++bytes;
    }
}

void scrubWreckwaterKeyArguments(
    int argc, char** argv) noexcept {
    if (argc <= 0 || argv == nullptr) return;
    for (int index = 0; index < argc; ++index) {
        if (argv[index] == nullptr
            || std::string_view{argv[index]}
                != "--wreckwater-key") {
            continue;
        }
        if (index + 1 < argc) {
            scrubCString(argv[index + 1]);
            ++index;
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

std::string trim(std::string_view str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return std::string{str.substr(start, end - start + 1)};
}

bool parseBool(std::string_view value, bool defaultValue) noexcept {
    if (value.empty()) return defaultValue;

    const auto equalsIgnoringCase = [value](std::string_view expected) {
        if (value.size() != expected.size()) return false;
        for (size_t index = 0; index < value.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(value[index]))
                != std::tolower(
                    static_cast<unsigned char>(expected[index])))
                return false;
        }
        return true;
    };

    if (equalsIgnoringCase("true") || equalsIgnoringCase("yes")
        || value == "1" || equalsIgnoringCase("on")) {
        return true;
    }
    if (equalsIgnoringCase("false") || equalsIgnoringCase("no")
        || value == "0" || equalsIgnoringCase("off")) {
        return false;
    }
    return defaultValue;
}

float parseFloat(std::string_view value, float defaultValue) noexcept {
    if (value.empty()) return defaultValue;
    
    // C++20: Use std::from_chars for parsing (faster and safer)
    float result = defaultValue;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec == std::errc{} && ptr == value.data() + value.size() && std::isfinite(result)) {
        return result;
    }
    return defaultValue;
}

int parseInt(std::string_view value, int defaultValue) noexcept {
    if (value.empty()) return defaultValue;
    
    // C++20: Use std::from_chars for parsing
    int result = defaultValue;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec == std::errc{} && ptr == value.data() + value.size()) {
        return result;
    }
    return defaultValue;
}

bool parseWreckwaterAuthenticationKey(
    std::string_view value,
    std::array<std::byte, 32>& output) noexcept {
    if (value.size() != output.size() * 2u) return false;
    std::array<std::byte, 32> candidate{};
    for (size_t index = 0u; index < candidate.size(); ++index) {
        const int high = hexDigit(value[index * 2u]);
        const int low = hexDigit(value[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        candidate[index] = static_cast<std::byte>(
            static_cast<uint8_t>((high << 4) | low));
    }
    output = candidate;
    return true;
}

const char* wreckwaterClientConfigStatusName(
    WreckwaterClientConfigStatus status) noexcept {
    switch (status) {
        case WreckwaterClientConfigStatus::Disabled:
            return "disabled";
        case WreckwaterClientConfigStatus::Ready:
            return "ready";
        case WreckwaterClientConfigStatus::Incomplete:
            return "incomplete";
        case WreckwaterClientConfigStatus::MalformedValue:
            return "malformed value";
        case WreckwaterClientConfigStatus::InvalidServer:
            return "invalid server";
        case WreckwaterClientConfigStatus::InvalidPort:
            return "invalid port";
        case WreckwaterClientConfigStatus::InvalidPeer:
            return "invalid peer";
        case WreckwaterClientConfigStatus::InvalidKey:
            return "invalid key";
        case WreckwaterClientConfigStatus::InvalidIdentity:
            return "invalid identity";
    }
    return "unknown";
}

WreckwaterClientConfigStatus validateWreckwaterClientConfig(
    const WreckwaterClientConfig& config) noexcept {
    if (config.presentFields == 0u && !config.malformedValue) {
        return WreckwaterClientConfigStatus::Disabled;
    }
    if (config.malformedValue) {
        return WreckwaterClientConfigStatus::MalformedValue;
    }
    if (config.presentFields != kWreckwaterClientRequiredFields) {
        return WreckwaterClientConfigStatus::Incomplete;
    }
    if (config.server.empty() || config.server.size() > 255u) {
        return WreckwaterClientConfigStatus::InvalidServer;
    }
    for (const char character : config.server) {
        const auto value =
            static_cast<unsigned char>(character);
        if (value <= 0x20u || value == 0x7fu) {
            return WreckwaterClientConfigStatus::InvalidServer;
        }
    }
    if (config.port == 0u) {
        return WreckwaterClientConfigStatus::InvalidPort;
    }
    // The graphical roster is the fixed four-player 2v2 product roster.
    if (config.peerId == 0u || config.peerId > 4u) {
        return WreckwaterClientConfigStatus::InvalidPeer;
    }
    bool anyKeyByte = false;
    for (const std::byte value : config.authenticationKey) {
        anyKeyByte = anyKeyByte || value != std::byte{};
    }
    if (!anyKeyByte) {
        return WreckwaterClientConfigStatus::InvalidKey;
    }
    if (config.sessionId == 0u || config.matchId == 0u
        || config.worldId == 0u || config.worldEpoch == 0u
        || config.authorityEpoch == 0u) {
        return WreckwaterClientConfigStatus::InvalidIdentity;
    }
    return WreckwaterClientConfigStatus::Ready;
}

namespace {

// Parse a vec3 from string like "[0.5, 0.8, 0.3]"
std::array<float, 3> parseVec3(std::string_view value, const std::array<float, 3>& defaultValue) {
    if (value.size() < 2u || value.front() != '[' || value.back() != ']')
        return defaultValue;
    const auto inner = value.substr(1u, value.size() - 2u);
    std::array<float, 3> result{};
    size_t pos = 0;
    for (size_t component = 0; component < result.size(); ++component) {
        const auto comma = inner.find(',', pos);
        if ((component + 1u < result.size())
                != (comma != std::string_view::npos))
            return defaultValue;
        const auto token = comma == std::string_view::npos
            ? inner.substr(pos) : inner.substr(pos, comma - pos);
        const std::string clean = trim(token);
        float parsed = 0.0f;
        const auto [end, error] = std::from_chars(
            clean.data(), clean.data() + clean.size(), parsed);
        if (clean.empty() || error != std::errc{}
            || end != clean.data() + clean.size()
            || !std::isfinite(parsed))
            return defaultValue;
        result[component] = parsed;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return result;
}

// Remove quotes from string value
std::string unquote(std::string_view str) {
    if (str.size() >= 2 && 
        ((str.front() == '"' && str.back() == '"') ||
         (str.front() == '\'' && str.back() == '\''))) {
        const char quote = str.front();
        std::string result;
        result.reserve(str.size() - 2);
        const auto inner = str.substr(1, str.size() - 2);
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] != '\\' || i + 1u >= inner.size()) {
                result += inner[i];
                continue;
            }
            const char escaped = inner[i + 1u];
            switch (escaped) {
                case '0': result += '\0'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '\\': result += '\\'; break;
                default:
                    if (escaped == quote) {
                        result += quote;
                    } else {
                        // Keep unknown escapes lossless.
                        result += '\\';
                        continue;
                    }
                    break;
            }
            ++i;
        }
        return result;
    }
    return std::string{str};
}

size_t findClosingQuote(std::string_view value) {
    const char quote = value.front();
    bool escaped = false;
    for (size_t i = 1; i < value.size(); ++i) {
        if (!escaped && value[i] == quote) {
            return i;
        }
        if (!escaped && value[i] == '\\') {
            escaped = true;
        } else {
            escaped = false;
        }
    }
    return std::string_view::npos;
}

std::string quote(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result += '"';
    for (const char c : value) {
        switch (c) {
            case '\0': result += R"(\0)"; break;
            case '\b': result += R"(\b)"; break;
            case '\f': result += R"(\f)"; break;
            case '\n': result += R"(\n)"; break;
            case '\r': result += R"(\r)"; break;
            case '\t': result += R"(\t)"; break;
            case '"': result += R"(\")"; break;
            case '\\': result += R"(\\)"; break;
            default: result += c; break;
        }
    }
    result += '"';
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Command-Line Argument Parsing
// ─────────────────────────────────────────────────────────────────────────────

CommandLineArgs parseArgs(std::span<char*> args) {
    CommandLineArgs result;
    
    for (size_t i = 1; i < args.size(); ++i) {
        if (!args[i]) {
            LOG_WARN("Ignoring null command-line argument {}", i);
            continue;
        }
        std::string_view arg = args[i];
        const auto hasNext = [&] {
            return i + 1u < args.size() && args[i + 1u] != nullptr;
        };
        
        if (arg == "--help" || arg == "-h") {
            result.help = true;
        } else if (arg == "--config" && hasNext()) {
            result.configPath = args[++i];
        } else if (arg == "--render-path" && hasNext()) {
            result.renderPath = args[++i];
        } else if (arg == "--heightmap" && hasNext()) {
            result.heightmap = args[++i];
        } else if (arg == "--physics-backend" && hasNext()) {
            result.physicsBackend = args[++i];
        } else if (arg == "--physics-max-bodies" && hasNext()) {
            result.gpuMaxBodies = std::max(parseInt(args[++i], 131072), 2);
        } else if (arg == "--physics-cpu-fallback") {
            result.physicsCpuFallback = true;
        } else if (arg == "--no-physics-cpu-fallback") {
            result.physicsCpuFallback = false;
        } else if (arg == "--jolt-job-system" && hasNext()) {
            result.joltJobSystem = args[++i];
        } else if (arg == "--jolt-workers" && hasNext()) {
            result.joltWorkerThreads = std::max(parseInt(args[++i], 0), 0);
        } else if (arg == "--box3d-workers" && hasNext()) {
            result.box3dWorkerThreads = std::max(parseInt(args[++i], 1), 1);
        } else if (arg == "--width" && hasNext()) {
            result.width = parseInt(args[++i], 1280);
        } else if (arg == "--height" && hasNext()) {
            result.height = parseInt(args[++i], 720);
        } else if (arg == "--vsync") {
            result.vsync = true;
        } else if (arg == "--uncapped" || arg == "--no-vsync") {
            result.vsync = false;
        } else if (arg == "--fullscreen") {
            result.fullscreen = true;
        } else if (arg == "--log-level" && hasNext()) {
            result.logLevel = args[++i];
        } else if (arg == "--no-validation") {
            result.noValidation = true;
        } else if (arg == "--benchmark") {
            result.benchmark = true;
        } else if (arg == "--benchmark-bodies" && hasNext()) {
            result.benchmarkBodies = std::max(parseInt(args[++i], 0), 0);
            result.benchmark = true;
        } else if (arg == "--benchmark-min-fps" && hasNext()) {
            result.benchmarkMinimumFps = std::max(
                parseFloat(args[++i], 0.0f), 0.0f);
            result.benchmark = true;
        } else if (arg == "--benchmark-fixed-hz" && hasNext()) {
            result.benchmarkFixedHz = std::max(
                parseFloat(args[++i], 0.0f), 0.0f);
            result.benchmark = true;
        } else if (arg == "--inspection-motion" || arg == "--inspection-motion-output") {
            // Retain an empty value on missing input so native startup rejects
            // an incomplete capture request instead of silently running normally.
            auto& value = arg == "--inspection-motion" ? result.inspectionMotionRecipe : result.inspectionMotionOutput;
            value = hasNext() ? std::string(args[++i]) : std::string{};
        } else if (arg == "--teleport-index" && hasNext()) {
            result.teleportIndex = parseInt(args[++i], 0);
        } else if (arg == "--screenshot" && hasNext()) {
            result.screenshotPath = args[++i];
        } else if (arg == "--screenshot-frames" && hasNext()) {
            result.screenshotFrames = parseInt(args[++i], 10);
        } else if (arg == "--screenshot-tour" && hasNext()) {
            result.screenshotTourCount = std::clamp(
                parseInt(args[++i], 0), 0, 256);
        } else if (arg == "--screenshot-dir" && hasNext()) {
            result.screenshotDir = args[++i];
        } else if (arg == "--wreckwater-server") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientServerField;
            if (hasNext()) {
                result.wreckwaterClient.server = args[++i];
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-port") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientPortField;
            uint16_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.port = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-peer") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientPeerField;
            uint32_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.peerId = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-key") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientKeyField;
            std::array<std::byte, 32> value{};
            if (hasNext()
                && parseWreckwaterAuthenticationKey(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.authenticationKey = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-session") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientSessionField;
            uint64_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.sessionId = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-match") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientMatchField;
            uint64_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.matchId = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-world") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientWorldField;
            uint64_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.worldId = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-world-epoch") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientWorldEpochField;
            uint32_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.worldEpoch = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else if (arg == "--wreckwater-authority-epoch") {
            result.wreckwaterClient.presentFields |=
                kWreckwaterClientAuthorityEpochField;
            uint32_t value = 0u;
            if (hasNext()
                && parseExactUnsigned(
                    std::string_view{args[++i]}, value)) {
                result.wreckwaterClient.authorityEpoch = value;
            } else {
                result.wreckwaterClient.malformedValue = true;
            }
        } else {
            LOG_WARN("Unknown argument: {}", arg);
        }
    }
    
    return result;
}

CommandLineArgs parseArgs(int argc, char** argv) {
    if (argc <= 0 || !argv) return {};
    return parseArgs(std::span{argv, static_cast<size_t>(argc)});
}

void printHelp(std::string_view programName) {
    std::printf(
        "Usage: %.*s [options]\n\n"
        "Options:\n"
        "  --help, -h              Show this help message\n"
        "  --config <path>         Config file path (default: voxy.cfg)\n"
        "  --expedition-world <id> Native: resume this saved cove (32 lowercase hex digits)\n"
        "  --expedition-root <dir> Native: absolute save root; P then F10 saves\n"
        "  --render-path <path>    Override render path (raycast|triangle)\n"
        "  --heightmap <path>      Override heightmap file\n"
        "  --physics-backend <b>   Physics backend (jolt|box3d|webgpu)\n"
        "  --physics-max-bodies <n> WebGPU resident slots (default: 131072)\n"
        "  --physics-cpu-fallback  Fall back from WebGPU to Box3D (default)\n"
        "  --no-physics-cpu-fallback Fail if WebGPU physics cannot initialize\n"
        "  --jolt-job-system <m>   Jolt scheduler (single_threaded|thread_pool)\n"
        "  --jolt-workers <n>      Jolt worker threads (0 = automatic)\n"
        "  --box3d-workers <n>     Box3D worker threads (default: 1)\n"
        "  --width <n>             Window width\n"
        "  --height <n>            Window height\n"
        "  --uncapped              Use immediate presentation (no refresh cap)\n"
        "  --vsync                 Use FIFO presentation\n"
        "  --fullscreen            Start in fullscreen mode\n"
        "  --log-level <level>     Set log level (trace|debug|info|warn|error)\n"
        "  --no-validation         Disable WebGPU validation layers\n"
        "  --benchmark             Run in benchmark mode\n"
        "  --benchmark-bodies N    Spawn N deterministic benchmark bodies\n"
        "  --benchmark-min-fps N   Fail if aggregate throughput is below N FPS\n"
        "  --benchmark-fixed-hz N  Advance scripted time at exactly N frames/s\n"
        "  --teleport-index <n>    Teleport to stored target index on startup\n"
        "  --screenshot <path>     Save screenshot to path after N frames and exit\n"
        "  --screenshot-frames <n> Frames to render before screenshot (default: 10)\n"
        "  --screenshot-tour <n>   Capture screenshots for teleport indices [0..n-1]\n"
        "  --screenshot-dir <dir>  Output directory for screenshot tour (default: screenshots)\n"
        "  --inspection-motion <json> Native asset camera-motion recipe (capture overhead; not a benchmark)\n"
        "  --inspection-motion-output <dir> New, empty output path for the motion recording\n"
        "\n"
        "Native WRECKWATER client (all fields are required):\n"
        "  --wreckwater-server <host>\n"
        "  --wreckwater-port <1..65535>\n"
        "  --wreckwater-peer <1..4>\n"
        "  --wreckwater-key <64 hex digits>\n"
        "  --wreckwater-session <nonzero id>\n"
        "  --wreckwater-match <nonzero id>\n"
        "  --wreckwater-world <nonzero id>\n"
        "  --wreckwater-world-epoch <nonzero id>\n"
        "  --wreckwater-authority-epoch <nonzero id>\n"
        "\n",
        static_cast<int>(programName.size()), programName.data()
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration File Parsing
// ─────────────────────────────────────────────────────────────────────────────

Config load(std::string_view path) {
    Config config;
    
    std::ifstream file{std::string{path}};
    if (!file.is_open()) {
        if (path != "voxy.cfg") {
            LOG_DEBUG("Config file not found: {} (using defaults)", path);
            return config;
        }
        // Try absolute path at root (common for WASM virtual filesystem)
        std::string absPath = "/voxy.cfg";
        file.open(absPath);
        if (!file.is_open()) {
            LOG_DEBUG("Config file not found: {} (using defaults)", path);
            return config;
        }
        LOG_INFO("Loading config from absolute path: {}", absPath);
    } else {
        LOG_INFO("Loading config: {}", path);
    }
    std::string currentSection;
    std::string line;
    uint64_t lineNum = 0;
    
    while (std::getline(file, line)) {
        lineNum++;
        auto trimmedLine = trim(line);
        
        // Skip empty lines and comments
        if (trimmedLine.empty() || trimmedLine[0] == '#') {
            continue;
        }
        
        // Section header
        if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
            currentSection = trimmedLine.substr(1, trimmedLine.size() - 2);
            continue;
        }
        
        // Key-value pair
        auto eqPos = trimmedLine.find('=');
        if (eqPos == std::string::npos) {
            LOG_WARN("Config line {}: invalid format (missing '=')", lineNum);
            continue;
        }
        
        std::string key = trim(trimmedLine.substr(0, eqPos));
        std::string value = trim(trimmedLine.substr(eqPos + 1));
        // A malformed explicit route must not silently fall back to a legacy title.
        if (currentSection == "game" && key == "mode") config.game.mode = "";
        if (currentSection == "game" && key == "asset_fixture_registry") config.game.assetFixtureRegistry = "";
        if (currentSection == "game" && key == "asset_fixture_catalog") config.game.assetFixtureCatalog = "";
        if (currentSection == "game" && key == "asset_fixture_guides") config.game.assetFixtureGuides = "";
        if (currentSection == "game" && key == "asset_fixture_lod") config.game.assetFixtureLod = "";
        if (currentSection == "game" && key == "asset_fixture_lighting") config.game.assetFixtureLighting = "";
        if (currentSection == "game" && key == "asset_fixture_anchor") config.game.assetFixtureAnchor = "";
        
        // Strip inline comments (but be careful with # inside quotes)
        if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
            auto closeQuote = findClosingQuote(value);
            if (closeQuote == std::string::npos) {
                LOG_WARN("Config line {}: unterminated quoted value", lineNum);
                continue;
            }
            const std::string trailing = trim(value.substr(closeQuote + 1u));
            if (!trailing.empty() && trailing.front() != '#') {
                LOG_WARN("Config line {}: invalid text after quoted value",
                         lineNum);
                continue;
            }
            value = value.substr(0, closeQuote + 1);
        } else {
            auto commentPos = value.find('#');
            if (commentPos != std::string::npos) {
                value = trim(value.substr(0, commentPos));
            }
        }
        
        value = unquote(value);
        bool sensitiveValue = false;
        
        // Apply value based on section and key
        if (currentSection == "game") {
            if (key == "mode") config.game.mode = value;
            else if (key == "asset_fixture_registry") config.game.assetFixtureRegistry = value;
            else if (key == "asset_fixture_catalog") config.game.assetFixtureCatalog = value;
            else if (key == "asset_fixture_guides") config.game.assetFixtureGuides = value;
            else if (key == "asset_fixture_lod") config.game.assetFixtureLod = value;
            else if (key == "asset_fixture_lighting") config.game.assetFixtureLighting = value;
            else if (key == "asset_fixture_anchor") config.game.assetFixtureAnchor = value;
        }
        else if (currentSection == "render") {
            if (key == "path") config.render.path = value;
            else if (key == "resolution_scale") config.render.resolutionScale = parseFloat(value, config.render.resolutionScale);
            else if (key == "vsync") config.render.vsync = parseBool(value, config.render.vsync);
            else if (key == "max_fps") config.render.maxFps = parseInt(value, config.render.maxFps);
        }
        else if (currentSection == "terrain") {
            if (key == "heightmap") config.terrain.heightmap = value;
            else if (key == "albedo") config.terrain.albedo = value;
            else if (key == "lightmap") config.terrain.lightmap = value;
            else if (key == "height_scale") config.terrain.heightScale = parseFloat(value, config.terrain.heightScale);
            else if (key == "cell_scale") config.terrain.cellScale = parseFloat(value, config.terrain.cellScale);
            else if (key == "ambient_light") config.lighting.ambientIntensity = parseFloat(value, config.lighting.ambientIntensity);
        }
        else if (currentSection == "water") {
            if (key == "enabled") config.water.enabled = parseBool(value, config.water.enabled);
            else if (key == "height") config.water.height = parseFloat(value, config.water.height);
            else if (key == "shallow_color") config.water.shallowColor = parseVec3(value, config.water.shallowColor);
            else if (key == "deep_color") config.water.deepColor = parseVec3(value, config.water.deepColor);
            else if (key == "roughness") config.water.roughness = parseFloat(value, config.water.roughness);
            else if (key == "wave_strength") config.water.waveStrength = parseFloat(value, config.water.waveStrength);
            else if (key == "reflection_strength") {
                config.water.reflectionStrength = parseFloat(value, config.water.reflectionStrength);
                if (config.water.reflectionStrength < 0.0f ||
                    config.water.reflectionStrength > 1.0f) {
                    LOG_WARN("Config line {}: [water] reflection_strength {} is "
                             "outside [0, 1] and will be clamped",
                             lineNum, config.water.reflectionStrength);
                    config.water.reflectionStrength =
                        std::clamp(config.water.reflectionStrength, 0.0f, 1.0f);
                }
            }
            else if (key == "shore_fade") config.water.shoreFade = parseFloat(value, config.water.shoreFade);
        }
        else if (currentSection == "physics") {
            if (key == "backend") config.physics.backend = value;
            else if (key == "gpu_max_bodies") {
                config.physics.gpuMaxBodies = std::max(
                    parseInt(value, config.physics.gpuMaxBodies), 2);
            }
            else if (key == "broad_phase_cell_size") {
                const float parsed = parseFloat(value, -1.0f);
                if (validBroadPhaseCellSize(parsed)) {
                    config.physics.broadPhaseCellSize = parsed;
                } else {
                    LOG_WARN("Config line {}: [physics] broad_phase_cell_size "
                             "must be positive and evenly divide 256; keeping {}",
                             lineNum, config.physics.broadPhaseCellSize);
                }
            }
            else if (key == "allow_cpu_fallback") {
                config.physics.allowCpuFallback = parseBool(
                    value, config.physics.allowCpuFallback);
            }
            else if (key == "jolt_job_system") {
                config.physics.joltJobSystem = value;
            }
            else if (key == "jolt_worker_threads") {
                config.physics.joltWorkerThreads = std::max(
                    parseInt(value, config.physics.joltWorkerThreads), 0);
            }
            else if (key == "box3d_worker_threads") {
                config.physics.box3dWorkerThreads = std::max(
                    parseInt(value, config.physics.box3dWorkerThreads), 1);
            }
        }
        else if (currentSection == "camera") {
            if (key == "fov") config.camera.fov = parseFloat(value, config.camera.fov);
            else if (key == "near_plane") config.camera.nearPlane = parseFloat(value, config.camera.nearPlane);
            else if (key == "far_plane") config.camera.farPlane = parseFloat(value, config.camera.farPlane);
            else if (key == "move_speed") config.camera.moveSpeed = parseFloat(value, config.camera.moveSpeed);
            else if (key == "mouse_sensitivity") config.camera.mouseSensitivity = parseFloat(value, config.camera.mouseSensitivity);
            else if (key == "eye_height") config.camera.eyeHeight = parseFloat(value, config.camera.eyeHeight);
        }
        else if (currentSection == "lighting") {
            if (key == "day_night_enabled") config.lighting.dayNightEnabled = parseBool(value, config.lighting.dayNightEnabled);
            else if (key == "day_cycle_minutes") config.lighting.dayCycleMinutes = parseFloat(value, config.lighting.dayCycleMinutes);
            else if (key == "day_start_hour") config.lighting.dayStartHour = parseFloat(value, config.lighting.dayStartHour);
            else if (key == "sun_direction") config.lighting.sunDirection = parseVec3(value, config.lighting.sunDirection);
            else if (key == "sun_color") config.lighting.sunColor = parseVec3(value, config.lighting.sunColor);
            else if (key == "ambient_color") config.lighting.ambientColor = parseVec3(value, config.lighting.ambientColor);
            else if (key == "fog_density") config.lighting.fogDensity = parseFloat(value, config.lighting.fogDensity);
            else if (key == "fog_color") config.lighting.fogColor = parseVec3(value, config.lighting.fogColor);
        }
        else if (currentSection == "debug") {
            if (key == "show_stats") config.debug.showStats = parseBool(value, config.debug.showStats);
            else if (key == "show_wireframe") config.debug.showWireframe = parseBool(value, config.debug.showWireframe);
            else if (key == "log_level") config.debug.logLevel = value;
            else if (key == "enable_validation") config.debug.enableValidation = parseBool(value, config.debug.enableValidation);
        }
        else if (currentSection == "window") {
            if (key == "width") config.window.width = parseInt(value, config.window.width);
            else if (key == "height") config.window.height = parseInt(value, config.window.height);
            else if (key == "fullscreen") config.window.fullscreen = parseBool(value, config.window.fullscreen);
            else if (key == "title") config.window.title = value;
        }
        else if (currentSection == "wreckwater_client") {
            auto& client = config.wreckwaterClient;
            if (key == "server") {
                client.presentFields |=
                    kWreckwaterClientServerField;
                client.server = value;
            } else if (key == "port") {
                client.presentFields |=
                    kWreckwaterClientPortField;
                uint16_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.port = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "peer") {
                client.presentFields |=
                    kWreckwaterClientPeerField;
                uint32_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.peerId = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "key") {
                sensitiveValue = true;
                client.presentFields |=
                    kWreckwaterClientKeyField;
                std::array<std::byte, 32> parsed{};
                if (parseWreckwaterAuthenticationKey(
                        value, parsed)) {
                    client.authenticationKey = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "session") {
                client.presentFields |=
                    kWreckwaterClientSessionField;
                uint64_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.sessionId = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "match") {
                client.presentFields |=
                    kWreckwaterClientMatchField;
                uint64_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.matchId = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "world") {
                client.presentFields |=
                    kWreckwaterClientWorldField;
                uint64_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.worldId = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "world_epoch") {
                client.presentFields |=
                    kWreckwaterClientWorldEpochField;
                uint32_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.worldEpoch = parsed;
                } else {
                    client.malformedValue = true;
                }
            } else if (key == "authority_epoch") {
                client.presentFields |=
                    kWreckwaterClientAuthorityEpochField;
                uint32_t parsed = 0u;
                if (parseExactUnsigned(value, parsed)) {
                    client.authorityEpoch = parsed;
                } else {
                    client.malformedValue = true;
                }
            }
        }
        if (sensitiveValue) {
            scrubString(value);
            scrubString(trimmedLine);
            scrubString(line);
        }
    }
    
    return config;
}

Config load(std::string_view path, const CommandLineArgs& args) {
    Config config = load(path);
    
    // Apply command-line overrides
    if (args.renderPath) config.render.path = *args.renderPath;
    if (args.heightmap) config.terrain.heightmap = *args.heightmap;
    if (args.physicsBackend) config.physics.backend = *args.physicsBackend;
    if (args.gpuMaxBodies) config.physics.gpuMaxBodies = *args.gpuMaxBodies;
    if (args.physicsCpuFallback) {
        config.physics.allowCpuFallback = *args.physicsCpuFallback;
    }
    if (args.joltJobSystem) config.physics.joltJobSystem = *args.joltJobSystem;
    if (args.joltWorkerThreads) {
        config.physics.joltWorkerThreads = *args.joltWorkerThreads;
    }
    if (args.box3dWorkerThreads) {
        config.physics.box3dWorkerThreads = *args.box3dWorkerThreads;
    }
    if (args.width) config.window.width = *args.width;
    if (args.height) config.window.height = *args.height;
    if (args.vsync) config.render.vsync = *args.vsync;
    if (args.fullscreen) config.window.fullscreen = true;
    if (args.logLevel) config.debug.logLevel = *args.logLevel;
    if (args.noValidation) config.debug.enableValidation = false;
    
    // Apply automation settings
    config.automation.inspectionMotionRecipe = args.inspectionMotionRecipe;
    config.automation.inspectionMotionOutput = args.inspectionMotionOutput;
    config.automation.benchmark = args.benchmark;
    config.automation.benchmarkBodies = args.benchmarkBodies;
    config.automation.benchmarkMinimumFps = args.benchmarkMinimumFps;
    config.automation.benchmarkFixedHz = args.benchmarkFixedHz;
    config.automation.teleportIndex = args.teleportIndex;
    config.automation.screenshotPath = args.screenshotPath;
    config.automation.screenshotFrames = args.screenshotFrames;
    config.automation.screenshotTourCount = args.screenshotTourCount;
    config.automation.screenshotDir = args.screenshotDir;

    const auto& source = args.wreckwaterClient;
    auto& target = config.wreckwaterClient;
    if ((source.presentFields
         & kWreckwaterClientServerField) != 0u) {
        target.server = source.server;
    }
    if ((source.presentFields
         & kWreckwaterClientPortField) != 0u) {
        target.port = source.port;
    }
    if ((source.presentFields
         & kWreckwaterClientPeerField) != 0u) {
        target.peerId = source.peerId;
    }
    if ((source.presentFields
         & kWreckwaterClientKeyField) != 0u) {
        target.authenticationKey = source.authenticationKey;
    }
    if ((source.presentFields
         & kWreckwaterClientSessionField) != 0u) {
        target.sessionId = source.sessionId;
    }
    if ((source.presentFields
         & kWreckwaterClientMatchField) != 0u) {
        target.matchId = source.matchId;
    }
    if ((source.presentFields
         & kWreckwaterClientWorldField) != 0u) {
        target.worldId = source.worldId;
    }
    if ((source.presentFields
         & kWreckwaterClientWorldEpochField) != 0u) {
        target.worldEpoch = source.worldEpoch;
    }
    if ((source.presentFields
         & kWreckwaterClientAuthorityEpochField) != 0u) {
        target.authorityEpoch = source.authorityEpoch;
    }
    target.presentFields |= source.presentFields;
    target.malformedValue =
        target.malformedValue || source.malformedValue;
    
    return config;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Saving (using C++20 std::format)
// ─────────────────────────────────────────────────────────────────────────────

bool save(const Config& config, std::string_view path) {
    std::ofstream file{std::string{path}};
    if (!file.is_open()) {
        LOG_ERROR("Failed to save config: {}", path);
        return false;
    }
    
    file << "# voxy configuration file\n\n";
    
    file << "[render]\n";
    file << std::format("path = {}\n", quote(config.render.path));
    file << std::format("resolution_scale = {}\n", config.render.resolutionScale);
    file << std::format("vsync = {}\n", config.render.vsync ? "true" : "false");
    file << std::format("max_fps = {}\n\n", config.render.maxFps);
    
    file << "[terrain]\n";
    file << std::format("heightmap = {}\n", quote(config.terrain.heightmap));
    if (!config.terrain.albedo.empty()) file << std::format("albedo = {}\n", quote(config.terrain.albedo));
    if (!config.terrain.lightmap.empty()) file << std::format("lightmap = {}\n", quote(config.terrain.lightmap));
    file << std::format("height_scale = {}\n", config.terrain.heightScale);
    file << std::format("cell_scale = {}\n", config.terrain.cellScale);
    file << std::format("ambient_light = {}\n\n", config.lighting.ambientIntensity);

    file << "[physics]\n";
    file << std::format("backend = {}\n", quote(config.physics.backend));
    file << std::format("gpu_max_bodies = {}\n",
                        config.physics.gpuMaxBodies);
    file << std::format("broad_phase_cell_size = {}\n",
                        config.physics.broadPhaseCellSize);
    file << std::format("allow_cpu_fallback = {}\n",
                        config.physics.allowCpuFallback ? "true" : "false");
    file << std::format("jolt_job_system = {}\n",
                        quote(config.physics.joltJobSystem));
    file << std::format("jolt_worker_threads = {}\n",
                        config.physics.joltWorkerThreads);
    file << std::format("box3d_worker_threads = {}\n\n",
                        config.physics.box3dWorkerThreads);

    file << "[water]\n";
    file << std::format("enabled = {}\n", config.water.enabled ? "true" : "false");
    file << std::format("height = {}\n", config.water.height);
    file << std::format("shallow_color = [{}, {}, {}]\n",
                        config.water.shallowColor[0],
                        config.water.shallowColor[1],
                        config.water.shallowColor[2]);
    file << std::format("deep_color = [{}, {}, {}]\n",
                        config.water.deepColor[0],
                        config.water.deepColor[1],
                        config.water.deepColor[2]);
    file << std::format("roughness = {}\n", config.water.roughness);
    file << std::format("wave_strength = {}\n", config.water.waveStrength);
    file << std::format("reflection_strength = {}\n", config.water.reflectionStrength);
    file << std::format("shore_fade = {}\n\n", config.water.shoreFade);
    
    file << "[camera]\n";
    file << std::format("fov = {}\n", config.camera.fov);
    file << std::format("near_plane = {}\n", config.camera.nearPlane);
    file << std::format("far_plane = {}\n", config.camera.farPlane);
    file << std::format("move_speed = {}\n", config.camera.moveSpeed);
    file << std::format("mouse_sensitivity = {}\n", config.camera.mouseSensitivity);
    file << std::format("eye_height = {}\n\n", config.camera.eyeHeight);
    
    file << "[lighting]\n";
    file << std::format("day_night_enabled = {}\nday_cycle_minutes = {}\nday_start_hour = {}\n",
        config.lighting.dayNightEnabled, config.lighting.dayCycleMinutes, config.lighting.dayStartHour);
    file << std::format("sun_direction = [{}, {}, {}]\n", 
                        config.lighting.sunDirection[0], 
                        config.lighting.sunDirection[1], 
                        config.lighting.sunDirection[2]);
    file << std::format("sun_color = [{}, {}, {}]\n", 
                        config.lighting.sunColor[0], 
                        config.lighting.sunColor[1], 
                        config.lighting.sunColor[2]);
    file << std::format("ambient_color = [{}, {}, {}]\n", 
                        config.lighting.ambientColor[0], 
                        config.lighting.ambientColor[1], 
                        config.lighting.ambientColor[2]);
    file << std::format("fog_density = {}\n", config.lighting.fogDensity);
    file << std::format("fog_color = [{}, {}, {}]\n\n", 
                        config.lighting.fogColor[0], 
                        config.lighting.fogColor[1], 
                        config.lighting.fogColor[2]);
    
    file << "[debug]\n";
    file << std::format("show_stats = {}\n", config.debug.showStats ? "true" : "false");
    file << std::format("show_wireframe = {}\n", config.debug.showWireframe ? "true" : "false");
    file << std::format("log_level = {}\n", quote(config.debug.logLevel));
    file << std::format("enable_validation = {}\n\n", config.debug.enableValidation ? "true" : "false");
    
    if (config.game.mode || config.game.assetFixtureRegistry || config.game.assetFixtureCatalog || config.game.assetFixtureGuides != "off"
        || config.game.assetFixtureLod != "auto" || config.game.assetFixtureLighting != "legacy"
        || config.game.assetFixtureAnchor != "inspection") {
        file << "[game]\n";
        if (config.game.mode) file << std::format("mode = {}\n", quote(*config.game.mode));
        if (config.game.assetFixtureRegistry) file << std::format("asset_fixture_registry = {}\n", quote(*config.game.assetFixtureRegistry));
        if (config.game.assetFixtureCatalog) file << std::format("asset_fixture_catalog = {}\n", quote(*config.game.assetFixtureCatalog));
        file << std::format("asset_fixture_guides = {}\n", quote(config.game.assetFixtureGuides));
        file << std::format("asset_fixture_lod = {}\n", quote(config.game.assetFixtureLod));
        file << std::format("asset_fixture_lighting = {}\n", quote(config.game.assetFixtureLighting));
        file << std::format("asset_fixture_anchor = {}\n", quote(config.game.assetFixtureAnchor));
        file << '\n';
    }
    file << "[window]\n";
    file << std::format("width = {}\n", config.window.width);
    file << std::format("height = {}\n", config.window.height);
    file << std::format("fullscreen = {}\n", config.window.fullscreen ? "true" : "false");
    file << std::format("title = {}\n", quote(config.window.title));
    
    file.close();
    if (file.fail()) {
        LOG_ERROR("Failed while writing config: {}", path);
        return false;
    }
    LOG_INFO("Saved config: {}", path);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Configuration
// ─────────────────────────────────────────────────────────────────────────────

void init(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    
    if (args.help) {
        printHelp(argc > 0 && argv && argv[0]
            ? std::string_view{argv[0]} : std::string_view{"voxy"});
        std::exit(0);
    }
    
    globalConfig = load(args.configPath, args);
    // C and C++ entry-point argument strings are writable. Remove the
    // printable CLI credential as soon as its binary form is loaded.
    // Shell history and process-monitor snapshots taken before this point
    // remain outside the process's control.
    scrubWreckwaterKeyArguments(argc, argv);
    initialized = true;
    
    // Apply log level from config
    log::setLevel(log::levelFromString(globalConfig.debug.logLevel));
}

const Config& get() noexcept {
    return globalConfig;
}

Config& getMutable() noexcept {
    return globalConfig;
}

} // namespace voxy::config

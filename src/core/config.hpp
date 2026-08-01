// ═══════════════════════════════════════════════════════════════════════════════
// config.hpp - Configuration System (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Simple configuration file parsing and command-line argument handling.
// Supports TOML-like format for human readability.
// Updated for C++20 with designated initializers, std::span, and concepts.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace voxy::config {

// ─────────────────────────────────────────────────────────────────────────────
// C++20 Concepts for Configuration Types
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
concept ConfigValue = std::integral<T> || std::floating_point<T>
                   || std::same_as<std::remove_cv_t<T>, std::string>;

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Structures (using C++20 designated initializers style)
// ─────────────────────────────────────────────────────────────────────────────

struct RenderConfig {
    std::string path = "raycast";       // "raycast" or "triangle"
    float resolutionScale = 1.0f;       // Render resolution multiplier
    bool vsync = true;                  // VSync enabled
    int maxFps = 0;                     // 0 = unlimited
    
    // C++20: Default comparison
    [[nodiscard]] constexpr auto operator<=>(const RenderConfig&) const = default;
};

struct TerrainConfig {
    std::string heightmap = "data/Rugged Terrain with Rocky Peaks Height Map PNG.png";
    std::string albedo = "data/canyon_diffuse.jpg";
    std::string lightmap;
    float heightScale = 500.0f;         // Vertical scale factor
    float cellScale = 1.0f;             // Horizontal scale factor
    
    [[nodiscard]] constexpr auto operator<=>(const TerrainConfig&) const = default;
};

struct WaterConfig {
    bool enabled = true;
    float height = -230.0f;
    std::array<float, 3> shallowColor = {0.12f, 0.46f, 0.50f};
    std::array<float, 3> deepColor = {0.0f, 0.28f, 0.42f};
    float roughness = 0.05f;
    float waveStrength = 1.0f;
    float reflectionStrength = 0.42f;   // Reflection amount, valid range [0, 1]
    float shoreFade = 12.0f;            // Water depth (world units) over which
                                        // the colour fades from shallow to deep

    [[nodiscard]] constexpr auto operator<=>(const WaterConfig&) const = default;
};

struct PhysicsConfig {
    std::string backend = "webgpu";
    int gpuMaxBodies = 131072;
    // Must evenly divide the GPU world's 256 m sector size.
    float broadPhaseCellSize = 4.0f;
    bool allowCpuFallback = true;
    std::string joltJobSystem = "thread_pool";
    int joltWorkerThreads = 0; // 0 lets Jolt choose in thread-pool mode.
    int box3dWorkerThreads = 1;

    [[nodiscard]] constexpr auto operator<=>(const PhysicsConfig&) const = default;
};

struct CameraConfig {
    float fov = 60.0f;                  // Field of view (degrees)
    float nearPlane = 0.1f;             // Near clipping plane
    float farPlane = 10000.0f;          // Far clipping plane
    float moveSpeed = 50.0f;            // Movement speed (units/sec)
    float mouseSensitivity = 0.002f;    // Mouse look sensitivity
    float eyeHeight = 1.8f;             // Eye height above ground (units)
    
    [[nodiscard]] constexpr auto operator<=>(const CameraConfig&) const = default;
};

struct LightingConfig {
    std::array<float, 3> sunDirection = {0.5f, 0.8f, 0.3f};
    std::array<float, 3> sunColor = {1.0f, 0.95f, 0.9f};
    std::array<float, 3> ambientColor = {0.1f, 0.12f, 0.15f};
    float ambientIntensity = 1.3f;
    float fogDensity = 0.0001f;
    std::array<float, 3> fogColor = {0.6f, 0.7f, 0.8f};
    
    [[nodiscard]] constexpr auto operator<=>(const LightingConfig&) const = default;
};

struct DebugConfig {
    bool showStats = true;              // Show FPS/stats overlay
    bool showWireframe = false;         // Wireframe rendering
    std::string logLevel = "info";      // Log level string
    bool enableValidation = true;       // WebGPU validation layers
    
    [[nodiscard]] constexpr auto operator<=>(const DebugConfig&) const = default;
};

struct WindowConfig {
    int width = 1280;                   // Window width
    int height = 720;                   // Window height
    bool fullscreen = false;            // Fullscreen mode
    std::string title = "voxy";         // Window title
    
    [[nodiscard]] constexpr auto operator<=>(const WindowConfig&) const = default;
};

struct AutomationConfig {
    bool benchmark = false;
    int benchmarkBodies = 0;
    float benchmarkMinimumFps = 0.0f;
    float benchmarkFixedHz = 0.0f;
    std::optional<int> teleportIndex;
    std::optional<std::string> screenshotPath;
    int screenshotFrames = 10;
    // Optional multi-screenshot tour
    int screenshotTourCount = 0;                 // Number of teleport targets to capture (0 = disabled)
    std::optional<std::string> screenshotDir;    // Directory for tour output

    // Optional comparison requires custom implementation or exclusion
    // For now, simpler equality check or just rely on default if optional supports it in C++20
    bool operator==(const AutomationConfig&) const = default;
};

inline constexpr uint32_t kWreckwaterClientServerField =
    1u << 0u;
inline constexpr uint32_t kWreckwaterClientPortField =
    1u << 1u;
inline constexpr uint32_t kWreckwaterClientPeerField =
    1u << 2u;
inline constexpr uint32_t kWreckwaterClientKeyField =
    1u << 3u;
inline constexpr uint32_t kWreckwaterClientSessionField =
    1u << 4u;
inline constexpr uint32_t kWreckwaterClientMatchField =
    1u << 5u;
inline constexpr uint32_t kWreckwaterClientWorldField =
    1u << 6u;
inline constexpr uint32_t kWreckwaterClientWorldEpochField =
    1u << 7u;
inline constexpr uint32_t kWreckwaterClientAuthorityEpochField =
    1u << 8u;
inline constexpr uint32_t kWreckwaterClientRequiredFields =
    (1u << 9u) - 1u;

struct WreckwaterClientConfig {
    std::string server;
    uint16_t port = 0u;
    uint32_t peerId = 0u;
    std::array<std::byte, 32> authenticationKey{};
    uint64_t sessionId = 0u;
    uint64_t matchId = 0u;
    uint64_t worldId = 0u;
    uint32_t worldEpoch = 0u;
    uint32_t authorityEpoch = 0u;
    uint32_t presentFields = 0u;
    bool malformedValue = false;

    [[nodiscard]] bool operator==(
        const WreckwaterClientConfig&) const = default;
};

enum class WreckwaterClientConfigStatus : uint32_t {
    Disabled = 0u,
    Ready,
    Incomplete,
    MalformedValue,
    InvalidServer,
    InvalidPort,
    InvalidPeer,
    InvalidKey,
    InvalidIdentity,
};

[[nodiscard]] const char* wreckwaterClientConfigStatusName(
    WreckwaterClientConfigStatus status) noexcept;

[[nodiscard]] WreckwaterClientConfigStatus
validateWreckwaterClientConfig(
    const WreckwaterClientConfig& config) noexcept;

// Parses exactly 64 hexadecimal digits into 32 bytes. The printable input is
// never copied into persistent configuration state.
[[nodiscard]] bool parseWreckwaterAuthenticationKey(
    std::string_view value,
    std::array<std::byte, 32>& output) noexcept;

struct Config {
    RenderConfig render;
    TerrainConfig terrain;
    WaterConfig water;
    PhysicsConfig physics;
    CameraConfig camera;
    LightingConfig lighting;
    DebugConfig debug;
    WindowConfig window;
    AutomationConfig automation;
    WreckwaterClientConfig wreckwaterClient;
    
    [[nodiscard]] constexpr auto operator<=>(const Config&) const = delete;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command-Line Arguments
// ─────────────────────────────────────────────────────────────────────────────

struct CommandLineArgs {
    std::string configPath = "voxy.cfg";
    std::optional<std::string> renderPath;
    std::optional<std::string> heightmap;
    std::optional<std::string> physicsBackend;
    std::optional<int> gpuMaxBodies;
    std::optional<bool> physicsCpuFallback;
    std::optional<std::string> joltJobSystem;
    std::optional<int> joltWorkerThreads;
    std::optional<int> box3dWorkerThreads;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<bool> vsync;
    bool fullscreen = false;
    std::optional<std::string> logLevel;
    bool noValidation = false;
    bool benchmark = false;
    int benchmarkBodies = 0;
    float benchmarkMinimumFps = 0.0f;
    float benchmarkFixedHz = 0.0f;
    bool help = false;

    // Automated Screenshot System
    std::optional<int> teleportIndex;
    std::optional<std::string> screenshotPath;
    int screenshotFrames = 10;
    int screenshotTourCount = 0;
    std::optional<std::string> screenshotDir;
    WreckwaterClientConfig wreckwaterClient;
};

// Parse command-line arguments (C++20: using span for safe array access)
[[nodiscard]] CommandLineArgs parseArgs(std::span<char*> args);

// Legacy overload for compatibility
[[nodiscard]] CommandLineArgs parseArgs(int argc, char** argv);

// Print help message
void printHelp(std::string_view programName);

// ─────────────────────────────────────────────────────────────────────────────
// Configuration Loading/Saving
// ─────────────────────────────────────────────────────────────────────────────

// Load configuration from file (returns default config if file not found)
[[nodiscard]] Config load(std::string_view path);

// Load configuration with command-line overrides
[[nodiscard]] Config load(std::string_view path, const CommandLineArgs& args);

// Save configuration to file
bool save(const Config& config, std::string_view path);

// ─────────────────────────────────────────────────────────────────────────────
// Global Configuration Access
// ─────────────────────────────────────────────────────────────────────────────

// Initialize global config (call once at startup)
void init(int argc, char** argv);

// Get global configuration (read-only)
[[nodiscard]] const Config& get() noexcept;

// Get mutable reference to global configuration
[[nodiscard]] Config& getMutable() noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

// Parse a boolean value from string
[[nodiscard]] bool parseBool(std::string_view value, bool defaultValue = false) noexcept;

// Parse a float value from string
[[nodiscard]] float parseFloat(std::string_view value, float defaultValue = 0.0f) noexcept;

// Parse an integer value from string
[[nodiscard]] int parseInt(std::string_view value, int defaultValue = 0) noexcept;

// Trim whitespace from string
[[nodiscard]] std::string trim(std::string_view str);

// C++20: Concept-constrained parse function template
template<ConfigValue T>
[[nodiscard]] T parse(std::string_view value, T defaultValue = T{})
    noexcept(!std::same_as<std::remove_cv_t<T>, std::string>) {
    if constexpr (std::same_as<T, bool>) {
        return parseBool(value, defaultValue);
    } else if constexpr (std::integral<T>) {
        T result = defaultValue;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size()
            ? result : defaultValue;
    } else if constexpr (std::floating_point<T>) {
        T result = defaultValue;
        const auto [end, error] = std::from_chars(
            value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size()
            && std::isfinite(result) ? result : defaultValue;
    } else if constexpr (std::same_as<T, std::string>) {
        return std::string{value};
    }
}

} // namespace voxy::config

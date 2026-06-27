// ═══════════════════════════════════════════════════════════════════════════════
// config.hpp - Configuration System (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Simple configuration file parsing and command-line argument handling.
// Supports TOML-like format for human readability.
// Updated for C++20 with designated initializers, std::span, and concepts.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <array>
#include <span>
#include <concepts>
#include <cstdint>

namespace voxy::config {

// ─────────────────────────────────────────────────────────────────────────────
// C++20 Concepts for Configuration Types
// ─────────────────────────────────────────────────────────────────────────────

template<typename T>
concept ConfigValue = std::integral<T> || std::floating_point<T> || 
                      std::same_as<T, std::string> || std::same_as<T, bool>;

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
    bool enableDecorations = true;      // Biome-driven vegetation/decorations
    int decorationSpacingCells = 10;    // Candidate spacing for tree placement
    int maxTreeInstances = 22000;       // Total generated tree cap
    
    [[nodiscard]] constexpr auto operator<=>(const TerrainConfig&) const = default;
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
    float ambientIntensity = 0.3f;
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

struct Config {
    RenderConfig render;
    TerrainConfig terrain;
    CameraConfig camera;
    LightingConfig lighting;
    DebugConfig debug;
    WindowConfig window;
    AutomationConfig automation;
    
    [[nodiscard]] constexpr auto operator<=>(const Config&) const = delete;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command-Line Arguments
// ─────────────────────────────────────────────────────────────────────────────

struct CommandLineArgs {
    std::string configPath = "voxy.cfg";
    std::optional<std::string> renderPath;
    std::optional<std::string> heightmap;
    std::optional<int> width;
    std::optional<int> height;
    bool fullscreen = false;
    std::optional<std::string> logLevel;
    bool noValidation = false;
    bool benchmark = false;
    bool help = false;

    // Automated Screenshot System
    std::optional<int> teleportIndex;
    std::optional<std::string> screenshotPath;
    int screenshotFrames = 10;
    int screenshotTourCount = 0;
    std::optional<std::string> screenshotDir;
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
[[nodiscard]] T parse(std::string_view value, T defaultValue = T{}) noexcept {
    if constexpr (std::same_as<T, bool>) {
        return parseBool(value, defaultValue);
    } else if constexpr (std::same_as<T, float>) {
        return parseFloat(value, defaultValue);
    } else if constexpr (std::same_as<T, int>) {
        return parseInt(value, defaultValue);
    } else if constexpr (std::same_as<T, std::string>) {
        return std::string{value};
    }
}

} // namespace voxy::config

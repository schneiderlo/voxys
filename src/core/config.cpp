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
#include <format>
#include <iostream>

namespace voxy::config {

// ─────────────────────────────────────────────────────────────────────────────
// Global State
// ─────────────────────────────────────────────────────────────────────────────

namespace {

Config globalConfig;
bool initialized = false;

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
    
    // Convert to lowercase for comparison
    std::string lower;
    lower.reserve(value.size());
    for (char c : value) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    if (lower == "true" || lower == "yes" || lower == "1" || lower == "on") {
        return true;
    }
    if (lower == "false" || lower == "no" || lower == "0" || lower == "off") {
        return false;
    }
    return defaultValue;
}

float parseFloat(std::string_view value, float defaultValue) noexcept {
    if (value.empty()) return defaultValue;
    
    // C++20: Use std::from_chars for parsing (faster and safer)
    float result = defaultValue;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec == std::errc{}) {
        return result;
    }
    return defaultValue;
}

int parseInt(std::string_view value, int defaultValue) noexcept {
    if (value.empty()) return defaultValue;
    
    // C++20: Use std::from_chars for parsing
    int result = defaultValue;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec == std::errc{}) {
        return result;
    }
    return defaultValue;
}

namespace {

// Parse a vec3 from string like "[0.5, 0.8, 0.3]"
std::array<float, 3> parseVec3(std::string_view value, const std::array<float, 3>& defaultValue) {
    std::array<float, 3> result = defaultValue;
    
    // Find brackets
    auto start = value.find('[');
    auto end = value.find(']');
    if (start == std::string_view::npos || end == std::string_view::npos) {
        return defaultValue;
    }
    
    auto inner = value.substr(start + 1, end - start - 1);
    
    // Parse comma-separated values
    size_t i = 0;
    size_t pos = 0;
    while (pos < inner.size() && i < 3) {
        auto comma = inner.find(',', pos);
        auto token = (comma != std::string_view::npos) 
                   ? inner.substr(pos, comma - pos) 
                   : inner.substr(pos);
        
        result[i] = parseFloat(trim(token), defaultValue[i]);
        i++;
        
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
        return std::string{str.substr(1, str.size() - 2)};
    }
    return std::string{str};
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Command-Line Argument Parsing
// ─────────────────────────────────────────────────────────────────────────────

CommandLineArgs parseArgs(std::span<char*> args) {
    CommandLineArgs result;
    
    for (size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            result.help = true;
        } else if (arg == "--config" && i + 1 < args.size()) {
            result.configPath = args[++i];
        } else if (arg == "--render-path" && i + 1 < args.size()) {
            result.renderPath = args[++i];
        } else if (arg == "--heightmap" && i + 1 < args.size()) {
            result.heightmap = args[++i];
        } else if (arg == "--width" && i + 1 < args.size()) {
            result.width = parseInt(args[++i], 1280);
        } else if (arg == "--height" && i + 1 < args.size()) {
            result.height = parseInt(args[++i], 720);
        } else if (arg == "--fullscreen") {
            result.fullscreen = true;
        } else if (arg == "--log-level" && i + 1 < args.size()) {
            result.logLevel = args[++i];
        } else if (arg == "--no-validation") {
            result.noValidation = true;
        } else if (arg == "--benchmark") {
            result.benchmark = true;
        } else if (arg == "--teleport-index" && i + 1 < args.size()) {
            result.teleportIndex = parseInt(args[++i], 0);
        } else if (arg == "--screenshot" && i + 1 < args.size()) {
            result.screenshotPath = args[++i];
        } else if (arg == "--screenshot-frames" && i + 1 < args.size()) {
            result.screenshotFrames = parseInt(args[++i], 10);
        } else if (arg == "--screenshot-tour" && i + 1 < args.size()) {
            result.screenshotTourCount = parseInt(args[++i], 0);
        } else if (arg == "--screenshot-dir" && i + 1 < args.size()) {
            result.screenshotDir = args[++i];
        } else {
            LOG_WARN("Unknown argument: {}", arg);
        }
    }
    
    return result;
}

CommandLineArgs parseArgs(int argc, char** argv) {
    return parseArgs(std::span{argv, static_cast<size_t>(argc)});
}

void printHelp(std::string_view programName) {
    std::printf(
        "Usage: %.*s [options]\n\n"
        "Options:\n"
        "  --help, -h              Show this help message\n"
        "  --config <path>         Config file path (default: voxy.cfg)\n"
        "  --render-path <path>    Override render path (raycast|triangle)\n"
        "  --heightmap <path>      Override heightmap file\n"
        "  --width <n>             Window width\n"
        "  --height <n>            Window height\n"
        "  --fullscreen            Start in fullscreen mode\n"
        "  --log-level <level>     Set log level (trace|debug|info|warn|error)\n"
        "  --no-validation         Disable WebGPU validation layers\n"
        "  --benchmark             Run in benchmark mode\n"
        "  --teleport-index <n>    Teleport to stored target index on startup\n"
        "  --screenshot <path>     Save screenshot to path after N frames and exit\n"
        "  --screenshot-frames <n> Frames to render before screenshot (default: 10)\n"
        "  --screenshot-tour <n>   Capture screenshots for teleport indices [0..n-1]\n"
        "  --screenshot-dir <dir>  Output directory for screenshot tour (default: screenshots)\n"
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
    std::cout << "DEBUG: Loading config from: " << (file.is_open() ? path : "/voxy.cfg") << std::endl;
    
    std::string currentSection;
    std::string line;
    int lineNum = 0;
    
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
        
        // Strip inline comments (but be careful with # inside quotes)
        if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
            char quote = value[0];
            auto closeQuote = value.find(quote, 1);
            if (closeQuote != std::string::npos) {
                value = value.substr(0, closeQuote + 1);
            }
        } else {
            auto commentPos = value.find('#');
            if (commentPos != std::string::npos) {
                value = trim(value.substr(0, commentPos));
            }
        }
        
        value = unquote(value);
        
        // Apply value based on section and key
        if (currentSection == "render") {
            if (key == "path") config.render.path = value;
            else if (key == "resolution_scale") config.render.resolutionScale = parseFloat(value, 1.0f);
            else if (key == "vsync") config.render.vsync = parseBool(value, true);
            else if (key == "max_fps") config.render.maxFps = parseInt(value, 0);
        }
        else if (currentSection == "terrain") {
            if (key == "heightmap") config.terrain.heightmap = value;
            else if (key == "albedo") config.terrain.albedo = value;
            else if (key == "lightmap") config.terrain.lightmap = value;
            else if (key == "height_scale") config.terrain.heightScale = parseFloat(value, 500.0f);
            else if (key == "cell_scale") config.terrain.cellScale = parseFloat(value, 1.0f);
            else if (key == "enable_decorations") config.terrain.enableDecorations = parseBool(value, true);
            else if (key == "decoration_spacing_cells") config.terrain.decorationSpacingCells = parseInt(value, 10);
            else if (key == "max_tree_instances") config.terrain.maxTreeInstances = parseInt(value, 22000);
            else if (key == "ambient_light") config.lighting.ambientIntensity = parseFloat(value, 0.3f);
        }
        else if (currentSection == "camera") {
            if (key == "fov") config.camera.fov = parseFloat(value, 60.0f);
            else if (key == "near_plane") config.camera.nearPlane = parseFloat(value, 0.1f);
            else if (key == "far_plane") config.camera.farPlane = parseFloat(value, 10000.0f);
            else if (key == "move_speed") config.camera.moveSpeed = parseFloat(value, 50.0f);
            else if (key == "mouse_sensitivity") config.camera.mouseSensitivity = parseFloat(value, 0.002f);
            else if (key == "eye_height") config.camera.eyeHeight = parseFloat(value, 1.8f);
        }
        else if (currentSection == "lighting") {
            if (key == "sun_direction") config.lighting.sunDirection = parseVec3(value, config.lighting.sunDirection);
            else if (key == "sun_color") config.lighting.sunColor = parseVec3(value, config.lighting.sunColor);
            else if (key == "ambient_color") config.lighting.ambientColor = parseVec3(value, config.lighting.ambientColor);
            else if (key == "fog_density") config.lighting.fogDensity = parseFloat(value, 0.0001f);
            else if (key == "fog_color") config.lighting.fogColor = parseVec3(value, config.lighting.fogColor);
        }
        else if (currentSection == "debug") {
            if (key == "show_stats") config.debug.showStats = parseBool(value, true);
            else if (key == "show_wireframe") config.debug.showWireframe = parseBool(value, false);
            else if (key == "log_level") config.debug.logLevel = value;
            else if (key == "enable_validation") config.debug.enableValidation = parseBool(value, true);
        }
        else if (currentSection == "window") {
            if (key == "width") config.window.width = parseInt(value, 1280);
            else if (key == "height") config.window.height = parseInt(value, 720);
            else if (key == "fullscreen") config.window.fullscreen = parseBool(value, false);
            else if (key == "title") config.window.title = value;
        }
    }
    
    return config;
}

Config load(std::string_view path, const CommandLineArgs& args) {
    Config config = load(path);
    
    // Apply command-line overrides
    if (args.renderPath) config.render.path = *args.renderPath;
    if (args.heightmap) config.terrain.heightmap = *args.heightmap;
    if (args.width) config.window.width = *args.width;
    if (args.height) config.window.height = *args.height;
    if (args.fullscreen) config.window.fullscreen = true;
    if (args.logLevel) config.debug.logLevel = *args.logLevel;
    if (args.noValidation) config.debug.enableValidation = false;
    
    // Apply automation settings
    config.automation.benchmark = args.benchmark;
    config.automation.teleportIndex = args.teleportIndex;
    config.automation.screenshotPath = args.screenshotPath;
    config.automation.screenshotFrames = args.screenshotFrames;
    config.automation.screenshotTourCount = args.screenshotTourCount;
    config.automation.screenshotDir = args.screenshotDir;
    
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
    file << std::format("path = \"{}\"\n", config.render.path);
    file << std::format("resolution_scale = {}\n", config.render.resolutionScale);
    file << std::format("vsync = {}\n", config.render.vsync ? "true" : "false");
    file << std::format("max_fps = {}\n\n", config.render.maxFps);
    
    file << "[terrain]\n";
    file << std::format("heightmap = \"{}\"\n", config.terrain.heightmap);
    if (!config.terrain.albedo.empty()) file << std::format("albedo = \"{}\"\n", config.terrain.albedo);
    if (!config.terrain.lightmap.empty()) file << std::format("lightmap = \"{}\"\n", config.terrain.lightmap);
    file << std::format("height_scale = {}\n", config.terrain.heightScale);
    file << std::format("cell_scale = {}\n", config.terrain.cellScale);
    file << std::format("enable_decorations = {}\n", config.terrain.enableDecorations ? "true" : "false");
    file << std::format("decoration_spacing_cells = {}\n", config.terrain.decorationSpacingCells);
    file << std::format("max_tree_instances = {}\n\n", config.terrain.maxTreeInstances);
    
    file << "[camera]\n";
    file << std::format("fov = {}\n", config.camera.fov);
    file << std::format("near_plane = {}\n", config.camera.nearPlane);
    file << std::format("far_plane = {}\n", config.camera.farPlane);
    file << std::format("move_speed = {}\n", config.camera.moveSpeed);
    file << std::format("mouse_sensitivity = {}\n\n", config.camera.mouseSensitivity);
    
    file << "[lighting]\n";
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
    file << std::format("log_level = \"{}\"\n", config.debug.logLevel);
    file << std::format("enable_validation = {}\n\n", config.debug.enableValidation ? "true" : "false");
    
    file << "[window]\n";
    file << std::format("width = {}\n", config.window.width);
    file << std::format("height = {}\n", config.window.height);
    file << std::format("fullscreen = {}\n", config.window.fullscreen ? "true" : "false");
    file << std::format("title = \"{}\"\n", config.window.title);
    
    LOG_INFO("Saved config: {}", path);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Configuration
// ─────────────────────────────────────────────────────────────────────────────

void init(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    
    if (args.help) {
        printHelp(argv[0]);
        std::exit(0);
    }
    
    globalConfig = load(args.configPath, args);
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

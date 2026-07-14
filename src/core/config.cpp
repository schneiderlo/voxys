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
        const char quote = str.front();
        std::string result;
        result.reserve(str.size() - 2);
        const auto inner = str.substr(1, str.size() - 2);
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '\\' && i + 1 < inner.size() &&
                (inner[i + 1] == quote || inner[i + 1] == '\\')) {
                result += inner[++i];
            } else {
                result += inner[i];
            }
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
        if (c == '"' || c == '\\') {
            result += '\\';
        }
        result += c;
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
        std::string_view arg = args[i];
        
        if (arg == "--help" || arg == "-h") {
            result.help = true;
        } else if (arg == "--config" && i + 1 < args.size()) {
            result.configPath = args[++i];
        } else if (arg == "--render-path" && i + 1 < args.size()) {
            result.renderPath = args[++i];
        } else if (arg == "--heightmap" && i + 1 < args.size()) {
            result.heightmap = args[++i];
        } else if (arg == "--physics-backend" && i + 1 < args.size()) {
            result.physicsBackend = args[++i];
        } else if (arg == "--physics-max-bodies" && i + 1 < args.size()) {
            result.gpuMaxBodies = std::max(parseInt(args[++i], 131072), 2);
        } else if (arg == "--physics-cpu-fallback") {
            result.physicsCpuFallback = true;
        } else if (arg == "--no-physics-cpu-fallback") {
            result.physicsCpuFallback = false;
        } else if (arg == "--jolt-job-system" && i + 1 < args.size()) {
            result.joltJobSystem = args[++i];
        } else if (arg == "--jolt-workers" && i + 1 < args.size()) {
            result.joltWorkerThreads = std::max(parseInt(args[++i], 0), 0);
        } else if (arg == "--box3d-workers" && i + 1 < args.size()) {
            result.box3dWorkerThreads = std::max(parseInt(args[++i], 1), 1);
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
        "  --physics-backend <b>   Physics backend (jolt|box3d|webgpu)\n"
        "  --physics-max-bodies <n> WebGPU resident slots (default: 131072)\n"
        "  --physics-cpu-fallback  Fall back from WebGPU to Box3D (default)\n"
        "  --no-physics-cpu-fallback Fail if WebGPU physics cannot initialize\n"
        "  --jolt-job-system <m>   Jolt scheduler (single_threaded|thread_pool)\n"
        "  --jolt-workers <n>      Jolt worker threads (0 = automatic)\n"
        "  --box3d-workers <n>     Box3D worker threads (default: 1)\n"
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
            auto closeQuote = findClosingQuote(value);
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
            if (key == "sun_direction") config.lighting.sunDirection = parseVec3(value, config.lighting.sunDirection);
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
    
    file << "[window]\n";
    file << std::format("width = {}\n", config.window.width);
    file << std::format("height = {}\n", config.window.height);
    file << std::format("fullscreen = {}\n", config.window.fullscreen ? "true" : "false");
    file << std::format("title = {}\n", quote(config.window.title));
    
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

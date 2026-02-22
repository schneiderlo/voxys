# Logging, Instrumentation & Configuration Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

This document specifies the logging, performance instrumentation, and configuration systems for the voxy project.

---

## 1. Logging System

### 1.1 Log Levels

| Level | Value | Usage |
|-------|-------|-------|
| `TRACE` | 0 | Detailed debugging (disabled in release) |
| `DEBUG` | 1 | Development debugging information |
| `INFO` | 2 | General operational information |
| `WARN` | 3 | Potential issues that don't prevent operation |
| `ERROR` | 4 | Errors that affect functionality |
| `FATAL` | 5 | Critical errors causing termination |

### 1.2 API

```cpp
namespace voxy::log {

enum class Level { Trace, Debug, Info, Warn, Error, Fatal };

void setLevel(Level minLevel);
Level getLevel();

void trace(const char* fmt, ...);
void debug(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);
void fatal(const char* fmt, ...);

// Scoped logging context
class Scope {
public:
    explicit Scope(const char* name);
    ~Scope();
};

} // namespace voxy::log

// Convenience macros
#define LOG_TRACE(...) voxy::log::trace(__VA_ARGS__)
#define LOG_DEBUG(...) voxy::log::debug(__VA_ARGS__)
#define LOG_INFO(...)  voxy::log::info(__VA_ARGS__)
#define LOG_WARN(...)  voxy::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) voxy::log::error(__VA_ARGS__)
#define LOG_FATAL(...) voxy::log::fatal(__VA_ARGS__)

#define LOG_SCOPE(name) voxy::log::Scope _log_scope_##__LINE__(name)
```

### 1.3 Output Format

```
[LEVEL] [TIMESTAMP] [SCOPE] Message
```

Examples:
```
[INFO ] [00:00:00.123] [Init] WebGPU adapter: NVIDIA GeForce RTX 3080
[DEBUG] [00:00:00.456] [Heightmap] Decompressing terrain.ldh (12.5 MB)
[WARN ] [00:00:01.234] [Render] Mip generation took 45ms (expected <20ms)
[ERROR] [00:00:02.000] [GPU] Pipeline creation failed: Invalid shader
```

### 1.4 Platform-Specific Output

**Native:**
```cpp
void platformLog(Level level, const char* message) {
    // Color-coded console output
    const char* color = "";
    switch (level) {
        case Level::Trace: color = "\033[90m"; break;  // Gray
        case Level::Debug: color = "\033[36m"; break;  // Cyan
        case Level::Info:  color = "\033[32m"; break;  // Green
        case Level::Warn:  color = "\033[33m"; break;  // Yellow
        case Level::Error: color = "\033[31m"; break;  // Red
        case Level::Fatal: color = "\033[35m"; break;  // Magenta
    }
    fprintf(stderr, "%s%s\033[0m\n", color, message);
    
    // Also write to log file if enabled
    if (logFile_) {
        fprintf(logFile_, "%s\n", message);
        fflush(logFile_);
    }
}
```

**Web (Emscripten):**
```cpp
void platformLog(Level level, const char* message) {
    switch (level) {
        case Level::Trace:
        case Level::Debug:
            emscripten_console_log(message);
            break;
        case Level::Info:
            emscripten_console_log(message);
            break;
        case Level::Warn:
            emscripten_console_warn(message);
            break;
        case Level::Error:
        case Level::Fatal:
            emscripten_console_error(message);
            break;
    }
}
```

### 1.5 Required Log Points

| Component | Event | Level | Information |
|-----------|-------|-------|-------------|
| Init | WebGPU adapter selection | INFO | Adapter name, backend |
| Init | Device creation | INFO | Limits, features |
| Init | Pipeline creation | DEBUG | Shader path, layout |
| Heightmap | Load start | INFO | File path, size |
| Heightmap | Decompress complete | INFO | Time, dimensions |
| Heightmap | Mip generation | DEBUG | Levels, time |
| Render | Frame start | TRACE | Frame number |
| Render | Pass timing | DEBUG | Pass name, GPU time |
| Render | Path switch | INFO | Old path, new path |
| Camera | Position update | TRACE | Position, direction |
| Error | Shader compile | ERROR | Error message, line |
| Error | Buffer creation | ERROR | Size, usage, error |

---

## 2. Performance Instrumentation

### 2.1 CPU Timing

```cpp
namespace voxy::perf {

class Timer {
public:
    void start();
    void stop();
    double elapsedMs() const;
    
private:
    std::chrono::high_resolution_clock::time_point start_;
    double elapsed_ = 0.0;
};

class ScopedTimer {
public:
    ScopedTimer(const char* name, double* outMs = nullptr);
    ~ScopedTimer();
    
private:
    const char* name_;
    double* outMs_;
    Timer timer_;
};

// Frame statistics
struct FrameStats {
    double totalMs;
    double updateMs;
    double renderMs;
    double presentMs;
    uint32_t frameNumber;
};

void beginFrame();
void endFrame();
const FrameStats& getLastFrameStats();
const FrameStats& getAverageStats(uint32_t frameCount = 60);

} // namespace voxy::perf

// Convenience macro
#define PERF_SCOPE(name) voxy::perf::ScopedTimer _perf_##__LINE__(name)
```

### 2.2 GPU Timing

WebGPU timestamp queries (where available):

```cpp
class GPUTimer {
public:
    GPUTimer(wgpu::Device device, uint32_t maxQueries = 64);
    
    // Insert timestamp in command encoder
    void writeTimestamp(wgpu::CommandEncoder encoder, const char* label);
    
    // Resolve and read results (async)
    void resolve(wgpu::CommandEncoder encoder);
    void readResults(std::function<void(const std::vector<GPUTimestamp>&)> callback);
    
    struct GPUTimestamp {
        const char* label;
        double timeMs;
    };
    
private:
    wgpu::QuerySet querySet_;
    wgpu::Buffer resolveBuffer_;
    wgpu::Buffer readbackBuffer_;
    std::vector<const char*> labels_;
    uint32_t queryIndex_ = 0;
};
```

### 2.3 Statistics Display

```cpp
struct RenderStats {
    // Frame timing
    double frameTimeMs;
    double fps;
    
    // CPU breakdown
    double cpuUpdateMs;
    double cpuRenderMs;
    
    // GPU breakdown (when available)
    double gpuRaycastMs;
    double gpuBlitMs;
    double gpuTotalMs;
    
    // Ray-caster stats
    uint32_t totalRays;
    uint32_t terrainHits;
    uint32_t skyHits;
    
    // Memory
    size_t heightmapMemory;
    size_t textureMemory;
    size_t bufferMemory;
};

void updateStats(RenderStats& stats);
void renderStatsOverlay(const RenderStats& stats);
```

### 2.4 Profiling Integration

For detailed profiling, integrate with platform tools:

**macOS (Instruments):**
```cpp
#if defined(__APPLE__) && defined(VOXY_ENABLE_PROFILING)
#include <os/signpost.h>

os_log_t profLog = os_log_create("com.voxy.profiling", "Render");

void beginProfileRegion(const char* name) {
    os_signpost_interval_begin(profLog, OS_SIGNPOST_ID_EXCLUSIVE, "Region", "%s", name);
}

void endProfileRegion() {
    os_signpost_interval_end(profLog, OS_SIGNPOST_ID_EXCLUSIVE, "Region");
}
#endif
```

**Chrome Tracing (Web):**
```cpp
#if defined(VOXY_WASM)
void emitTraceEvent(const char* name, const char* phase) {
    EM_ASM({
        if (typeof performance !== 'undefined' && performance.mark) {
            performance.mark(UTF8ToString($0) + '_' + UTF8ToString($1));
        }
    }, name, phase);
}
#endif
```

---

## 3. Configuration System

### 3.1 Configuration File Format

TOML-like format for human readability:

```toml
# voxy.cfg

[render]
path = "raycast"           # "raycast" or "triangle"
resolution_scale = 1.0     # Render resolution multiplier
vsync = true
max_fps = 0                # 0 = unlimited

[terrain]
heightmap = "assets/terrain.ldh"
height_scale = 500.0
cell_scale = 1.0

[camera]
fov = 60.0
near_plane = 0.1
far_plane = 10000.0
move_speed = 50.0
mouse_sensitivity = 0.002

[lighting]
sun_direction = [0.5, 0.8, 0.3]
sun_color = [1.0, 0.95, 0.9]
ambient_color = [0.1, 0.12, 0.15]
fog_density = 0.0001
fog_color = [0.6, 0.7, 0.8]

[debug]
show_stats = true
show_wireframe = false
log_level = "info"
enable_validation = true
```

### 3.2 Configuration API

```cpp
namespace voxy::config {

struct RenderConfig {
    std::string path = "raycast";
    float resolutionScale = 1.0f;
    bool vsync = true;
    int maxFps = 0;
};

struct TerrainConfig {
    std::string heightmap = "assets/terrain.ldh";
    float heightScale = 500.0f;
    float cellScale = 1.0f;
};

struct CameraConfig {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float moveSpeed = 50.0f;
    float mouseSensitivity = 0.002f;
};

struct LightingConfig {
    glm::vec3 sunDirection = {0.5f, 0.8f, 0.3f};
    glm::vec3 sunColor = {1.0f, 0.95f, 0.9f};
    glm::vec3 ambientColor = {0.1f, 0.12f, 0.15f};
    float fogDensity = 0.0001f;
    glm::vec3 fogColor = {0.6f, 0.7f, 0.8f};
};

struct DebugConfig {
    bool showStats = true;
    bool showWireframe = false;
    std::string logLevel = "info";
    bool enableValidation = true;
};

struct Config {
    RenderConfig render;
    TerrainConfig terrain;
    CameraConfig camera;
    LightingConfig lighting;
    DebugConfig debug;
};

// Load configuration from file
Config load(const std::string& path);

// Load with command-line overrides
Config load(const std::string& path, int argc, char** argv);

// Save configuration
void save(const Config& config, const std::string& path);

// Get global config
const Config& get();

} // namespace voxy::config
```

### 3.3 Command-Line Arguments

| Argument | Description | Example |
|----------|-------------|---------|
| `--config <path>` | Config file path | `--config myconfig.cfg` |
| `--render-path <path>` | Override render path | `--render-path triangle` |
| `--heightmap <path>` | Override heightmap | `--heightmap test.ldh` |
| `--width <n>` | Window width | `--width 1920` |
| `--height <n>` | Window height | `--height 1080` |
| `--fullscreen` | Start fullscreen | `--fullscreen` |
| `--log-level <level>` | Set log level | `--log-level debug` |
| `--no-validation` | Disable GPU validation | `--no-validation` |
| `--benchmark` | Run benchmark mode | `--benchmark` |
| `--help` | Show help | `--help` |

### 3.4 Argument Parsing

```cpp
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
};

CommandLineArgs parseArgs(int argc, char** argv) {
    CommandLineArgs args;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else if (arg == "--config" && i + 1 < argc) {
            args.configPath = argv[++i];
        } else if (arg == "--render-path" && i + 1 < argc) {
            args.renderPath = argv[++i];
        } else if (arg == "--heightmap" && i + 1 < argc) {
            args.heightmap = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            args.width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            args.height = std::stoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            args.fullscreen = true;
        } else if (arg == "--log-level" && i + 1 < argc) {
            args.logLevel = argv[++i];
        } else if (arg == "--no-validation") {
            args.noValidation = true;
        } else if (arg == "--benchmark") {
            args.benchmark = true;
        }
    }
    
    return args;
}
```

### 3.5 Web Configuration

For WASM builds, configuration comes from:

1. **URL Parameters:**
   ```
   https://example.com/voxy/?render=triangle&heightmap=test.ldh
   ```

2. **JavaScript Configuration:**
   ```javascript
   VoxyModule({
       canvas: document.getElementById('canvas'),
       config: {
           render: { path: 'raycast' },
           terrain: { heightmap: 'terrain.ldh' },
           debug: { showStats: true }
       }
   });
   ```

3. **LocalStorage:**
   ```javascript
   // Save user preferences
   localStorage.setItem('voxy_config', JSON.stringify(config));
   ```

---

## 4. Error Handling

### 4.1 Error Categories

```cpp
enum class ErrorCategory {
    None,
    IO,           // File operations
    GPU,          // WebGPU errors
    Shader,       // Shader compilation
    Config,       // Configuration parsing
    Memory,       // Allocation failures
    Validation,   // Data validation
};

struct Error {
    ErrorCategory category;
    std::string message;
    std::string details;
    std::string file;
    int line;
};
```

### 4.2 Result Type

```cpp
template<typename T>
class Result {
public:
    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(Error error) { return Result(std::move(error)); }
    
    bool isOk() const { return std::holds_alternative<T>(data_); }
    bool isErr() const { return !isOk(); }
    
    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    
    Error& error() { return std::get<Error>(data_); }
    const Error& error() const { return std::get<Error>(data_); }
    
    T valueOr(T defaultValue) const {
        return isOk() ? value() : defaultValue;
    }
    
private:
    std::variant<T, Error> data_;
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}
};

// Usage
Result<Heightmap> loadHeightmap(const std::string& path) {
    auto file = readFile(path);
    if (!file) {
        return Result<Heightmap>::err({
            ErrorCategory::IO,
            "Failed to read heightmap",
            path,
            __FILE__,
            __LINE__
        });
    }
    // ...
}
```

### 4.3 WebGPU Error Handling

```cpp
void setupErrorHandling(wgpu::Device device) {
    device.SetUncapturedErrorCallback(
        [](WGPUErrorType type, const char* message, void*) {
            const char* typeStr = "Unknown";
            switch (type) {
                case WGPUErrorType_Validation: typeStr = "Validation"; break;
                case WGPUErrorType_OutOfMemory: typeStr = "OutOfMemory"; break;
                case WGPUErrorType_Internal: typeStr = "Internal"; break;
                case WGPUErrorType_DeviceLost: typeStr = "DeviceLost"; break;
            }
            LOG_ERROR("WebGPU %s error: %s", typeStr, message);
        },
        nullptr
    );
    
    device.SetDeviceLostCallback(
        [](WGPUDeviceLostReason reason, const char* message, void*) {
            LOG_FATAL("WebGPU device lost: %s", message);
        },
        nullptr
    );
}
```

---

## 5. Debug Features

### 5.1 Debug Overlays

| Overlay | Key | Description |
|---------|-----|-------------|
| Stats | F1 | FPS, frame time, memory |
| Wireframe | F2 | Triangle path wireframe |
| Normals | F3 | Visualize surface normals |
| Depth | F4 | Visualize depth buffer |
| Mip Levels | F5 | Color by mip level used |
| Heat Map | F6 | Ray iteration count |

### 5.2 Debug Uniforms

```wgsl
struct DebugUniforms {
    mode: u32,          // Debug visualization mode
    param0: f32,        // Mode-specific parameter
    param1: f32,
    param2: f32,
};

// In fragment shader
fn applyDebugVisualization(color: vec3<f32>, depth: f32, 
                           normal: vec3<f32>, mipLevel: u32) -> vec3<f32> {
    switch (debug.mode) {
        case 1u: {  // Normals
            return normal * 0.5 + 0.5;
        }
        case 2u: {  // Depth
            let d = depth / debug.param0;  // Normalize by max depth
            return vec3(d);
        }
        case 3u: {  // Mip levels
            let colors = array<vec3<f32>, 8>(
                vec3(1.0, 0.0, 0.0),  // Mip 0: Red
                vec3(1.0, 0.5, 0.0),  // Mip 1: Orange
                vec3(1.0, 1.0, 0.0),  // Mip 2: Yellow
                vec3(0.0, 1.0, 0.0),  // Mip 3: Green
                vec3(0.0, 1.0, 1.0),  // Mip 4: Cyan
                vec3(0.0, 0.0, 1.0),  // Mip 5: Blue
                vec3(0.5, 0.0, 1.0),  // Mip 6: Purple
                vec3(1.0, 0.0, 1.0),  // Mip 7: Magenta
            );
            return colors[min(mipLevel, 7u)];
        }
        default: {
            return color;
        }
    }
}
```

### 5.3 Screenshot Capture

```cpp
void captureScreenshot(const std::string& filename) {
    // Read back from framebuffer
    wgpu::BufferDescriptor bufDesc = {};
    bufDesc.size = width_ * height_ * 4;
    bufDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer readbackBuffer = device_.CreateBuffer(&bufDesc);
    
    wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();
    
    wgpu::ImageCopyTexture src = {};
    src.texture = swapchainTexture_;
    
    wgpu::ImageCopyBuffer dst = {};
    dst.buffer = readbackBuffer;
    dst.layout.bytesPerRow = width_ * 4;
    dst.layout.rowsPerImage = height_;
    
    encoder.CopyTextureToBuffer(&src, &dst, {width_, height_, 1});
    
    wgpu::CommandBuffer commands = encoder.Finish();
    queue_.Submit(1, &commands);
    
    // Map and save
    readbackBuffer.MapAsync(wgpu::MapMode::Read, 0, bufDesc.size,
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            if (status == WGPUBufferMapAsyncStatus_Success) {
                // Write PNG with stb_image_write
            }
        }, nullptr);
}
```

---

## 6. Benchmark Mode

### 6.1 Benchmark Scenarios

```cpp
struct BenchmarkScenario {
    std::string name;
    glm::vec3 cameraPos;
    glm::vec3 cameraTarget;
    uint32_t frameCount;
};

std::vector<BenchmarkScenario> defaultScenarios = {
    {"Overhead", {0, 1000, 0}, {0, 0, 0}, 300},
    {"Ground Level", {100, 10, 100}, {200, 10, 200}, 300},
    {"Horizon", {0, 500, -2000}, {0, 0, 2000}, 300},
    {"Close Detail", {50, 20, 50}, {60, 15, 60}, 300},
};
```

### 6.2 Benchmark Output

```
=== voxy Benchmark Results ===

System: NVIDIA GeForce RTX 3080
Resolution: 1920x1080
Render Path: raycast
Heightmap: 8192x8192

Scenario: Overhead
  Frames: 300
  Total Time: 4523 ms
  Avg Frame: 15.08 ms (66.3 FPS)
  Min Frame: 14.2 ms
  Max Frame: 18.1 ms
  GPU Time: 12.3 ms avg

Scenario: Ground Level
  Frames: 300
  Total Time: 5102 ms
  Avg Frame: 17.01 ms (58.8 FPS)
  Min Frame: 15.8 ms
  Max Frame: 21.4 ms
  GPU Time: 14.8 ms avg

...

Overall Average: 62.1 FPS
```


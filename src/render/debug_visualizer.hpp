// ═══════════════════════════════════════════════════════════════════════════════
// debug_visualizer.hpp - Debug Visualization Renderer (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Provides debug visualization for ray-caster output verification.
// Features:
//   - Grayscale depth visualization
//   - Configurable depth range mapping
//   - Multiple visualization modes
//   - Sky pixel highlighting
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <filesystem>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualization Mode
// ─────────────────────────────────────────────────────────────────────────────

enum class DebugVisMode : uint32_t {
    GrayscaleDepth = 0,   ///< Grayscale: near=white, far=dark
    ColorGradient = 1,    ///< Color gradient: white->yellow->red->dark
    RawDepth = 2,         ///< Raw depth scaled arbitrarily
};

// ─────────────────────────────────────────────────────────────────────────────
// Debug Parameters (matches shader struct)
// ─────────────────────────────────────────────────────────────────────────────

struct DebugParams {
    float nearDist = 1.0f;       ///< Near distance (maps to white/start)
    float farDist = 5000.0f;     ///< Far distance (maps to black/end)
    uint32_t mode = 0;           ///< Visualization mode (see DebugVisMode)
    uint32_t padding = 0;        ///< Alignment padding
};

static_assert(sizeof(DebugParams) == 16, "DebugParams must be 16 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualizer Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct DebugVisualizerConfig {
    std::filesystem::path shaderPath;    ///< Path to debug_depth.wgsl shader
    WGPUTextureFormat colorFormat;       ///< Output color format
    float nearDist;                      ///< Near depth for visualization
    float farDist;                       ///< Far depth for visualization
    DebugVisMode mode;                   ///< Initial visualization mode

    /// Default configuration
    static DebugVisualizerConfig defaults() {
        return DebugVisualizerConfig{
            .shaderPath = "shaders/debug_depth.wgsl",
            .colorFormat = WGPUTextureFormat_BGRA8Unorm,
            .nearDist = 1.0f,
            .farDist = 5000.0f,
            .mode = DebugVisMode::GrayscaleDepth
        };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Debug Visualizer
// ─────────────────────────────────────────────────────────────────────────────

/// Debug visualization renderer for ray-caster depth output.
/// Renders depth buffer as grayscale or color-coded image for verification.
class DebugVisualizer {
public:
    DebugVisualizer() = default;
    ~DebugVisualizer();

    // Non-copyable
    DebugVisualizer(const DebugVisualizer&) = delete;
    DebugVisualizer& operator=(const DebugVisualizer&) = delete;

    // Movable
    DebugVisualizer(DebugVisualizer&& other) noexcept;
    DebugVisualizer& operator=(DebugVisualizer&& other) noexcept;

    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────

    /// Initialize the debug visualizer
    /// @param device WebGPU device
    /// @param queue WebGPU queue
    /// @param config Visualizer configuration
    /// @return true on success
    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const DebugVisualizerConfig& config = DebugVisualizerConfig::defaults());

    /// Check if initialized
    [[nodiscard]] bool isInitialized() const noexcept { return pipeline_ != nullptr; }

    /// Release all GPU resources
    void shutdown();

    // ─────────────────────────────────────────────────────────────────────────
    // Depth Texture Binding
    // ─────────────────────────────────────────────────────────────────────────

    /// Set the depth texture to visualize
    /// @param depthView Texture view of R32Float depth from ray-caster
    void setDepthTexture(WGPUTextureView depthView);

    // ─────────────────────────────────────────────────────────────────────────
    // Visualization Settings
    // ─────────────────────────────────────────────────────────────────────────

    /// Set depth range for visualization
    /// @param near Near distance (maps to white/start of gradient)
    /// @param far Far distance (maps to black/end of gradient)
    void setDepthRange(float near, float far);

    /// Set visualization mode
    void setMode(DebugVisMode mode);

    /// Get current visualization mode
    [[nodiscard]] DebugVisMode getMode() const noexcept;

    /// Get current near distance
    [[nodiscard]] float getNearDist() const noexcept { return params_.nearDist; }

    /// Get current far distance
    [[nodiscard]] float getFarDist() const noexcept { return params_.farDist; }

    // ─────────────────────────────────────────────────────────────────────────
    // Rendering
    // ─────────────────────────────────────────────────────────────────────────

    /// Render the depth visualization to the given color target
    /// @param encoder Command encoder
    /// @param colorView Output color texture view
    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView);

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal Methods
    // ─────────────────────────────────────────────────────────────────────────

    bool createUniformBuffer();
    bool createBindGroupLayout();
    bool createPipeline(const DebugVisualizerConfig& config);
    bool createBindGroup();
    void updateUniformBuffer();

    // ─────────────────────────────────────────────────────────────────────────
    // GPU Resources
    // ─────────────────────────────────────────────────────────────────────────

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Pipeline resources
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;

    // Bind group resources
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // Buffers
    WGPUBuffer uniformBuffer_ = nullptr;

    // Depth texture binding
    WGPUTextureView depthView_ = nullptr;

    // State
    DebugParams params_;
    DebugVisualizerConfig config_ = DebugVisualizerConfig::defaults();
    bool uniformsDirty_ = true;
    bool bindGroupDirty_ = true;
};

} // namespace voxy::render




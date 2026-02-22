// ═══════════════════════════════════════════════════════════════════════════════
// context.hpp - WebGPU Context Management (C++20)
// ═══════════════════════════════════════════════════════════════════════════════
// Handles WebGPU initialization including adapter selection, device creation,
// surface/swapchain setup, and error callback handling. Supports both native
// (wgpu-native) and WASM (browser WebGPU) builds.
// Updated for C++20 with concepts, string_view, and modern attributes.
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <concepts>

// WebGPU header - same API for native (wgpu-native) and WASM
#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

// Forward declaration
namespace voxy {
    class Window;
}

namespace voxy::gpu {

// ─────────────────────────────────────────────────────────────────────────────
// C++20 Concepts for GPU Types
// ─────────────────────────────────────────────────────────────────────────────

// Concept for error callback functions
template<typename F>
concept ErrorCallbackFn = std::invocable<F, WGPUErrorType, const char*>;

// Concept for device lost callback functions
template<typename F>
concept DeviceLostCallbackFn = std::invocable<F, WGPUDeviceLostReason, const char*>;

// ─────────────────────────────────────────────────────────────────────────────
// GPU Context Configuration
// ─────────────────────────────────────────────────────────────────────────────

struct ContextConfig {
    // Adapter selection preferences
    WGPUPowerPreference powerPreference = WGPUPowerPreference_HighPerformance;
    bool forceDiscreteGPU = false;
    
    // Device features and limits
    bool enableValidation = true;   // Enable validation layers (debug)
    bool enableTimestamps = false;  // Enable GPU timestamp queries
    
    // Swapchain configuration
    WGPUTextureFormat preferredFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // VSync
    uint32_t swapchainWidth = 0;   // 0 = use window framebuffer size
    uint32_t swapchainHeight = 0;
    
    // C++20: Default comparison
    [[nodiscard]] constexpr auto operator<=>(const ContextConfig&) const = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Info
// ─────────────────────────────────────────────────────────────────────────────

struct AdapterInfo {
    std::string vendor;
    std::string architecture;
    std::string device;
    std::string description;
    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
    WGPUBackendType backendType = WGPUBackendType_Undefined;
    
    // C++20: Default comparison
    [[nodiscard]] constexpr auto operator<=>(const AdapterInfo&) const = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU Context Class
// ─────────────────────────────────────────────────────────────────────────────

class Context {
public:
    // Error callback type
    using ErrorCallback = std::function<void(WGPUErrorType type, const char* message)>;
    using DeviceLostCallback = std::function<void(WGPUDeviceLostReason reason, const char* message)>;
    
    Context() = default;
    ~Context();
    
    // Non-copyable
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    
    // Movable
    Context(Context&& other) noexcept;
    Context& operator=(Context&& other) noexcept;
    
    // ─────────────────────────────────────────────────────────────────────────
    // Initialization
    // ─────────────────────────────────────────────────────────────────────────
    
    // Initialize WebGPU context with a window for rendering (native builds)
    [[nodiscard]] bool init(Window& window, const ContextConfig& config = {});
    
    // Initialize WebGPU context without a window (compute-only)
    [[nodiscard]] bool initHeadless(const ContextConfig& config = {});
    
#if defined(VOXY_WASM)
    // Initialize WebGPU context from canvas element (WASM builds)
    // Uses the canvas selector specified (default: "#voxy-canvas")
    [[nodiscard]] bool initFromCanvas(const char* canvasSelector = "#voxy-canvas",
                                       const ContextConfig& config = {});
#endif
    
    // Shutdown and release all resources
    void shutdown();
    
    // ─────────────────────────────────────────────────────────────────────────
    // State Queries
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] constexpr bool isInitialized() const noexcept { return device_ != nullptr; }
    [[nodiscard]] constexpr bool hasSurface() const noexcept { return surface_ != nullptr; }
    
    // Get adapter information
    [[nodiscard]] const AdapterInfo& getAdapterInfo() const noexcept { return adapterInfo_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // WebGPU Object Access
    // ─────────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] constexpr WGPUInstance getInstance() const noexcept { return instance_; }
    [[nodiscard]] constexpr WGPUAdapter getAdapter() const noexcept { return adapter_; }
    [[nodiscard]] constexpr WGPUDevice getDevice() const noexcept { return device_; }
    [[nodiscard]] constexpr WGPUQueue getQueue() const noexcept { return queue_; }
    [[nodiscard]] constexpr WGPUSurface getSurface() const noexcept { return surface_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Swapchain Management
    // ─────────────────────────────────────────────────────────────────────────
    
    // Resize swapchain (call on window resize)
    void resizeSwapchain(uint32_t width, uint32_t height);
    
    // Update presentation mode (e.g., VSync On/Off)
    void setPresentMode(WGPUPresentMode mode);

    // Get current swapchain texture for rendering
    [[nodiscard]] WGPUTextureView getCurrentTextureView();
    
    // Get current swapchain texture handle (valid only after getCurrentTextureView)
    [[nodiscard]] WGPUTexture getCurrentTexture() const { return currentTexture_; }

    // Present the current frame
    void present();
    
    // Get swapchain format
    [[nodiscard]] constexpr WGPUTextureFormat getSwapchainFormat() const noexcept { return swapchainFormat_; }
    
    // Get swapchain dimensions
    [[nodiscard]] constexpr uint32_t getSwapchainWidth() const noexcept { return swapchainWidth_; }
    [[nodiscard]] constexpr uint32_t getSwapchainHeight() const noexcept { return swapchainHeight_; }
    
    // ─────────────────────────────────────────────────────────────────────────
    // Error Handling (C++20: Concept-constrained setters)
    // ─────────────────────────────────────────────────────────────────────────
    
    void setErrorCallback(ErrorCallback callback) { errorCallback_ = std::move(callback); }
    void setDeviceLostCallback(DeviceLostCallback callback) { deviceLostCallback_ = std::move(callback); }
    
    // Process pending async operations (call in main loop)
    void tick();
    
private:
    // WebGPU objects
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    
    // Swapchain state
    WGPUTextureFormat swapchainFormat_ = WGPUTextureFormat_Undefined;
    uint32_t swapchainWidth_ = 0;
    uint32_t swapchainHeight_ = 0;
    WGPUTexture currentTexture_ = nullptr;       // Surface texture for current frame
    WGPUTextureView currentTextureView_ = nullptr;
    WGPUSurfaceConfiguration lastSurfaceConfig_ = {};
    
    // Adapter information
    AdapterInfo adapterInfo_;
    
    // Callbacks
    ErrorCallback errorCallback_;
    DeviceLostCallback deviceLostCallback_;
    
    // Internal initialization helpers
    bool createInstance();
    bool requestAdapter(const ContextConfig& config);
    bool requestDevice(const ContextConfig& config);
    bool createSurface(Window& window);
    bool configureSurface(const ContextConfig& config);
    void queryAdapterInfo();
    
#if defined(VOXY_WASM)
    bool createSurfaceFromCanvas(const char* selector);
#endif
    
    // Internal callback handlers
    static void onUncapturedError(WGPUErrorType type, const char* message, void* userdata);
    static void onDeviceLost(WGPUDeviceLostReason reason, const char* message, void* userdata);
};

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

// Convert WebGPU types to strings for logging
[[nodiscard]] const char* errorTypeToString(WGPUErrorType type) noexcept;
[[nodiscard]] const char* adapterTypeToString(WGPUAdapterType type) noexcept;
[[nodiscard]] const char* backendTypeToString(WGPUBackendType type) noexcept;
[[nodiscard]] const char* textureFormatToString(WGPUTextureFormat format) noexcept;

} // namespace voxy::gpu

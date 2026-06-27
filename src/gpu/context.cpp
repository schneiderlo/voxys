// ═══════════════════════════════════════════════════════════════════════════════
// context.cpp - WebGPU Context Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/context.hpp"
#if defined(VOXY_NATIVE)
    #include "engine/platform/window.hpp"
#endif
#include "core/log.hpp"

#include <cstring>
#include <vector>

// Platform-specific sleep includes for Dawn async polling
#if defined(VOXY_USE_DAWN)
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <unistd.h>
    #endif
#endif

#if defined(VOXY_WASM)
extern "C" {
    WGPUDevice emscripten_webgpu_get_device(void);
}
#endif

namespace voxy::gpu {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Context::~Context() {
    shutdown();
}

Context::Context(Context&& other) noexcept
    : instance_(other.instance_)
    , adapter_(other.adapter_)
    , device_(other.device_)
    , queue_(other.queue_)
    , surface_(other.surface_)
    , swapchainFormat_(other.swapchainFormat_)
    , swapchainWidth_(other.swapchainWidth_)
    , swapchainHeight_(other.swapchainHeight_)
    , currentTexture_(other.currentTexture_)
    , currentTextureView_(other.currentTextureView_)
    , adapterInfo_(std::move(other.adapterInfo_))
    , errorCallback_(std::move(other.errorCallback_))
    , deviceLostCallback_(std::move(other.deviceLostCallback_))
{
    other.instance_ = nullptr;
    other.adapter_ = nullptr;
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.surface_ = nullptr;
    other.currentTexture_ = nullptr;
    other.currentTextureView_ = nullptr;
}

Context& Context::operator=(Context&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        instance_ = other.instance_;
        adapter_ = other.adapter_;
        device_ = other.device_;
        queue_ = other.queue_;
        surface_ = other.surface_;
        swapchainFormat_ = other.swapchainFormat_;
        swapchainWidth_ = other.swapchainWidth_;
        swapchainHeight_ = other.swapchainHeight_;
        currentTexture_ = other.currentTexture_;
        currentTextureView_ = other.currentTextureView_;
        adapterInfo_ = std::move(other.adapterInfo_);
        errorCallback_ = std::move(other.errorCallback_);
        deviceLostCallback_ = std::move(other.deviceLostCallback_);
        
        other.instance_ = nullptr;
        other.adapter_ = nullptr;
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.surface_ = nullptr;
        other.currentTexture_ = nullptr;
        other.currentTextureView_ = nullptr;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool Context::init(Window& window, const ContextConfig& config) {
    LOG_SCOPE("GPU");
    
    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }
    
    // wgpu-native uses standard WebGPU C API - no proc table setup needed
    
    if (!createInstance()) {
        return false;
    }
    
    if (!createSurface(window)) {
        return false;
    }
    
    if (!requestAdapter(config)) {
        return false;
    }
    
    queryAdapterInfo();
    
    if (!requestDevice(config)) {
        return false;
    }
    
    // Get the queue
    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        return false;
    }
    
    if (!configureSurface(config)) {
        return false;
    }
    
    LOG_INFO("GPU context initialized successfully");
    return true;
}

bool Context::initHeadless(const ContextConfig& config) {
    LOG_SCOPE("GPU");
    
    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }
    
    if (!createInstance()) {
        return false;
    }
    
    if (!requestAdapter(config)) {
        return false;
    }
    
    queryAdapterInfo();
    
    if (!requestDevice(config)) {
        return false;
    }
    
    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        return false;
    }
    
    LOG_INFO("Headless GPU context initialized successfully");
    return true;
}

#if defined(VOXY_WASM)
bool Context::initFromCanvas(const char* canvasSelector, const ContextConfig& config) {
    LOG_SCOPE("GPU");
    
    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }
    
    if (!createInstance()) {
        return false;
    }
    
    if (!createSurfaceFromCanvas(canvasSelector)) {
        return false;
    }

#if defined(VOXY_WASM)
    device_ = emscripten_webgpu_get_device();
    if (!device_) {
        LOG_ERROR("emscripten_webgpu_get_device returned null");
        return false;
    }
    
    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        return false;
    }
    
    if (!configureSurface(config)) {
        return false;
    }
    
    LOG_INFO("WASM GPU context initialized from canvas: {}", canvasSelector);
    return true;
#else
    if (!requestAdapter(config)) {
        return false;
    }
    
    queryAdapterInfo();
    
    if (!requestDevice(config)) {
        return false;
    }
    
    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        return false;
    }
    
    if (!configureSurface(config)) {
        return false;
    }
    
    LOG_INFO("WASM GPU context initialized from canvas: {}", canvasSelector);
    return true;
#endif
}

bool Context::createSurfaceFromCanvas(const char* selector) {
#if defined(VOXY_WASM)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasDesc.selector.data = selector;
    canvasDesc.selector.length = std::strlen(selector);
    
    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
#else
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = selector;
    
    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = &canvasDesc.chain;
#endif
    
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (!surface_) {
        LOG_ERROR("Failed to create WebGPU surface from canvas: {}", selector);
        return false;
    }
    
    LOG_DEBUG("WebGPU surface created from canvas: {}", selector);
    return true;
}
#endif

void Context::shutdown() {
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }
    
    if (surface_) {
        wgpuSurfaceUnconfigure(surface_);
        wgpuSurfaceRelease(surface_);
        surface_ = nullptr;
    }
    
    if (queue_) {
        wgpuQueueRelease(queue_);
        queue_ = nullptr;
    }
    
    if (device_) {
        wgpuDeviceRelease(device_);
        device_ = nullptr;
    }
    
    if (adapter_) {
        wgpuAdapterRelease(adapter_);
        adapter_ = nullptr;
    }
    
    if (instance_) {
        wgpuInstanceRelease(instance_);
        instance_ = nullptr;
    }
    
    swapchainFormat_ = WGPUTextureFormat_Undefined;
    swapchainWidth_ = 0;
    swapchainHeight_ = 0;
    adapterInfo_ = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Instance Creation
// ─────────────────────────────────────────────────────────────────────────────

bool Context::createInstance() {
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;
    
    instance_ = wgpuCreateInstance(&instanceDesc);
    if (!instance_) {
        LOG_ERROR("Failed to create WebGPU instance");
        return false;
    }
    
    LOG_DEBUG("WebGPU instance created");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface Creation
// ─────────────────────────────────────────────────────────────────────────────

bool Context::createSurface(Window& window) {
#if defined(VOXY_NATIVE)
    WGPUSurfaceDescriptor surfaceDesc = {};
    
    #if defined(__APPLE__)
    // macOS: Use Metal layer
    WGPUSurfaceDescriptorFromMetalLayer metalDesc = {};
    metalDesc.chain.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;
    metalDesc.layer = window.getCocoaWindow();  // Need to get CAMetalLayer
    surfaceDesc.nextInChain = &metalDesc.chain;
    
    #elif defined(_WIN32)
    // Windows: Use HWND
    WGPUSurfaceDescriptorFromWindowsHWND windowsDesc = {};
    windowsDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    windowsDesc.hinstance = window.getWin32Instance();
    windowsDesc.hwnd = window.getWin32Window();
    surfaceDesc.nextInChain = &windowsDesc.chain;
    
    #elif defined(__linux__)
    // Linux: Use X11
    WGPUSurfaceDescriptorFromXlibWindow x11Desc = {};
    x11Desc.chain.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;
    x11Desc.display = window.getX11Display();
    x11Desc.window = window.getX11Window();
    surfaceDesc.nextInChain = &x11Desc.chain;
    #endif
    
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (!surface_) {
        LOG_ERROR("Failed to create WebGPU surface");
        return false;
    }
    
    LOG_DEBUG("WebGPU surface created");
    return true;
    
#elif defined(VOXY_WASM)
    // WASM: Use canvas element
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasDesc.selector.data = "#voxy-canvas";
    canvasDesc.selector.length = std::strlen(canvasDesc.selector.data);
    
    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);
    
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (!surface_) {
        LOG_ERROR("Failed to create WebGPU surface from canvas");
        return false;
    }
    
    return true;
#else
    LOG_ERROR("Surface creation not supported on this platform");
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Request
// ─────────────────────────────────────────────────────────────────────────────

bool Context::requestAdapter(const ContextConfig& config) {
#if defined(VOXY_WASM)
    (void)config;
    LOG_ERROR("requestAdapter should not be called on WASM build");
    return false;
#else
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.powerPreference = config.powerPreference;
    adapterOpts.compatibleSurface = surface_;
    adapterOpts.forceFallbackAdapter = false;
    
    // Synchronous adapter request using blocking pattern
    struct AdapterUserData {
        WGPUAdapter adapter = nullptr;
        bool done = false;
    } userData;
    
    auto callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                       const char* message, void* userdata) {
        auto* data = static_cast<AdapterUserData*>(userdata);
        if (status == WGPURequestAdapterStatus_Success) {
            data->adapter = adapter;
        } else {
            LOG_ERROR("Failed to request adapter: {}", message ? message : "unknown error");
        }
        data->done = true;
    };
    
    wgpuInstanceRequestAdapter(instance_, &adapterOpts, callback, &userData);
    
    // wgpu-native calls the callback synchronously.
    // Dawn (and browser WebGPU) requires wgpuInstanceProcessEvents() to be called
    // in a loop until the callback fires. For Dawn compatibility, we poll events.
    // Note: If using Dawn, this requires linking against Dawn's implementation.
#if defined(VOXY_USE_DAWN)
    // Dawn is asynchronous - poll events until callback is done
    constexpr int maxPollAttempts = 1000;
    int pollAttempt = 0;
    while (!userData.done && pollAttempt < maxPollAttempts) {
        wgpuInstanceProcessEvents(instance_);
        pollAttempt++;
        // Small sleep to avoid busy-waiting (platform-specific)
        #if defined(_WIN32)
            Sleep(1);
        #else
            usleep(1000);
        #endif
    }
    
    if (pollAttempt >= maxPollAttempts) {
        LOG_ERROR("Adapter request timed out after {} poll attempts", maxPollAttempts);
        return false;
    }
#endif
    
    if (!userData.done) {
        LOG_ERROR("Adapter request callback was not called (this may indicate Dawn backend is in use without VOXY_USE_DAWN defined)");
        return false;
    }
    
    if (!userData.adapter) {
        LOG_ERROR("No suitable GPU adapter found");
        return false;
    }
    
    adapter_ = userData.adapter;
    LOG_DEBUG("GPU adapter acquired");
    return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Request
// ─────────────────────────────────────────────────────────────────────────────

bool Context::requestDevice(const ContextConfig& config) {
#if defined(VOXY_WASM)
    (void)config;
    LOG_ERROR("requestDevice should not be called on WASM build");
    return false;
#else
    // Set up required features
    std::vector<WGPUFeatureName> requiredFeatures;
    
    if (config.enableTimestamps) {
        requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
    }
    
    // Device descriptor
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredFeatureCount = requiredFeatures.size();
    deviceDesc.requiredFeatures = requiredFeatures.data();
    
    // Use adapter's default limits - don't request specific limits
    // This ensures compatibility with various adapters including software renderers
    // Specific limits can be requested later when features require them
    deviceDesc.requiredLimits = nullptr;
    
    // Set up device lost callback
    deviceDesc.deviceLostCallback = onDeviceLost;
    deviceDesc.deviceLostUserdata = this;
    
    // Synchronous device request
    struct DeviceUserData {
        WGPUDevice device = nullptr;
        bool done = false;
    } userData;
    
    auto callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                       const char* message, void* userdata) {
        auto* data = static_cast<DeviceUserData*>(userdata);
        if (status == WGPURequestDeviceStatus_Success) {
            data->device = device;
        } else {
            LOG_ERROR("Failed to request device: {}", message ? message : "unknown error");
        }
        data->done = true;
    };
    
    wgpuAdapterRequestDevice(adapter_, &deviceDesc, callback, &userData);
    
    // wgpu-native calls the callback synchronously.
    // Dawn (and browser WebGPU) requires wgpuInstanceProcessEvents() to be called.
#if defined(VOXY_USE_DAWN)
    // Dawn is asynchronous - poll events until callback is done
    constexpr int maxPollAttempts = 1000;
    int pollAttempt = 0;
    while (!userData.done && pollAttempt < maxPollAttempts) {
        wgpuInstanceProcessEvents(instance_);
        pollAttempt++;
        #if defined(_WIN32)
            Sleep(1);
        #else
            usleep(1000);
        #endif
    }
    
    if (pollAttempt >= maxPollAttempts) {
        LOG_ERROR("Device request timed out after {} poll attempts", maxPollAttempts);
        return false;
    }
#endif
    
    if (!userData.done) {
        LOG_ERROR("Device request callback was not called (this may indicate Dawn backend is in use without VOXY_USE_DAWN defined)");
        return false;
    }
    
    if (!userData.device) {
        LOG_ERROR("Failed to create GPU device");
        return false;
    }
    
    device_ = userData.device;
    
    // Note: wgpu-native uses push/pop error scope instead of uncaptured error callback
    // For now, we rely on the device lost callback for critical errors
    
    LOG_DEBUG("GPU device created");
    return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Surface Configuration
// ─────────────────────────────────────────────────────────────────────────────

bool Context::configureSurface(const ContextConfig& config) {
    if (!surface_) {
        return true;  // Headless mode
    }
    
#if defined(VOXY_WASM)
    swapchainFormat_ = (config.preferredFormat != WGPUTextureFormat_Undefined)
        ? config.preferredFormat
        : WGPUTextureFormat_BGRA8Unorm;
    
    swapchainWidth_ = config.swapchainWidth > 0 ? config.swapchainWidth : 1280;
    swapchainHeight_ = config.swapchainHeight > 0 ? config.swapchainHeight : 720;
    
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device_;
    surfaceConfig.format = swapchainFormat_;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    
    // Cache configuration
    lastSurfaceConfig_ = surfaceConfig;

    wgpuSurfaceConfigure(surface_, &surfaceConfig);
    
    LOG_DEBUG("WASM surface configured: {}x{}, format={}",
              swapchainWidth_, swapchainHeight_,
              textureFormatToString(swapchainFormat_));
    
    return true;
#else
    // Get surface capabilities
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    
    // Choose format
    swapchainFormat_ = config.preferredFormat;
    bool formatSupported = false;
    for (size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == config.preferredFormat) {
            formatSupported = true;
            break;
        }
    }
    
    if (!formatSupported && caps.formatCount > 0) {
        swapchainFormat_ = caps.formats[0];
        LOG_WARN("Preferred format not supported, using: {}", 
                 textureFormatToString(swapchainFormat_));
    }
    
    // Use provided dimensions or default to reasonable size
    swapchainWidth_ = config.swapchainWidth > 0 ? config.swapchainWidth : 1280;
    swapchainHeight_ = config.swapchainHeight > 0 ? config.swapchainHeight : 720;
    
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device_;
    surfaceConfig.format = swapchainFormat_;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;
    surfaceConfig.presentMode = config.presentMode;
    
    // Cache configuration
    lastSurfaceConfig_ = surfaceConfig;

    wgpuSurfaceConfigure(surface_, &surfaceConfig);
    
    LOG_DEBUG("Surface configured: {}x{}, format={}", 
              swapchainWidth_, swapchainHeight_,
              textureFormatToString(swapchainFormat_));
    
    // Free capabilities
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    
    return true;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Adapter Info Query
// ─────────────────────────────────────────────────────────────────────────────

void Context::queryAdapterInfo() {
#if defined(VOXY_WASM)
    adapterInfo_.vendor = "WebGPU";
    adapterInfo_.architecture = "Browser";
    adapterInfo_.device = "WebGPU (Emscripten)";
    adapterInfo_.description = "Browser WebGPU Device";
    adapterInfo_.adapterType = WGPUAdapterType_Unknown;
    adapterInfo_.backendType = WGPUBackendType_WebGPU;
#else
    WGPUAdapterInfo info = {};
    wgpuAdapterGetInfo(adapter_, &info);
    
    adapterInfo_.vendor = info.vendor ? info.vendor : "";
    adapterInfo_.architecture = info.architecture ? info.architecture : "";
    adapterInfo_.device = info.device ? info.device : "";
    adapterInfo_.description = info.description ? info.description : "";
    adapterInfo_.adapterType = info.adapterType;
    adapterInfo_.backendType = info.backendType;
    
    LOG_INFO("GPU Adapter: {}", adapterInfo_.device);
    LOG_INFO("  Vendor: {}", adapterInfo_.vendor);
    LOG_INFO("  Backend: {}", backendTypeToString(adapterInfo_.backendType));
    LOG_INFO("  Type: {}", adapterTypeToString(adapterInfo_.adapterType));
    
    wgpuAdapterInfoFreeMembers(info);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain Management
// ─────────────────────────────────────────────────────────────────────────────

void Context::resizeSwapchain(uint32_t width, uint32_t height) {
    if (!surface_ || width == 0 || height == 0) {
        return;
    }
    
    if (width == swapchainWidth_ && height == swapchainHeight_) {
        return;
    }
    
    // Release current texture view if any
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    
    swapchainWidth_ = width;
    swapchainHeight_ = height;
    
    // Reuse last config but update size
    WGPUSurfaceConfiguration surfaceConfig = lastSurfaceConfig_;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;

    // Ensure we have the device set (should be from cache, but safe to set)
    surfaceConfig.device = device_;

    lastSurfaceConfig_ = surfaceConfig;
    
    wgpuSurfaceConfigure(surface_, &surfaceConfig);
    
    LOG_DEBUG("Swapchain resized: {}x{}", swapchainWidth_, swapchainHeight_);
}

void Context::setPresentMode(WGPUPresentMode mode) {
#if defined(VOXY_WASM)
    // Browser WebGPU only supports Fifo present mode.
    // Uncapped FPS on WASM is achieved via Emscripten's main loop timing
    // (EM_TIMING_SETIMMEDIATE), not by changing the surface present mode.
    // Attempting to configure with Immediate mode would cause an assertion failure.
    (void)mode;
    LOG_DEBUG("setPresentMode ignored on WASM (browser only supports Fifo)");
    return;
#else
    if (!surface_ || lastSurfaceConfig_.width == 0) {
        return;
    }

    if (lastSurfaceConfig_.presentMode == mode) {
        return;
    }

    // Release current resources if any, though usually handled in next frame loop
    // Reconfiguring surface invalidates current texture
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }

    lastSurfaceConfig_.presentMode = mode;
    wgpuSurfaceConfigure(surface_, &lastSurfaceConfig_);

    LOG_INFO("Presentation mode changed to: {}",
             (mode == WGPUPresentMode_Fifo) ? "Fifo (VSync)" :
             (mode == WGPUPresentMode_Immediate) ? "Immediate (Uncapped)" :
             (mode == WGPUPresentMode_Mailbox) ? "Mailbox" : "Unknown");
#endif
}

WGPUTextureView Context::getCurrentTextureView() {
    if (!surface_) {
        return nullptr;
    }
    
    // Release previous frame's texture and view
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }
    
    WGPUSurfaceTexture surfaceTexture = {};
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
    
#if defined(VOXY_WASM)
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        LOG_ERROR("Failed to get current surface texture: status={}", static_cast<int>(surfaceTexture.status));
        return nullptr;
    }
#else
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        LOG_ERROR("Failed to get current surface texture: status={}", static_cast<int>(surfaceTexture.status));
        return nullptr;
    }
#endif
    
    if (!surfaceTexture.texture) {
        return nullptr;
    }
    
    // Store the texture - we'll release it after present
    currentTexture_ = surfaceTexture.texture;
    
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = swapchainFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    
    currentTextureView_ = wgpuTextureCreateView(currentTexture_, &viewDesc);
    
    return currentTextureView_;
}

void Context::present() {
#if defined(VOXY_WASM)
    // Browser presentation happens implicitly on RAF; nothing to do.
    (void)surface_;
#else
    if (surface_ && currentTexture_) {
        wgpuSurfacePresent(surface_);
    }
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }
#endif
}

void Context::tick() {
    // In wgpu-native, work is processed automatically
    // This function is kept for API compatibility
    (void)device_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Error Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void Context::onUncapturedError(WGPUErrorType type, const char* message, void* userdata) {
    auto* ctx = static_cast<Context*>(userdata);
    
    LOG_ERROR("WebGPU uncaptured error [{}]: {}", 
              errorTypeToString(type), message ? message : "unknown");
    
    if (ctx && ctx->errorCallback_) {
        ctx->errorCallback_(type, message);
    }
}

void Context::onDeviceLost(WGPUDeviceLostReason reason, const char* message, void* userdata) {
    auto* ctx = static_cast<Context*>(userdata);
    
    const char* reasonStr = "unknown";
    switch (reason) {
        case WGPUDeviceLostReason_Unknown: reasonStr = "unknown"; break;
        case WGPUDeviceLostReason_Destroyed: reasonStr = "destroyed"; break;
        default: break;
    }
    
    LOG_ERROR("WebGPU device lost [{}]: {}", reasonStr, message ? message : "unknown");
    
    if (ctx && ctx->deviceLostCallback_) {
        ctx->deviceLostCallback_(reason, message);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

const char* errorTypeToString(WGPUErrorType type) noexcept {
    switch (type) {
        case WGPUErrorType_NoError: return "NoError";
        case WGPUErrorType_Validation: return "Validation";
        case WGPUErrorType_OutOfMemory: return "OutOfMemory";
        case WGPUErrorType_Internal: return "Internal";
        case WGPUErrorType_Unknown: return "Unknown";
#if !defined(VOXY_WASM)
        case WGPUErrorType_DeviceLost: return "DeviceLost";
#endif
        default: return "???";
    }
}

const char* adapterTypeToString(WGPUAdapterType type) noexcept {
    switch (type) {
        case WGPUAdapterType_DiscreteGPU: return "Discrete GPU";
        case WGPUAdapterType_IntegratedGPU: return "Integrated GPU";
        case WGPUAdapterType_CPU: return "CPU";
        case WGPUAdapterType_Unknown: return "Unknown";
        default: return "???";
    }
}

const char* backendTypeToString(WGPUBackendType type) noexcept {
    switch (type) {
        case WGPUBackendType_Undefined: return "Undefined";
        case WGPUBackendType_Null: return "Null";
        case WGPUBackendType_WebGPU: return "WebGPU";
        case WGPUBackendType_D3D11: return "D3D11";
        case WGPUBackendType_D3D12: return "D3D12";
        case WGPUBackendType_Metal: return "Metal";
        case WGPUBackendType_Vulkan: return "Vulkan";
        case WGPUBackendType_OpenGL: return "OpenGL";
        case WGPUBackendType_OpenGLES: return "OpenGLES";
        default: return "???";
    }
}

const char* textureFormatToString(WGPUTextureFormat format) noexcept {
    switch (format) {
        case WGPUTextureFormat_Undefined: return "Undefined";
        case WGPUTextureFormat_R8Unorm: return "R8Unorm";
        case WGPUTextureFormat_R8Snorm: return "R8Snorm";
        case WGPUTextureFormat_R8Uint: return "R8Uint";
        case WGPUTextureFormat_R8Sint: return "R8Sint";
        case WGPUTextureFormat_R16Uint: return "R16Uint";
        case WGPUTextureFormat_R16Sint: return "R16Sint";
        case WGPUTextureFormat_R16Float: return "R16Float";
        case WGPUTextureFormat_RG8Unorm: return "RG8Unorm";
        case WGPUTextureFormat_RG8Snorm: return "RG8Snorm";
        case WGPUTextureFormat_RG8Uint: return "RG8Uint";
        case WGPUTextureFormat_RG8Sint: return "RG8Sint";
        case WGPUTextureFormat_R32Float: return "R32Float";
        case WGPUTextureFormat_R32Uint: return "R32Uint";
        case WGPUTextureFormat_R32Sint: return "R32Sint";
        case WGPUTextureFormat_RG16Uint: return "RG16Uint";
        case WGPUTextureFormat_RG16Sint: return "RG16Sint";
        case WGPUTextureFormat_RG16Float: return "RG16Float";
        case WGPUTextureFormat_RGBA8Unorm: return "RGBA8Unorm";
        case WGPUTextureFormat_RGBA8UnormSrgb: return "RGBA8UnormSrgb";
        case WGPUTextureFormat_RGBA8Snorm: return "RGBA8Snorm";
        case WGPUTextureFormat_RGBA8Uint: return "RGBA8Uint";
        case WGPUTextureFormat_RGBA8Sint: return "RGBA8Sint";
        case WGPUTextureFormat_BGRA8Unorm: return "BGRA8Unorm";
        case WGPUTextureFormat_BGRA8UnormSrgb: return "BGRA8UnormSrgb";
        case WGPUTextureFormat_RGB10A2Uint: return "RGB10A2Uint";
        case WGPUTextureFormat_RGB10A2Unorm: return "RGB10A2Unorm";
        case WGPUTextureFormat_RG11B10Ufloat: return "RG11B10Ufloat";
        case WGPUTextureFormat_RGB9E5Ufloat: return "RGB9E5Ufloat";
        case WGPUTextureFormat_RG32Float: return "RG32Float";
        case WGPUTextureFormat_RG32Uint: return "RG32Uint";
        case WGPUTextureFormat_RG32Sint: return "RG32Sint";
        case WGPUTextureFormat_RGBA16Uint: return "RGBA16Uint";
        case WGPUTextureFormat_RGBA16Sint: return "RGBA16Sint";
        case WGPUTextureFormat_RGBA16Float: return "RGBA16Float";
        case WGPUTextureFormat_RGBA32Float: return "RGBA32Float";
        case WGPUTextureFormat_RGBA32Uint: return "RGBA32Uint";
        case WGPUTextureFormat_RGBA32Sint: return "RGBA32Sint";
        case WGPUTextureFormat_Depth16Unorm: return "Depth16Unorm";
        case WGPUTextureFormat_Depth24Plus: return "Depth24Plus";
        case WGPUTextureFormat_Depth24PlusStencil8: return "Depth24PlusStencil8";
        case WGPUTextureFormat_Depth32Float: return "Depth32Float";
        case WGPUTextureFormat_Depth32FloatStencil8: return "Depth32FloatStencil8";
        default: return "Unknown";
    }
}

} // namespace voxy::gpu

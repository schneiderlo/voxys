#include "gpu/context.hpp"

#include "core/log.hpp"
#include "gpu/webgpu_compat.hpp"
#if defined(VOXY_NATIVE)
    #include "engine/platform/window.hpp"
#endif

#include <cstring>

#if defined(VOXY_WASM)
extern "C" {
    WGPUDevice emscripten_webgpu_get_device(void);
}
#endif

namespace voxy::gpu {

namespace {

#if !defined(VOXY_WASM)
const char* presentModeToString(WGPUPresentMode mode) noexcept {
    switch (mode) {
        case WGPUPresentMode_Fifo: return "Fifo (VSync)";
        case WGPUPresentMode_FifoRelaxed: return "FifoRelaxed";
        case WGPUPresentMode_Immediate: return "Immediate (uncapped)";
        case WGPUPresentMode_Mailbox: return "Mailbox";
        default: return "Unknown";
    }
}

bool supportsPresentMode(const WGPUSurfaceCapabilities& caps,
                         WGPUPresentMode mode) noexcept {
    for (size_t index = 0; index < caps.presentModeCount; ++index) {
        if (caps.presentModes[index] == mode) return true;
    }
    return false;
}

WGPUPresentMode selectPresentMode(const WGPUSurfaceCapabilities& caps,
                                  WGPUPresentMode requested) noexcept {
    if (supportsPresentMode(caps, requested)) return requested;
    if (requested == WGPUPresentMode_Immediate
        && supportsPresentMode(caps, WGPUPresentMode_Mailbox)) {
        return WGPUPresentMode_Mailbox;
    }
    if (supportsPresentMode(caps, WGPUPresentMode_Fifo)) {
        return WGPUPresentMode_Fifo;
    }
    return caps.presentModeCount > 0u
        ? caps.presentModes[0] : WGPUPresentMode_Fifo;
}
#endif

bool validSurfaceExtent(
    WGPUDevice device, uint32_t width, uint32_t height) {
    if (!device || width == 0u || height == 0u) return false;
    WGPULimits limits{};
    return getDeviceLimits(device, limits)
        && width <= limits.maxTextureDimension2D
        && height <= limits.maxTextureDimension2D;
}

} // namespace

bool Context::init(Window& window, const ContextConfig& config) {
    LOG_SCOPE("GPU");

    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }

    if (!createInstance(config)) {
        shutdown();
        return false;
    }

    if (!createSurface(window)) {
        shutdown();
        return false;
    }

    if (!requestAdapter(config)) {
        shutdown();
        return false;
    }

    queryAdapterInfo();
    if (config.forceDiscreteGPU
        && adapterInfo_.adapterType != WGPUAdapterType_DiscreteGPU) {
        LOG_ERROR(
            "A discrete GPU was required, but the selected adapter is {}",
            adapterTypeToString(adapterInfo_.adapterType));
        shutdown();
        return false;
    }

    if (!requestDevice(config)) {
        shutdown();
        return false;
    }

    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        shutdown();
        return false;
    }

    if (!configureSurface(config)) {
        shutdown();
        return false;
    }

    LOG_INFO("GPU context initialized successfully");
    return true;
}

#if defined(VOXY_WASM)
bool Context::initFromCanvas(
    const char* canvasSelector, const ContextConfig& config) {
    LOG_SCOPE("GPU");

    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }

    if (!createInstance(config)) {
        shutdown();
        return false;
    }

    if (!createSurfaceFromCanvas(canvasSelector)) {
        shutdown();
        return false;
    }

    device_ = emscripten_webgpu_get_device();
    if (!device_) {
        LOG_ERROR("emscripten_webgpu_get_device returned null");
        shutdown();
        return false;
    }
    if (!callbackState_) {
        callbackState_ = std::make_unique<CallbackState>();
    }
    callbackState_->context = this;

    queue_ = wgpuDeviceGetQueue(device_);
    if (!queue_) {
        LOG_ERROR("Failed to get device queue");
        shutdown();
        return false;
    }

    if (!configureSurface(config)) {
        shutdown();
        return false;
    }

    LOG_INFO("WASM GPU context initialized from canvas: {}", canvasSelector);
    return true;
}
#endif

bool Context::createSurface(Window& window) {
#if defined(VOXY_NATIVE)
    WGPUSurfaceDescriptor surfaceDesc = {};

    #if defined(__APPLE__)
    WGPUSurfaceDescriptorFromMetalLayer metalDesc = {};
    metalDesc.chain.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;
    metalDesc.layer = window.getCocoaWindow();
    surfaceDesc.nextInChain = &metalDesc.chain;

    #elif defined(_WIN32)
    WGPUSurfaceDescriptorFromWindowsHWND windowsDesc = {};
    windowsDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    windowsDesc.hinstance = window.getWin32Instance();
    windowsDesc.hwnd = window.getWin32Window();
    surfaceDesc.nextInChain = &windowsDesc.chain;

    #elif defined(__linux__)
    WGPUSurfaceDescriptorFromWaylandSurface waylandDesc = {};
    WGPUSurfaceDescriptorFromXlibWindow x11Desc = {};
    switch (window.getNativePlatform()) {
        case NativeWindowPlatform::Wayland:
            waylandDesc.chain.sType =
                WGPUSType_SurfaceDescriptorFromWaylandSurface;
            waylandDesc.display = window.getWaylandDisplay();
            waylandDesc.surface = window.getWaylandSurface();
            surfaceDesc.nextInChain = &waylandDesc.chain;
            break;
        case NativeWindowPlatform::X11:
            x11Desc.chain.sType =
                WGPUSType_SurfaceDescriptorFromXlibWindow;
            x11Desc.display = window.getX11Display();
            x11Desc.window = window.getX11Window();
            surfaceDesc.nextInChain = &x11Desc.chain;
            break;
        default:
            LOG_ERROR(
                "Unsupported Linux window backend: {}",
                nativeWindowPlatformName(
                    window.getNativePlatform()));
            return false;
    }
    #endif

    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (!surface_) {
        LOG_ERROR("Failed to create WebGPU surface");
        return false;
    }

    LOG_DEBUG(
        "WebGPU surface created for {}",
        nativeWindowPlatformName(window.getNativePlatform()));
    return true;

#elif defined(VOXY_WASM)
    static_cast<void>(window);
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasDesc.selector.data = "#voxy-canvas";
    canvasDesc.selector.length =
        std::strlen(canvasDesc.selector.data);

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain =
        reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);

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

#if defined(VOXY_WASM)
bool Context::createSurfaceFromCanvas(const char* selector) {
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasDesc.selector.data = selector;
    canvasDesc.selector.length = std::strlen(selector);

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain =
        reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);

    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    if (!surface_) {
        LOG_ERROR(
            "Failed to create WebGPU surface from canvas: {}", selector);
        return false;
    }

    LOG_DEBUG("WebGPU surface created from canvas: {}", selector);
    return true;
}
#endif

bool Context::configureSurface(const ContextConfig& config) {
    if (!surface_) {
        return true;
    }

    if ((config.swapchainWidth == 0u) != (config.swapchainHeight == 0u)) {
        LOG_ERROR("Surface dimensions must both be zero or both be nonzero");
        return false;
    }

#if defined(VOXY_WASM)
    swapchainFormat_ = (config.preferredFormat != WGPUTextureFormat_Undefined)
        ? config.preferredFormat
        : WGPUTextureFormat_BGRA8Unorm;

    swapchainWidth_ =
        config.swapchainWidth > 0 ? config.swapchainWidth : 1280;
    swapchainHeight_ =
        config.swapchainHeight > 0 ? config.swapchainHeight : 720;
    if (!validSurfaceExtent(
            device_, swapchainWidth_, swapchainHeight_)) {
        LOG_ERROR("Invalid or unsupported surface extent: {}x{}",
                  swapchainWidth_, swapchainHeight_);
        return false;
    }

    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device_;
    surfaceConfig.format = swapchainFormat_;
    surfaceConfig.usage =
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;

    lastSurfaceConfig_ = surfaceConfig;
    wgpuSurfaceConfigure(surface_, &surfaceConfig);

    LOG_DEBUG("WASM surface configured: {}x{}, format={}",
              swapchainWidth_, swapchainHeight_,
              textureFormatToString(swapchainFormat_));
    return true;
#else
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    if (caps.formatCount == 0u || !caps.formats) {
        LOG_ERROR("Surface reports no supported texture formats");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return false;
    }
    constexpr WGPUTextureUsageFlags requiredSurfaceUsage =
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    if ((caps.usages & requiredSurfaceUsage) != requiredSurfaceUsage
        || caps.presentModeCount == 0u || !caps.presentModes) {
        LOG_ERROR("Surface lacks required usage or presentation support");
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return false;
    }

    swapchainFormat_ = config.preferredFormat;
    bool formatSupported = false;
    for (size_t index = 0; index < caps.formatCount; ++index) {
        if (caps.formats[index] == config.preferredFormat) {
            formatSupported = true;
            break;
        }
    }

    if (!formatSupported) {
        swapchainFormat_ = caps.formats[0];
        LOG_WARN("Preferred format not supported, using: {}",
                 textureFormatToString(swapchainFormat_));
    }

    swapchainWidth_ =
        config.swapchainWidth > 0 ? config.swapchainWidth : 1280;
    swapchainHeight_ =
        config.swapchainHeight > 0 ? config.swapchainHeight : 720;
    if (!validSurfaceExtent(
            device_, swapchainWidth_, swapchainHeight_)) {
        LOG_ERROR("Invalid or unsupported surface extent: {}x{}",
                  swapchainWidth_, swapchainHeight_);
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return false;
    }

    const WGPUPresentMode selectedPresentMode =
        selectPresentMode(caps, config.presentMode);
    if (selectedPresentMode != config.presentMode) {
        LOG_WARN("Requested presentation mode {} is unavailable; using {}",
                 presentModeToString(config.presentMode),
                 presentModeToString(selectedPresentMode));
    }

    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.device = device_;
    surfaceConfig.format = swapchainFormat_;
    surfaceConfig.usage =
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;
    surfaceConfig.presentMode = selectedPresentMode;

    lastSurfaceConfig_ = surfaceConfig;
    wgpuSurfaceConfigure(surface_, &surfaceConfig);

    LOG_INFO("Surface configured: {}x{}, format={}, presentation={}",
             swapchainWidth_, swapchainHeight_,
             textureFormatToString(swapchainFormat_),
             presentModeToString(selectedPresentMode));

    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return true;
#endif
}

bool Context::resizeSwapchain(uint32_t width, uint32_t height) {
    if (!surface_ || !validSurfaceExtent(device_, width, height)
        || lastSurfaceConfig_.format == WGPUTextureFormat_Undefined) {
        return false;
    }

    if (width == swapchainWidth_ && height == swapchainHeight_) {
        return true;
    }

    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }

    swapchainWidth_ = width;
    swapchainHeight_ = height;

    WGPUSurfaceConfiguration surfaceConfig = lastSurfaceConfig_;
    surfaceConfig.width = swapchainWidth_;
    surfaceConfig.height = swapchainHeight_;
    surfaceConfig.device = device_;
    lastSurfaceConfig_ = surfaceConfig;

    wgpuSurfaceConfigure(surface_, &surfaceConfig);

    LOG_DEBUG("Swapchain resized: {}x{}", swapchainWidth_, swapchainHeight_);
    return true;
}

bool Context::setPresentMode(WGPUPresentMode mode) {
#if defined(VOXY_WASM)
    static_cast<void>(mode);
    LOG_DEBUG("setPresentMode ignored on WASM (browser only supports Fifo)");
    return true;
#else
    if (!surface_ || lastSurfaceConfig_.width == 0) {
        return false;
    }

    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    if (caps.presentModeCount == 0u || !caps.presentModes) {
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        return false;
    }
    const WGPUPresentMode selectedMode = selectPresentMode(caps, mode);
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    if (selectedMode != mode) {
        LOG_WARN("Requested presentation mode {} is unavailable; using {}",
                 presentModeToString(mode),
                 presentModeToString(selectedMode));
    }

    if (lastSurfaceConfig_.presentMode == selectedMode) {
        return mode == selectedMode
            || (mode == WGPUPresentMode_Immediate
                && selectedMode == WGPUPresentMode_Mailbox);
    }

    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }

    lastSurfaceConfig_.presentMode = selectedMode;
    wgpuSurfaceConfigure(surface_, &lastSurfaceConfig_);

    LOG_INFO("Presentation mode changed to: {}",
             presentModeToString(selectedMode));
    return mode == selectedMode
        || (mode == WGPUPresentMode_Immediate
            && selectedMode == WGPUPresentMode_Mailbox);
#endif
}

WGPUTextureView Context::getCurrentTextureView() {
    if (!surface_) {
        return nullptr;
    }

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

    if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Timeout) {
        // No frame was acquired. The caller returns before reserving GPU or
        // simulation work and can try again on its next render iteration.
        if (!surfaceTimeoutPending_) {
            LOG_WARN("Surface acquisition timed out; skipping this frame");
            surfaceTimeoutPending_ = true;
        }
        if (surfaceTexture.texture) wgpuTextureRelease(surfaceTexture.texture);
        return nullptr;
    }

#if defined(VOXY_WASM)
    if (surfaceTexture.status
            != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
        && surfaceTexture.status
            != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
#else
    if (surfaceTexture.status
        != WGPUSurfaceGetCurrentTextureStatus_Success) {
#endif
        LOG_ERROR("Failed to get current surface texture: status={}",
                  static_cast<int>(surfaceTexture.status));
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        return nullptr;
    }

    if (!surfaceTexture.texture) {
        LOG_ERROR("Surface acquisition succeeded without a texture");
        return nullptr;
    }

    if (surfaceTimeoutPending_) {
        LOG_INFO("Surface acquisition recovered after a temporary timeout");
        surfaceTimeoutPending_ = false;
    }
    currentTexture_ = surfaceTexture.texture;

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = swapchainFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    currentTextureView_ =
        wgpuTextureCreateView(currentTexture_, &viewDesc);
    if (!currentTextureView_) {
        LOG_ERROR("Failed to create the current surface texture view");
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
        return nullptr;
    }

    return currentTextureView_;
}

void Context::present() {
#if defined(VOXY_WASM)
    static_cast<void>(surface_);
#else
    if (surface_ && currentTexture_) {
        wgpuSurfacePresent(surface_);

        if (currentTextureView_) {
            wgpuTextureViewRelease(currentTextureView_);
            currentTextureView_ = nullptr;
        }
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }
#endif
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
        case WGPUTextureFormat_Depth24PlusStencil8:
            return "Depth24PlusStencil8";
        case WGPUTextureFormat_Depth32Float: return "Depth32Float";
        case WGPUTextureFormat_Depth32FloatStencil8:
            return "Depth32FloatStencil8";
        default: return "Unknown";
    }
}

} // namespace voxy::gpu

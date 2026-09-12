// ═══════════════════════════════════════════════════════════════════════════════
// context.cpp - WebGPU Context Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "gpu/context.hpp"
#include "core/log.hpp"

#include <vector>

#if !defined(VOXY_WASM) && !defined(VOXY_USE_DAWN)
    #include <wgpu/wgpu.h>
#endif

// Platform-specific sleep includes for Dawn async polling
#if defined(VOXY_USE_DAWN)
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <unistd.h>
    #endif
#endif

namespace voxy::gpu {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

Context::Context() = default;

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
    , lastSurfaceConfig_(other.lastSurfaceConfig_)
    , surfaceTimeoutPending_(other.surfaceTimeoutPending_)
    , adapterInfo_(std::move(other.adapterInfo_))
    , errorCallback_(std::move(other.errorCallback_))
    , deviceLostCallback_(std::move(other.deviceLostCallback_))
    , callbackState_(std::move(other.callbackState_))
{
    if (callbackState_) {
        callbackState_->context = this;
    }
    other.instance_ = nullptr;
    other.adapter_ = nullptr;
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.surface_ = nullptr;
    other.currentTexture_ = nullptr;
    other.currentTextureView_ = nullptr;
    other.surfaceTimeoutPending_ = false;
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
        lastSurfaceConfig_ = other.lastSurfaceConfig_;
        surfaceTimeoutPending_ = other.surfaceTimeoutPending_;
        adapterInfo_ = std::move(other.adapterInfo_);
        errorCallback_ = std::move(other.errorCallback_);
        deviceLostCallback_ = std::move(other.deviceLostCallback_);
        callbackState_ = std::move(other.callbackState_);
        if (callbackState_) {
            callbackState_->context = this;
        }
        
        other.instance_ = nullptr;
        other.adapter_ = nullptr;
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.surface_ = nullptr;
        other.currentTexture_ = nullptr;
        other.currentTextureView_ = nullptr;
        other.surfaceTimeoutPending_ = false;
    }
    return *this;
}

bool Context::initHeadless(const ContextConfig& config) {
    LOG_SCOPE("GPU");
    
    if (device_) {
        LOG_WARN("GPU context already initialized");
        return true;
    }
    
    if (!createInstance(config)) {
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
        LOG_ERROR("A discrete GPU was required, but the selected adapter is {}",
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
    
    LOG_INFO("Headless GPU context initialized successfully");
    return true;
}

void Context::shutdown() {
    // A final device callback may be delivered while the backend releases its
    // last handle. Never let that callback re-enter a half-torn-down Context.
    if (callbackState_) {
        callbackState_->context = nullptr;
    }

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
    lastSurfaceConfig_ = {};
    surfaceTimeoutPending_ = false;
    adapterInfo_ = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Instance Creation
// ─────────────────────────────────────────────────────────────────────────────

bool Context::createInstance(const ContextConfig& config) {
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;

#if defined(VOXY_NATIVE)
    WGPUInstanceExtras extras{};
    extras.chain.sType =
        static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    extras.backends = WGPUInstanceBackend_All;
    // A nonzero non-validation flag is deliberate: in wgpu-native, zero asks
    // for build-dependent defaults rather than an explicitly lean instance.
    extras.flags = config.enableValidation
        ? WGPUInstanceFlag_Validation
        : WGPUInstanceFlag_DiscardHalLabels;
    extras.dx12ShaderCompiler = WGPUDx12Compiler_Undefined;
    extras.gles3MinorVersion = WGPUGles3MinorVersion_Automatic;
    instanceDesc.nextInChain =
        reinterpret_cast<WGPUChainedStruct*>(&extras);
#else
    static_cast<void>(config);
#endif
    
    instance_ = wgpuCreateInstance(&instanceDesc);
    if (!instance_) {
        LOG_ERROR("Failed to create WebGPU instance");
        return false;
    }
    
    LOG_DEBUG("WebGPU instance created");
    return true;
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
    // This callback owns pointers to stack state. The legacy callback API has
    // no cancellation operation, so returning on an arbitrary timeout would
    // leave Dawn able to write through a dangling userdata pointer.
    while (!userData.done) {
        wgpuInstanceProcessEvents(instance_);
        #if defined(_WIN32)
            Sleep(1);
        #else
            usleep(1000);
        #endif
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
    
    if (config.enableTimestamps
        && wgpuAdapterHasFeature(adapter_, WGPUFeatureName_TimestampQuery)) {
        requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
    } else if (config.enableTimestamps) {
        LOG_WARN("GPU timestamp queries requested but unsupported; continuing without them");
    }
    
    // The callback userdata must stay at a stable address if Context is moved.
    if (!callbackState_) {
        callbackState_ = std::make_unique<CallbackState>();
    }
    callbackState_->context = this;

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
    deviceDesc.deviceLostUserdata = callbackState_.get();
    deviceDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    deviceDesc.uncapturedErrorCallbackInfo.userdata = callbackState_.get();
    
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
    // See requestAdapter(): request userdata must remain alive until Dawn
    // invokes the non-cancellable legacy callback.
    while (!userData.done) {
        wgpuInstanceProcessEvents(instance_);
        #if defined(_WIN32)
            Sleep(1);
        #else
            usleep(1000);
        #endif
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

void Context::tick() {
#if defined(VOXY_USE_DAWN)
    if (instance_) {
        wgpuInstanceProcessEvents(instance_);
    }
#elif !defined(VOXY_WASM)
    // Retire queue/map callbacks even when frame or simulation backpressure
    // prevents another submission. Polling must never wait for GPU completion.
    if(device_)static_cast<void>(wgpuDevicePoll(device_,false,nullptr));
#else
    // The browser event loop dispatches WebGPU callbacks.
    (void)device_;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Error Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void Context::onUncapturedError(WGPUErrorType type, const char* message, void* userdata) {
    auto* state = static_cast<CallbackState*>(userdata);
    auto* ctx = state ? state->context : nullptr;
    
    LOG_ERROR("WebGPU uncaptured error [{}]: {}", 
              errorTypeToString(type), message ? message : "unknown");
    
    if (ctx && ctx->errorCallback_) {
        ctx->errorCallback_(type, message);
    }
}

void Context::onDeviceLost(WGPUDeviceLostReason reason, const char* message, void* userdata) {
    auto* state = static_cast<CallbackState*>(userdata);
    auto* ctx = state ? state->context : nullptr;
    
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

} // namespace voxy::gpu

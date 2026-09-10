// ═══════════════════════════════════════════════════════════════════════════════
// test_gpu_context.cpp - Unit tests for GPU Context class
// ═══════════════════════════════════════════════════════════════════════════════
// Note: Full GPU context tests require wgpu-native to be installed. These tests
// verify the interface and basic functionality without requiring actual GPU access.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "gpu/context.hpp"
#include "perf/gpu_timer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstddef>
#include <cstdint>

#ifndef WGPUWrappedSubmissionIndex
using WGPUSubmissionIndex = uint64_t;
struct WGPUWrappedSubmissionIndex {
    WGPUQueue queue;
    WGPUSubmissionIndex submissionIndex;
};
#endif

extern "C" WGPUSubmissionIndex wgpuQueueSubmitForIndex(
    WGPUQueue queue, size_t commandCount,
    const WGPUCommandBuffer* commands);
extern "C" WGPUBool wgpuDevicePoll(
    WGPUDevice device, WGPUBool wait,
    const WGPUWrappedSubmissionIndex* wrappedSubmissionIndex);

namespace voxy::gpu {

// ─────────────────────────────────────────────────────────────────────────────
// ContextConfig Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ContextConfigTest, DefaultValues) {
    ContextConfig config;
    
    EXPECT_EQ(config.powerPreference, WGPUPowerPreference_HighPerformance);
    EXPECT_FALSE(config.forceDiscreteGPU);
    EXPECT_TRUE(config.enableValidation);
    EXPECT_FALSE(config.enableTimestamps);
    EXPECT_EQ(config.preferredFormat, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_EQ(config.presentMode, WGPUPresentMode_Fifo);
    EXPECT_EQ(config.swapchainWidth, 0u);
    EXPECT_EQ(config.swapchainHeight, 0u);
}

TEST(ContextConfigTest, CustomValues) {
    ContextConfig config;
    config.powerPreference = WGPUPowerPreference_LowPower;
    config.forceDiscreteGPU = true;
    config.enableValidation = false;
    config.enableTimestamps = true;
    config.preferredFormat = WGPUTextureFormat_RGBA8Unorm;
    config.presentMode = WGPUPresentMode_Immediate;
    config.swapchainWidth = 1920;
    config.swapchainHeight = 1080;
    
    EXPECT_EQ(config.powerPreference, WGPUPowerPreference_LowPower);
    EXPECT_TRUE(config.forceDiscreteGPU);
    EXPECT_FALSE(config.enableValidation);
    EXPECT_TRUE(config.enableTimestamps);
    EXPECT_EQ(config.preferredFormat, WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(config.presentMode, WGPUPresentMode_Immediate);
    EXPECT_EQ(config.swapchainWidth, 1920u);
    EXPECT_EQ(config.swapchainHeight, 1080u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AdapterInfo Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(AdapterInfoTest, DefaultValues) {
    AdapterInfo info;
    
    EXPECT_TRUE(info.vendor.empty());
    EXPECT_TRUE(info.architecture.empty());
    EXPECT_TRUE(info.device.empty());
    EXPECT_TRUE(info.description.empty());
    EXPECT_EQ(info.adapterType, WGPUAdapterType_Unknown);
    EXPECT_EQ(info.backendType, WGPUBackendType_Undefined);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context Class Tests (interface only)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ContextTest, DefaultConstruction) {
    Context context;
    
    EXPECT_FALSE(context.isInitialized());
    EXPECT_FALSE(context.hasSurface());
    EXPECT_EQ(context.getInstance(), nullptr);
    EXPECT_EQ(context.getAdapter(), nullptr);
    EXPECT_EQ(context.getDevice(), nullptr);
    EXPECT_EQ(context.getQueue(), nullptr);
    EXPECT_EQ(context.getSurface(), nullptr);
}

TEST(ContextTest, MoveConstruction) {
    Context context1;
    Context context2(std::move(context1));
    
    EXPECT_FALSE(context1.isInitialized());
    EXPECT_FALSE(context2.isInitialized());
}

TEST(ContextTest, MoveAssignment) {
    Context context1;
    Context context2;
    
    context2 = std::move(context1);
    
    EXPECT_FALSE(context1.isInitialized());
    EXPECT_FALSE(context2.isInitialized());
}

TEST(ContextTest, SwapchainDimensionsDefault) {
    Context context;
    
    EXPECT_EQ(context.getSwapchainWidth(), 0u);
    EXPECT_EQ(context.getSwapchainHeight(), 0u);
    EXPECT_EQ(context.getSwapchainFormat(), WGPUTextureFormat_Undefined);
}

TEST(ContextTest, CallbackSetters) {
    Context context;
    
    bool errorCalled = false;
    bool deviceLostCalled = false;
    
    // Setting callbacks should not crash
    context.setErrorCallback([&](WGPUErrorType, const char*) { errorCalled = true; });
    context.setDeviceLostCallback([&](WGPUDeviceLostReason, const char*) { deviceLostCalled = true; });
    
    SUCCEED();
}

TEST(ContextTest, GetCurrentTextureViewWithoutInit) {
    Context context;
    
    // Should return nullptr without crashing
    EXPECT_EQ(context.getCurrentTextureView(), nullptr);
}

TEST(ContextTest, PresentWithoutInit) {
    Context context;
    
    // Should not crash
    context.present();
    SUCCEED();
}

TEST(ContextTest, TickWithoutInit) {
    Context context;
    
    // Should not crash
    context.tick();
    SUCCEED();
}

TEST(ContextTest, TickRetiresReadbackAndQueueCallbacksWithoutFurtherSubmissions) {
    struct Signals {
        std::atomic<bool> mapped=false,completed=false;
        WGPUBufferMapAsyncStatus mapStatus{};
        WGPUQueueWorkDoneStatus queueStatus{};
    } signals; // Must outlive device shutdown, including assertion failure.
    Context context;
    ASSERT_TRUE(context.initHeadless({}));
    WGPUBufferDescriptor desc{};desc.size=16;
    desc.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;
    auto buffer=wgpuDeviceCreateBuffer(context.getDevice(),&desc);ASSERT_NE(buffer,nullptr);
    const std::array<uint32_t,4> expected{1,17,9001,0xffffffffu};
    wgpuQueueWriteBuffer(context.getQueue(),buffer,0,expected.data(),sizeof(expected));
    wgpuQueueSubmit(context.getQueue(),0,nullptr);
    wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,sizeof(expected),[](auto status,void* data){
        auto& result=*static_cast<Signals*>(data);result.mapStatus=status;result.mapped=true;
    },&signals);
    wgpuQueueOnSubmittedWorkDone(context.getQueue(),[](auto status,void* data){
        auto& result=*static_cast<Signals*>(data);result.queueStatus=status;result.completed=true;
    },&signals);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while((!signals.mapped || !signals.completed) && std::chrono::steady_clock::now()<deadline){
        context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(signals.completed);EXPECT_TRUE(signals.mapped);
    if(signals.mapped && signals.mapStatus==WGPUBufferMapAsyncStatus_Success){
        const auto* values=static_cast<const uint32_t*>(wgpuBufferGetConstMappedRange(buffer,0,sizeof(expected)));
        ASSERT_NE(values,nullptr);
        for(size_t i=0;i<expected.size();++i)EXPECT_EQ(values[i],expected[i]);
        wgpuBufferUnmap(buffer);
    }
    EXPECT_EQ(signals.mapStatus,WGPUBufferMapAsyncStatus_Success);
    EXPECT_EQ(signals.queueStatus,WGPUQueueWorkDoneStatus_Success);
    wgpuBufferRelease(buffer);
}

TEST(ContextTest, ResizeSwapchainWithoutInit) {
    Context context;
    
    EXPECT_FALSE(context.resizeSwapchain(1920, 1080));
    EXPECT_FALSE(context.resizeSwapchain(0, 1080));
}

TEST(ContextTest, ShutdownWithoutInit) {
    Context context;
    
    // Should not crash
    context.shutdown();
    EXPECT_FALSE(context.isInitialized());
}

TEST(GPUTimerTest, ResolvesTimestampQueriesAndPreservesLiveState) {
    Context context;
    ContextConfig config;
    config.enableTimestamps = true;
    if (!context.initHeadless(config)) GTEST_SKIP() << "No WebGPU adapter";
    if (!wgpuDeviceHasFeature(
            context.getDevice(), WGPUFeatureName_TimestampQuery)) {
        GTEST_SKIP() << "Timestamp queries unavailable";
    }

    perf::GPUTimer timer;
    ASSERT_TRUE(timer.init(context.getDevice()));
    EXPECT_FALSE(timer.init(nullptr));
    EXPECT_TRUE(timer.isSupported());
    ASSERT_TRUE(timer.beginFrame());

    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDesc);
    ASSERT_NE(encoder, nullptr);
    EXPECT_FALSE(timer.writeTimestamp(encoder, nullptr));
    ASSERT_TRUE(timer.writeTimestamp(encoder, "begin"));
    ASSERT_TRUE(timer.writeTimestamp(encoder, "end"));
    ASSERT_TRUE(timer.resolve(encoder));
    EXPECT_FALSE(timer.beginFrame());

    WGPUCommandBufferDescriptor commandDesc{};
    WGPUCommandBuffer command =
        wgpuCommandEncoderFinish(encoder, &commandDesc);
    ASSERT_NE(command, nullptr);
    const WGPUSubmissionIndex submissionIndex = wgpuQueueSubmitForIndex(
        context.getQueue(), 1u, &command);
    const WGPUWrappedSubmissionIndex submission{
        context.getQueue(), submissionIndex};
    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);

    perf::GPUTimingResult result = timer.readResults();
    for (uint32_t attempt = 0; !result.valid && attempt < 64u; ++attempt) {
        static_cast<void>(wgpuDevicePoll(
            context.getDevice(), true, &submission));
        result = timer.readResults();
    }
    ASSERT_TRUE(result.valid);
    ASSERT_EQ(result.timestamps.size(), 2u);
    EXPECT_EQ(result.timestamps[0].label, "begin");
    EXPECT_EQ(result.timestamps[1].label, "end");
    EXPECT_DOUBLE_EQ(result.timestamps[0].timeMs, 0.0);
    EXPECT_GE(result.timestamps[1].timeMs, 0.0);
    EXPECT_EQ(timer.getLastResults().timestamps.size(), 2u);
    EXPECT_TRUE(timer.beginFrame());
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility Function Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ContextUtilsTest, ErrorTypeToString) {
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_NoError), "NoError");
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_Validation), "Validation");
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_OutOfMemory), "OutOfMemory");
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_Internal), "Internal");
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_Unknown), "Unknown");
    EXPECT_STREQ(errorTypeToString(WGPUErrorType_DeviceLost), "DeviceLost");
}

TEST(ContextUtilsTest, AdapterTypeToString) {
    EXPECT_STREQ(adapterTypeToString(WGPUAdapterType_DiscreteGPU), "Discrete GPU");
    EXPECT_STREQ(adapterTypeToString(WGPUAdapterType_IntegratedGPU), "Integrated GPU");
    EXPECT_STREQ(adapterTypeToString(WGPUAdapterType_CPU), "CPU");
    EXPECT_STREQ(adapterTypeToString(WGPUAdapterType_Unknown), "Unknown");
}

TEST(ContextUtilsTest, BackendTypeToString) {
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_Undefined), "Undefined");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_Null), "Null");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_WebGPU), "WebGPU");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_D3D11), "D3D11");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_D3D12), "D3D12");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_Metal), "Metal");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_Vulkan), "Vulkan");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_OpenGL), "OpenGL");
    EXPECT_STREQ(backendTypeToString(WGPUBackendType_OpenGLES), "OpenGLES");
}

TEST(ContextUtilsTest, TextureFormatToString) {
    EXPECT_STREQ(textureFormatToString(WGPUTextureFormat_Undefined), "Undefined");
    EXPECT_STREQ(textureFormatToString(WGPUTextureFormat_BGRA8Unorm), "BGRA8Unorm");
    EXPECT_STREQ(textureFormatToString(WGPUTextureFormat_RGBA8Unorm), "RGBA8Unorm");
    EXPECT_STREQ(textureFormatToString(WGPUTextureFormat_Depth32Float), "Depth32Float");
    EXPECT_STREQ(textureFormatToString(WGPUTextureFormat_R32Float), "R32Float");
}

} // namespace voxy::gpu

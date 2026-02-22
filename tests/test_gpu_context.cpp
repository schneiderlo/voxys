// ═══════════════════════════════════════════════════════════════════════════════
// test_gpu_context.cpp - Unit tests for GPU Context class
// ═══════════════════════════════════════════════════════════════════════════════
// Note: Full GPU context tests require wgpu-native to be installed. These tests
// verify the interface and basic functionality without requiring actual GPU access.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "gpu/context.hpp"

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

TEST(ContextTest, ResizeSwapchainWithoutInit) {
    Context context;
    
    // Should not crash
    context.resizeSwapchain(1920, 1080);
    SUCCEED();
}

TEST(ContextTest, ShutdownWithoutInit) {
    Context context;
    
    // Should not crash
    context.shutdown();
    EXPECT_FALSE(context.isInitialized());
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


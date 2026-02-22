// ═══════════════════════════════════════════════════════════════════════════════
// test_gpu_resources.cpp - Unit tests for GPU Resource Helpers
// ═══════════════════════════════════════════════════════════════════════════════
// Tests for buffer, texture, sampler, bind group, and shader module creation
// helpers. These tests verify the interface and descriptor builders without
// requiring actual GPU access.
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include "gpu/resources.hpp"

namespace voxy::gpu {

// ═══════════════════════════════════════════════════════════════════════════════
// BufferDesc Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BufferDescTest, DefaultValues) {
    BufferDesc desc;
    
    EXPECT_TRUE(desc.label.empty());
    EXPECT_EQ(desc.size, 0u);
    EXPECT_EQ(desc.usage, WGPUBufferUsage_None);
    EXPECT_FALSE(desc.mappedAtCreation);
}

TEST(BufferDescTest, UniformFactory) {
    auto desc = BufferDesc::uniform(256, "test_uniform");
    
    EXPECT_EQ(desc.label, "test_uniform");
    EXPECT_EQ(desc.size, 256u);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_Uniform);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopyDst);
    EXPECT_FALSE(desc.mappedAtCreation);
}

TEST(BufferDescTest, VertexFactory) {
    auto desc = BufferDesc::vertex(1024, "test_vertex");
    
    EXPECT_EQ(desc.label, "test_vertex");
    EXPECT_EQ(desc.size, 1024u);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_Vertex);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopyDst);
}

TEST(BufferDescTest, IndexFactory) {
    auto desc = BufferDesc::index(512, "test_index");
    
    EXPECT_EQ(desc.label, "test_index");
    EXPECT_EQ(desc.size, 512u);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_Index);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopyDst);
}

TEST(BufferDescTest, StorageFactory) {
    auto desc = BufferDesc::storage(2048, false, "test_storage");
    
    EXPECT_EQ(desc.label, "test_storage");
    EXPECT_EQ(desc.size, 2048u);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_Storage);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopyDst);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopySrc);
    
    // Read-only storage should not have CopySrc
    auto readOnlyDesc = BufferDesc::storage(2048, true, "test_storage_ro");
    EXPECT_TRUE(readOnlyDesc.usage & WGPUBufferUsage_Storage);
    EXPECT_FALSE(readOnlyDesc.usage & WGPUBufferUsage_CopySrc);
}

TEST(BufferDescTest, StagingFactory) {
    auto desc = BufferDesc::staging(4096, "test_staging");
    
    EXPECT_EQ(desc.label, "test_staging");
    EXPECT_EQ(desc.size, 4096u);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_CopySrc);
    EXPECT_TRUE(desc.usage & WGPUBufferUsage_MapWrite);
    EXPECT_TRUE(desc.mappedAtCreation);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TextureDesc Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TextureDescTest, DefaultValues) {
    TextureDesc desc;
    
    EXPECT_TRUE(desc.label.empty());
    EXPECT_EQ(desc.width, 1u);
    EXPECT_EQ(desc.height, 1u);
    EXPECT_EQ(desc.depthOrArrayLayers, 1u);
    EXPECT_EQ(desc.mipLevelCount, 1u);
    EXPECT_EQ(desc.sampleCount, 1u);
    EXPECT_EQ(desc.dimension, WGPUTextureDimension_2D);
    EXPECT_EQ(desc.format, WGPUTextureFormat_RGBA8Unorm);
}

TEST(TextureDescTest, Tex2DFactory) {
    auto desc = TextureDesc::tex2D(512, 256, WGPUTextureFormat_R16Uint,
                                    WGPUTextureUsage_TextureBinding, "test_tex2d");
    
    EXPECT_EQ(desc.label, "test_tex2d");
    EXPECT_EQ(desc.width, 512u);
    EXPECT_EQ(desc.height, 256u);
    EXPECT_EQ(desc.format, WGPUTextureFormat_R16Uint);
    EXPECT_EQ(desc.mipLevelCount, 1u);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_TextureBinding);
}

TEST(TextureDescTest, Tex2DMipmappedFactory) {
    auto desc = TextureDesc::tex2DMipmapped(1024, 1024, WGPUTextureFormat_RGBA8Unorm);
    
    EXPECT_EQ(desc.width, 1024u);
    EXPECT_EQ(desc.height, 1024u);
    EXPECT_EQ(desc.mipLevelCount, 11u);  // log2(1024) + 1 = 11
    
    // Non-square texture
    auto desc2 = TextureDesc::tex2DMipmapped(256, 64, WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(desc2.mipLevelCount, 9u);  // log2(256) + 1 = 9
}

TEST(TextureDescTest, DepthFactory) {
    auto desc = TextureDesc::depth(1920, 1080, WGPUTextureFormat_Depth32Float, "depth_buffer");
    
    EXPECT_EQ(desc.label, "depth_buffer");
    EXPECT_EQ(desc.width, 1920u);
    EXPECT_EQ(desc.height, 1080u);
    EXPECT_EQ(desc.format, WGPUTextureFormat_Depth32Float);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_RenderAttachment);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_TextureBinding);
}

TEST(TextureDescTest, StorageFactory) {
    auto desc = TextureDesc::storage(640, 480, WGPUTextureFormat_R32Float, "storage_tex");
    
    EXPECT_EQ(desc.label, "storage_tex");
    EXPECT_EQ(desc.width, 640u);
    EXPECT_EQ(desc.height, 480u);
    EXPECT_EQ(desc.format, WGPUTextureFormat_R32Float);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_StorageBinding);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_TextureBinding);
}

TEST(TextureDescTest, RenderTargetFactory) {
    auto desc = TextureDesc::renderTarget(800, 600, WGPUTextureFormat_BGRA8Unorm, "render_target");
    
    EXPECT_EQ(desc.label, "render_target");
    EXPECT_EQ(desc.width, 800u);
    EXPECT_EQ(desc.height, 600u);
    EXPECT_EQ(desc.format, WGPUTextureFormat_BGRA8Unorm);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_RenderAttachment);
    EXPECT_TRUE(desc.usage & WGPUTextureUsage_TextureBinding);
}

// ═══════════════════════════════════════════════════════════════════════════════
// TextureViewDesc Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(TextureViewDescTest, DefaultValues) {
    TextureViewDesc desc;
    
    EXPECT_TRUE(desc.label.empty());
    EXPECT_EQ(desc.format, WGPUTextureFormat_Undefined);
    EXPECT_EQ(desc.dimension, WGPUTextureViewDimension_2D);
    EXPECT_EQ(desc.baseMipLevel, 0u);
    EXPECT_EQ(desc.mipLevelCount, 1u);
    EXPECT_EQ(desc.baseArrayLayer, 0u);
    EXPECT_EQ(desc.arrayLayerCount, 1u);
    EXPECT_EQ(desc.aspect, WGPUTextureAspect_All);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SamplerDesc Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SamplerDescTest, DefaultValues) {
    SamplerDesc desc;
    
    EXPECT_TRUE(desc.label.empty());
    EXPECT_EQ(desc.addressModeU, WGPUAddressMode_ClampToEdge);
    EXPECT_EQ(desc.addressModeV, WGPUAddressMode_ClampToEdge);
    EXPECT_EQ(desc.addressModeW, WGPUAddressMode_ClampToEdge);
    EXPECT_EQ(desc.magFilter, WGPUFilterMode_Linear);
    EXPECT_EQ(desc.minFilter, WGPUFilterMode_Linear);
    EXPECT_EQ(desc.mipmapFilter, WGPUMipmapFilterMode_Linear);
    EXPECT_FLOAT_EQ(desc.lodMinClamp, 0.0f);
    EXPECT_FLOAT_EQ(desc.lodMaxClamp, 32.0f);
    EXPECT_EQ(desc.compare, WGPUCompareFunction_Undefined);
    EXPECT_EQ(desc.maxAnisotropy, 1u);
}

TEST(SamplerDescTest, LinearFactory) {
    auto desc = SamplerDesc::linear("linear_sampler");
    
    EXPECT_EQ(desc.label, "linear_sampler");
    EXPECT_EQ(desc.magFilter, WGPUFilterMode_Linear);
    EXPECT_EQ(desc.minFilter, WGPUFilterMode_Linear);
}

TEST(SamplerDescTest, NearestFactory) {
    auto desc = SamplerDesc::nearest("nearest_sampler");
    
    EXPECT_EQ(desc.label, "nearest_sampler");
    EXPECT_EQ(desc.magFilter, WGPUFilterMode_Nearest);
    EXPECT_EQ(desc.minFilter, WGPUFilterMode_Nearest);
    EXPECT_EQ(desc.mipmapFilter, WGPUMipmapFilterMode_Nearest);
}

TEST(SamplerDescTest, RepeatFactory) {
    auto desc = SamplerDesc::repeat(WGPUFilterMode_Linear, "repeat_sampler");
    
    EXPECT_EQ(desc.label, "repeat_sampler");
    EXPECT_EQ(desc.addressModeU, WGPUAddressMode_Repeat);
    EXPECT_EQ(desc.addressModeV, WGPUAddressMode_Repeat);
    EXPECT_EQ(desc.addressModeW, WGPUAddressMode_Repeat);
}

TEST(SamplerDescTest, ComparisonFactory) {
    auto desc = SamplerDesc::comparison(WGPUCompareFunction_Less, "shadow_sampler");
    
    EXPECT_EQ(desc.label, "shadow_sampler");
    EXPECT_EQ(desc.compare, WGPUCompareFunction_Less);
}

TEST(SamplerDescTest, AnisotropicFactory) {
    auto desc = SamplerDesc::anisotropic(8, "aniso_sampler");
    
    EXPECT_EQ(desc.label, "aniso_sampler");
    EXPECT_EQ(desc.maxAnisotropy, 8u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BindGroupLayoutEntry Tests (Builder Pattern)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BindGroupLayoutEntryTest, Construction) {
    BindGroupLayoutEntry entry(0);
    auto& raw = entry.get();
    
    EXPECT_EQ(raw.binding, 0u);
    EXPECT_EQ(raw.visibility, WGPUShaderStage_None);
}

TEST(BindGroupLayoutEntryTest, VisibilityFlags) {
    BindGroupLayoutEntry entry(0);
    entry.vertexVisible().fragmentVisible();
    
    auto& raw = entry.get();
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Vertex);
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Fragment);
    EXPECT_FALSE(raw.visibility & WGPUShaderStage_Compute);
}

TEST(BindGroupLayoutEntryTest, ComputeVisibility) {
    BindGroupLayoutEntry entry(1);
    entry.computeVisible();
    
    auto& raw = entry.get();
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Compute);
}

TEST(BindGroupLayoutEntryTest, AllStagesVisible) {
    BindGroupLayoutEntry entry(2);
    entry.allStagesVisible();
    
    auto& raw = entry.get();
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Vertex);
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Fragment);
    EXPECT_TRUE(raw.visibility & WGPUShaderStage_Compute);
}

TEST(BindGroupLayoutEntryTest, UniformBuffer) {
    BindGroupLayoutEntry entry(0);
    entry.vertexVisible().uniformBuffer(false, 64);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.buffer.type, WGPUBufferBindingType_Uniform);
    EXPECT_FALSE(raw.buffer.hasDynamicOffset);
    EXPECT_EQ(raw.buffer.minBindingSize, 64u);
}

TEST(BindGroupLayoutEntryTest, StorageBuffer) {
    BindGroupLayoutEntry entry(1);
    entry.computeVisible().storageBuffer(false, false, 128);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.buffer.type, WGPUBufferBindingType_Storage);
    EXPECT_EQ(raw.buffer.minBindingSize, 128u);
    
    // Read-only storage
    BindGroupLayoutEntry roEntry(2);
    roEntry.computeVisible().storageBuffer(true);
    
    auto& roRaw = roEntry.get();
    EXPECT_EQ(roRaw.buffer.type, WGPUBufferBindingType_ReadOnlyStorage);
}

TEST(BindGroupLayoutEntryTest, TextureBinding) {
    BindGroupLayoutEntry entry(0);
    entry.fragmentVisible().texture(WGPUTextureSampleType_Float, WGPUTextureViewDimension_2D);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.texture.sampleType, WGPUTextureSampleType_Float);
    EXPECT_EQ(raw.texture.viewDimension, WGPUTextureViewDimension_2D);
    EXPECT_FALSE(raw.texture.multisampled);
}

TEST(BindGroupLayoutEntryTest, StorageTexture) {
    BindGroupLayoutEntry entry(0);
    entry.computeVisible().storageTexture(WGPUStorageTextureAccess_WriteOnly,
                                           WGPUTextureFormat_R32Float);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.storageTexture.access, WGPUStorageTextureAccess_WriteOnly);
    EXPECT_EQ(raw.storageTexture.format, WGPUTextureFormat_R32Float);
    EXPECT_EQ(raw.storageTexture.viewDimension, WGPUTextureViewDimension_2D);
}

TEST(BindGroupLayoutEntryTest, SamplerBinding) {
    BindGroupLayoutEntry entry(0);
    entry.fragmentVisible().sampler(WGPUSamplerBindingType_Filtering);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.sampler.type, WGPUSamplerBindingType_Filtering);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BindGroupEntry Tests (Builder Pattern)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BindGroupEntryTest, Construction) {
    BindGroupEntry entry(0);
    auto& raw = entry.get();
    
    EXPECT_EQ(raw.binding, 0u);
    EXPECT_EQ(raw.buffer, nullptr);
    EXPECT_EQ(raw.sampler, nullptr);
    EXPECT_EQ(raw.textureView, nullptr);
}

TEST(BindGroupEntryTest, BufferBinding) {
    BindGroupEntry entry(1);
    // Note: We can't test with actual buffers without a device,
    // but we can verify the structure is set up correctly
    entry.buffer(nullptr, 64, 128);
    
    auto& raw = entry.get();
    EXPECT_EQ(raw.binding, 1u);
    EXPECT_EQ(raw.offset, 64u);
    EXPECT_EQ(raw.size, 128u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Function Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(UtilityTest, BytesPerPixel) {
    // 8-bit formats
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_R8Unorm), 1u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_R8Uint), 1u);
    
    // 16-bit formats
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_R16Uint), 2u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_R16Float), 2u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_RG8Unorm), 2u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_Depth16Unorm), 2u);
    
    // 32-bit formats
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_R32Float), 4u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_RGBA8Unorm), 4u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_BGRA8Unorm), 4u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_Depth32Float), 4u);
    
    // 64-bit formats
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_RG32Float), 8u);
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_RGBA16Float), 8u);
    
    // 128-bit formats
    EXPECT_EQ(getBytesPerPixel(WGPUTextureFormat_RGBA32Float), 16u);
}

TEST(UtilityTest, IsDepthStencilFormat) {
    EXPECT_TRUE(isDepthStencilFormat(WGPUTextureFormat_Depth16Unorm));
    EXPECT_TRUE(isDepthStencilFormat(WGPUTextureFormat_Depth24Plus));
    EXPECT_TRUE(isDepthStencilFormat(WGPUTextureFormat_Depth24PlusStencil8));
    EXPECT_TRUE(isDepthStencilFormat(WGPUTextureFormat_Depth32Float));
    EXPECT_TRUE(isDepthStencilFormat(WGPUTextureFormat_Depth32FloatStencil8));
    
    EXPECT_FALSE(isDepthStencilFormat(WGPUTextureFormat_R8Unorm));
    EXPECT_FALSE(isDepthStencilFormat(WGPUTextureFormat_RGBA8Unorm));
    EXPECT_FALSE(isDepthStencilFormat(WGPUTextureFormat_R32Float));
}

TEST(UtilityTest, AlignUniformBufferSize) {
    EXPECT_EQ(alignUniformBufferSize(0), 0u);
    EXPECT_EQ(alignUniformBufferSize(1), 256u);
    EXPECT_EQ(alignUniformBufferSize(255), 256u);
    EXPECT_EQ(alignUniformBufferSize(256), 256u);
    EXPECT_EQ(alignUniformBufferSize(257), 512u);
    EXPECT_EQ(alignUniformBufferSize(512), 512u);
    EXPECT_EQ(alignUniformBufferSize(1000), 1024u);
}

TEST(UtilityTest, CalculateMipLevelCount) {
    // Power of two textures
    EXPECT_EQ(calculateMipLevelCount(1, 1), 1u);
    EXPECT_EQ(calculateMipLevelCount(2, 2), 2u);
    EXPECT_EQ(calculateMipLevelCount(4, 4), 3u);
    EXPECT_EQ(calculateMipLevelCount(256, 256), 9u);
    EXPECT_EQ(calculateMipLevelCount(1024, 1024), 11u);
    EXPECT_EQ(calculateMipLevelCount(8192, 8192), 14u);
    
    // Non-square textures (uses max dimension)
    EXPECT_EQ(calculateMipLevelCount(256, 64), 9u);
    EXPECT_EQ(calculateMipLevelCount(1024, 512), 11u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Null Device Tests (Error Handling)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ErrorHandlingTest, CreateBufferNullDevice) {
    auto desc = BufferDesc::uniform(256);
    EXPECT_EQ(createBuffer(nullptr, desc), nullptr);
}

TEST(ErrorHandlingTest, CreateBufferZeroSize) {
    // Note: Would need a valid device to test this fully
    BufferDesc desc;
    desc.size = 0;
    EXPECT_EQ(createBuffer(nullptr, desc), nullptr);
}

TEST(ErrorHandlingTest, CreateTextureNullDevice) {
    auto desc = TextureDesc::tex2D(256, 256, WGPUTextureFormat_RGBA8Unorm);
    EXPECT_EQ(createTexture(nullptr, desc), nullptr);
}

TEST(ErrorHandlingTest, CreateTextureViewNullTexture) {
    EXPECT_EQ(createTextureView(nullptr), nullptr);
}

TEST(ErrorHandlingTest, CreateSamplerNullDevice) {
    auto desc = SamplerDesc::linear();
    EXPECT_EQ(createSampler(nullptr, desc), nullptr);
}

TEST(ErrorHandlingTest, CreateBindGroupLayoutNullDevice) {
    std::vector<BindGroupLayoutEntry> entries;
    EXPECT_EQ(createBindGroupLayout(nullptr, entries), nullptr);
}

TEST(ErrorHandlingTest, CreateBindGroupNullDevice) {
    std::vector<BindGroupEntry> entries;
    EXPECT_EQ(createBindGroup(nullptr, nullptr, entries), nullptr);
}

TEST(ErrorHandlingTest, CreateShaderModuleNullDevice) {
    EXPECT_EQ(createShaderModule(nullptr, "// empty shader"), nullptr);
}

TEST(ErrorHandlingTest, CreateShaderModuleEmptySource) {
    EXPECT_EQ(createShaderModule(nullptr, ""), nullptr);
}

TEST(ErrorHandlingTest, CreatePipelineLayoutNullDevice) {
    std::vector<WGPUBindGroupLayout> layouts;
    EXPECT_EQ(createPipelineLayout(nullptr, layouts), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Buffer Usage Operator Tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(BufferUsageTest, OrOperator) {
    auto combined = BufferUsage::Vertex | BufferUsage::CopyDst;
    EXPECT_TRUE(combined & WGPUBufferUsage_Vertex);
    EXPECT_TRUE(combined & WGPUBufferUsage_CopyDst);
}

TEST(BufferUsageTest, OrOperatorWithFlags) {
    WGPUBufferUsageFlags flags = WGPUBufferUsage_Uniform;
    auto combined = flags | BufferUsage::CopyDst;
    EXPECT_TRUE(combined & WGPUBufferUsage_Uniform);
    EXPECT_TRUE(combined & WGPUBufferUsage_CopyDst);
}

} // namespace voxy::gpu






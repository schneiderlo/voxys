// ═══════════════════════════════════════════════════════════════════════════════
// mip_pipeline.cpp - GPU Max-Height Mip Chain Generation Pipeline Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "render/mip_pipeline.hpp"
#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <array>
#include <fstream>
#include <sstream>
#include <vector>

namespace voxy::render {

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

MipGeneratorPipeline::~MipGeneratorPipeline() {
    shutdown();
}

MipGeneratorPipeline::MipGeneratorPipeline(MipGeneratorPipeline&& other) noexcept
    : shaderModule_(other.shaderModule_)
    , pipeline_(other.pipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , pipelineLayout_(other.pipelineLayout_)
    , paramsBuffer_(other.paramsBuffer_)
{
    other.shaderModule_ = nullptr;
    other.pipeline_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.paramsBuffer_ = nullptr;
}

MipGeneratorPipeline& MipGeneratorPipeline::operator=(MipGeneratorPipeline&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        shaderModule_ = other.shaderModule_;
        pipeline_ = other.pipeline_;
        bindGroupLayout_ = other.bindGroupLayout_;
        pipelineLayout_ = other.pipelineLayout_;
        paramsBuffer_ = other.paramsBuffer_;
        
        other.shaderModule_ = nullptr;
        other.pipeline_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.paramsBuffer_ = nullptr;
    }
    return *this;
}

void MipGeneratorPipeline::shutdown() {
    if (paramsBuffer_) {
        wgpuBufferDestroy(paramsBuffer_);
        wgpuBufferRelease(paramsBuffer_);
        paramsBuffer_ = nullptr;
    }
    
    if (pipeline_) {
        wgpuComputePipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════════════════════

bool MipGeneratorPipeline::init(WGPUDevice device, const std::filesystem::path& shaderPath) {
    if (!device) {
        LOG_ERROR("MipGeneratorPipeline: Cannot init with null device");
        return false;
    }
    
    // Load shader source from file
    std::ifstream file(shaderPath);
    if (!file.is_open()) {
        LOG_ERROR("MipGeneratorPipeline: Failed to open shader file: {}", shaderPath.string());
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    return initWithSource(device, source);
}

bool MipGeneratorPipeline::initWithSource(WGPUDevice device, std::string_view shaderSource) {
    if (!device) {
        LOG_ERROR("MipGeneratorPipeline: Cannot init with null device");
        return false;
    }
    
    // Release any existing resources
    shutdown();
    
    // Create shader module
    shaderModule_ = gpu::createShaderModule(device, shaderSource, "mip_generate");
    if (!shaderModule_) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create shader module");
        return false;
    }
    
    // Create bind group layout
    // Binding 0: Source texture (sampled)
    // Binding 1: Destination texture (storage, write-only)
    //            NOTE: r16uint is NOT a valid storage texture format in WebGPU 1.0.
    //            We use r32uint instead. The heightmap must be uploaded as R32Uint
    //            when GPU mip generation is enabled.
    // Binding 2: Params uniform buffer
    std::array<gpu::BindGroupLayoutEntry, 3> layoutEntries = {
        gpu::BindGroupLayoutEntry(0)
            .computeVisible()
            .texture(WGPUTextureSampleType_Uint, WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(1)
            .computeVisible()
            .storageTexture(WGPUStorageTextureAccess_WriteOnly, 
                           WGPUTextureFormat_R32Uint, 
                           WGPUTextureViewDimension_2D),
        gpu::BindGroupLayoutEntry(2)
            .computeVisible()
            .uniformBuffer(false, sizeof(MipParams))
    };
    
    bindGroupLayout_ = gpu::createBindGroupLayout(device, layoutEntries, "mip_generate_layout");
    if (!bindGroupLayout_) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create bind group layout");
        shutdown();
        return false;
    }
    
    // Create pipeline layout
    pipelineLayout_ = gpu::createPipelineLayout(device, 
        std::span<const WGPUBindGroupLayout>(&bindGroupLayout_, 1),
        "mip_generate_pipeline_layout");
    if (!pipelineLayout_) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create pipeline layout");
        shutdown();
        return false;
    }
    
    // Create compute pipeline
    WGPUComputePipelineDescriptor pipelineDesc{};
    pipelineDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(pipelineDesc, "mip_generate_pipeline");
    pipelineDesc.layout = pipelineLayout_;
    pipelineDesc.compute.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "cs_generate_mip");
    pipelineDesc.compute.constantCount = 0;
    pipelineDesc.compute.constants = nullptr;
    
    pipeline_ = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    if (!pipeline_) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create compute pipeline");
        shutdown();
        return false;
    }
    
    // Create params uniform buffer
    auto bufferDesc = gpu::BufferDesc::uniform(sizeof(MipParams), "mip_params");
    paramsBuffer_ = gpu::createBuffer(device, bufferDesc);
    if (!paramsBuffer_) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create params buffer");
        shutdown();
        return false;
    }
    
    LOG_DEBUG("MipGeneratorPipeline: Initialized successfully");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mip Generation
// ═══════════════════════════════════════════════════════════════════════════════

bool MipGeneratorPipeline::generateMipChain(WGPUDevice device, WGPUQueue queue,
                                             WGPUTexture texture, uint32_t mipLevelCount) {
    if (!isInitialized()) {
        LOG_ERROR("MipGeneratorPipeline: Not initialized");
        return false;
    }
    
    if (!texture || mipLevelCount < 2) {
        LOG_ERROR("MipGeneratorPipeline: Invalid texture or mip level count");
        return false;
    }
    
    // Get texture base dimensions
    uint32_t baseWidth = wgpuTextureGetWidth(texture);
    uint32_t baseHeight = wgpuTextureGetHeight(texture);
    
    // Create a single command encoder for all mip levels (batched submission)
    // This significantly reduces CPU overhead compared to submitting per-level
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPU_SET_LABEL(encoderDesc, "mip_generate_encoder_batched");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    if (!encoder) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create command encoder");
        return false;
    }
    
    // Track resources to release after submission
    std::vector<WGPUTextureView> viewsToRelease;
    std::vector<WGPUBindGroup> bindGroupsToRelease;
    
    // Generate each mip level from the previous one
    for (uint32_t level = 1; level < mipLevelCount; level++) {
        uint32_t srcLevel = level - 1;
        uint32_t dstLevel = level;
        
        // Create texture views for source and destination mip levels
        WGPUTextureView srcView = gpu::createMipView(texture, srcLevel, WGPUTextureFormat_R32Uint);
        if (!srcView) {
            LOG_ERROR("MipGeneratorPipeline: Failed to create source mip view (level {})", srcLevel);
            // Release already-created resources
            for (auto view : viewsToRelease) wgpuTextureViewRelease(view);
            for (auto bg : bindGroupsToRelease) wgpuBindGroupRelease(bg);
            wgpuCommandEncoderRelease(encoder);
            return false;
        }
        viewsToRelease.push_back(srcView);
        
        WGPUTextureView dstView = gpu::createMipView(texture, dstLevel, WGPUTextureFormat_R32Uint);
        if (!dstView) {
            LOG_ERROR("MipGeneratorPipeline: Failed to create destination mip view (level {})", dstLevel);
            for (auto view : viewsToRelease) wgpuTextureViewRelease(view);
            for (auto bg : bindGroupsToRelease) wgpuBindGroupRelease(bg);
            wgpuCommandEncoderRelease(encoder);
            return false;
        }
        viewsToRelease.push_back(dstView);
        
        // Calculate dimensions for this level
        auto [srcWidth, srcHeight] = getMipDimensions(baseWidth, baseHeight, srcLevel);
        auto [dstWidth, dstHeight] = getMipDimensions(baseWidth, baseHeight, dstLevel);
        
        // Update params buffer
        MipParams params{
            .srcWidth = srcWidth,
            .srcHeight = srcHeight,
            .dstWidth = dstWidth,
            .dstHeight = dstHeight
        };
        gpu::writeBuffer(queue, paramsBuffer_, 0, params);
        
        // Create bind group for this pass
        std::array<gpu::BindGroupEntry, 3> entries = {
            gpu::BindGroupEntry(0).textureView(srcView),
            gpu::BindGroupEntry(1).textureView(dstView),
            gpu::BindGroupEntry(2).buffer(paramsBuffer_)
        };
        
        WGPUBindGroup bindGroup = gpu::createBindGroup(device, bindGroupLayout_, entries, "mip_generate_bind_group");
        if (!bindGroup) {
            LOG_ERROR("MipGeneratorPipeline: Failed to create bind group for level {}", level);
            for (auto view : viewsToRelease) wgpuTextureViewRelease(view);
            for (auto bg : bindGroupsToRelease) wgpuBindGroupRelease(bg);
            wgpuCommandEncoderRelease(encoder);
            return false;
        }
        bindGroupsToRelease.push_back(bindGroup);
        
        // Create compute pass for this mip level
        WGPUComputePassDescriptor passDesc{};
        WGPU_SET_LABEL(passDesc, "mip_generate_pass");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        
        wgpuComputePassEncoderSetPipeline(pass, pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        
        // Dispatch workgroups
        uint32_t dispatchX = calculateDispatchX(dstWidth);
        uint32_t dispatchY = calculateDispatchY(dstHeight);
        wgpuComputePassEncoderDispatchWorkgroups(pass, dispatchX, dispatchY, 1);
        
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }
    
    // Submit all mip generation work in a single command buffer
    WGPUCommandBufferDescriptor cmdDesc{};
    WGPU_SET_LABEL(cmdDesc, "mip_generate_commands_batched");
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);
    
    // Release all resources
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    for (auto view : viewsToRelease) wgpuTextureViewRelease(view);
    for (auto bg : bindGroupsToRelease) wgpuBindGroupRelease(bg);
    
    LOG_DEBUG("MipGeneratorPipeline: Generated {} mip levels (batched submission)", mipLevelCount - 1);
    return true;
}

bool MipGeneratorPipeline::generateSingleMip(WGPUDevice device, WGPUQueue queue,
                                              WGPUTexture texture,
                                              uint32_t srcMipLevel, uint32_t dstMipLevel) {
    if (!isInitialized()) {
        LOG_ERROR("MipGeneratorPipeline: Not initialized");
        return false;
    }
    
    if (!device || !queue || !texture) {
        LOG_ERROR("MipGeneratorPipeline: Invalid device, queue, or texture");
        return false;
    }
    
    // Get texture dimensions at base level
    // Note: We need to compute the dimensions at each mip level
    // WebGPU doesn't provide a direct API to query texture dimensions,
    // so we need the caller to track base dimensions
    // For now, we'll create views and let the shader handle bounds
    
    // Create texture views for source and destination mip levels
    // NOTE: This pipeline requires R32Uint textures because r16uint is NOT a valid
    // storage texture format in WebGPU 1.0. The source texture is sampled as uint,
    // and the destination is written as r32uint storage.
    WGPUTextureView srcView = gpu::createMipView(texture, srcMipLevel, WGPUTextureFormat_R32Uint);
    if (!srcView) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create source mip view (level {})", srcMipLevel);
        return false;
    }
    
    WGPUTextureView dstView = gpu::createMipView(texture, dstMipLevel, WGPUTextureFormat_R32Uint);
    if (!dstView) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create destination mip view (level {})", dstMipLevel);
        wgpuTextureViewRelease(srcView);
        return false;
    }
    
    // Get texture dimensions (need to query from the texture)
    // Note: wgpu-native and Dawn have different APIs for this
    // We'll use wgpuTextureGetWidth/Height
    uint32_t baseWidth = wgpuTextureGetWidth(texture);
    uint32_t baseHeight = wgpuTextureGetHeight(texture);
    
    auto [srcWidth, srcHeight] = getMipDimensions(baseWidth, baseHeight, srcMipLevel);
    auto [dstWidth, dstHeight] = getMipDimensions(baseWidth, baseHeight, dstMipLevel);
    
    // Update params buffer
    MipParams params{
        .srcWidth = srcWidth,
        .srcHeight = srcHeight,
        .dstWidth = dstWidth,
        .dstHeight = dstHeight
    };
    gpu::writeBuffer(queue, paramsBuffer_, 0, params);
    
    // Create bind group for this pass
    std::array<gpu::BindGroupEntry, 3> entries = {
        gpu::BindGroupEntry(0).textureView(srcView),
        gpu::BindGroupEntry(1).textureView(dstView),
        gpu::BindGroupEntry(2).buffer(paramsBuffer_)
    };
    
    WGPUBindGroup bindGroup = gpu::createBindGroup(device, bindGroupLayout_, entries, "mip_generate_bind_group");
    if (!bindGroup) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create bind group");
        wgpuTextureViewRelease(srcView);
        wgpuTextureViewRelease(dstView);
        return false;
    }
    
    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc{};
    WGPU_SET_LABEL(encoderDesc, "mip_generate_encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    if (!encoder) {
        LOG_ERROR("MipGeneratorPipeline: Failed to create command encoder");
        wgpuBindGroupRelease(bindGroup);
        wgpuTextureViewRelease(srcView);
        wgpuTextureViewRelease(dstView);
        return false;
    }
    
    // Create compute pass
    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "mip_generate_pass");
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    
    wgpuComputePassEncoderSetPipeline(pass, pipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    
    // Dispatch workgroups
    uint32_t dispatchX = calculateDispatchX(dstWidth);
    uint32_t dispatchY = calculateDispatchY(dstHeight);
    wgpuComputePassEncoderDispatchWorkgroups(pass, dispatchX, dispatchY, 1);
    
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    
    // Submit command buffer
    WGPUCommandBufferDescriptor cmdDesc{};
    WGPU_SET_LABEL(cmdDesc, "mip_generate_commands");
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &commands);
    
    // Release resources
    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(srcView);
    wgpuTextureViewRelease(dstView);
    
    return true;
}

WGPUBindGroup MipGeneratorPipeline::createMipBindGroup(WGPUDevice device,
                                                        WGPUTextureView srcView,
                                                        WGPUTextureView dstView,
                                                        uint32_t /*srcWidth*/, uint32_t /*srcHeight*/,
                                                        uint32_t /*dstWidth*/, uint32_t /*dstHeight*/) {
    // This helper is kept for potential future use with batched operations
    std::array<gpu::BindGroupEntry, 3> entries = {
        gpu::BindGroupEntry(0).textureView(srcView),
        gpu::BindGroupEntry(1).textureView(dstView),
        gpu::BindGroupEntry(2).buffer(paramsBuffer_)
    };
    
    return gpu::createBindGroup(device, bindGroupLayout_, entries, "mip_generate_bind_group");
}

} // namespace voxy::render

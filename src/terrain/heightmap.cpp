// ═══════════════════════════════════════════════════════════════════════════════
// heightmap.cpp - Heightmap Loading and GPU Texture Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/heightmap.hpp"
#include "terrain/mip_generator.hpp"
#include "core/log.hpp"
#include "gpu/resources.hpp"

// Only include mip pipeline when WebGPU is available
#if defined(VOXY_NATIVE) || defined(VOXY_WASM)
    #include "render/mip_pipeline.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

#include "terrain/compression.hpp"

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// Error Handling
// ═══════════════════════════════════════════════════════════════════════════════

std::string_view errorToString(HeightmapError error) noexcept {
    switch (error) {
        case HeightmapError::None:
            return "No error";
        case HeightmapError::FileNotFound:
            return "File not found";
        case HeightmapError::ReadError:
            return "Failed to read file";
        case HeightmapError::InvalidFormat:
            return "Invalid or unrecognized format";
        case HeightmapError::InvalidDimensions:
            return "Invalid dimensions";
        case HeightmapError::DecodeFailed:
            return "Image decoding failed";
        case HeightmapError::Not16Bit:
            return "PNG is not 16-bit depth";
        case HeightmapError::TextureCreationFailed:
            return "GPU texture creation failed";
        case HeightmapError::UploadFailed:
            return "GPU texture upload failed";
        case HeightmapError::ExrDecodeFailed:
            return "EXR decoding failed";
    }
    return "Unknown error";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Heightmap Implementation
// ═══════════════════════════════════════════════════════════════════════════════

Heightmap::~Heightmap() {
    release();
}

Heightmap::Heightmap(Heightmap&& other) noexcept
    : data_(std::move(other.data_))
    , width_(other.width_)
    , height_(other.height_)
    , loadTimeMs_(other.loadTimeMs_)
    , texture_(other.texture_)
    , textureView_(other.textureView_)
    , mipLevelCount_(other.mipLevelCount_)
    , cachedMinMax_(std::move(other.cachedMinMax_))
{
    other.width_ = 0;
    other.height_ = 0;
    other.loadTimeMs_ = 0.0;
    other.texture_ = nullptr;
    other.textureView_ = nullptr;
    other.mipLevelCount_ = 0;
}

Heightmap& Heightmap::operator=(Heightmap&& other) noexcept {
    if (this != &other) {
        release();
        
        data_ = std::move(other.data_);
        width_ = other.width_;
        height_ = other.height_;
        loadTimeMs_ = other.loadTimeMs_;
        texture_ = other.texture_;
        textureView_ = other.textureView_;
        mipLevelCount_ = other.mipLevelCount_;
        cachedMinMax_ = std::move(other.cachedMinMax_);
        
        other.width_ = 0;
        other.height_ = 0;
        other.loadTimeMs_ = 0.0;
        other.texture_ = nullptr;
        other.textureView_ = nullptr;
        other.mipLevelCount_ = 0;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// File Reading Helper
// ─────────────────────────────────────────────────────────────────────────────

Result<std::vector<std::byte>, HeightmapError>
Heightmap::readFileToMemory(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        LOG_ERROR("Heightmap file not found: {}", path.string());
        return HeightmapError::FileNotFound;
    }
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open heightmap file: {}", path.string());
        return HeightmapError::ReadError;
    }
    
    const auto fileSize = file.tellg();
    if (fileSize <= 0) {
        LOG_ERROR("Heightmap file is empty or unreadable: {}", path.string());
        return HeightmapError::ReadError;
    }
    
    file.seekg(0, std::ios::beg);
    
    std::vector<std::byte> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        LOG_ERROR("Failed to read heightmap file: {}", path.string());
        return HeightmapError::ReadError;
    }
    
    return buffer;
}

// ─────────────────────────────────────────────────────────────────────────────
// Loading Implementation
// ─────────────────────────────────────────────────────────────────────────────

VoidResult Heightmap::loadFromFile(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    
    if (ext == ".ldh") {
        return loadLdh(path);
    }
    else {
        LOG_ERROR("Unsupported heightmap format: {}. Only .ldh is supported.", ext);
        return HeightmapError::InvalidFormat;
    }
}

VoidResult Heightmap::loadLdh(const std::filesystem::path& path) {
    auto result = decompressFromFile(path);
    if (!result) {
        if (result.error() == CompressionError::FileNotFound) {
            return HeightmapError::FileNotFound;
        }
        LOG_ERROR("Failed to load LDH file: {}", errorToString(result.error()));
        return HeightmapError::ReadError; // Map compression error to heightmap error
    }
    
    auto& decompressed = result.value();
    
    // Release previous data
    release();
    
    // Copy data
    width_ = decompressed.width;
    height_ = decompressed.height;
    data_ = std::move(decompressed.data);
    loadTimeMs_ = decompressed.stats.totalTimeMs;
    
    LOG_INFO("Loaded LDH heightmap: {}x{} ({:.2f} MB, {:.2f} ms)",
             width_, height_,
             static_cast<double>(getSizeBytes()) / (1024.0 * 1024.0),
             loadTimeMs_);
             
    return VoidResult();
}



VoidResult Heightmap::loadRawFromMemory(std::span<const std::byte> data,
                                         uint32_t width, uint32_t height) {
    const auto startTime = std::chrono::high_resolution_clock::now();
    
    if (width == 0 || height == 0) {
        LOG_ERROR("Invalid heightmap dimensions: {}x{}", width, height);
        return HeightmapError::InvalidDimensions;
    }
    
    const size_t expectedSize = static_cast<size_t>(width) * height * sizeof(uint16_t);
    if (data.size() != expectedSize) {
        LOG_ERROR("RAW heightmap size mismatch: expected {} bytes for {}x{}, got {} bytes",
                  expectedSize, width, height, data.size());
        return HeightmapError::InvalidDimensions;
    }
    
    // Release previous data
    release();
    
    // Copy data
    width_ = width;
    height_ = height;
    data_.resize(static_cast<size_t>(width) * height);
    std::memcpy(data_.data(), data.data(), data.size());
    
    const auto endTime = std::chrono::high_resolution_clock::now();
    loadTimeMs_ = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    LOG_INFO("Loaded RAW heightmap: {}x{} ({:.2f} MB, {:.2f} ms)",
             width_, height_, 
             static_cast<double>(getSizeBytes()) / (1024.0 * 1024.0),
             loadTimeMs_);
    
    return VoidResult();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resizing / Upscaling
// ─────────────────────────────────────────────────────────────────────────────

VoidResult Heightmap::resize(uint32_t targetWidth, uint32_t targetHeight) {
    if (!isLoaded()) {
        LOG_ERROR("Cannot resize: no heightmap loaded");
        return HeightmapError::InvalidDimensions;
    }
    
    // Auto-calculate target dimensions if not specified
    if (targetWidth == 0) {
        targetWidth = nextPowerOfTwo(width_);
    }
    if (targetHeight == 0) {
        targetHeight = nextPowerOfTwo(height_);
    }
    
    // Skip if already the right size
    if (targetWidth == width_ && targetHeight == height_) {
        LOG_DEBUG("Heightmap already {}x{}, no resize needed", width_, height_);
        return VoidResult();
    }
    
    const auto startTime = std::chrono::high_resolution_clock::now();
    
    LOG_INFO("Resizing heightmap from {}x{} to {}x{} (bilinear interpolation)",
             width_, height_, targetWidth, targetHeight);
    
    // Allocate new buffer
    std::vector<uint16_t> newData(static_cast<size_t>(targetWidth) * targetHeight);
    
    // Bilinear interpolation
    // Avoid division by zero if target dimension is 1
    const float scaleX = (targetWidth > 1) 
        ? static_cast<float>(width_ - 1) / static_cast<float>(targetWidth - 1)
        : 0.0f;
    const float scaleY = (targetHeight > 1) 
        ? static_cast<float>(height_ - 1) / static_cast<float>(targetHeight - 1)
        : 0.0f;
    
    for (uint32_t y = 0; y < targetHeight; y++) {
        for (uint32_t x = 0; x < targetWidth; x++) {
            // Source coordinates
            const float srcX = static_cast<float>(x) * scaleX;
            const float srcY = static_cast<float>(y) * scaleY;
            
            // Sample with bilinear interpolation
            const float value = sampleBilinear(srcX, srcY);
            newData[y * targetWidth + x] = static_cast<uint16_t>(
                std::clamp(value, 0.0f, 65535.0f));
        }
    }
    
    // Replace data
    data_ = std::move(newData);
    width_ = targetWidth;
    height_ = targetHeight;
    cachedMinMax_.reset();  // Invalidate cache
    
    const auto endTime = std::chrono::high_resolution_clock::now();
    const double resizeTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    
    LOG_INFO("Heightmap resized to {}x{} ({:.2f} MB, {:.2f} ms)",
             width_, height_,
             static_cast<double>(getSizeBytes()) / (1024.0 * 1024.0),
             resizeTimeMs);
    
    return VoidResult();
}

VoidResult Heightmap::resizeToPowerOfTwo() {
    return resize(0, 0);  // Auto-calculate to next power of 2
}

// ─────────────────────────────────────────────────────────────────────────────
// CPU Data Access
// ─────────────────────────────────────────────────────────────────────────────

uint16_t Heightmap::sample(uint32_t x, uint32_t y) const noexcept {
    if (data_.empty()) return 0;
    x = std::min(x, width_ - 1);
    y = std::min(y, height_ - 1);
    return data_[y * width_ + x];
}

float Heightmap::sampleBilinear(float x, float y) const noexcept {
    if (data_.empty()) return 0.0f;
    
    // Clamp coordinates
    x = std::clamp(x, 0.0f, static_cast<float>(width_ - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height_ - 1));
    
    // Get integer and fractional parts
    const uint32_t x0 = static_cast<uint32_t>(x);
    const uint32_t y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, width_ - 1);
    const uint32_t y1 = std::min(y0 + 1, height_ - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    
    // Sample four corners
    const float s00 = static_cast<float>(data_[y0 * width_ + x0]);
    const float s10 = static_cast<float>(data_[y0 * width_ + x1]);
    const float s01 = static_cast<float>(data_[y1 * width_ + x0]);
    const float s11 = static_cast<float>(data_[y1 * width_ + x1]);
    
    // Bilinear interpolation
    const float s0 = s00 * (1.0f - fx) + s10 * fx;
    const float s1 = s01 * (1.0f - fx) + s11 * fx;
    return s0 * (1.0f - fy) + s1 * fy;
}

float Heightmap::sampleNormalized(uint32_t x, uint32_t y) const noexcept {
    return static_cast<float>(sample(x, y)) / 65535.0f;
}

std::pair<uint16_t, uint16_t> Heightmap::getMinMax() const noexcept {
    if (cachedMinMax_) {
        return *cachedMinMax_;
    }
    
    if (data_.empty()) {
        cachedMinMax_ = {0, 0};
        return *cachedMinMax_;
    }
    
    auto [minIt, maxIt] = std::minmax_element(data_.begin(), data_.end());
    cachedMinMax_ = {*minIt, *maxIt};
    return *cachedMinMax_;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Texture
// ─────────────────────────────────────────────────────────────────────────────

VoidResult Heightmap::uploadToGPU(WGPUDevice device, WGPUQueue queue, 
                                   std::string_view label) {
    if (!isLoaded()) {
        LOG_ERROR("Cannot upload heightmap to GPU: no data loaded");
        return HeightmapError::UploadFailed;
    }
    
    if (!device || !queue) {
        LOG_ERROR("Cannot upload heightmap to GPU: invalid device or queue");
        return HeightmapError::UploadFailed;
    }
    
    // Release existing GPU resources
    releaseGPU();
    
    // Create texture descriptor (single mip level)
    auto desc = gpu::TextureDesc::tex2D(
        width_, height_,
        WGPUTextureFormat_R16Uint,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        label
    );
    
    // Create texture with data
    const uint32_t bytesPerRow = width_ * sizeof(uint16_t);
    texture_ = gpu::createTextureWithData(device, queue, desc, getDataBytes(), bytesPerRow);
    
    if (!texture_) {
        LOG_ERROR("Failed to create heightmap GPU texture");
        return HeightmapError::TextureCreationFailed;
    }
    
    mipLevelCount_ = 1;  // Single mip level
    LOG_DEBUG("Uploaded heightmap to GPU: {}x{} (R16Uint, 1 mip level)", width_, height_);
    return VoidResult();
}

WGPUTextureView Heightmap::getTextureView() {
    if (!texture_) return nullptr;
    
    if (!textureView_) {
        // Create a view that includes all mip levels
        gpu::TextureViewDesc viewDesc{};
        viewDesc.label = "heightmap_view";
        viewDesc.format = WGPUTextureFormat_R16Uint;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = mipLevelCount_ > 0 ? mipLevelCount_ : 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        
        textureView_ = gpu::createTextureView(texture_, viewDesc);
    }
    
    return textureView_;
}

void Heightmap::releaseGPU() {
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
    
    mipLevelCount_ = 0;
}

void Heightmap::release() {
    releaseGPU();
    
    data_.clear();
    data_.shrink_to_fit();
    width_ = 0;
    height_ = 0;
    loadTimeMs_ = 0.0;
    cachedMinMax_.reset();
}

WGPUTextureView Heightmap::getMipView(uint32_t level) {
    if (!texture_ || level >= mipLevelCount_) {
        return nullptr;
    }
    
    return gpu::createMipView(texture_, level, WGPUTextureFormat_R16Uint);
}

VoidResult Heightmap::uploadToGPUWithMips(WGPUDevice device, WGPUQueue queue,
                                           bool useGPUMips,
                                           const std::filesystem::path& shaderPath,
                                           std::string_view label) {
    if (!isLoaded()) {
        LOG_ERROR("Cannot upload heightmap to GPU: no data loaded");
        return HeightmapError::UploadFailed;
    }
    
    if (!device || !queue) {
        LOG_ERROR("Cannot upload heightmap to GPU: invalid device or queue");
        return HeightmapError::UploadFailed;
    }
    
    // Release existing GPU resources
    releaseGPU();
    
    // Calculate mip levels
    mipLevelCount_ = calculateMipLevels(width_, height_);
    
    // Create texture descriptor with mip chain
    // Note: For GPU mip generation, we need StorageBinding usage (not supported on all devices)
    WGPUTextureDescriptor texDesc{};
    texDesc.nextInChain = nullptr;
    WGPU_SET_LABEL(texDesc, label.data());
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size.width = width_;
    texDesc.size.height = height_;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.format = WGPUTextureFormat_R16Uint;
    texDesc.mipLevelCount = mipLevelCount_;
    texDesc.sampleCount = 1;
    texDesc.viewFormatCount = 0;
    texDesc.viewFormats = nullptr;
    
    // GPU mip generation is NOT supported for R16Uint textures because:
    // 1. r16uint is NOT a valid storage texture format in WebGPU 1.0
    // 2. Only 32-bit formats (r32uint, r32sint, r32float, etc.) are supported
    // We always fall back to CPU mip generation for R16Uint heightmaps.
    // For GPU mip generation, the texture would need to be R32Uint format.
    bool canUseGPUMips = false;
    if (useGPUMips) {
        // Note: R16Uint cannot be used as a storage texture in WebGPU.
        // The shader uses r32uint for storage textures, but we can't use that
        // with our R16Uint heightmap without format conversion.
        LOG_WARN("GPU mip generation requested but R16Uint is not a valid storage texture format in WebGPU 1.0. "
                 "Falling back to CPU mip generation.");
    }
    
    // Create texture without storage binding if needed
    if (!texture_) {
        texture_ = wgpuDeviceCreateTexture(device, &texDesc);
    }
    
    if (!texture_) {
        LOG_ERROR("Failed to create mipped heightmap texture");
        mipLevelCount_ = 0;
        return HeightmapError::TextureCreationFailed;
    }
    
    // Upload base level (level 0)
    gpu::writeTexture(queue, texture_, getDataBytes(), width_, height_, 
                      width_ * sizeof(uint16_t), 0);
    
    // Generate mip levels
    bool mipsGenerated = false;
    
#if defined(VOXY_NATIVE) || defined(VOXY_WASM)
    if (canUseGPUMips && !shaderPath.empty()) {
        // Try GPU mip generation
        render::MipGeneratorPipeline mipPipeline;
        if (mipPipeline.init(device, shaderPath)) {
            mipsGenerated = mipPipeline.generateMipChain(device, queue, texture_, mipLevelCount_);
            if (mipsGenerated) {
                LOG_DEBUG("Generated {} mip levels on GPU for {}x{} heightmap",
                          mipLevelCount_ - 1, width_, height_);
            }
        }
    }
#endif
    
    if (!mipsGenerated) {
        // Fall back to CPU mip generation
        if (!uploadMipsFromCPU(device, queue)) {
            LOG_ERROR("Failed to generate mip chain (CPU fallback)");
            // Note: Texture is still valid, just without full mip chain
        } else {
            LOG_DEBUG("Generated {} mip levels on CPU for {}x{} heightmap",
                      mipLevelCount_ - 1, width_, height_);
        }
    }
    
    LOG_INFO("Uploaded heightmap to GPU with mips: {}x{} ({} levels, {:.2f} MB total)",
             width_, height_, mipLevelCount_,
             static_cast<double>(getSizeBytes() * 4 / 3) / (1024.0 * 1024.0));
    
    return VoidResult();
}

bool Heightmap::uploadMipsFromCPU(WGPUDevice /*device*/, WGPUQueue queue) {
    if (!texture_) {
        LOG_WARN("uploadMipsFromCPU: No texture to upload mips to");
        return false;  // This is an error condition - texture should exist
    }
    
    if (mipLevelCount_ < 2) {
        // Single mip level (or none) - no additional mips to generate
        LOG_DEBUG("uploadMipsFromCPU: Texture has {} mip levels, no additional mips to generate", 
                  mipLevelCount_);
        return true;
    }
    
    // The terrain ray-caster shader relies on mips existing up to level 7 for
    // hierarchical traversal. Warn if we have fewer mip levels than expected.
    constexpr uint32_t RAYCAST_EXPECTED_MIP_LEVELS = 8;  // levels 0-7
    if (mipLevelCount_ < RAYCAST_EXPECTED_MIP_LEVELS) {
        LOG_WARN("Heightmap has {} mip levels, but ray-caster expects {}. "
                 "This may affect hierarchical traversal performance for smaller heightmaps.",
                 mipLevelCount_, RAYCAST_EXPECTED_MIP_LEVELS);
    }
    
    // Generate mip chain using CPU
    MaxHeightMipChain mipChain;
    if (!mipChain.generateWithoutBase(data_, width_, height_)) {
        LOG_ERROR("Failed to generate CPU mip chain");
        return false;
    }
    
    // Upload each mip level
    for (uint32_t level = 1; level < mipLevelCount_; level++) {
        const MipLevel* mipLevel = mipChain.getLevel(level);
        if (!mipLevel || !mipLevel->isValid()) {
            LOG_ERROR("Invalid mip level {}", level);
            return false;
        }
        
        // Convert to byte span for upload
        std::span<const std::byte> levelBytes(
            reinterpret_cast<const std::byte*>(mipLevel->data.data()),
            mipLevel->sizeBytes()
        );
        
        gpu::writeTexture(queue, texture_, levelBytes,
                          mipLevel->width, mipLevel->height,
                          mipLevel->width * sizeof(uint16_t), level);
    }
    
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static Factory Functions
// ─────────────────────────────────────────────────────────────────────────────

Result<LoadResult, HeightmapError>
Heightmap::load(const std::filesystem::path& path) {
    Heightmap hm;
    auto result = hm.loadFromFile(path);
    
    if (!result) {
        return result.error();
    }
    
    LoadResult loadResult;
    loadResult.data = std::move(hm.data_);
    loadResult.width = hm.width_;
    loadResult.height = hm.height_;
    loadResult.loadTimeMs = hm.loadTimeMs_;
    
    return loadResult;
}

Heightmap Heightmap::createFlat(uint32_t width, uint32_t height, uint16_t value) {
    Heightmap hm;
    hm.width_ = width;
    hm.height_ = height;
    hm.data_.resize(static_cast<size_t>(width) * height, value);
    return hm;
}

Heightmap Heightmap::createFromData(std::vector<uint16_t>&& data,
                                     uint32_t width, uint32_t height) {
    Heightmap hm;
    hm.data_ = std::move(data);
    hm.width_ = width;
    hm.height_ = height;
    return hm;
}

} // namespace voxy::terrain


// ═══════════════════════════════════════════════════════════════════════════════
// heightmap.cpp - Heightmap Loading and GPU Texture Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/heightmap.hpp"
#include "terrain/mip_generator.hpp"
#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

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
    if (fileSize <= 0
        || fileSize > std::numeric_limits<std::streamsize>::max()) {
        LOG_ERROR("Heightmap file is empty, unreadable, or too large: {}",
                  path.string());
        return HeightmapError::ReadError;
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::byte> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(fileSize))) {
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

    size_t sampleCount = 0;
    size_t expectedSize = 0;
    if (!tryCalculateHeightmapLayout(
            width, height, sampleCount, expectedSize)
        || sampleCount > data_.max_size()) {
        LOG_ERROR("Invalid heightmap dimensions: {}x{}", width, height);
        return HeightmapError::InvalidDimensions;
    }

    if (data.size() != expectedSize) {
        LOG_ERROR("RAW heightmap size mismatch: expected {} bytes for {}x{}, got {} bytes",
                  expectedSize, width, height, data.size());
        return HeightmapError::InvalidDimensions;
    }

    std::vector<uint16_t> nextData(sampleCount);
    std::memcpy(nextData.data(), data.data(), data.size());

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double nextLoadTimeMs =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Commit only after validation, allocation, and copy all succeeded.
    release();
    width_ = width;
    height_ = height;
    data_ = std::move(nextData);
    loadTimeMs_ = nextLoadTimeMs;

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

    size_t targetSampleCount = 0;
    size_t targetByteCount = 0;
    if (!tryCalculateHeightmapLayout(
            targetWidth, targetHeight, targetSampleCount, targetByteCount)
        || targetSampleCount > data_.max_size()) {
        LOG_ERROR("Cannot resize heightmap to invalid dimensions: {}x{}",
                  targetWidth, targetHeight);
        return HeightmapError::InvalidDimensions;
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
    std::vector<uint16_t> newData(targetSampleCount);

    struct AxisSample {
        uint32_t i0 = 0;
        uint32_t i1 = 0;
        float f = 0.0f;
    };

    // Bilinear interpolation
    // Avoid division by zero if target dimension is 1
    const float scaleX = (targetWidth > 1)
        ? static_cast<float>(width_ - 1) / static_cast<float>(targetWidth - 1)
        : 0.0f;
    const float scaleY = (targetHeight > 1)
        ? static_cast<float>(height_ - 1) / static_cast<float>(targetHeight - 1)
        : 0.0f;

    std::vector<AxisSample> xSamples(targetWidth);
    for (uint32_t x = 0; x < targetWidth; ++x) {
        const float srcX = static_cast<float>(x) * scaleX;
        const uint32_t x0 = std::min(static_cast<uint32_t>(srcX), width_ - 1);
        xSamples[x] = AxisSample{
            .i0 = x0,
            .i1 = std::min(x0 + 1, width_ - 1),
            .f = srcX - static_cast<float>(x0)
        };
    }

    std::vector<AxisSample> ySamples(targetHeight);
    for (uint32_t y = 0; y < targetHeight; ++y) {
        const float srcY = static_cast<float>(y) * scaleY;
        const uint32_t y0 = std::min(static_cast<uint32_t>(srcY), height_ - 1);
        ySamples[y] = AxisSample{
            .i0 = y0,
            .i1 = std::min(y0 + 1, height_ - 1),
            .f = srcY - static_cast<float>(y0)
        };
    }

    for (uint32_t y = 0; y < targetHeight; ++y) {
        const AxisSample& ys = ySamples[y];
        const uint16_t* row0 = data_.data() + static_cast<size_t>(ys.i0) * width_;
        const uint16_t* row1 = data_.data() + static_cast<size_t>(ys.i1) * width_;
        uint16_t* dst = newData.data() + static_cast<size_t>(y) * targetWidth;

        for (uint32_t x = 0; x < targetWidth; ++x) {
            const AxisSample& xs = xSamples[x];
            const float s00 = static_cast<float>(row0[xs.i0]);
            const float s10 = static_cast<float>(row0[xs.i1]);
            const float s01 = static_cast<float>(row1[xs.i0]);
            const float s11 = static_cast<float>(row1[xs.i1]);
            const float s0 = s00 * (1.0f - xs.f) + s10 * xs.f;
            const float s1 = s01 * (1.0f - xs.f) + s11 * xs.f;
            const float value = s0 * (1.0f - ys.f) + s1 * ys.f;
            // Round to nearest: a plain truncating cast always rounds toward zero,
            // biasing resized heightmaps downward by ~0.5 units on average.
            dst[x] = static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 65535.0f)));
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
    return data_[static_cast<size_t>(y) * width_ + x];
}

float Heightmap::sampleBilinear(float x, float y) const noexcept {
    if (data_.empty() || !std::isfinite(x) || !std::isfinite(y)) {
        return 0.0f;
    }

    // Clamp coordinates
    const double sampleX = std::clamp(
        static_cast<double>(x), 0.0, static_cast<double>(width_ - 1));
    const double sampleY = std::clamp(
        static_cast<double>(y), 0.0, static_cast<double>(height_ - 1));

    // Get integer and fractional parts
    const uint32_t x0 = static_cast<uint32_t>(sampleX);
    const uint32_t y0 = static_cast<uint32_t>(sampleY);
    const uint32_t x1 = x0 < width_ - 1 ? x0 + 1 : x0;
    const uint32_t y1 = y0 < height_ - 1 ? y0 + 1 : y0;
    const float fx = static_cast<float>(
        sampleX - static_cast<double>(x0));
    const float fy = static_cast<float>(
        sampleY - static_cast<double>(y0));

    // Sample four corners
    const size_t row0 = static_cast<size_t>(y0) * width_;
    const size_t row1 = static_cast<size_t>(y1) * width_;
    const float s00 = static_cast<float>(data_[row0 + x0]);
    const float s10 = static_cast<float>(data_[row0 + x1]);
    const float s01 = static_cast<float>(data_[row1 + x0]);
    const float s11 = static_cast<float>(data_[row1 + x1]);

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
    if (width_ > std::numeric_limits<uint32_t>::max()
                     / sizeof(uint16_t)) {
        LOG_ERROR("Heightmap row is too wide for WebGPU upload");
        return HeightmapError::UploadFailed;
    }

    // Create texture descriptor (single mip level)
    auto desc = gpu::TextureDesc::tex2D(
        width_, height_,
        WGPUTextureFormat_R16Uint,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        label
    );

    // Create texture with data
    const uint32_t bytesPerRow = width_ * sizeof(uint16_t);
    WGPUTexture nextTexture = gpu::createTextureWithData(
        device, queue, desc, getDataBytes(), bytesPerRow);

    if (!nextTexture) {
        LOG_ERROR("Failed to create heightmap GPU texture");
        return HeightmapError::TextureCreationFailed;
    }

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "heightmap_view";
    viewDesc.format = WGPUTextureFormat_R16Uint;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    WGPUTextureView nextView = gpu::createTextureView(
        nextTexture, viewDesc);
    if (!nextView) {
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return HeightmapError::TextureCreationFailed;
    }

    releaseGPU();
    texture_ = nextTexture;
    textureView_ = nextView;
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
    if (width_ > std::numeric_limits<uint32_t>::max()
                     / sizeof(uint16_t)) {
        LOG_ERROR("Heightmap row is too wide for WebGPU upload");
        return HeightmapError::UploadFailed;
    }

    const uint32_t nextMipLevelCount = calculateMipLevels(width_, height_);

    // Create texture descriptor with mip chain
    WGPUTextureDescriptor texDesc{};
    texDesc.nextInChain = nullptr;
    const std::string labelString(label);
    WGPU_SET_LABEL(texDesc, labelString.c_str());
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size.width = width_;
    texDesc.size.height = height_;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.format = WGPUTextureFormat_R16Uint;
    texDesc.mipLevelCount = nextMipLevelCount;
    texDesc.sampleCount = 1;
    texDesc.viewFormatCount = 0;
    texDesc.viewFormats = nullptr;

    if (useGPUMips) {
        LOG_WARN("GPU mip generation requested but R16Uint is not a valid storage texture format in WebGPU 1.0. "
                 "Falling back to CPU mip generation.");
    }
    static_cast<void>(shaderPath);

    WGPUTexture nextTexture = wgpuDeviceCreateTexture(device, &texDesc);

    if (!nextTexture) {
        LOG_ERROR("Failed to create mipped heightmap texture");
        return HeightmapError::TextureCreationFailed;
    }

    // Upload base level (level 0)
    if (!gpu::writeTexture(
            queue, nextTexture, getDataBytes(), width_, height_,
            width_ * sizeof(uint16_t), 0)) {
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return HeightmapError::UploadFailed;
    }

    if (!uploadMipsFromCPU(queue, nextTexture, nextMipLevelCount)) {
        LOG_ERROR("Failed to generate mip chain (CPU fallback)");
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return HeightmapError::UploadFailed;
    }
    LOG_DEBUG("Generated {} mip levels on CPU for {}x{} heightmap",
              nextMipLevelCount - 1, width_, height_);

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "heightmap_view";
    viewDesc.format = WGPUTextureFormat_R16Uint;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = nextMipLevelCount;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    WGPUTextureView nextView = gpu::createTextureView(
        nextTexture, viewDesc);
    if (!nextView) {
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return HeightmapError::TextureCreationFailed;
    }

    releaseGPU();
    texture_ = nextTexture;
    textureView_ = nextView;
    mipLevelCount_ = nextMipLevelCount;

    LOG_INFO("Uploaded heightmap to GPU with mips: {}x{} ({} levels, {:.2f} MB total)",
             width_, height_, mipLevelCount_,
             static_cast<double>(getSizeBytes()) * (4.0 / 3.0)
                 / (1024.0 * 1024.0));

    return VoidResult();
}

bool Heightmap::uploadMipsFromCPU(
    WGPUQueue queue, WGPUTexture texture,
    uint32_t mipLevelCount) {
    if (!texture || !queue) {
        LOG_WARN("uploadMipsFromCPU: No texture to upload mips to");
        return false;  // This is an error condition - texture should exist
    }

    if (mipLevelCount < 2) {
        // Single mip level (or none) - no additional mips to generate
        LOG_DEBUG("uploadMipsFromCPU: Texture has {} mip levels, no additional mips to generate",
                  mipLevelCount);
        return true;
    }

    // The terrain ray-caster shader relies on mips existing up to level 7 for
    // hierarchical traversal. Warn if we have fewer mip levels than expected.
    constexpr uint32_t RAYCAST_EXPECTED_MIP_LEVELS = 8;  // levels 0-7
    if (mipLevelCount < RAYCAST_EXPECTED_MIP_LEVELS) {
        LOG_WARN("Heightmap has {} mip levels, but ray-caster expects {}. "
                 "This may affect hierarchical traversal performance for smaller heightmaps.",
                 mipLevelCount, RAYCAST_EXPECTED_MIP_LEVELS);
    }

    std::span<const uint16_t> previousData = data_;
    uint32_t previousWidth = width_;
    uint32_t previousHeight = height_;
    MipLevel currentLevel;

    for (uint32_t level = 1; level < mipLevelCount; level++) {
        MipLevel nextLevel = level == 1u
            ? generateFirstHeightfieldMipLevel(
                previousData, previousWidth, previousHeight)
            : generateNextMipLevel(
                previousData, previousWidth, previousHeight);
        if (!nextLevel.isValid()) {
            LOG_ERROR("Failed to generate CPU mip level {}", level);
            return false;
        }

        std::span<const std::byte> levelBytes(
            reinterpret_cast<const std::byte*>(nextLevel.data.data()),
            nextLevel.sizeBytes()
        );

        if (!gpu::writeTexture(
                queue, texture, levelBytes,
                nextLevel.width, nextLevel.height,
                nextLevel.width * sizeof(uint16_t), level)) {
            return false;
        }

        currentLevel = std::move(nextLevel);
        previousData = currentLevel.data;
        previousWidth = currentLevel.width;
        previousHeight = currentLevel.height;
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
    size_t sampleCount = 0;
    size_t byteCount = 0;
    if (!tryCalculateHeightmapLayout(
            width, height, sampleCount, byteCount)
        || sampleCount > hm.data_.max_size()) {
        LOG_ERROR("Cannot create flat heightmap with invalid dimensions: {}x{}",
                  width, height);
        return hm;
    }

    hm.width_ = width;
    hm.height_ = height;
    hm.data_.resize(sampleCount, value);
    return hm;
}

Heightmap Heightmap::createFromData(std::vector<uint16_t>&& data,
                                     uint32_t width, uint32_t height) {
    Heightmap hm;
    size_t expectedSize = 0;
    size_t expectedBytes = 0;
    if (!tryCalculateHeightmapLayout(
            width, height, expectedSize, expectedBytes)
        || expectedSize > data.max_size()
        || data.size() != expectedSize) {
        LOG_ERROR("Cannot create heightmap: expected {} samples for {}x{}, got {}",
                  expectedSize, width, height, data.size());
        return hm;
    }

    hm.data_ = std::move(data);
    hm.width_ = width;
    hm.height_ = height;
    return hm;
}

} // namespace voxy::terrain

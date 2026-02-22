// ═══════════════════════════════════════════════════════════════════════════════
// textures.cpp - Terrain Texture Loading and Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/textures.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

// stb_image for texture loading (implementation in core/stb_impl.cpp)
// Note: STBI_NO_STDIO is defined, so we must use stbi_load_from_memory
#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#endif

namespace voxy::terrain {

// ═══════════════════════════════════════════════════════════════════════════════
// TerrainTextures Implementation
// ═══════════════════════════════════════════════════════════════════════════════

TerrainTextures::~TerrainTextures() {
    shutdown();
}

TerrainTextures::TerrainTextures(TerrainTextures&& other) noexcept
    : device_(other.device_)
    , queue_(other.queue_)
    , albedoTexture_(other.albedoTexture_)
    , albedoView_(other.albedoView_)
    , albedoWidth_(other.albedoWidth_)
    , albedoHeight_(other.albedoHeight_)
    , lightmapTexture_(other.lightmapTexture_)
    , lightmapView_(other.lightmapView_)
    , lightmapWidth_(other.lightmapWidth_)
    , lightmapHeight_(other.lightmapHeight_)
    , sampler_(other.sampler_)
    , config_(other.config_)
{
    // Null out the source
    other.device_ = nullptr;
    other.queue_ = nullptr;
    other.albedoTexture_ = nullptr;
    other.albedoView_ = nullptr;
    other.lightmapTexture_ = nullptr;
    other.lightmapView_ = nullptr;
    other.sampler_ = nullptr;
}

TerrainTextures& TerrainTextures::operator=(TerrainTextures&& other) noexcept {
    if (this != &other) {
        shutdown();
        
        device_ = other.device_;
        queue_ = other.queue_;
        albedoTexture_ = other.albedoTexture_;
        albedoView_ = other.albedoView_;
        albedoWidth_ = other.albedoWidth_;
        albedoHeight_ = other.albedoHeight_;
        lightmapTexture_ = other.lightmapTexture_;
        lightmapView_ = other.lightmapView_;
        lightmapWidth_ = other.lightmapWidth_;
        lightmapHeight_ = other.lightmapHeight_;
        sampler_ = other.sampler_;
        config_ = other.config_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.albedoTexture_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapTexture_ = nullptr;
        other.lightmapView_ = nullptr;
        other.sampler_ = nullptr;
    }
    return *this;
}

void TerrainTextures::shutdown() {
    if (albedoView_) {
        wgpuTextureViewRelease(albedoView_);
        albedoView_ = nullptr;
    }
    if (albedoTexture_) {
        wgpuTextureRelease(albedoTexture_);
        albedoTexture_ = nullptr;
    }
    if (lightmapView_) {
        wgpuTextureViewRelease(lightmapView_);
        lightmapView_ = nullptr;
    }
    if (lightmapTexture_) {
        wgpuTextureRelease(lightmapTexture_);
        lightmapTexture_ = nullptr;
    }
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    
    device_ = nullptr;
    queue_ = nullptr;
    albedoWidth_ = 0;
    albedoHeight_ = 0;
    lightmapWidth_ = 0;
    lightmapHeight_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::init(WGPUDevice device, WGPUQueue queue,
                           const TerrainTextureConfig& config) {
    LOG_SCOPE("TerrainTextures::init");
    
    if (isInitialized()) {
        LOG_ERROR("TerrainTextures::init: already initialized");
        return false;
    }
    
    if (!device || !queue) {
        LOG_ERROR("TerrainTextures::init: device or queue is null");
        return false;
    }
    
    device_ = device;
    queue_ = queue;
    config_ = config;
    
    // Create sampler first
    if (!createSampler()) {
        LOG_ERROR("Failed to create sampler");
        shutdown();
        return false;
    }
    
    // Load or create albedo texture
    if (!config.albedoPath.empty() && std::filesystem::exists(config.albedoPath)) {
        if (!loadAlbedo(config.albedoPath)) {
            LOG_WARN("Failed to load albedo texture, creating placeholder");
            if (!createPlaceholderAlbedo(config.placeholderWidth, config.placeholderHeight)) {
                LOG_ERROR("Failed to create placeholder albedo texture");
                shutdown();
                return false;
            }
        }
    } else {
        if (!createPlaceholderAlbedo(config.placeholderWidth, config.placeholderHeight)) {
            LOG_ERROR("Failed to create placeholder albedo texture");
            shutdown();
            return false;
        }
    }
    
    // Load or create lightmap texture
    if (!config.lightmapPath.empty() && std::filesystem::exists(config.lightmapPath)) {
        if (!loadLightmap(config.lightmapPath)) {
            LOG_WARN("Failed to load lightmap texture, creating white lightmap");
            if (!createWhiteLightmap(config.placeholderWidth, config.placeholderHeight)) {
                LOG_ERROR("Failed to create white lightmap texture");
                shutdown();
                return false;
            }
        }
    } else {
        if (!createWhiteLightmap(config.placeholderWidth, config.placeholderHeight)) {
            LOG_ERROR("Failed to create white lightmap texture");
            shutdown();
            return false;
        }
    }
    
    LOG_INFO("TerrainTextures initialized: albedo {}x{}, lightmap {}x{}",
             albedoWidth_, albedoHeight_, lightmapWidth_, lightmapHeight_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::createSampler() {
    gpu::SamplerDesc samplerDesc = gpu::SamplerDesc::linear("terrain_sampler");
    sampler_ = gpu::createSampler(device_, samplerDesc);
    
    if (!sampler_) {
        LOG_ERROR("Failed to create terrain sampler");
        return false;
    }
    
    LOG_DEBUG("Created terrain sampler");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Loading
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::loadAlbedo(const std::filesystem::path& path) {
    LOG_SCOPE("TerrainTextures::loadAlbedo");
    
    // Read file into memory (STBI_NO_STDIO is defined)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open albedo texture file: {}", path.string());
        return false;
    }
    
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        LOG_ERROR("Failed to read albedo texture file: {}", path.string());
        return false;
    }
    file.close();
    
    int width, height, channels;
    uint8_t* data = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                           &width, &height, &channels, 4);
    
    if (!data) {
        LOG_ERROR("Failed to decode albedo texture: {}", path.string());
        return false;
    }
    
    LOG_DEBUG("Loaded albedo texture: {}x{} from {}", width, height, path.string());
    
    // Copy data to vector
    std::vector<uint8_t> pixels(data, data + (width * height * 4));
    stbi_image_free(data);
    
    return uploadAlbedoTexture(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool TerrainTextures::loadLightmap(const std::filesystem::path& path) {
    LOG_SCOPE("TerrainTextures::loadLightmap");
    
    // Read file into memory (STBI_NO_STDIO is defined)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open lightmap texture file: {}", path.string());
        return false;
    }
    
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize)) {
        LOG_ERROR("Failed to read lightmap texture file: {}", path.string());
        return false;
    }
    file.close();
    
    int width, height, channels;
    uint8_t* data = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()),
                                           &width, &height, &channels, 1);
    
    if (!data) {
        LOG_ERROR("Failed to decode lightmap texture: {}", path.string());
        return false;
    }
    
    LOG_DEBUG("Loaded lightmap texture: {}x{} from {}", width, height, path.string());
    
    // Copy data to vector
    std::vector<uint8_t> pixels(data, data + (width * height));
    stbi_image_free(data);
    
    return uploadLightmapTexture(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// Placeholder Texture Generation
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::createPlaceholderAlbedo(uint32_t width, uint32_t height) {
    LOG_SCOPE("TerrainTextures::createPlaceholderAlbedo");
    
    auto data = generateTerrainColorData(width, height);
    return uploadAlbedoTexture(data, width, height);
}

bool TerrainTextures::createWhiteLightmap(uint32_t width, uint32_t height) {
    LOG_SCOPE("TerrainTextures::createWhiteLightmap");
    
    auto data = generateWhiteLightmapData(width, height);
    return uploadLightmapTexture(data, width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Upload
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::uploadAlbedoTexture(const std::vector<uint8_t>& data,
                                           uint32_t width, uint32_t height) {
    // Release old texture if exists
    if (albedoView_) {
        wgpuTextureViewRelease(albedoView_);
        albedoView_ = nullptr;
    }
    if (albedoTexture_) {
        wgpuTextureRelease(albedoTexture_);
        albedoTexture_ = nullptr;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_albedo"
    );
    
    albedoTexture_ = gpu::createTexture(device_, texDesc);
    if (!albedoTexture_) {
        LOG_ERROR("Failed to create albedo texture");
        return false;
    }
    
    // Upload data
    uint32_t bytesPerRow = width * 4;
    gpu::writeTexture(queue_, albedoTexture_,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                      width, height, bytesPerRow);
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_albedo_view";
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    
    albedoView_ = gpu::createTextureView(albedoTexture_, viewDesc);
    if (!albedoView_) {
        LOG_ERROR("Failed to create albedo texture view");
        return false;
    }
    
    albedoWidth_ = width;
    albedoHeight_ = height;
    
    LOG_DEBUG("Created albedo texture: {}x{}", width, height);
    return true;
}

bool TerrainTextures::uploadLightmapTexture(const std::vector<uint8_t>& data,
                                             uint32_t width, uint32_t height) {
    // Release old texture if exists
    if (lightmapView_) {
        wgpuTextureViewRelease(lightmapView_);
        lightmapView_ = nullptr;
    }
    if (lightmapTexture_) {
        wgpuTextureRelease(lightmapTexture_);
        lightmapTexture_ = nullptr;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_R8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_lightmap"
    );
    
    lightmapTexture_ = gpu::createTexture(device_, texDesc);
    if (!lightmapTexture_) {
        LOG_ERROR("Failed to create lightmap texture");
        return false;
    }
    
    // Upload data
    uint32_t bytesPerRow = width;
    gpu::writeTexture(queue_, lightmapTexture_,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()),
                      width, height, bytesPerRow);
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_lightmap_view";
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    
    lightmapView_ = gpu::createTextureView(lightmapTexture_, viewDesc);
    if (!lightmapView_) {
        LOG_ERROR("Failed to create lightmap texture view");
        return false;
    }
    
    lightmapWidth_ = width;
    lightmapHeight_ = height;
    
    LOG_DEBUG("Created lightmap texture: {}x{}", width, height);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> generateTerrainColorData(uint32_t width, uint32_t height) {
    std::vector<uint8_t> data(width * height * 4);
    
    // Generate a procedural terrain color pattern for Canyon biome
    // Base colors for canyon (reddish/brownish rock)
    // Reddish-brown base
    const uint8_t baseR = 180;
    const uint8_t baseG = 110;
    const uint8_t baseB = 80;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            
            // Add some subtle variation based on position
            // Using a simple pseudo-random pattern based on coordinates
            float fx = static_cast<float>(x) / static_cast<float>(width);
            float fy = static_cast<float>(y) / static_cast<float>(height);
            
            // Simple procedural noise-like variation
            float noise = std::sin(fx * 25.0f) * std::cos(fy * 25.0f) * 0.5f + 0.5f;
            // Add some banding for strata effect
            float strata = std::sin(fy * 80.0f + noise * 5.0f) * 0.5f + 0.5f;

            float variation = noise * 0.15f + strata * 0.15f;
            
            // Apply variation to base colors
            float vr = std::clamp(1.0f + variation * 0.4f, 0.8f, 1.3f);
            float vg = std::clamp(1.0f + variation * 0.3f, 0.8f, 1.2f);
            float vb = std::clamp(1.0f + variation * 0.2f, 0.8f, 1.2f);
            
            data[idx + 0] = static_cast<uint8_t>(std::clamp(baseR * vr, 0.0f, 255.0f));
            data[idx + 1] = static_cast<uint8_t>(std::clamp(baseG * vg, 0.0f, 255.0f));
            data[idx + 2] = static_cast<uint8_t>(std::clamp(baseB * vb, 0.0f, 255.0f));
            data[idx + 3] = 255;  // Full alpha
        }
    }
    
    return data;
}

std::vector<uint8_t> generateWhiteLightmapData(uint32_t width, uint32_t height) {
    // Create a white lightmap (full light visibility everywhere)
    std::vector<uint8_t> data(width * height, 255);
    return data;
}

} // namespace voxy::terrain


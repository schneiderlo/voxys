// ═══════════════════════════════════════════════════════════════════════════════
// textures.cpp - Terrain Texture Loading and Management Implementation
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/textures.hpp"
#include "gpu/resources.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

// stb_image for texture loading (implementation in core/stb_impl.cpp)
// Note: STBI_NO_STDIO is defined, so we must use stbi_load_from_memory
#if __has_include(<stb_image.h>)
    #include <stb_image.h>
#endif

namespace voxy::terrain {

namespace {

constexpr uint32_t kMaximumTextureExtent = 8'192u;
constexpr std::streamoff kMaximumEncodedTextureBytes =
    512ll * 1024ll * 1024ll;

bool validImageLayout(uint32_t width, uint32_t height,
                      uint32_t bytesPerPixel, size_t& byteCount,
                      uint32_t& bytesPerRow) noexcept {
    if (width == 0u || height == 0u
        || width > kMaximumTextureExtent
        || height > kMaximumTextureExtent
        || bytesPerPixel == 0u) {
        return false;
    }
    const uint64_t row =
        static_cast<uint64_t>(width) * bytesPerPixel;
    const uint64_t total = row * height;
    if (row > std::numeric_limits<uint32_t>::max()
        || total > std::numeric_limits<size_t>::max()
        || total > std::vector<uint8_t>{}.max_size()) {
        return false;
    }
    bytesPerRow = static_cast<uint32_t>(row);
    byteCount = static_cast<size_t>(total);
    return true;
}

bool validDecodedImage(
    int width, int height, uint32_t bytesPerPixel,
    size_t& byteCount, uint32_t& bytesPerRow) noexcept {
    return width > 0 && height > 0
        && validImageLayout(
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            bytesPerPixel, byteCount, bytesPerRow);
}

const std::array<float, 256>& srgbDecodeTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> result{};
        for (size_t index = 0; index < result.size(); ++index) {
            const float encoded = static_cast<float>(index) / 255.0f;
            result[index] = encoded <= 0.04045f
                ? encoded / 12.92f
                : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }
        return result;
    }();
    return table;
}

const std::array<uint8_t, 65'536>& srgbEncodeTable() {
    // A 16-bit linear-light lookup avoids millions of pow() calls while
    // building a 4K mip chain. The quantization is far below RGBA8 precision.
    static const std::array<uint8_t, 65'536> table = [] {
        std::array<uint8_t, 65'536> result{};
        for (size_t index = 0; index < result.size(); ++index) {
            const float linear =
                static_cast<float>(index) /
                static_cast<float>(result.size() - 1u);
            const float encoded = linear <= 0.0031308f
                ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            result[index] = static_cast<uint8_t>(
                std::lround(encoded * 255.0f));
        }
        return result;
    }();
    return table;
}

uint8_t encodeSrgb8(float linear) noexcept {
    const auto& table = srgbEncodeTable();
    const size_t index = static_cast<size_t>(std::lround(
        std::clamp(linear, 0.0f, 1.0f) *
        static_cast<float>(table.size() - 1u)));
    return table[index];
}

struct DecodedImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

std::optional<DecodedImage> decodeImage(
    const std::filesystem::path& path, int requestedChannels) {
    if (requestedChannels < 1 || requestedChannels > 4) return std::nullopt;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;
    const auto fileSize = file.tellg();
    if (fileSize <= 0
        || fileSize > std::numeric_limits<std::streamsize>::max()
        || fileSize > std::numeric_limits<int>::max()
        || fileSize > kMaximumEncodedTextureBytes) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> encoded(static_cast<size_t>(fileSize));
    if (!file.read(
            reinterpret_cast<char*>(encoded.data()),
            static_cast<std::streamsize>(fileSize))) {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    size_t pixelBytes = 0u;
    uint32_t bytesPerRow = 0u;
    if (!stbi_info_from_memory(
            encoded.data(), static_cast<int>(encoded.size()),
            &width, &height, &sourceChannels)
        || !validDecodedImage(
            width, height, static_cast<uint32_t>(requestedChannels),
            pixelBytes, bytesPerRow)) {
        return std::nullopt;
    }

    uint8_t* decoded = stbi_load_from_memory(
        encoded.data(), static_cast<int>(encoded.size()),
        &width, &height, &sourceChannels, requestedChannels);
    if (!decoded
        || !validDecodedImage(
            width, height, static_cast<uint32_t>(requestedChannels),
            pixelBytes, bytesPerRow)) {
        if (decoded) stbi_image_free(decoded);
        return std::nullopt;
    }

    DecodedImage result{
        .pixels = std::vector<uint8_t>(decoded, decoded + pixelBytes),
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
    };
    stbi_image_free(decoded);
    return result;
}

bool writeTextureArrayLayer(
    WGPUQueue queue, WGPUTexture texture, std::span<const uint8_t> data,
    uint32_t width, uint32_t height, uint32_t mipLevel,
    uint32_t arrayLayer) {
    if (!queue || !texture
        || mipLevel >= wgpuTextureGetMipLevelCount(texture)
        || arrayLayer >= wgpuTextureGetDepthOrArrayLayers(texture)) {
        return false;
    }
    const uint32_t bytesPerTexel =
        gpu::getBytesPerPixel(wgpuTextureGetFormat(texture));
    const uint32_t bytesPerRow = width * bytesPerTexel;
    if (!gpu::isTextureUploadDataValid(
            data.size(), width, height, bytesPerRow, bytesPerTexel)) {
        return false;
    }

    WGPUOrigin3D origin{};
    origin.z = arrayLayer;
    auto destination =
        gpu::makeTextureCopyDest(texture, mipLevel, origin);
    const auto layout =
        gpu::makeTextureDataLayout(0u, bytesPerRow, height);
    WGPUExtent3D extent{};
    extent.width = width;
    extent.height = height;
    extent.depthOrArrayLayers = 1u;
    wgpuQueueWriteTexture(
        queue, &destination, data.data(), data.size(), &layout, &extent);
    return true;
}

struct MaterialSource {
    std::string_view stem;
};

constexpr std::array<MaterialSource, TerrainTextures::kMaterialLayerCount>
    kMaterialSources{{
        {"Ground054_1K-JPG"}, // dry and wet sand
        {"Ground037_1K-JPG"}, // soil and sparse ground cover
        {"Grass001_1K-JPG"},  // dense grass
        {"Rock050_1K-JPG"},   // exposed rock
    }};

} // namespace

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
    , materialAlbedoTexture_(other.materialAlbedoTexture_)
    , materialAlbedoView_(other.materialAlbedoView_)
    , materialNormalRoughnessTexture_(
          other.materialNormalRoughnessTexture_)
    , materialNormalRoughnessView_(other.materialNormalRoughnessView_)
    , materialWidth_(other.materialWidth_)
    , materialHeight_(other.materialHeight_)
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
    other.materialAlbedoTexture_ = nullptr;
    other.materialAlbedoView_ = nullptr;
    other.materialNormalRoughnessTexture_ = nullptr;
    other.materialNormalRoughnessView_ = nullptr;
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
        materialAlbedoTexture_ = other.materialAlbedoTexture_;
        materialAlbedoView_ = other.materialAlbedoView_;
        materialNormalRoughnessTexture_ =
            other.materialNormalRoughnessTexture_;
        materialNormalRoughnessView_ =
            other.materialNormalRoughnessView_;
        materialWidth_ = other.materialWidth_;
        materialHeight_ = other.materialHeight_;
        sampler_ = other.sampler_;
        config_ = other.config_;
        
        other.device_ = nullptr;
        other.queue_ = nullptr;
        other.albedoTexture_ = nullptr;
        other.albedoView_ = nullptr;
        other.lightmapTexture_ = nullptr;
        other.lightmapView_ = nullptr;
        other.materialAlbedoTexture_ = nullptr;
        other.materialAlbedoView_ = nullptr;
        other.materialNormalRoughnessTexture_ = nullptr;
        other.materialNormalRoughnessView_ = nullptr;
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
    if (materialAlbedoView_) {
        wgpuTextureViewRelease(materialAlbedoView_);
        materialAlbedoView_ = nullptr;
    }
    if (materialAlbedoTexture_) {
        wgpuTextureRelease(materialAlbedoTexture_);
        materialAlbedoTexture_ = nullptr;
    }
    if (materialNormalRoughnessView_) {
        wgpuTextureViewRelease(materialNormalRoughnessView_);
        materialNormalRoughnessView_ = nullptr;
    }
    if (materialNormalRoughnessTexture_) {
        wgpuTextureRelease(materialNormalRoughnessTexture_);
        materialNormalRoughnessTexture_ = nullptr;
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
    materialWidth_ = 0;
    materialHeight_ = 0;
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
            if (!createWhiteLightmap(1, 1)) {
                LOG_ERROR("Failed to create white lightmap texture");
                shutdown();
                return false;
            }
        }
    } else {
        if (!createWhiteLightmap(1, 1)) {
            LOG_ERROR("Failed to create white lightmap texture");
            shutdown();
            return false;
        }
    }

    const bool materialAssetsPresent =
        !config.materialDirectory.empty()
        && std::filesystem::exists(config.materialDirectory);
    if (!materialAssetsPresent
        || !loadTerrainMaterials(config.materialDirectory)) {
        if (materialAssetsPresent) {
            LOG_WARN(
                "Terrain material pack is incomplete; using bounded fallback");
        }
        if (!createFallbackTerrainMaterials()) {
            LOG_ERROR("Failed to create terrain material arrays");
            shutdown();
            return false;
        }
    }
    
    LOG_INFO(
        "TerrainTextures initialized: macro {}x{}, lightmap {}x{}, "
        "materials {}x{}x{}",
        albedoWidth_, albedoHeight_, lightmapWidth_, lightmapHeight_,
        materialWidth_, materialHeight_, kMaterialLayerCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampler Creation
// ─────────────────────────────────────────────────────────────────────────────

bool TerrainTextures::createSampler() {
    // Terrain is commonly viewed at grazing angles. Mips remove minification
    // shimmer; anisotropy preserves useful along-slope detail.
    gpu::SamplerDesc samplerDesc =
        gpu::SamplerDesc::anisotropic(8u, "terrain_sampler");
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
    
    const auto fileSize = file.tellg();
    if (fileSize <= 0
        || fileSize > std::numeric_limits<std::streamsize>::max()
        || fileSize > std::numeric_limits<int>::max()
        || fileSize > kMaximumEncodedTextureBytes) {
        LOG_ERROR("Albedo texture file is empty or too large: {}",
                  path.string());
        return false;
    }
    file.seekg(0, std::ios::beg);

    const size_t byteCount = static_cast<size_t>(fileSize);
    std::vector<uint8_t> fileData(byteCount);
    if (!file.read(reinterpret_cast<char*>(fileData.data()),
                   static_cast<std::streamsize>(fileSize))) {
        LOG_ERROR("Failed to read albedo texture file: {}", path.string());
        return false;
    }
    file.close();

    int width = 0;
    int height = 0;
    int channels = 0;
    size_t pixelBytes = 0u;
    uint32_t bytesPerRow = 0u;
    if (!stbi_info_from_memory(
            fileData.data(), static_cast<int>(fileData.size()),
            &width, &height, &channels)
        || !validDecodedImage(
            width, height, 4u, pixelBytes, bytesPerRow)) {
        LOG_ERROR("Albedo texture dimensions are invalid or too large: {}x{}",
                  width, height);
        return false;
    }

    uint8_t* data = stbi_load_from_memory(
        fileData.data(), static_cast<int>(fileData.size()),
        &width, &height, &channels, 4);
    
    if (!data) {
        LOG_ERROR("Failed to decode albedo texture: {}", path.string());
        return false;
    }
    if (!validDecodedImage(
            width, height, 4u, pixelBytes, bytesPerRow)) {
        stbi_image_free(data);
        LOG_ERROR("Decoded albedo dimensions are too large: {}x{}",
                  width, height);
        return false;
    }
    
    LOG_DEBUG("Loaded albedo texture: {}x{} from {}", width, height, path.string());
    
    // WebGPU's queue write copies the source immediately. Upload directly from
    // stb's allocation so large terrain images never exist twice in the WASM
    // heap during startup.
    const bool uploaded = uploadAlbedoTexture(
        std::span<const uint8_t>(data, pixelBytes),
        static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    stbi_image_free(data);
    return uploaded;
}

bool TerrainTextures::loadLightmap(const std::filesystem::path& path) {
    LOG_SCOPE("TerrainTextures::loadLightmap");
    
    // Read file into memory (STBI_NO_STDIO is defined)
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open lightmap texture file: {}", path.string());
        return false;
    }
    
    const auto fileSize = file.tellg();
    if (fileSize <= 0
        || fileSize > std::numeric_limits<std::streamsize>::max()
        || fileSize > std::numeric_limits<int>::max()
        || fileSize > kMaximumEncodedTextureBytes) {
        LOG_ERROR("Lightmap texture file is empty or too large: {}",
                  path.string());
        return false;
    }
    file.seekg(0, std::ios::beg);

    const size_t byteCount = static_cast<size_t>(fileSize);
    std::vector<uint8_t> fileData(byteCount);
    if (!file.read(reinterpret_cast<char*>(fileData.data()),
                   static_cast<std::streamsize>(fileSize))) {
        LOG_ERROR("Failed to read lightmap texture file: {}", path.string());
        return false;
    }
    file.close();

    int width = 0;
    int height = 0;
    int channels = 0;
    size_t pixelBytes = 0u;
    uint32_t bytesPerRow = 0u;
    if (!stbi_info_from_memory(
            fileData.data(), static_cast<int>(fileData.size()),
            &width, &height, &channels)
        || !validDecodedImage(
            width, height, 1u, pixelBytes, bytesPerRow)) {
        LOG_ERROR("Lightmap dimensions are invalid or too large: {}x{}",
                  width, height);
        return false;
    }

    uint8_t* data = stbi_load_from_memory(
        fileData.data(), static_cast<int>(fileData.size()),
        &width, &height, &channels, 1);
    
    if (!data) {
        LOG_ERROR("Failed to decode lightmap texture: {}", path.string());
        return false;
    }
    if (!validDecodedImage(
            width, height, 1u, pixelBytes, bytesPerRow)) {
        stbi_image_free(data);
        LOG_ERROR("Decoded lightmap dimensions are too large: {}x{}",
                  width, height);
        return false;
    }
    
    LOG_DEBUG("Loaded lightmap texture: {}x{} from {}", width, height, path.string());
    
    // Copy data to vector
    std::vector<uint8_t> pixels(data, data + pixelBytes);
    stbi_image_free(data);
    
    return uploadLightmapTexture(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool TerrainTextures::loadTerrainMaterials(
    const std::filesystem::path& directory) {
    LOG_SCOPE("TerrainTextures::loadTerrainMaterials");
    if (directory.empty()) return false;

    std::array<std::vector<uint8_t>, kMaterialLayerCount> albedo;
    std::array<std::vector<uint8_t>, kMaterialLayerCount> normalRoughness;
    uint32_t commonWidth = 0u;
    uint32_t commonHeight = 0u;

    for (uint32_t layer = 0u; layer < kMaterialLayerCount; ++layer) {
        const std::string stem(kMaterialSources[layer].stem);
        auto color = decodeImage(
            directory / (stem + "_Color.jpg"), 4);
        auto normal = decodeImage(
            directory / (stem + "_NormalGL.jpg"), 3);
        auto roughness = decodeImage(
            directory / (stem + "_Roughness.jpg"), 1);
        if (!color || !normal || !roughness) {
            LOG_WARN("Missing or invalid terrain material layer '{}'", stem);
            return false;
        }
        if (layer == 0u) {
            commonWidth = color->width;
            commonHeight = color->height;
        }
        if (color->width != commonWidth || color->height != commonHeight
            || normal->width != commonWidth
            || normal->height != commonHeight
            || roughness->width != commonWidth
            || roughness->height != commonHeight) {
            LOG_WARN("Terrain material '{}' dimensions do not match", stem);
            return false;
        }

        albedo[layer] = std::move(color->pixels);
        const size_t texelCount =
            static_cast<size_t>(commonWidth) * commonHeight;
        normalRoughness[layer].resize(texelCount * 4u);
        for (size_t texel = 0u; texel < texelCount; ++texel) {
            const size_t packed = texel * 4u;
            const size_t sourceNormal = texel * 3u;
            normalRoughness[layer][packed + 0u] =
                normal->pixels[sourceNormal + 0u];
            normalRoughness[layer][packed + 1u] =
                normal->pixels[sourceNormal + 1u];
            normalRoughness[layer][packed + 2u] =
                normal->pixels[sourceNormal + 2u];
            normalRoughness[layer][packed + 3u] =
                roughness->pixels[texel];
        }
    }

    if (!uploadTerrainMaterialArrays(
            albedo, normalRoughness, commonWidth, commonHeight)) {
        return false;
    }
    LOG_INFO(
        "Loaded CC0 terrain material arrays from '{}' ({}x{}x{})",
        directory.string(), commonWidth, commonHeight, kMaterialLayerCount);
    return true;
}

bool TerrainTextures::createFallbackTerrainMaterials(
    uint32_t width, uint32_t height) {
    size_t byteCount = 0u;
    uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, byteCount, bytesPerRow)) {
        return false;
    }

    constexpr std::array<std::array<uint8_t, 3>, kMaterialLayerCount>
        baseColor{{
            {166u, 145u, 108u},
            {112u, 101u, 72u},
            {62u, 94u, 43u},
            {84u, 85u, 82u},
        }};
    constexpr std::array<uint8_t, kMaterialLayerCount> roughness{
        194u, 210u, 220u, 184u,
    };
    std::array<std::vector<uint8_t>, kMaterialLayerCount> albedo;
    std::array<std::vector<uint8_t>, kMaterialLayerCount> normalRoughness;
    for (uint32_t layer = 0u; layer < kMaterialLayerCount; ++layer) {
        albedo[layer].resize(byteCount);
        normalRoughness[layer].resize(byteCount);
        for (uint32_t y = 0u; y < height; ++y) {
            for (uint32_t x = 0u; x < width; ++x) {
                const size_t index =
                    (static_cast<size_t>(y) * width + x) * 4u;
                const uint32_t hash =
                    (x * 1'664'525u + y * 1'013'904'223u
                     + layer * 747'796'405u) >> 27u;
                const int variation = static_cast<int>(hash) - 16;
                for (size_t channel = 0u; channel < 3u; ++channel) {
                    albedo[layer][index + channel] =
                        static_cast<uint8_t>(std::clamp(
                            static_cast<int>(baseColor[layer][channel])
                                + variation,
                            0, 255));
                }
                albedo[layer][index + 3u] = 255u;
                normalRoughness[layer][index + 0u] = 128u;
                normalRoughness[layer][index + 1u] = 128u;
                normalRoughness[layer][index + 2u] = 255u;
                normalRoughness[layer][index + 3u] = roughness[layer];
            }
        }
    }
    return uploadTerrainMaterialArrays(
        albedo, normalRoughness, width, height);
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

bool TerrainTextures::uploadAlbedoTexture(std::span<const uint8_t> data,
                                          uint32_t width, uint32_t height) {
    size_t expectedBytes = 0u;
    [[maybe_unused]] uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, expectedBytes, bytesPerRow)
        || data.size() < expectedBytes) {
        LOG_ERROR("Invalid albedo upload: {} bytes for {}x{}",
                  data.size(), width, height);
        return false;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2DMipmapped(
        width, height,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_albedo"
    );
    
    WGPUTexture nextTexture = gpu::createTexture(device_, texDesc);
    if (!nextTexture) {
        LOG_ERROR("Failed to create albedo texture");
        return false;
    }
    
    // Upload data
    if (!gpu::writeTexture(
            queue_, nextTexture,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(data.data()), data.size()),
            width, height, bytesPerRow)) {
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return false;
    }

    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    std::span<const uint8_t> previous = data;
    std::vector<uint8_t> ownedPrevious;
    const uint32_t mipLevelCount = wgpuTextureGetMipLevelCount(nextTexture);
    for (uint32_t level = 1u; level < mipLevelCount; ++level) {
        std::vector<uint8_t> next =
            downsampleTerrainAlbedoSrgb(previous, mipWidth, mipHeight);
        const uint32_t nextWidth = std::max(mipWidth >> 1u, 1u);
        const uint32_t nextHeight = std::max(mipHeight >> 1u, 1u);
        if (next.empty()
            || !gpu::writeTexture(
                queue_, nextTexture, std::as_bytes(std::span(next)),
                nextWidth, nextHeight, nextWidth * 4u, level)) {
            wgpuTextureDestroy(nextTexture);
            wgpuTextureRelease(nextTexture);
            return false;
        }
        ownedPrevious = std::move(next);
        previous = ownedPrevious;
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_albedo_view";
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.mipLevelCount = mipLevelCount;
    
    WGPUTextureView nextView = gpu::createTextureView(nextTexture, viewDesc);
    if (!nextView) {
        LOG_ERROR("Failed to create albedo texture view");
        wgpuTextureRelease(nextTexture);
        return false;
    }

    if (albedoView_) wgpuTextureViewRelease(albedoView_);
    if (albedoTexture_) wgpuTextureRelease(albedoTexture_);
    albedoTexture_ = nextTexture;
    albedoView_ = nextView;
    
    albedoWidth_ = width;
    albedoHeight_ = height;
    
    LOG_DEBUG("Created albedo texture: {}x{}", width, height);
    return true;
}

bool TerrainTextures::uploadLightmapTexture(const std::vector<uint8_t>& data,
                                             uint32_t width, uint32_t height) {
    size_t expectedBytes = 0u;
    [[maybe_unused]] uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 1u, expectedBytes, bytesPerRow)
        || data.size() < expectedBytes) {
        LOG_ERROR("Invalid lightmap upload: {} bytes for {}x{}",
                  data.size(), width, height);
        return false;
    }
    
    // Create texture
    gpu::TextureDesc texDesc = gpu::TextureDesc::tex2D(
        width, height,
        WGPUTextureFormat_R8Unorm,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_lightmap"
    );
    
    WGPUTexture nextTexture = gpu::createTexture(device_, texDesc);
    if (!nextTexture) {
        LOG_ERROR("Failed to create lightmap texture");
        return false;
    }
    
    // Upload data
    if (!gpu::writeTexture(
            queue_, nextTexture,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(data.data()), data.size()),
            width, height, bytesPerRow)) {
        wgpuTextureDestroy(nextTexture);
        wgpuTextureRelease(nextTexture);
        return false;
    }
    
    // Create texture view
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "terrain_lightmap_view";
    viewDesc.format = WGPUTextureFormat_R8Unorm;
    
    WGPUTextureView nextView = gpu::createTextureView(nextTexture, viewDesc);
    if (!nextView) {
        LOG_ERROR("Failed to create lightmap texture view");
        wgpuTextureRelease(nextTexture);
        return false;
    }

    if (lightmapView_) wgpuTextureViewRelease(lightmapView_);
    if (lightmapTexture_) wgpuTextureRelease(lightmapTexture_);
    lightmapTexture_ = nextTexture;
    lightmapView_ = nextView;
    
    lightmapWidth_ = width;
    lightmapHeight_ = height;
    
    LOG_DEBUG("Created lightmap texture: {}x{}", width, height);
    return true;
}

bool TerrainTextures::uploadTerrainMaterialArrays(
    const std::array<std::vector<uint8_t>, kMaterialLayerCount>& albedo,
    const std::array<std::vector<uint8_t>, kMaterialLayerCount>&
        normalRoughness,
    uint32_t width, uint32_t height) {
    size_t expectedBytes = 0u;
    uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, expectedBytes, bytesPerRow)) {
        return false;
    }
    for (uint32_t layer = 0u; layer < kMaterialLayerCount; ++layer) {
        if (albedo[layer].size() < expectedBytes
            || normalRoughness[layer].size() < expectedBytes) {
            return false;
        }
    }

    gpu::TextureDesc description = gpu::TextureDesc::tex2DMipmapped(
        width, height, WGPUTextureFormat_RGBA8UnormSrgb,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "terrain_material_albedo_array");
    description.depthOrArrayLayers = kMaterialLayerCount;
    WGPUTexture nextAlbedo = gpu::createTexture(device_, description);
    if (!nextAlbedo) return false;

    description.label = "terrain_material_normal_roughness_array";
    description.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTexture nextNormalRoughness =
        gpu::createTexture(device_, description);
    if (!nextNormalRoughness) {
        wgpuTextureDestroy(nextAlbedo);
        wgpuTextureRelease(nextAlbedo);
        return false;
    }

    const auto fail = [&]() {
        wgpuTextureDestroy(nextNormalRoughness);
        wgpuTextureRelease(nextNormalRoughness);
        wgpuTextureDestroy(nextAlbedo);
        wgpuTextureRelease(nextAlbedo);
        return false;
    };
    const uint32_t mipLevelCount =
        wgpuTextureGetMipLevelCount(nextAlbedo);
    for (uint32_t layer = 0u; layer < kMaterialLayerCount; ++layer) {
        if (!writeTextureArrayLayer(
                queue_, nextAlbedo, albedo[layer],
                width, height, 0u, layer)
            || !writeTextureArrayLayer(
                queue_, nextNormalRoughness, normalRoughness[layer],
                width, height, 0u, layer)) {
            return fail();
        }

        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        std::span<const uint8_t> previousAlbedo = albedo[layer];
        std::span<const uint8_t> previousNormal =
            normalRoughness[layer];
        std::vector<uint8_t> ownedAlbedo;
        std::vector<uint8_t> ownedNormal;
        for (uint32_t level = 1u; level < mipLevelCount; ++level) {
            std::vector<uint8_t> nextAlbedoMip =
                downsampleTerrainAlbedoSrgb(
                    previousAlbedo, mipWidth, mipHeight);
            std::vector<uint8_t> nextNormalMip =
                downsampleTerrainNormalRoughness(
                    previousNormal, mipWidth, mipHeight);
            const uint32_t nextWidth = std::max(mipWidth >> 1u, 1u);
            const uint32_t nextHeight = std::max(mipHeight >> 1u, 1u);
            if (nextAlbedoMip.empty() || nextNormalMip.empty()
                || !writeTextureArrayLayer(
                    queue_, nextAlbedo, nextAlbedoMip,
                    nextWidth, nextHeight, level, layer)
                || !writeTextureArrayLayer(
                    queue_, nextNormalRoughness, nextNormalMip,
                    nextWidth, nextHeight, level, layer)) {
                return fail();
            }
            ownedAlbedo = std::move(nextAlbedoMip);
            ownedNormal = std::move(nextNormalMip);
            previousAlbedo = ownedAlbedo;
            previousNormal = ownedNormal;
            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }
    }

    gpu::TextureViewDesc viewDescription{};
    viewDescription.format = WGPUTextureFormat_RGBA8UnormSrgb;
    viewDescription.dimension = WGPUTextureViewDimension_2DArray;
    viewDescription.mipLevelCount = mipLevelCount;
    viewDescription.arrayLayerCount = kMaterialLayerCount;
    viewDescription.label = "terrain_material_albedo_array_view";
    WGPUTextureView nextAlbedoView =
        gpu::createTextureView(nextAlbedo, viewDescription);
    if (!nextAlbedoView) return fail();
    viewDescription.label =
        "terrain_material_normal_roughness_array_view";
    viewDescription.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUTextureView nextNormalRoughnessView =
        gpu::createTextureView(nextNormalRoughness, viewDescription);
    if (!nextNormalRoughnessView) {
        wgpuTextureViewRelease(nextAlbedoView);
        return fail();
    }

    if (materialAlbedoView_) {
        wgpuTextureViewRelease(materialAlbedoView_);
    }
    if (materialAlbedoTexture_) {
        wgpuTextureRelease(materialAlbedoTexture_);
    }
    if (materialNormalRoughnessView_) {
        wgpuTextureViewRelease(materialNormalRoughnessView_);
    }
    if (materialNormalRoughnessTexture_) {
        wgpuTextureRelease(materialNormalRoughnessTexture_);
    }
    materialAlbedoTexture_ = nextAlbedo;
    materialAlbedoView_ = nextAlbedoView;
    materialNormalRoughnessTexture_ = nextNormalRoughness;
    materialNormalRoughnessView_ = nextNormalRoughnessView;
    materialWidth_ = width;
    materialHeight_ = height;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<uint8_t> generateTerrainColorData(uint32_t width, uint32_t height) {
    size_t byteCount = 0u;
    [[maybe_unused]] uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, byteCount, bytesPerRow)) {
        return {};
    }
    std::vector<uint8_t> data(byteCount);
    
    // Generate a procedural terrain color pattern for Canyon biome
    // Base colors for canyon (reddish/brownish rock)
    // Reddish-brown base
    const uint8_t baseR = 180;
    const uint8_t baseG = 110;
    const uint8_t baseB = 80;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t idx =
                (static_cast<size_t>(y) * width + x) * 4u;
            
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
    size_t byteCount = 0u;
    [[maybe_unused]] uint32_t bytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 1u, byteCount, bytesPerRow)) {
        return {};
    }
    std::vector<uint8_t> data(byteCount, 255);
    return data;
}

std::vector<uint8_t> downsampleTerrainAlbedoSrgb(
    std::span<const uint8_t> source, uint32_t width, uint32_t height) {
    size_t sourceBytes = 0u;
    uint32_t sourceBytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, sourceBytes, sourceBytesPerRow)
        || source.size() < sourceBytes
        || (width == 1u && height == 1u)) {
        return {};
    }

    const uint32_t outputWidth = std::max(width >> 1u, 1u);
    const uint32_t outputHeight = std::max(height >> 1u, 1u);
    size_t outputBytes = 0u;
    uint32_t outputBytesPerRow = 0u;
    if (!validImageLayout(
            outputWidth, outputHeight, 4u,
            outputBytes, outputBytesPerRow)) {
        return {};
    }

    const auto& decode = srgbDecodeTable();
    std::vector<uint8_t> output(outputBytes);
    for (uint32_t y = 0u; y < outputHeight; ++y) {
        const float sourceY0 =
            static_cast<float>(y) * static_cast<float>(height) /
            static_cast<float>(outputHeight);
        const float sourceY1 =
            static_cast<float>(y + 1u) * static_cast<float>(height) /
            static_cast<float>(outputHeight);
        const uint32_t firstY = static_cast<uint32_t>(
            std::floor(sourceY0));
        const uint32_t lastY = std::min(
            static_cast<uint32_t>(std::ceil(sourceY1)), height);
        for (uint32_t x = 0u; x < outputWidth; ++x) {
            const float sourceX0 =
                static_cast<float>(x) * static_cast<float>(width) /
                static_cast<float>(outputWidth);
            const float sourceX1 =
                static_cast<float>(x + 1u) * static_cast<float>(width) /
                static_cast<float>(outputWidth);
            const uint32_t firstX = static_cast<uint32_t>(
                std::floor(sourceX0));
            const uint32_t lastX = std::min(
                static_cast<uint32_t>(std::ceil(sourceX1)), width);
            const size_t destination =
                (size_t{y} * outputWidth + x) * 4u;

            std::array<float, 4> accumulated{};
            float totalWeight = 0.0f;
            for (uint32_t sourceY = firstY;
                 sourceY < lastY; ++sourceY) {
                const float weightY = std::max(
                    0.0f,
                    std::min(sourceY1, static_cast<float>(sourceY + 1u)) -
                    std::max(sourceY0, static_cast<float>(sourceY)));
                for (uint32_t sourceX = firstX;
                     sourceX < lastX; ++sourceX) {
                    const float weightX = std::max(
                        0.0f,
                        std::min(
                            sourceX1, static_cast<float>(sourceX + 1u)) -
                        std::max(sourceX0, static_cast<float>(sourceX)));
                    const float weight = weightX * weightY;
                    const size_t sample =
                        (size_t{sourceY} * width + sourceX) * 4u;
                    for (size_t channel = 0u; channel < 3u; ++channel) {
                        accumulated[channel] +=
                            decode[source[sample + channel]] * weight;
                    }
                    accumulated[3] +=
                        static_cast<float>(source[sample + 3u]) * weight;
                    totalWeight += weight;
                }
            }

            const float inverseWeight =
                totalWeight > 0.0f ? 1.0f / totalWeight : 0.0f;
            for (size_t channel = 0u; channel < 3u; ++channel) {
                output[destination + channel] =
                    encodeSrgb8(accumulated[channel] * inverseWeight);
            }
            output[destination + 3u] =
                static_cast<uint8_t>(std::lround(std::clamp(
                    accumulated[3] * inverseWeight, 0.0f, 255.0f)));
        }
    }
    return output;
}

std::vector<uint8_t> downsampleTerrainNormalRoughness(
    std::span<const uint8_t> source, uint32_t width, uint32_t height) {
    size_t sourceBytes = 0u;
    uint32_t sourceBytesPerRow = 0u;
    if (!validImageLayout(
            width, height, 4u, sourceBytes, sourceBytesPerRow)
        || source.size() < sourceBytes
        || (width == 1u && height == 1u)) {
        return {};
    }

    const uint32_t outputWidth = std::max(width >> 1u, 1u);
    const uint32_t outputHeight = std::max(height >> 1u, 1u);
    size_t outputBytes = 0u;
    uint32_t outputBytesPerRow = 0u;
    if (!validImageLayout(
            outputWidth, outputHeight, 4u,
            outputBytes, outputBytesPerRow)) {
        return {};
    }

    std::vector<uint8_t> output(outputBytes);
    for (uint32_t y = 0u; y < outputHeight; ++y) {
        const float sourceY0 =
            static_cast<float>(y) * static_cast<float>(height)
            / static_cast<float>(outputHeight);
        const float sourceY1 =
            static_cast<float>(y + 1u) * static_cast<float>(height)
            / static_cast<float>(outputHeight);
        const uint32_t firstY =
            static_cast<uint32_t>(std::floor(sourceY0));
        const uint32_t lastY = std::min(
            static_cast<uint32_t>(std::ceil(sourceY1)), height);
        for (uint32_t x = 0u; x < outputWidth; ++x) {
            const float sourceX0 =
                static_cast<float>(x) * static_cast<float>(width)
                / static_cast<float>(outputWidth);
            const float sourceX1 =
                static_cast<float>(x + 1u) * static_cast<float>(width)
                / static_cast<float>(outputWidth);
            const uint32_t firstX =
                static_cast<uint32_t>(std::floor(sourceX0));
            const uint32_t lastX = std::min(
                static_cast<uint32_t>(std::ceil(sourceX1)), width);

            std::array<float, 3> normalSum{};
            float roughnessFourthSum = 0.0f;
            float totalWeight = 0.0f;
            for (uint32_t sourceY = firstY;
                 sourceY < lastY; ++sourceY) {
                const float weightY = std::max(
                    0.0f,
                    std::min(sourceY1, static_cast<float>(sourceY + 1u))
                        - std::max(sourceY0, static_cast<float>(sourceY)));
                for (uint32_t sourceX = firstX;
                     sourceX < lastX; ++sourceX) {
                    const float weightX = std::max(
                        0.0f,
                        std::min(
                            sourceX1, static_cast<float>(sourceX + 1u))
                            - std::max(
                                sourceX0, static_cast<float>(sourceX)));
                    const float weight = weightX * weightY;
                    const size_t sample =
                        (static_cast<size_t>(sourceY) * width + sourceX)
                        * 4u;
                    std::array<float, 3> normal{
                        static_cast<float>(source[sample + 0u])
                            / 127.5f - 1.0f,
                        static_cast<float>(source[sample + 1u])
                            / 127.5f - 1.0f,
                        static_cast<float>(source[sample + 2u])
                            / 127.5f - 1.0f,
                    };
                    const float normalLength = std::sqrt(
                        normal[0] * normal[0]
                        + normal[1] * normal[1]
                        + normal[2] * normal[2]);
                    if (normalLength > 1.0e-6f) {
                        for (float& component : normal) {
                            component /= normalLength;
                        }
                    } else {
                        normal = {0.0f, 0.0f, 1.0f};
                    }
                    for (size_t component = 0u;
                         component < normal.size(); ++component) {
                        normalSum[component] +=
                            normal[component] * weight;
                    }
                    const float perceptualRoughness =
                        static_cast<float>(source[sample + 3u]) / 255.0f;
                    const float roughnessSquared =
                        perceptualRoughness * perceptualRoughness;
                    roughnessFourthSum +=
                        roughnessSquared * roughnessSquared * weight;
                    totalWeight += weight;
                }
            }

            const float inverseWeight =
                totalWeight > 0.0f ? 1.0f / totalWeight : 0.0f;
            for (float& component : normalSum) {
                component *= inverseWeight;
            }
            const float averageLength = std::sqrt(
                normalSum[0] * normalSum[0]
                + normalSum[1] * normalSum[1]
                + normalSum[2] * normalSum[2]);
            if (averageLength > 1.0e-6f) {
                for (float& component : normalSum) {
                    component /= averageLength;
                }
            } else {
                normalSum = {0.0f, 0.0f, 1.0f};
            }

            // Perceptual roughness squared is GGX alpha. Average alpha^2,
            // then add a conservative Toksvig-style normal variance term.
            const float roughnessFourth =
                roughnessFourthSum * inverseWeight;
            const float normalVariance =
                std::clamp(1.0f - averageLength, 0.0f, 1.0f) * 0.35f;
            const float filteredRoughness = std::pow(
                std::clamp(
                    roughnessFourth + normalVariance, 0.0f, 1.0f),
                0.25f);
            const size_t destination =
                (static_cast<size_t>(y) * outputWidth + x) * 4u;
            for (size_t component = 0u; component < 3u; ++component) {
                output[destination + component] =
                    static_cast<uint8_t>(std::lround(std::clamp(
                        normalSum[component] * 0.5f + 0.5f,
                        0.0f, 1.0f) * 255.0f));
            }
            output[destination + 3u] =
                static_cast<uint8_t>(std::lround(
                    filteredRoughness * 255.0f));
        }
    }
    return output;
}

} // namespace voxy::terrain

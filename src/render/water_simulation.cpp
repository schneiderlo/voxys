#include "render/water_simulation.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

namespace voxy::render {
namespace {

constexpr uint32_t kElementCount = WaterSimulation::RESOLUTION *
                                   WaterSimulation::RESOLUTION *
                                   WaterSimulation::CASCADE_COUNT;
constexpr size_t kCpuWaveModeCount = 24;

// Matches WaveData in water_fft.wgsl. Before evolution the first four complex
// lanes hold the pre-expanded temporal coefficients and normalized wavevector.
// The same storage becomes three complex displacement spectra for the FFT.
struct alignas(16) WaveData {
    glm::vec2 height{};
    glm::vec2 displacementX{};
    glm::vec2 displacementZ{};
    glm::vec2 padding{};
};
static_assert(sizeof(WaveData) == 32);

struct CoastPixel {
    uint32_t direction;
    uint32_t distanceDepth;
};
static_assert(sizeof(CoastPixel) == 8);

CoastPixel packCoastPixel(const glm::vec2& onshoreDirection,
                          float coastDistance, float waterDepth) {
    return {
        glm::packHalf2x16(onshoreDirection),
        glm::packHalf2x16(glm::vec2(coastDistance, waterDepth)),
    };
}

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTau = 2.0f * kPi;
constexpr float kGravity = 9.81f;
constexpr float kFrequencyTick = kTau / 8192.0f;

float cascadeValue(const glm::vec2& values, uint32_t cascade) {
    return cascade == 0u ? values.x : values.y;
}

float fract(float value) {
    return value - std::floor(value);
}

float spectrumHash(float value) {
    const float p = fract(value * 0.1031f);
    const float q = p + 19.19f;
    return fract(q * (q + 47.43f) * p);
}

float smoothTransition(float edge0, float edge1, float value) {
    if (edge0 == edge1) return value < edge0 ? 0.0f : 1.0f;
    const float x = std::clamp((value - edge0) / (edge1 - edge0),
                               0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float directionalIntegral(float exponent, float blend) {
    double sum = 0.0;
    for (uint32_t index = 0; index < 64; ++index) {
        const double angle = (static_cast<double>(index) + 0.5) / 64.0 *
                             2.0 * std::numbers::pi;
        const double cosineLobe = 0.5 * (1.0 + std::cos(angle));
        sum += std::pow(cosineLobe, static_cast<double>(exponent)) *
               (static_cast<double>(blend) +
                (1.0 - static_cast<double>(blend)) * cosineLobe);
    }
    return static_cast<float>(2.0 * sum * std::numbers::pi / 64.0);
}

float integrateSpectrum(float minimum, float maximum, float lengthScale,
                        float directionalBlend, float significantWaveHeight,
                        float peakEnhancement) {
    if (maximum <= minimum) return 0.0f;
    const double logMinimum = std::log(static_cast<double>(minimum));
    const double step = (std::log(static_cast<double>(maximum)) - logMinimum) /
                        64.0;
    const double peakFrequency = 0.877 * static_cast<double>(kGravity) /
                                 static_cast<double>(significantWaveHeight);
    double sum = 0.0;
    for (uint32_t index = 0; index <= 64; ++index) {
        const double waveNumber =
            std::exp(logMinimum + static_cast<double>(index) * step);
        const double frequency =
            std::sqrt(static_cast<double>(kGravity) * waveNumber);
        const double ratio = frequency / std::max(peakFrequency, 1.0e-4);
        const double exponent = ratio <= 1.0
            ? 9.77 * std::pow(ratio, 5.0)
            : 9.77 * std::pow(ratio, -2.5);
        const float spreadExponent = static_cast<float>(
            std::max(0.5, exponent * 0.65));
        const double direction = static_cast<double>(directionalIntegral(
            spreadExponent, directionalBlend));
        const double sigma = frequency < peakFrequency ? 0.07 : 0.09;
        const double difference = frequency - peakFrequency;
        const double peakShape = std::exp(
            -(difference * difference) /
            (2.0 * sigma * sigma * peakFrequency * peakFrequency + 1.0e-4));
    const double peak = std::pow(
            static_cast<double>(peakEnhancement), peakShape);
        const double density =
            std::exp(-1.0 /
                     std::pow(waveNumber * static_cast<double>(lengthScale), 2.0)) *
            peak * direction / (waveNumber * waveNumber);
        sum += density * (index == 0 || index == 64 ? 0.5 : 1.0) * step;
    }
    return static_cast<float>(sum);
}

glm::vec2 complexMultiply(glm::vec2 a, glm::vec2 b) {
    return {a.x * b.x - a.y * b.y,
            a.x * b.y + a.y * b.x};
}

WGPUComputePipeline createComputePipeline(WGPUDevice device,
                                          WGPUPipelineLayout layout,
                                          WGPUShaderModule module,
                                          const char* entryPoint,
                                          const char* label) {
    WGPUComputePipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, label);
    desc.layout = layout;
    desc.compute.module = module;
    WGPU_SET_ENTRY_POINT(desc.compute, entryPoint);
    return wgpuDeviceCreateComputePipeline(device, &desc);
}

WaterSpectrumConfig sanitizeSpectrum(WaterSpectrumConfig config) {
    config.significantWaveHeight = std::clamp(
        config.significantWaveHeight, 0.1f, 100.0f);
    config.directionRadians = std::remainder(config.directionRadians, kTau);
    config.choppiness = std::clamp(config.choppiness, 0.0f, 5.0f);
    config.peakEnhancement = std::clamp(config.peakEnhancement, 0.05f, 10.0f);
    config.windAlignment = std::clamp(config.windAlignment, 0.0f, 1.0f);
    config.animationSpeed = std::clamp(config.animationSpeed, 0.0f, 5.0f);
    config.patchLengths.x = std::clamp(config.patchLengths.x, 64.0f, 8192.0f);
    config.patchLengths.y = std::clamp(config.patchLengths.y, 16.0f, 2048.0f);
    config.cascadeAmplitudes = glm::clamp(
        config.cascadeAmplitudes, glm::vec2(0.0f), glm::vec2(2.0f));
    config.directionalSineScale = std::clamp(
        config.directionalSineScale, 0.0f, 1.5f);
    return config;
}

bool spectrumShapeMatches(const WaterSpectrumConfig& a,
                          const WaterSpectrumConfig& b) {
    return a.significantWaveHeight == b.significantWaveHeight &&
           a.directionRadians == b.directionRadians &&
           a.peakEnhancement == b.peakEnhancement &&
           a.windAlignment == b.windAlignment &&
           a.patchLengths.x == b.patchLengths.x &&
           a.patchLengths.y == b.patchLengths.y;
}

} // namespace

WaterSimulation::~WaterSimulation() {
    shutdown();
}

bool WaterSimulation::init(WGPUDevice device, WGPUQueue queue,
                           const std::filesystem::path& shaderDirectory,
                           std::span<const uint16_t> terrainHeights,
                           uint32_t terrainWidth, uint32_t terrainHeight,
                           float terrainHeightScale, float cellScale,
                           float waterHeight,
                           const WaterSpectrumConfig& spectrum) {
    if (!device || !queue) {
        LOG_ERROR("WaterSimulation::init: device or queue is null");
        return false;
    }
    device_ = device;
    queue_ = queue;
    spectrumConfig_ = sanitizeSpectrum(spectrum);

    if (!createSpectrum() || !createBuffers() || !createOutputTexture() ||
        !createCoastField(terrainHeights, terrainWidth, terrainHeight,
                          terrainHeightScale, cellScale, waterHeight) ||
        !createPipelines(shaderDirectory) || !createBindGroups() ||
        !createFoamResources(shaderDirectory)) {
        LOG_ERROR("Failed to initialize FFT water simulation");
        shutdown();
        return false;
    }

    LOG_INFO("FFT water simulation initialized: {} cascades at {}x{}",
             CASCADE_COUNT, RESOLUTION, RESOLUTION);
    return true;
}

bool WaterSimulation::reconfigure(const WaterSpectrumConfig& spectrum) {
    const WaterSpectrumConfig next = sanitizeSpectrum(spectrum);
    const bool rebuildSpectrum = !spectrumShapeMatches(spectrumConfig_, next);
    const WaterSpectrumConfig previous = spectrumConfig_;
    spectrumConfig_ = next;
    cpuCacheTime_ = -1.0f;
    if (!rebuildSpectrum || !device_ || !queue_) return true;

    releaseSimulationBindGroups();
    if (!createSpectrum()) {
        spectrumConfig_ = previous;
        static_cast<void>(createBindGroups());
        LOG_ERROR("Failed to rebuild FFT water spectrum; keeping previous GPU data");
        return false;
    }
    if (!createBindGroups()) {
        LOG_ERROR("Failed to bind rebuilt FFT water spectrum");
        return false;
    }
    foamFrame_ = 0u;
    LOG_INFO("Rebuilt FFT water spectrum: Hs {:.2f}, direction {:.1f} degrees",
             spectrumConfig_.significantWaveHeight,
             spectrumConfig_.directionRadians * 180.0f / kPi);
    return true;
}

bool WaterSimulation::rebuildCoastField(
    std::span<const uint16_t> terrainHeights,
    uint32_t terrainWidth, uint32_t terrainHeight,
    float terrainHeightScale, float cellScale, float waterHeight) {
    if (!device_ || !queue_) return false;
    return createCoastField(terrainHeights, terrainWidth, terrainHeight,
                            terrainHeightScale, cellScale, waterHeight);
}

bool WaterSimulation::createCoastField(
    std::span<const uint16_t> terrainHeights,
    uint32_t terrainWidth, uint32_t terrainHeight,
    float terrainHeightScale, float cellScale, float waterHeight) {
    const bool validTerrain = terrainWidth > 1 && terrainHeight > 1 &&
        terrainHeights.size() == static_cast<size_t>(terrainWidth) * terrainHeight &&
        std::isfinite(terrainHeightScale) && terrainHeightScale > 0.0f &&
        std::isfinite(cellScale) && cellScale > 0.0f;

    uint32_t fieldWidth = 1;
    uint32_t fieldHeight = 1;
    std::vector<CoastPixel> pixels;

    if (!validTerrain) {
        // Tests and tools without terrain still receive a valid, entirely
        // offshore field. A large distance gives the FFT path full weight.
        pixels.push_back(packCoastPixel(glm::normalize(glm::vec2(0.91f, 0.414f)),
                                        4096.0f, 64.0f));
    } else {
        fieldWidth = std::min(COAST_FIELD_RESOLUTION, terrainWidth);
        fieldHeight = std::min(COAST_FIELD_RESOLUTION, terrainHeight);
        const size_t fieldSize = static_cast<size_t>(fieldWidth) * fieldHeight;
        std::vector<float> depths(fieldSize);
        std::vector<uint8_t> landMask(fieldSize, 0u);
        std::vector<int32_t> nearestX(fieldSize, -1);
        std::vector<int32_t> nearestY(fieldSize, -1);
        bool hasLand = false;

        for (uint32_t y = 0; y < fieldHeight; ++y) {
            const uint32_t sourceY = static_cast<uint32_t>(std::lround(
                static_cast<double>(y) * (terrainHeight - 1) /
                std::max(fieldHeight - 1u, 1u)));
            for (uint32_t x = 0; x < fieldWidth; ++x) {
                const uint32_t sourceX = static_cast<uint32_t>(std::lround(
                    static_cast<double>(x) * (terrainWidth - 1) /
                    std::max(fieldWidth - 1u, 1u)));
                const float normalized = static_cast<float>(
                    terrainHeights[static_cast<size_t>(sourceY) * terrainWidth + sourceX]) /
                    65535.0f;
                const float terrainWorld = (normalized * 2.0f - 1.0f) *
                                           terrainHeightScale;
                const size_t index = static_cast<size_t>(y) * fieldWidth + x;
                depths[index] = std::max(waterHeight - terrainWorld, 0.0f);
                if (terrainWorld >= waterHeight) {
                    landMask[index] = 1u;
                    nearestX[index] = static_cast<int32_t>(x);
                    nearestY[index] = static_cast<int32_t>(y);
                    hasLand = true;
                }
            }
        }

        const float worldStepX = static_cast<float>(terrainWidth - 1) * cellScale /
                                 static_cast<float>(std::max(fieldWidth - 1u, 1u));
        const float worldStepY = static_cast<float>(terrainHeight - 1) * cellScale /
                                 static_cast<float>(std::max(fieldHeight - 1u, 1u));
        const auto consider = [&](uint32_t x, uint32_t y, int32_t candidateX,
                                  int32_t candidateY) {
            if (candidateX < 0 || candidateY < 0) return;
            const size_t index = static_cast<size_t>(y) * fieldWidth + x;
            const auto distanceSquared = [&](int32_t sourceX, int32_t sourceY) {
                if (sourceX < 0 || sourceY < 0) {
                    return std::numeric_limits<float>::infinity();
                }
                const float dx = (static_cast<float>(sourceX) -
                                  static_cast<float>(x)) * worldStepX;
                const float dy = (static_cast<float>(sourceY) -
                                  static_cast<float>(y)) * worldStepY;
                return dx * dx + dy * dy;
            };
            if (distanceSquared(candidateX, candidateY) <
                distanceSquared(nearestX[index], nearestY[index])) {
                nearestX[index] = candidateX;
                nearestY[index] = candidateY;
            }
        };

        if (hasLand) {
            // Two Euclidean chamfer sweeps propagate the nearest land sample.
            // Unlike a local height gradient this remains stable through flat
            // shallows and naturally points around islands and into bays.
            for (uint32_t y = 0; y < fieldHeight; ++y) {
                for (uint32_t x = 0; x < fieldWidth; ++x) {
                    if (x > 0) {
                        const size_t i = static_cast<size_t>(y) * fieldWidth + x - 1;
                        consider(x, y, nearestX[i], nearestY[i]);
                    }
                    if (y > 0) {
                        for (int32_t ox = -1; ox <= 1; ++ox) {
                            const int32_t nx = static_cast<int32_t>(x) + ox;
                            if (nx < 0 || nx >= static_cast<int32_t>(fieldWidth)) continue;
                            const size_t i = static_cast<size_t>(y - 1) * fieldWidth +
                                             static_cast<uint32_t>(nx);
                            consider(x, y, nearestX[i], nearestY[i]);
                        }
                    }
                }
            }
            for (uint32_t y = fieldHeight; y-- > 0;) {
                for (uint32_t x = fieldWidth; x-- > 0;) {
                    if (x + 1 < fieldWidth) {
                        const size_t i = static_cast<size_t>(y) * fieldWidth + x + 1;
                        consider(x, y, nearestX[i], nearestY[i]);
                    }
                    if (y + 1 < fieldHeight) {
                        for (int32_t ox = -1; ox <= 1; ++ox) {
                            const int32_t nx = static_cast<int32_t>(x) + ox;
                            if (nx < 0 || nx >= static_cast<int32_t>(fieldWidth)) continue;
                            const size_t i = static_cast<size_t>(y + 1) * fieldWidth +
                                             static_cast<uint32_t>(nx);
                            consider(x, y, nearestX[i], nearestY[i]);
                        }
                    }
                }
            }
        }

        // Directional land shadow. Waves travel along incomingDirection, so
        // each water cell inherits occlusion from the up-wave column. Energy
        // recovers over distance to approximate diffraction around headlands.
        const glm::vec2 incomingDirection = glm::normalize(glm::vec2(0.91f, 0.414f));
        std::vector<float> waveShadow(fieldSize, 0.0f);
        constexpr float shelterRecoveryDistance = 900.0f;
        const float advanceDistance = worldStepX /
            std::max(std::abs(incomingDirection.x), 0.01f);
        const float shadowDecay = advanceDistance / shelterRecoveryDistance;
        for (uint32_t x = 0; x < fieldWidth; ++x) {
            for (uint32_t y = 0; y < fieldHeight; ++y) {
                const size_t index = static_cast<size_t>(y) * fieldWidth + x;
                if (landMask[index] != 0u) {
                    waveShadow[index] = 1.0f;
                    continue;
                }
                if (x == 0u) continue;

                const float upstreamY = static_cast<float>(y) -
                    incomingDirection.y / incomingDirection.x;
                if (upstreamY < 0.0f ||
                    upstreamY > static_cast<float>(fieldHeight - 1u)) {
                    continue;
                }
                const uint32_t y0 = static_cast<uint32_t>(std::floor(upstreamY));
                const uint32_t y1 = std::min(y0 + 1u, fieldHeight - 1u);
                const float fraction = upstreamY - static_cast<float>(y0);
                const float upstreamShadow = std::lerp(
                    waveShadow[static_cast<size_t>(y0) * fieldWidth + x - 1u],
                    waveShadow[static_cast<size_t>(y1) * fieldWidth + x - 1u],
                    fraction);
                waveShadow[index] = std::max(upstreamShadow - shadowDecay, 0.0f);
            }
        }

        pixels.resize(fieldSize);
        const float halfCell = 0.5f * std::sqrt(worldStepX * worldStepX +
                                               worldStepY * worldStepY);
        for (uint32_t y = 0; y < fieldHeight; ++y) {
            for (uint32_t x = 0; x < fieldWidth; ++x) {
                const size_t index = static_cast<size_t>(y) * fieldWidth + x;
                glm::vec2 direction = glm::normalize(incomingDirection);
                float distance = 4096.0f;
                if (nearestX[index] >= 0) {
                    const glm::vec2 delta(
                        static_cast<float>(nearestX[index] - static_cast<int32_t>(x)) *
                            worldStepX,
                        static_cast<float>(nearestY[index] - static_cast<int32_t>(y)) *
                            worldStepY);
                    const float centerDistance = glm::length(delta);
                    distance = std::max(centerDistance - halfCell, 0.0f);
                    if (centerDistance > 1e-4f) direction = delta / centerDistance;
                }
                const float exposure = 1.0f - waveShadow[index];
                pixels[index] = packCoastPixel(direction * exposure,
                                               distance, depths[index]);
            }
        }
    }

    gpu::TextureDesc desc = gpu::TextureDesc::tex2D(
        fieldWidth, fieldHeight, WGPUTextureFormat_RGBA16Float,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        "water_coast_field");
    WGPUTexture nextTexture = gpu::createTextureWithData(
        device_, queue_, desc, std::as_bytes(std::span<const CoastPixel>(pixels)),
        fieldWidth * sizeof(CoastPixel));
    if (!nextTexture) return false;
    WGPUTextureView nextView = gpu::createTextureView(nextTexture);
    if (!nextView) {
        wgpuTextureRelease(nextTexture);
        return false;
    }

    if (coastView_) wgpuTextureViewRelease(coastView_);
    if (coastTexture_) wgpuTextureRelease(coastTexture_);
    coastTexture_ = nextTexture;
    coastView_ = nextView;

    LOG_INFO("Coastal refraction field initialized: {}x{}", fieldWidth, fieldHeight);
    return true;
}

bool WaterSimulation::createSpectrum() {
    std::vector<std::complex<float>> h0(kElementCount);
    std::array<float, CASCADE_COUNT> minimum{};
    std::array<float, CASCADE_COUNT> maximum{};
    std::array<float, CASCADE_COUNT> lowCutoff{};
    std::array<float, CASCADE_COUNT> highCutoff{};
    std::array<float, CASCADE_COUNT> normalization{};
    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        minimum[cascade] = kTau /
                           cascadeValue(spectrumConfig_.patchLengths, cascade);
        maximum[cascade] = kPi * static_cast<float>(RESOLUTION) /
                           cascadeValue(spectrumConfig_.patchLengths, cascade);
    }

    const float lengthScale = spectrumConfig_.significantWaveHeight *
                              spectrumConfig_.significantWaveHeight / kGravity;
    const float directionalBlend = 0.07f + 0.93f * spectrumConfig_.windAlignment;
    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        lowCutoff[cascade] = cascade > 0
            ? std::sqrt(maximum[cascade - 1] * minimum[cascade])
            : minimum[cascade];
        highCutoff[cascade] = cascade + 1 < CASCADE_COUNT
            ? std::sqrt(maximum[cascade] * minimum[cascade + 1])
            : maximum[cascade];
        const float own = integrateSpectrum(
            minimum[cascade], maximum[cascade], lengthScale,
            directionalBlend, spectrumConfig_.significantWaveHeight,
            spectrumConfig_.peakEnhancement);
        const float band = integrateSpectrum(
            lowCutoff[cascade], highCutoff[cascade], lengthScale,
            directionalBlend, spectrumConfig_.significantWaveHeight,
            spectrumConfig_.peakEnhancement);
        normalization[cascade] = band > 1.0e-30f
            ? std::sqrt(own / band) : 1.0f;
    }

    const float peakFrequency = 0.877f * kGravity /
                                spectrumConfig_.significantWaveHeight;
    const glm::vec2 dominantDirection{
        std::cos(spectrumConfig_.directionRadians),
        std::sin(spectrumConfig_.directionRadians)};

    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        const float patchLength =
            cascadeValue(spectrumConfig_.patchLengths, cascade);
        const float deltaK = kTau / patchLength;
        const float seed = static_cast<float>(cascade + 1u);
        for (uint32_t y = 0; y < RESOLUTION; ++y) {
            for (uint32_t x = 0; x < RESOLUTION; ++x) {
                const uint32_t localIndex = y * RESOLUTION + x;
                const uint32_t index = cascade * RESOLUTION * RESOLUTION +
                                       localIndex;
                const float kx =
                    (static_cast<float>(x) - 0.5f * RESOLUTION) * deltaK;
                const float kz =
                    (static_cast<float>(y) - 0.5f * RESOLUTION) * deltaK;
                const float magnitude =
                    std::max(std::sqrt(kx * kx + kz * kz), 1.0e-4f);
                const float alignment =
                    (kx * dominantDirection.x + kz * dominantDirection.y) /
                    magnitude;
                const float frequency = std::sqrt(kGravity * magnitude);
                const float ratio = frequency /
                    std::max(peakFrequency, 1.0e-4f);
                const float spreadingExponent = std::max(
                    (ratio <= 1.0f ? 9.77f * std::pow(ratio, 5.0f)
                                   : 9.77f * std::pow(ratio, -2.5f)) * 0.65f,
                    0.5f);
                const float directionLobe = std::pow(
                    std::sqrt(std::max((alignment + 1.0f) * 0.5f,
                                       1.0e-4f)),
                    spreadingExponent * 2.0f);
                const float directionWeight = directionLobe *
                    (directionalBlend +
                     (1.0f - directionalBlend) * (alignment + 1.0f) * 0.5f);
                const float magnitude2 = magnitude * magnitude;
                const float base =
                    std::exp(-1.0f /
                             (magnitude2 * lengthScale * lengthScale)) /
                    (magnitude2 * magnitude2) * directionWeight /
                    (patchLength * patchLength);
                const float sigma = frequency < peakFrequency ? 0.07f : 0.09f;
                const float difference = frequency - peakFrequency;
                const float peakShape = std::exp(
                    -(difference * difference) /
                    (2.0f * std::pow(sigma * peakFrequency, 2.0f) + 1.0e-4f));
                const float peaked = base *
                    std::pow(spectrumConfig_.peakEnhancement, peakShape);
                const float window =
                    smoothTransition(lowCutoff[cascade],
                                     lowCutoff[cascade] * 1.5f, magnitude) *
                    (1.0f - smoothTransition(highCutoff[cascade] / 1.5f,
                                             highCutoff[cascade], magnitude));
                const float density = std::max(peaked * window, 0.0f);
                const float randomIndex = static_cast<float>(localIndex) +
                                          seed * 100000.0f;
                const float randomA =
                    std::max(spectrumHash(randomIndex), 1.0e-4f);
                const float randomB = spectrumHash(randomIndex + 1000.0f);
                const float gaussianRadius =
                    std::sqrt(-2.0f * std::log(randomA));
                const float gaussianAngle = kTau * randomB;
                const float magnitudeScale = gaussianRadius *
                    std::sqrt(density) * 0.707107f * normalization[cascade];
                h0[index] = {
                    magnitudeScale * std::cos(gaussianAngle),
                    magnitudeScale * std::sin(gaussianAngle)};
            }
        }
    }

    struct CpuModeCandidate {
        float energy = 0.0f;
        CpuWaveMode mode;
    };
    std::vector<CpuModeCandidate> candidates;
    candidates.reserve(kElementCount / 2);
    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        const uint32_t base = cascade * RESOLUTION * RESOLUTION;
        const float patchLength =
            cascadeValue(spectrumConfig_.patchLengths, cascade);
        const float deltaK = kTau / patchLength;
        for (uint32_t y = 0; y < RESOLUTION; ++y) {
            for (uint32_t x = 0; x < RESOLUTION; ++x) {
                const uint32_t mirrorX = (RESOLUTION - x) % RESOLUTION;
                const uint32_t mirrorY = (RESOLUTION - y) % RESOLUTION;
                const uint32_t index = base + y * RESOLUTION + x;
                const uint32_t mirror = base + mirrorY * RESOLUTION + mirrorX;
                if (index >= mirror) continue;

                const glm::vec2 waveVector =
                    (glm::vec2(static_cast<float>(x), static_cast<float>(y)) -
                     glm::vec2(0.5f * RESOLUTION)) * deltaK;
                const float waveNumber = glm::length(waveVector);
                if (waveNumber < 1e-5f) continue;

                const float energy = std::norm(h0[index]) + std::norm(h0[mirror]);
                candidates.push_back({
                    energy,
                    CpuWaveMode{
                        waveVector,
                        glm::vec2(-0.5f * patchLength /
                                  static_cast<float>(RESOLUTION)),
                        glm::vec2(h0[index].real(), h0[index].imag()),
                        glm::vec2(std::conj(h0[mirror]).real(),
                                  std::conj(h0[mirror]).imag()),
                        std::round(std::sqrt(kGravity * (waveNumber + 1.0e-4f)) /
                                   kFrequencyTick) * kFrequencyTick,
                        cascadeValue(spectrumConfig_.cascadeAmplitudes, cascade),
                        cascade}});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const CpuModeCandidate& a, const CpuModeCandidate& b) {
                  return a.energy > b.energy;
              });
    const size_t modeCount = std::min(kCpuWaveModeCount, candidates.size());
    cpuWaveModes_.clear();
    cpuWaveModes_.reserve(modeCount);
    for (size_t index = 0; index < modeCount; ++index) {
        cpuWaveModes_.push_back(candidates[index].mode);
    }
    cpuEvolvedHeight_.resize(modeCount);
    cpuEvolvedVelocity_.resize(modeCount);
    cpuCacheTime_ = -1.0f;

    std::vector<WaveData> packed(kElementCount);
    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        const uint32_t base = cascade * RESOLUTION * RESOLUTION;
        for (uint32_t y = 0; y < RESOLUTION; ++y) {
            for (uint32_t x = 0; x < RESOLUTION; ++x) {
                const uint32_t mirrorX = (RESOLUTION - x) % RESOLUTION;
                const uint32_t mirrorY = (RESOLUTION - y) % RESOLUTION;
                const uint32_t index = base + y * RESOLUTION + x;
                const uint32_t mirror = base + mirrorY * RESOLUTION + mirrorX;
                const auto conjugateMirror = std::conj(h0[mirror]);
                packed[index].height = {
                    h0[index].real() + conjugateMirror.real(),
                    h0[index].imag() - conjugateMirror.imag()};
                packed[index].displacementX = {
                    h0[index].imag() + conjugateMirror.imag(),
                    -h0[index].real() + conjugateMirror.real()
                };
                const glm::vec2 k =
                    (glm::vec2(static_cast<float>(x), static_cast<float>(y)) -
                     glm::vec2(0.5f * RESOLUTION)) *
                    (kTau / cascadeValue(
                        spectrumConfig_.patchLengths, cascade));
                const float magnitude = glm::length(k) + 1.0e-4f;
                packed[index].displacementZ = k / magnitude;
                packed[index].padding = {
                    std::round(std::sqrt(kGravity * magnitude) /
                               kFrequencyTick) * kFrequencyTick,
                    0.0f
                };
            }
        }
    }

    const uint64_t byteSize = packed.size() * sizeof(WaveData);
    auto desc = gpu::BufferDesc::storage(byteSize, true, "water_initial_spectrum");
    WGPUBuffer nextSpectrum = gpu::createBufferWithData(
        device_, queue_, desc, std::span<const WaveData>(packed));
    if (!nextSpectrum) return false;
    if (initialSpectrumBuffer_) wgpuBufferRelease(initialSpectrumBuffer_);
    initialSpectrumBuffer_ = nextSpectrum;
    return true;
}

void WaterSimulation::updateCpuWaveCache(float timeSeconds) const {
    if (cpuCacheTime_ == timeSeconds) return;
    cpuCacheTime_ = timeSeconds;
    for (size_t index = 0; index < cpuWaveModes_.size(); ++index) {
        const CpuWaveMode& mode = cpuWaveModes_[index];
        const float angle = -mode.angularFrequency * timeSeconds;
        const glm::vec2 positive(std::cos(angle), std::sin(angle));
        const glm::vec2 negative(positive.x, -positive.y);
        const glm::vec2 positivePart = complexMultiply(mode.initialPositive, positive);
        const glm::vec2 negativePart = complexMultiply(mode.conjugateNegative, negative);
        cpuEvolvedHeight_[index] = positivePart + negativePart;
        cpuEvolvedVelocity_[index] = {
            mode.angularFrequency * (positivePart.y - negativePart.y),
            mode.angularFrequency * (-positivePart.x + negativePart.x)};
    }
}

WaterSimulation::SurfaceSample WaterSimulation::sampleSurface(
    glm::vec2 worldPosition, float timeSeconds, float strength) const {
    SurfaceSample sample;
    if (!std::isfinite(timeSeconds)) return sample;
    updateCpuWaveCache(timeSeconds * spectrumConfig_.animationSpeed);

    for (size_t index = 0; index < cpuWaveModes_.size(); ++index) {
        const CpuWaveMode& mode = cpuWaveModes_[index];
        const float spatialAngle = glm::dot(
            mode.waveVector, worldPosition + mode.sampleOffset);
        const glm::vec2 spatialPhase(std::cos(spatialAngle), std::sin(spatialAngle));
        const glm::vec2 spatialHeight =
            complexMultiply(cpuEvolvedHeight_[index], spatialPhase);
        const glm::vec2 spatialVelocity =
            complexMultiply(cpuEvolvedVelocity_[index], spatialPhase);
        const float waveNumber = glm::length(mode.waveVector);
        const float modeAmplitude = cascadeValue(
            spectrumConfig_.cascadeAmplitudes, mode.cascade);

        sample.heightOffset -= 2.0f * spatialHeight.x * modeAmplitude;
        sample.slope += 2.0f * spatialHeight.y * mode.waveVector *
                        modeAmplitude;
        sample.velocity.y -= 2.0f * spatialVelocity.x * modeAmplitude *
                             spectrumConfig_.animationSpeed;
        if (waveNumber > 1e-5f) {
            const glm::vec2 horizontal = mode.waveVector / waveNumber
                                       * (2.0f * mode.angularFrequency
                                          * spatialHeight.x * modeAmplitude *
                                          spectrumConfig_.choppiness *
                                          spectrumConfig_.animationSpeed);
            sample.velocity.x += horizontal.x;
            sample.velocity.z += horizontal.y;
        }
    }

    struct LongWave {
        glm::vec2 direction;
        float amplitude;
        float wavelength;
        float phase;
        float omega;
    };
    constexpr std::array<LongWave, 4> longWaves{{
        {{ 0.923059017f, 0.384658357f}, 5.1541f,  440.298507f,
         0.0f,       0.374291312f},
        {{ 0.700400636f, 0.713749921f}, 5.1541f,  701.258144f,
         5.553108549f, 0.296825282f},
        {{ 0.367164395f, 0.930156066f}, 5.1541f, 1116.885424f,
         4.823031791f, 0.234699061f},
        {{-0.024039031f, 0.999711021f}, 5.1541f, 1778.85f,
         4.092955033f, 0.186378666f},
    }};
    for (const LongWave& wave : longWaves) {
        const float waveNumber = kTau / (wave.wavelength + 1.0e-4f);
        const float phase = waveNumber * glm::dot(wave.direction, worldPosition) -
                            wave.omega * timeSeconds + wave.phase;
        const float sine = std::sin(phase);
        const float cosine = std::cos(phase);
        sample.heightOffset += wave.amplitude * cosine;
        sample.slope -= wave.direction *
                        (waveNumber * wave.amplitude * sine);
        sample.velocity.y += wave.amplitude * wave.omega * sine;
        const glm::vec2 horizontalVelocity = wave.direction *
            (wave.amplitude * wave.omega * cosine);
        sample.velocity.x += horizontalVelocity.x;
        sample.velocity.z += horizontalVelocity.y;
    }

    const float amplitude = std::clamp(strength, 0.0f, 2.0f);
    sample.heightOffset *= amplitude;
    sample.slope *= amplitude;
    sample.velocity *= amplitude;
    return sample;
}

bool WaterSimulation::createBuffers() {
    const uint64_t byteSize = static_cast<uint64_t>(kElementCount) * sizeof(WaveData);
    pongBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(byteSize, false, "water_fft_pong"));
    simulationUniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(sizeof(SimParams), "water_sim_params"));
    if (!pongBuffer_ || !simulationUniformBuffer_) {
        return false;
    }

    // All 256-point FFT workgroups use the same 255 radix-2 twiddles. Baking
    // them once avoids millions of repeated sin/cos evaluations per frame.
    std::array<glm::vec2, RESOLUTION - 1> twiddles{};
    for (uint32_t stage = 0; stage < FFT_STAGE_COUNT; ++stage) {
        const uint32_t halfSpan = 1u << stage;
        const uint32_t span = halfSpan << 1u;
        const uint32_t offset = halfSpan - 1u;
        for (uint32_t j = 0; j < halfSpan; ++j) {
            const float angle = 2.0f * std::numbers::pi_v<float> *
                                static_cast<float>(j) /
                                static_cast<float>(span);
            twiddles[offset + j] = {std::cos(angle), std::sin(angle)};
        }
    }
    auto twiddleDesc = gpu::BufferDesc::storage(
        sizeof(twiddles), true, "water_fft_twiddles");
    fftTwiddleBuffer_ = gpu::createBufferWithData(
        device_, queue_, twiddleDesc,
        std::span<const glm::vec2>(twiddles));
    if (!fftTwiddleBuffer_) {
        return false;
    }

    for (uint32_t axis = 0; axis < axisUniformBuffers_.size(); ++axis) {
        SimParams params{.time = 0.0f, .stage = 0, .axis = axis, .size = RESOLUTION};
        auto desc = gpu::BufferDesc::uniform(sizeof(SimParams), "water_fft_axis_params");
        axisUniformBuffers_[axis] = gpu::createBufferWithData(
            device_, queue_, desc, std::span<const SimParams>(&params, 1));
        if (!axisUniformBuffers_[axis]) {
            return false;
        }
    }
    return true;
}

bool WaterSimulation::createOutputTexture() {
    gpu::TextureDesc desc = gpu::TextureDesc::storage(
        RESOLUTION, RESOLUTION, WGPUTextureFormat_RGBA16Float,
        "water_displacement_cascades");
    desc.depthOrArrayLayers = OUTPUT_LAYER_COUNT;
    outputTexture_ = gpu::createTexture(device_, desc);
    if (!outputTexture_) return false;

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "water_displacement_cascades_view";
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.arrayLayerCount = OUTPUT_LAYER_COUNT;
    outputView_ = gpu::createTextureView(outputTexture_, viewDesc);
    if (!outputView_) return false;

    auto samplerDesc = gpu::SamplerDesc::linear("water_displacement_sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    sampler_ = gpu::createSampler(device_, samplerDesc);
    return sampler_ != nullptr;
}

bool WaterSimulation::createPipelines(const std::filesystem::path& shaderDirectory) {
    fftShader_ = gpu::loadShaderModule(device_, shaderDirectory / "water_fft.wgsl",
                                       "water_fft.wgsl");
    if (!fftShader_) return false;

    std::array<gpu::BindGroupLayoutEntry, 4> fftEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().uniformBuffer(false, sizeof(SimParams)),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(2).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(3).computeVisible().storageBuffer(true),
    };
    fftBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, fftEntries, "water_fft_bind_group_layout");
    if (!fftBindGroupLayout_) return false;
    std::array<WGPUBindGroupLayout, 1> fftLayouts = {fftBindGroupLayout_};
    fftPipelineLayout_ = gpu::createPipelineLayout(
        device_, fftLayouts, "water_fft_pipeline_layout");
    if (!fftPipelineLayout_) return false;

    evolvePipeline_ = createComputePipeline(device_, fftPipelineLayout_, fftShader_,
                                            "evolve", "water_spectrum_evolve");
    fftPipeline_ = createComputePipeline(device_, fftPipelineLayout_, fftShader_,
                                         "fftAxis", "water_inverse_fft");
    if (!evolvePipeline_ || !fftPipeline_) return false;

    finalizeShader_ = gpu::loadShaderModule(device_, shaderDirectory / "water_finalize.wgsl",
                                            "water_finalize.wgsl");
    if (!finalizeShader_) return false;
    std::array<gpu::BindGroupLayoutEntry, 3> finalizeEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageTexture(
            WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RGBA16Float,
            WGPUTextureViewDimension_2DArray),
        gpu::BindGroupLayoutEntry(2).computeVisible().uniformBuffer(
            false, sizeof(SimParams)),
    };
    finalizeBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, finalizeEntries, "water_finalize_bind_group_layout");
    if (!finalizeBindGroupLayout_) return false;
    std::array<WGPUBindGroupLayout, 1> finalizeLayouts = {finalizeBindGroupLayout_};
    finalizePipelineLayout_ = gpu::createPipelineLayout(
        device_, finalizeLayouts, "water_finalize_pipeline_layout");
    if (!finalizePipelineLayout_) return false;
    finalizePipeline_ = createComputePipeline(device_, finalizePipelineLayout_, finalizeShader_,
                                              "main", "water_fft_finalize");
    return finalizePipeline_ != nullptr;
}

bool WaterSimulation::createBindGroups() {
    releaseSimulationBindGroups();
    const uint64_t byteSize = static_cast<uint64_t>(kElementCount) * sizeof(WaveData);
    std::array<gpu::BindGroupEntry, 4> evolveEntries = {
        gpu::BindGroupEntry(0).buffer(simulationUniformBuffer_, 0, sizeof(SimParams)),
        gpu::BindGroupEntry(1).buffer(initialSpectrumBuffer_, 0, byteSize),
        gpu::BindGroupEntry(2).buffer(pongBuffer_, 0, byteSize),
        gpu::BindGroupEntry(3).buffer(
            fftTwiddleBuffer_, 0, (RESOLUTION - 1u) * sizeof(glm::vec2)),
    };
    evolveBindGroup_ = gpu::createBindGroup(
        device_, fftBindGroupLayout_, evolveEntries, "water_evolve_bind_group");
    if (!evolveBindGroup_) return false;

    for (uint32_t axis = 0; axis < fftAxisBindGroups_.size(); ++axis) {
        std::array<gpu::BindGroupEntry, 4> entries = {
            gpu::BindGroupEntry(0).buffer(axisUniformBuffers_[axis], 0, sizeof(SimParams)),
            gpu::BindGroupEntry(1).buffer(initialSpectrumBuffer_, 0, byteSize),
            gpu::BindGroupEntry(2).buffer(pongBuffer_, 0, byteSize),
            gpu::BindGroupEntry(3).buffer(
                fftTwiddleBuffer_, 0, (RESOLUTION - 1u) * sizeof(glm::vec2)),
        };
        fftAxisBindGroups_[axis] = gpu::createBindGroup(
            device_, fftBindGroupLayout_, entries, "water_fft_axis_bind_group");
        if (!fftAxisBindGroups_[axis]) return false;
    }

    std::array<gpu::BindGroupEntry, 3> finalizeEntries = {
        gpu::BindGroupEntry(0).buffer(pongBuffer_, 0, byteSize),
        gpu::BindGroupEntry(1).textureView(outputView_),
        gpu::BindGroupEntry(2).buffer(
            simulationUniformBuffer_, 0, sizeof(SimParams)),
    };
    finalizeBindGroup_ = gpu::createBindGroup(
        device_, finalizeBindGroupLayout_, finalizeEntries,
        "water_finalize_bind_group");
    return finalizeBindGroup_ != nullptr;
}

void WaterSimulation::releaseSimulationBindGroups() {
    if (finalizeBindGroup_) {
        wgpuBindGroupRelease(finalizeBindGroup_);
        finalizeBindGroup_ = nullptr;
    }
    for (auto& group : fftAxisBindGroups_) {
        if (group) wgpuBindGroupRelease(group);
        group = nullptr;
    }
    if (evolveBindGroup_) {
        wgpuBindGroupRelease(evolveBindGroup_);
        evolveBindGroup_ = nullptr;
    }
}

bool WaterSimulation::createFoamResources(
    const std::filesystem::path& shaderDirectory) {
    struct FoamParams {
        float deltaTime;
        float time;
        glm::vec2 padding;
    };
    static_assert(sizeof(FoamParams) == 16);

    const uint64_t foamByteSize = static_cast<uint64_t>(RESOLUTION) *
                                  RESOLUTION * sizeof(float);
    std::vector<float> zeros(RESOLUTION * RESOLUTION, 0.0f);
    for (uint32_t i = 0; i < foamBuffers_.size(); ++i) {
        auto desc = gpu::BufferDesc::storage(foamByteSize, false, "water_foam_history");
        foamBuffers_[i] = gpu::createBufferWithData(
            device_, queue_, desc, std::span<const float>(zeros));
        if (!foamBuffers_[i]) return false;
    }
    foamUniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(sizeof(FoamParams), "water_foam_params"));
    if (!foamUniformBuffer_) return false;

    auto textureDesc = gpu::TextureDesc::storage(
        RESOLUTION, RESOLUTION, WGPUTextureFormat_RGBA16Float, "water_foam_texture");
    foamTexture_ = gpu::createTexture(device_, textureDesc);
    if (!foamTexture_) return false;
    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "water_foam_texture_view";
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    foamView_ = gpu::createTextureView(foamTexture_, viewDesc);
    if (!foamView_) return false;

    foamShader_ = gpu::loadShaderModule(device_, shaderDirectory / "water_foam.wgsl",
                                        "water_foam.wgsl");
    if (!foamShader_) return false;
    std::array<gpu::BindGroupLayoutEntry, 6> entries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().uniformBuffer(false, sizeof(FoamParams)),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(2).computeVisible().storageBuffer(false),
        gpu::BindGroupLayoutEntry(3).computeVisible().texture(
            WGPUTextureSampleType_Float, WGPUTextureViewDimension_2DArray, false),
        gpu::BindGroupLayoutEntry(4).computeVisible().sampler(WGPUSamplerBindingType_Filtering),
        gpu::BindGroupLayoutEntry(5).computeVisible().storageTexture(
            WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RGBA16Float,
            WGPUTextureViewDimension_2D),
    };
    foamBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "water_foam_bind_group_layout");
    if (!foamBindGroupLayout_) return false;
    std::array<WGPUBindGroupLayout, 1> layouts = {foamBindGroupLayout_};
    foamPipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "water_foam_pipeline_layout");
    if (!foamPipelineLayout_) return false;
    foamPipeline_ = createComputePipeline(device_, foamPipelineLayout_, foamShader_,
                                          "main", "water_foam_update");
    if (!foamPipeline_) return false;

    for (uint32_t parity = 0; parity < 2; ++parity) {
        std::array<gpu::BindGroupEntry, 6> groupEntries = {
            gpu::BindGroupEntry(0).buffer(foamUniformBuffer_, 0, sizeof(FoamParams)),
            gpu::BindGroupEntry(1).buffer(foamBuffers_[parity], 0, foamByteSize),
            gpu::BindGroupEntry(2).buffer(foamBuffers_[1u - parity], 0, foamByteSize),
            gpu::BindGroupEntry(3).textureView(outputView_),
            gpu::BindGroupEntry(4).sampler(sampler_),
            gpu::BindGroupEntry(5).textureView(foamView_),
        };
        foamBindGroups_[parity] = gpu::createBindGroup(
            device_, foamBindGroupLayout_, groupEntries, "water_foam_bind_group");
        if (!foamBindGroups_[parity]) return false;
    }
    return true;
}

void WaterSimulation::update(WGPUCommandEncoder encoder, float timeSeconds,
                             WGPUQuerySet timestampQuerySet,
                             uint32_t timestampBegin,
                             uint32_t timestampEnd) {
    if (!isInitialized() || !encoder) return;

    // The display on this target retires at 85 Hz. Running the complete 256²
    // FFT hundreds of times between visible scans cannot add image detail, so
    // keep its physical state at a conservative 120 Hz while the analytic long
    // swells, material, refraction, foam and presentation continue every frame.
    // This is a simulation-rate decoupling, never a spatial-resolution change.
    float elapsed = timeSeconds - lastUpdateTime_;
    if (elapsed < 0.0f) elapsed += 4096.0f;
    if (foamFrame_ != 0u && elapsed < 1.0f / SPECTRAL_UPDATE_HZ) {
        if (timestampQuerySet) {
            WGPUComputePassDescriptor idlePassDesc{};
            WGPU_SET_LABEL(idlePassDesc, "water_fft_idle_pass");
            gpu::CompatPassTimestampWrites idleTimestampWrites{};
            idleTimestampWrites.querySet = timestampQuerySet;
            idleTimestampWrites.beginningOfPassWriteIndex = timestampBegin;
            idleTimestampWrites.endOfPassWriteIndex = timestampEnd;
            idlePassDesc.timestampWrites = &idleTimestampWrites;
            WGPUComputePassEncoder idlePass =
                wgpuCommandEncoderBeginComputePass(encoder, &idlePassDesc);
            wgpuComputePassEncoderEnd(idlePass);
            wgpuComputePassEncoderRelease(idlePass);
        }
        return;
    }

    SimParams params{
        .time = timeSeconds * spectrumConfig_.animationSpeed,
        .stage = 0,
        .axis = 0,
        .size = RESOLUTION,
        .patchLengths = spectrumConfig_.patchLengths,
        .cascadeAmplitudes = spectrumConfig_.cascadeAmplitudes,
        .choppiness = spectrumConfig_.choppiness,
        .directionalSineScale = spectrumConfig_.directionalSineScale,
    };
    gpu::writeBuffer(queue_, simulationUniformBuffer_, 0, params);

    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "water_fft_compute_pass");
    gpu::CompatPassTimestampWrites timestampWrites{};
    if (timestampQuerySet) {
        timestampWrites.querySet = timestampQuerySet;
        timestampWrites.beginningOfPassWriteIndex = timestampBegin;
        timestampWrites.endOfPassWriteIndex = timestampEnd;
        passDesc.timestampWrites = &timestampWrites;
    }
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    constexpr uint32_t evolveGroups = (kElementCount + 255u) / 256u;
    wgpuComputePassEncoderSetPipeline(pass, evolvePipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, evolveBindGroup_, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, evolveGroups, 1, 1);

    wgpuComputePassEncoderSetPipeline(pass, fftPipeline_);
    for (WGPUBindGroup bindGroup : fftAxisBindGroups_) {
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass, RESOLUTION * CASCADE_COUNT, 1, 1);
    }

    wgpuComputePassEncoderSetPipeline(pass, finalizePipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeBindGroup_, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(
        pass, (RESOLUTION + 7u) / 8u, (RESOLUTION + 7u) / 8u, CASCADE_COUNT);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    lastUpdateTime_ = timeSeconds;
    ++foamFrame_;
}

void WaterSimulation::shutdown() {
    for (auto& group : foamBindGroups_) if (group) wgpuBindGroupRelease(group);
    if (foamPipeline_) wgpuComputePipelineRelease(foamPipeline_);
    if (foamPipelineLayout_) wgpuPipelineLayoutRelease(foamPipelineLayout_);
    if (foamBindGroupLayout_) wgpuBindGroupLayoutRelease(foamBindGroupLayout_);
    if (foamShader_) wgpuShaderModuleRelease(foamShader_);
    if (foamView_) wgpuTextureViewRelease(foamView_);
    if (foamTexture_) wgpuTextureRelease(foamTexture_);
    for (auto& buffer : foamBuffers_) if (buffer) wgpuBufferRelease(buffer);
    if (foamUniformBuffer_) wgpuBufferRelease(foamUniformBuffer_);
    if (finalizeBindGroup_) wgpuBindGroupRelease(finalizeBindGroup_);
    for (auto& group : fftAxisBindGroups_) if (group) wgpuBindGroupRelease(group);
    if (evolveBindGroup_) wgpuBindGroupRelease(evolveBindGroup_);
    if (finalizePipeline_) wgpuComputePipelineRelease(finalizePipeline_);
    if (fftPipeline_) wgpuComputePipelineRelease(fftPipeline_);
    if (evolvePipeline_) wgpuComputePipelineRelease(evolvePipeline_);
    if (finalizePipelineLayout_) wgpuPipelineLayoutRelease(finalizePipelineLayout_);
    if (fftPipelineLayout_) wgpuPipelineLayoutRelease(fftPipelineLayout_);
    if (finalizeBindGroupLayout_) wgpuBindGroupLayoutRelease(finalizeBindGroupLayout_);
    if (fftBindGroupLayout_) wgpuBindGroupLayoutRelease(fftBindGroupLayout_);
    if (finalizeShader_) wgpuShaderModuleRelease(finalizeShader_);
    if (fftShader_) wgpuShaderModuleRelease(fftShader_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (coastView_) wgpuTextureViewRelease(coastView_);
    if (coastTexture_) wgpuTextureRelease(coastTexture_);
    if (outputView_) wgpuTextureViewRelease(outputView_);
    if (outputTexture_) wgpuTextureRelease(outputTexture_);
    for (auto& buffer : axisUniformBuffers_) if (buffer) wgpuBufferRelease(buffer);
    if (simulationUniformBuffer_) wgpuBufferRelease(simulationUniformBuffer_);
    if (fftTwiddleBuffer_) wgpuBufferRelease(fftTwiddleBuffer_);
    if (pongBuffer_) wgpuBufferRelease(pongBuffer_);
    if (initialSpectrumBuffer_) wgpuBufferRelease(initialSpectrumBuffer_);

    finalizeBindGroup_ = nullptr;
    foamBindGroups_.fill(nullptr);
    foamPipeline_ = nullptr;
    foamPipelineLayout_ = nullptr;
    foamBindGroupLayout_ = nullptr;
    foamShader_ = nullptr;
    foamView_ = nullptr;
    foamTexture_ = nullptr;
    foamBuffers_.fill(nullptr);
    foamUniformBuffer_ = nullptr;
    foamFrame_ = 0;
    lastUpdateTime_ = 0.0f;
    cpuWaveModes_.clear();
    cpuEvolvedHeight_.clear();
    cpuEvolvedVelocity_.clear();
    cpuCacheTime_ = -1.0f;
    fftAxisBindGroups_.fill(nullptr);
    evolveBindGroup_ = nullptr;
    finalizePipeline_ = nullptr;
    fftPipeline_ = nullptr;
    evolvePipeline_ = nullptr;
    finalizePipelineLayout_ = nullptr;
    fftPipelineLayout_ = nullptr;
    finalizeBindGroupLayout_ = nullptr;
    fftBindGroupLayout_ = nullptr;
    finalizeShader_ = nullptr;
    fftShader_ = nullptr;
    sampler_ = nullptr;
    coastView_ = nullptr;
    coastTexture_ = nullptr;
    outputView_ = nullptr;
    outputTexture_ = nullptr;
    axisUniformBuffers_.fill(nullptr);
    simulationUniformBuffer_ = nullptr;
    fftTwiddleBuffer_ = nullptr;
    pongBuffer_ = nullptr;
    initialSpectrumBuffer_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

} // namespace voxy::render

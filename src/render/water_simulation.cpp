#include "render/water_simulation.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <random>
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

// Matches WaveData in water_fft.wgsl. Two complex displacement spectra plus
// height are padded to two vec4s for naturally aligned storage-buffer access.
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

constexpr std::array<float, WaterSimulation::CASCADE_COUNT> kPatchLengths = {
    96.0f, 384.0f, 1536.0f
};
constexpr std::array<glm::vec2, WaterSimulation::CASCADE_COUNT> kWavelengthBands = {
    glm::vec2(1.5f, 36.0f),
    glm::vec2(18.0f, 150.0f),
    glm::vec2(76.0f, 900.0f),
};

float smoothBand(float wavelength, glm::vec2 band) {
    const auto smooth = [](float a, float b, float x) {
        const float t = std::clamp((x - a) / std::max(b - a, 1e-6f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float lower = smooth(band.x * 0.72f, band.x, wavelength);
    const float upper = 1.0f - smooth(band.y, band.y * 1.25f, wavelength);
    return lower * upper;
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

} // namespace

WaterSimulation::~WaterSimulation() {
    shutdown();
}

bool WaterSimulation::init(WGPUDevice device, WGPUQueue queue,
                           const std::filesystem::path& shaderDirectory,
                           std::span<const uint16_t> terrainHeights,
                           uint32_t terrainWidth, uint32_t terrainHeight,
                           float terrainHeightScale, float cellScale,
                           float waterHeight) {
    if (!device || !queue) {
        LOG_ERROR("WaterSimulation::init: device or queue is null");
        return false;
    }
    device_ = device;
    queue_ = queue;

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
    coastTexture_ = gpu::createTextureWithData(
        device_, queue_, desc, std::as_bytes(std::span<const CoastPixel>(pixels)),
        fieldWidth * sizeof(CoastPixel));
    if (!coastTexture_) return false;
    coastView_ = gpu::createTextureView(coastTexture_);
    if (!coastView_) return false;

    LOG_INFO("Coastal refraction field initialized: {}x{}", fieldWidth, fieldHeight);
    return true;
}

bool WaterSimulation::createSpectrum() {
    std::vector<std::complex<float>> h0(kElementCount);
    std::mt19937 rng(0x5ea5ca1eu);
    std::normal_distribution<float> gaussian(0.0f, 1.0f);

    constexpr float gravity = 9.81f;
    constexpr float windSpeed = 19.0f;
    constexpr float phillipsAmplitude = 0.00055f;
    const glm::vec2 windDirection = glm::normalize(glm::vec2(0.91f, 0.414f));
    const float largestWave = windSpeed * windSpeed / gravity;

    for (uint32_t cascade = 0; cascade < CASCADE_COUNT; ++cascade) {
        const float patchLength = kPatchLengths[cascade];
        const float deltaK = 2.0f * std::numbers::pi_v<float> / patchLength;
        for (uint32_t y = 0; y < RESOLUTION; ++y) {
            const int32_t sy = y <= RESOLUTION / 2 ? static_cast<int32_t>(y)
                                                   : static_cast<int32_t>(y) - static_cast<int32_t>(RESOLUTION);
            for (uint32_t x = 0; x < RESOLUTION; ++x) {
                const int32_t sx = x <= RESOLUTION / 2 ? static_cast<int32_t>(x)
                                                       : static_cast<int32_t>(x) - static_cast<int32_t>(RESOLUTION);
                const glm::vec2 k = glm::vec2(static_cast<float>(sx), static_cast<float>(sy)) * deltaK;
                const float kLength = glm::length(k);
                const uint32_t index = cascade * RESOLUTION * RESOLUTION + y * RESOLUTION + x;
                if (kLength < 1e-5f) {
                    h0[index] = {};
                    continue;
                }

                const float wavelength = 2.0f * std::numbers::pi_v<float> / kLength;
                const float band = smoothBand(wavelength, kWavelengthBands[cascade]);
                const glm::vec2 kDirection = k / kLength;
                const float alignment = glm::dot(kDirection, windDirection);
                const float directional = alignment >= 0.0f
                    ? std::pow(alignment, 4.0f)
                    : 0.075f * std::pow(-alignment, 4.0f);
                const float k2 = kLength * kLength;
                const float phillips = phillipsAmplitude *
                    std::exp(-1.0f / (k2 * largestWave * largestWave)) /
                    (k2 * k2) * directional *
                    std::exp(-k2 * 0.045f * 0.045f) * band;
                const float sigma = std::sqrt(std::max(phillips, 0.0f) *
                                              deltaK * deltaK * 0.5f);
                h0[index] = std::complex<float>(gaussian(rng), gaussian(rng)) * sigma;
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
        const float deltaK = 2.0f * std::numbers::pi_v<float> /
                             kPatchLengths[cascade];
        for (uint32_t y = 0; y < RESOLUTION; ++y) {
            const int32_t sy = y <= RESOLUTION / 2 ? static_cast<int32_t>(y)
                : static_cast<int32_t>(y) - static_cast<int32_t>(RESOLUTION);
            for (uint32_t x = 0; x < RESOLUTION; ++x) {
                const uint32_t mirrorX = (RESOLUTION - x) % RESOLUTION;
                const uint32_t mirrorY = (RESOLUTION - y) % RESOLUTION;
                const uint32_t index = base + y * RESOLUTION + x;
                const uint32_t mirror = base + mirrorY * RESOLUTION + mirrorX;
                if (index >= mirror) continue;

                const int32_t sx = x <= RESOLUTION / 2 ? static_cast<int32_t>(x)
                    : static_cast<int32_t>(x) - static_cast<int32_t>(RESOLUTION);
                const glm::vec2 waveVector =
                    glm::vec2(static_cast<float>(sx), static_cast<float>(sy)) * deltaK;
                const float waveNumber = glm::length(waveVector);
                if (waveNumber < 1e-5f) continue;

                const float energy = std::norm(h0[index]) + std::norm(h0[mirror]);
                candidates.push_back({
                    energy,
                    CpuWaveMode{
                        waveVector,
                        glm::vec2(-0.5f * kPatchLengths[cascade] /
                                  static_cast<float>(RESOLUTION)),
                        glm::vec2(h0[index].real(), h0[index].imag()),
                        glm::vec2(std::conj(h0[mirror]).real(),
                                  std::conj(h0[mirror]).imag()),
                        std::sqrt(gravity * waveNumber)}});
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
                packed[index].height = {h0[index].real(), h0[index].imag()};
                const auto conjugateMirror = std::conj(h0[mirror]);
                packed[index].displacementX = {
                    conjugateMirror.real(), conjugateMirror.imag()
                };
            }
        }
    }

    const uint64_t byteSize = packed.size() * sizeof(WaveData);
    auto desc = gpu::BufferDesc::storage(byteSize, true, "water_initial_spectrum");
    initialSpectrumBuffer_ = gpu::createBufferWithData(
        device_, queue_, desc, std::span<const WaveData>(packed));
    return initialSpectrumBuffer_ != nullptr;
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
    if (cpuWaveModes_.empty() || !std::isfinite(timeSeconds)) return sample;
    updateCpuWaveCache(timeSeconds);

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

        sample.heightOffset += 2.0f * spatialHeight.x;
        sample.slope -= 2.0f * spatialHeight.y * mode.waveVector;
        sample.velocity.y += 2.0f * spatialVelocity.x;
        if (waveNumber > 1e-5f) {
            const glm::vec2 horizontal = mode.waveVector / waveNumber
                                       * (2.0f * mode.angularFrequency
                                          * spatialHeight.x);
            sample.velocity.x += horizontal.x;
            sample.velocity.z += horizontal.y;
        }
    }

    const float amplitude = std::clamp(strength, 0.0f, 1.0f);
    sample.heightOffset *= amplitude;
    sample.slope *= amplitude;
    sample.velocity *= amplitude;
    return sample;
}

bool WaterSimulation::createBuffers() {
    const uint64_t byteSize = static_cast<uint64_t>(kElementCount) * sizeof(WaveData);
    pingBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(byteSize, false, "water_fft_ping"));
    pongBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(byteSize, false, "water_fft_pong"));
    simulationUniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(sizeof(SimParams), "water_sim_params"));
    if (!pingBuffer_ || !pongBuffer_ || !simulationUniformBuffer_) {
        return false;
    }

    for (uint32_t axis = 0; axis < 2; ++axis) {
        for (uint32_t stage = 0; stage < FFT_STAGE_COUNT; ++stage) {
            const uint32_t pass = axis * FFT_STAGE_COUNT + stage;
            SimParams params{.time = 0.0f, .stage = stage, .axis = axis, .size = RESOLUTION};
            auto desc = gpu::BufferDesc::uniform(sizeof(SimParams), "water_fft_stage_params");
            stageUniformBuffers_[pass] = gpu::createBufferWithData(
                device_, queue_, desc, std::span<const SimParams>(&params, 1));
            if (!stageUniformBuffers_[pass]) {
                return false;
            }
        }
    }
    return true;
}

bool WaterSimulation::createOutputTexture() {
    gpu::TextureDesc desc = gpu::TextureDesc::storage(
        RESOLUTION, RESOLUTION, WGPUTextureFormat_RGBA16Float,
        "water_displacement_cascades");
    desc.depthOrArrayLayers = CASCADE_COUNT;
    outputTexture_ = gpu::createTexture(device_, desc);
    if (!outputTexture_) return false;

    gpu::TextureViewDesc viewDesc{};
    viewDesc.label = "water_displacement_cascades_view";
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    viewDesc.dimension = WGPUTextureViewDimension_2DArray;
    viewDesc.arrayLayerCount = CASCADE_COUNT;
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

    std::array<gpu::BindGroupLayoutEntry, 3> fftEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().uniformBuffer(false, sizeof(SimParams)),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(2).computeVisible().storageBuffer(false),
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
                                         "fft", "water_inverse_fft");
    if (!evolvePipeline_ || !fftPipeline_) return false;

    finalizeShader_ = gpu::loadShaderModule(device_, shaderDirectory / "water_finalize.wgsl",
                                            "water_finalize.wgsl");
    if (!finalizeShader_) return false;
    std::array<gpu::BindGroupLayoutEntry, 2> finalizeEntries = {
        gpu::BindGroupLayoutEntry(0).computeVisible().storageBuffer(true),
        gpu::BindGroupLayoutEntry(1).computeVisible().storageTexture(
            WGPUStorageTextureAccess_WriteOnly, WGPUTextureFormat_RGBA16Float,
            WGPUTextureViewDimension_2DArray),
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
    const uint64_t byteSize = static_cast<uint64_t>(kElementCount) * sizeof(WaveData);
    std::array<gpu::BindGroupEntry, 3> evolveEntries = {
        gpu::BindGroupEntry(0).buffer(simulationUniformBuffer_, 0, sizeof(SimParams)),
        gpu::BindGroupEntry(1).buffer(initialSpectrumBuffer_, 0, byteSize),
        gpu::BindGroupEntry(2).buffer(pongBuffer_, 0, byteSize),
    };
    evolveBindGroup_ = gpu::createBindGroup(
        device_, fftBindGroupLayout_, evolveEntries, "water_evolve_bind_group");
    if (!evolveBindGroup_) return false;

    for (uint32_t axis = 0; axis < 2; ++axis) {
        for (uint32_t stage = 0; stage < FFT_STAGE_COUNT; ++stage) {
            const uint32_t pass = axis * FFT_STAGE_COUNT + stage;
            // Every axis starts from pong. Even stages write ping, odd stages
            // write pong; eight stages therefore finish in pong.
            WGPUBuffer source = (stage & 1u) == 0u ? pongBuffer_ : pingBuffer_;
            WGPUBuffer destination = (stage & 1u) == 0u ? pingBuffer_ : pongBuffer_;
            std::array<gpu::BindGroupEntry, 3> entries = {
                gpu::BindGroupEntry(0).buffer(stageUniformBuffers_[pass], 0, sizeof(SimParams)),
                gpu::BindGroupEntry(1).buffer(source, 0, byteSize),
                gpu::BindGroupEntry(2).buffer(destination, 0, byteSize),
            };
            fftBindGroups_[pass] = gpu::createBindGroup(
                device_, fftBindGroupLayout_, entries, "water_fft_stage_bind_group");
            if (!fftBindGroups_[pass]) return false;
        }
    }

    std::array<gpu::BindGroupEntry, 2> finalizeEntries = {
        gpu::BindGroupEntry(0).buffer(pongBuffer_, 0, byteSize),
        gpu::BindGroupEntry(1).textureView(outputView_),
    };
    finalizeBindGroup_ = gpu::createBindGroup(
        device_, finalizeBindGroupLayout_, finalizeEntries,
        "water_finalize_bind_group");
    return finalizeBindGroup_ != nullptr;
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

void WaterSimulation::update(WGPUCommandEncoder encoder, float timeSeconds) {
    if (!isInitialized() || !encoder) return;

    SimParams params{.time = timeSeconds, .stage = 0, .axis = 0, .size = RESOLUTION};
    gpu::writeBuffer(queue_, simulationUniformBuffer_, 0, params);
    struct FoamParams {
        float deltaTime;
        float time;
        glm::vec2 padding;
    };
    const float rawDelta = foamFrame_ == 0 ? 1.0f / 60.0f : timeSeconds - lastUpdateTime_;
    const FoamParams foamParams{
        .deltaTime = std::clamp(rawDelta < 0.0f ? rawDelta + 4096.0f : rawDelta,
                                1.0f / 240.0f, 0.1f),
        .time = timeSeconds,
        .padding = glm::vec2(0.0f),
    };
    gpu::writeBuffer(queue_, foamUniformBuffer_, 0, foamParams);

    WGPUComputePassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "water_fft_compute_pass");
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    constexpr uint32_t evolveGroups = (kElementCount + 255u) / 256u;
    wgpuComputePassEncoderSetPipeline(pass, evolvePipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, evolveBindGroup_, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, evolveGroups, 1, 1);

    constexpr uint32_t butterflyCount = kElementCount / 2u;
    constexpr uint32_t butterflyGroups = (butterflyCount + 255u) / 256u;
    wgpuComputePassEncoderSetPipeline(pass, fftPipeline_);
    for (uint32_t fftPass = 0; fftPass < fftBindGroups_.size(); ++fftPass) {
        wgpuComputePassEncoderSetBindGroup(pass, 0, fftBindGroups_[fftPass], 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, butterflyGroups, 1, 1);
    }

    wgpuComputePassEncoderSetPipeline(pass, finalizePipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, finalizeBindGroup_, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(
        pass, (RESOLUTION + 7u) / 8u, (RESOLUTION + 7u) / 8u, CASCADE_COUNT);

    wgpuComputePassEncoderSetPipeline(pass, foamPipeline_);
    wgpuComputePassEncoderSetBindGroup(
        pass, 0, foamBindGroups_[foamFrame_ & 1u], 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(
        pass, (RESOLUTION + 7u) / 8u, (RESOLUTION + 7u) / 8u, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    lastUpdateTime_ = timeSeconds;
    foamFrame_++;
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
    for (auto& group : fftBindGroups_) if (group) wgpuBindGroupRelease(group);
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
    for (auto& buffer : stageUniformBuffers_) if (buffer) wgpuBufferRelease(buffer);
    if (simulationUniformBuffer_) wgpuBufferRelease(simulationUniformBuffer_);
    if (pongBuffer_) wgpuBufferRelease(pongBuffer_);
    if (pingBuffer_) wgpuBufferRelease(pingBuffer_);
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
    fftBindGroups_.fill(nullptr);
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
    stageUniformBuffers_.fill(nullptr);
    simulationUniformBuffer_ = nullptr;
    pongBuffer_ = nullptr;
    pingBuffer_ = nullptr;
    initialSpectrumBuffer_ = nullptr;
    device_ = nullptr;
    queue_ = nullptr;
}

} // namespace voxy::render

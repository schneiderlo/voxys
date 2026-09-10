#include "render/environment_lighting.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

#if defined(VOXY_WASM)
#include <emscripten.h>
extern "C" WGPUDevice emscripten_webgpu_get_device(void);
#else
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, const WGPUWrappedSubmissionIndex*);
#endif

namespace voxy::render {
namespace {

struct Context {
#if defined(VOXY_WASM)
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    bool init() {
        device = emscripten_webgpu_get_device();
        if (device) queue = wgpuDeviceGetQueue(device);
        return device && queue;
    }
    WGPUDevice gpuDevice() const { return device; }
    WGPUQueue gpuQueue() const { return queue; }
    ~Context() { if (queue) wgpuQueueRelease(queue); if (device) wgpuDeviceRelease(device); }
#else
    gpu::Context context;
    bool init() { return context.initHeadless(); }
    WGPUDevice gpuDevice() const { return context.getDevice(); }
    WGPUQueue gpuQueue() const { return context.getQueue(); }
#endif
    void pump() {
#if defined(VOXY_WASM)
        emscripten_sleep(1);
#else
        static_cast<void>(wgpuDevicePoll(gpuDevice(), false, nullptr));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    bool wait(const std::atomic<int>& state) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (state.load(std::memory_order_acquire) == 0 && std::chrono::steady_clock::now() < end) pump();
        return state.load(std::memory_order_acquire) == 1;
    }
};

struct ValidationScope {
    Context& context;
    explicit ValidationScope(Context& c) : context(c) { wgpuDevicePushErrorScope(c.gpuDevice(), WGPUErrorFilter_Validation); }
    ~ValidationScope() {
        struct Result { std::atomic<int> state{0}; std::string message; };
        auto result = std::make_shared<Result>();
        using Payload = std::shared_ptr<Result>;
        auto* payload = new Payload(result);
#if defined(VOXY_WASM)
        WGPUPopErrorScopeCallbackInfo info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous;
        info.userdata1 = payload;
        info.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* data, void*) {
            const std::unique_ptr<Payload> p(static_cast<Payload*>(data));
            if (message.data) (*p)->message.assign(message.data, message.length);
            (*p)->state.store(status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError ? 1 : 2, std::memory_order_release);
        };
        static_cast<void>(wgpuDevicePopErrorScope(context.gpuDevice(), info));
#else
        wgpuDevicePopErrorScope(context.gpuDevice(), [](WGPUErrorType type, const char* message, void* data) {
            const std::unique_ptr<Payload> p(static_cast<Payload*>(data));
            if (message) (*p)->message = message;
            (*p)->state.store(type == WGPUErrorType_NoError ? 1 : 2, std::memory_order_release);
        }, payload);
#endif
        const bool clean = context.wait(result->state);
        // The atomic publishes the string. On a timeout leave it unread.
        EXPECT_TRUE(clean) << (result->state.load(std::memory_order_acquire) ? result->message : "validation callback timeout");
    }
};

struct Sky {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    ~Sky() { if (view) wgpuTextureViewRelease(view); if (texture) wgpuTextureRelease(texture); }
    bool init(Context& context, bool directional, glm::vec3 color = {0.25f, 1.0f, 4.0f}) {
        constexpr uint32_t width = 256, height = 128;
        auto desc = gpu::TextureDesc::tex2DMipmapped(width, height, WGPUTextureFormat_RGBA16Float);
        texture = gpu::createTexture(context.gpuDevice(), desc);
        if (!texture) return false;
        view = gpu::createTextureView(texture, {.mipLevelCount = desc.mipLevelCount});
        if (!view) return false;
        std::vector<glm::vec4> pixels(width * height);
        constexpr float pi = std::numbers::pi_v<float>;
        for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
            const float theta = pi * (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            const float phi = 2.0f * pi * ((static_cast<float>(x) + 0.5f) / static_cast<float>(width) - 0.5f);
            const glm::vec3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            pixels[y * width + x] = glm::vec4(directional ? glm::vec3(1.0f) + 0.5f * d : color, 1.0f);
        }
        uint32_t w = width, h = height;
        for (uint32_t mip = 0; mip < desc.mipLevelCount; ++mip) {
            std::vector<uint16_t> half(pixels.size() * 4);
            for (size_t i = 0; i < pixels.size(); ++i) for (size_t c = 0; c < 4; ++c)
                half[i * 4 + c] = glm::packHalf1x16(pixels[i][static_cast<glm::length_t>(c)]);
            if (!gpu::writeTexture(context.gpuQueue(), texture, std::as_bytes(std::span(half)), w, h, w * 8, mip)) return false;
            if (mip + 1 == desc.mipLevelCount) break;
            const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            std::vector<glm::vec4> reduced(nw * nh, glm::vec4(0));
            for (uint32_t y = 0; y < nh; ++y) for (uint32_t x = 0; x < nw; ++x)
                for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx)
                    reduced[y * nw + x] += pixels[std::min(y * 2 + dy, h - 1) * w + std::min(x * 2 + dx, w - 1)] * 0.25f;
            pixels = std::move(reduced); w = nw; h = nh;
        }
        return true;
    }
};

// Independent consumer probes the public cube/LUT views. Request.w encodes
// kind in its integer part and specular LOD/16 in its fraction (up to mip 9).
constexpr auto kProbe = R"wgsl(
@group(0) @binding(0) var specular: texture_cube<f32>;
@group(0) @binding(1) var diffuse: texture_cube<f32>;
@group(0) @binding(2) var brdf: texture_2d<f32>;
@group(0) @binding(3) var linearSampler: sampler;
@group(0) @binding(4) var<storage, read> requests: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> values: array<vec4<f32>>;
@compute @workgroup_size(64)
fn probe(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= arrayLength(&requests)) { return; }
    let r = requests[id.x];
    if (r.w < 1.0) {
        values[id.x] = textureSampleLevel(specular, linearSampler, r.xyz, r.w * 16.0);
    } else if (r.w < 2.0) {
        values[id.x] = textureSampleLevel(diffuse, linearSampler, r.xyz, 0.0);
    } else {
        values[id.x] = textureSampleLevel(brdf, linearSampler, r.xy, 0.0);
    }
}
)wgsl";

struct Probe {
    WGPUShaderModule shader = nullptr;
    WGPUComputePipeline pipeline = nullptr;
    WGPUBindGroupLayout layout = nullptr;
    WGPUBindGroup group = nullptr;
    WGPUSampler sampler = nullptr;
    WGPUBuffer input = nullptr, output = nullptr, readback = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer command = nullptr;
    ~Probe() {
        if (command) wgpuCommandBufferRelease(command);
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (group) wgpuBindGroupRelease(group);
        if (sampler) wgpuSamplerRelease(sampler);
        if (layout) wgpuBindGroupLayoutRelease(layout);
        if (pipeline) wgpuComputePipelineRelease(pipeline);
        if (shader) wgpuShaderModuleRelease(shader);
        for (auto buffer : {input, output, readback}) if (buffer) wgpuBufferRelease(buffer);
    }
    bool run(Context& context, EnvironmentLighting& lighting, WGPUTextureView source,
             const std::vector<glm::vec4>& requests, std::vector<glm::vec4>& result,
             bool releaseBeforeSubmit = false) {
        const size_t bytes = requests.size() * sizeof(glm::vec4);
        shader = gpu::createShaderModule(context.gpuDevice(), kProbe, "environment_probe");
        if (!shader) return false;
        WGPUComputePipelineDescriptor pipelineDesc{};
        pipelineDesc.compute.module = shader; WGPU_SET_ENTRY_POINT(pipelineDesc.compute, "probe");
        pipeline = wgpuDeviceCreateComputePipeline(context.gpuDevice(), &pipelineDesc);
        if (!pipeline) return false;
        layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        sampler = gpu::createSampler(context.gpuDevice(), gpu::SamplerDesc::linear());
        input = gpu::createBufferWithData(context.gpuDevice(), context.gpuQueue(),
            gpu::BufferDesc::storage(bytes, true), std::as_bytes(std::span(requests)));
        output = gpu::createBuffer(context.gpuDevice(), gpu::BufferDesc::storage(bytes));
        readback = gpu::createBuffer(context.gpuDevice(), {.size = bytes, .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead});
        if (!layout || !sampler || !input || !output || !readback) return false;
        const auto views = lighting.views();
        const std::array entries{
            gpu::BindGroupEntry(0).textureView(views.specular), gpu::BindGroupEntry(1).textureView(views.diffuse),
            gpu::BindGroupEntry(2).textureView(views.brdf), gpu::BindGroupEntry(3).sampler(sampler),
            gpu::BindGroupEntry(4).buffer(input), gpu::BindGroupEntry(5).buffer(output)};
        group = gpu::createBindGroup(context.gpuDevice(), layout, entries);
        WGPUCommandEncoderDescriptor encoderDesc{};
        encoder = wgpuDeviceCreateCommandEncoder(context.gpuDevice(), &encoderDesc);
        if (!group || !encoder || !lighting.encodeBake(encoder, source)) return false;
        WGPUComputePassDescriptor passDesc{};
        const auto pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        if (!pass) return false;
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, static_cast<uint32_t>((requests.size() + 63) / 64), 1, 1);
        wgpuComputePassEncoderEnd(pass); wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readback, 0, bytes);
        WGPUCommandBufferDescriptor commandDesc{};
        command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        if (!command) return false;
        if (releaseBeforeSubmit) lighting.releaseHandles();
        wgpuQueueSubmit(context.gpuQueue(), 1, &command);
        auto state = std::make_shared<std::atomic<int>>(0);
        using Payload = std::shared_ptr<std::atomic<int>>;
        auto* payload = new Payload(state);
#if defined(VOXY_WASM)
        WGPUBufferMapCallbackInfo info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        info.mode = WGPUCallbackMode_AllowSpontaneous; info.userdata1 = payload;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* data, void*) {
            const std::unique_ptr<Payload> p(static_cast<Payload*>(data));
            (*p)->store(status == WGPUMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
        };
        static_cast<void>(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bytes, info));
#else
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, bytes, [](WGPUBufferMapAsyncStatus status, void* data) {
            const std::unique_ptr<Payload> p(static_cast<Payload*>(data));
            (*p)->store(status == WGPUBufferMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
        }, payload);
#endif
        if (!context.wait(*state)) return false;
        const auto* data = wgpuBufferGetConstMappedRange(readback, 0, bytes);
        if (!data) return false;
        result.resize(requests.size()); std::memcpy(result.data(), data, static_cast<size_t>(bytes));
        wgpuBufferUnmap(readback);
        for (const auto value : result) for (int c = 0; c < 4; ++c) if (!std::isfinite(value[c])) return false;
        if (const char* directory = std::getenv("VOXY_ENVIRONMENT_CAPTURE_DIR"); directory && *directory) {
            static uint32_t sequence = 0;
            const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
            const auto base = std::filesystem::path(directory) / (std::to_string(sequence++) + "-" + test->name());
            std::ofstream raw(base.string() + ".f32", std::ios::binary);
            raw.write(reinterpret_cast<const char*>(result.data()), static_cast<std::streamsize>(bytes));
            std::ofstream req(base.string() + ".requests.f32", std::ios::binary);
            req.write(reinterpret_cast<const char*>(requests.data()), static_cast<std::streamsize>(bytes));
            const size_t height = (result.size() + 63) / 64;
            std::vector<uint8_t> preview(64 * height * 4, 0);
            for (size_t i = 0; i < result.size(); ++i) {
                for (int c = 0; c < 3; ++c)
                    preview[i * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(std::lround(255 * std::clamp(result[i][c] / 4.0f, 0.0f, 1.0f)));
                preview[i * 4 + 3] = 255;
            }
            std::ofstream rgba(base.string() + ".rgba", std::ios::binary);
            rgba.write(reinterpret_cast<const char*>(preview.data()), static_cast<std::streamsize>(preview.size()));
            std::ofstream meta(base.string() + ".json");
            meta << "{\"width\":64,\"height\":" << height << ",\"values\":" << result.size()
                 << ",\"format\":\"float32-le-rgba\",\"preview\":\"linear RGB divided by 4; data visualization only\"}";
            if (!raw || !req || !rgba || !meta) return false;
        }
        return true;
    }
};

EnvironmentLightingConfig smallConfig() {
    EnvironmentLightingConfig config;
    config.specularSize = 16; config.diffuseSize = 4; config.brdfSize = 16; config.samples = 1024;
    return config;
}

glm::vec3 cubeDirection(uint32_t face, float u, float v) {
    const std::array<glm::vec3, 6> axes{{{1,-v,-u},{-1,-v,u},{u,1,v},{u,-1,-v},{u,-v,1},{-u,-v,-1}}};
    return glm::normalize(axes[face]);
}

std::vector<glm::vec4> allTexels(const EnvironmentLightingConfig& config) {
    std::vector<glm::vec4> requests;
    const uint32_t levels = static_cast<uint32_t>(std::bit_width(config.specularSize));
    for (uint32_t mip = 0; mip < levels; ++mip) {
        const uint32_t size = config.specularSize >> mip;
        for (uint32_t face = 0; face < 6; ++face) for (uint32_t y = 0; y < size; ++y) for (uint32_t x = 0; x < size; ++x)
            requests.emplace_back(cubeDirection(face, 2 * (static_cast<float>(x) + .5f) / static_cast<float>(size) - 1,
                2 * (static_cast<float>(y) + .5f) / static_cast<float>(size) - 1), static_cast<float>(mip) / 16.0f);
    }
    for (uint32_t face = 0; face < 6; ++face) for (uint32_t y = 0; y < config.diffuseSize; ++y) for (uint32_t x = 0; x < config.diffuseSize; ++x)
        requests.emplace_back(cubeDirection(face, 2 * (static_cast<float>(x) + .5f) / static_cast<float>(config.diffuseSize) - 1,
            2 * (static_cast<float>(y) + .5f) / static_cast<float>(config.diffuseSize) - 1), 1.0f);
    for (uint32_t y = 0; y < config.brdfSize; ++y) for (uint32_t x = 0; x < config.brdfSize; ++x)
        requests.emplace_back((static_cast<float>(x) + .5f) / static_cast<float>(config.brdfSize),
            (static_cast<float>(y) + .5f) / static_cast<float>(config.brdfSize), 0.0f, 2.0f);
    return requests;
}

// Independent midpoint integration over incoming directions, not the shader's
// half-vector importance sequence. Integrates D*G*F/(4*NoV) dOmega; Lambert's
// cosine has cancelled the NoL denominator of the microfacet BRDF.
glm::dvec2 quadrature(double noV, double roughness, uint32_t rows, uint32_t columns) {
    constexpr double pi = std::numbers::pi;
    const double alpha2 = std::pow(roughness, 4), k = roughness * roughness / 2;
    const glm::dvec3 view(std::sqrt(1 - noV * noV), 0, noV);
    glm::dvec2 total(0);
    for (uint32_t y = 0; y < rows; ++y) {
        const double noL = (static_cast<double>(y) + .5) / rows;
        const double radial = std::sqrt(1 - noL * noL);
        const double g = (noV / (noV * (1-k) + k)) * (noL / (noL * (1-k) + k));
        for (uint32_t x = 0; x < columns; ++x) {
            const double phi = 2 * pi * (static_cast<double>(x) + .5) / columns;
            const auto half = glm::normalize(view + glm::dvec3(radial * std::cos(phi), radial * std::sin(phi), noL));
            const double denominator = half.z * half.z * (alpha2 - 1) + 1;
            const double d = alpha2 / (pi * denominator * denominator);
            const double fc = std::pow(1 - glm::dot(view, half), 5);
            total += glm::dvec2(1-fc, fc) * (d * g / (4 * noV));
        }
    }
    return total * (2 * pi / (static_cast<double>(rows) * columns));
}

} // namespace

TEST(EnvironmentLightingTest, RejectsMissingHandles) {
    EnvironmentLighting lighting;
    EXPECT_FALSE(lighting.init(nullptr, nullptr));
    EXPECT_FALSE(lighting.encodeBake(nullptr, nullptr));
    EXPECT_EQ(lighting.requestedBytes(), 0u);
    EXPECT_EQ(lighting.views().specular, nullptr);
}

TEST(EnvironmentLightingTest, ConstantRadianceAllMipsAndReleaseBeforeSubmit) {
    Context context; ASSERT_TRUE(context.init()); ValidationScope validation(context);
    EnvironmentLighting lighting;
    ASSERT_TRUE(lighting.init(context.gpuDevice(), context.gpuQueue()));
    EXPECT_EQ(lighting.requestedBytes(), 1228944u);
    const auto previous = lighting.views();
    for (const auto invalid : {0u, 15u, 17u, 1024u}) {
        auto config = smallConfig(); config.specularSize = invalid;
        EXPECT_FALSE(lighting.init(context.gpuDevice(), context.gpuQueue(), config));
        EXPECT_EQ(lighting.views().specular, previous.specular);
    }
    for (const auto invalid : {0u, 63u, 4097u, UINT32_MAX}) {
        auto config = smallConfig(); config.samples = invalid;
        EXPECT_FALSE(lighting.init(context.gpuDevice(), context.gpuQueue(), config));
        EXPECT_EQ(lighting.views().brdf, previous.brdf);
    }
    const auto config = smallConfig();
    ASSERT_TRUE(lighting.init(context.gpuDevice(), context.gpuQueue(), config));
    Sky sky; ASSERT_TRUE(sky.init(context, false));
    const auto requests = allTexels(config);
    std::vector<glm::vec4> values; Probe probe;
    ASSERT_TRUE(probe.run(context, lighting, sky.view, requests, values, true));
    EXPECT_EQ(lighting.views().specular, nullptr); EXPECT_EQ(lighting.requestedBytes(), 0u);
    float maximum = 0;
    for (size_t i = 0; i < requests.size(); ++i) if (requests[i].w < 2)
        for (int c = 0; c < 3; ++c) maximum = std::max(maximum, std::abs(values[i][c] - glm::vec3(.25f,1,4)[c]));
    EXPECT_LE(maximum, .002f);
    std::cout << "CONSTANT maximum_absolute_error=" << maximum << " samples=" << values.size() << '\n';
}

TEST(EnvironmentLightingTest, DirectionalIrradianceRoughnessAndCubeSeams) {
    Context context; ASSERT_TRUE(context.init()); ValidationScope validation(context);
    auto config = smallConfig(); config.specularSize = 32; config.diffuseSize = 16; config.samples = 4096;
    EnvironmentLighting lighting; ASSERT_TRUE(lighting.init(context.gpuDevice(), context.gpuQueue(), config));
    Sky sky; ASSERT_TRUE(sky.init(context, true));
    auto requests = allTexels(config);
    const size_t texels = requests.size();
    // Both sides of every signed cube edge at nine positions. Includes triple
    // corners; actual cube sampling must agree across the shared edge.
    for (uint32_t axis = 0; axis < 3; ++axis) for (int s : {-1,1}) for (int t : {-1,1}) for (int p = -4; p <= 4; ++p) {
        glm::vec3 d(0); d[static_cast<int>(axis)] = static_cast<float>(s);
        d[static_cast<int>((axis+1)%3)] = static_cast<float>(t);
        d[static_cast<int>((axis+2)%3)] = static_cast<float>(p) / 4;
        for (float kind : {0.0f, .125f, .3125f, 1.0f}) {
            auto a = d, b = d; a[static_cast<int>(axis)] *= 1.0001f; b[static_cast<int>((axis+1)%3)] *= 1.0001f;
            requests.emplace_back(glm::normalize(a), kind); requests.emplace_back(glm::normalize(b), kind);
        }
    }
    std::vector<glm::vec4> values; Probe probe; ASSERT_TRUE(probe.run(context, lighting, sky.view, requests, values));
    float sharpError = 0, diffuseError = 0, roughError = 0, seamError = 0;
    for (size_t i = 0; i < texels; ++i) {
        const auto r = requests[i];
        if (r.w == 0 || r.w == 1 || r.w == .3125f) {
            const auto expected = glm::vec3(1) + glm::vec3(r) * (r.w == 0 ? .5f : 1.0f/3.0f);
            for (int c = 0; c < 3; ++c) {
                auto& error = r.w == 0 ? sharpError : r.w == 1 ? diffuseError : roughError;
                error = std::max(error, std::abs(values[i][c] - expected[c]));
            }
        }
    }
    for (size_t i = texels; i < requests.size(); i += 2) for (int c = 0; c < 3; ++c)
        seamError = std::max(seamError, std::abs(values[i][c] - values[i+1][c]));
    EXPECT_LE(sharpError, .002f); EXPECT_LE(diffuseError, .006f);
    EXPECT_LE(roughError, .006f); EXPECT_LE(seamError, .002f);
    std::cout << "DIRECTIONAL sharp=" << sharpError << " diffuse=" << diffuseError
              << " rough=" << roughError << " seams=" << seamError << " samples=" << values.size() << '\n';
}

TEST(EnvironmentLightingTest, BrdfMatchesIndependentHemisphereQuadrature) {
    Context context; ASSERT_TRUE(context.init()); ValidationScope validation(context);
    auto config = smallConfig(); config.brdfSize = 32; config.samples = 4096;
    EnvironmentLighting lighting; ASSERT_TRUE(lighting.init(context.gpuDevice(), context.gpuQueue(), config));
    Sky sky; ASSERT_TRUE(sky.init(context, false));
    std::vector<glm::vec4> requests;
    for (uint32_t y = 0; y < 32; ++y) for (uint32_t x = 0; x < 32; ++x)
        requests.emplace_back((static_cast<float>(x)+.5f)/32, (static_cast<float>(y)+.5f)/32, 0, 2);
    std::vector<glm::vec4> values; Probe probe; ASSERT_TRUE(probe.run(context, lighting, sky.view, requests, values));
    float maximumEnergy = 0;
    for (const auto value : values) {
        EXPECT_GE(value.x, 0); EXPECT_GE(value.y, 0);
        maximumEnergy = std::max(maximumEnergy, value.x + value.y);
    }
    EXPECT_LE(maximumEnergy, 1.003f);
    double maximumError = 0, maximumReferenceDelta = 0;
    for (uint32_t y : {10u, 20u, 31u}) for (uint32_t x : {6u, 16u, 31u}) {
        const size_t i = y * 32 + x;
        const auto coarse = quadrature(static_cast<double>(requests[i].x), static_cast<double>(requests[i].y), 256, 512);
        const auto fine = quadrature(static_cast<double>(requests[i].x), static_cast<double>(requests[i].y), 512, 1024);
        for (int c = 0; c < 2; ++c) {
            maximumReferenceDelta = std::max(maximumReferenceDelta, std::abs(coarse[c] - fine[c]));
            maximumError = std::max(maximumError, std::abs(static_cast<double>(values[i][c]) - fine[c]));
        }
    }
    EXPECT_LE(maximumReferenceDelta, .0002); EXPECT_LE(maximumError, .004);
    std::cout << "BRDF maximum_absolute_error=" << maximumError << " reference_convergence="
              << maximumReferenceDelta << " maximum_white_energy=" << maximumEnergy << '\n';
}

TEST(EnvironmentLightingTest, AbandonedEncodingAndRebakeRetainOutputViews) {
    Context context; ASSERT_TRUE(context.init()); ValidationScope validation(context);
    EnvironmentLighting lighting; ASSERT_TRUE(lighting.init(context.gpuDevice(), context.gpuQueue(), smallConfig()));
    Sky first, second; ASSERT_TRUE(first.init(context, false)); ASSERT_TRUE(second.init(context, false, {2,.5f,.125f}));
    const auto before = lighting.views();
    WGPUCommandEncoderDescriptor desc{};
    auto abandoned = wgpuDeviceCreateCommandEncoder(context.gpuDevice(), &desc); ASSERT_NE(abandoned, nullptr);
    EXPECT_TRUE(lighting.encodeBake(abandoned, first.view));
    wgpuCommandEncoderRelease(abandoned); // No submission, deliberately.
    const std::vector<glm::vec4> requests{{1,0,0,0},{0,1,0,.25f},{0,0,1,1}};
    for (bool changed : {false, true}) {
        std::vector<glm::vec4> values; Probe probe;
        ASSERT_TRUE(probe.run(context, lighting, changed ? second.view : first.view, requests, values));
        const glm::vec3 expected = changed ? glm::vec3(2,.5f,.125f) : glm::vec3(.25f,1,4);
        for (const auto value : values) for (int c = 0; c < 3; ++c) EXPECT_NEAR(value[c], expected[c], .002f);
        EXPECT_EQ(lighting.views().specular, before.specular);
        EXPECT_EQ(lighting.views().diffuse, before.diffuse);
        EXPECT_EQ(lighting.views().brdf, before.brdf);
    }
}

} // namespace voxy::render

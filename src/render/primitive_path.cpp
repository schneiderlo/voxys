#include "render/primitive_path.hpp"

#include "render/primitive_instance_packing.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"
#include "gpu/webgpu_compat.hpp"
#include "render/frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <vector>

namespace voxy::render {
namespace {

constexpr size_t kInitialInstanceCapacity = 64;
constexpr uint32_t kSegments = 16;
constexpr uint32_t kSphereRings = 12;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
};

struct alignas(16) PrimitiveUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 lightDirAndRayDepth{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 viewport{1.0f};
    glm::vec4 lightingColor{1.0f, 0.95f, 0.9f, 1.0f};
    glm::vec4 ambientColor{0.1f, 0.12f, 0.15f, 1.3f};
    glm::vec4 fogColorExposure{0.36f, 0.58f, 0.64f, 1.0f};
};

static_assert(sizeof(Vertex) == 24);
static_assert(sizeof(PrimitiveUniforms) == 160);

struct alignas(16) CompactPose {
    glm::vec4 positionInvMass{0.0f};
    glm::vec4 orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct alignas(16) CompactShape {
    glm::vec4 dimensionsType{0.0f};
    glm::vec4 invInertiaMaterial{0.0f};
    glm::vec4 materialCoefficients{-1.0f, -1.0f, -1.0f, 1.0f};
};

static_assert(sizeof(CompactPose) == 32);
static_assert(sizeof(CompactShape) == 48);

using Shape = physics::PhysicsWorld::ThrowableShape;
using detail::GpuInstance;

bool finiteVec(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool finiteMat(const glm::mat4& value) noexcept {
    for (glm::length_t column = 0; column < 4; ++column) {
        for (glm::length_t row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) return false;
        }
    }
    return true;
}

bool validLighting(const PrimitiveLighting& lighting) noexcept {
    const float directionLengthSquared =
        glm::dot(lighting.direction, lighting.direction);
    return finiteVec(lighting.direction)
        && std::isfinite(directionLengthSquared)
        && directionLengthSquared > std::numeric_limits<float>::min()
        && finiteVec(lighting.sunColor)
        && std::isfinite(lighting.sunIntensity)
        && finiteVec(lighting.ambientColor)
        && std::isfinite(lighting.ambientIntensity)
        && finiteVec(lighting.fogColor)
        && std::isfinite(lighting.fogDensity)
        && std::isfinite(lighting.exposure);
}

void appendSphere(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    const uint16_t base = static_cast<uint16_t>(vertices.size());
    for (uint32_t ring = 0; ring <= kSphereRings; ++ring) {
        const float phi = -0.5f * std::numbers::pi_v<float>
                        + std::numbers::pi_v<float> * static_cast<float>(ring)
                        / static_cast<float>(kSphereRings);
        const float y = std::sin(phi);
        const float radius = std::cos(phi);
        for (uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float angle = 2.0f * std::numbers::pi_v<float>
                              * static_cast<float>(segment)
                              / static_cast<float>(kSegments);
            const glm::vec3 normal(radius * std::cos(angle), y,
                                   radius * std::sin(angle));
            vertices.push_back({normal * 0.5f, normal});
        }
    }
    const uint16_t stride = static_cast<uint16_t>(kSegments + 1);
    for (uint16_t ring = 0; ring < kSphereRings; ++ring) {
        for (uint16_t segment = 0; segment < kSegments; ++segment) {
            const uint16_t a = static_cast<uint16_t>(base + ring * stride + segment);
            const uint16_t b = static_cast<uint16_t>(a + stride);
            indices.insert(indices.end(), {a, b, static_cast<uint16_t>(b + 1),
                                           a, static_cast<uint16_t>(b + 1),
                                           static_cast<uint16_t>(a + 1)});
        }
    }
}

void appendBox(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    auto face = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                    glm::vec3 normal) {
        const uint16_t base = static_cast<uint16_t>(vertices.size());
        vertices.insert(vertices.end(), {{a, normal}, {b, normal}, {c, normal}, {d, normal}});
        indices.insert(indices.end(), {base, static_cast<uint16_t>(base + 1),
            static_cast<uint16_t>(base + 2), base,
            static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 3)});
    };
    constexpr float h = 0.5f;
    face({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}, {0,0,1});
    face({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, {0,0,-1});
    face({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h}, {1,0,0});
    face({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}, {-1,0,0});
    face({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}, {0,1,0});
    face({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}, {0,-1,0});
}

void appendCylinder(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    const uint16_t sideBase = static_cast<uint16_t>(vertices.size());
    for (uint32_t y = 0; y < 2; ++y) {
        for (uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float angle = 2.0f * std::numbers::pi_v<float>
                              * static_cast<float>(segment)
                              / static_cast<float>(kSegments);
            const glm::vec3 normal(std::cos(angle), 0.0f, std::sin(angle));
            vertices.push_back({{0.5f * normal.x, y == 0 ? -0.5f : 0.5f,
                                 0.5f * normal.z}, normal});
        }
    }
    const uint16_t stride = static_cast<uint16_t>(kSegments + 1);
    for (uint16_t segment = 0; segment < kSegments; ++segment) {
        const uint16_t a = static_cast<uint16_t>(sideBase + segment);
        const uint16_t b = static_cast<uint16_t>(a + stride);
        indices.insert(indices.end(), {a, b, static_cast<uint16_t>(b + 1),
                                       a, static_cast<uint16_t>(b + 1),
                                       static_cast<uint16_t>(a + 1)});
    }

    for (uint32_t cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? -0.5f : 0.5f;
        const glm::vec3 normal(0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f);
        const uint16_t center = static_cast<uint16_t>(vertices.size());
        vertices.push_back({{0.0f, y, 0.0f}, normal});
        const uint16_t ring = static_cast<uint16_t>(vertices.size());
        for (uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float angle = 2.0f * std::numbers::pi_v<float>
                              * static_cast<float>(segment)
                              / static_cast<float>(kSegments);
            vertices.push_back({{0.5f * std::cos(angle), y,
                                 0.5f * std::sin(angle)}, normal});
        }
        for (uint16_t segment = 0; segment < kSegments; ++segment) {
            if (cap == 0) {
                indices.insert(indices.end(), {center,
                    static_cast<uint16_t>(ring + segment + 1),
                    static_cast<uint16_t>(ring + segment)});
            } else {
                indices.insert(indices.end(), {center,
                    static_cast<uint16_t>(ring + segment),
                    static_cast<uint16_t>(ring + segment + 1)});
            }
        }
    }
}

void appendCapsule(std::vector<Vertex>& vertices, std::vector<uint16_t>& indices) {
    constexpr uint32_t hemisphereRings = 6;
    constexpr float verticalRadius = 0.35f / 1.8f;
    constexpr float center = 0.5f - verticalRadius;
    const uint16_t base = static_cast<uint16_t>(vertices.size());
    const uint32_t totalRings = 2 * (hemisphereRings + 1);

    for (uint32_t ring = 0; ring < totalRings; ++ring) {
        const bool upper = ring > hemisphereRings;
        const uint32_t localRing = upper ? ring - (hemisphereRings + 1) : ring;
        const float phi = upper
            ? (0.5f * std::numbers::pi_v<float> * static_cast<float>(localRing)
               / static_cast<float>(hemisphereRings))
            : (-0.5f * std::numbers::pi_v<float>
               + 0.5f * std::numbers::pi_v<float> * static_cast<float>(localRing)
               / static_cast<float>(hemisphereRings));
        const float capCenter = upper ? center : -center;
        for (uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float angle = 2.0f * std::numbers::pi_v<float>
                              * static_cast<float>(segment)
                              / static_cast<float>(kSegments);
            const float radial = std::cos(phi);
            const glm::vec3 normal(radial * std::cos(angle), std::sin(phi),
                                   radial * std::sin(angle));
            const glm::vec3 position(0.5f * normal.x,
                                     capCenter + verticalRadius * normal.y,
                                     0.5f * normal.z);
            vertices.push_back({position, normal});
        }
    }
    const uint16_t stride = static_cast<uint16_t>(kSegments + 1);
    for (uint32_t ring = 0; ring + 1 < totalRings; ++ring) {
        for (uint32_t segment = 0; segment < kSegments; ++segment) {
            const uint16_t a = static_cast<uint16_t>(
                base + ring * stride + segment);
            const uint16_t b = static_cast<uint16_t>(a + stride);
            indices.insert(indices.end(), {a, b, static_cast<uint16_t>(b + 1),
                                           a, static_cast<uint16_t>(b + 1),
                                           static_cast<uint16_t>(a + 1)});
        }
    }
}

} // namespace

PrimitivePath::~PrimitivePath() { shutdown(); }

bool PrimitivePath::init(WGPUDevice device, WGPUQueue queue,
                         const PrimitivePathConfig& config) {
    if (!device || !queue) return false;
    device_ = device;
    queue_ = queue;
    colorFormat_ = config.colorFormat;
    depthFormat_ = config.depthFormat;
    if (!createGeometry() || !createBuffers()
        || !createLayoutAndPipeline(config)
        || !createCompactLayoutAndPipeline(config)) {
        shutdown();
        return false;
    }
    std::array<PrimitiveDrawGeometry, PrimitiveGpuCulling::kShapeCount> geometry{};
    for (uint32_t shape = 0; shape < geometry.size(); ++shape) {
        geometry[shape] = {ranges_[shape].indexCount, ranges_[shape].firstIndex};
    }
    const std::filesystem::path cullShaderPath = config.cullShaderPath.empty()
        ? config.shaderPath.parent_path() / "physics_primitive_cull.wgsl"
        : config.cullShaderPath;
    if (!gpuCulling_.initialize(device_, queue_, cullShaderPath, geometry)) {
        shutdown();
        return false;
    }
    return true;
}

void PrimitivePath::shutdown() {
    gpuCulling_.shutdown();
    if (compactRenderBundle_) { wgpuRenderBundleRelease(compactRenderBundle_); compactRenderBundle_ = nullptr; }
    if (compactBindGroup_) { wgpuBindGroupRelease(compactBindGroup_); compactBindGroup_ = nullptr; }
    if (compactPipeline_) { wgpuRenderPipelineRelease(compactPipeline_); compactPipeline_ = nullptr; }
    if (compactPipelineLayout_) { wgpuPipelineLayoutRelease(compactPipelineLayout_); compactPipelineLayout_ = nullptr; }
    if (compactBindGroupLayout_) { wgpuBindGroupLayoutRelease(compactBindGroupLayout_); compactBindGroupLayout_ = nullptr; }
    if (compactShaderModule_) { wgpuShaderModuleRelease(compactShaderModule_); compactShaderModule_ = nullptr; }
    if (cpuPoseBuffer_) { wgpuBufferDestroy(cpuPoseBuffer_); wgpuBufferRelease(cpuPoseBuffer_); cpuPoseBuffer_ = nullptr; }
    if (cpuShapeBuffer_) { wgpuBufferDestroy(cpuShapeBuffer_); wgpuBufferRelease(cpuShapeBuffer_); cpuShapeBuffer_ = nullptr; }
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (pipelineLayout_) { wgpuPipelineLayoutRelease(pipelineLayout_); pipelineLayout_ = nullptr; }
    if (bindGroupLayout_) { wgpuBindGroupLayoutRelease(bindGroupLayout_); bindGroupLayout_ = nullptr; }
    if (shaderModule_) { wgpuShaderModuleRelease(shaderModule_); shaderModule_ = nullptr; }
    if (instanceBuffer_) { wgpuBufferRelease(instanceBuffer_); instanceBuffer_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (indexBuffer_) { wgpuBufferRelease(indexBuffer_); indexBuffer_ = nullptr; }
    if (vertexBuffer_) { wgpuBufferRelease(vertexBuffer_); vertexBuffer_ = nullptr; }
    device_ = nullptr;
    queue_ = nullptr;
    rayDepthView_ = nullptr;
    boundRayDepthView_ = nullptr;
    compactBoundPoseBuffer_ = nullptr;
    compactBoundShapeBuffer_ = nullptr;
    compactBoundVisibleBuffer_ = nullptr;
    compactBoundVisibleSegmentCapacity_ = 0;
    compactBoundRayDepthView_ = nullptr;
    physicsRenderView_ = {};
    instanceCache_ = {};
    uploadedInstanceCacheTokens_ = {};
    cpuPoseUpload_ = {};
    cpuShapeUpload_ = {};
    uploadedCpuShapeDimensions_ = {};
    lastUploadStats_ = {};
    lastCompactUploadStats_ = {};
    lastCpuTimings_ = {};
    instanceCapacity_ = 0;
    compactInstanceCapacity_ = 0;
    instanceCount_ = 0;
    instanceBufferContentsValid_ = false;
    cpuShapeBufferContentsValid_ = false;
    colorFormat_ = WGPUTextureFormat_BGRA8Unorm;
    depthFormat_ = WGPUTextureFormat_Depth32Float;
}

bool PrimitivePath::createGeometry() {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;

    auto append = [&](Shape shape, auto generator) {
        auto& range = ranges_[static_cast<size_t>(shape)];
        range.firstIndex = static_cast<uint32_t>(indices.size());
        generator(vertices, indices);
        range.indexCount = static_cast<uint32_t>(indices.size()) - range.firstIndex;
    };

    append(Shape::Sphere, appendSphere);
    append(Shape::Cube, appendBox);
    ranges_[static_cast<size_t>(Shape::Box)].firstIndex =
        ranges_[static_cast<size_t>(Shape::Cube)].firstIndex;
    ranges_[static_cast<size_t>(Shape::Box)].indexCount =
        ranges_[static_cast<size_t>(Shape::Cube)].indexCount;
    append(Shape::Capsule, appendCapsule);
    append(Shape::Cylinder, appendCylinder);

    vertexBuffer_ = gpu::createBufferWithData(
        device_, queue_, gpu::BufferDesc::vertex(vertices.size() * sizeof(Vertex),
                                                  "physics_primitive_vertices"),
        std::span<const Vertex>(vertices));
    indexBuffer_ = gpu::createBufferWithData(
        device_, queue_, gpu::BufferDesc::index(indices.size() * sizeof(uint16_t),
                                                 "physics_primitive_indices"),
        std::span<const uint16_t>(indices));
    return vertexBuffer_ && indexBuffer_;
}

bool PrimitivePath::createBuffers() {
    uniformBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::uniform(sizeof(PrimitiveUniforms),
                                           "physics_primitive_uniforms"));
    instanceCapacity_ = kInitialInstanceCapacity;
    instanceBuffer_ = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(instanceCapacity_ * sizeof(GpuInstance),
                                           true, "physics_primitive_instances"));
    instanceBufferContentsValid_ = false;
    return uniformBuffer_ && instanceBuffer_;
}

bool PrimitivePath::createLayoutAndPipeline(const PrimitivePathConfig& config) {
    std::array<gpu::BindGroupLayoutEntry, 3> entries = {
        gpu::BindGroupLayoutEntry(0).vertexVisible().fragmentVisible()
            .uniformBuffer(false, sizeof(PrimitiveUniforms)),
        gpu::BindGroupLayoutEntry(1).vertexVisible()
            .storageBuffer(true, false, sizeof(GpuInstance)),
        gpu::BindGroupLayoutEntry(2).fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false)
    };
    bindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "physics_primitive_bind_group_layout");
    if (!bindGroupLayout_) return false;

    std::array<WGPUBindGroupLayout, 1> layouts = {bindGroupLayout_};
    pipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "physics_primitive_pipeline_layout");
    shaderModule_ = gpu::loadShaderModule(
        device_, config.shaderPath, "physics_primitives.wgsl");
    if (!pipelineLayout_ || !shaderModule_) return false;

    std::array<WGPUVertexAttribute, 2> attributes{};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(glm::vec3);
    attributes[1].shaderLocation = 1;
    WGPUVertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(Vertex);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = attributes.size();
    vertexBufferLayout.attributes = attributes.data();

    WGPUVertexState vertexState{};
    vertexState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 1;
    vertexState.buffers = &vertexBufferLayout;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = shaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;

    WGPUDepthStencilState depthState{};
    depthState.format = config.depthFormat;
    depthState.depthWriteEnabled = gpu::toOptionalBool(true);
    depthState.depthCompare = WGPUCompareFunction_Less;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthState.stencilBack = depthState.stencilFront;
    depthState.stencilReadMask = 0xFFFFFFFFu;
    depthState.stencilWriteMask = 0xFFFFFFFFu;

    WGPUMultisampleState multisample{};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFFu;

    WGPURenderPipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, "physics_primitive_pipeline");
    desc.layout = pipelineLayout_;
    desc.vertex = vertexState;
    desc.fragment = &fragmentState;
    desc.primitive = primitiveState;
    desc.depthStencil = &depthState;
    desc.multisample = multisample;
    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &desc);
    return pipeline_ != nullptr;
}

bool PrimitivePath::createCompactLayoutAndPipeline(
    const PrimitivePathConfig& config) {
    std::array<gpu::BindGroupLayoutEntry, 5> entries = {
        gpu::BindGroupLayoutEntry(0).vertexVisible().fragmentVisible()
            .uniformBuffer(false, sizeof(PrimitiveUniforms)),
        gpu::BindGroupLayoutEntry(1).vertexVisible()
            .storageBuffer(true, false, sizeof(CompactPose)),
        gpu::BindGroupLayoutEntry(2).vertexVisible()
            .storageBuffer(true, false, sizeof(CompactShape)),
        gpu::BindGroupLayoutEntry(3).vertexVisible()
            .storageBuffer(true, true, sizeof(uint32_t)),
        gpu::BindGroupLayoutEntry(4).fragmentVisible()
            .texture(WGPUTextureSampleType_UnfilterableFloat,
                     WGPUTextureViewDimension_2D, false),
    };
    compactBindGroupLayout_ = gpu::createBindGroupLayout(
        device_, entries, "physics_primitive_compact_bind_group_layout");
    if (!compactBindGroupLayout_) return false;

    const std::array<WGPUBindGroupLayout, 1> layouts{compactBindGroupLayout_};
    compactPipelineLayout_ = gpu::createPipelineLayout(
        device_, layouts, "physics_primitive_compact_pipeline_layout");
    const std::filesystem::path shaderPath = config.compactShaderPath.empty()
        ? config.shaderPath.parent_path() / "physics_primitives_compact.wgsl"
        : config.compactShaderPath;
    compactShaderModule_ = gpu::loadShaderModule(
        device_, shaderPath, "physics_primitives_compact.wgsl");
    if (!compactPipelineLayout_ || !compactShaderModule_) return false;

    std::array<WGPUVertexAttribute, 2> attributes{};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(glm::vec3);
    attributes[1].shaderLocation = 1;
    WGPUVertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(Vertex);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = attributes.size();
    vertexBufferLayout.attributes = attributes.data();

    WGPUVertexState vertexState{};
    vertexState.module = compactShaderModule_;
    WGPU_SET_ENTRY_POINT(vertexState, "vs");
    vertexState.bufferCount = 1;
    vertexState.buffers = &vertexBufferLayout;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = config.colorFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragmentState{};
    fragmentState.module = compactShaderModule_;
    WGPU_SET_ENTRY_POINT(fragmentState, "fs");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPUPrimitiveState primitiveState{};
    primitiveState.topology = WGPUPrimitiveTopology_TriangleList;
    primitiveState.frontFace = WGPUFrontFace_CCW;
    primitiveState.cullMode = WGPUCullMode_None;
    WGPUDepthStencilState depthState{};
    depthState.format = config.depthFormat;
    depthState.depthWriteEnabled = gpu::toOptionalBool(true);
    depthState.depthCompare = WGPUCompareFunction_Less;
    depthState.stencilFront.compare = WGPUCompareFunction_Always;
    depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthState.stencilBack = depthState.stencilFront;
    depthState.stencilReadMask = 0xFFFFFFFFu;
    depthState.stencilWriteMask = 0xFFFFFFFFu;
    WGPUMultisampleState multisample{};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFFu;

    WGPURenderPipelineDescriptor desc{};
    WGPU_SET_LABEL(desc, "physics_primitive_compact_pipeline");
    desc.layout = compactPipelineLayout_;
    desc.vertex = vertexState;
    desc.fragment = &fragmentState;
    desc.primitive = primitiveState;
    desc.depthStencil = &depthState;
    desc.multisample = multisample;
    compactPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &desc);
    return compactPipeline_ != nullptr;
}

void PrimitivePath::setRayDepthTexture(WGPUTextureView view) {
    rayDepthView_ = view;
    boundRayDepthView_ = nullptr;
    compactBoundRayDepthView_ = nullptr;
}

bool PrimitivePath::ensureInstanceCapacity(size_t requiredCapacity) {
    if (requiredCapacity <= instanceCapacity_) return true;

    size_t newCapacity = std::max(instanceCapacity_, kInitialInstanceCapacity);
    while (newCapacity < requiredCapacity) {
        newCapacity *= 2;
    }

    WGPUBuffer newBuffer = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(newCapacity * sizeof(GpuInstance), true,
                                           "physics_primitive_instances"));
    if (!newBuffer) {
        LOG_ERROR("Failed to grow physics primitive instance buffer to {} entries",
                  newCapacity);
        return false;
    }

    if (instanceBuffer_) wgpuBufferRelease(instanceBuffer_);
    instanceBuffer_ = newBuffer;
    instanceCapacity_ = newCapacity;
    instanceBufferContentsValid_ = false;
    boundRayDepthView_ = nullptr;
    return true;
}

bool PrimitivePath::ensureCompactInstanceCapacity(size_t requiredCapacity) {
    if (requiredCapacity <= compactInstanceCapacity_
        && cpuPoseBuffer_ && cpuShapeBuffer_) return true;
    size_t newCapacity = std::max<size_t>(kInitialInstanceCapacity,
                                         compactInstanceCapacity_);
    while (newCapacity < requiredCapacity) newCapacity *= 2;
    WGPUBuffer newPoseBuffer = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            newCapacity * sizeof(CompactPose), true,
            "physics_primitive_cpu_compact_poses"));
    WGPUBuffer newShapeBuffer = gpu::createBuffer(
        device_, gpu::BufferDesc::storage(
            newCapacity * sizeof(CompactShape), true,
            "physics_primitive_cpu_compact_shapes"));
    if (!newPoseBuffer || !newShapeBuffer) {
        if (newPoseBuffer) {
            wgpuBufferDestroy(newPoseBuffer);
            wgpuBufferRelease(newPoseBuffer);
        }
        if (newShapeBuffer) {
            wgpuBufferDestroy(newShapeBuffer);
            wgpuBufferRelease(newShapeBuffer);
        }
        LOG_ERROR("Failed to grow compact physics buffers to {} bodies",
                  newCapacity);
        return false;
    }
    if (cpuPoseBuffer_) {
        wgpuBufferDestroy(cpuPoseBuffer_);
        wgpuBufferRelease(cpuPoseBuffer_);
    }
    if (cpuShapeBuffer_) {
        wgpuBufferDestroy(cpuShapeBuffer_);
        wgpuBufferRelease(cpuShapeBuffer_);
    }
    cpuPoseBuffer_ = newPoseBuffer;
    cpuShapeBuffer_ = newShapeBuffer;
    compactInstanceCapacity_ = newCapacity;
    compactBoundPoseBuffer_ = nullptr;
    compactBoundShapeBuffer_ = nullptr;
    cpuShapeBufferContentsValid_ = false;
    return true;
}

void PrimitivePath::updateBindGroup() {
    if (!rayDepthView_ || boundRayDepthView_ == rayDepthView_) return;
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    std::array<gpu::BindGroupEntry, 3> entries = {
        gpu::BindGroupEntry(0).buffer(uniformBuffer_, 0, sizeof(PrimitiveUniforms)),
        gpu::BindGroupEntry(1).buffer(instanceBuffer_, 0,
                                      instanceCapacity_ * sizeof(GpuInstance)),
        gpu::BindGroupEntry(2).textureView(rayDepthView_)
    };
    bindGroup_ = gpu::createBindGroup(
        device_, bindGroupLayout_, entries, "physics_primitive_bind_group");
    if (bindGroup_) boundRayDepthView_ = rayDepthView_;
}

void PrimitivePath::updateCompactBindGroup() {
    const WGPUBuffer visibleBuffer = gpuCulling_.visibleBodyIds();
    const WGPUBuffer renderPoseBuffer = gpuCulling_.renderPoseBuffer();
    const uint32_t visibleSegmentCapacity = gpuCulling_.segmentCapacity();
    if (!rayDepthView_ || !physicsRenderView_.valid() || !visibleBuffer
        || !renderPoseBuffer)
        return;
    if (compactBindGroup_
        && compactBoundPoseBuffer_ == renderPoseBuffer
        && compactBoundShapeBuffer_ == physicsRenderView_.shapeBuffer
        && compactBoundVisibleBuffer_ == visibleBuffer
        && compactBoundVisibleSegmentCapacity_ == visibleSegmentCapacity
        && compactBoundRayDepthView_ == rayDepthView_) return;
    if (compactBindGroup_) {
        if (compactRenderBundle_) {
            wgpuRenderBundleRelease(compactRenderBundle_);
            compactRenderBundle_ = nullptr;
        }
        wgpuBindGroupRelease(compactBindGroup_);
        compactBindGroup_ = nullptr;
    }
    const uint64_t visibleSegmentBytes =
        uint64_t{visibleSegmentCapacity} * sizeof(uint32_t);
    const std::array<gpu::BindGroupEntry, 5> entries = {
        gpu::BindGroupEntry(0).buffer(
            uniformBuffer_, 0, sizeof(PrimitiveUniforms)),
        gpu::BindGroupEntry(1).buffer(renderPoseBuffer),
        gpu::BindGroupEntry(2).buffer(physicsRenderView_.shapeBuffer),
        gpu::BindGroupEntry(3).buffer(
            visibleBuffer, 0, visibleSegmentBytes),
        gpu::BindGroupEntry(4).textureView(rayDepthView_),
    };
    compactBindGroup_ = gpu::createBindGroup(
        device_, compactBindGroupLayout_, entries,
        "physics_primitive_compact_bind_group");
    if (compactBindGroup_) {
        compactBoundPoseBuffer_ = renderPoseBuffer;
        compactBoundShapeBuffer_ = physicsRenderView_.shapeBuffer;
        compactBoundVisibleBuffer_ = visibleBuffer;
        compactBoundVisibleSegmentCapacity_ = visibleSegmentCapacity;
        compactBoundRayDepthView_ = rayDepthView_;
        static_cast<void>(rebuildCompactRenderBundle());
    }
}

bool PrimitivePath::rebuildCompactRenderBundle() {
    if (compactRenderBundle_) {
        wgpuRenderBundleRelease(compactRenderBundle_);
        compactRenderBundle_ = nullptr;
    }
    if (!compactBindGroup_ || !compactPipeline_ || !vertexBuffer_
        || !indexBuffer_ || !gpuCulling_.indirectDrawArgs()) {
        return false;
    }

    WGPURenderBundleEncoderDescriptor encoderDesc{};
    WGPU_SET_LABEL(encoderDesc, "physics_primitive_compact_bundle_encoder");
    encoderDesc.colorFormatCount = 1u;
    encoderDesc.colorFormats = &colorFormat_;
    encoderDesc.depthStencilFormat = depthFormat_;
    encoderDesc.sampleCount = 1u;
    encoderDesc.depthReadOnly = false;
    encoderDesc.stencilReadOnly = true;
    WGPURenderBundleEncoder bundleEncoder =
        wgpuDeviceCreateRenderBundleEncoder(device_, &encoderDesc);
    if (!bundleEncoder) return false;

    wgpuRenderBundleEncoderSetPipeline(bundleEncoder, compactPipeline_);
    wgpuRenderBundleEncoderSetVertexBuffer(
        bundleEncoder, 0, vertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderBundleEncoderSetIndexBuffer(
        bundleEncoder, indexBuffer_, WGPUIndexFormat_Uint16,
        0, WGPU_WHOLE_SIZE);
    for (uint32_t shape = 0; shape < ranges_.size(); ++shape) {
        const uint32_t visibleOffset = shape
            * gpuCulling_.segmentCapacity() * sizeof(uint32_t);
        wgpuRenderBundleEncoderSetBindGroup(
            bundleEncoder, 0, compactBindGroup_, 1, &visibleOffset);
        wgpuRenderBundleEncoderDrawIndexedIndirect(
            bundleEncoder, gpuCulling_.indirectDrawArgs(),
            uint64_t{shape} * 5u * sizeof(uint32_t));
    }

    WGPURenderBundleDescriptor bundleDesc{};
    WGPU_SET_LABEL(bundleDesc, "physics_primitive_compact_bundle");
    compactRenderBundle_ =
        wgpuRenderBundleEncoderFinish(bundleEncoder, &bundleDesc);
    wgpuRenderBundleEncoderRelease(bundleEncoder);
    return compactRenderBundle_ != nullptr;
}

void PrimitivePath::setCompactPhysicsInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    lastCompactUploadStats_ = {};
    const size_t validBodyCount = static_cast<size_t>(std::count_if(
        bodies.begin(), bodies.end(), detail::isRenderablePrimitiveSnapshot));
    if (validBodyCount == 0u
        || validBodyCount > std::numeric_limits<uint32_t>::max()) {
        clearPhysicsRenderView();
        return;
    }
    if (!ensureCompactInstanceCapacity(validBodyCount)) {
        clearPhysicsRenderView();
        return;
    }
    cpuPoseUpload_.resize(validBodyCount * 2u);
    cpuShapeUpload_.resize(validBodyCount * 2u);
    const bool fullShapeUpload = !cpuShapeBufferContentsValid_;
    size_t firstDirtyShape = fullShapeUpload ? 0u : validBodyCount;
    size_t lastDirtyShape = fullShapeUpload ? validBodyCount : 0u;
    const size_t previousShapeCount = uploadedCpuShapeDimensions_.size();
    uploadedCpuShapeDimensions_.resize(validBodyCount);
    size_t index = 0u;
    for (const auto& body : bodies) {
        if (!detail::isRenderablePrimitiveSnapshot(body)) continue;
        cpuPoseUpload_[index * 2u] = glm::vec4(body.position, 1.0f);
        const float rotationLengthSquared =
            glm::dot(body.rotation, body.rotation);
        const glm::quat rotation =
            rotationLengthSquared > std::numeric_limits<float>::min()
                ? glm::normalize(body.rotation)
                : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        cpuPoseUpload_[index * 2u + 1u] = glm::vec4(
            rotation.x, rotation.y, rotation.z, rotation.w);
        const glm::vec4 dimensionsType(
            body.dimensions, static_cast<float>(body.shape));
        const bool shapeChanged = fullShapeUpload
            || index >= previousShapeCount
            || std::memcmp(&dimensionsType,
                           &uploadedCpuShapeDimensions_[index],
                           sizeof(glm::vec4)) != 0;
        if (shapeChanged) {
            firstDirtyShape = std::min(firstDirtyShape, index);
            lastDirtyShape = index + 1u;
            cpuShapeUpload_[index * 2u] = dimensionsType;
            cpuShapeUpload_[index * 2u + 1u] = glm::vec4(0.0f);
        }
        uploadedCpuShapeDimensions_[index] = dimensionsType;
        ++index;
    }
    if (!gpu::writeBuffer(
            queue_, cpuPoseBuffer_, 0,
            std::as_bytes(std::span<const glm::vec4>(cpuPoseUpload_)))) {
        cpuShapeBufferContentsValid_ = false;
        clearPhysicsRenderView();
        return;
    }

    uint32_t writeCalls = 1u;
    size_t bytesUploaded = validBodyCount * sizeof(CompactPose);
    if (firstDirtyShape < lastDirtyShape) {
        const auto shapes = std::span<const glm::vec4>(cpuShapeUpload_)
            .subspan(firstDirtyShape * 2u,
                     (lastDirtyShape - firstDirtyShape) * 2u);
        if (!gpu::writeBuffer(
                queue_, cpuShapeBuffer_,
                firstDirtyShape * sizeof(CompactShape),
                std::as_bytes(shapes))) {
            cpuShapeBufferContentsValid_ = false;
            clearPhysicsRenderView();
            return;
        }
        ++writeCalls;
        bytesUploaded +=
            (lastDirtyShape - firstDirtyShape) * sizeof(CompactShape);
    }
    cpuShapeBufferContentsValid_ = true;
    lastCompactUploadStats_ = {
        bytesUploaded, writeCalls, fullShapeUpload};
    if (!gpuCulling_.setBodyView({
        .poseBuffer = cpuPoseBuffer_,
        .shapeBuffer = cpuShapeBuffer_,
        .residentBodyCapacity = static_cast<uint32_t>(validBodyCount),
        .shapeCount = static_cast<uint32_t>(Shape::Count),
    })) {
        clearPhysicsRenderView();
        return;
    }
    physicsRenderView_ = {
        .poseBuffer = cpuPoseBuffer_,
        .shapeBuffer = cpuShapeBuffer_,
        .residentBodyCapacity = static_cast<uint32_t>(validBodyCount),
        .shapeCount = static_cast<uint32_t>(Shape::Count),
    };
}

void PrimitivePath::setPhysicsRenderView(
    const physics::PhysicsRenderView& view) {
    if (!gpuCulling_.setBodyView(view)) return;
    if (physicsRenderView_.poseBuffer != view.poseBuffer
        || physicsRenderView_.shapeBuffer != view.shapeBuffer
        || physicsRenderView_.metadataBuffer != view.metadataBuffer) {
        compactBoundPoseBuffer_ = nullptr;
        compactBoundShapeBuffer_ = nullptr;
    }
    physicsRenderView_ = view;
}

void PrimitivePath::clearPhysicsRenderView() {
    (void)gpuCulling_.setBodyView({});
    physicsRenderView_ = {};
}

void PrimitivePath::setInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    using Clock = std::chrono::steady_clock;
    lastUploadStats_ = {};
    lastCpuTimings_ = {};
    const auto packingStart = Clock::now();
    auto batch = detail::packPrimitiveInstances(bodies, instanceCache_);
    for (uint32_t shapeIndex = 0; shapeIndex < ranges_.size(); ++shapeIndex) {
        auto& range = ranges_[shapeIndex];
        range.firstInstance = batch.firstInstances[shapeIndex];
        range.instanceCount = batch.instanceCounts[shapeIndex];
    }
    lastCpuTimings_.packingMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - packingStart).count();

    const auto uploadStart = Clock::now();
    instanceCount_ = static_cast<uint32_t>(batch.instances.size());
    if (!ensureInstanceCapacity(batch.instances.size())) {
        instanceCount_ = 0;
        lastCpuTimings_.uploadMs =
            std::chrono::duration<double, std::milli>(
                Clock::now() - uploadStart).count();
        return;
    }

    const auto uploadPlan = detail::planPrimitiveInstanceUpload(
        batch.instances.size(), batch.cacheTokens,
        uploadedInstanceCacheTokens_, instanceBufferContentsValid_,
        batch.forceFullUpload);
    bool uploadSucceeded = true;
    if (uploadPlan.fullUpload) {
        if (!batch.instances.empty()) {
            uploadSucceeded = gpu::writeBuffer(
                queue_, instanceBuffer_, 0,
                std::as_bytes(
                    std::span<const GpuInstance>(batch.instances)));
        }
        if (uploadSucceeded) {
            lastUploadStats_ = {
                uploadPlan.byteCount,
                batch.instances.empty() ? 0u : 1u,
                true};
        }
    } else {
        for (size_t rangeIndex = 0;
             rangeIndex < uploadPlan.rangeCount; ++rangeIndex) {
            const auto& range = uploadPlan.ranges[rangeIndex];
            const auto instances = std::span<const GpuInstance>(batch.instances)
                                       .subspan(range.firstInstance,
                                                range.instanceCount);
            if (!gpu::writeBuffer(
                    queue_, instanceBuffer_,
                    range.firstInstance * sizeof(GpuInstance),
                    std::as_bytes(instances))) {
                uploadSucceeded = false;
                break;
            }
        }
        if (uploadSucceeded) {
            lastUploadStats_ = {
                uploadPlan.byteCount,
                static_cast<uint32_t>(uploadPlan.rangeCount), false};
        }
    }
    if (!uploadSucceeded) {
        instanceCount_ = 0;
        instanceBufferContentsValid_ = false;
        lastUploadStats_ = {};
        lastCpuTimings_.uploadMs =
            std::chrono::duration<double, std::milli>(
                Clock::now() - uploadStart).count();
        return;
    }
    uploadedInstanceCacheTokens_ = std::move(batch.cacheTokens);
    instanceBufferContentsValid_ = instanceBuffer_ != nullptr;
    lastCpuTimings_.uploadMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - uploadStart).count();
}

void PrimitivePath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                           WGPUTextureView depthView, const glm::mat4& view,
                           const glm::mat4& projection,
                           const glm::vec3& cameraPosition,
                           const glm::vec3& lightDirection, uint32_t width,
                           uint32_t height, bool useRayDepth,
                           const glm::ivec3& cameraSector,
                           WGPUQuerySet timestampQuerySet,
                           uint32_t timestampBegin,
                           uint32_t timestampEnd) {
    PrimitiveLighting lighting;
    lighting.direction = lightDirection;
    render(encoder, colorView, depthView, view, projection, cameraPosition,
           lighting, width, height, useRayDepth, cameraSector,
           timestampQuerySet, timestampBegin, timestampEnd);
}

void PrimitivePath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                           WGPUTextureView depthView, const glm::mat4& view,
                           const glm::mat4& projection,
                           const glm::vec3& cameraPosition,
                           const PrimitiveLighting& lighting, uint32_t width,
                           uint32_t height, bool useRayDepth,
                           const glm::ivec3& cameraSector,
                           WGPUQuerySet timestampQuerySet,
                           uint32_t timestampBegin,
                           uint32_t timestampEnd) {
    if (!pipeline_ || !compactPipeline_ || !encoder || !colorView || !depthView)
        return;
    const glm::mat4 viewProjection = projection * view;
    if (width == 0u || height == 0u || width > 8'192u || height > 8'192u
        || !finiteMat(view) || !finiteMat(projection)
        || !finiteMat(viewProjection) || !finiteVec(cameraPosition)
        || !validLighting(lighting)
        || !Frustum::fromViewProj(viewProjection).valid()) {
        LOG_ERROR("PrimitivePath::render: invalid frame inputs");
        return;
    }

    bool overlayReady = instanceCount_ != 0;
    if (overlayReady) {
        updateBindGroup();
        overlayReady = bindGroup_ != nullptr;
    }
    const bool compactCandidate = physicsRenderView_.valid()
        && physicsRenderView_.residentBodyCapacity != 0;
    if (!overlayReady && !compactCandidate) return;

    PrimitiveUniforms uniforms;
    uniforms.viewProj = viewProjection;
    uniforms.cameraPos = glm::vec4(cameraPosition, 1.0f);
    uniforms.lightDirAndRayDepth = glm::vec4(glm::normalize(lighting.direction),
                                             useRayDepth ? 1.0f : 0.0f);
    uniforms.viewport = glm::vec4(static_cast<float>(width),
                                  static_cast<float>(height),
                                  std::max(lighting.fogDensity, 0.0f), 0.0f);
    uniforms.lightingColor = glm::vec4(
        glm::max(lighting.sunColor, glm::vec3(0.0f)),
        std::max(lighting.sunIntensity, 0.0f));
    uniforms.ambientColor = glm::vec4(
        glm::max(lighting.ambientColor, glm::vec3(0.0f)),
        std::max(lighting.ambientIntensity, 0.0f));
    uniforms.fogColorExposure = glm::vec4(
        glm::max(lighting.fogColor, glm::vec3(0.0f)),
        std::max(lighting.exposure, 0.0f));
    if (!gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms)) return;

    bool compactReady = false;
    if (compactCandidate) {
        compactReady = gpuCulling_.encode(
            encoder, viewProjection, cameraSector);
        if (compactReady) {
            updateCompactBindGroup();
            compactReady = compactBindGroup_ != nullptr;
        }
    }
    if (!overlayReady && !compactReady) return;

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = useRayDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor passDesc{};
    WGPU_SET_LABEL(passDesc, "physics_primitive_pass");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;
    gpu::CompatRenderPassTimestampWrites timestampWrites{};
    if (timestampQuerySet) {
        timestampWrites.querySet = timestampQuerySet;
        timestampWrites.beginningOfPassWriteIndex = timestampBegin;
        timestampWrites.endOfPassWriteIndex = timestampEnd;
        passDesc.timestampWrites = &timestampWrites;
    }
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        LOG_ERROR("PrimitivePath::render: failed to begin render pass");
        return;
    }
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer_, WGPUIndexFormat_Uint16,
                                        0, WGPU_WHOLE_SIZE);
    if (compactReady) {
        if (compactRenderBundle_) {
            wgpuRenderPassEncoderExecuteBundles(
                pass, 1u, &compactRenderBundle_);
        } else {
            // Render bundles are core WebGPU, but retain the direct path for
            // implementations that reject bundle creation at runtime.
            wgpuRenderPassEncoderSetPipeline(pass, compactPipeline_);
            for (uint32_t shape = 0; shape < ranges_.size(); ++shape) {
                const uint32_t visibleOffset = shape
                    * gpuCulling_.segmentCapacity() * sizeof(uint32_t);
                wgpuRenderPassEncoderSetBindGroup(
                    pass, 0, compactBindGroup_, 1, &visibleOffset);
                wgpuRenderPassEncoderDrawIndexedIndirect(
                    pass, gpuCulling_.indirectDrawArgs(),
                    uint64_t{shape} * 5u * sizeof(uint32_t));
            }
        }
    }
    if (overlayReady) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        // Render bundles have isolated state. Rebind the overlay geometry
        // after executing the compact-body bundle.
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 0, vertexBuffer_, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(
            pass, indexBuffer_, WGPUIndexFormat_Uint16,
            0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
        for (const DrawRange& range : ranges_) {
            if (range.instanceCount == 0) continue;
            wgpuRenderPassEncoderDrawIndexed(
                pass, range.indexCount, range.instanceCount,
                range.firstIndex, 0, range.firstInstance);
        }
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

} // namespace voxy::render

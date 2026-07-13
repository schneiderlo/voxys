#include "render/primitive_path.hpp"

#include "core/log.hpp"
#include "gpu/resources.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <algorithm>
#include <cmath>
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

struct alignas(16) GpuInstance {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct alignas(16) PrimitiveUniforms {
    glm::mat4 viewProj{1.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 lightDirAndRayDepth{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 viewport{1.0f};
};

static_assert(sizeof(Vertex) == 24);
static_assert(sizeof(GpuInstance) == 80);
static_assert(sizeof(PrimitiveUniforms) == 112);

using Shape = physics::PhysicsWorld::ThrowableShape;

glm::vec4 shapeColor(Shape shape) {
    switch (shape) {
        case Shape::Sphere: return {0.95f, 0.28f, 0.18f, 1.0f};
        case Shape::Cube: return {0.20f, 0.62f, 0.95f, 1.0f};
        case Shape::Box: return {0.96f, 0.70f, 0.16f, 1.0f};
        case Shape::Capsule: return {0.42f, 0.85f, 0.36f, 1.0f};
        case Shape::Cylinder: return {0.68f, 0.38f, 0.92f, 1.0f};
        case Shape::Count: break;
    }
    return glm::vec4(1.0f);
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
    if (!createGeometry() || !createBuffers()
        || !createLayoutAndPipeline(config)) {
        shutdown();
        return false;
    }
    return true;
}

void PrimitivePath::shutdown() {
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
    instanceCapacity_ = 0;
    instanceCount_ = 0;
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

void PrimitivePath::setRayDepthTexture(WGPUTextureView view) {
    rayDepthView_ = view;
    boundRayDepthView_ = nullptr;
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
    boundRayDepthView_ = nullptr;
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

void PrimitivePath::setInstances(
    std::span<const physics::PhysicsWorld::DynamicBodySnapshot> bodies) {
    std::vector<GpuInstance> instances;
    instances.reserve(bodies.size());
    for (auto& range : ranges_) {
        range.firstInstance = 0;
        range.instanceCount = 0;
    }

    for (uint32_t shapeIndex = 0;
         shapeIndex < static_cast<uint32_t>(Shape::Count); ++shapeIndex) {
        auto& range = ranges_[shapeIndex];
        range.firstInstance = static_cast<uint32_t>(instances.size());
        for (const auto& body : bodies) {
            if (static_cast<uint32_t>(body.shape) != shapeIndex) continue;
            GpuInstance instance;
            instance.model = glm::translate(glm::mat4(1.0f), body.position)
                           * glm::mat4_cast(body.rotation)
                           * glm::scale(glm::mat4(1.0f), body.dimensions);
            instance.color = shapeColor(body.shape);
            instances.push_back(instance);
            ++range.instanceCount;
        }
    }

    instanceCount_ = static_cast<uint32_t>(instances.size());
    if (!ensureInstanceCapacity(instances.size())) {
        instanceCount_ = 0;
        return;
    }
    if (!instances.empty()) {
        gpu::writeBuffer(queue_, instanceBuffer_, 0,
                         std::as_bytes(std::span<const GpuInstance>(instances)));
    }
}

void PrimitivePath::render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                           WGPUTextureView depthView, const glm::mat4& view,
                           const glm::mat4& projection,
                           const glm::vec3& cameraPosition,
                           const glm::vec3& lightDirection, uint32_t width,
                           uint32_t height, bool useRayDepth) {
    if (!pipeline_ || !encoder || !colorView || !depthView || instanceCount_ == 0) return;
    updateBindGroup();
    if (!bindGroup_) return;

    PrimitiveUniforms uniforms;
    uniforms.viewProj = projection * view;
    uniforms.cameraPos = glm::vec4(cameraPosition, 1.0f);
    uniforms.lightDirAndRayDepth = glm::vec4(glm::normalize(lightDirection),
                                             useRayDepth ? 1.0f : 0.0f);
    uniforms.viewport = glm::vec4(static_cast<float>(width),
                                  static_cast<float>(height), 0.0f, 0.0f);
    gpu::writeBuffer(queue_, uniformBuffer_, 0, uniforms);

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
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer_, WGPUIndexFormat_Uint16,
                                        0, WGPU_WHOLE_SIZE);
    for (const DrawRange& range : ranges_) {
        if (range.instanceCount == 0) continue;
        wgpuRenderPassEncoderDrawIndexed(pass, range.indexCount, range.instanceCount,
                                         range.firstIndex, 0, range.firstInstance);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

} // namespace voxy::render

#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "moto/vmesh.hpp"
#include "render/mesh_path.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace voxy::render {

TEST(MeshPathTest, RejectsNullGpuHandlesAndInvalidData) {
    MeshPath path;
    EXPECT_FALSE(path.init(nullptr, nullptr));
}

TEST(MeshPathGPUTest, LoadsAndRendersAuthoredBikeAndRider) {
    gpu::Context context;
    if (!context.initHeadless()) {
        GTEST_SKIP() << "GPU context not available";
    }

    MeshPathConfig config;
    config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    for (const auto& candidate : {
             std::filesystem::path("shaders/mesh_path.wgsl"),
             std::filesystem::path("../shaders/mesh_path.wgsl"),
             std::filesystem::path("../../shaders/mesh_path.wgsl")}) {
        if (std::filesystem::exists(candidate)) {
            config.shaderPath = candidate;
            break;
        }
    }
    ASSERT_TRUE(std::filesystem::exists(config.shaderPath));

    MeshPath path;
    ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    ASSERT_TRUE(path.loadMesh("data/moto/bike.vmesh"));
    ASSERT_TRUE(path.loadMesh("data/moto/rider.vmesh"));
    ASSERT_TRUE(path.loadMesh("data/moto/track.vmesh"));

    // Exercise all material texture uploads, mip generation, tangent-space
    // normals, and the alpha-blend pipeline. The authored bike currently uses
    // factor-only materials, so a synthetic textured primitive is required to
    // keep these production paths covered.
    moto::VmeshData textured;
    textured.header.flags = moto::kVmeshHasTangent;
    textured.header.vertexCount = 3u;
    textured.header.indexCount = 3u;
    textured.header.indexStride = 2u;
    textured.header.submeshCount = 1u;
    textured.header.materialCount = 1u;
    textured.header.meshCount = 1u;
    const std::array<moto::VmeshVertex, 3> texturedVertices = {{
        {{-0.4f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {}, {}},
        {{0.4f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {}, {}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}, {}, {}},
    }};
    textured.vertices.resize(sizeof(texturedVertices));
    std::memcpy(textured.vertices.data(), texturedVertices.data(),
                sizeof(texturedVertices));
    const std::array<uint16_t, 3> texturedIndices = {0u, 1u, 2u};
    textured.indices.resize(sizeof(texturedIndices));
    std::memcpy(textured.indices.data(), texturedIndices.data(),
                sizeof(texturedIndices));
    textured.submeshes.push_back({0u, 3u, 0u, 0u});
    textured.materials.resize(1u);
    moto::VmeshMaterial& texturedMaterial = textured.materials[0];
    texturedMaterial.alphaMode = moto::VmeshAlphaBlend;
    texturedMaterial.baseColorFactor[3] = 0.75f;
    const std::array<std::array<uint8_t, 16>, moto::VmeshTextureCount>
        texturePixels = {{
            {{220u, 80u, 24u, 224u, 180u, 48u, 16u, 224u,
              240u, 120u, 32u, 224u, 200u, 64u, 20u, 224u}},
            {{128u, 128u, 255u, 255u, 144u, 128u, 254u, 255u,
              128u, 144u, 254u, 255u, 112u, 128u, 254u, 255u}},
            {{255u, 160u, 48u, 255u, 255u, 128u, 64u, 255u,
              255u, 192u, 32u, 255u, 255u, 144u, 80u, 255u}},
            {{16u, 4u, 1u, 255u, 24u, 6u, 2u, 255u,
              8u, 2u, 1u, 255u, 20u, 5u, 1u, 255u}},
        }};
    for (uint32_t slot = 0u; slot < moto::VmeshTextureCount; ++slot) {
        texturedMaterial.hasTexture[slot] = 1u;
        texturedMaterial.textureIsSrgb[slot] =
            slot == moto::VmeshTextureBaseColor
                    || slot == moto::VmeshTextureEmissive
                ? 1u : 0u;
        texturedMaterial.textureWidth[slot] = 2u;
        texturedMaterial.textureHeight[slot] = 2u;
        texturedMaterial.textureOffset[slot] =
            static_cast<uint32_t>(textured.images.size());
        texturedMaterial.textureSize[slot] = 16u;
        textured.images.insert(textured.images.end(),
                               texturePixels[slot].begin(),
                               texturePixels[slot].end());
    }
    std::vector<uint8_t> texturedBytes;
    std::string texturedError;
    ASSERT_TRUE(moto::writeVmesh(
        textured, &texturedBytes, &texturedError)) << texturedError;
    ASSERT_TRUE(path.loadMeshFromBytes(texturedBytes));
    ASSERT_EQ(path.assetCount(), 4u);

    for (uint32_t mesh = 0u; mesh < 52u; ++mesh) {
        path.addInstance({.assetIndex = 0u, .meshIndex = mesh});
    }
    for (uint32_t mesh = 0u; mesh < 24u; ++mesh) {
        path.addInstance({.assetIndex = 1u, .meshIndex = mesh});
    }
    path.addInstance({.assetIndex = 2u, .meshIndex = 0u});
    MeshDrawInstance culledInstance;
    culledInstance.assetIndex = 2u;
    culledInstance.meshIndex = 0u;
    culledInstance.modelMatrix = glm::translate(
        glm::mat4(1.0f), glm::vec3(10000.0f, 0.0f, 0.0f));
    path.addInstance(culledInstance);

    constexpr uint32_t extent = 128u;
    WGPUTexture colorTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::renderTarget(
            extent, extent, WGPUTextureFormat_RGBA8Unorm,
            "mesh_path_test_color"));
    WGPUTexture depthTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::depth(
            extent, extent, WGPUTextureFormat_Depth32Float,
            "mesh_path_test_depth"));
    ASSERT_NE(colorTexture, nullptr);
    ASSERT_NE(depthTexture, nullptr);
    WGPUTextureView colorView = gpu::createTextureView(colorTexture);
    WGPUTextureView depthView = gpu::createTextureView(depthTexture);
    ASSERT_NE(colorView, nullptr);
    ASSERT_NE(depthView, nullptr);

    WGPUCommandEncoderDescriptor encoderDescriptor{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDescriptor);
    ASSERT_NE(encoder, nullptr);

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor clearDescriptor{};
    clearDescriptor.colorAttachmentCount = 1u;
    clearDescriptor.colorAttachments = &colorAttachment;
    clearDescriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder clearPass =
        wgpuCommandEncoderBeginRenderPass(encoder, &clearDescriptor);
    ASSERT_NE(clearPass, nullptr);
    wgpuRenderPassEncoderEnd(clearPass);
    wgpuRenderPassEncoderRelease(clearPass);

    const glm::vec3 cameraPosition(3.0f, 2.2f, -4.5f);
    path.render(
        encoder, colorView, depthView,
        glm::lookAt(cameraPosition, glm::vec3(0.0f, 0.8f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 100.0f),
        cameraPosition, glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)),
        extent, extent, true);
    EXPECT_EQ(path.lastCulledInstanceCount(), 1u);
    EXPECT_EQ(path.lastSubmittedDrawCount(), 77u);

    WGPUCommandBufferDescriptor commandDescriptor{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDescriptor);
    ASSERT_NE(command, nullptr);
    wgpuQueueSubmit(context.getQueue(), 1u, &command);
    context.tick();

    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(depthView);
    wgpuTextureViewRelease(colorView);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
    wgpuTextureDestroy(colorTexture);
    wgpuTextureRelease(colorTexture);
    path.shutdown();
    context.shutdown();
}

}  // namespace voxy::render

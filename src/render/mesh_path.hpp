// ═══════════════════════════════════════════════════════════════════════════════
// mesh_path.hpp - RIDGEBREAK mesh renderer
// ═══════════════════════════════════════════════════════════════════════════════
// Draws .vmesh models (bike, rider, environment) with a metallic-roughness
// PBR material model and the scene's shared sun + sky environment. Mirrors the
// PrimitivePath integration contract: renders into the same color/depth targets
// after the terrain/water blit, honoring useRayDepth so the bike occludes
// correctly against the raycast terrain.

#pragma once

#include "render/scene_shadows.hpp"

#include "moto/vmesh.hpp"
#include "render/environment_lighting.hpp"
#include "physics/physics_types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#if defined(VOXY_WASM)
    #include <webgpu/webgpu.h>
#else
    #include <webgpu.h>
#endif

namespace voxy::render {

struct PrimitiveLighting;

struct MeshPathConfig {
    std::filesystem::path shaderPath = "shaders/mesh_path.wgsl";
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float;
    // glTF's outward winding projects clockwise with the project's LH camera.
    // Keep historical CCW as default; admitted salvage rigid fixtures select CW.
    WGPUFrontFace frontFace = WGPUFrontFace_CCW;
    uint32_t maxInstances = 256;
    uint32_t maxDrawsPerFrame = 512;
    // Explicit diagnostic path: ignore attachment depth and never write it.
    // Call render with useRayDepth=false as well for a true X-ray overlay.
    bool depthOverlay = false;
    bool linearHdrOutput = false; // Opaque/masked RGBA16F + R32F radial depth for water.
    bool filteredEnvironment = false; // Explicit opt-in; legacy routes keep their lighting.
    bool sunShadows = false; // Live authored meshes cast/receive; no terrain-cache writes.
};

/// One draw instance: a mesh rendered with a model matrix and a color tint.
/// The application resolves its node hierarchy before submitting instances, so
/// the renderer stays a simple instanced drawer.
struct MeshDrawInstance {
    uint32_t assetIndex = 0;     // loaded .vmesh asset
    uint32_t meshIndex = 0;      // logical glTF mesh inside that asset
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 tintColor{1.0f};   // rgba multiplier over the material factors
    float emissiveBoost = 0.0f;  // added to emissive contribution
    uint32_t pad[3] = {};
    physics::BodyHandle physicsBody{}; // If valid, modelMatrix is root-local.
    bool castsSunShadow = true;
};

/// A loaded mesh part (one glTF mesh). The importer flattens multi-primitive
/// meshes into the submesh list; the renderer draws one drawcall per submesh.
struct MeshAsset {
    // GPU handles owned by MeshPath.
    // Vertex buffer layout is fixed to VmeshVertex.
    // submeshes map to draw ranges.
    std::vector<uint32_t> submeshMaterialIndex;  // parallel to submeshes
    // World-space bounds for culling (computed at upload).
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

class MeshPath {
public:
    MeshPath() = default;
    ~MeshPath();

    MeshPath(const MeshPath&) = delete;
    MeshPath& operator=(const MeshPath&) = delete;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const MeshPathConfig& config = {});
    void shutdown();
    /// Exceptional teardown while commands may still reference resources.
    /// Release external refs without Destroy; WebGPU retains internal command
    /// and bind-group dependencies. This does not certify queue completion.
    void releaseHandles();
    [[nodiscard]] bool isInitialized() const noexcept {
        return opaquePipeline_ != nullptr && blendPipeline_ != nullptr;
    }

    /// Load a .vmesh file (native loads from disk; WASM callers pass the
    /// preloaded bytes). Uploads geometry, materials, and textures.
    [[nodiscard]] bool loadMesh(const std::filesystem::path& path);
    [[nodiscard]] bool loadMeshFromBytes(const std::vector<uint8_t>& bytes);
    /// Upload an already parsed, owned snapshot. Performs the same drawable
    /// validation as byte loading; a failed upload adds no asset. The caller
    /// owns profile/content admission and retained CPU node hierarchy.
    [[nodiscard]] bool loadMeshData(const moto::VmeshData& data);
    /// Reset all runtime instances (keep loaded meshes).
    void clearInstances();
    void addInstance(const MeshDrawInstance& instance);
    // Borrowed only for an owned physics submission. Static draws bind inert
    // buffers; dynamic draws resolve COM/principal pose directly on the GPU.
    [[nodiscard]] bool setAuthoredBodyView(const physics::PhysicsRenderView&, physics::WorldPosition camera);
    [[nodiscard]] size_t assetCount() const noexcept { return assets_.size(); }
    // Color-pass draw count. Sun-shadow mode additionally replays the opaque
    // caster subset (bounded by the same maxDrawsPerFrame, not included here).
    [[nodiscard]] uint32_t lastSubmittedDrawCount() const noexcept {
        return lastSubmittedDrawCount_;
    }
    [[nodiscard]] uint32_t lastCulledInstanceCount() const noexcept {
        return lastCulledInstanceCount_;
    }

    /// Bind the shared sky environment texture view (from BlitPath) for IBL.
    void setEnvironmentTexture(WGPUTextureView view);

    /// Bind the ray-caster's linear scene depth for terrain occlusion.
    void setRayDepthTexture(WGPUTextureView view);

    /// Update both borrowed views as one transaction. False keeps old bindings.
    /// The caller still needs WebGPU error scopes for asynchronous validation.
    [[nodiscard]] bool setSceneTextures(WGPUTextureView environment,
                                         WGPUTextureView rayDepth);

    /// Encode the optional filter after the source sky is ready, before render.
    /// A successful bake remains provisional until actual submission is acknowledged.
    [[nodiscard]] bool encodeEnvironmentLighting(WGPUCommandEncoder encoder);
    void acknowledgeEnvironmentSubmission() noexcept;
    void discardEnvironmentEncoding() noexcept;
    /// Required if the source contents change without replacing its view.
    [[nodiscard]] bool invalidateEnvironmentLighting() noexcept;
    [[nodiscard]] uint64_t environmentLightingBytes() const noexcept {
        return filteredEnvironment_.requestedBytes();
    }
    static constexpr uint64_t filteredEnvironmentReservationBytes = 1228944u;
    static constexpr uint32_t sunShadowResolution = 1024u;
    static constexpr uint64_t sunShadowReservationBytes =
        uint64_t(sunShadowResolution) * sunShadowResolution * 4u + sizeof(SunShadowUniforms);
    [[nodiscard]] uint32_t environmentBakeCount() const noexcept { return environmentBakeCount_; }
    [[nodiscard]] bool environmentLightingReady() const noexcept { return filteredEnvironmentReady_; }

    void render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                WGPUTextureView depthView, const glm::mat4& view,
                const glm::mat4& projection, const glm::vec3& cameraPosition,
                const glm::vec3& lightDirection, uint32_t width,
                uint32_t height, bool useRayDepth);

    /// Same render with full lighting control (mirrors PrimitivePath).
    bool render(WGPUCommandEncoder encoder, WGPUTextureView colorView,
                WGPUTextureView depthView, const glm::mat4& view,
                const glm::mat4& projection, const glm::vec3& cameraPosition,
                const PrimitiveLighting& lighting, uint32_t width,
                uint32_t height, bool useRayDepth, WGPUTextureView linearDepthOutput = nullptr,
                SceneShadowConsumer beforeColor = {}, glm::vec3 shadowFrameWorldOrigin = {});

private:
    WGPUTexture sunShadowTexture_ = nullptr;
    WGPUTextureView sunShadowView_ = nullptr;
    WGPUBuffer sunShadowUniform_ = nullptr;
    WGPUSampler sunShadowSampler_ = nullptr;
    WGPUBindGroupLayout sunShadowLayout_ = nullptr, sunCasterLayout_ = nullptr;
    WGPUBindGroup sunShadowBinding_ = nullptr, sunCasterBinding_ = nullptr;
    WGPUPipelineLayout sunCasterPipelineLayout_ = nullptr;
    WGPURenderPipeline sunCasterPipeline_ = nullptr;
    bool sunShadows_ = false;
    WGPUBindGroupLayout bodyLayout_ = nullptr;
    WGPUBindGroup bodyBinding_ = nullptr;
    WGPUBuffer bodyFallback_ = nullptr, bodyCamera_ = nullptr;
    void teardown(bool destroyResources);
    struct MeshBounds {
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
        bool valid = false;
    };

    struct GpuMaterialResources {
        std::array<WGPUTexture, moto::VmeshTextureCount> textures{};
        std::array<WGPUTextureView, moto::VmeshTextureCount> views{};
        WGPUBindGroup bindGroup = nullptr;
    };

    struct GpuAsset {
        WGPUBuffer vertexBuffer = nullptr;
        WGPUBuffer indexBuffer = nullptr;
        WGPUBuffer materialBuffer = nullptr;
        std::vector<GpuMaterialResources> materials;
        std::vector<moto::VmeshSubmesh> submeshes;
        std::vector<MeshBounds> meshBounds;
        std::vector<uint8_t> materialAlphaModes;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    [[nodiscard]] bool createPipeline(const MeshPathConfig& config);
    [[nodiscard]] bool uploadMesh(const moto::VmeshData& data);
    [[nodiscard]] bool ensureInstanceCapacity(size_t required);
    [[nodiscard]] bool rebuildBindGroups();

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPURenderPipeline opaquePipeline_ = nullptr;
    WGPURenderPipeline blendPipeline_ = nullptr;
    WGPUBuffer instanceBuffer_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUTextureView environmentView_ = nullptr;
    WGPUTextureView boundEnvironmentView_ = nullptr;
    WGPUTextureView rayDepthView_ = nullptr;
    WGPUTextureView boundRayDepthView_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUSampler materialSampler_ = nullptr;
    WGPUSampler filteredSampler_ = nullptr;
    EnvironmentLighting filteredEnvironment_;
    bool filteredEnvironmentReady_ = false;
    bool filteredEnvironmentEncoded_ = false;
    uint32_t environmentBakeCount_ = 0;
    WGPUTexture fallbackCubeTexture_ = nullptr;
    WGPUTextureView fallbackCubeView_ = nullptr;
    WGPUTexture fallbackEnvironmentTexture_ = nullptr;
    WGPUTextureView fallbackEnvironmentView_ = nullptr;
    WGPUTexture fallbackRayDepthTexture_ = nullptr;
    WGPUTextureView fallbackRayDepthView_ = nullptr;
    std::vector<GpuAsset> assets_;
    std::vector<MeshDrawInstance> instances_;
    size_t instanceCapacity_ = 0;
    uint32_t maxDrawsPerFrame_ = 512;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Depth32Float;
    bool linearHdrOutput_ = false;
    bool instancesValid_ = false;
    uint32_t lastSubmittedDrawCount_ = 0u;
    uint32_t lastCulledInstanceCount_ = 0u;
};

}  // namespace voxy::render

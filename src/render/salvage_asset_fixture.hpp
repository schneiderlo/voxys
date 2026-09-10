#pragma once

#include "game/assets/cooked_part_bundle.hpp"
#include "game/assets/fixture_limits.hpp"
#include "render/mesh_path.hpp"
#include "render/inspection_guides.hpp"
#include "render/primitive_path.hpp"

#include <memory>
#include <string>

namespace voxy::render {

struct SalvageFixtureConfig {
    std::filesystem::path shaderPath = "shaders/mesh_path.wgsl";
    WGPUTextureFormat colorFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth32Float;
    // Includes the conservative fixed buffer/fallback reservation below.
    uint64_t maximumOwnerGpuBytes = 16ull * 1024ull * 1024ull;
    uint64_t maximumResidentGpuBytes = 48ull * 1024ull * 1024ull;
    bool linearHdrOutput = false;
    bool filteredEnvironment = false;
};

struct SalvageFixturePlacement {
    uint32_t bundleIndex = 0;
    uint64_t lodId = 0; // Stable content ID, never a threshold-array index.
    glm::dmat4 cameraRelativeRoot{1.0};
    game::construction::GridTransform placement{};
    bool prototype = false; // bundleIndex selects the prototype array; lodId must be zero.
    std::optional<std::span<const game::construction::SocketId>> selectedSockets{};
    physics::BodyHandle physicsBody{};
    glm::vec4 tint{1.0f}; // Workshop selection; material remains authored.
};

struct SalvageFixtureSolid {glm::mat4 model{1};glm::vec4 color{1};};

struct SalvageFixtureFrame {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraPosition{0.0f};
    PrimitiveLighting lighting{};
    uint32_t width = 0;
    uint32_t height = 0;
    WGPUTextureView linearDepthOutput = nullptr; // Required only by the HDR opaque path.
    bool useRayDepth = false;
    InspectionGuides guides = InspectionGuides::Off;
    physics::PhysicsRenderView physics{};
    physics::WorldPosition worldCamera{};
    std::span<const SalvageFixtureSolid> harborStructure{}; // At most ten opaque structural boxes.
    std::span<const std::array<glm::vec3,2>> harborCables{}; // At most four actual constraint lines.
    std::optional<std::array<glm::vec3,2>> towCable{}; // Camera-relative presentation endpoints.
};

struct SalvageFixtureTicket {
    uint64_t generation = 0;
    uint64_t serial = 0;
    [[nodiscard]] bool operator==(const SalvageFixtureTicket&) const = default;
};

enum class SalvageFixtureStatus {
    Uninitialized, Idle, Active, CandidateValidating, CandidateReady,
    CandidateRejected, ViewsValidating, Leaving, Drained, Fatal
};

struct SalvageFixtureOwnerStats {
    uint64_t generation = 0;
    uint64_t assetGpuBytes = 0;
    uint64_t reservedGpuBytes = 0;
    uint64_t lastSubmittedSerial = 0;
    uint64_t lastCompletedSerial = 0;
    uint32_t uniqueUploads = 0;
    uint32_t prototypeUploads = 0;
    uint32_t pendingCallbacks = 0;
    uint64_t environmentGpuBytes = 0;
    uint32_t environmentBakeCount = 0;
    bool environmentReady = false;
};

struct SalvageFixtureStats {
    SalvageFixtureOwnerStats active{}, candidate{}, retiring{};
    SalvageFixtureTicket unresolved{};
    uint32_t lastEncodedDraws = 0;
    uint32_t lastSubmittedDraws = 0;
    uint32_t lastEncodedGuideBoxes = 0;
    uint32_t pendingViewCallbacks = 0;
};

// Static asset-inspection owner; does not create inventory, physics or gameplay.
// All methods run on the owning application thread. poll() observes callbacks;
// the platform must separately pump its GPU/event loop. No frame-delay fences.
class SalvageAssetFixture final {
public:
    static constexpr uint32_t maximumBundles = game::assets::kMaximumFixtureBundles;
    static constexpr uint32_t maximumPrototypes = 8;
    static constexpr uint32_t maximumUniqueAssets = 48;
    // Three presentation-only native palette thumbnails never consume scene
    // slots, owned parts, collision shapes or inventory.
    static constexpr uint32_t maximumPalettePlacements = 3;
    static constexpr uint32_t maximumPlacements = game::assets::kMaximumFixturePlacements + maximumPalettePlacements;
    static constexpr uint32_t maximumMeshInstances = 256; // Authored nodes, excluding helper boxes.
    static constexpr uint32_t maximumExpandedDraws = 512; // Per model or guide path.
    // Model and X-ray paths each reserve 512 * 96-byte instances, 160-byte
    // uniforms, two 1x1 2D fallbacks and one 1x1 cube, plus the 1936-byte guide:
    // 100624 requested bytes fit. The independent paths can each draw 512;
    // guides do not consume the model path's already reserved capacity.
    // Conservative requested-storage reservation, not driver working set.
    static constexpr uint64_t fixedGpuReservationBytes = 128ull * 1024ull;
    // Optional filter is added once per generation, outside the fixed reserve
    // but inside the unchanged owner/resident ceilings. Guides never own one.

    SalvageAssetFixture();
    ~SalvageAssetFixture();
    SalvageAssetFixture(const SalvageAssetFixture&) = delete;
    SalvageAssetFixture& operator=(const SalvageAssetFixture&) = delete;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue,
                            const SalvageFixtureConfig& config, std::string& error);
    [[nodiscard]] bool beginCandidate(
        std::span<const std::shared_ptr<const game::assets::CookedPartBundle>> bundles,
        std::string& error,
        std::span<const game::construction::PartDefinition> prototypes = {});
    [[nodiscard]] SalvageFixtureStatus poll();
    [[nodiscard]] bool publishCandidate(std::string& error);
    // Retains refs. Rebinding is validated asynchronously; encode waits until
    // poll finishes. Invalid scene views are a surfaced terminal scene error.
    [[nodiscard]] bool setSceneViews(WGPUTextureView environment,
                                     WGPUTextureView rayDepth, std::string& error);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder, WGPUTextureView color,
        WGPUTextureView depth, std::span<const SalvageFixturePlacement> placements,
        const SalvageFixtureFrame& frame, SalvageFixtureTicket& output, std::string& error);
    // Call ONLY after actual queue submission, or actual command/encoder
    // release without submission. Publication/rebind/Leave refuse open tickets.
    [[nodiscard]] bool submitted(SalvageFixtureTicket ticket, std::string& error);
    [[nodiscard]] bool discarded(SalvageFixtureTicket ticket, std::string& error);
    [[nodiscard]] bool resetInstances(std::string& error);
    [[nodiscard]] bool requestLeave(std::string& error);
    // Forward a platform-confirmed loss on the application thread. A platform
    // callback must first signal stable CPU state, never capture this owner.
    void notifyDeviceLost() noexcept;
    void shutdown(); // Exceptional release is not evidence of drained Leave.
    [[nodiscard]] SalvageFixtureStats stats() const;
    [[nodiscard]] std::string_view lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint64_t nextGeneration_ = 1;
    uint64_t nextSerial_ = 1;
};

} // namespace voxy::render

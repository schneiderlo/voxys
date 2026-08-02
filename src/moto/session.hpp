// ═══════════════════════════════════════════════════════════════════════════════
// session.hpp - RIDGEBREAK local freeride session
// ═══════════════════════════════════════════════════════════════════════════════

#pragma once

#include "moto/bike.hpp"
#include "moto/vmesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace voxy::moto {

struct MotoSessionConfig {
    std::filesystem::path bikeAsset = "data/moto/bike.vmesh";
    std::filesystem::path riderAsset = "data/moto/rider.vmesh";
    uint32_t bikeAssetIndex = 0u;
    uint32_t riderAssetIndex = 1u;
    glm::vec3 spawnPosition{0.0f, 1.0f, 0.0f};
    float spawnYaw = 0.0f;
    float waterHeight = -200.0f;
};

struct MotoPartPose {
    uint32_t assetIndex = 0u;
    uint32_t meshIndex = 0u;
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 tintColor{1.0f};
    float emissiveBoost = 0.0f;
};

struct MotoCameraPose {
    glm::vec3 position{0.0f, 2.0f, -4.0f};
    glm::vec3 target{0.0f, 1.0f, 0.0f};
};

enum class MotoFixedStepMode : uint8_t {
    Simulate = 0,
    HoldGrid = 1,
};

class MotoSession {
public:
    MotoSession() = default;
    ~MotoSession() = default;

    MotoSession(const MotoSession&) = delete;
    MotoSession& operator=(const MotoSession&) = delete;

    [[nodiscard]] bool initialize(const MotoSessionConfig& config,
                                  std::string* error);
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }

    void update(const BikeInput& input,
                const std::function<float(float x, float z)>& heightAt,
                float deltaTime);
    /// Selects grid ownership immediately before every fixed tick and reports
    /// that tick immediately after it is produced. This keeps race authority
    /// and bike simulation on one 60 Hz boundary, including catch-up frames.
    void update(
        const BikeInput& input,
        const std::function<float(float x, float z)>& heightAt,
        float deltaTime,
        const std::function<MotoFixedStepMode()>& fixedStepMode,
        const std::function<void(const BikeState&)>& onFixedStep);
    void reset();
    void resetAt(glm::vec3 position, float yaw);

    [[nodiscard]] const BikeState& bikeState() const noexcept {
        return bike_.state();
    }
    /// Pose state interpolated between the two latest fixed simulation ticks.
    /// Gameplay and networking must continue to use bikeState().
    [[nodiscard]] const BikeState& presentationBikeState() const noexcept {
        return presentationBikeState_;
    }
    [[nodiscard]] std::span<const MotoPartPose> partPoses() const noexcept {
        return partPoses_;
    }
    [[nodiscard]] const MotoCameraPose& cameraPose() const noexcept {
        return cameraPose_;
    }
    /// Number of newest shift edges rejected because the bounded FIFO was
    /// already full. Cleared by initialize(), reset(), and shutdown().
    [[nodiscard]] uint32_t droppedShiftInputs() const noexcept {
        return droppedShiftInputs_;
    }
    /// Canonical states produced by the fixed 60 Hz steps in the most recent
    /// update. Authority/race code consumes these instead of render frames.
    [[nodiscard]] std::span<const BikeState> fixedStepStates() const noexcept {
        return {fixedStepStates_.data(), fixedStepStateCount_};
    }

private:
    static constexpr size_t kDustParticleCount = 16u;

    struct ModelPart {
        std::string name;
        uint32_t meshIndex = 0u;
    };

    struct DustParticle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float age = 1.0f;
        float lifetime = 0.0f;
        float size = 0.0f;
    };

    [[nodiscard]] bool loadModelParts(const std::filesystem::path& path,
                                      std::vector<ModelPart>* parts,
                                      std::string* error);
    void rebuildPartPoses(const BikeInput& input, const BikeState& poseState);
    void updateCamera(float deltaTime, const BikeState& poseState);
    void updateVisualFeedback(
        const BikeInput& input,
        const std::function<float(float x, float z)>& heightAt,
        float deltaTime, const BikeState& poseState);

    MotoSessionConfig config_{};
    Bike bike_{};
    BikeState previousBikeState_{};
    BikeState presentationBikeState_{};
    std::vector<ModelPart> bikeParts_;
    std::vector<ModelPart> riderParts_;
    std::vector<MotoPartPose> partPoses_;
    MotoCameraPose cameraPose_{};
    float accumulator_ = 0.0f;
    std::array<int8_t, 8> pendingShifts_{};
    uint8_t pendingShiftHead_ = 0u;
    uint8_t pendingShiftCount_ = 0u;
    uint32_t droppedShiftInputs_ = 0u;
    bool pendingRemount_ = false;
    float pendingRemountAge_ = 0.0f;
    std::array<BikeState, 8> fixedStepStates_{};
    uint8_t fixedStepStateCount_ = 0u;
    std::array<DustParticle, kDustParticleCount> dustParticles_{};
    glm::vec3 groundPoint_{0.0f};
    glm::vec3 groundNormal_{0.0f, 1.0f, 0.0f};
    float dustEmissionAccumulator_ = 0.0f;
    uint32_t dustSequence_ = 0u;
    bool feedbackWasAirborne_ = true;
    bool initialized_ = false;
};

}  // namespace voxy::moto

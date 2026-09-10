#pragma once

#include "gpu/shader_source.hpp"
#include "gpu/webgpu_compat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace voxy::physics {

enum class GpuAttachmentCommandType : uint32_t {
    Destroy = 0,
    CreateDistance = 1,
    SetTargetLength = 2,
    SetMotorSpeed = 3,
};

struct alignas(16) GpuAttachmentCommand {
    // Command type, attachment slot, attachment generation, target tick.
    std::array<uint32_t, 4> header{};
    // Body A, generation A, body B, generation B.
    std::array<uint32_t, 4> bodies{};
    // Local anchor A and target length.
    std::array<float, 4> anchorATarget{};
    // Local anchor B and motor speed. Positive speed reels in.
    std::array<float, 4> anchorBMotor{};
    // Minimum length, maximum length, maximum force, break force.
    std::array<float, 4> limits{};
    // Axial compliance (m/N), reserved, reserved, reserved.
    std::array<float, 4> material{};
};

// This record is both the live constraint and its GPU-resident break record.
// Event packing reads it directly and selects records whose break tick equals
// the tick being packed, preserving stable slot order without another buffer.
struct alignas(16) GpuDistanceAttachment {
    // Generation, flags, body A, body B.
    std::array<uint32_t, 4> identity{};
    // Body generations, reserved, reserved.
    std::array<uint32_t, 4> bodyGenerations{};
    std::array<float, 4> anchorATarget{};
    std::array<float, 4> anchorBMotor{};
    std::array<float, 4> limits{};
    // Required impulse, required force, measured distance, target length.
    std::array<float, 4> evidence{};
    // Break tick, reason, reserved, reserved.
    std::array<uint32_t, 4> breakContext{};
    // Per-tick accumulated impulse, immutable compliance bits, reserved, reserved.
    std::array<uint32_t, 4> reserved{};
};

struct GpuAttachmentInput {
    WGPUBuffer poseBuffer = nullptr;
    WGPUBuffer motionBuffer = nullptr;
    WGPUBuffer shapeBuffer = nullptr;
    WGPUBuffer metadataBuffer = nullptr;
    WGPUBuffer coreCountersBuffer = nullptr;
    uint32_t bodyCapacity = 0;

    [[nodiscard]] bool valid() const noexcept {
        return poseBuffer && motionBuffer && shapeBuffer && metadataBuffer
            && coreCountersBuffer && bodyCapacity != 0;
    }
};

struct GpuAttachmentTelemetry {
    uint32_t live = 0;
    uint32_t taut = 0;
    uint32_t slack = 0;
    uint32_t breaks = 0;
    uint32_t commands = 0;
    uint32_t staleCommands = 0;
    uint32_t invalidEndpoints = 0;
    uint32_t tick = 0;
    uint32_t highLive = 0;
    uint32_t highTaut = 0;
    uint32_t highSlack = 0;
    uint32_t highBreaks = 0;
    uint32_t highCommands = 0;
};

class GpuAttachmentSolver {
public:
    static constexpr uint32_t kTelemetryWordCount = 16;

    struct Config {
        // Includes slot zero, which is always invalid.
        uint32_t attachmentCapacity = 1'025;
        uint32_t commandCapacity = 4'096;
        float tickSeconds = 1.0f / 60.0f;
        float linearSlop = 0.005f;
        float biasFactor = 0.2f;
        uint32_t maximumSectorDelta = 4'096;
        std::filesystem::path shaderPath =
            "shaders/physics_attachments.wgsl";
        std::span<const gpu::ShaderSource> shaderSources{};
    };

    GpuAttachmentSolver();
    ~GpuAttachmentSolver();
    GpuAttachmentSolver(const GpuAttachmentSolver&) = delete;
    GpuAttachmentSolver& operator=(const GpuAttachmentSolver&) = delete;
    GpuAttachmentSolver(GpuAttachmentSolver&&) noexcept;
    GpuAttachmentSolver& operator=(GpuAttachmentSolver&&) noexcept;

    [[nodiscard]] bool initialize(WGPUDevice device, WGPUQueue queue,
                                  const Config& config);
    void shutdown();
    void setInput(const GpuAttachmentInput& input);
    [[nodiscard]] bool uploadCommands(
        std::span<const GpuAttachmentCommand> commands);
    [[nodiscard]] bool encode(WGPUCommandEncoder encoder);

    [[nodiscard]] WGPUBuffer attachmentBuffer() const noexcept;
    [[nodiscard]] WGPUBuffer telemetryBuffer() const noexcept;
    [[nodiscard]] size_t persistentBytes() const noexcept;
    [[nodiscard]] size_t scratchBytes() const noexcept;

    [[nodiscard]] static GpuAttachmentTelemetry decodeTelemetry(
        std::span<const uint32_t> words) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(sizeof(GpuAttachmentCommand) == 96);
static_assert(sizeof(GpuDistanceAttachment) == 128);

} // namespace voxy::physics

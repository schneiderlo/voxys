#include "game/expedition/salvage_preview.hpp"
#include "physics/gpu/gpu_body_metadata.hpp"
#include "physics/physics_world.hpp"
#include "render/primitive_material.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace voxy::game::expedition {
namespace {
bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

SalvagePreview::~SalvagePreview() { static_cast<void>(shutdown()); }

bool SalvagePreview::initialize(physics::PhysicsWorld& world, glm::vec3 origin) {
    if (!world.isInitialized() || world.backendType() != physics::BackendType::WebGpuSoft) return false;
    return initialize({
        [&world](const physics::BodySpawnDesc& desc) { return world.spawnBody(desc); },
        [&world](physics::BodyHandle handle) { return world.destroyBody(handle); },
    }, origin);
}

bool SalvagePreview::initialize(BodyAccess access, glm::vec3 origin) {
    if (count_ != 0u || phase_ != Phase::Empty || !finite(origin)
        || !access.spawn || !access.destroy
        || revision_ == std::numeric_limits<uint64_t>::max()) return false;
    access_ = std::move(access);
    origin_ = origin;
    resets_ = 0;
    ++revision_;
    pending_.reset();
    leaveAfterRetirement_ = false;
    error_ = {};
    return spawnScene();
}

bool SalvagePreview::spawnScene() {
    destroyAccepted_.fill(false);
    std::array<physics::BodySpawnDesc, MaximumBodies> pieces{};
    uint32_t pieceCount = 0;
    const glm::vec3 wood{.39f, .25f, .13f}, teal{.16f, .40f, .39f};
    const glm::vec3 rust{.58f, .25f, .12f}, pale{.75f, .65f, .45f};
    const auto box = [&](glm::vec3 position, glm::vec3 size, glm::vec3 color,
                         float yaw = 0.0f, bool brick = false) {
        if (pieceCount >= pieces.size()) return;
        auto& desc = pieces[pieceCount++];
        desc.shape = physics::ThrowableShape::Box;
        desc.position = origin_ + position;
        desc.dimensions = size;
        desc.orientation = glm::quat(std::cos(yaw * .5f), 0.0f, std::sin(yaw * .5f), 0.0f);
        desc.inverseMass = 0.0f;
        physics::PhysicsMaterial material;
        material.friction = .8f;
        material.restitution = .02f;
        material.flags = render::packPrimitiveMaterial({color, .48f, 0.0f, true});
        if (brick) material.flags = (material.flags & 0x0fffffffu) | physics::kLegoBrickMaterial;
        desc.material = material;
    };
    // A walkable timber pier, its pilings, and low mooring posts.
    for (int i = 0; i < 8; ++i) box({0, 0, 8.0f - float(i) * 2.0f}, {5.8f, .28f, 1.92f}, wood);
    for (float x : {-2.4f, 2.4f}) {
        for (float z : {-5.0f, 7.0f}) {
            box({x, -1.7f, z}, {.6f, 3.4f, .6f}, wood);
            box({x, .6f, z}, {.45f, 1.2f, .45f}, pale);
        }
    }
    // A broken hull next to the pier: keel, two sides, transom and exposed ribs.
    box({-8, .5f, -3}, {4.0f, .7f, 11.0f}, rust);
    box({-10, 1.4f, -3}, {.42f, 1.8f, 11.0f}, teal, -.10f);
    box({-6, 1.4f, -3}, {.42f, 1.8f, 8.0f}, teal, .10f);
    box({-8, 1.3f, 2.4f}, {4.0f, 1.6f, .42f}, teal);
    box({-8.8f, 1.2f, -9}, {.42f, 1.6f, 3.4f}, teal, -.55f);
    box({-7.2f, 1.2f, -9}, {.42f, 1.6f, 3.4f}, teal, .55f);
    for (int i = 0; i < 4; ++i) box({-8, 1.0f, float(i) * 2.0f - 6}, {3.4f, .4f, .3f}, pale);
    box({-8, 3.6f, -4}, {.35f, 5.0f, .35f}, wood);
    box({-8, 5.5f, -4}, {4.6f, .26f, .28f}, wood);
    // A few bounded studded modules mark the dock's workshop corner.
    for (int i = 0; i < 4; ++i) {
        box({1.4f, .71f, 3.0f - float(i) * 2.0f}, {.96f, 1.14f, 1.96f},
            i % 2 == 0 ? teal : rust, 0.0f, true);
    }

    legoCount_ = 0;
    for (uint32_t i = 0; i < pieceCount; ++i) {
        const auto handle = access_.spawn(pieces[i]);
        if (!handle.valid()) {
            // These spawns have not crossed a frame submission. The GPU backend
            // cancels their queued lifetimes, even when its command queue is full.
            const bool rolledBack = shutdown();
            phase_ = Phase::Failed;
            error_ = rolledBack ? "Cove creation failed; no preview bodies remain."
                                : "Cove rollback failed; close the preview to release its world.";
            return false;
        }
        handles_[count_++] = handle;
        if ((pieces[i].material->flags & 0xf0000000u) == physics::kLegoBrickMaterial)
            legoIds_[legoCount_++] = handle.index;
    }
    phase_ = Phase::Ready;
    error_ = {};
    return true;
}

bool SalvagePreview::request(Action action) {
    if (action != Action::Reset && action != Action::Leave) return false;
    if (phase_ == Phase::Empty || phase_ == Phase::Failed) return false;
    if (action == Action::Leave) {
        pending_ = action;
        leaveAfterRetirement_ = true;
    } else if (!pending_ && !busy() && !leaveAfterRetirement_) {
        pending_ = action;
    }
    return true;
}

void SalvagePreview::update() {
    if (pending_) {
        if (*pending_ == Action::Leave) leaveAfterRetirement_ = true;
        pending_.reset();
        if (phase_ == Phase::Ready) {
            if (revision_ == std::numeric_limits<uint64_t>::max()) {
                phase_ = Phase::Failed;
                error_ = "Preview lifecycle counter exhausted; close the preview.";
                return;
            }
            ++revision_;
            destroyAccepted_.fill(false);
            phase_ = Phase::Removing;
        }
    }
    if (phase_ != Phase::Removing) return;
    bool allAccepted = true;
    for (uint32_t i = 0; i < count_; ++i) {
        if (!destroyAccepted_[i]) destroyAccepted_[i] = access_.destroy(handles_[i]);
        allAccepted = allAccepted && destroyAccepted_[i];
    }
    if (allAccepted) phase_ = Phase::AwaitingRetirement;
}

std::optional<SalvagePreview::SnapshotRequest> SalvagePreview::retirementSnapshot() const {
    if (phase_ != Phase::AwaitingRetirement) return std::nullopt;
    uint32_t size = 0;
    for (uint32_t i = 0; i < count_; ++i) size = std::max(size, handles_[i].index + 1u);
    return SnapshotRequest{revision_, size};
}

void SalvagePreview::observeRetirement(uint64_t revision, std::span<const std::byte> metadata) {
    if (phase_ != Phase::AwaitingRetirement || revision != revision_) return;
    for (uint32_t i = 0; i < count_; ++i) {
        const size_t offset = size_t{handles_[i].index} * sizeof(glm::uvec4);
        if (offset + sizeof(glm::uvec4) > metadata.size()) return;
        uint32_t packed = 0;
        std::memcpy(&packed, metadata.data() + offset + 3u * sizeof(uint32_t), sizeof(packed));
        if ((packed & physics::kGpuBodyAliveFlag) != 0u
            && (packed & physics::kGpuBodyGenerationMask) == handles_[i].generation) return;
    }
    // A completed GPU copy proves that every old generation has disappeared.
    handles_ = {};
    count_ = legoCount_ = 0;
    if (leaveAfterRetirement_) {
        phase_ = Phase::Empty;
        return;
    }
    if (spawnScene()) ++resets_;
}

bool SalvagePreview::shutdown() {
    bool cleared = true;
    uint32_t retained = 0;
    for (uint32_t i = 0; i < count_; ++i) {
        // Already accepted destroys remain owned by the GPU command stream.
        if (!destroyAccepted_[i] && (!access_.destroy || !access_.destroy(handles_[i]))) {
            handles_[retained++] = handles_[i];
            cleared = false;
        }
    }
    count_ = retained;
    legoCount_ = 0;
    destroyAccepted_.fill(false);
    pending_.reset();
    phase_ = cleared ? Phase::Empty : Phase::Failed;
    return cleared;
}

} // namespace voxy::game::expedition

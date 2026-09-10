#pragma once

#include "game/assets/gameplay_sidecar.hpp"
#include "game/assets/rigid_prefab.hpp"

#include <functional>
#include <memory>
#include <span>

namespace voxy::game::assets {

struct PartLodAdmissionRule {
    uint64_t id = 0;
    RigidPrefabLimits limits{};
};

// Supplied by the trusted installed-content registry, independently of the
// bundle being admitted. A new manifest digest is an explicit content update.
struct CookedPartSelection {
    construction::ContentKey part{};
    std::string manifestSha256{};
    std::vector<PartLodAdmissionRule> lodRules{};
    uint64_t maximumInputBytes = 16ull * 1024ull * 1024ull;
    uint64_t maximumDecodedBytes = 16ull * 1024ull * 1024ull;
    uint64_t maximumGpuBytes = 8ull * 1024ull * 1024ull;
};

// Provider must enforce the requested cap before allocation and return one
// owned snapshot from the selected package root. Names are single validated
// leaves. It must not follow links or allow an arbitrary network URL. The loader
// rechecks length/digest and never reopens bytes after hashing them.
using CookedPartByteProvider = std::function<std::optional<std::vector<uint8_t>>(
    std::string_view filename, size_t maximumBytes, std::string& error)>;

struct AdmittedPartLod {
    uint64_t id = 0;
    construction::ContentKey asset{};
    std::string vmeshSha256{};
    moto::VmeshData mesh{};
    RigidPrefab prefab{};
};

class CookedPartBundle final {
public:
    [[nodiscard]] const GameplaySidecar& sidecar() const noexcept { return sidecar_; }
    [[nodiscard]] std::span<const AdmittedPartLod> lods() const noexcept { return lods_; }
    [[nodiscard]] std::string_view manifestJson() const noexcept { return manifestJson_; }
    [[nodiscard]] uint64_t inputBytes() const noexcept { return inputBytes_; }
    [[nodiscard]] uint64_t decodedBytes() const noexcept { return decodedBytes_; }
    [[nodiscard]] uint64_t requestedGpuBytes() const noexcept { return gpuBytes_; }

private:
    CookedPartBundle() = default;
    friend std::unique_ptr<const CookedPartBundle> admitCookedPartBundle(
        const CookedPartSelection&, const CookedPartByteProvider&, std::string&);
    GameplaySidecar sidecar_{};
    std::vector<AdmittedPartLod> lods_{};
    std::string manifestJson_{};
    uint64_t inputBytes_ = 0;
    uint64_t decodedBytes_ = 0;
    uint64_t gpuBytes_ = 0;
};

// Pure CPU admission; no catalog registration, GPU allocation or active-owner
// mutation. Failure returns null and a reason. Allocation exceptions propagate;
// the caller retains its previous bundle/owner until full upload publication.
// Source GLB/tool digests are bound provenance, not rehashed runtime inputs.
[[nodiscard]] std::unique_ptr<const CookedPartBundle> admitCookedPartBundle(
    const CookedPartSelection& selection, const CookedPartByteProvider& provider,
    std::string& error);

} // namespace voxy::game::assets

#pragma once

#include "physics/authored_body_frame.hpp"
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voxy::game::adventure {

// Source metadata is independent of the player's grid-build representation.
// The full import may retain unsupported pieces/rotations. D2 physical admission
// deliberately selects a small supported section and refuses everything else.
struct ImportedPart {
    uint64_t sourceId = 0;
    std::string sourcePath, partNumber;
    uint32_t colour = 0, meshNode = 0;
    glm::dvec3 translation{}; // Assembly-local top-of-body LDraw pivot, Y-up.
    glm::dquat rotation{1,0,0,0}; // w,x,y,z; arbitrary rigid source rotation.
};
struct ImportedStudBond {
    uint64_t id = 0, lowerPart = 0, upperPart = 0;
    uint16_t lowerSlot = 0, upperSlot = 0;
    bool active = true;
};
struct ImportedAssemblySource {
    uint32_t schemaVersion = 1, catalogVersion = 1;
    uint64_t revision = 1;
    std::string assetId, sourceSha256;
    std::vector<ImportedPart> parts;
    std::vector<ImportedStudBond> bonds;
    // Explicit external support, supplied by the source-reviewed section recipe.
    // These do not arise from bounding-box proximity. Several anchors may exist.
    std::vector<uint64_t> anchors;
};
struct ImportedPartCatalogEntry {
    std::string_view partNumber;
    uint16_t studsX = 0, studsZ = 0;
    double height = 0, massKg = 0;
    [[nodiscard]] glm::dvec3 connector(uint16_t slot, bool underside) const noexcept;
    [[nodiscard]] uint16_t connectorCount() const noexcept { return uint16_t(studsX*studsZ); }
};
[[nodiscard]] const ImportedPartCatalogEntry* importedPartCatalog(std::string_view partNumber) noexcept;
// Validates metadata, IDs, finite rigid transforms and referential integrity.
// Does not claim that unknown parts or non-orthogonal transforms are physical.
[[nodiscard]] bool validateImportedSource(const ImportedAssemblySource&, std::string& error);
// Reads only a bounded installed section. The caller pins the complete artifact
// set; optional expectedSha256 additionally pins the exact wall.json bytes.
[[nodiscard]] std::optional<ImportedAssemblySource> loadImportedSection(
    const std::filesystem::path&, std::string& error, std::string_view expectedSha256 = {});

class ImportedAssembly {
public:
    // After fracture, validated graph bonds own stud clutch. Collision retains
    // the complete hollow shell but omits stud strips; rendering and full-part
    // mass properties remain unchanged. This is an explicit gameplay profile.
    enum class CollisionProfile { Detailed, ReleasedShell };
    static constexpr size_t maximumSourceParts = 8192, maximumSourceBonds = 32768;
    static constexpr size_t maximumSectionParts = 128, maximumSectionBonds = 1024, maximumRoots = 64;
    struct Root {
        uint64_t key = 0; // Least durable source ID, stable under input permutation.
        bool anchored = false; // Connected to ANY explicit external support.
        glm::dvec3 origin{}; // Assembly-local root frame; orientation is identity.
        std::vector<uint32_t> partIndices; // Indices in sorted source().parts.
        physics::AuthoredShape shape;
        physics::RigidMassInput mass;
    };
    [[nodiscard]] static std::optional<ImportedAssembly> prepare(
        const ImportedAssemblySource&, std::string& error,
        CollisionProfile = CollisionProfile::Detailed);
    [[nodiscard]] std::optional<ImportedAssembly> prepareReleasedShell(
        uint64_t expectedRevision, std::string& error) const;
    [[nodiscard]] CollisionProfile collisionProfile() const noexcept { return collisionProfile_; }
    // Immutable, atomic preparation. Anchor cuts remove named external support;
    // bond cuts remove only catalog-validated connections. Neither applies forces.
    [[nodiscard]] std::optional<ImportedAssembly> prepareCut(uint64_t expectedRevision,
        std::span<const uint64_t> bondIds, std::span<const uint64_t> anchorPartIds,
        std::string& error) const;
    // Manual source-part removal, not impact damage. Remove the owned record,
    // every incident bond and its external anchor in one validated revision.
    // Remaining source identities and transforms are preserved; the old graph
    // stays immutable and can be used for an explicit rebuild.
    [[nodiscard]] std::optional<ImportedAssembly> prepareRemovePart(uint64_t expectedRevision,
        uint64_t sourceId, std::string& error) const;
    [[nodiscard]] const ImportedAssemblySource& source() const noexcept { return source_; }
    [[nodiscard]] std::span<const Root> roots() const noexcept { return roots_; }
    [[nodiscard]] std::optional<size_t> rootForPart(uint64_t sourceId) const noexcept;
    [[nodiscard]] glm::dmat4 partMatrix(uint32_t partIndex) const noexcept;
    // Proxy input labels pack sorted part index and local catalog box ordinal.
    [[nodiscard]] std::optional<uint64_t> partForFeature(uint32_t sourceLabel) const noexcept;
    // Certified parent observations are supplied by the owner. This only maps
    // unchanged rigid motion to new origins (v + omega cross offset), adding no
    // impact impulse and never pretending a static parent already had motion.
    [[nodiscard]] std::optional<std::vector<physics::AuthoredRootMotion>> inheritMotion(
        const ImportedAssembly& before, std::span<const physics::AuthoredRootMotion> parentMotion,
        std::string& error) const;
private:
    CollisionProfile collisionProfile_ = CollisionProfile::Detailed;
    ImportedAssemblySource source_;
    std::vector<Root> roots_;
};

} // namespace voxy::game::adventure

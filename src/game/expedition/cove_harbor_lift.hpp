#pragma once
#include "game/expedition/cove_boat.hpp"
#include "game/expedition/cove_player.hpp"
#include "game/expedition/cove_harbor_state.hpp"
#include "physics/authored_body_frame.hpp"

namespace voxy::game::expedition {
// Profile 1 is a fixed harbor service, not a player-built articulated crane.
// The host must also require durable banking and exclusive running authority.

class CoveHarborLift {
public:
    // Fixed world geometry exists independently of whether the current boat
    // can use it. Refits must never silently move its collision or rails.
    [[nodiscard]] static std::unique_ptr<CoveHarborLift> prepareStructure(
        const assets::CoveNavigation&,std::string& error);
    // Two intact, upright sealed pontoons provide two under-hull sling points
    // each. Wider/rotated/unsupported designs are refused, never silently given
    // fictional attachment points. The hull remains a dynamic GPU body.
    [[nodiscard]] static std::unique_ptr<CoveHarborLift> prepare(
        const CoveBoatAssembly&,const assets::CoveNavigation&,std::string& error);
    [[nodiscard]] const physics::AuthoredShape& shape() const noexcept {return shape_;}
    [[nodiscard]] glm::dvec3 center() const noexcept {return center_;}
    [[nodiscard]] std::span<const geometry::UnionBox> structure() const noexcept {return structure_;}
    [[nodiscard]] const auto& boatPoints() const noexcept {return boatPoints_;}
    [[nodiscard]] const auto& overheadPoints() const noexcept {return overheadPoints_;}
    [[nodiscard]] float minimumLength() const noexcept {return minimumLength_;}
    [[nodiscard]] bool applyPlayerCollision(CovePlayer&,const CoveBoatAssembly* cargo=nullptr,
        const physics::AuthoredRootMotion* cargoPose=nullptr,glm::dvec3 origin={}) const noexcept;
    [[nodiscard]] bool hasRig() const noexcept {return hasRig_;}
    [[nodiscard]] std::optional<std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines>> restoreLines(
        const CoveHarborLiftState&,std::string& error) const;
    // Plans all four lines before any live mutation. Both motions are authored
    // roots in world coordinates; the fixed gantry must keep its installed pose.
    [[nodiscard]] std::optional<std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines>> attach(
        const physics::AuthoredRootMotion& boat,const physics::AuthoredRootMotion& gantry,
        physics::BodyHandle boatBody,physics::BodyHandle gantryBody,std::string& error) const;
private:
    explicit CoveHarborLift(physics::AuthoredShape shape):shape_(std::move(shape)){}
    physics::AuthoredShape shape_;
    glm::dvec3 center_{};
    geometry::GridBox boatBounds_{};
    float minimumLength_=kCoveHarborLiftMinimumLength;
    bool hasRig_=false;
    std::array<geometry::UnionBox,10> structure_{};
    std::array<glm::vec3,kCoveHarborLiftLines> boatPoints_{},overheadPoints_{},boatBodyPoints_{};
};
} // namespace voxy::game::expedition

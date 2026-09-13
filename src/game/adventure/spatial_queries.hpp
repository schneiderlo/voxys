#pragma once

#include "game/construction/construction_types.hpp"
#include "game/expedition/cove_camera.hpp"
#include <map>
#include <span>
#include <vector>

namespace voxy::game::adventure {

// Every consumer reads the same complete accepted static revision. The owner
// publishes this packet at the same boundary as its static render/physics data.
class AdventureSpatialQueries {
public:
    struct Solid {
        construction::DurableId structure{},part{};
        glm::dvec3 minimum{},maximum{};
    };
    using SweepResult=expedition::CoveCamera::SweepResult;
    struct RayHit {
        bool complete=false,hit=false,terrain=false;
        double distance=0;
        glm::dvec3 point{},normal{};
        construction::DurableId structure{},part{};
    };
    static constexpr size_t maximumSolids=8192;
    static constexpr double sectorSize=256;
    // Retains a span only; the full terrain stays in the Application's one CPU copy.
    [[nodiscard]] bool bindTerrain(terrain::lego::Surface) noexcept;
    // Invalid/overflow/stale packets leave the old accepted geometry intact.
    [[nodiscard]] bool publish(std::span<const Solid>,uint64_t revision);
    [[nodiscard]] uint64_t revision() const noexcept {return revision_;}
    [[nodiscard]] size_t solidCount() const noexcept {return solids_.size();}
    [[nodiscard]] const terrain::lego::Surface& terrain() const noexcept {return terrain_;}
    [[nodiscard]] bool clearCapsule(glm::dvec3 feet,double radius=.3,double height=1.7) const noexcept;
    // Highest reachable surface BELOW maximumFeetHeight. This preserves ground
    // below bridges and floors below ceilings; callers choose their step allowance.
    [[nodiscard]] double supportHeight(glm::dvec2 position,double radius,double maximumFeetHeight) const noexcept;
    [[nodiscard]] SweepResult sweepCapsule(glm::dvec3 from,glm::dvec3 to,double radius=.3,double height=1.7) const noexcept;
    [[nodiscard]] SweepResult sweepSphere(glm::dvec3 from,glm::dvec3 to,double radius,uint64_t expectedRevision=0) const noexcept;
    [[nodiscard]] RayHit raycast(glm::dvec3 origin,glm::dvec3 direction,double distance) const noexcept;
    [[nodiscard]] expedition::CoveCamera::Sweep cameraSweep() const noexcept;
private:
    using Sector=std::pair<int32_t,int32_t>;
    [[nodiscard]] bool candidates(glm::dvec3 minimum,glm::dvec3 maximum,std::array<uint16_t,maximumSolids>&,size_t&) const noexcept;
    terrain::lego::Surface terrain_{};
    std::vector<Solid> solids_;
    std::map<Sector,std::vector<uint16_t>> sectors_;
    uint64_t revision_=0;
};

} // namespace voxy::game::adventure

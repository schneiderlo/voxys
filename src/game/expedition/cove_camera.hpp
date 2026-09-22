#pragma once

#include "terrain/lego_surface.hpp"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>

namespace voxy::game::expedition {

// Presentation only. Positions use Cove scene metres, never absolute float
// coordinates or boat-local coordinates. No part/body identities are invented.
class CoveCamera {
public:
    enum class Mode { Orbit, Chase };
    enum class Result { Ready, Obstructed, GeometryUnavailable, AnchorOverlapped, InvalidInput };
    struct Bounds { glm::dvec3 minimum{},maximum{}; };
    struct Settings {
        Mode mode=Mode::Chase;
        bool reducedMotion=false,frameLoad=false;
        double distanceLimit=12.0;
    };
    struct Projection { double verticalFov=1.05,aspect=16./9,nearPlane=.1; };
    struct Target {
        glm::dvec3 anchor{};
        // Authored -Z forward, rotated about +Y: (-sin(yaw),0,-cos(yaw)).
        double facingYaw=0;
        uint64_t geometryTick=0;
        std::optional<Bounds> load{};
        bool discontinuity=false,chaseActive=true;
    };
    struct Input {
        // Already-integrated radians, not rates: a quick mouse event must not
        // lose motion when the render delta is clamped after a long frame.
        glm::dvec2 orbitRadians{};
        double zoomSteps=0;
        bool recenter=false,active=true;
    };
    struct SweepResult {
        bool complete=false,hit=false,startOverlapped=false;
        double distance=0;
        glm::dvec3 normal{0};
    };
    struct Sweep {
        const void* context=nullptr;
        SweepResult (*cast)(const void*,glm::dvec3,glm::dvec3,double,uint64_t) noexcept=nullptr;
    };
    struct Pose {
        glm::dvec3 eye{},viewTarget{},forward{0,0,-1};
        double yaw=0,elevation=.32,distance=0,requestedDistance=4.8;
        double sphereRadius=0;
        uint64_t geometryTick=0;
        bool valid=false,hideAvatar=true,loadFramed=true;
        Result status=Result::GeometryUnavailable;
    };
    static constexpr double minimumDistance=1.5,maximumDistance=12.0;
    static constexpr double extendedMaximumDistance=64.0;
    static constexpr double minimumElevation=-.45,maximumElevation=1.20;
    static constexpr double presentationSkin=.02,contactPadding=.002;

    [[nodiscard]] bool settings(Settings value) noexcept;
    [[nodiscard]] Settings settings() const noexcept {return settings_;}
    // Initial facing is supplied only on a new session. Ordinary boarding,
    // menu return and mode changes preserve the orbit. Explicit discontinuity
    // discards the boom's old distance, never sweeps through the old location.
    void reset() noexcept;
    [[nodiscard]] Result update(const Target&,const Input&,Projection,Sweep,double seconds) noexcept;
    [[nodiscard]] const Pose& pose() const noexcept {return pose_;}
    [[nodiscard]] double userDistance() const noexcept {return userDistance_;}
    [[nodiscard]] bool setUserDistance(double metres) noexcept;
    [[nodiscard]] bool restoreOrbit(double yaw,double elevation,double distance) noexcept;
    // Sphere centered at the eye encloses every near-plane corner at this
    // viewport's actual aspect/FOV, plus admitted art's 2 cm envelope relief.
    [[nodiscard]] static std::optional<double> nearPlaneRadius(Projection) noexcept;
private:
    Settings settings_{};
    Pose pose_{};
    double yaw_=0,elevation_=.32,userDistance_=4.8,releaseDistance_=0;
    double orbitQuietSeconds_=0;
    bool initialized_=false,recentering_=false;
};

// Certified terrain half of CovePlayer::sweepSphere. The caller translates
// scene positions into the centered heightfield frame first. No allocations.
// Uses actual quantized columns/circular studs; smooth fallback conservatively
// encloses each bilinear cell by its maximum corner height. The outside floor
// matches Surface::heightAt (-heightScale). Overflow, malformed geometry and
// bounded-work exhaustion return complete=false, never a clear-path result.
[[nodiscard]] CoveCamera::SweepResult sweepCoveTerrainSphere(
    const terrain::lego::Surface&,glm::dvec3 start,glm::dvec3 end,double radius,
    bool lego=true,uint32_t maximumVisitedCells=4096) noexcept;

} // namespace voxy::game::expedition

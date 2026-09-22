#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {

// Toy limbs keep their molded joints. This cosmetic pose follows accepted
// velocity; it never moves the controller, water plane, or camera anchor.
class AdventureSwimAnimation {
public:
    void reset() noexcept {*this={};}
    void update(double seconds,bool swimming,glm::dvec3 velocity,double scale,bool reducedMotion) noexcept {
        if(!std::isfinite(seconds)||seconds<=0||!std::isfinite(scale)||scale<1
            ||!std::isfinite(velocity.x)||!std::isfinite(velocity.y)||!std::isfinite(velocity.z))return;
        const double dt=std::min(seconds,.25),follow=1-std::exp(-10*dt);
        const double speed=glm::length(velocity);
        const double effort=swimming?std::clamp(speed/(2.4*std::sqrt(scale)),0.,1.):0.;
        blend_+=((swimming?1.:0.)-blend_)*(1-std::exp(-12*dt));
        if(!swimming&&blend_<1e-4)blend_=0;
        travel_+=(effort-travel_)*follow;
        const double slope=speed>.05?std::atan2(velocity.y,std::hypot(velocity.x,velocity.z)):0;
        const double pitch=-.12+travel_*(-1.03+std::clamp(slope,-1.35,1.15));
        pitch_+=(pitch-pitch_)*follow;
        phase_=std::fmod(phase_+dt*(.65+.5*travel_)*2*std::numbers::pi,2*std::numbers::pi);
        reduced_=reducedMotion;
    }
    [[nodiscard]] bool active() const noexcept {return blend_>0;}
    [[nodiscard]] double blend() const noexcept {return blend_;}
    [[nodiscard]] double phase() const noexcept {return phase_;}
    [[nodiscard]] glm::dmat4 body() const noexcept {
        const double bob=reduced_?0:std::sin(phase_*2)*.018*blend_;
        const double roll=reduced_?0:std::sin(phase_)*.035*travel_*blend_;
        return glm::translate(glm::dmat4(1),glm::dvec3(0,.9+bob,0))
            *glm::rotate(glm::dmat4(1),pitch_*blend_,glm::dvec3(1,0,0))
            *glm::rotate(glm::dmat4(1),roll,glm::dvec3(0,0,1))
            *glm::translate(glm::dmat4(1),glm::dvec3(0,-.9,0));
    }
    [[nodiscard]] glm::dmat4 arm(glm::dvec3 shoulder,bool left) const noexcept {
        const double stroke=phase_+(left?0:std::numbers::pi),side=left?-1.:1.;
        const double swing=.75+.9*travel_+(.22+1.05*travel_)*std::sin(stroke);
        const double spread=side*(.42-.23*travel_+.08*std::cos(stroke));
        return around(shoulder,swing*blend_,spread*blend_);
    }
    [[nodiscard]] glm::dmat4 leg(glm::dvec3 hip,bool left) const noexcept {
        const double stroke=phase_+(left?std::numbers::pi:0);
        return around(hip,(.08+.22*travel_)*std::sin(stroke)*blend_,0);
    }
private:
    static glm::dmat4 around(glm::dvec3 pivot,double pitch,double roll) noexcept {
        return glm::translate(glm::dmat4(1),pivot)
            *glm::rotate(glm::dmat4(1),pitch,glm::dvec3(1,0,0))
            *glm::rotate(glm::dmat4(1),roll,glm::dvec3(0,0,1))
            *glm::translate(glm::dmat4(1),-pivot);
    }
    double blend_=0,travel_=0,pitch_=0,phase_=0;
    bool reduced_=false;
};

} // namespace voxy::game::adventure

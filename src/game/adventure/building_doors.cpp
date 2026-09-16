#include "game/adventure/building_doors.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {
const geometry::GridBox& doorLeafBox(bool open) noexcept {return open?kOpenDoorLeaf:kClosedDoorLeaf;}
glm::dmat4 doorLeafTransform(bool open) noexcept {
    if(!open)return glm::dmat4(1);
    const glm::dvec3 hinge=glm::dvec3(kDoorHinge.x,kDoorHinge.y,kDoorHinge.z)*.02;
    return glm::translate(glm::dmat4(1),hinge)
        *glm::rotate(glm::dmat4(1),-std::numbers::pi/2,glm::dvec3(0,1,0))
        *glm::translate(glm::dmat4(1),-hinge);
}
glm::dvec3 doorHandlePoint(bool open) noexcept {
    // Near the free edge at hand height, reachable from either side. Rays may
    // terminate on this door's own leaf; another wall still blocks the handle.
    return glm::dvec3(doorLeafTransform(open)*glm::dvec4(.52,1.16,0,1));
}
std::span<const geometry::GridBox> doorSweepBoxes() noexcept {
    static const auto boxes=[] {
        std::array<geometry::GridBox,16> result{};
        for(size_t i=0;i<result.size();++i) {
            const double low=double(i)*std::numbers::pi/32,high=double(i+1)*std::numbers::pi/32;
            double xmin=std::numeric_limits<double>::infinity(),zmin=xmin,xmax=-xmin,zmax=-xmin;
            const auto extrema=[&](double a,double b,double& minimum,double& maximum) {
                const auto at=[&](double angle) {const double value=a*std::cos(angle)+b*std::sin(angle);
                    minimum=std::min(minimum,value);maximum=std::max(maximum,value);};
                at(low);at(high);
                // A*cos(t)+B*sin(t) reaches every interior extremum at
                // atan2(B,A)+k*pi. Endpoints alone would underbound a swing.
                const double critical=std::atan2(b,a);
                for(int k=-1;k<=1;++k) {const double angle=critical+double(k)*std::numbers::pi;
                    if(angle>low&&angle<high)at(angle);}
            };
            for(int x:{kClosedDoorLeaf.minimum.x,kClosedDoorLeaf.maximum.x})
            for(int z:{kClosedDoorLeaf.minimum.z,kClosedDoorLeaf.maximum.z}) {
                const double relativeX=double(x-kDoorHinge.x),relativeZ=double(z-kDoorHinge.z);
                extrema(relativeX,-relativeZ,xmin,xmax);extrema(relativeZ,relativeX,zmin,zmax);
            }
            // The tiny integer snap only removes trig noise at exact endpoints;
            // its metre error is below the shared collision tolerance.
            result[i]={{int32_t(std::floor(xmin+1e-9))+kDoorHinge.x,kClosedDoorLeaf.minimum.y,
                        int32_t(std::floor(zmin+1e-9))+kDoorHinge.z},
                       {int32_t(std::ceil(xmax-1e-9))+kDoorHinge.x,kClosedDoorLeaf.maximum.y,
                        int32_t(std::ceil(zmax-1e-9))+kDoorHinge.z}};
        }
        return result;
    }();
    return boxes;
}
} // namespace voxy::game::adventure

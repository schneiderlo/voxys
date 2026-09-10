#pragma once
#include <glm/glm.hpp>

namespace voxy::game::expedition {

struct WorkshopBounds { glm::dvec3 minimum{}, maximum{}; };

// Presentation only, in the canonical build's metre frame. Never serialized
// into a blueprint or used to change part placement, ownership or physics.
class WorkshopCamera {
public:
    static constexpr double minimumDistance=2.5, maximumDistance=512;
    // Rectangle is [left,bottom,right,top] in NDC, excluding UI/edge margins.
    // Projection scales are the positive diagonal entries of the perspective.
    [[nodiscard]] bool viewport(glm::dvec2 projection,glm::dvec4 rectangle) noexcept;
    [[nodiscard]] bool frame(WorkshopBounds bounds) noexcept;
    void orbit(double yawDelta,double elevationDelta) noexcept;
    void zoom(double steps) noexcept;
    void pan(glm::dvec2 pixels,double viewportHeight) noexcept;
    [[nodiscard]] glm::dvec3 target() const noexcept { return target_; }
    [[nodiscard]] glm::dvec3 viewTarget() const noexcept;
    [[nodiscard]] glm::dvec3 eye() const noexcept;
    [[nodiscard]] double yaw() const noexcept { return yaw_; }
    [[nodiscard]] double elevation() const noexcept { return elevation_; }
    [[nodiscard]] double distance() const noexcept { return distance_; }
    [[nodiscard]] glm::dvec4 rectangle() const noexcept { return rectangle_; }
    [[nodiscard]] glm::dvec2 projection() const noexcept { return projection_; }
private:
    [[nodiscard]] glm::dvec3 back() const noexcept;
    [[nodiscard]] glm::dvec3 right() const noexcept;
    [[nodiscard]] glm::dvec3 up() const noexcept;
    glm::dvec3 target_{};
    glm::dvec2 projection_{1,1};
    glm::dvec4 rectangle_{-.92,-.74,.92,.92};
    double yaw_=.6,elevation_=.5028432109278609,distance_=12.55;
};
} // namespace voxy::game::expedition

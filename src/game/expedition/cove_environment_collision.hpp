#pragma once
#include "game/assets/cove_environment.hpp"
#include "physics/authored_shape.hpp"

namespace voxy::game::expedition {

// Pure CPU compound preparation for separately admitted presentation scenery.
// The owner MUST activate this shape as Static at the existing scene origin;
// unit reference mass is representation data, never a dynamic environment.
// No canonical part, inventory identity, resource handle or physics mutation.
class CoveEnvironmentCollision {
public:
    [[nodiscard]] static std::unique_ptr<CoveEnvironmentCollision> compile(
        std::span<const assets::CoveEnvironmentBox>,std::string& error);
    [[nodiscard]] const physics::AuthoredShape& shape() const noexcept {return shape_;}
    [[nodiscard]] std::span<const assets::CoveEnvironmentBox> boxes() const noexcept {return boxes_;}
    // No recentering: all box coordinates remain in the authored scene frame.
    [[nodiscard]] glm::dvec3 origin() const noexcept {return {};}
private:
    CoveEnvironmentCollision(physics::AuthoredShape shape,std::vector<assets::CoveEnvironmentBox> boxes)
        :shape_(std::move(shape)),boxes_(std::move(boxes)){}
    physics::AuthoredShape shape_;
    std::vector<assets::CoveEnvironmentBox> boxes_;
};

} // namespace voxy::game::expedition

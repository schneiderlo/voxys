#include "game/expedition/cove_environment_collision.hpp"

namespace voxy::game::expedition {
std::unique_ptr<CoveEnvironmentCollision> CoveEnvironmentCollision::compile(
    std::span<const assets::CoveEnvironmentBox> input,std::string& error) {
    const auto fail=[&](const char* reason)->std::unique_ptr<CoveEnvironmentCollision>{error=reason;return {};};
    try {
        if(input.empty()||input.size()>assets::kCoveEnvironmentMaximumCollisionBoxes)
            return fail("Cove scenery collision count is invalid.");
        std::vector<geometry::UnionBox> boxes;boxes.reserve(input.size());
        for(const auto& b:input) {
            for(const auto p:{b.minimum,b.maximum})if(p.x < -5000||p.x>5000||p.y < -5000||p.y>5000||p.z < -5000||p.z>5000)
                return fail("Cove scenery collision is outside its local frame.");
            if(b.minimum.x>=b.maximum.x||b.minimum.y>=b.maximum.y||b.minimum.z>=b.maximum.z)
                return fail("Cove scenery collision is empty.");
            if(b.maximum.x>-450&&b.minimum.x<400&&b.maximum.z>-3850&&b.minimum.z<-2350)
                return fail("Cove scenery collision blocks the original berth.");
            boxes.push_back({{b.minimum,b.maximum},static_cast<uint32_t>(boxes.size()+1)});
        }
        geometry::BoxUnionLimits limits;
        limits.inputBoxes=assets::kCoveEnvironmentMaximumCollisionBoxes;
        limits.cells=512;limits.faces=3072;limits.scratchPieces=512;limits.clipTests=262144;
        limits.radiusTicks=5000;
        geometry::BoxUnionIssue unionIssue;const auto exterior=geometry::BoxUnion::compile(boxes,unionIssue,limits);
        if(!exterior)return fail("Cove scenery exterior preparation exceeded its bounded profile.");
        // Same representation-only unit mass as CoveSceneryCollision. Static
        // admission zeros inverse mass/inertia; no floating or movable props.
        physics::AuthoredShapeIssue issue;
        auto shape=physics::AuthoredShape::prepare(*exterior,{1,{0,0,0},{1,0,0,0,1,0,0,0,1}},issue,
            {.cells=512,.faces=3072,.nodes=1023});
        if(!shape)return fail("Cove scenery immutable shape preparation failed.");
        auto result=std::unique_ptr<CoveEnvironmentCollision>(new CoveEnvironmentCollision(
            std::move(*shape),std::vector<assets::CoveEnvironmentBox>(input.begin(),input.end())));
        error.clear();return result;
    }catch(const std::bad_alloc&){return fail("Not enough memory for Cove scenery collision.");}
}
} // namespace voxy::game::expedition

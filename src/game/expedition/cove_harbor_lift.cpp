#include "game/expedition/cove_harbor_lift.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <limits>
#include <glm/gtc/quaternion.hpp>

namespace voxy::game::expedition {
bool CoveHarborLift::applyPlayerCollision(CovePlayer& player,const CoveBoatAssembly* cargo,
    const physics::AuthoredRootMotion* cargoPose,glm::dvec3 origin) const noexcept {
    std::array<CovePlayer::StaticObstacle,11> boxes;
    for(size_t i=0;i<structure_.size();++i){
        const auto a=structure_[i].bounds.minimum,b=structure_[i].bounds.maximum;
        boxes[i]={center_+glm::dvec3(a.x,a.y,a.z)*.02,center_+glm::dvec3(b.x,b.y,b.z)*.02};
    }
    size_t count=structure_.size();
    if(cargo&&cargoPose){
        const auto bounds=cargo->shape().rootBounds();
        const auto position=physics::worldPositionToAbsolute(cargoPose->position)-origin;
        glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());
        for(unsigned corner=0;corner<8;++corner){
            const glm::vec3 p{float((corner&1u)?bounds.maximum.x:bounds.minimum.x)*.02f,
                float((corner&2u)?bounds.maximum.y:bounds.minimum.y)*.02f,float((corner&4u)?bounds.maximum.z:bounds.minimum.z)*.02f};
            const auto world=position+glm::dvec3(cargoPose->orientation*p);lo=glm::min(lo,world);hi=glm::max(hi,world);
        }
        boxes[count++]={lo,hi};
    }
    return player.setStaticObstacles(std::span(boxes).first(count));
}
std::unique_ptr<CoveHarborLift> CoveHarborLift::prepareStructure(
    const assets::CoveNavigation& navigation,std::string& error){
    const auto fail=[&](const char* message)->std::unique_ptr<CoveHarborLift>{error=message;return {};};
    try {
        if(!navigation.delivery)return fail("This harbor has no lift berth.");
        const auto center=navigation.delivery->center;
        if(!std::isfinite(glm::length(center))||glm::length(center)>1e6)return fail("Invalid harbor lift position.");
        constexpr int32_t front=-35,back=35;
        // Ten exact lattice beams: four posts, an overhead frame, two trolley rails.
        // Every visible structural box must use these same bounds at integration.
        std::array<geometry::UnionBox,10> boxes{{
            {{{-210,0,-210},{-190,352,-190}},1},{{{190,0,-210},{210,352,-190}},2},
            {{{-210,0,190},{-190,352,210}},3},{{{190,0,190},{210,352,210}},4},
            {{{-210,352,-210},{210,372,-190}},5},{{{-210,352,190},{210,372,210}},6},
            {{{-210,352,-190},{-190,372,190}},7},{{{190,352,-190},{210,372,190}},8},
            {{{-190,352,front-8},{190,368,front+8}},9},{{{-190,352,back-8},{190,368,back+8}},10}}};
        geometry::BoxUnionIssue unionIssue;auto geometry=geometry::BoxUnion::compile(boxes,unionIssue);
        if(!geometry)return fail("Cannot prepare harbor lift structure.");
        // Static activation is mandatory. This positive reference mass exists
        // only to satisfy immutable authored-shape representation requirements.
        physics::AuthoredShapeIssue shapeIssue;auto shape=physics::AuthoredShape::prepare(*geometry,
            {1000,{0,3.4,0},{20000,0,0,0,20000,0,0,0,20000}},shapeIssue);
        if(!shape)return fail("Cannot prepare harbor lift collision.");
        auto result=std::unique_ptr<CoveHarborLift>(new CoveHarborLift(std::move(*shape)));
        result->center_=center;result->structure_=boxes;
        error.clear();return result;
    }catch(const std::bad_alloc&){return fail("Not enough memory to prepare the harbor lift.");}
}

std::unique_ptr<CoveHarborLift> CoveHarborLift::prepare(const CoveBoatAssembly& boat,
    const assets::CoveNavigation& navigation,std::string& error){
    const auto fail=[&](const char* message)->std::unique_ptr<CoveHarborLift>{error=message;return {};};
    try {
        if(!navigation.delivery)return fail("This harbor has no lift berth.");
        const auto center=navigation.delivery->center;
        if(!std::isfinite(glm::length(center))||glm::length(center)>1e6)return fail("Invalid harbor lift position.");
        const auto& assembly=boat.assembly();
        if(assembly.mass().roots().size()!=1||assembly.mass().roots()[0].mass.dryMassKg>6000)
            return fail("The harbor lift supports one boat up to 6,000 kg.");
        std::array<const construction::AssemblyBuoyancySource*,2> pontoons{};size_t count=0;
        for(const auto& source:assembly.buoyancy().sources())if(source.kind==construction::BuoyancyKind::SealedCompartment){
            size_t index=0;while(index<count&&pontoons[index]->part!=source.part)++index;
            if(index==count){
                if(count==pontoons.size())return fail("This lift needs a boat with two sealed pontoons.");
                pontoons[count++]=&source;
            }else{
                // A shaped pontoon contains several sealed boxes. Identify
                // the actual part once and use its largest central region.
                const auto volume=[](const auto& s){return double(s.halfExtents.x)*s.halfExtents.y*s.halfExtents.z;};
                if(volume(source)>volume(*pontoons[index]))pontoons[index]=&source;
            }
        }
        if(count!=2)return fail("This lift needs a boat with two sealed pontoons.");
        for(const auto* source:pontoons){
            // Initial sling recipe is explicitly bounded to upright Z-long
            // hulls. General arbitrary hull support is a later service profile.
            if(source->rootFromRegion.rotation.value!=0 || int64_t(source->halfExtents.z)<2*int64_t(source->halfExtents.x))
                return fail("Align both pontoons along the boat before using this lift.");
        }
        if(pontoons[1]->bounds.minimum.x<pontoons[0]->bounds.minimum.x)std::swap(pontoons[0],pontoons[1]);
        const auto& left=pontoons[0]->bounds;const auto& right=pontoons[1]->bounds;
        if(left.maximum.x>right.minimum.x||left.minimum.y!=right.minimum.y
            ||left.maximum.y!=right.maximum.y||left.minimum.z!=right.minimum.z||left.maximum.z!=right.maximum.z)
            return fail("Align the two pontoons to fit the harbor slings.");
        const auto root=assembly.mass().roots()[0].buildFromRoot.translation;
        const auto authoredRoot=glm::dvec3(root.x,root.y,root.z)*.02;
        const auto boatBounds=boat.shape().rootBounds();
        for(const auto& corner:{boatBounds.minimum,boatBounds.maximum})
            if(std::abs(authoredRoot.x+corner.x*.02-center.x)>3.6
                ||std::abs(authoredRoot.z+corner.z*.02-center.z)>3.6)
                return fail("The boat does not fit inside the harbor lift frame.");
        constexpr int32_t front=-35,back=35;
        auto result=prepareStructure(navigation,error);if(!result)return {};
        result->boatBounds_=boatBounds;result->hasRig_=true;
        physics::AuthoredFrameError frameError;
        for(size_t pontoon=0;pontoon<2;++pontoon){
            const auto& bounds=pontoons[pontoon]->bounds;
            for(size_t end=0;end<2;++end){
                const size_t i=pontoon*2+end;
                // A sling bears on the underside of the actual sealed hull;
                // quarter-length stations avoid unsupported extreme tips.
                const auto x=(double(bounds.minimum.x)+bounds.maximum.x)*.01;
                const auto z=(double(bounds.minimum.z)+(double(bounds.maximum.z)-bounds.minimum.z)*(end?.75:.25))*.02;
                double bottom=std::numeric_limits<double>::infinity();
                for(const auto& solid:assembly.collision().sources())if(solid.part==pontoons[pontoon]->part
                    &&x>=solid.bounds.minimum.x*.02&&x<=solid.bounds.maximum.x*.02
                    &&z>=solid.bounds.minimum.z*.02&&z<=solid.bounds.maximum.z*.02)
                    bottom=std::min(bottom,solid.bounds.minimum.y*.02);
                if(!std::isfinite(bottom))return fail("The pontoon has no solid support for a harbor sling.");
                result->boatPoints_[i]={x,bottom,z};
                result->overheadPoints_[i]={authoredRoot.x+x-center.x,7.04,double(end?back:front)*.02};
                if(std::abs(result->overheadPoints_[i].x)>3.4f||std::abs(result->overheadPoints_[i].z)>3.4f)
                    return fail("The boat is too wide or long for this harbor lift.");
                const auto bodyPoint=physics::AuthoredBodyFrame(boat.shape()).bodyPoint(result->boatPoints_[i],frameError);
                if(!bodyPoint)return fail("Cannot represent harbor sling anchors.");
                result->boatBodyPoints_[i]=*bodyPoint;
                // Keep the highest authored part clear of the overhead rails.
                // The boat may have a tall winch/helm above its buoyant hull.
                result->minimumLength_=std::max(result->minimumLength_,
                    float(boat.shape().rootBounds().maximum.y)*.02f-result->boatPoints_[i].y+.4f);
            }
        }
        if(result->minimumLength_>5.4f)return fail("The boat is too tall for this harbor lift.");
        error.clear();return result;
    }catch(const std::bad_alloc&){return fail("Not enough memory to prepare the harbor lift.");}
}
std::optional<std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines>> CoveHarborLift::attach(
    const physics::AuthoredRootMotion& boat,const physics::AuthoredRootMotion& gantry,
    physics::BodyHandle boatBody,physics::BodyHandle gantryBody,std::string& error) const {
    const auto fail=[&](const char* message)->std::optional<std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines>>{
        error=message;return {};};
    if(!hasRig_)return fail("The boat is not compatible with this lift.");
    const auto bp=physics::worldPositionToAbsolute(boat.position),gp=physics::worldPositionToAbsolute(gantry.position);
    const float q=glm::dot(boat.orientation,boat.orientation);
    if(!boatBody.valid()||!gantryBody.valid()||boatBody==gantryBody||!std::isfinite(glm::length(bp))
        ||!std::isfinite(glm::length(gp))||!std::isfinite(q)||std::abs(q-1)>.0001f
        ||gantry.orientation!=glm::quat(1,0,0,0)||gantry.originVelocity!=glm::vec3(0)
        ||gantry.angularVelocity!=glm::vec3(0))return fail("Invalid harbor lift body state.");
    if((boat.orientation*glm::vec3(0,1,0)).y<.8f
        ||!std::isfinite(glm::length(boat.originVelocity))||glm::length(boat.originVelocity)>.8f
        ||!std::isfinite(glm::length(boat.angularVelocity))||glm::length(boat.angularVelocity)>.5f)
        return fail("Bring the boat upright and slow down before attaching the lift.");
    for(unsigned corner=0;corner<8;++corner){
        const glm::vec3 point{float((corner&1u)?boatBounds_.maximum.x:boatBounds_.minimum.x)*.02f,
            float((corner&2u)?boatBounds_.maximum.y:boatBounds_.minimum.y)*.02f,
            float((corner&4u)?boatBounds_.maximum.z:boatBounds_.minimum.z)*.02f};
        const auto relative=bp+glm::dvec3(boat.orientation*point)-gp;
        if(std::abs(relative.x)>3.7||std::abs(relative.z)>3.7||relative.y>6.94)
            return fail("Align the whole boat inside the harbor lift frame.");
    }
    std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines> lines{};physics::AuthoredFrameError issue;
    for(size_t i=0;i<lines.size();++i){
        const auto a=gp+glm::dvec3(overheadPoints_[i]),b=bp+glm::dvec3(boat.orientation*boatPoints_[i]);
        const double distance=glm::length(a-b);
        if(std::hypot(a.x-b.x,a.z-b.z)>1.5||a.y-b.y<double(minimumLength_)
            ||distance+.05>double(kCoveHarborLiftMaximumLength))return fail("Align the boat beneath the harbor lift.");
        const auto fixedPoint=physics::AuthoredBodyFrame(shape_).bodyPoint(overheadPoints_[i],issue);
        if(!fixedPoint)return fail("Cannot represent harbor lift anchors.");
        lines[i]={.bodyA=gantryBody,.bodyB=boatBody,.localAnchorA=*fixedPoint,.localAnchorB=boatBodyPoints_[i],
            .targetLength=static_cast<float>(distance)+.05f,.minimumLength=minimumLength_,
            .maximumLength=kCoveHarborLiftMaximumLength,.motorSpeed=0,.maximumForce=kCoveHarborLiftLineForce,
            .breakForce=kCoveHarborLiftBreakForce,.springCompliance=kCoveHarborLiftCompliance};
    }
    error.clear();return lines;
}
std::optional<std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines>> CoveHarborLift::restoreLines(
    const CoveHarborLiftState& saved,std::string& error) const {
    if(!hasRig_||saved.profile!=kCoveHarborLiftProfile||!validCoveHarborLiftState(saved)
        ||saved.mode==CoveHarborLiftMode::Detached){error="Invalid saved harbor rig.";return {};}
    std::array<physics::DistanceAttachmentDesc,kCoveHarborLiftLines> lines{};physics::AuthoredFrameError issue;
    for(size_t i=0;i<lines.size();++i){
        if(saved.lengths[i]<minimumLength_){error="Saved lift cable does not clear this boat.";return {};}
        const auto fixedPoint=physics::AuthoredBodyFrame(shape_).bodyPoint(overheadPoints_[i],issue);
        if(!fixedPoint){error="Cannot represent saved harbor anchors.";return {};}
        lines[i]={.localAnchorA=*fixedPoint,.localAnchorB=boatBodyPoints_[i],.targetLength=saved.lengths[i],
            .minimumLength=minimumLength_,.maximumLength=kCoveHarborLiftMaximumLength,.motorSpeed=0,
            .maximumForce=kCoveHarborLiftLineForce,.breakForce=kCoveHarborLiftBreakForce,.springCompliance=kCoveHarborLiftCompliance};
    }
    error.clear();return lines;
}
} // namespace voxy::game::expedition

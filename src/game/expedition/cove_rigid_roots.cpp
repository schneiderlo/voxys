#include "game/expedition/cove_rigid_roots.hpp"
#include "game/expedition/cove_player.hpp"
#include "game/construction/assembly_fracture.hpp"
#include "game/construction/build_refit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::expedition {

std::unique_ptr<CoveRigidRoots> CoveRigidRoots::prepare(
    const CoveBoatAssembly& boat, std::string& error) {
    const auto mass = boat.assembly().mass().roots();
    if (mass.empty() || mass.size() > maximumRoots || mass.size() != boat.roots().size()
        || boat.primaryRootIndex() >= mass.size()) {
        error = "This cove cannot own all of the machine's physical sections.";
        return {};
    }
    try {
        auto result = std::unique_ptr<CoveRigidRoots>(new CoveRigidRoots);
        result->build_ = boat.build().id;
        result->revision_ = boat.build().revision;
        result->count_ = mass.size();
        result->primary_ = boat.primaryRootIndex();
        for (size_t i = 0; i < mass.size(); ++i) result->roots_[i].key = mass[i].key;
        error.clear();
        return result;
    } catch (const std::bad_alloc&) {
        error = "Not enough memory to prepare every machine section.";
        return {};
    }
}

bool CoveRigidRoots::matches(const CoveBoatAssembly& boat) const noexcept {
    if (build_ != boat.build().id || revision_ != boat.build().revision
        || count_ != boat.roots().size() || primary_ != boat.primaryRootIndex()) return false;
    const auto mass = boat.assembly().mass().roots();
    if (mass.size() != count_) return false;
    for (size_t i = 0; i < count_; ++i) if (roots_[i].key != mass[i].key) return false;
    return true;
}

std::optional<size_t> CoveRigidRoots::indexForKey(construction::DurableId key) const noexcept {
    for (size_t i = 0; i < count_; ++i) if (roots_[i].key == key) return i;
    return {};
}

std::optional<size_t> CoveRigidRoots::indexForPart(
    const CoveBoatAssembly& boat, construction::DurableId part) const noexcept {
    if (!matches(boat)) return {};
    const auto index = boat.rootForPart(part);
    return index ? std::optional<size_t>{*index} : std::nullopt;
}

uint64_t CoveRigidRoots::joinedTick() const noexcept {
    const auto tick = roots_[0].observedTick;
    if (!count_ || !tick) return 0;
    for (const auto& root : roots())
        if (!root.body.valid() || !root.shape.valid() || root.retired
            || root.observedTick < root.admissionTick || root.observedTick != tick) return 0;
    return tick;
}

bool CoveRigidRoots::allAdmitted() const noexcept {
    return count_&&std::all_of(roots().begin(),roots().end(),[](const auto& root){return root.body.valid()&&root.shape.valid()&&!root.retired;});
}
bool CoveRigidRoots::allRetired() const noexcept {
    return count_&&std::all_of(roots().begin(),roots().end(),[](const auto& root){return root.retired;});
}

void CoveRigidRoots::includeBodyRange(uint32_t& first, uint32_t& last) const noexcept {
    for (const auto& root : roots()) if (root.body.valid()) {
        first = std::min(first, root.body.index);
        last = std::max(last, root.body.index);
    }
}

bool CoveRigidRoots::bindPlayer(CovePlayer& player,const CoveBoatAssembly& boat,glm::dvec3 origin,std::string& error) const {
    const auto fail=[&]{error="The player cannot bind every machine section.";return false;};
    if(!matches(boat)||boat.parts().size()>32)return fail();
    std::array<CovePlayer::BoatRoot,maximumRoots> poses{};
    std::array<CovePlayer::BoatPart,32> parts{};
    for(size_t i=0;i<count_;++i) {
        const auto& motion=roots_[i].observed;const auto anchor=boat.assembly().mass().roots()[i].buildFromRoot.translation;
        physics::AuthoredFrameError frameError;
        if(!physics::AuthoredBodyFrame(boat.roots()[i].shape).bodyMotion(motion,frameError))return fail();
        poses[i]={roots_[i].key,glm::translate(glm::dmat4(1),physics::worldPositionToAbsolute(motion.position)-origin)
            *glm::mat4_cast(glm::normalize(glm::dquat(motion.orientation)))
            *glm::translate(glm::dmat4(1),-glm::dvec3(anchor.x,anchor.y,anchor.z)*.02)};
    }
    for(size_t i=0;i<boat.parts().size();++i) {
        const auto& member=boat.parts()[i];const auto root=boat.rootForPart(member.id);
        if(!root||*root>=count_)return fail();
        parts[i]={member.placement,member.id,roots_[*root].key};
    }
    if(!player.bindBoatRoots({poses.data(),count_},{parts.data(),boat.parts().size()},primary().key))return fail();
    error.clear();return true;
}

std::optional<CoveRigidRoots::CutTarget> CoveRigidRoots::reachableWeld(const CoveBoatAssembly& boat,
    glm::dvec3 hand,std::optional<construction::DurableId> exact) const noexcept {
    if(!matches(boat)||!joinedTick()||!std::isfinite(hand.x)||!std::isfinite(hand.y)||!std::isfinite(hand.z))return {};
    const auto& functions=boat.assembly().functions();
    std::optional<CutTarget> nearest;
    for(const auto& binding:functions.connections()) {
        const auto& link=binding.definition;
        if(!link.enabled||link.kind!=construction::ConnectionKind::Weld||(exact&&link.id!=*exact))continue;
        const auto& socket=functions.sockets()[binding.socketA];
        if(socket.root>=count_||functions.sockets()[binding.socketB].root!=socket.root)return {};
        const auto& motion=roots_[socket.root].observed;
        physics::AuthoredFrameError frameError;
        if(!physics::AuthoredBodyFrame(boat.roots()[socket.root].shape).bodyMotion(motion,frameError))return {};
        const auto p=socket.rootFromSocket.translation;
        const auto point=physics::worldPositionToAbsolute(motion.position)
            +glm::normalize(glm::dquat(motion.orientation))*glm::dvec3(p.x,p.y,p.z)*.02;
        const double distance=glm::length(point-hand);
        if(distance<=cutterReachMetres&&(!nearest||distance<nearest->distance
            ||(distance==nearest->distance&&link.id<nearest->weld)))nearest=CutTarget{link.id,link.a.part,link.b.part,distance};
    }
    return nearest;
}

bool CoveRigidRoots::transferCutPlayer(CovePlayer& destination,const CoveBoatAssembly& boat,
    const CovePlayer& source,glm::dvec3 origin,std::string& error) const {
    auto state=source.state();
    if(state.mode==CovePlayer::Mode::Airborne||state.mode==CovePlayer::Mode::Swimming) {
        error="Stand on the dock or a machine section before cutting.";return false;
    }
    if(state.onBoat) {
        const auto root=indexForPart(boat,source.supportingPart());
        if(!root){error="The cutter cannot keep your footing on this section.";return false;}
        state.root=roots_[*root].key;
        if(state.mode==CovePlayer::Mode::Helm&&state.root!=primary().key)state.mode=CovePlayer::Mode::Walking;
    }
    if(!bindPlayer(destination,boat,origin,error))return false;
    if(!destination.restore(state)){error="The cutter cannot keep your footing on this section.";return false;}
    error.clear();return true;
}

std::optional<CoveRigidRoots::Recovery> CoveRigidRoots::prepareRecovery(const CoveBoatAssembly& boat,
    const physics::WorldPosition& sceneOrigin,double waterHeightOffset,uint64_t sourceTick,std::string& error) const {
    const auto fail=[&](const char* reason)->std::optional<Recovery>{error=reason;return {};};
    if(!matches(boat)||!sourceTick||sourceTick==std::numeric_limits<uint64_t>::max()||joinedTick()!=sourceTick
        ||!std::isfinite(waterHeightOffset))return fail("Recovery needs every section at one completed tick.");
    for(size_t i=0;i<count_;++i)for(size_t j=0;j<i;++j)
        if(roots_[i].body.index==roots_[j].body.index)return fail("Recovery section lifetimes overlap.");
    for(size_t i=0;i<count_;++i){
        physics::AuthoredFrameError frameError;
        if(!physics::AuthoredBodyFrame(boat.roots()[i].shape).bodyMotion(roots_[i].observed,frameError))
            return fail("A recovery source has invalid observed motion.");
    }
    // Use the whole accepted layout to find the home waterline. The helm may
    // be an isolated metal part with insufficient displacement after a cut, so its root alone
    // cannot define the recovery berth. Disabled welds remain disabled.
    try {
    double mass=0,minimumBuildY=std::numeric_limits<double>::infinity();
    const auto massRoots=boat.assembly().mass().roots();
    for(size_t i=0;i<count_;++i){
        mass+=massRoots[i].mass.dryMassKg;
        for(const auto& cell:boat.roots()[i].shape.cells())minimumBuildY=std::min(minimumBuildY,
            (double(cell.minimum[1])+double(massRoots[i].buildFromRoot.translation.y))*.02);
    }
    if(!(mass>0)||!std::isfinite(minimumBuildY))return fail("The recovery layout has no supported physical bounds.");
    const auto sources=boat.assembly().buoyancy().sources();
    if(sources.size()>construction::kMaximumUnionInputBoxes)return fail("The recovery waterline exceeds its volume budget.");
    std::vector<construction::UnionBox> boxes;boxes.reserve(sources.size());
    for(const auto& source:sources){
        if(source.root>=count_)return fail("The recovery waterline has an unknown section.");
        const auto box=construction::transformBounds(massRoots[source.root].buildFromRoot,source.bounds);
        if(!box)return fail("The recovery waterline is outside the supported layout.");
        boxes.push_back({*box,static_cast<uint32_t>(boxes.size()+1)});
    }
    construction::BoxCoverageIssue issue;
    const auto coverage=construction::BoxCoverage::compile(boxes,issue);
    if(!coverage)return fail("The recovery waterline cannot be prepared within its volume budget.");
    // Deduplicate volumes across newly separate roots in the home layout. A
    // cut cannot change the berth solely by giving overlapping cells two owners.
    const double capacity=double(coverage->stats().volumeTicks3)*.000008;
    double lower=-256,upper=256;
    for(int iteration=0;iteration<48;++iteration){
        const double y=(lower+upper)*.5;double volume=0;
            for(const auto& cell:coverage->cells()){
                const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
                const double submerged=std::clamp(-y-double(a.y)*.02,0.0,double(b.y-a.y)*.02);
                volume+=double(b.x-a.x)*.02*double(b.z-a.z)*.02*submerged;
            }
        if(volume*1000>mass)lower=y;else upper=y;
    }
    // An overloaded layout still comes home. Start its lowest point just above
    // the water so the player can service it from the workshop; recovery adds
    // no flotation, weld, permanent support or new owned part.
    const double lift=capacity*1000>mass?(lower+upper)*.5:.05-minimumBuildY;
    Recovery prepared;prepared.count=count_;prepared.sourceTick=sourceTick;
    physics::AuthoredFrameError frameError;
    for(size_t i=0;i<count_;++i){
        const auto anchor=massRoots[i].buildFromRoot.translation;
        const auto position=physics::translateAuthoredPosition(sceneOrigin,
            glm::dvec3(anchor.x,anchor.y,anchor.z)*.02+glm::dvec3(0,lift+waterHeightOffset,0),frameError);
        if(!position)return fail("The recovery berth is outside the supported world.");
        auto& target=prepared.targets[i];target.position=*position;
        const auto body=physics::AuthoredBodyFrame(boat.roots()[i].shape).bodyMotion(target,frameError);
        if(!body)return fail("A recovered section cannot be represented by its mass frame.");
        auto& teleport=prepared.commands[3*i];teleport.type=physics::PhysicsCommandType::Teleport;
        teleport.body=roots_[i].body;teleport.sector=body->centerPosition.sector;
        teleport.a=glm::vec4(body->centerPosition.local,0);
        teleport.b={body->orientation.x,body->orientation.y,body->orientation.z,body->orientation.w};
        auto& velocity=prepared.commands[3*i+1];velocity.type=physics::PhysicsCommandType::SetVelocity;velocity.body=roots_[i].body;
        auto& angular=prepared.commands[3*i+2];angular.type=physics::PhysicsCommandType::SetAngularVelocity;angular.body=roots_[i].body;
    }
    error.clear();return prepared;
    }catch(const std::bad_alloc&){return fail("Not enough memory to prepare the complete recovery.");}
}

bool CoveRigidRoots::inheritFractureMotion(const CoveBoatAssembly& destination,
    const construction::AssemblyFracturePlan& fracture, const CoveBoatAssembly& source,
    const CoveRigidRoots& sourceRoots, uint64_t completedTick, std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    if(!matches(destination) || !sourceRoots.matches(source)
        || !completedTick || sourceRoots.joinedTick()!=completedTick
        || destination.build().id!=fracture.afterBuild().id
        || destination.build().revision!=fracture.afterBuild().revision
        || destination.build().owner!=fracture.afterBuild().owner
        || destination.build().editLease!=fracture.afterBuild().editLease
        || destination.build().parts!=fracture.afterBuild().parts
        || !std::equal(destination.build().connections.begin(),destination.build().connections.end(),
            fracture.afterBuild().connections.begin(),fracture.afterBuild().connections.end(),construction::sameConnection)
        || destination.assembly().inputIdentity()!=fracture.after().inputIdentity()
        || source.assembly().inputIdentity()!=fracture.before().inputIdentity())
        return fail("The cut no longer matches the completed machine state.");
    for(const auto& root:roots())if(root.body.valid() || root.admissionTick || root.observedTick || root.retired)
        return fail("Cut motion cannot replace an admitted machine section.");
    try {
        std::array<construction::AssemblyRootMotion,maximumRoots> inputs{};
        physics::AuthoredFrameError frameError;
        for(size_t i=0;i<sourceRoots.count_;++i) {
            const auto& root=sourceRoots.roots_[i];const auto& motion=root.observed;
            if(!physics::AuthoredBodyFrame(source.roots()[i].shape).bodyMotion(motion,frameError))
                return fail("A parent section has an invalid completed motion.");
            auto& input=inputs[i];input.root=root.key;
            const auto p=physics::worldPositionToAbsolute(motion.position);
            input.worldFromRoot.position={p.x,p.y,p.z};
            const auto rotation=glm::mat3_cast(glm::normalize(glm::dquat(motion.orientation)));
            for(int row=0;row<3;++row)for(int column=0;column<3;++column)
                input.worldFromRoot.orientation[static_cast<size_t>(row*3+column)]=rotation[column][row];
            input.originVelocity={motion.originVelocity.x,motion.originVelocity.y,motion.originVelocity.z};
            input.angularVelocity={motion.angularVelocity.x,motion.angularVelocity.y,motion.angularVelocity.z};
        }
        construction::AssemblyFractureIssue issue;
        const auto inherited=fracture.inheritMotion({sourceRoots.build_,sourceRoots.revision_,
            construction::SimulationTick{completedTick},{inputs.data(),sourceRoots.count_}},
            construction::SimulationTick{completedTick},issue);
        if(!inherited || inherited->size()!=count_ || fracture.fragments().size()!=count_)
            return fail("The cut cannot inherit every section's motion.");
        std::array<physics::AuthoredRootMotion,maximumRoots> staged{};
        for(size_t i=0;i<count_;++i) {
            const auto& value=(*inherited)[i];const auto parent=fracture.fragments()[i].parentRoot;
            if(value.root!=roots_[i].key || parent>=sourceRoots.count_)
                return fail("The cut section mapping is invalid.");
            auto& motion=staged[i];const auto& parentMotion=sourceRoots.roots_[parent].observed;
            const auto& offset=fracture.fragments()[i].parentFromRoot;
            const auto translated=physics::translateAuthoredPosition(parentMotion.position,
                glm::normalize(glm::dquat(parentMotion.orientation))*glm::dvec3(offset[0],offset[1],offset[2]),frameError);
            if(!translated)return fail("A cut section would leave the supported world.");
            motion.position=*translated;
            // Assembly roots differ by translation only. Retain the actual
            // parent's quaternion without an unnecessary matrix round-trip.
            motion.orientation=parentMotion.orientation;
            const auto& v=value.originVelocity;const auto& w=value.angularVelocity;
            motion.originVelocity={float(v[0]),float(v[1]),float(v[2])};
            motion.angularVelocity={float(w[0]),float(w[1]),float(w[2])};
            if(!physics::AuthoredBodyFrame(destination.roots()[i].shape).bodyMotion(motion,frameError))
                return fail("A cut section's motion cannot be represented by physics.");
        }
        for(size_t i=0;i<count_;++i)roots_[i].spawn=staged[i];
        error.clear();return true;
    } catch(const std::bad_alloc&) {
        return fail("Not enough memory to prepare every cut section's motion.");
    }
}

} // namespace voxy::game::expedition

#include "game/adventure/imported_wall_physics.hpp"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <set>

namespace voxy::game::adventure {
namespace {
bool joined(const physics::PhysicsTickFrontier& f) {
    return f.supported&&!f.failed&&f.incarnation&&f.scheduled==f.completed
        &&f.encoded==f.completed&&f.submitted==f.completed;
}
std::vector<ImportedWallPhysics::Cell> cellsFor(const ImportedAssembly& graph,
    std::span<const physics::AuthoredRootMotion> motions) {
    std::vector<ImportedWallPhysics::Cell> cells;
    for(size_t i=0;i<graph.roots().size();++i) {
        const auto& root=graph.roots()[i];
        const auto origin=physics::worldPositionToAbsolute(motions[i].position);
        const glm::dquat q(motions[i].orientation);
        for(const auto& box:root.shape.cells()) {
            const auto id=graph.partForFeature(box.source);if(!id)continue;
            ImportedWallPhysics::Cell cell{*id,glm::dvec3(std::numeric_limits<double>::infinity()),glm::dvec3(-std::numeric_limits<double>::infinity())};
            for(unsigned corner=0;corner<8;++corner) {
                glm::dvec3 local;
                for(unsigned axis=0;axis<3;++axis)local[int(axis)]=double((corner&(1u<<axis))?box.maximum[axis]:box.minimum[axis])*physics::kAuthoredShapeTickMetres;
                const auto point=origin+q*local;cell.minimum=glm::min(cell.minimum,point);cell.maximum=glm::max(cell.maximum,point);
            }
            cells.push_back(cell);
        }
    }
    return cells;
}
}
std::vector<physics::AuthoredRootMotion> ImportedWallPhysics::initialMotion(const ImportedAssembly& graph) const {
    std::vector<physics::AuthoredRootMotion> result;
    for(const auto& root:graph.roots())result.push_back({
        .position=physics::worldPositionFromAbsolute(origin_+glm::dquat(orientation_)*root.origin),
        .orientation=orientation_});
    return result;
}
bool ImportedWallPhysics::stage(ImportedAssembly graph,std::vector<physics::AuthoredRootMotion> motions,
    std::vector<bool> dynamic,Phase phase,std::string& error,std::optional<physics::BodyHandle> consumeBody) {
    if(pending_||motions.size()!=graph.roots().size()||dynamic.size()!=motions.size()) {
        error="The wall is still changing.";return false;
    }
    CannonPhysicsScene::Packet packet;packet.revision=revision_+1;
    if(!packet.revision){error="Wall revision exhausted.";return false;}
    packet.solidCount=graph.source().parts.size();packet.consumeBody=consumeBody;
    for(size_t i=0;i<motions.size();++i)packet.partitions.push_back({
        .origin=physics::worldPositionToAbsolute(motions[i].position),.shape=graph.roots()[i].shape,
        .orientation=motions[i].orientation,.dynamic=dynamic[i],
        .originVelocity=motions[i].originVelocity,.angularVelocity=motions[i].angularVelocity});
    if(!scene_.prepareAuthored(std::move(packet),error))return false;
    ++revision_;pending_=std::move(graph);pendingMotions_=std::move(motions);
    pendingDynamic_=std::move(dynamic);phase_=phase;failure_.clear();return true;
}
bool ImportedWallPhysics::initialize(const ImportedAssemblySource& source,glm::dvec3 origin,
    glm::quat orientation,std::string& error) {
    if(active()||phase_!=Phase::Empty){error="Wall already installed.";return false;}
    for(int axis=0;axis<3;++axis)if(!std::isfinite(origin[axis])||std::abs(origin[axis])>1e10) {
        error="Invalid wall origin.";return false;
    }
    const auto norm=glm::dot(orientation,orientation);
    if(!std::isfinite(norm)||std::abs(norm-1.f)>.001f){error="Invalid wall orientation.";return false;}
    orientation=glm::normalize(orientation);
    auto graph=ImportedAssembly::prepare(source,error);if(!graph)return false;
    origin_=origin;orientation_=orientation;
    const auto motion=initialMotion(*graph);
    if(!stage(*graph,motion,std::vector<bool>(motion.size(),false),Phase::Installing,error))return false;
    initialCells_=cellsFor(*graph,motion);original_=std::move(graph);return true;
}
bool ImportedWallPhysics::release(std::string& error,std::optional<uint64_t> removePart) {
    if(phase_!=Phase::Intact||!original_){error="The wall is not ready to release.";return false;}
    std::set<uint64_t> eligible;
    for(const auto& root:original_->roots())if(root.anchored)
        for(const auto index:root.partIndices)eligible.insert(original_->source().parts[index].sourceId);
    if(eligible.empty()){error="This wall has no validated foundation anchors.";return false;}
    std::optional<ImportedAssembly> cut;
    if(removePart) {
        if(!accepted_||!accepted_->rootForPart(*removePart)||!eligible.contains(*removePart)) {
            error="Manual removal requires a current part with validated support.";return false;
        }
        cut=original_->prepareRemovePart(original_->source().revision,*removePart,error);
        if(!cut)return false;
    } else cut=*original_;
    std::vector<uint64_t> bonds;
    for(const auto& bond:cut->source().bonds)
        if(bond.active&&eligible.contains(bond.lowerPart)&&eligible.contains(bond.upperPart))bonds.push_back(bond.id);
    if(!bonds.empty()||!cut->source().anchors.empty()) {
        cut=cut->prepareCut(cut->source().revision,bonds,cut->source().anchors,error);
        if(!cut)return false;
    }
    std::vector<bool> dynamic;
    for(const auto& root:cut->roots())dynamic.push_back(std::any_of(root.partIndices.begin(),root.partIndices.end(),
        [&](auto index){return eligible.contains(cut->source().parts[index].sourceId);}));
    auto motion=initialMotion(*cut);
    if(!stage(std::move(*cut),std::move(motion),std::move(dynamic),Phase::Releasing,error))return false;
    changed_=true;observationPending_=false;return true;
}
std::optional<uint64_t> ImportedWallPhysics::contactPart(physics::BodyHandle body,uint32_t feature) const noexcept {
    if(!ready()||!accepted_||!(feature&0x80000000u))return {};
    const auto face=feature&0x7fffffffu;
    for(size_t i=0;i<accepted_->roots().size();++i)if(scene_.body(i)==body) {
        const auto faces=accepted_->roots()[i].shape.faces();
        return face<faces.size()?accepted_->partForFeature(faces[face].source):std::nullopt;
    }
    return {};
}
bool ImportedWallPhysics::impact(const Impact& hit,std::string& error) {
    error.clear();
    const auto part=contactPart(hit.target,hit.targetFeature);
    if(!part||!original_||hit.geometryRevision!=publishedRevision_||!hit.projectile.valid()
        ||hit.projectile==hit.target) {error="The impact no longer matches the accepted wall.";return false;}
    for(const auto& binding:bindings_)if(binding.body==hit.projectile) {
        error="The projectile cannot be a wall root.";return false;
    }
    const double directionLength=glm::length(glm::dvec3(hit.direction));
    if(!std::isfinite(directionLength)||directionLength<.99||directionLength>1.01
        ||!std::isfinite(hit.normalImpulse)||hit.normalImpulse<=0
        ||!std::isfinite(hit.closingSpeed)||hit.closingSpeed<=0
        ||!std::isfinite(hit.projectileMass)||hit.projectileMass<=0
        ||!std::isfinite(hit.projectileEnergy)||hit.projectileEnergy<=0) {
        error="The impact has no finite kinetic evidence.";return false;
    }
    std::set<uint64_t> eligible;
    for(const auto& root:original_->roots())if(root.anchored)
        for(auto index:root.partIndices)eligible.insert(original_->source().parts[index].sourceId);
    if(!eligible.contains(*part)){error="This source part has no validated detachable support.";return false;}
    const auto oldRoot=*accepted_->rootForPart(*part);
    physics::AuthoredFrameError frameError;
    const auto localPoint=physics::AuthoredBodyFrame(accepted_->roots()[oldRoot].shape)
        .rootPoint(hit.targetLocalPoint,frameError);
    if(!localPoint){error="The impact contact point is invalid.";return false;}
    const auto& oldMotion=motions_[oldRoot];
    const auto point=physics::worldPositionToAbsolute(oldMotion.position)
        +glm::dquat(oldMotion.orientation)*glm::dvec3(*localPoint);
    // A compact, connected source neighborhood. Distance uses the current
    // certified pose, so a second hit never teleports debris to its source pose.
    std::vector<std::pair<double,uint64_t>> candidates;
    for(uint32_t i=0;i<accepted_->source().parts.size();++i) {
        const auto& candidate=accepted_->source().parts[i];if(!eligible.contains(candidate.sourceId))continue;
        const auto r=*accepted_->rootForPart(candidate.sourceId);
        const auto* catalog=importedPartCatalog(candidate.partNumber);
        if(!catalog)continue;
        // Distance to the actual catalog body envelope, not its centre. A long
        // plate can support the impact neighborhood at its far end. Transform
        // through the accepted root pose and the source part's rotation so
        // subsequent impacts use the current oriented body, not an expanded AABB.
        const auto inRoot=glm::inverse(glm::dquat(motions_[r].orientation))
            *(point-physics::worldPositionToAbsolute(motions_[r].position));
        const auto inPart=glm::inverse(candidate.rotation)
            *(inRoot+accepted_->roots()[r].origin-candidate.translation);
        const glm::dvec3 minimum(-double(catalog->studsX)*.5,-catalog->height,-double(catalog->studsZ)*.5);
        const glm::dvec3 maximum(double(catalog->studsX)*.5,0,double(catalog->studsZ)*.5);
        const auto distance=glm::length(inPart-glm::clamp(inPart,minimum,maximum));
        if(distance<=2.5||candidate.sourceId==*part)candidates.emplace_back(distance,candidate.sourceId);
    }
    std::sort(candidates.begin(),candidates.end());
    std::set<uint64_t> selected{*part};
    // Original validated adjacency also permits a later shot to move already
    // disconnected, settled neighbors; their current positions must still fit.
    bool grew=true;
    while(grew&&selected.size()<8) {
        grew=false;
        for(const auto& [distance,id]:candidates) {
            static_cast<void>(distance);if(selected.contains(id))continue;
            const bool adjacent=std::any_of(original_->source().bonds.begin(),original_->source().bonds.end(),[&](const auto& bond){
                return (bond.lowerPart==id&&selected.contains(bond.upperPart))
                    ||(bond.upperPart==id&&selected.contains(bond.lowerPart));});
            if(adjacent){selected.insert(id);grew=true;if(selected.size()==8)break;}
        }
    }
    std::vector<uint64_t> bonds,anchors;
    for(const auto& bond:accepted_->source().bonds)if(bond.active
        &&(selected.contains(bond.lowerPart)||selected.contains(bond.upperPart)))bonds.push_back(bond.id);
    for(const auto anchor:accepted_->source().anchors)if(selected.contains(anchor))anchors.push_back(anchor);
    auto graph=*accepted_;auto motion=motions_;
    if(!bonds.empty()||!anchors.empty()) {
        auto cut=accepted_->prepareCut(accepted_->source().revision,bonds,anchors,error);if(!cut)return false;
        auto inherited=cut->inheritMotion(*accepted_,motions_,error);if(!inherited)return false;
        graph=std::move(*cut);motion=std::move(*inherited);
    }
    // Resolve contact identity/point against the accepted detailed shape first.
    // Replace every selected-section shell in the same atomic transaction as
    // the cut, so rigid stud interlocking cannot reintroduce broken graph bonds.
    // Retained house geometry and original rebuild geometry remain untouched.
    if(graph.collisionProfile()==ImportedAssembly::CollisionProfile::Detailed) {
        auto shell=graph.prepareReleasedShell(graph.source().revision,error);if(!shell)return false;
        auto inherited=shell->inheritMotion(graph,motion,error);if(!inherited)return false;
        graph=std::move(*shell);motion=std::move(*inherited);
    }
    std::vector<bool> dynamic(graph.roots().size(),false);size_t released=0;double totalMass=0;
    for(size_t i=0;i<graph.roots().size();++i) {
        const auto& root=graph.roots()[i];
        bool touched=false,wasAnchored=false;
        for(const auto index:root.partIndices) {
            const auto id=graph.source().parts[index].sourceId;
            touched|=selected.contains(id);
            wasAnchored|=accepted_->roots()[*accepted_->rootForPart(id)].anchored;
        }
        dynamic[i]=!root.anchored&&(touched||wasAnchored);
        if(dynamic[i]){released+=root.partIndices.size();totalMass+=root.mass.massKg;}
    }
    if(!released||released>32||!std::isfinite(totalMass)||totalMass<=0) {
        error="This local impact cannot produce a bounded supported fragment set.";return false;
    }
    // Deliberately dissipative interim transfer. The projectile already hit a
    // fixed wall and is consumed. Its impulse enters only the struck root at
    // the closest actual collision material. Released neighbors start at rest;
    // GPU contacts and gravity determine their motion instead of broadcasting
    // energy/torque into unrelated corners of the source neighborhood.
    const double budget=std::min({36.,.2*hit.projectileEnergy,
        .1*hit.projectileMass*hit.closingSpeed*hit.closingSpeed});
    struct Transfer {
        physics::AuthoredBodyMotion body;
        glm::dvec3 angular{},inverseInertia{};
        double mass=0;
    };
    std::vector<Transfer> transfers(motion.size());
    const auto direction=glm::dvec3(hit.direction)/directionLength;
    const auto struckRoot=graph.rootForPart(*part);
    if(!struckRoot||!dynamic[*struckRoot]) {error="The struck fragment did not detach.";return false;}
    double unitEnergy=0,maximumAngular=0,packedMass=0;
    for(size_t i=0;i<motion.size();++i)if(i==*struckRoot) {
        const auto& shape=graph.roots()[i].shape;const auto& packed=shape.packedMass();
        auto& transfer=transfers[i];
        const auto body=physics::AuthoredBodyFrame(shape).bodyMotion(motion[i],frameError);
        if(!body){error="The fragment has an invalid current physical frame.";return false;}
        transfer.body=*body;transfer.mass=1./double(packed.centerInverseMass[3]);
        transfer.inverseInertia={packed.inverseInertiaRadius[0],packed.inverseInertiaRadius[1],packed.inverseInertiaRadius[2]};
        const auto rootRotation=glm::dquat(motion[i].orientation);
        const auto inRoot=glm::inverse(rootRotation)
            *(point-physics::worldPositionToAbsolute(motion[i].position));
        glm::dvec3 closest{};double nearest=std::numeric_limits<double>::infinity();
        for(const auto& cell:shape.cells()) {
            const glm::dvec3 minimum=glm::dvec3(cell.minimum[0],cell.minimum[1],cell.minimum[2])*physics::kAuthoredShapeTickMetres;
            const glm::dvec3 maximum=glm::dvec3(cell.maximum[0],cell.maximum[1],cell.maximum[2])*physics::kAuthoredShapeTickMetres;
            const auto candidate=glm::clamp(inRoot,minimum,maximum);
            const auto distance=glm::dot(candidate-inRoot,candidate-inRoot);
            if(distance<nearest){nearest=distance;closest=candidate;}
        }
        const glm::dvec3 center(packed.centerInverseMass[0],packed.centerInverseMass[1],packed.centerInverseMass[2]);
        const auto lever=rootRotation*(closest-center);
        const auto principalRotation=glm::dquat(body->orientation);
        const auto torque=glm::inverse(principalRotation)*glm::cross(lever,transfer.mass*direction);
        const auto angularPrincipal=transfer.inverseInertia*torque;
        transfer.angular=principalRotation*angularPrincipal;
        const double energy=.5*(transfer.mass+glm::dot(angularPrincipal,angularPrincipal/transfer.inverseInertia));
        if(!std::isfinite(nearest)||!std::isfinite(energy)||energy<=0) {
            error="The contact cannot produce a finite physical fragment impulse.";return false;
        }
        unitEnergy+=energy;packedMass+=transfer.mass;
        maximumAngular=std::max(maximumAngular,glm::length(transfer.angular));
    }
    // Unit response has COM speed1 and total impulse equal to struck mass. A single
    // scale preserves its torque/translation ratio while bounding ALL energy.
    double scale=std::min({12.,std::sqrt(budget/unitEnergy),.2*hit.normalImpulse/packedMass});
    if(maximumAngular>0)scale=std::min(scale,10./maximumAngular);
    scale*=.999999; // Reserve float conversion roundoff inside the energy cap.
    if(!std::isfinite(scale)||scale<=0){error="The impact has no transferable energy.";return false;}
    double actualEnergy=0,rotationalEnergy=0;
    for(size_t i=0;i<motion.size();++i) {
        motion[i].angularVelocity={};motion[i].originVelocity={};
        if(i!=*struckRoot)continue;
        auto& transfer=transfers[i];
        transfer.body.centerVelocity=glm::vec3(direction*scale);
        transfer.body.angularVelocity=glm::vec3(transfer.angular*scale);
        const auto rootMotion=physics::AuthoredBodyFrame(graph.roots()[i].shape).rootMotion(transfer.body,frameError);
        if(!rootMotion){error="The fragment impulse is not representable.";return false;}
        motion[i]=*rootMotion;
        // Account for the exact motion that typed admission reconstructs from
        // the rounded root-origin velocity, not just the double calculation.
        const auto admitted=physics::AuthoredBodyFrame(graph.roots()[i].shape).bodyMotion(motion[i],frameError);
        if(!admitted){error="The fragment impulse frame is not representable.";return false;}
        const auto angular=glm::inverse(glm::dquat(admitted->orientation))*glm::dvec3(admitted->angularVelocity);
        const double rotational=.5*glm::dot(angular,angular/transfer.inverseInertia);
        rotationalEnergy+=rotational;
        actualEnergy+=.5*transfer.mass*glm::dot(glm::dvec3(admitted->centerVelocity),glm::dvec3(admitted->centerVelocity))+rotational;
    }
    if(!std::isfinite(actualEnergy)||actualEnergy>budget) {
        error="The prepared fragment motion exceeds its energy budget.";return false;
    }
    if(!stage(std::move(graph),std::move(motion),std::move(dynamic),Phase::Releasing,error,hit.projectile))return false;
    impactStats_={impactStats_.count+1,*part,released,budget,actualEnergy,rotationalEnergy,point,direction};
    changed_=true;observationPending_=false;return true;
}
double ImportedWallPhysics::maximumPartDisplacement() const noexcept {
    if(!ready()||!accepted_||motions_.size()!=accepted_->roots().size())return 0;
    double maximum=0;
    for(size_t i=0;i<accepted_->roots().size();++i) {
        const auto& root=accepted_->roots()[i];const auto& motion=motions_[i];
        for(const auto index:root.partIndices) {
            const auto& part=accepted_->source().parts[index];
            const auto original=origin_+glm::dquat(orientation_)*part.translation;
            const auto current=physics::worldPositionToAbsolute(motion.position)
                +glm::dquat(motion.orientation)*(part.translation-root.origin);
            maximum=std::max(maximum,glm::length(current-original));
        }
    }
    return maximum;
}
bool ImportedWallPhysics::reset(std::string& error) {
    if(!original_||phase_==Phase::Clearing||phase_==Phase::Recovering) {
        error="Wait for the wall change to finish before rebuilding.";return false;
    }
    if(sceneFailed_) {
        // A failed bridge can own both accepted roots and partially reserved
        // replacement resources. Drain all of them before constructing a fresh
        // bridge. Keep the source/changed state until its new admission completes.
        scene_.requestClear();phase_=Phase::Recovering;observationPending_=false;
        failure_.clear();error.clear();return true;
    }
    if(pending_){error="Wait for the wall change to finish before rebuilding.";return false;}
    auto motion=initialMotion(*original_);
    if(!stage(*original_,motion,std::vector<bool>(motion.size(),false),Phase::Resetting,error))return false;
    observationPending_=false;return true;
}
std::span<const ImportedWallPhysics::Binding> ImportedWallPhysics::bindingsForEncodedTick(uint64_t tick) const noexcept {
    if(phase_==Phase::Recovering||phase_==Phase::Clearing)return {};
    if(pendingBindingTick_&&tick>=pendingBindingTick_)return pendingBindings_;
    return bindings_;
}
void ImportedWallPhysics::cachePendingBindings() {
    const auto target=scene_.pendingExecutionTick();
    if(!pending_||!target||phase_==Phase::Recovering||phase_==Phase::Clearing||pendingBindingTick_==target)return;
    std::vector<Binding> next;
    for(size_t i=0;i<pending_->roots().size();++i) {
        const auto& root=pending_->roots()[i];const auto body=scene_.pendingBody(i);
        if(!body.valid())return;
        for(const auto index:root.partIndices) {
            const auto& part=pending_->source().parts[index];
            next.push_back({part.sourceId,part.meshNode,
                glm::translate(glm::dmat4(1),-root.origin)*pending_->partMatrix(index),body});
        }
    }
    pendingBindings_=std::move(next);pendingBindingTick_=target;
}
void ImportedWallPhysics::publish() {
    pendingBindings_.clear();pendingBindingTick_=0;
    accepted_=std::move(pending_);pending_.reset();motions_=std::move(pendingMotions_);dynamic_=std::move(pendingDynamic_);
    bindings_.clear();cells_.clear();
    for(size_t i=0;i<accepted_->roots().size();++i) {
        const auto& root=accepted_->roots()[i];
        for(const auto index:root.partIndices) {
            const auto& part=accepted_->source().parts[index];
            bindings_.push_back({part.sourceId,part.meshNode,
                glm::translate(glm::dmat4(1),-root.origin)*accepted_->partMatrix(index),scene_.body(i)});
        }

    }
    if(std::none_of(dynamic_.begin(),dynamic_.end(),[](bool v){return v;}))cells_=cellsFor(*accepted_,motions_);
    publishedRevision_=revision_;
}
bool ImportedWallPhysics::observe(const physics::DebugSnapshot& snapshot,bool exact,
    const physics::PhysicsTickFrontier& frontier,std::vector<physics::AuthoredRootMotion>& motions) const {
    if(!accepted_||snapshot.confirmedIncarnation!=frontier.incarnation||!snapshot.tick
        ||snapshot.tick>frontier.completed||(exact&&(!joined(frontier)||snapshot.tick!=frontier.completed)))return false;
    motions=motions_;
    for(size_t i=0;i<dynamic_.size();++i)if(dynamic_[i]) {
        const auto body=scene_.body(i);
        const auto found=std::find_if(snapshot.bodies.begin(),snapshot.bodies.end(),[&](const auto& s){return s.handle==body;});
        if(found==snapshot.bodies.end()||!found->alive||found->awake)return false;
        physics::AuthoredFrameError issue;
        const auto root=physics::AuthoredBodyFrame(accepted_->roots()[i].shape).rootMotion({
            .centerPosition={found->sector,found->position},.orientation=found->orientation,
            .centerVelocity=found->linearVelocity,.angularVelocity=found->angularVelocity},issue);
        if(!root)return false;
        motions[i]=*root;motions[i].originVelocity={};motions[i].angularVelocity={};
    }
    return true;
}
void ImportedWallPhysics::requestObservation(physics::PhysicsWorld& world) {
    uint32_t first=UINT32_MAX,last=0;
    for(size_t i=0;i<dynamic_.size();++i)if(dynamic_[i]) {const auto body=scene_.body(i);first=std::min(first,body.index);last=std::max(last,body.index);}
    if(last>=first){world.requestDebugSnapshot({first,last-first+1});observationPending_=true;}
}
bool ImportedWallPhysics::needsQuiescentBoundary() const noexcept {
    return scene_.needsQuiescentBoundary()||phase_==Phase::Joining||phase_==Phase::Certifying;
}
void ImportedWallPhysics::requestClear() noexcept {scene_.requestClear();phase_=Phase::Clearing;}
void ImportedWallPhysics::abandonAfterWorldShutdown() noexcept {
    scene_.abandonAfterWorldShutdown();original_.reset();accepted_.reset();pending_.reset();
    motions_.clear();pendingMotions_.clear();dynamic_.clear();pendingDynamic_.clear();bindings_.clear();pendingBindings_.clear();cells_.clear();initialCells_.clear();
    phase_=Phase::Empty;revision_=publishedRevision_=releaseTick_=certificationTick_=pendingBindingTick_=0;changed_=observationPending_=sceneFailed_=false;failure_.clear();impactStats_={};
}
bool ImportedWallPhysics::update(physics::PhysicsWorld& world,std::string& error) {
    error.clear();if(phase_==Phase::Empty)return true;
    if(world.tickFrontier().failed) {
        failure_="Physics stopped safely. Reload the world to restore the house.";
        error=failure_;phase_=Phase::Failed;sceneFailed_=true;return false;
    }
    const auto progress=scene_.update(world,error);
    if(progress==CannonPhysicsScene::Progress::Failed){failure_=error;phase_=Phase::Failed;sceneFailed_=true;return false;}
    if(phase_==Phase::Recovering) {
        if(!scene_.empty())return true;
        // The bridge is certified empty; no live-world handle is abandoned.
        scene_.abandonAfterWorldShutdown();pending_.reset();accepted_.reset();
        motions_.clear();pendingMotions_.clear();dynamic_.clear();pendingDynamic_.clear();
        bindings_.clear();pendingBindings_.clear();pendingBindingTick_=0;cells_.clear();sceneFailed_=false;
        auto motion=initialMotion(*original_);
        if(!stage(*original_,motion,std::vector<bool>(motion.size(),false),Phase::Resetting,error)) {
            failure_=error;phase_=Phase::Failed;sceneFailed_=true;return false;
        }
        return true;
    }
    if(phase_==Phase::Clearing) {
        if(scene_.empty())abandonAfterWorldShutdown(); // Empty means fully retired; no world abandonment is needed.
        return true;
    }
    cachePendingBindings();
    if(pending_&&scene_.ready(revision_)) {
        const auto previous=phase_;publish();
        if(previous==Phase::Releasing) {
            phase_=std::any_of(dynamic_.begin(),dynamic_.end(),[](bool value){return value;})
                ?Phase::Falling:Phase::Settled;
            releaseTick_=world.tickFrontier().completed;
        }
        else if(previous==Phase::Freezing)phase_=Phase::Settled;
        else {phase_=Phase::Intact;changed_=false;}
    }
    if(phase_!=Phase::Falling&&phase_!=Phase::Joining&&phase_!=Phase::Certifying) {
        if(phase_==Phase::Failed){error=failure_;return false;}return true;
    }
    const auto frontier=world.tickFrontier();
    if(frontier.completed>releaseTick_+1200) {
        failure_="Pieces haven't settled; rebuild the wall.";error=failure_;phase_=Phase::Failed;return false;
    }
    while(auto snapshot=world.pollDebugSnapshot()) {
        observationPending_=false;
        std::vector<physics::AuthoredRootMotion> motion;
        if(phase_==Phase::Falling&&observe(*snapshot,false,frontier,motion))phase_=Phase::Joining;
        else if(phase_==Phase::Certifying&&snapshot->tick==certificationTick_) {
            if(observe(*snapshot,true,frontier,motion)) {
                const auto count=motion.size();
                if(!stage(*accepted_,std::move(motion),std::vector<bool>(count,false),Phase::Freezing,error)) {
                    failure_=error;phase_=Phase::Failed;return false;
                }
            } else phase_=Phase::Falling;
        }
    }
    if(phase_==Phase::Joining&&joined(frontier)) {
        requestObservation(world);
        // A joined frontier already proves the current poses. Capture those
        // poses in an owned zero-tick submission, without taking control of the
        // borrowed world's accumulator/fixed-tick scheduling mode.
        certificationTick_=frontier.completed;phase_=Phase::Certifying;
    } else if(phase_==Phase::Falling&&!observationPending_)requestObservation(world);
    return true;
}
} // namespace voxy::game::adventure

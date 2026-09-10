#include "game/expedition/cove_harbor_runtime.hpp"
#include "physics/authored_shape_resources.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace voxy::game::expedition {
namespace {
bool overlaps(glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,glm::dvec3 d){
    return a.x<d.x&&b.x>c.x&&a.y<d.y&&b.y>c.y&&a.z<d.z&&b.z>c.z;
}
bool occupied(const CoveHarborLift& structure,glm::dvec3 fixed,
    const CoveBoatAssembly& assembly,const physics::AuthoredRootMotion& motion){
    const auto origin=physics::worldPositionToAbsolute(motion.position)-fixed;
    for(const auto& cell:assembly.shape().cells()){
        glm::dvec3 lo(std::numeric_limits<double>::infinity()),hi(-std::numeric_limits<double>::infinity());
        for(unsigned corner=0;corner<8;++corner){
            const glm::vec3 local{float((corner&1u)?cell.maximum[0]:cell.minimum[0])*.02f,
                float((corner&2u)?cell.maximum[1]:cell.minimum[1])*.02f,
                float((corner&4u)?cell.maximum[2]:cell.minimum[2])*.02f};
            const auto p=origin+glm::dvec3(motion.orientation*local);lo=glm::min(lo,p);hi=glm::max(hi,p);
        }
        for(const auto& solid:structure.structure()){
            const auto a=solid.bounds.minimum,b=solid.bounds.maximum;
            if(overlaps(lo-glm::dvec3(.1),hi+glm::dvec3(.1),glm::dvec3(a.x,a.y,a.z)*.02,glm::dvec3(b.x,b.y,b.z)*.02))return true;
        }
    }
    return false;
}
}
std::unique_ptr<CoveHarborRuntime> CoveHarborRuntime::create(const assets::CoveNavigation& navigation,
    glm::dvec3 origin,const CoveBoatAssembly& boat,CoveHarborLiftState saved,std::string& error){
    try {
        if(!validCoveHarborLiftState(saved)){error="Invalid saved harbor state.";return {};}
        auto result=std::make_unique<CoveHarborRuntime>();result->navigation_=navigation;
        result->message_.reserve(256);result->rigIssue_.reserve(256);
        result->structure_=CoveHarborLift::prepareStructure(navigation,error);if(!result->structure_)return {};
        result->motion_.position=physics::worldPositionFromAbsolute(origin+result->structure_->center());
        if(!result->configureBoat(boat)){error="Cannot configure harbor rig.";return {};}
        if(saved.mode!=CoveHarborLiftMode::Detached&&(!result->rig_||!result->rig_->restoreLines(saved,error)))return {};
        result->saved_=saved;result->durable_=saved.profile!=0;
        result->stage_=saved.profile?Stage::Uploading:Stage::Absent;
        result->restoreRopes_=saved.mode!=CoveHarborLiftMode::Detached;
        error.clear();return result;
    }catch(const std::bad_alloc&){error="Not enough memory for the harbor lift.";return {};}
}
bool CoveHarborRuntime::fail(std::string_view error){stage_=Stage::Failed;message_=error;return false;}
bool CoveHarborRuntime::hasRopes() const noexcept {
    return std::any_of(ropes_.begin(),ropes_.end(),[](auto rope){return rope.valid();});
}
bool CoveHarborRuntime::configureBoat(const CoveBoatAssembly& boat){
    if(rigIdentity_==boat.assembly().inputIdentity())return true;
    if(hasRopes()||changing_)return false;
    rig_=CoveHarborLift::prepare(boat,navigation_,rigIssue_);
    rigIdentity_=boat.assembly().inputIdentity();return true;
}
bool CoveHarborRuntime::requestInstall(){
    if(stage_!=Stage::Absent||saved_.profile)return false;
    stage_=Stage::Requested;installing_=true;message_="Preparing the harbor lift…";return true;
}
void CoveHarborRuntime::cancelInstallation(std::string_view reason){
    if(stage_==Stage::Requested){stage_=Stage::Absent;installing_=false;message_=reason;}
}
std::optional<physics::AuthoredRootMotion> CoveHarborRuntime::prepareCargoParking(
    const CoveBoatAssembly& cargo,const CoveBoatAssembly& boat,const physics::AuthoredRootMotion& boatPose,
    const CoveSceneryCollision& scenery,glm::dvec3 origin,glm::dvec3 feet,const CovePlayer::Ground& ground){
    if(stage_!=Stage::Requested||!ground)return {};
    const auto bounds=cargo.shape().rootBounds();
    const auto lo=glm::dvec3(bounds.minimum.x,bounds.minimum.y,bounds.minimum.z)*.02;
    const auto hi=glm::dvec3(bounds.maximum.x,bounds.maximum.y,bounds.maximum.z)*.02;
    const int nx=static_cast<int>(std::ceil((hi.x-lo.x)/.16))+1,nz=static_cast<int>(std::ceil((hi.z-lo.z)/.16))+1;
    if(nx<2||nz<2||nx>64||nz>64||nx*nz>4096)return {};
    // Place the banked load along the outer edge of the existing pier,
    // leaving its inner walking lane and the boat berth clear. Support comes
    // from the unchanged LEGO terrain or actual fixed scenery below deck height.
    for(const double offset:{2.,4.,6.}){
        auto target=navigation_.spawn+glm::dvec3(1,0,offset);
        double top=-std::numeric_limits<double>::infinity(),bottom=std::numeric_limits<double>::infinity();
        for(int x=0;x<nx;++x)for(int z=0;z<nz;++z){
            const auto px=target.x+lo.x+(hi.x-lo.x)*x/(nx-1);
            const auto pz=target.z+lo.z+(hi.z-lo.z)*z/(nz-1);
            auto y=ground(px,pz);
            if(!std::isfinite(y)){top=std::numeric_limits<double>::infinity();continue;}
            for(const auto& cell:scenery.shape().cells()){
                const auto a=scenery.origin()+glm::dvec3(cell.minimum[0],cell.minimum[1],cell.minimum[2])*.02;
                const auto b=scenery.origin()+glm::dvec3(cell.maximum[0],cell.maximum[1],cell.maximum[2])*.02;
                if(px>=a.x&&px<=b.x&&pz>=a.z&&pz<=b.z&&b.y<=navigation_.spawn.y+.64)
                    y=std::max(y,b.y);
            }
            top=std::max(top,y);bottom=std::min(bottom,y);
        }
        if(!std::isfinite(top)||bottom<0||top-bottom>.64)continue;
        target.y=top-lo.y+.005;const auto a=target+lo,b=target+hi;
        if(overlaps(a,b,feet-glm::dvec3(.4,.05,.4),feet+glm::dvec3(.4,1.8,.4)))continue;
        const auto collides=[&](const physics::AuthoredShape& shape,glm::dvec3 position,glm::quat orientation,glm::dvec3 clearance){
            for(const auto& cell:shape.cells()){
                glm::dvec3 minimum(std::numeric_limits<double>::infinity()),maximum(-std::numeric_limits<double>::infinity());
                for(unsigned corner=0;corner<8;++corner){
                    const glm::vec3 local{float((corner&1u)?cell.maximum[0]:cell.minimum[0])*.02f,
                        float((corner&2u)?cell.maximum[1]:cell.minimum[1])*.02f,float((corner&4u)?cell.maximum[2]:cell.minimum[2])*.02f};
                    const auto p=position+glm::dvec3(orientation*local);minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);
                }
                if(overlaps(a-clearance,b+clearance,minimum,maximum))return true;
            }
            return false;
        };
        if(collides(scenery.shape(),scenery.origin(),glm::quat(1,0,0,0),{.1,0,.1})
            ||collides(boat.shape(),physics::worldPositionToAbsolute(boatPose.position)-origin,boatPose.orientation,glm::dvec3(.1)))continue;
        physics::AuthoredRootMotion pose;pose.position=physics::worldPositionFromAbsolute(origin+target);return pose;
    }
    message_="Clear space on the pier before powering the lift.";return {};
}
bool CoveHarborRuntime::checkInstallationSpace(const CoveBoatAssembly& boat,const physics::AuthoredRootMotion& boatPose,
    const CoveBoatAssembly& cargo,const physics::AuthoredRootMotion& cargoPose,glm::dvec3 feet){
    if(stage_!=Stage::Requested)return false;
    const auto fixed=physics::worldPositionToAbsolute(motion_.position);
    bool blocked=occupied(*structure_,fixed,boat,boatPose)||occupied(*structure_,fixed,cargo,cargoPose);
    const auto player=feet-fixed;
    for(const auto& solid:structure_->structure()){
        const auto a=solid.bounds.minimum,b=solid.bounds.maximum;
        blocked|=overlaps(player-glm::dvec3(.4,.05,.4),player+glm::dvec3(.4,1.8,.4),
            glm::dvec3(a.x,a.y,a.z)*.02,glm::dvec3(b.x,b.y,b.z)*.02);
    }
    if(blocked){stage_=Stage::Absent;installing_=false;message_="Move away from the lift posts, then try again.";return false;}
    stage_=Stage::Uploading;return true;
}
bool CoveHarborRuntime::update(physics::PhysicsWorld& world,physics::BodyHandle boat,bool& needsTick){
    needsTick=false;
    if(stage_==Stage::Failed)return false;
    if(stage_==Stage::Absent||stage_==Stage::Requested||stage_==Stage::Drained)return true;
    if(boat_!=boat){if(hasRopes())return fail("A suspended boat cannot be replaced.");boat_=boat;}
    auto* resources=world.authoredShapeResources();if(!resources)return fail("Harbor shape storage is unavailable.");
    if(stage_==Stage::Uploading){
        if(resources->stats().phase!=physics::ShapeResourcePhase::Ready)return true;
        if(!shape_.valid()){
            auto shape=structure_->shape();physics::ShapeResourceError issue;
            shape_=resources->upload(std::move(shape),issue);if(!shape_.valid())return fail("Harbor lift upload failed.");
        }
        if(resources->state(shape_)==physics::ShapeResourceState::Ready){
            const auto result=world.spawnAuthoredBody({.shape=shape_,.motion=motion_,.motionType=physics::AuthoredBodyMotionType::Static});
            if(!result)return fail("Harbor lift admission failed.");
            body_=result.body;bodyChangedTick_=world.tickFrontier().scheduled+1;saved_.profile=kCoveHarborLiftProfile;stage_=Stage::Admitting;needsTick=true;
        }
    }
    if(restoreRopes_&&body_.valid()&&boat_.valid()){
        const auto descriptors=rig_?rig_->restoreLines(saved_,message_):std::nullopt;
        if(!descriptors)return fail("Saved lift constraints cannot be restored.");
        for(size_t i=0;i<ropes_.size();++i){
            if(saved_.brokenMask&(1u<<i)){observed_[i].broken=true;continue;}
            auto desc=(*descriptors)[i];desc.bodyA=body_;desc.bodyB=boat_;
            ropes_[i]=world.createDistanceAttachment(desc);if(!ropes_[i].valid())return fail("Saved lift constraint admission failed.");
        }
        restoreRopes_=false;changing_=hasRopes();changedTick_=world.tickFrontier().scheduled+1;needsTick=true;
    }
    if(saved_.mode==CoveHarborLiftMode::Broken&&motor_!=0){if(!stop(world))return false;needsTick=true;}
    if(stage_==Stage::Closing&&!body_.valid()&&!hasRopes()){
        if(!retired_){
            if(shape_.valid()&&resources->retire(shape_)!=physics::ShapeResourceError::None)return fail("Harbor retirement failed.");
            retired_=true;
        }
        if(!shape_.valid()||resources->state(shape_)==physics::ShapeResourceState::Missing)stage_=Stage::Drained;
    }
    return true;
}
bool CoveHarborRuntime::observe(const physics::DebugSnapshot& snapshot){
    for(const auto& body:snapshot.bodies)if(body_.valid()&&body.handle.index==body_.index&&snapshot.tick>=bodyChangedTick_){
        if(!body.alive){if(stage_!=Stage::Closing)return fail("Harbor lift body disappeared.");body_={};continue;}
        if(body.handle!=body_||body.authoredShape!=shape_)return fail("Harbor body identity changed.");
        physics::AuthoredFrameError issue;
        const auto root=physics::AuthoredBodyFrame(structure_->shape()).rootMotion(
            {{body.sector,body.position},body.orientation,body.linearVelocity,body.angularVelocity},issue);
        if(!root||glm::length(physics::worldPositionToAbsolute(root->position)-physics::worldPositionToAbsolute(motion_.position))>.001
            ||std::abs(glm::dot(root->orientation,motion_.orientation))<.99999f
            ||glm::length(root->originVelocity)>.0001f||glm::length(root->angularVelocity)>.0001f)
            return fail("Fixed harbor lift moved.");
        observedTick_=snapshot.tick;if(stage_==Stage::Admitting)stage_=Stage::Ready;
    }
    for(const auto& observed:snapshot.attachments)for(size_t i=0;i<ropes_.size();++i)
        if(ropes_[i].valid()&&observed.handle.index==ropes_[i].index&&snapshot.tick>=changedTick_){
            if((releasing_||stage_==Stage::Closing)&&!observed.alive&&!observed.broken&&observed.handle.generation==0){
                // Destroy zeros the GPU slot. The owned, post-command packet
                // proves retirement; matching an old live generation cannot.
                ropes_[i]={};continue;
            }
            if(observed.handle!=ropes_[i])return fail("Harbor cable identity changed.");
            if(releasing_||stage_==Stage::Closing)continue;
            if((!observed.alive&&!observed.broken)||observed.distance.bodyA!=body_||observed.distance.bodyB!=boat_
                ||!std::isfinite(observed.distance.targetLength)||observed.distance.targetLength<observed.distance.minimumLength
                ||observed.distance.targetLength>observed.distance.maximumLength||!std::isfinite(observed.distance.motorSpeed))
                return fail("Invalid harbor cable observation.");
            observed_[i]=observed;ropeTicks_[i]=snapshot.tick;saved_.lengths[i]=observed.distance.targetLength;
            if(observed.broken){saved_.brokenMask|=static_cast<uint8_t>(1u<<i);saved_.mode=CoveHarborLiftMode::Broken;}
        }
    if(releasing_&&!hasRopes()){
        saved_={.profile=saved_.profile};releasing_=false;changing_=false;observed_={};ropeTicks_={};
    }
    if(changing_&&!releasing_){
        bool done=true;
        for(size_t i=0;i<ropes_.size();++i)if(ropes_[i].valid()&&ropeTicks_[i]<changedTick_)done=false;
        if(done)changing_=false;
    }
    return true;
}
bool CoveHarborRuntime::observe(const physics::PhysicsEventBatch& events){
    for(const auto& event:events.events)if(event.type==physics::PhysicsEventType::AttachmentBreak)
        for(size_t i=0;i<ropes_.size();++i)if(ropes_[i].valid()&&event.attachmentHandle()==ropes_[i]&&!releasing_){
            saved_.brokenMask|=static_cast<uint8_t>(1u<<i);saved_.mode=CoveHarborLiftMode::Broken;
            message_="A lift cable broke. Release the rig before trying again.";
        }
    return true;
}
bool CoveHarborRuntime::action(Action action,physics::PhysicsWorld& world,physics::BodyHandle boat,
    const physics::AuthoredRootMotion& pose){
    if(action==Action::Stop)return stop(world);
    if(stage_!=Stage::Ready||!durable_||installing_||boat!=boat_)return false;
    if(action==Action::Release)return release(world);
    if(changing_||releasing_)return false;
    if(action==Action::Attach){
        if(saved_.mode!=CoveHarborLiftMode::Detached||hasRopes())return false;
        if(!rig_){message_=rigIssue_;return false;}
        const auto descriptors=rig_->attach(pose,motion_,boat_,body_,message_);
        if(!descriptors){
            if(!attachRequested_){attachRequestTick_=world.tickFrontier().completed;attachAttemptTick_=0;}
            attachRequested_=true;message_="Waiting for safe alignment. Stop cancels the request.";return true;
        }
        attachRequested_=false;
        for(size_t i=0;i<ropes_.size();++i){
            ropes_[i]=world.createDistanceAttachment((*descriptors)[i]);
            if(!ropes_[i].valid()){
                if(!release(world))return false;
                message_="The lift cannot attach right now. Wait, then try again.";return false;
            }
            saved_.lengths[i]=(*descriptors)[i].targetLength;
        }
        saved_.mode=CoveHarborLiftMode::Attached;saved_.brokenMask=0;observed_={};ropeTicks_={};
        message_="Lift attached. Raise or lower the boat.";
    }else{
        if(saved_.mode!=CoveHarborLiftMode::Attached||!hasRopes())return false;
        const float speed=action==Action::Raise?kCoveHarborLiftReelSpeed:-kCoveHarborLiftLowerSpeed;
        for(const auto rope:ropes_)if(!world.setAttachmentMotorSpeed(rope,speed))return fail("Harbor motor control failed.");
        motor_=speed;
    }
    changedTick_=world.tickFrontier().scheduled+1;changing_=true;return true;
}
bool CoveHarborRuntime::pollAttachment(physics::PhysicsWorld& world,physics::BodyHandle boat,
    const physics::AuthoredRootMotion& pose,uint64_t tick){
    if(!attachRequested_||tick<=attachAttemptTick_)return true;
    attachAttemptTick_=tick;
    if(tick>=attachRequestTick_&&tick-attachRequestTick_>=600){
        attachRequested_=false;message_="Attachment timed out. Reposition the boat and try again.";return true;
    }
    (void)action(Action::Attach,world,boat,pose);
    return stage_!=Stage::Failed;
}
bool CoveHarborRuntime::stop(physics::PhysicsWorld& world){
    if(attachRequested_){attachRequested_=false;message_="Attachment canceled.";}
    if(motor_==0)return true;
    for(size_t i=0;i<ropes_.size();++i)if(ropes_[i].valid()&&!(saved_.brokenMask&(1u<<i)))
        if(!world.setAttachmentMotorSpeed(ropes_[i],0))return fail("Harbor motor stop failed.");
    motor_=0;changedTick_=world.tickFrontier().scheduled+1;changing_=hasRopes();return true;
}
bool CoveHarborRuntime::release(physics::PhysicsWorld& world){
    attachRequested_=false;
    if(releasing_)return true;
    for(const auto rope:ropes_)if(rope.valid()&&!world.destroyAttachment(rope))return fail("Harbor cable release failed.");
    motor_=0;releasing_=hasRopes();changing_=hasRopes();changedTick_=world.tickFrontier().scheduled+1;
    if(!hasRopes())saved_={.profile=saved_.profile};
    message_="Lift released.";return true;
}
bool CoveHarborRuntime::close(physics::PhysicsWorld& world){
    if(stage_==Stage::Closing||stage_==Stage::Drained)return true;
    if(!release(world))return false;
    if(body_.valid()&&!world.destroyBody(body_))return fail("Harbor lift shutdown failed.");
    bodyChangedTick_=world.tickFrontier().scheduled+1;stage_=Stage::Closing;return true;
}
bool CoveHarborRuntime::joined(uint64_t tick) const noexcept {
    if(stage_==Stage::Absent||((stage_==Stage::Requested||stage_==Stage::Uploading)&&!saved_.profile))return true;
    if(stage_!=Stage::Ready||attachRequested_||changing_||releasing_||restoreRopes_||observedTick_!=tick)return false;
    for(size_t i=0;i<ropes_.size();++i)if(ropes_[i].valid()&&ropeTicks_[i]!=tick)return false;
    return true;
}
std::optional<CoveHarborLiftState> CoveHarborRuntime::capture(uint64_t tick) const noexcept {
    if((stage_!=Stage::Absent&&stage_!=Stage::Ready)||!joined(tick)||motor_!=0||!validCoveHarborLiftState(saved_))return {};
    for(size_t i=0;i<ropes_.size();++i)if(ropes_[i].valid()&&!(saved_.brokenMask&(1u<<i))
        &&(!observed_[i].alive||observed_[i].distance.motorSpeed!=0))return {};
    return saved_;
}
bool CoveHarborRuntime::acknowledgeInstallation(){
    if(!installing_||stage_!=Stage::Ready||!saved_.profile)return false;
    message_="Harbor lift powered and saved. Resume to use it.";installing_=false;durable_=true;return true;
}
void CoveHarborRuntime::includeAttachmentRange(uint32_t& first,uint32_t& last) const noexcept {
    for(const auto rope:ropes_)if(rope.valid()){first=std::min(first,rope.index);last=std::max(last,rope.index);}
}
} // namespace voxy::game::expedition

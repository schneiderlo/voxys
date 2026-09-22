#include "game/adventure/builder_motorbike.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace voxy::game::adventure {
namespace {
glm::dvec3 forward(double yaw){return {-std::sin(yaw),0,-std::cos(yaw)};}
bool finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
constexpr double skin=.008,stepUp=.85,gravity=18.,maximumPitch=.60,bellyClearance=.55;
}
bool BuilderMotorbike::clear(const AdventureSpatialQueries& q,glm::dvec3 p,double yaw) const noexcept {
    for(int i=-1;i<=1;++i) {
        const auto at=p+forward(yaw)*(double(i)*halfWheelbase);
        if(!q.clearCapsule(at,i==0?1.12:.52,i==0?5.65:3.3))return false;
    }
    return true;
}
bool BuilderMotorbike::sweep(const AdventureSpatialQueries& q,glm::dvec3 a,double ay,glm::dvec3 b,double by) const noexcept {
    for(int i=-1;i<=1;++i) {
        const auto from=a+forward(ay)*(double(i)*halfWheelbase),to=b+forward(by)*(double(i)*halfWheelbase);
        const auto hit=q.sweepCapsule(from,to,i==0?1.12:.52,i==0?5.65:3.3);
        if(!hit.complete||hit.startOverlapped||hit.hit)return false;
    }
    return true;
}
std::optional<glm::dvec3> BuilderMotorbike::ground(const AdventureSpatialQueries& q,glm::dvec3 p,double yaw,double water) const noexcept {
    glm::dvec3 heights;
    for(int i=-1;i<=1;++i) {
        const auto at=p+forward(yaw)*(double(i)*halfWheelbase);
        const double y=q.supportHeight({at.x,at.z},i==0?1.12:.52,p.y+stepUp);
        if(!std::isfinite(y)||y<water+.05)return {};
        heights[i+1]=y;
    }
    return heights;
}
std::optional<std::array<double,2>> BuilderMotorbike::wheelSupport(const AdventureSpatialQueries& q,
    glm::dvec3 p,double yaw,double pitch,double water) const noexcept {
    const auto& terrain=q.terrain();
    if(!terrain.valid())return {};
    const auto origin=glm::dvec2(terrain.origin());
    std::array<double,2> result{};
    for(size_t i=0;i<2;++i) {
        const auto at=p+forward(yaw)*((i?1.:-1.)*halfWheelbase*std::cos(pitch));
        const glm::dvec2 cell=(glm::dvec2(at.x,at.z)+origin)/double(terrain.cellScale);
        const double margin=wheelRadius/double(terrain.cellScale);
        if(cell.x<margin||cell.y<margin||cell.x>double(terrain.width-1)-margin
            ||cell.y>double(terrain.height-1)-margin)return {};
        // The real tire radius rolls around stud and plate edges. The old .52
        // support probe was smaller than the .784 wheel and made it cut corners.
        const double y=q.supportHeight({at.x,at.z},wheelRadius,p.y+stepUp);
        if(!std::isfinite(y)||y<water+.05)return {};
        result[i]=y+wheelRadius+skin;
    }
    return result;
}
bool BuilderMotorbike::place(const AdventureSpatialQueries& q,glm::dvec3 p,double yaw,double water) noexcept {
    if(!finite(p)||!std::isfinite(yaw)||!std::isfinite(water))return false;
    double pitch=0,low=-maximumPitch,high=maximumPitch;
    std::optional<std::array<double,2>> wheels;
    for(int i=0;i<24;++i) {
        wheels=wheelSupport(q,p,yaw,pitch,water);if(!wheels)return false;
        const double error=((*wheels)[1]-(*wheels)[0])-2*halfWheelbase*std::sin(pitch);
        if(std::abs(error)<1e-6)break;
        if(error>0)low=pitch;else high=pitch;
        pitch=(low+high)*.5;
    }
    wheels=wheelSupport(q,p,yaw,pitch,water);if(!wheels)return false;
    // Mount only when both tires have a stable supported pose.
    if(std::abs(((*wheels)[1]-(*wheels)[0])-2*halfWheelbase*std::sin(pitch))>1e-5)return false;
    const double body=((*wheels)[0]+(*wheels)[1])*.5-wheelRadius;
    const auto heights=ground(q,p,yaw,water);if(!heights)return false;
    if(heights->y+skin>body+bellyClearance)return false;
    p.y=std::max({body,heights->x+skin,heights->y+skin,heights->z+skin});
    if(!clear(q,p,yaw))return false;
    state_={};state_.feet=p;state_.yaw=std::remainder(yaw,2*std::numbers::pi);
    state_.pitch=pitch;state_.axleHeight=*wheels;state_.groundOffset=body-p.y;
    accumulator_=0;available_=true;return true;
}
// Two equal wheel masses give independent vertical contact constraints. Their
// midpoint and separation define a rigid frame; unsupported wheels fall under
// gravity, instead of forcing the whole bike onto the highest middle probe.
bool BuilderMotorbike::terrainPose(const AdventureSpatialQueries& q,glm::dvec3 p,double yaw,double water,State& out) const noexcept {
    std::array<double,2> predicted{},velocity{};
    for(size_t i=0;i<2;++i) {
        velocity[i]=std::max(-45.,state_.axleVelocity[i]-gravity*fixedStep);
        predicted[i]=state_.axleHeight[i]+velocity[i]*fixedStep;
    }
    double low=-maximumPitch,high=maximumPitch,pitch=state_.pitch;
    std::array<double,2> axes{},support{};std::array<bool,2> contacts{};
    // Solve pitch against the projected tire footprints. Simple iteration can
    // oscillate at a ledge, alternating between ground and empty space. Bracket
    // the angle instead, then conservatively project both tires out of terrain.
    for(int iteration=0;iteration<24;++iteration) {
        const auto sampled=wheelSupport(q,p,yaw,pitch,water);if(!sampled)return false;
        support=*sampled;axes=predicted;
        for(size_t i=0;i<2;++i) {
            // Keep grip across tiny stud ripples, but release genuine drops.
            if(axes[i]<=support[i]+(state_.wheelGrounded[i]?.12:0.))axes[i]=support[i];
        }
        const double error=(axes[1]-axes[0])-2*halfWheelbase*std::sin(pitch);
        if(std::abs(error)<1e-6||high-low<1e-7)break;
        if(error>0)low=pitch;else high=pitch;
        pitch=(low+high)*.5;
    }
    const auto finalSupport=wheelSupport(q,p,yaw,pitch,water);if(!finalSupport)return false;
    support=*finalSupport;
    const double rise=halfWheelbase*std::sin(pitch);
    // At the pitch limit (or a discontinuous ledge), keep the lower end in the
    // air rather than allowing the higher tire to penetrate the platform.
    const double midpoint=std::max({axes[0]+rise,axes[1]-rise,support[0]+rise,support[1]-rise});
    axes={midpoint-rise,midpoint+rise};
    for(size_t i=0;i<2;++i)contacts[i]=axes[i]-support[i]<1e-5;
    double body=midpoint-wheelRadius;
    const auto heights=ground(q,p,yaw,water);if(!heights)return false;
    // Let the underside rest on a ledge while the front wheel rolls off it.
    // Rejecting this contact would freeze the bike halfway over the edge.
    const double bellyLift=std::max(0.,heights->y+skin-bellyClearance-body);
    body+=bellyLift;
    for(size_t i=0;i<2;++i) {
        axes[i]+=bellyLift;
        contacts[i]=axes[i]-support[i]<1e-5;
    }
    p.y=std::max({body,heights->x+skin,heights->y+skin,heights->z+skin});
    if(p.y>state_.feet.y+stepUp)return false;
    bool accepted=clear(q,p,yaw)&&sweep(q,state_.feet,state_.yaw,p,yaw);
    if(!accepted&&p.y>=state_.feet.y&&p.y-state_.feet.y<=stepUp) {
        auto raised=state_.feet;raised.y=p.y;
        accepted=clear(q,p,yaw)&&sweep(q,state_.feet,state_.yaw,raised,state_.yaw)
            &&sweep(q,raised,state_.yaw,p,yaw);
    }
    if(!accepted&&p.y<state_.feet.y) {
        auto across=p;across.y=state_.feet.y;
        accepted=clear(q,p,yaw)&&sweep(q,state_.feet,state_.yaw,across,yaw)
            &&sweep(q,across,yaw,p,yaw);
    }
    if(!accepted&&clear(q,p,yaw)) {
        // A turning guard can cross a stud crown higher than either endpoint.
        // Lift only this conservative guard over that small intermediate rise;
        // the visible frame still follows the independently solved tire pose.
        // All three segments are swept, including the descent and overhead.
        auto raised=state_.feet;
        raised.y=std::max(state_.feet.y,p.y)+.32;
        auto across=p;across.y=raised.y;
        if(raised.y-state_.feet.y<=stepUp)
            accepted=sweep(q,state_.feet,state_.yaw,raised,state_.yaw)
                &&sweep(q,raised,state_.yaw,across,yaw)&&sweep(q,across,yaw,p,yaw);
    }
    if(!accepted)return false;
    out=state_;out.feet=p;out.yaw=yaw;out.pitch=pitch;out.groundOffset=body-p.y;
    out.axleHeight=axes;out.wheelGrounded=contacts;out.grounded=contacts[0]||contacts[1];
    for(size_t i=0;i<2;++i) {
        // Filter stud-scale impulses while carrying the uphill tangent off a
        // crest. Brakes/throttle cannot erase airborne vertical momentum.
        const double tangent=std::clamp((axes[i]-state_.axleHeight[i])/fixedStep,-16.,16.);
        out.axleVelocity[i]=contacts[i]?state_.axleVelocity[i]+(tangent-state_.axleVelocity[i])*(1-std::exp(-8*fixedStep)):velocity[i];
        if(!contacts[i]&&axes[i]>predicted[i]+1e-5)
            out.axleVelocity[i]=std::max(out.axleVelocity[i],0.);
    }
    out.verticalSpeed=(out.axleVelocity[0]+out.axleVelocity[1])*.5;
    return true;
}
void BuilderMotorbike::advance(const AdventureSpatialQueries& q,double seconds,Input input,double water) noexcept {
    if(!available_||!std::isfinite(seconds)||seconds<0||!std::isfinite(input.throttle)||!std::isfinite(input.steer)||!std::isfinite(water))return;
    input.throttle=std::clamp(input.throttle,-1.,1.);input.steer=std::clamp(input.steer,-1.,1.);
    accumulator_+=std::min(seconds,.25);
    for(int i=0;i<15&&accumulator_+1e-12>=fixedStep;++i){accumulator_-=fixedStep;step(q,input,water);}
}
void BuilderMotorbike::step(const AdventureSpatialQueries& q,Input input,double water) noexcept {
    const auto before=state_;
    const double targetSteer=input.steer*.40/(1+std::abs(state_.speed)/20.);
    const double steering=state_.steering+(targetSteer-state_.steering)*(1-std::exp(-10*fixedStep));
    double speed=state_.speed;
    if(state_.grounded) {
        double acceleration=0;
        if(input.brake)acceleration=-std::copysign(std::min(std::abs(speed)/fixedStep,22.),speed);
        else {
            const double grade=-gravity*std::sin(state_.pitch);
            if(input.throttle*speed<-.05)acceleration=input.throttle*18.+grade;
            else if(std::abs(input.throttle)>.01)acceleration=input.throttle*(input.throttle>0?10.:6.)+grade;
            else {
                const double rolling=3.;
                acceleration=grade-std::copysign(rolling,std::abs(speed)>.01?speed:grade);
                if(std::abs(speed)<.01&&std::abs(grade)<=rolling)acceleration=0;
                if(speed*(speed+acceleration*fixedStep)<0&&std::abs(grade)<=rolling)acceleration=-speed/fixedStep;
            }
        }
        speed=std::clamp(speed+acceleration*fixedStep,-4.,22.);
    }
    const double desiredTurn=speed*std::tan(steering)/(2*halfWheelbase);
    // A momentarily unloaded front tire must not switch yaw velocity to zero.
    // Ground contact steers; in the air, retain and gently damp angular momentum.
    double turn=state_.wheelGrounded[1]
        ?state_.yawRate+(desiredTurn-state_.yawRate)*(1-std::exp(-12*fixedStep))
        :state_.yawRate*std::exp(-.5*fixedStep);
    if(state_.grounded&&std::abs(speed)<.05)turn=0;
    const double yaw=std::remainder(state_.yaw+turn*fixedStep,2*std::numbers::pi);
    // Midpoint heading avoids introducing a sideways step during turns.
    const auto next=state_.feet+forward(state_.yaw+turn*fixedStep*.5)*(speed*fixedStep);
    State accepted;
    if(!terrainPose(q,next,yaw,water,accepted)) {
        speed=0;turn=0;
        // A blocked horizontal move must still settle vertically. The former
        // early returns could freeze a falling bike at a shore or steep edge.
        if(!terrainPose(q,state_.feet,state_.yaw,water,accepted))accepted=state_;
    }
    state_=accepted;state_.speed=speed;state_.yawRate=turn;state_.steering=steering;
    const double lean=state_.grounded?std::clamp(std::atan(turn*speed/gravity),-.30,.30):0.;
    state_.lean+=(lean-state_.lean)*(1-std::exp(-7*fixedStep));
    const double distance=glm::length(glm::dvec2(state_.feet.x-before.feet.x,state_.feet.z-before.feet.z));
    state_.spin=std::remainder(state_.spin+std::copysign(distance,speed)/(wheelRadius*std::max(.8,std::cos(state_.pitch))),2*std::numbers::pi);
    ++state_.tick;
}
std::optional<glm::dvec3> BuilderMotorbike::dismount(const AdventureSpatialQueries& q,double water) const noexcept {
    if(!available_||!state_.wheelGrounded[0]||!state_.wheelGrounded[1]||std::abs(state_.speed)>2)return {};
    const auto right=glm::dvec3(std::cos(state_.yaw),0,-std::sin(state_.yaw));
    for(double side:{1.,-1.})for(double distance:{2.5,3.25}) {
        auto p=state_.feet+right*(side*distance);
        const double support=q.supportHeight({p.x,p.z},1.12,p.y+.85);
        if(!std::isfinite(support)||support<water+.05||std::abs(support-p.y)>1.2)continue;
        p.y=support+.005;
        if(!q.clearCapsule(p,1.12,4.76))continue;
        // Do not dismount through a wall to an otherwise clear landing spot.
        auto start=state_.feet;start.y=std::max(start.y,p.y);
        auto end=p;end.y=start.y;
        auto hit=q.sweepCapsule(start,end,1.12,4.76);
        if(hit.complete&&!hit.hit&&!hit.startOverlapped)return p;
    }
    return {};
}
}

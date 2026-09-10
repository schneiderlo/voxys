#include "game/expedition/cove_player.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::game::expedition {
namespace {
constexpr double skin = .005, stepHeight = .36;
bool finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
glm::dvec3 metres(construction::GridPosition p) { return glm::dvec3(p.x, p.y, p.z) * .02; }
}

bool CovePlayer::initialize(const assets::LoadedAssetFixture& scene, Ground ground, std::string& error, std::span<const uint32_t> boatSlots) {
    if (!scene.registry.navigation || !ground) { error = "Cove navigation and ground are required"; return false; }
    CovePlayer candidate;
    candidate.navigation_ = *scene.registry.navigation;
    candidate.ground_ = std::move(ground);
    for (size_t index = 0; index < scene.registry.placements.size(); ++index) {
        const auto& cargo=candidate.navigation_.cargoPlacements;
        if(std::find(cargo.begin(),cargo.end(),index)!=cargo.end()) continue;
        const auto& placement = scene.registry.placements[index];
        const auto& members = candidate.navigation_.boatPlacements;
        const bool boat = std::find(members.begin(), members.end(), index) != members.end();
        // A dismantled craft slot is absent, never newly classified as scenery.
        if(!boat && std::find(boatSlots.begin(),boatSlots.end(),index)!=boatSlots.end())continue;
        if (placement.prototype || placement.bundleIndex >= scene.bundles.size()) {
            error = "Cove collision requires admitted authored parts"; return false;
        }
        for (const auto& proxy : scene.bundles[placement.bundleIndex]->sidecar().part.collision) {
            const auto local = construction::boxBounds(proxy);
            const auto bounds = local ? construction::transformBounds(placement.placement, *local) : std::nullopt;
            if (!bounds || candidate.boxes_.size() >= 1024) { error = "Cove collision bounds/capacity"; return false; }
            candidate.boxes_.push_back({metres(bounds->minimum), metres(bounds->maximum), boat,static_cast<uint32_t>(index)});
        }
    }
    for (const auto& [point,name] : std::array<std::pair<glm::dvec3,const char*>,4>{{
            {candidate.navigation_.spawn,"Dock spawn"},{candidate.navigation_.dockBoarding,"Dock boarding"},
            {candidate.navigation_.boatBoarding,"Boat boarding"},{candidate.navigation_.helmStanding,"Helm standing"}}}) {
        auto p=point;p.y+=skin;
        if (!finite(p) || !candidate.clear(p) || !candidate.supported(p)) {
            error=std::string(name)+" needs clear standing room and support";return false;
        }
    }
    if (!candidate.boatSupport(candidate.navigation_.boatBoarding)
        || !candidate.boatSupport(candidate.navigation_.helmStanding)
        || candidate.boatSupport(candidate.navigation_.spawn)
        || candidate.boatSupport(candidate.navigation_.dockBoarding)) {
        error = "Cove boarding/helm positions must match dock and boat collision ownership"; return false;
    }
    const auto look = candidate.navigation_.lookTarget - candidate.navigation_.spawn;
    if (!finite(look) || std::hypot(look.x, look.z) < .01) { error = "Cove starting view is invalid"; return false; }
    candidate.reset();
    *this = std::move(candidate);
    error.clear();
    return true;
}

bool CovePlayer::setStaticObstacles(std::span<const StaticObstacle> boxes) noexcept {
    if(boxes.size()>staticObstacles_.size())return false;
    for(const auto& b:boxes)if(!finite(b.minimum)||!finite(b.maximum)
        ||glm::any(glm::greaterThanEqual(b.minimum,b.maximum)))return false;
    std::copy(boxes.begin(),boxes.end(),staticObstacles_.begin());staticObstacleCount_=boxes.size();return true;
}

namespace {
bool rigidPose(const glm::dmat4& pose) noexcept {
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(pose[c][r]))return false;
    if(pose[0][3]!=0||pose[1][3]!=0||pose[2][3]!=0||pose[3][3]!=1)return false;
    const auto rotation=glm::dmat3(pose);const auto unit=glm::transpose(rotation)*rotation;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)if(std::abs(unit[c][r]-(c==r?1.:0.))>1e-5)return false;
    return std::abs(glm::determinant(rotation)-1)<1e-5;
}
}

bool CovePlayer::bindBoatRoots(std::span<const BoatRoot> roots,std::span<const BoatPart> parts,
    construction::DurableId helmRoot) noexcept {
    if(roots.empty()||roots.size()>roots_.size()||parts.empty()||parts.size()>assets::kMaximumFixturePlacements)return false;
    const auto find=[&](construction::DurableId key)->std::optional<uint8_t>{
        for(size_t i=0;i<roots.size();++i)if(roots[i].key==key)return static_cast<uint8_t>(i);
        return {};
    };
    for(size_t i=0;i<roots.size();++i) {
        if(!construction::isValid(roots[i].key)||!rigidPose(roots[i].sceneFromBoat))return false;
        for(size_t j=0;j<i;++j)if(roots[j].key==roots[i].key)return false;
    }
    const auto helm=find(helmRoot);if(!helm)return false;
    std::array<const BoatPart*,assets::kMaximumFixturePlacements> slots{};std::array<bool,assets::kMaximumFixturePlacements> used{};
    for(const auto& part:parts) {
        const auto root=find(part.root);
        if(part.placement>=slots.size()||slots[part.placement]||!root||!construction::isValid(part.part)
            ||part.part.world!=helmRoot.world)return false;
        for(const auto& previous:slots)if(previous&&previous->part==part.part)return false;
        slots[part.placement]=&part;used[*root]=true;
    }
    for(size_t i=0;i<roots.size();++i)if(!used[i]||roots[i].key.world!=helmRoot.world)return false;
    for(const auto& b:boxes_)if(b.boat&&(b.placement>=slots.size()||!slots[b.placement]))return false;
    for(const auto& part:parts)if(std::none_of(boxes_.begin(),boxes_.end(),[&](const auto& b){return b.boat&&b.placement==part.placement;}))return false;
    const auto support=[&](glm::dvec3 point)->std::optional<uint8_t>{
        for(const auto& b:boxes_)if(b.boat&&std::abs(point.y-b.max.y)<=.02
            &&point.x+radius>b.min.x&&point.x-radius<b.max.x&&point.z+radius>b.min.z&&point.z-radius<b.max.z)
            return find(slots[b.placement]->root);
        return {};
    };
    const auto boarding=support(navigation_.boatBoarding);
    if(!boarding)return false;
    std::optional<uint8_t> rider=helm;
    if(onBoat_) {
        const auto previous=state().root;
        rider=rootCount_?find(previous):support(feet_);
        if(!rider)return false;
    }
    for(auto& b:boxes_)if(b.boat){b.root=*find(slots[b.placement]->root);b.part=slots[b.placement]->part;}
    std::copy(roots.begin(),roots.end(),roots_.begin());rootCount_=roots.size();
    helmRoot_=*helm;boardingRoot_=*boarding;riderRoot_=*rider;
    sceneFromBoat_=roots_[helmRoot_].sceneFromBoat;dynamicBoat_=true;return true;
}

bool CovePlayer::setBoatRootTransform(construction::DurableId key,const glm::dmat4& pose) noexcept {
    if(!rigidPose(pose))return false;
    for(size_t i=0;i<rootCount_;++i)if(roots_[i].key==key) {
        roots_[i].sceneFromBoat=pose;if(i==helmRoot_)sceneFromBoat_=pose;return true;
    }
    return false;
}

void CovePlayer::setBoatTransform(const glm::dmat4& transform) noexcept {
    sceneFromBoat_=transform;dynamicBoat_=true;
    if(rootCount_)roots_[helmRoot_].sceneFromBoat=transform;
}

bool CovePlayer::collisionApplies(const Box& b) const noexcept {
    if(!dynamicBoat_)return true;
    if(b.boat!=onBoat_)return false;
    return !onBoat_||!rootCount_||b.root==riderRoot_;
}

construction::DurableId CovePlayer::supportingPart() const noexcept {
    if(!onBoat_||!rootCount_||(mode_!=Mode::Walking&&mode_!=Mode::Helm))return {};
    for(const auto& b:boxes_)if(b.boat&&b.root==riderRoot_&&std::abs(feet_.y-b.max.y)<=.02
        &&feet_.x+radius>b.min.x&&feet_.x-radius<b.max.x&&feet_.z+radius>b.min.z&&feet_.z-radius<b.max.z)return b.part;
    return {};
}

bool CovePlayer::clear(glm::dvec3 p) const noexcept {
    const auto lo = p + glm::dvec3(-radius, 0, -radius);
    const auto hi = p + glm::dvec3(radius, height, radius);
    for (const auto& b : boxes_) {
        if (!collisionApplies(b)) continue;
        if (lo.x < b.max.x - 1e-8 && hi.x > b.min.x + 1e-8 &&
            lo.y < b.max.y - 1e-8 && hi.y > b.min.y + 1e-8 &&
            lo.z < b.max.z - 1e-8 && hi.z > b.min.z + 1e-8) return false;
    }
    if(!onBoat_)for(size_t i=0;i<staticObstacleCount_;++i){
        const auto& b=staticObstacles_[i];
        if(glm::all(glm::lessThan(lo,b.maximum-glm::dvec3(1e-8)))
            &&glm::all(glm::greaterThan(hi,b.minimum+glm::dvec3(1e-8))))return false;
    }
    return true;
}

double CovePlayer::groundHeight(glm::dvec3 p) const {
    if (onBoat_) return -std::numeric_limits<double>::infinity();
    double top = -std::numeric_limits<double>::infinity();
    for (double x : {-radius, 0.0, radius}) for (double z : {-radius, 0.0, radius}) {
        const double h = ground_(p.x + x, p.z + z);
        if (std::isfinite(h)) top = std::max(top, h);
    }
    return top;
}

bool CovePlayer::supported(glm::dvec3 p) const {
    if (std::abs(p.y - groundHeight(p)) <= .02) return true;
    for (const auto& b : boxes_) {
        if (!collisionApplies(b)) continue;
        if (std::abs(p.y - b.max.y) <= .02 && p.x + radius > b.min.x && p.x - radius < b.max.x
            && p.z + radius > b.min.z && p.z - radius < b.max.z) return true;
    }
    if(!onBoat_)for(size_t i=0;i<staticObstacleCount_;++i){
        const auto& b=staticObstacles_[i];
        if(std::abs(p.y-b.maximum.y)<=.02&&p.x+radius>b.minimum.x&&p.x-radius<b.maximum.x
            &&p.z+radius>b.minimum.z&&p.z-radius<b.maximum.z)return true;
    }
    return false;
}

bool CovePlayer::boatSupport(glm::dvec3 p) const noexcept {
    if(dynamicBoat_&&!onBoat_)return false;
    for (const auto& b : boxes_) if (b.boat && (!rootCount_||b.root==riderRoot_) && std::abs(p.y - b.max.y) <= .02
        && p.x + radius > b.min.x && p.x - radius < b.max.x
        && p.z + radius > b.min.z && p.z - radius < b.max.z) return true;
    return false;
}

double CovePlayer::moveAxis(glm::dvec3 p, int axis, double distance) const noexcept {
    const auto lo = p + glm::dvec3(-radius, 0, -radius);
    const auto hi = p + glm::dvec3(radius, height, radius);
    for (const auto& b : boxes_) {
        if (!collisionApplies(b)) continue;
        bool overlap = true;
        for (int other = 0; other < 3; ++other) if (other != axis)
            overlap = overlap && lo[other] < b.max[other] - 1e-8 && hi[other] > b.min[other] + 1e-8;
        if (!overlap) continue;
        if (distance > 0 && hi[axis] <= b.min[axis] + skin)
            distance = std::min(distance, std::max(0.0, b.min[axis] - hi[axis] - skin));
        if (distance < 0 && lo[axis] >= b.max[axis] - skin)
            distance = std::max(distance, std::min(0.0, b.max[axis] - lo[axis] + skin));
    }
    if(!onBoat_)for(size_t i=0;i<staticObstacleCount_;++i){
        const auto& b=staticObstacles_[i];bool overlap=true;
        for(int other=0;other<3;++other)if(other!=axis)
            overlap=overlap&&lo[other]<b.maximum[other]-1e-8&&hi[other]>b.minimum[other]+1e-8;
        if(!overlap)continue;
        if(distance>0&&hi[axis]<=b.minimum[axis]+skin)
            distance=std::min(distance,std::max(0.0,b.minimum[axis]-hi[axis]-skin));
        if(distance<0&&lo[axis]>=b.maximum[axis]-skin)
            distance=std::max(distance,std::min(0.0,b.maximum[axis]-lo[axis]+skin));
    }
    return distance;
}

void CovePlayer::reset() noexcept {
    feet_ = navigation_.spawn + glm::dvec3(0, skin, 0);
    verticalSpeed_ = accumulator_ = 0;
    tick_ = interactions_ = 0;
    mode_ = Mode::Walking;
    onBoat_ = pendingJump_ = pendingInteraction_ = false;
}

CovePlayer::State CovePlayer::state() const noexcept {
    return {feet_,verticalSpeed_,tick_,interactions_,mode_,onBoat_,onBoat_&&rootCount_?roots_[riderRoot_].key:construction::DurableId{}};
}

bool CovePlayer::restore(const State& saved) {
    if (!ground_ || !finite(saved.feet) || glm::any(glm::greaterThan(glm::abs(saved.feet),glm::dvec3(1'000'000)))
        || !std::isfinite(saved.verticalSpeed) || saved.verticalSpeed < -30 || saved.verticalSpeed > 6
        || saved.mode<Mode::Walking || saved.mode>Mode::Helm
        || (saved.onBoat && !dynamicBoat_)
        || (saved.mode!=Mode::Airborne && saved.verticalSpeed!=0)
        || (saved.mode==Mode::Helm && (!saved.onBoat
            || glm::length(saved.feet-navigation_.helmStanding-glm::dvec3(0,skin,0))>1e-6))
        || (saved.mode==Mode::Swimming && (saved.onBoat || std::abs(saved.feet.y+.8)>1e-6))) return false;
    // Load is outside the simulation publication boundary. The bounded copy
    // permits collision/terrain validation without exposing a partially loaded
    // player or losing the existing one if a ground callback throws.
    CovePlayer candidate=*this;
    if(saved.onBoat&&rootCount_) {
        const auto root=std::find_if(roots_.begin(),roots_.begin()+static_cast<std::ptrdiff_t>(rootCount_),[&](const auto& r){return r.key==saved.root;});
        if(root==roots_.begin()+static_cast<std::ptrdiff_t>(rootCount_))return false;
        candidate.riderRoot_=static_cast<uint8_t>(root-roots_.begin());
        if(saved.mode==Mode::Helm&&candidate.riderRoot_!=helmRoot_)return false;
    } else if(saved.root!=construction::DurableId{})return false;
    candidate.feet_=saved.feet;candidate.verticalSpeed_=saved.verticalSpeed;
    candidate.tick_=saved.tick;candidate.interactions_=saved.interactions;
    candidate.mode_=saved.mode;candidate.onBoat_=saved.onBoat;
    candidate.accumulator_=0;candidate.pendingJump_=candidate.pendingInteraction_=false;
    if (!candidate.clear(saved.feet) || saved.feet.y+skin<candidate.groundHeight(saved.feet)
        || ((saved.mode==Mode::Walking || saved.mode==Mode::Helm) && !candidate.supported(saved.feet))) return false;
    *this=std::move(candidate);
    return true;
}

CovePlayer::Interaction CovePlayer::interaction() const noexcept {
    if (mode_ == Mode::Helm) return Interaction::LeaveHelm;
    if (mode_ != Mode::Walking) return Interaction::None;
    if (onBoat_) {
        if ((!rootCount_||riderRoot_==helmRoot_)&&glm::length(feet_ - navigation_.helmStanding) < .85) return Interaction::UseHelm;
        if ((!rootCount_||riderRoot_==boardingRoot_)&&glm::length(feet_ - navigation_.boatBoarding) < .9
            && glm::length(feet() - navigation_.dockBoarding) < 3.2) return Interaction::ReturnToDock;
    } else if (glm::length(feet_ - navigation_.dockBoarding) < .9
        && glm::length(glm::dvec3(boardingTransform()*glm::dvec4(navigation_.boatBoarding,1))-feet_) < 3.2)
        return Interaction::Board;
    return Interaction::None;
}

bool CovePlayer::requestInteraction() noexcept {
    if (pendingInteraction_ || interaction() == Interaction::None) return false;
    pendingInteraction_ = true;
    return true;
}

void CovePlayer::advance(double seconds, Input input) {
    if (!ground_ || !std::isfinite(seconds) || seconds < 0 || !std::isfinite(input.movement.x)
        || !std::isfinite(input.movement.y)) return;
    const double length = glm::length(input.movement);
    if (length > 1) input.movement /= length;
    pendingJump_ = pendingJump_ || input.jump;
    accumulator_ += std::min(seconds, .25); // Bounded catch-up for this local character only.
    while (accumulator_ + 1e-12 >= fixedStep) {
        input.jump = pendingJump_; pendingJump_ = false;
        step(input);
        accumulator_ -= fixedStep;
        if(tick_!=UINT64_MAX) ++tick_;
    }
}

void CovePlayer::step(Input input) {
    if (onBoat_) {
        const auto local=glm::transpose(glm::dmat3(riderTransform()))*glm::dvec3(input.movement.x,0,input.movement.y);
        input.movement={local.x,local.z};
    }
    if (pendingInteraction_) {
        pendingInteraction_ = false;
        const auto action = interaction(); // Recheck at execution, never trust stale UI.
        auto target = feet_;
        if (action == Interaction::Board) target = navigation_.boatBoarding + glm::dvec3(0,skin,0);
        if (action == Interaction::ReturnToDock) target = navigation_.dockBoarding + glm::dvec3(0,skin,0);
        if (action == Interaction::UseHelm) target = navigation_.helmStanding + glm::dvec3(0,skin,0);
        const bool previousBoat=onBoat_;const auto previousRoot=riderRoot_;
        if (action == Interaction::Board) {onBoat_=true;riderRoot_=boardingRoot_;}
        if (action == Interaction::ReturnToDock) onBoat_=false;
        if (action != Interaction::None && clear(target) && supported(target)) {
            feet_ = target; verticalSpeed_ = 0;if(interactions_!=UINT64_MAX) ++interactions_;
            if (action == Interaction::Board) onBoat_ = true;
            if (action == Interaction::ReturnToDock) onBoat_ = false;
            mode_ = action == Interaction::UseHelm ? Mode::Helm : Mode::Walking;
            return;
        }
        onBoat_=previousBoat;riderRoot_=previousRoot;
    }
    if (mode_ == Mode::Helm) return;
    const bool swimming = mode_ == Mode::Swimming;
    if (input.jump && mode_ == Mode::Walking) { verticalSpeed_ = 6; mode_ = Mode::Airborne; }
    for (int axis : {0, 2}) {
        const double desired = input.movement[axis == 0 ? 0 : 1] * (swimming ? 2.0 : 3.6) * fixedStep;
        double distance = moveAxis(feet_, axis, desired);
        if (std::abs(distance - desired) > 1e-8 && mode_ == Mode::Walking) {
            auto raised = feet_;
            raised.y += stepHeight;
            if (clear(raised) && std::abs(moveAxis(raised, axis, desired) - desired) < 1e-8) {
                raised[axis] += desired;
                const double drop = moveAxis(raised, 1, -stepHeight);
                raised.y += drop;
                if (clear(raised) && supported(raised)) { feet_ = raised; continue; }
            }
        }
        auto next = feet_; next[axis] += distance;
        const double ground = groundHeight(next);
        if (ground > next.y + stepHeight) continue;
        if (ground > next.y && mode_ == Mode::Walking) next.y = ground + skin;
        if (clear(next)) feet_ = next;
    }
    verticalSpeed_ = std::max(-30.0, verticalSpeed_ - 18 * fixedStep);
    const double desiredY = verticalSpeed_ * fixedStep;
    const double actualY = moveAxis(feet_, 1, desiredY);
    feet_.y += actualY;
    bool grounded = desiredY < 0 && actualY > desiredY + 1e-8;
    const double ground = groundHeight(feet_);
    if (feet_.y <= ground + skin && verticalSpeed_ <= 0) { feet_.y = ground + skin; grounded = true; }
    if (std::abs(actualY - desiredY) > 1e-8 || grounded) verticalSpeed_ = 0;
    mode_ = grounded ? Mode::Walking : Mode::Airborne;
    if (grounded) onBoat_ = boatSupport(feet_);
    // Water datum is local zero. Surface swimming avoids an unrecoverable fall;
    // animated-wave coupling and moving-platform dynamics belong to ACT/SIM.
    if (feet().y < -.8) {
        feet_=feet();
        feet_.y = -.8; verticalSpeed_ = 0; mode_ = Mode::Swimming; onBoat_ = false;
    }
}

const char* CovePlayer::modeName(Mode mode) noexcept {
    switch (mode) { case Mode::Walking: return "walking"; case Mode::Airborne: return "airborne";
        case Mode::Swimming: return "swimming"; case Mode::Helm: return "helm"; }
    return "unknown";
}
const char* CovePlayer::interactionName(Interaction action) noexcept {
    switch (action) { case Interaction::None: return "none"; case Interaction::Board: return "board";
        case Interaction::ReturnToDock: return "dock"; case Interaction::UseHelm: return "helm";
        case Interaction::LeaveHelm: return "leave-helm"; }
    return "none";
}
} // namespace voxy::game::expedition

#include "game/adventure/cannon_physics_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace voxy::game::adventure {
namespace {
using Solid = AdventureSpatialQueries::Solid;
constexpr double tick = physics::kAuthoredShapeTickMetres;
bool finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool retry(physics::ShapeResourceError e) {
    return e == physics::ShapeResourceError::Busy || e == physics::ShapeResourceError::NotReady;
}
bool joined(const physics::PhysicsTickFrontier& f) {
    return f.supported && !f.failed && f.incarnation && f.completed == f.scheduled
        && f.completed == f.encoded && f.completed == f.submitted;
}
}

physics::ShapeResourceLimits CannonPhysicsScene::resourceLimits() noexcept {
    physics::ShapeResourceLimits limits;
    // Two fully populated scenery revisions may coexist while a submitted
    // replacement drains. The remainder is reserved for both wall revisions;
    // do not enlarge the engine-wide default pool or omit accepted scenery.
    limits.cpu.cells=32768;
    limits.cpu.faces=196608;
    limits.cpu.nodes=65536;
    limits.cpu.bytes=24*1024*1024;
    return limits;
}

std::optional<CannonPhysicsScene::Packet> CannonPhysicsScene::compile(
    std::span<const Solid> solids, glm::dvec3 sceneOrigin, uint64_t revision, std::string& error) {
    error.clear();
    if (!revision || !finite(sceneOrigin)
        || glm::any(glm::greaterThan(glm::abs(sceneOrigin),glm::dvec3(1e10)))
        || solids.size() > AdventureSpatialQueries::maximumSolids) {
        error = "Invalid cannon collision revision, origin or solid capacity."; return {};
    }
    for (const auto& s : solids) {
        if (!finite(s.minimum) || !finite(s.maximum)
            || glm::any(glm::lessThanEqual(s.maximum, s.minimum))
            || glm::any(glm::greaterThan(glm::abs(s.minimum-sceneOrigin), glm::dvec3(4096)))
            || glm::any(glm::greaterThan(glm::abs(s.maximum-sceneOrigin), glm::dvec3(4096)))) {
            error = "Cannon collision region contains an invalid or distant solid."; return {};
        }
    }
    try {
        Packet result; result.revision = revision; result.solidCount = solids.size();
        std::vector<size_t> order(solids.size()); std::iota(order.begin(), order.end(), 0);
        const auto build = [&](auto&& self, size_t first, size_t last) -> bool {
            if (first == last) return true;
            glm::dvec3 lo = solids[order[first]].minimum, hi = solids[order[first]].maximum;
            for (size_t i=first+1; i<last; ++i) {
                lo = glm::min(lo, solids[order[i]].minimum); hi = glm::max(hi, solids[order[i]].maximum);
            }
            const auto extent = hi-lo;
            int axis = extent.y > extent.x ? 1 : 0; if (extent.z > extent[axis]) axis=2;
            if (last-first > maximumBoxesPerPartition || extent[axis] > 96) {
                if (last-first == 1) { error="A cannon collider is too large for a bounded partition."; return false; }
                std::stable_sort(order.begin()+static_cast<std::ptrdiff_t>(first), order.begin()+static_cast<std::ptrdiff_t>(last), [&](size_t a,size_t b) {
                    const double ca=solids[a].minimum[axis]+solids[a].maximum[axis];
                    const double cb=solids[b].minimum[axis]+solids[b].maximum[axis];
                    return ca == cb ? a < b : ca < cb;
                });
                const size_t mid=first+(last-first)/2;
                return self(self,first,mid) && self(self,mid,last);
            }
            if (result.partitions.size() == maximumPartitions) {
                error="Cannon collision region exceeds the static partition budget."; return false;
            }
            // Anchor in the requested local frame, retaining precision in distant sectors.
            const glm::dvec3 origin=sceneOrigin+glm::round(((lo+hi)*.5-sceneOrigin)/tick)*tick;
            std::vector<geometry::UnionBox> boxes; boxes.reserve(last-first);
            for (size_t i=first; i<last; ++i) {
                const size_t index=order[i];
                const auto minimum=glm::floor((solids[index].minimum-origin)/tick+glm::dvec3(1e-7));
                const auto maximum=glm::ceil((solids[index].maximum-origin)/tick-glm::dvec3(1e-7));
                boxes.push_back({{{int32_t(minimum.x),int32_t(minimum.y),int32_t(minimum.z)},
                    {int32_t(maximum.x),int32_t(maximum.y),int32_t(maximum.z)}},uint32_t(index+1)});
            }
            geometry::BoxUnionIssue unionIssue;
            const auto volume=geometry::BoxUnion::compile(boxes,unionIssue);
            if (!volume) { error="Cannon collision partition failed exterior compilation ("+std::to_string(int(unionIssue.error))+")."; return false; }
            // Only the frame is used: these bodies are explicitly static. A
            // finite reference box mass avoids fabricated physical LEGO density.
            const auto center=(lo+hi)*.5-origin;
            physics::RigidMassInput mass{1,{center.x,center.y,center.z},
                {(extent.y*extent.y+extent.z*extent.z)/12,0,0,
                 0,(extent.x*extent.x+extent.z*extent.z)/12,0,
                 0,0,(extent.x*extent.x+extent.y*extent.y)/12}};
            physics::AuthoredShapeIssue shapeIssue;
            auto shape=physics::AuthoredShape::prepare(*volume,mass,shapeIssue);
            if (!shape) { error="Cannon collision partition has an unrepresentable authored frame."; return false; }
            const auto cost=shape->cost();
            result.cost.cells+=cost.cells; result.cost.faces+=cost.faces; result.cost.nodes+=cost.nodes; result.cost.bytes+=cost.bytes;
            if (result.cost.cells>maximumCells || result.cost.faces>maximumFaces
                || result.cost.nodes>maximumNodes || result.cost.bytes>8*1024*1024) {
                error="Cannon collision region exceeds its complete shape resource budget (cells="+std::to_string(result.cost.cells)+", faces="+std::to_string(result.cost.faces)+", nodes="+std::to_string(result.cost.nodes)+", bytes="+std::to_string(result.cost.bytes)+")."; return false;
            }
            result.partitions.push_back({origin,std::move(*shape)}); return true;
        };
        if (!build(build,0,order.size())) return {};
        return result;
    } catch (const std::bad_alloc&) { error="Cannon collision preparation ran out of memory."; return {}; }
}

bool CannonPhysicsScene::prepare(std::span<const Solid> solids, glm::dvec3 sceneOrigin,
    uint64_t revision, std::string& error) {
    error.clear();
    if (clearing_ || failed_ || executionTick_ || packet_) {
        error="Cannon collision is still changing or retiring."; return false;
    }
    if (!revision) { error="Cannon collision revision must be nonzero."; return false; }
    if (revision<=acceptedRevision_) {
        if (revision==acceptedRevision_) return true;
        error="Stale cannon collision revision refused."; return false;
    }
    auto prepared=compile(solids,sceneOrigin,revision,error); if (!prepared) return false;
    return prepareAuthored(std::move(*prepared),error);
}

bool CannonPhysicsScene::prepareAuthored(Packet prepared, std::string& error) {
    error.clear();
    if(clearing_||failed_||executionTick_||packet_) {
        error="Authored scene is still changing or retiring.";return false;
    }
    if(!prepared.revision||prepared.revision<=acceptedRevision_
        ||prepared.partitions.size()>maximumPartitions) {
        error="Invalid authored scene revision or partition budget.";return false;
    }
    if(prepared.consumeBody&&(!prepared.consumeBody->valid()
        ||std::any_of(live_.begin(),live_.end(),[&](const auto& item){return item.body==*prepared.consumeBody;}))) {
        error="Projectile retirement must name a separate live body.";return false;
    }
    physics::ShapeResourceCost cost{};
    for(const auto& part:prepared.partitions) {
        physics::AuthoredFrameError frameError;
        if(!finite(part.origin)||glm::any(glm::greaterThan(glm::abs(part.origin),glm::dvec3(1e10)))
            ||!physics::AuthoredBodyFrame(part.shape).bodyMotion(
                {.position=physics::worldPositionFromAbsolute(part.origin),.orientation=part.orientation,
                 .originVelocity=part.originVelocity,.angularVelocity=part.angularVelocity},frameError)
            ||(!part.dynamic&&(part.originVelocity!=glm::vec3(0)||part.angularVelocity!=glm::vec3(0)))) {
            error="Imported section has an invalid physical frame.";return false;
        }
        const auto one=part.shape.cost();
        if(one.cells>maximumCells-cost.cells||one.faces>maximumFaces-cost.faces
            ||one.nodes>maximumNodes-cost.nodes||one.bytes>8*1024*1024-cost.bytes) {
            error="Imported sections exceed the complete shape budget.";return false;
        }
        cost.cells+=one.cells;cost.faces+=one.faces;cost.nodes+=one.nodes;cost.bytes+=one.bytes;
    }
    prepared.cost=cost;
    try { next_.resize(prepared.partitions.size()); }
    catch (const std::bad_alloc&) { error="Cannon collision handle allocation failed."; return false; }
    retirementBody_=prepared.consumeBody.value_or(physics::BodyHandle{});
    packet_=std::move(prepared); return true;
}

bool CannonPhysicsScene::ready(uint64_t revision) const noexcept {
    return revision && revision==acceptedRevision_ && !packet_ && !executionTick_ && !clearing_ && !failed_;
}
bool CannonPhysicsScene::empty() const noexcept {
    return live_.empty() && next_.empty() && !packet_ && !executionTick_ && !retirementBody_.valid();
}
void CannonPhysicsScene::abandonAfterWorldShutdown() noexcept {
    packet_.reset(); live_.clear(); next_.clear(); retirementBody_={}; acceptedRevision_=executionTick_=incarnation_=0;
    clearing_=failed_=clearingSubmitted_=false;
}
bool CannonPhysicsScene::retireShapes(physics::IAuthoredShapeResources& resources,
    std::vector<Owned>& owned, std::string& error) {
    for (auto& item:owned) if (item.shape.valid()) {
        const auto status=resources.retire(item.shape);
        if (status!=physics::ShapeResourceError::None) {
            error="Cannon shape retirement failed."; return false;
        }
        item.shape={};
    }
    owned.clear(); return true;
}

CannonPhysicsScene::Progress CannonPhysicsScene::update(physics::PhysicsWorld& world, std::string& error) {
    error.clear();
    const auto fail=[&](std::string message) { failed_=true; error=std::move(message); return Progress::Failed; };
    // A failed owned submission also makes the backend uninitialized. Inspect
    // its proof before the startup wait, otherwise a dead world waits forever.
    const auto frontier=world.tickFrontier();
    if (frontier.failed) return fail("Physics stopped safely. Reload the world to restore the house.");
    if (failed_ && !clearing_) { error="Cannon collision bridge has stopped after an earlier failure."; return Progress::Failed; }
    if (!world.isInitialized()) return Progress::Waiting;
    auto* resources=world.authoredShapeResources();
    if (!resources) {
        const auto enabled=world.enableAuthoredShapeResources(resourceLimits());
        if (retry(enabled)) return Progress::Waiting;
        if (enabled!=physics::ShapeResourceError::None && enabled!=physics::ShapeResourceError::AlreadyConfigured)
            return fail("GPU authored scenery is unavailable for the cannon.");
        resources=world.authoredShapeResources(); if (!resources) return Progress::Waiting;
    }
    resources->poll();
    if (resources->stats().phase==physics::ShapeResourcePhase::Failed)
        return fail("Cannon scenery upload failed on the GPU.");
    if (resources->stats().phase!=physics::ShapeResourcePhase::Ready) return Progress::Waiting;
    if (incarnation_ && frontier.incarnation!=incarnation_) return fail("Cannon physics world changed without retiring the old scene.");
    if (frontier.incarnation) incarnation_=frontier.incarnation;
    if (executionTick_) {
        if (frontier.completed<executionTick_) return Progress::Waiting;
        if (!retireShapes(*resources,live_,error)) { failed_=true; return Progress::Failed; }
        executionTick_=0;
        if (clearingSubmitted_) {
            clearingSubmitted_=false; acceptedRevision_=0;
        } else {
            live_=std::move(next_); acceptedRevision_=packet_->revision; packet_.reset();
        }
    }
    if (clearing_) {
        // Never retire a shape with a future body admission. An in-flight
        // transition above first joins; only unadmitted uploads remain here.
        if (packet_) {
            // Cancel unsubmitted reservations before retiring their shapes.
            // An admitted replacement was joined above and belongs to live_.
            for (auto& item:next_) if(item.body.valid()) {
                if(!world.destroyBody(item.body)) return fail("Cannon pending reservation cleanup failed.");
                item.body={};
            }
            if (!retireShapes(*resources,next_,error)) { failed_=true; return Progress::Failed; }
            packet_.reset();
        }
        if (live_.empty()&&!retirementBody_.valid()) { acceptedRevision_=0; return Progress::Ready; }
        if (!joined(frontier)) return Progress::Waiting;
        std::array<physics::BodyHandle,maximumPartitions+1> bodies{};
        for (size_t i=0;i<live_.size();++i) bodies[i]=live_[i].body;
        size_t count=live_.size();if(retirementBody_.valid())bodies[count++]=retirementBody_;
        const auto transaction=world.prepareMutationBatch({
            .bodyDestroys={bodies.data(),count},
            .joinedBoundary=physics::PhysicsMutationJoin{frontier.incarnation,frontier.completed}});
        if (!transaction) {
            if(transaction.status==physics::PhysicsMutationStatus::InvalidInput)
                return fail("Cannon retirement lost body ownership. Reload the world to recover.");
            return Progress::Waiting;
        }
        if (!world.commitPrepared(transaction)) return fail("Cannon scene retirement could not be committed.");
        retirementBody_={};executionTick_=transaction.targetTick; clearingSubmitted_=true; return Progress::Waiting;
    }
    if (!packet_) return acceptedRevision_ ? Progress::Ready : Progress::Waiting;
    for (size_t i=0;i<next_.size();++i) {
        if (!next_[i].shape.valid()) {
            physics::ShapeResourceError status;
            next_[i].shape=resources->upload(std::move(packet_->partitions[i].shape),status);
            if (!next_[i].shape.valid()) {
                if (retry(status)) return Progress::Waiting;
                return fail("The complete cannon collision scene does not fit the GPU shape pool.");
            }
        }
    }
    if (std::any_of(next_.begin(),next_.end(),[&](const auto& p) {
        return resources->state(p.shape)!=physics::ShapeResourceState::Ready;
    }) || !joined(frontier)) return Progress::Waiting;
    // Synchronous reservation: no simulation/submission occurs between the first
    // spawn and cancellation or commit. Failed admission cancels every new spawn.
    const auto cancel=[&] {
        bool ok=true;
        for (auto& p:next_) if (p.body.valid()) {
            if (world.destroyBody(p.body)) p.body={}; else ok=false;
        }
        return ok;
    };
    try {
        for (size_t i=0;i<next_.size();++i) {
            physics::AuthoredBodySpawnDesc desc; desc.shape=next_[i].shape;
            desc.motionType=packet_->partitions[i].dynamic
                ?physics::AuthoredBodyMotionType::Dynamic:physics::AuthoredBodyMotionType::Static;
            desc.motion.position=physics::worldPositionFromAbsolute(packet_->partitions[i].origin);
            desc.motion.orientation=packet_->partitions[i].orientation;
            desc.motion.originVelocity=packet_->partitions[i].originVelocity;
            desc.motion.angularVelocity=packet_->partitions[i].angularVelocity;
            const auto spawned=world.spawnAuthoredBody(desc);
            if (!spawned) {
                if (!cancel()) return fail("Cannon collider reservation rollback failed.");
                if (spawned.error==physics::AuthoredBodyError::Busy || spawned.error==physics::AuthoredBodyError::NotReady)
                    return Progress::Waiting;
                return fail("The complete cannon collision scene does not fit the GPU body pool.");
            }
            next_[i].body=spawned.body;
        }
    } catch (const std::bad_alloc&) {
        if (!cancel()) return fail("Cannon collider allocation rollback failed.");
        return fail("Cannon collider admission ran out of memory.");
    }
    uint64_t targetTick=frontier.completed+1;
    const bool consumesBody=retirementBody_.valid();
    if (!live_.empty()||consumesBody) {
        std::array<physics::BodyHandle,maximumPartitions+1> bodies{};
        for (size_t i=0;i<live_.size();++i) bodies[i]=live_[i].body;
        size_t count=live_.size();if(consumesBody)bodies[count++]=retirementBody_;
        const auto transaction=world.prepareMutationBatch({
            .bodyDestroys={bodies.data(),count},
            .joinedBoundary=physics::PhysicsMutationJoin{frontier.incarnation,frontier.completed}});
        if (!transaction) {
            if (!cancel()) return fail("Cannon collider replacement rollback failed.");
            if(transaction.status==physics::PhysicsMutationStatus::InvalidInput)
                return fail("Cannon replacement names a stale projectile or root.");
            return Progress::Waiting;
        }
        if (transaction.targetTick!=targetTick) {
            if (!world.discardPrepared(transaction) || !cancel()) return fail("Cannon collision boundary rollback failed.");
            return Progress::Waiting;
        }
        if (!world.commitPrepared(transaction)) {
            if(!cancel()) return fail("Cannon collider replacement rollback failed.");
            return fail("Cannon collider replacement could not be committed.");
        }
        retirementBody_={};
    }
    // An empty initial packet has no commands to certify; its complete empty
    // scene is already valid at this joined boundary.
    if (live_.empty() && next_.empty() && !consumesBody) { acceptedRevision_=packet_->revision; packet_.reset(); return Progress::Ready; }
    executionTick_=targetTick; return Progress::Waiting;
}

} // namespace voxy::game::adventure

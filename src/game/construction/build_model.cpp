#include "game/construction/build_model.hpp"

#include "core/sha256.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>
#include <utility>

namespace voxy::game::construction {
namespace {

constexpr DurableId kNoId{};
constexpr CubeRotation kMatingRotation{2};
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

[[nodiscard]] BuildIssue fail(BuildError error, DurableId id, std::string_view field) noexcept {
    return {error, id, field};
}

[[nodiscard]] bool contains(GridBox outer, GridBox inner) noexcept {
    return outer.minimum.x <= inner.minimum.x && outer.maximum.x >= inner.maximum.x
        && outer.minimum.y <= inner.minimum.y && outer.maximum.y >= inner.maximum.y
        && outer.minimum.z <= inner.minimum.z && outer.maximum.z >= inner.maximum.z;
}

[[nodiscard]] GridBox intersection(GridBox a, GridBox b) noexcept {
    return {{std::max(a.minimum.x, b.minimum.x), std::max(a.minimum.y, b.minimum.y),
             std::max(a.minimum.z, b.minimum.z)},
            {std::min(a.maximum.x, b.maximum.x), std::min(a.maximum.y, b.maximum.y),
             std::min(a.maximum.z, b.maximum.z)}};
}

[[nodiscard]] bool validSettings(const ModuleSettings& settings, const PartDefinition& definition) noexcept {
    const auto expected = defaultModuleSettings(definition);
    if (settings.kind != expected.kind || settings.controlChannel > 15) return false;
    switch (settings.kind) {
    case SettingsKind::Passive:
        return settings.controlChannel == 0 && settings.limitPermille == 0
            && !settings.reversed && settings.defaultLineLengthMillimetres == 0;
    case SettingsKind::Power:
        return settings.limitPermille <= 1000 && settings.defaultLineLengthMillimetres == 0;
    case SettingsKind::Steering:
        return settings.limitPermille <= 1000 && !settings.reversed && settings.defaultLineLengthMillimetres == 0;
    case SettingsKind::Winch: {
        const auto* winch = std::get_if<WinchModule>(&definition.module);
        const double length = static_cast<double>(settings.defaultLineLengthMillimetres) / 1000.0;
        return winch && settings.limitPermille == 0 && !settings.reversed
            && length >= winch->minimumLengthMetres && length <= winch->maximumLengthMetres;
    }
    }
    return false;
}

[[nodiscard]] bool strengthWithin(const StrengthLimits& value, const StrengthLimits& ceiling) noexcept {
    const std::array<double, 4> values{value.tensionNewtons, value.shearNewtons,
        value.bendingNewtonMetres, value.torsionNewtonMetres};
    const std::array<double, 4> maxima{ceiling.tensionNewtons, ceiling.shearNewtons,
        ceiling.bendingNewtonMetres, ceiling.torsionNewtonMetres};
    for (size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i]) || values[i] <= 0.0 || values[i] > maxima[i]) return false;
    }
    return true;
}

[[nodiscard]] const PartInstance* findPart(const BuildSnapshot& build, DurableId id) noexcept {
    const auto found = std::lower_bound(build.parts.begin(), build.parts.end(), id,
        [](const auto& part, const auto& value) { return part.id < value; });
    return found != build.parts.end() && found->id == id ? &*found : nullptr;
}

[[nodiscard]] OccupiedSocket* findOccupied(std::vector<OccupiedSocket>& sockets, SocketEndpoint endpoint) noexcept {
    const auto found = std::lower_bound(sockets.begin(), sockets.end(), endpoint,
        [](const auto& socket, const auto& value) { return socket.endpoint < value; });
    return found != sockets.end() && found->endpoint == endpoint ? &*found : nullptr;
}

[[nodiscard]] bool sameConnectionIdentity(const Connection& a, const Connection& b) noexcept {
    return a.a == b.a && a.b == b.b && a.kind == b.kind;
}

[[nodiscard]] BuildIssue prepare(
    const BuildSnapshot& draft, const PartCatalog& catalog, BuildSnapshot& canonical,
    std::vector<OccupiedSolid>& solids, std::vector<OccupiedSocket>& sockets) {
    if (draft.parts.size() > kMaximumBuildParts || draft.connections.size() > kMaximumBuildConnections)
        return fail(BuildError::Capacity, draft.id, "records");
    if (!isValid(draft.id) || !isValid(draft.owner)) return fail(BuildError::InvalidId, draft.id, "build");
    if (draft.editLease && (!isValid(draft.editLease->holder) || !draft.editLease->epoch.valid()
        || draft.editLease->expiresAfter.value() == 0)) return fail(BuildError::InvalidLease, draft.id, "editLease");
    BuildSnapshot copy = draft;
    std::sort(copy.parts.begin(), copy.parts.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    for (auto& connection : copy.connections) if (connection.b < connection.a) std::swap(connection.a, connection.b);
    std::sort(copy.connections.begin(), copy.connections.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::set<DurableId> physicalIds{copy.id};
    for (const auto& part : copy.parts) {
        if (!isValid(part.id)) return fail(BuildError::InvalidId, part.id, "part.id");
        if (part.id.world != copy.id.world) return fail(BuildError::WrongWorld, part.id, "part.id");
        if (!physicalIds.insert(part.id).second) return fail(BuildError::DuplicateId, part.id, "part.id");
        if (part.owningBuild != copy.id) return fail(BuildError::WrongBuild, part.id, "part.owningBuild");
        const auto definition = catalog.lookup(part.definition);
        if (!definition) return fail(definition.error == CatalogError::UnknownVersion
            ? BuildError::UnknownDefinitionVersion : BuildError::UnknownDefinition, part.id, "part.definition");
        if (!isValid(part.placement.translation) || !isValid(part.placement.rotation)
            || (definition.definition->permittedRotationMask & (1u << part.placement.rotation.value)) == 0)
            return fail(BuildError::InvalidPlacement, part.id, "part.placement");
        if (part.health > kFullHealth) return fail(BuildError::InvalidHealth, part.id, "part.health");
        if (!validSettings(part.settings, *definition.definition)) return fail(BuildError::InvalidSettings, part.id, "part.settings");
        const auto& provenance = part.provenance;
        if ((provenance.origin == PartOrigin::Paid && provenance.starterEntitlement != kNoId)
            || (provenance.origin == PartOrigin::StarterLoan && (!isValid(provenance.starterEntitlement)
                || provenance.starterEntitlement.world != copy.id.world))
            || (provenance.origin != PartOrigin::Paid && provenance.origin != PartOrigin::StarterLoan))
            return fail(BuildError::InvalidProvenance, part.id, "part.provenance");
        if (solids.size() + definition.definition->solidOccupancy.size() > kMaximumBuildSolidProxies
            || sockets.size() + definition.definition->sockets.size() > kMaximumBuildSocketRecords)
            return fail(BuildError::Capacity, part.id, "derivedOccupancy");
        for (const auto& proxy : definition.definition->solidOccupancy) {
            const auto local = boxBounds(proxy); // Catalog is already immutable and validated.
            const auto bounds = local ? transformBounds(part.placement, *local) : std::nullopt;
            if (!bounds) return fail(BuildError::InvalidPlacement, part.id, "solid.bounds");
            solids.push_back({part.id, proxy.id, *bounds});
        }
        for (const auto& socket : definition.definition->sockets) {
            const auto frame = compose(part.placement, socket.frame);
            const auto bounds = frame ? transformBounds(*frame, socket.clearance) : std::nullopt;
            if (!frame || !bounds) return fail(BuildError::InvalidPlacement, part.id, "socket.frame");
            sockets.push_back({{part.id, socket.id}, *frame, *bounds, 0, socket.connectionCapacity});
        }
    }
    struct MatingPair { SocketEndpoint a; SocketEndpoint b; };
    std::vector<MatingPair> matingPairs;
    std::set<std::pair<SocketEndpoint, SocketEndpoint>> connections;
    for (const auto& connection : copy.connections) {
        if (!isValid(connection.id)) return fail(BuildError::InvalidId, connection.id, "connection.id");
        if (connection.id.world != copy.id.world) return fail(BuildError::WrongWorld, connection.id, "connection.id");
        if (!physicalIds.insert(connection.id).second) return fail(BuildError::DuplicateId, connection.id, "connection.id");
        const auto* a = findPart(copy, connection.a.part);
        const auto* b = findPart(copy, connection.b.part);
        if (!a || !b) return fail(BuildError::UnknownPart, connection.id, "connection.endpoint");
        if (a->id == b->id) return fail(BuildError::SamePartConnection, connection.id, "connection.endpoint");
        const auto* aDefinition = catalog.lookup(a->definition).definition;
        const auto* bDefinition = catalog.lookup(b->definition).definition;
        const auto* aSocket = findSocket(*aDefinition, connection.a.socket);
        const auto* bSocket = findSocket(*bDefinition, connection.b.socket);
        if (!aSocket || !bSocket) return fail(BuildError::UnknownSocket, connection.id, "connection.endpoint");
        if (matchSockets(*aSocket, *bSocket, connection.kind) != SocketMatchError::None)
            return fail(BuildError::IncompatibleSocket, connection.id, "connection.kind");
        if (connection.damage > kFullHealth || !strengthWithin(connection.strength, aSocket->strength)
            || !strengthWithin(connection.strength, bSocket->strength)
            || !strengthWithin(connection.strength, aDefinition->strength)
            || !strengthWithin(connection.strength, bDefinition->strength))
            return fail(BuildError::InvalidConnection, connection.id, "connection.strengthOrDamage");
        if (connection.kind == ConnectionKind::Rope) {
            if (connection.minimumLengthMillimetres == 0 || connection.maximumLengthMillimetres > 1000000
                || connection.minimumLengthMillimetres > connection.restLengthMillimetres
                || connection.restLengthMillimetres > connection.maximumLengthMillimetres)
                return fail(BuildError::InvalidConnection, connection.id, "connection.ropeLimits");
            for (const auto* definition : {aDefinition, bDefinition}) {
                if (const auto* winch = std::get_if<WinchModule>(&definition->module)) {
                    if (static_cast<double>(connection.minimumLengthMillimetres) / 1000.0 < winch->minimumLengthMetres
                        || static_cast<double>(connection.maximumLengthMillimetres) / 1000.0 > winch->maximumLengthMetres
                        || connection.strength.tensionNewtons > winch->maximumForceNewtons)
                        return fail(BuildError::InvalidConnection, connection.id, "connection.winchLimits");
                }
            }
        } else if (connection.minimumLengthMillimetres != 0 || connection.maximumLengthMillimetres != 0
            || connection.restLengthMillimetres != 0) return fail(BuildError::InvalidConnection, connection.id, "connection.unusedLimits");
        if (!connections.emplace(connection.a, connection.b).second)
            return fail(BuildError::DuplicateConnection, connection.id, "connection.endpoints");
        auto* occupiedA = findOccupied(sockets, connection.a);
        auto* occupiedB = findOccupied(sockets, connection.b);
        if (!occupiedA || !occupiedB) return fail(BuildError::UnknownSocket, connection.id, "connection.occupancy");
        if (occupiedA->usedSlots >= occupiedA->capacity || occupiedB->usedSlots >= occupiedB->capacity)
            return fail(BuildError::SocketCapacity, connection.id, "connection.occupancy");
        ++occupiedA->usedSlots;
        ++occupiedB->usedSlots;
        // Disabled welds remain exact authored welds but do not provide a mating
        // clearance exception. Rope/latch records never imply capture or merging.
        if (connection.kind == ConnectionKind::Weld) {
            if (occupiedA->frame.translation != occupiedB->frame.translation
                || compose(occupiedA->frame.rotation, kMatingRotation) != occupiedB->frame.rotation)
                return fail(BuildError::MisalignedWeld, connection.id, "connection.frame");
            if (connection.enabled) matingPairs.push_back({connection.a, connection.b});
        }
    }
    for (const auto& part : copy.parts) {
        if (part.provenance.origin == PartOrigin::StarterLoan && physicalIds.contains(part.provenance.starterEntitlement))
            return fail(BuildError::InvalidProvenance, part.id, "part.entitlementAliasesPhysicalId");
    }
    size_t candidates = 0;
    const auto chargeCandidate = [&candidates]() { return ++candidates <= kMaximumBuildCandidatePairs; };
    // A one-axis sweep avoids comparing distant solids. Candidate count is an
    // explicit work bound, independent of the number of canonical parts/links.
    // Choose the widest occupied axis. A fixed X sweep made a vertical stack
    // spend its entire pair budget comparing bricks dozens of metres apart.
    // Signed 64-bit extents keep extreme valid grid coordinates well-defined.
    const auto component=[](GridPosition p,unsigned axis){return axis==0?p.x:axis==1?p.y:p.z;};
    unsigned sweepAxis=0;int64_t widest=-1;
    for(unsigned axis=0;axis<3;++axis) {
        int64_t low=std::numeric_limits<int64_t>::max(),high=std::numeric_limits<int64_t>::min();
        for(const auto& solid:solids){low=std::min(low,int64_t(component(solid.bounds.minimum,axis)));high=std::max(high,int64_t(component(solid.bounds.maximum,axis)));}
        if(!solids.empty()&&high-low>widest){widest=high-low;sweepAxis=axis;}
    }
    const auto coordinate=[&](GridPosition p){return component(p,sweepAxis);};
    std::vector<const OccupiedSolid*> sortedSolids;
    sortedSolids.reserve(solids.size());
    for (const auto& solid : solids) sortedSolids.push_back(&solid);
    std::stable_sort(sortedSolids.begin(), sortedSolids.end(), [&](const auto* a, const auto* b) {
        return coordinate(a->bounds.minimum) < coordinate(b->bounds.minimum);
    });
    for (size_t i = 0; i < sortedSolids.size(); ++i) {
        for (size_t j = i + 1; j < sortedSolids.size(); ++j) {
            const auto& a = *sortedSolids[i];
            const auto& b = *sortedSolids[j];
            if (coordinate(b.bounds.minimum) >= coordinate(a.bounds.maximum)) break;
            if (a.part == b.part) continue; // Catalog proxy unions may overlap.
            if (!chargeCandidate()) return fail(BuildError::Capacity, copy.id, "candidatePairs");
            if (solidBoxesOverlap(a.bounds, b.bounds)) return fail(BuildError::SolidOverlap, b.part, "solidOccupancy");
        }
    }
    // Only enabled welded endpoints reserve a mating insertion region. An
    // unconnected socket is queryable but does not require globally empty air.
    for (const auto& mating : matingPairs) {
        for (const auto endpoints : {std::pair{mating.a, mating.b}, std::pair{mating.b, mating.a}}) {
            const auto* own = findOccupied(sockets, endpoints.first);
            const auto* mate = findOccupied(sockets, endpoints.second);
            for (const auto* solid : sortedSolids) {
                if (coordinate(solid->bounds.minimum) >= coordinate(own->clearance.maximum)) break;
                if (solid->part == own->endpoint.part || coordinate(solid->bounds.maximum) <= coordinate(own->clearance.minimum)) continue;
                if (!chargeCandidate()) return fail(BuildError::Capacity, copy.id, "candidatePairs");
                if (!solidBoxesOverlap(own->clearance, solid->bounds)) continue;
                if (solid->part != mate->endpoint.part
                    || !contains(mate->clearance, intersection(own->clearance, solid->bounds)))
                    return fail(BuildError::ClearanceBlocked, solid->part, "socket.clearance");
            }
        }
    }
    canonical = std::move(copy);
    return {};
}

class Writer {
public:
    std::vector<std::byte> bytes;
    void u8(uint8_t value) { bytes.push_back(static_cast<std::byte>(value)); }
    void integer(uint64_t value, size_t count) {
        for (size_t i = 0; i < count; ++i) u8(static_cast<uint8_t>((value >> (8 * i)) & 255u));
    }
    void id(DurableId value) { for (auto byte : value.world.bytes) u8(byte); integer(value.counter, 8); }
    void frame(GridTransform value) {
        for (auto coordinate : {value.translation.x, value.translation.y, value.translation.z})
            integer(std::bit_cast<uint32_t>(coordinate), 4);
        u8(value.rotation.value);
    }
    void settings(ModuleSettings value) {
        u8(static_cast<uint8_t>(value.kind)); u8(value.enabled ? 1 : 0); u8(value.controlChannel);
        integer(value.limitPermille, 2); u8(value.reversed ? 1 : 0); integer(value.defaultLineLengthMillimetres, 4);
    }
    void endpoint(SocketEndpoint value) { id(value.part); integer(value.socket.value(), 8); }
    void strength(StrengthLimits value) {
        for (double field : {value.tensionNewtons, value.shearNewtons, value.bendingNewtonMetres, value.torsionNewtonMetres})
            integer(std::bit_cast<uint64_t>(field), 8);
    }
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> input) : bytes(input) {}
    std::span<const std::byte> bytes;
    size_t position = 0;
    bool valid = true;
    uint64_t integer(size_t count) noexcept {
        if (position > bytes.size() || count > bytes.size() - position) { valid = false; return 0; }
        uint64_t value = 0;
        for (size_t i = 0; i < count; ++i) value |= static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[position++])) << (8 * i);
        return value;
    }
    uint8_t u8() noexcept { return static_cast<uint8_t>(integer(1)); }
    bool boolean() noexcept { const auto value = u8(); if (value > 1) valid = false; return value == 1; }
    DurableId id() noexcept {
        DurableId value;
        for (auto& byte : value.world.bytes) byte = u8();
        value.counter = integer(8); return value;
    }
    GridTransform frame() noexcept {
        GridTransform value;
        value.translation.x = std::bit_cast<int32_t>(static_cast<uint32_t>(integer(4)));
        value.translation.y = std::bit_cast<int32_t>(static_cast<uint32_t>(integer(4)));
        value.translation.z = std::bit_cast<int32_t>(static_cast<uint32_t>(integer(4)));
        value.rotation.value = u8(); return value;
    }
    ModuleSettings settings() noexcept {
        ModuleSettings value;
        value.kind = static_cast<SettingsKind>(u8()); value.enabled = boolean(); value.controlChannel = u8();
        value.limitPermille = static_cast<uint16_t>(integer(2)); value.reversed = boolean();
        value.defaultLineLengthMillimetres = static_cast<uint32_t>(integer(4)); return value;
    }
    SocketEndpoint endpoint() noexcept { const auto part = id(); return {part, SocketId{integer(8)}}; }
    StrengthLimits strength() noexcept {
        StrengthLimits value;
        value.tensionNewtons = std::bit_cast<double>(integer(8)); value.shearNewtons = std::bit_cast<double>(integer(8));
        value.bendingNewtonMetres = std::bit_cast<double>(integer(8)); value.torsionNewtonMetres = std::bit_cast<double>(integer(8));
        return value;
    }
};

} // namespace

ModuleSettings defaultModuleSettings(const PartDefinition& definition) noexcept {
    ModuleSettings result;
    if (std::holds_alternative<EngineModule>(definition.module) || std::holds_alternative<PropellerModule>(definition.module)) {
        result.kind = SettingsKind::Power; result.limitPermille = 1000;
    } else if (std::holds_alternative<HelmModule>(definition.module)) {
        result.kind = SettingsKind::Steering; result.limitPermille = 1000;
    } else if (const auto* winch = std::get_if<WinchModule>(&definition.module)) {
        result.kind = SettingsKind::Winch;
        if (std::isfinite(winch->minimumLengthMetres) && winch->minimumLengthMetres > 0.0
            && winch->minimumLengthMetres <= 1000.0)
            result.defaultLineLengthMillimetres = static_cast<uint32_t>(std::ceil(winch->minimumLengthMetres * 1000.0));
    }
    return result;
}

std::optional<GridBox> transformBounds(GridTransform frame, GridBox box) noexcept {
    if (box.minimum.x >= box.maximum.x || box.minimum.y >= box.maximum.y || box.minimum.z >= box.maximum.z) return std::nullopt;
    const auto a = transformPosition(frame, box.minimum);
    const auto b = transformPosition(frame, box.maximum);
    if (!a || !b) return std::nullopt;
    return GridBox{{std::min(a->x, b->x), std::min(a->y, b->y), std::min(a->z, b->z)},
                   {std::max(a->x, b->x), std::max(a->y, b->y), std::max(a->z, b->z)}};
}

bool solidBoxesOverlap(GridBox a, GridBox b) noexcept {
    return a.minimum.x < b.maximum.x && b.minimum.x < a.maximum.x
        && a.minimum.y < b.maximum.y && b.minimum.y < a.maximum.y
        && a.minimum.z < b.maximum.z && b.minimum.z < a.maximum.z;
}

std::optional<GridPosition> coarsePlacementOffset(GridPosition localSteps, CubeRotation localAxes) noexcept {
    const std::array<int64_t, 3> values{static_cast<int64_t>(localSteps.x) * kStudTicks,
        static_cast<int64_t>(localSteps.y) * kPlateTicks, static_cast<int64_t>(localSteps.z) * kStudTicks};
    for (auto value : values) if (value < -static_cast<int64_t>(kMaximumGridCoordinate) || value > kMaximumGridCoordinate) return std::nullopt;
    return rotate(localAxes, {static_cast<int32_t>(values[0]), static_cast<int32_t>(values[1]), static_cast<int32_t>(values[2])});
}

std::optional<BuildModel> BuildModel::create(const BuildSnapshot& draft, const PartCatalog& catalog, BuildIssue& issue) {
    BuildModel result;
    issue = prepare(draft, catalog, result.snapshot_, result.solids_, result.sockets_);
    return issue ? std::nullopt : std::optional{std::move(result)};
}

BuildIssue BuildModel::cutWelds(TopologyRevision expected,std::span<const DurableId> requested,const PartCatalog& catalog) {
    if(snapshot_.revision!=expected)return fail(BuildError::StaleRevision,snapshot_.id,"cut.revision");
    if(!next(expected))return fail(BuildError::RevisionExhausted,snapshot_.id,"cut.revision");
    if(requested.empty()||requested.size()>kMaximumBuildConnections)
        return fail(BuildError::InvalidConnection,snapshot_.id,"cut.connections");
    std::vector<DurableId> cuts(requested.begin(),requested.end());std::sort(cuts.begin(),cuts.end());
    if(std::adjacent_find(cuts.begin(),cuts.end())!=cuts.end())return fail(BuildError::DuplicateConnection,{},"cut.duplicate");
    auto draft=snapshot_;
    for(auto id:cuts) {
        const auto found=std::lower_bound(draft.connections.begin(),draft.connections.end(),id,
            [](const Connection& c,DurableId key){return c.id<key;});
        if(found==draft.connections.end()||found->id!=id)return fail(BuildError::InvalidConnection,id,"cut.unknown");
        if(found->kind!=ConnectionKind::Weld)return fail(BuildError::InvalidConnection,id,"cut.kind");
        if(!found->enabled)return fail(BuildError::InvalidConnection,id,"cut.inactive");
        found->enabled=false;found->damage=kFullHealth;
    }
    return replace(expected,draft,catalog);
}

BuildIssue BuildModel::replace(TopologyRevision expected, const BuildSnapshot& draft, const PartCatalog& catalog) {
    if (expected != snapshot_.revision || draft.revision != expected) return fail(BuildError::StaleRevision, snapshot_.id, "revision");
    const auto revision = next(expected);
    if (!revision) return fail(BuildError::RevisionExhausted, snapshot_.id, "revision");
    if (draft.id != snapshot_.id) return fail(BuildError::ImmutableIdentity, draft.id, "build.id");
    BuildIssue issue;
    auto replacement = create(draft, catalog, issue);
    if (!replacement) return issue;
    for (const auto& part : replacement->snapshot_.parts) {
        const auto oldConnection = std::lower_bound(snapshot_.connections.begin(), snapshot_.connections.end(), part.id,
            [](const auto& value, const auto& id) { return value.id < id; });
        if (oldConnection != snapshot_.connections.end() && oldConnection->id == part.id)
            return fail(BuildError::ImmutableIdentity, part.id, "part.reusedConnectionId");
        if (const auto* previous = findPart(snapshot_, part.id)) {
            if (part.definition != previous->definition || part.owningBuild != previous->owningBuild
                || part.provenance != previous->provenance)
                return fail(BuildError::ImmutableIdentity, part.id, "part.identity");
        }
    }
    for (const auto& connection : replacement->snapshot_.connections) {
        if (findPart(snapshot_, connection.id))
            return fail(BuildError::ImmutableIdentity, connection.id, "connection.reusedPartId");
        const auto found = std::lower_bound(snapshot_.connections.begin(), snapshot_.connections.end(), connection.id,
            [](const auto& value, const auto& id) { return value.id < id; });
        if (found != snapshot_.connections.end() && found->id == connection.id && !sameConnectionIdentity(connection, *found))
            return fail(BuildError::ImmutableIdentity, connection.id, "connection.identity");
    }
    replacement->snapshot_.revision = *revision;
    *this = std::move(*replacement);
    return {};
}

BuildIssue encodeBuild(const BuildSnapshot& draft, const PartCatalog& catalog, std::vector<std::byte>& output) {
    BuildIssue issue;
    const auto model = BuildModel::create(draft, catalog, issue);
    if (!model) return issue;
    const auto& build = model->snapshot();
    const size_t size = kBuildHeaderBytes + build.parts.size() * kBuildPartBytes + build.connections.size() * kBuildConnectionBytes;
    if (size > kMaximumBuildBytes) return fail(BuildError::Capacity, build.id, "encodedBytes");
    Writer writer;
    writer.bytes.reserve(size);
    for (auto byte : std::array<uint8_t, 4>{'S', 'V', 'B', 'M'}) writer.u8(byte);
    writer.integer(kBuildSchemaVersion, 4); writer.id(build.id); writer.integer(build.revision.value(), 8); writer.id(build.owner);
    writer.u8(build.editLease ? 1 : 0);
    writer.id(build.editLease ? build.editLease->holder : kNoId);
    writer.integer(build.editLease ? build.editLease->epoch.value() : 0, 8);
    writer.integer(build.editLease ? build.editLease->expiresAfter.value() : 0, 8);
    writer.integer(build.parts.size(), 4); writer.integer(build.connections.size(), 4);
    for (const auto& part : build.parts) {
        writer.id(part.id); writer.id(part.definition.id); writer.integer(part.definition.version, 4);
        writer.frame(part.placement); writer.id(part.owningBuild); writer.integer(part.health, 2);
        for (auto color : part.paint) writer.u8(color);
        writer.settings(part.settings); writer.u8(static_cast<uint8_t>(part.provenance.origin));
        writer.id(part.provenance.starterEntitlement);
    }
    for (const auto& connection : build.connections) {
        writer.id(connection.id); writer.endpoint(connection.a); writer.endpoint(connection.b);
        writer.u8(static_cast<uint8_t>(connection.kind)); writer.u8(connection.enabled ? 1 : 0);
        writer.integer(connection.damage, 2); writer.strength(connection.strength);
        writer.integer(connection.minimumLengthMillimetres, 4); writer.integer(connection.maximumLengthMillimetres, 4);
        writer.integer(connection.restLengthMillimetres, 4);
    }
    if (writer.bytes.size() != size) return fail(BuildError::InvalidEncoding, build.id, "encoderSize");
    output = std::move(writer.bytes);
    return {};
}

BuildIssue decodeBuild(std::span<const std::byte> bytes, const PartCatalog& catalog, std::optional<BuildModel>& output) {
    if (bytes.size() > kMaximumBuildBytes) return fail(BuildError::Capacity, {}, "encodedBytes");
    if (bytes.size() < kBuildHeaderBytes) return fail(BuildError::InvalidEncoding, {}, "header");
    Reader reader(bytes);
    if (reader.u8() != 'S' || reader.u8() != 'V' || reader.u8() != 'B' || reader.u8() != 'M')
        return fail(BuildError::InvalidEncoding, {}, "magic");
    if (reader.integer(4) != kBuildSchemaVersion) return fail(BuildError::UnsupportedSchema, {}, "schema");
    BuildSnapshot draft;
    draft.id = reader.id(); draft.revision = TopologyRevision{reader.integer(8)}; draft.owner = reader.id();
    const bool hasLease = reader.boolean();
    const auto holder = reader.id(); const auto epoch = reader.integer(8); const auto expiry = reader.integer(8);
    if (hasLease) draft.editLease = EditLease{holder, AuthorityEpoch{epoch}, SimulationTick{expiry}};
    else if (holder != kNoId || epoch != 0 || expiry != 0) return fail(BuildError::InvalidEncoding, draft.id, "unusedLease");
    const auto partCount = reader.integer(4); const auto connectionCount = reader.integer(4);
    if (partCount > kMaximumBuildParts || connectionCount > kMaximumBuildConnections)
        return fail(BuildError::Capacity, draft.id, "records");
    if (bytes.size() != kBuildHeaderBytes + partCount * kBuildPartBytes + connectionCount * kBuildConnectionBytes)
        return fail(BuildError::InvalidEncoding, draft.id, "size");
    draft.parts.reserve(static_cast<size_t>(partCount)); draft.connections.reserve(static_cast<size_t>(connectionCount));
    for (uint64_t i = 0; i < partCount; ++i) {
        PartInstance part;
        part.id = reader.id(); part.definition.id = reader.id(); part.definition.version = static_cast<uint32_t>(reader.integer(4));
        part.placement = reader.frame(); part.owningBuild = reader.id(); part.health = static_cast<uint16_t>(reader.integer(2));
        for (auto& color : part.paint) color = reader.u8();
        part.settings = reader.settings(); part.provenance.origin = static_cast<PartOrigin>(reader.u8());
        part.provenance.starterEntitlement = reader.id();
        if (!draft.parts.empty() && !(draft.parts.back().id < part.id))
            return fail(draft.parts.back().id == part.id ? BuildError::DuplicateId : BuildError::NonCanonicalOrder, part.id, "part.order");
        draft.parts.push_back(part);
    }
    for (uint64_t i = 0; i < connectionCount; ++i) {
        Connection connection;
        connection.id = reader.id(); connection.a = reader.endpoint(); connection.b = reader.endpoint();
        connection.kind = static_cast<ConnectionKind>(reader.u8()); connection.enabled = reader.boolean();
        connection.damage = static_cast<uint16_t>(reader.integer(2)); connection.strength = reader.strength();
        connection.minimumLengthMillimetres = static_cast<uint32_t>(reader.integer(4));
        connection.maximumLengthMillimetres = static_cast<uint32_t>(reader.integer(4));
        connection.restLengthMillimetres = static_cast<uint32_t>(reader.integer(4));
        if (connection.b < connection.a) return fail(BuildError::NonCanonicalOrder, connection.id, "endpoint.order");
        if (!draft.connections.empty() && !(draft.connections.back().id < connection.id))
            return fail(draft.connections.back().id == connection.id ? BuildError::DuplicateId : BuildError::NonCanonicalOrder,
                connection.id, "connection.order");
        draft.connections.push_back(connection);
    }
    if (!reader.valid || reader.position != bytes.size()) return fail(BuildError::InvalidEncoding, draft.id, "fields");
    BuildIssue issue;
    auto model = BuildModel::create(draft, catalog, issue);
    if (!model) return issue;
    output = std::move(model);
    return {};
}

namespace {
// These private IDs exist only while validating design intent. They never cross
// this codec's boundary and cannot be imported as owned parts or entitlements.
std::optional<BuildModel> blueprintModel(const BuildBlueprint& blueprint,const PartCatalog& catalog,BuildIssue& issue) {
    if(blueprint.schemaVersion!=kBlueprintSchemaVersion) {issue=fail(BuildError::UnsupportedSchema,{},"blueprint.schema");return {};}
    if(blueprint.parts.empty() || blueprint.parts.size()>kMaximumBuildParts || blueprint.connections.size()>kMaximumBuildConnections) {
        issue=fail(BuildError::Capacity,{},"blueprint.records");return {};
    }
    constexpr WorldNamespace world{{'v','o','x','y','-','d','e','s','i','g','n','-','o','n','l','y'}};
    const auto id=[&](uint64_t value){return DurableId{world,value};};
    auto parts=blueprint.parts;
    std::sort(parts.begin(),parts.end(),[](const auto& a,const auto& b){return a.ordinal<b.ordinal;});
    BuildSnapshot build;build.id=id(1);build.owner=id(2);
    for(size_t i=0;i<parts.size();++i) {
        const auto& p=parts[i];
        if(p.ordinal!=i+1) {issue=fail(BuildError::InvalidId,{},"blueprint.ordinal");return {};}
        PartInstance part;part.id=id(100+p.ordinal);part.owningBuild=build.id;part.definition=p.definition;
        part.placement=p.placement;part.paint=p.paint;part.settings=p.settings;build.parts.push_back(part);
    }
    for(size_t i=0;i<blueprint.connections.size();++i) {
        const auto& c=blueprint.connections[i];
        if(!c.a.partOrdinal || c.a.partOrdinal>parts.size() || !c.b.partOrdinal || c.b.partOrdinal>parts.size()) {
            issue=fail(BuildError::UnknownPart,{},"blueprint.endpoint");return {};
        }
        Connection link;link.id=id(1024+i);link.a={id(100+c.a.partOrdinal),c.a.socket};link.b={id(100+c.b.partOrdinal),c.b.socket};
        link.kind=c.kind;link.enabled=c.enabled;link.strength=c.strength;link.minimumLengthMillimetres=c.minimumLengthMillimetres;
        link.maximumLengthMillimetres=c.maximumLengthMillimetres;link.restLengthMillimetres=c.restLengthMillimetres;
        build.connections.push_back(link);
    }
    return BuildModel::create(build,catalog,issue);
}
}
BuildIssue encodeBlueprint(const BuildBlueprint& draft,const PartCatalog& catalog,std::vector<std::byte>& output) {
    BuildIssue issue;const auto model=blueprintModel(draft,catalog,issue);if(!model)return issue;
    const auto blueprint=duplicateDesign(*model);Writer writer;
    const size_t size=16+blueprint.parts.size()*59+blueprint.connections.size()*70+32;
    if(size>kMaximumBlueprintBytes)return fail(BuildError::Capacity,{},"blueprint.bytes");
    writer.bytes.reserve(size);
    for(auto c:std::array<uint8_t,4>{'S','V','B','P'})writer.u8(c);
    writer.integer(kBlueprintSchemaVersion,4);writer.integer(blueprint.parts.size(),4);writer.integer(blueprint.connections.size(),4);
    for(const auto& p:blueprint.parts) {
        writer.integer(p.ordinal,4);writer.id(p.definition.id);writer.integer(p.definition.version,4);writer.frame(p.placement);
        for(auto c:p.paint)writer.u8(c);
        writer.settings(p.settings);
    }
    for(const auto& c:blueprint.connections) {
        writer.integer(c.a.partOrdinal,4);writer.integer(c.a.socket.value(),8);writer.integer(c.b.partOrdinal,4);writer.integer(c.b.socket.value(),8);
        writer.u8(static_cast<uint8_t>(c.kind));writer.u8(c.enabled?1:0);writer.strength(c.strength);
        writer.integer(c.minimumLengthMillimetres,4);writer.integer(c.maximumLengthMillimetres,4);writer.integer(c.restLengthMillimetres,4);
    }
    const auto digest=core::sha256(writer.bytes);
    writer.bytes.insert(writer.bytes.end(),digest.bytes.begin(),digest.bytes.end());
    if(writer.bytes.size()!=size)return fail(BuildError::InvalidEncoding,{},"blueprint.encoderSize");
    output=std::move(writer.bytes);return {};
}
BuildIssue decodeBlueprint(std::span<const std::byte> bytes,const PartCatalog& catalog,std::optional<BuildBlueprint>& output) {
    if(bytes.size()>kMaximumBlueprintBytes)return fail(BuildError::Capacity,{},"blueprint.bytes");
    if(bytes.size()<48)return fail(BuildError::InvalidEncoding,{},"blueprint.header");
    const auto payload=bytes.first(bytes.size()-32);const auto digest=core::sha256(payload);
    if(!std::equal(digest.bytes.begin(),digest.bytes.end(),bytes.end()-32))return fail(BuildError::InvalidEncoding,{},"blueprint.checksum");
    Reader reader(payload);
    if(reader.u8()!='S'||reader.u8()!='V'||reader.u8()!='B'||reader.u8()!='P')return fail(BuildError::InvalidEncoding,{},"blueprint.magic");
    if(reader.integer(4)!=kBlueprintSchemaVersion)return fail(BuildError::UnsupportedSchema,{},"blueprint.schema");
    const auto count=reader.integer(4),links=reader.integer(4);
    if(count==0 || count>kMaximumBuildParts || links>kMaximumBuildConnections)return fail(BuildError::Capacity,{},"blueprint.records");
    if(bytes.size()!=16+count*59+links*70+32)return fail(BuildError::InvalidEncoding,{},"blueprint.size");
    BuildBlueprint draft;draft.parts.reserve(static_cast<size_t>(count));draft.connections.reserve(static_cast<size_t>(links));
    for(uint64_t i=0;i<count;++i) {
        DesignPart p;p.ordinal=static_cast<uint32_t>(reader.integer(4));p.definition.id=reader.id();p.definition.version=static_cast<uint32_t>(reader.integer(4));
        p.placement=reader.frame();for(auto& c:p.paint)c=reader.u8();p.settings=reader.settings();draft.parts.push_back(p);
    }
    for(uint64_t i=0;i<links;++i) {
        DesignConnection c;c.a.partOrdinal=static_cast<uint32_t>(reader.integer(4));c.a.socket=SocketId{reader.integer(8)};
        c.b.partOrdinal=static_cast<uint32_t>(reader.integer(4));c.b.socket=SocketId{reader.integer(8)};
        c.kind=static_cast<ConnectionKind>(reader.u8());c.enabled=reader.boolean();c.strength=reader.strength();
        c.minimumLengthMillimetres=static_cast<uint32_t>(reader.integer(4));c.maximumLengthMillimetres=static_cast<uint32_t>(reader.integer(4));
        c.restLengthMillimetres=static_cast<uint32_t>(reader.integer(4));draft.connections.push_back(c);
    }
    if(!reader.valid || reader.position!=payload.size())return fail(BuildError::InvalidEncoding,{},"blueprint.fields");
    std::vector<std::byte> canonical;const auto issue=encodeBlueprint(draft,catalog,canonical);if(issue)return issue;
    if(!std::equal(bytes.begin(),bytes.end(),canonical.begin(),canonical.end()))return fail(BuildError::NonCanonicalOrder,{},"blueprint.order");
    output=std::move(draft);return {};
}

BuildBlueprint duplicateDesign(const BuildModel& model) {
    BuildBlueprint result;
    const auto& build = model.snapshot();
    result.parts.reserve(build.parts.size()); result.connections.reserve(build.connections.size());
    for (size_t i = 0; i < build.parts.size(); ++i) {
        const auto& part = build.parts[i];
        result.parts.push_back({static_cast<uint32_t>(i + 1), part.definition, part.placement, part.paint, part.settings});
    }
    const auto ordinal = [&build](DurableId id) {
        const auto found = std::lower_bound(build.parts.begin(), build.parts.end(), id,
            [](const auto& part, const auto& value) { return part.id < value; });
        return static_cast<uint32_t>(std::distance(build.parts.begin(), found)) + 1;
    };
    for (const auto& connection : build.connections) {
        result.connections.push_back({{ordinal(connection.a.part), connection.a.socket},
            {ordinal(connection.b.part), connection.b.socket}, connection.kind, connection.enabled,
            connection.strength, connection.minimumLengthMillimetres, connection.maximumLengthMillimetres,
            connection.restLengthMillimetres});
    }
    std::sort(result.connections.begin(), result.connections.end(), [](const auto& a, const auto& b) {
        return std::pair{a.a, a.b} < std::pair{b.a, b.b};
    });
    return result;
}

} // namespace voxy::game::construction

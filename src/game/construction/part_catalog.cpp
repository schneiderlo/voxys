#include "game/construction/part_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <type_traits>
#include <utility>

namespace voxy::game::construction {
namespace {

constexpr WorldNamespace kStarterNamespace{{
    'v', 'o', 'x', 'y', 's', '-', 's', 'a', 'l', 'v', 'a', 'g', 'e', '-', 'v', '1',
}};
constexpr WorldNamespace kPrototypeNamespace{{
    'v', 'o', 'x', 'y', 's', '-', 'p', 'r', 'o', 't', 'o', 't', 'y', 'p', 'e', 1,
}};
constexpr double kMaximumMassKg = 1.0e6;
constexpr double kMaximumStrength = 1.0e9;
constexpr uint64_t kMaximumResourceAmount = 1000000000ull;

[[nodiscard]] bool positive(double value, double maximum) noexcept {
    return std::isfinite(value) && value > 0.0 && value <= maximum;
}

[[nodiscard]] bool bounded(double value, double minimum, double maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool validGridBox(const GridBox& box) noexcept {
    if (!isValid(box.minimum) || !isValid(box.maximum)) {
        return false;
    }
    const std::array<int32_t, 3> low{box.minimum.x, box.minimum.y, box.minimum.z};
    const std::array<int32_t, 3> high{box.maximum.x, box.maximum.y, box.maximum.z};
    for (size_t axis = 0; axis < low.size(); ++axis) {
        if (low[axis] < -kMaximumPartExtentTicks || high[axis] > kMaximumPartExtentTicks
            || low[axis] >= high[axis]
            || static_cast<int64_t>(high[axis]) - low[axis] > kMaximumPartExtentTicks) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool contains(const GridBox& outer, const GridBox& inner) noexcept {
    return inner.minimum.x >= outer.minimum.x && inner.minimum.y >= outer.minimum.y
        && inner.minimum.z >= outer.minimum.z && inner.maximum.x <= outer.maximum.x
        && inner.maximum.y <= outer.maximum.y && inner.maximum.z <= outer.maximum.z;
}

[[nodiscard]] bool contains(const GridBox& outer, GridPosition position) noexcept {
    return contains(outer, GridBox{position, position});
}

[[nodiscard]] bool overlaps(const GridBox& a, const GridBox& b) noexcept {
    return a.minimum.x < b.maximum.x && b.minimum.x < a.maximum.x
        && a.minimum.y < b.maximum.y && b.minimum.y < a.maximum.y
        && a.minimum.z < b.maximum.z && b.minimum.z < a.maximum.z;
}

[[nodiscard]] bool validStrength(const StrengthLimits& strength) noexcept {
    return positive(strength.tensionNewtons, kMaximumStrength)
        && positive(strength.shearNewtons, kMaximumStrength)
        && positive(strength.bendingNewtonMetres, kMaximumStrength)
        && positive(strength.torsionNewtonMetres, kMaximumStrength);
}

[[nodiscard]] bool validFrame(const GridTransform& frame, const GridBox& footprint) noexcept {
    return isValid(frame.rotation) && isValid(frame.translation) && contains(footprint, frame.translation);
}

[[nodiscard]] bool validSocket(const SocketDefinition& socket) noexcept {
    if (!socket.id.valid() || socket.profile == 0 || socket.connectionCapacity == 0
        || socket.connectionCapacity > 4 || !isValid(socket.frame.translation)
        || !isValid(socket.frame.rotation) || !validGridBox(socket.clearance) || !validStrength(socket.strength)) {
        return false;
    }
    switch (socket.family) {
    case SocketFamily::Structural:
    case SocketFamily::DriveShaft:
    case SocketFamily::TowLine:
    case SocketFamily::CargoLatch:
        break;
    default:
        return false;
    }
    switch (socket.role) {
    case SocketRole::Neutral:
        return socket.family == SocketFamily::Structural;
    case SocketRole::Plug:
    case SocketRole::Receptacle:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] double determinant(const std::array<double, 9>& m) noexcept {
    return m[0] * (m[4] * m[8] - m[5] * m[7])
        - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

[[nodiscard]] bool validName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 96 || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    });
}

[[nodiscard]] bool validAssetPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > 240 || !path.ends_with(".vmesh")) {
        return false;
    }
    for (char c : path) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
              || c == '_' || c == '-' || c == '.' || c == '/')) {
            return false;
        }
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t separator = path.find('/', start);
        const auto segment = path.substr(start, separator == std::string_view::npos ? path.size() - start : separator - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            return true;
        }
        start = separator + 1;
    }
    return false;
}

[[nodiscard]] CatalogError validateBoxes(
    std::span<const PartBox> boxes, const GridBox& footprint, uint64_t& elementId) {
    if (boxes.empty() || boxes.size() > kMaximumPartBoxes) {
        return boxes.empty() ? CatalogError::InvalidBox : CatalogError::Capacity;
    }
    std::set<ProxyId> ids;
    for (const auto& box : boxes) {
        elementId = box.id.value();
        const auto bounds = boxBounds(box);
        if (!box.id.valid() || !bounds || !contains(footprint, *bounds)) {
            return CatalogError::InvalidBox;
        }
        if (!ids.insert(box.id).second) {
            return CatalogError::DuplicateProxyId;
        }
    }
    return CatalogError::None;
}

[[nodiscard]] CatalogError requireSocket(
    const PartDefinition& part, SocketId id, SocketFamily family, SocketRole role) noexcept {
    const auto* socket = findSocket(part, id);
    if (!socket) {
        return CatalogError::MissingModuleSocket;
    }
    return socket->family == family && socket->role == role
        ? CatalogError::None : CatalogError::IncompatibleModuleSocket;
}

[[nodiscard]] CatalogError validateModule(const PartDefinition& part) {
    if (part.module.valueless_by_exception()) {
        return CatalogError::InvalidModule;
    }
    return std::visit([&part](const auto& module) -> CatalogError {
        using T = std::decay_t<decltype(module)>;
        bool valid = true;
        CatalogError socketError = CatalogError::None;
        if constexpr (std::is_same_v<T, FlotationModule>) {
            valid = std::any_of(part.buoyancy.begin(), part.buoyancy.end(), [](const auto& region) {
                return region.kind == BuoyancyKind::SealedCompartment;
            });
            for (double coefficient : module.dragCoefficients) {
                valid = valid && bounded(coefficient, 0.0, 10.0);
            }
        } else if constexpr (std::is_same_v<T, EngineModule>) {
            valid = positive(module.maximumPowerWatts, 1.0e7) && positive(module.maximumTorqueNewtonMetres, 1.0e7);
            socketError = requireSocket(part, module.shaft, SocketFamily::DriveShaft, SocketRole::Plug);
        } else if constexpr (std::is_same_v<T, PropellerModule>) {
            valid = validFrame(module.forceFrame, part.footprint) && positive(module.maximumThrustNewtons, 1.0e7)
                && positive(module.requiredPowerWatts, 1.0e7);
            socketError = requireSocket(part, module.shaft, SocketFamily::DriveShaft, SocketRole::Receptacle);
        } else if constexpr (std::is_same_v<T, HelmModule>) {
            valid = validFrame(module.operatorFrame, part.footprint)
                && positive(module.maximumSteeringRadians, std::numbers::pi);
        } else if constexpr (std::is_same_v<T, WinchModule>) {
            valid = positive(module.minimumLengthMetres, 1000.0) && positive(module.maximumLengthMetres, 1000.0)
                && module.minimumLengthMetres < module.maximumLengthMetres
                && positive(module.reelSpeedMetresPerSecond, 20.0) && positive(module.maximumForceNewtons, kMaximumStrength);
            const auto* line = findSocket(part, module.line);
            if (!line) {
                socketError = CatalogError::MissingModuleSocket;
            } else if (line->family != SocketFamily::TowLine || line->role != SocketRole::Plug) {
                socketError = CatalogError::IncompatibleModuleSocket;
            } else {
                valid = valid && module.maximumForceNewtons <= line->strength.tensionNewtons;
            }
        } else if constexpr (std::is_same_v<T, TowEyeModule>) {
            socketError = requireSocket(part, module.eye, SocketFamily::TowLine, SocketRole::Receptacle);
        } else if constexpr (std::is_same_v<T, CargoCradleModule>) {
            valid = positive(module.maximumCargoMassKg, kMaximumMassKg)
                && positive(module.captureDistanceMetres, 0.5) && positive(module.captureAngleRadians, std::numbers::pi / 4.0)
                && positive(module.captureSpeedMetresPerSecond, 10.0)
                && positive(module.captureAngularSpeedRadiansPerSecond, 2.0 * std::numbers::pi);
            socketError = requireSocket(part, module.latch, SocketFamily::CargoLatch, SocketRole::Receptacle);
        } else if constexpr (std::is_same_v<T, BraceModule>) {
            valid = positive(module.loadTransferFactor, 4.0);
        } else if constexpr (std::is_same_v<T, RepairModule>) {
            valid = positive(module.reachMetres, 100.0) && positive(module.healthFractionPerSecond, 1.0)
                && module.materialUnitsPerFullHealth > 0 && module.materialUnitsPerFullHealth <= kMaximumResourceAmount;
        }
        if (!valid) {
            return CatalogError::InvalidModule;
        }
        return socketError;
    }, part.module);
}

[[nodiscard]] CatalogIssue validateDefinition(
    const PartDefinition& part, size_t index, const CookedAssetResolver& resolver, CatalogPolicy policy) {
    const auto fail = [index](CatalogError error, std::string_view field, uint64_t element = 0) {
        return CatalogIssue{error, index, field, element};
    };
    if (!isValid(part.key.id)) {
        return fail(CatalogError::InvalidDefinitionId, "key.id");
    }
    if (part.key.version == 0) {
        return fail(CatalogError::InvalidVersion, "key.version");
    }
    if (!validName(part.nameKey)) {
        return fail(CatalogError::InvalidName, "nameKey");
    }
    if (part.permittedRotationMask == 0 || (part.permittedRotationMask & 0xff000000u) != 0) {
        return fail(CatalogError::InvalidRotationMask, "permittedRotationMask");
    }
    if (!validGridBox(part.footprint)) {
        return fail(CatalogError::InvalidFootprint, "footprint");
    }
    uint64_t elementId = 0;
    auto boxesError = validateBoxes(part.solidOccupancy, part.footprint, elementId);
    if (boxesError != CatalogError::None) {
        return fail(boxesError, "solidOccupancy", elementId);
    }
    if (part.collision.empty()) {
        return fail(CatalogError::MissingCollision, "collision");
    }
    boxesError = validateBoxes(part.collision, part.footprint, elementId);
    if (boxesError != CatalogError::None) {
        return fail(boxesError, "collision", elementId);
    }
    if (!bounded(part.mass.dryMassKg, 0.001, kMaximumMassKg)) {
        return fail(CatalogError::InvalidMass, "mass.dryMassKg");
    }
    const auto& center = part.mass.localCenterOfMass;
    const auto minimum = *toMetres(part.footprint.minimum);
    const auto maximum = *toMetres(part.footprint.maximum);
    if (!bounded(center.x, minimum.x, maximum.x) || !bounded(center.y, minimum.y, maximum.y)
        || !bounded(center.z, minimum.z, maximum.z)) {
        return fail(CatalogError::InvalidCenterOfMass, "mass.localCenterOfMass");
    }
    if (!physicallyValidInertia(part.mass.inertia)) {
        return fail(CatalogError::InvalidInertia, "mass.inertia");
    }
    const auto& tensor = part.mass.inertia.elements;
    const double halfTrace = (tensor[0] + tensor[4] + tensor[8]) / 2.0;
    const std::array<double, 3> centers{center.x, center.y, center.z};
    const std::array<double, 3> minima{minimum.x, minimum.y, minimum.z};
    const std::array<double, 3> maxima{maximum.x, maximum.y, maximum.z};
    for (size_t axis = 0; axis < 3; ++axis) {
        // A bounded mass distribution with mean c has variance <= (c-min)(max-c).
        const double maximumSecondMoment = part.mass.dryMassKg
            * (centers[axis] - minima[axis]) * (maxima[axis] - centers[axis]);
        const double secondMoment = halfTrace - tensor[axis * 4];
        if (secondMoment > maximumSecondMoment + 1.0e-8 * std::max(1.0, maximumSecondMoment)) {
            return fail(CatalogError::InvalidInertia, "mass.inertia");
        }
    }
    if (!validStrength(part.strength)) {
        return fail(CatalogError::InvalidStrength, "strength");
    }
    if (part.buoyancy.size() > kMaximumPartBoxes) {
        return fail(CatalogError::Capacity, "buoyancy");
    }
    std::set<ProxyId> volumeIds;
    std::vector<GridBox> volumeBounds;
    for (const auto& region : part.buoyancy) {
        const auto bounds = boxBounds(region.box);
        if (!region.box.id.valid() || !bounds || !contains(part.footprint, *bounds)
            || (region.kind != BuoyancyKind::SolidMaterial && region.kind != BuoyancyKind::SealedCompartment)) {
            return fail(CatalogError::InvalidBuoyancy, "buoyancy", region.box.id.value());
        }
        if (!volumeIds.insert(region.box.id).second) {
            return fail(CatalogError::DuplicateProxyId, "buoyancy", region.box.id.value());
        }
        for (const auto& previous : volumeBounds) {
            if (overlaps(previous, *bounds)) {
                return fail(CatalogError::OverlappingBuoyancy, "buoyancy", region.box.id.value());
            }
        }
        volumeBounds.push_back(*bounds);
    }
    if (part.sockets.empty() || part.sockets.size() > kMaximumPartSockets) {
        return fail(part.sockets.empty() ? CatalogError::InvalidSocket : CatalogError::Capacity, "sockets");
    }
    std::set<SocketId> socketIds;
    for (const auto& socket : part.sockets) {
        if (!validSocket(socket) || !validFrame(socket.frame, part.footprint)) {
            return fail(CatalogError::InvalidSocket, "sockets", socket.id.value());
        }
        if (!socketIds.insert(socket.id).second) {
            return fail(CatalogError::DuplicateSocketId, "sockets", socket.id.value());
        }
    }
    const auto moduleError = validateModule(part);
    if (moduleError != CatalogError::None) {
        return fail(moduleError, "module");
    }
    if ((part.cost.salvageMaterial == 0 && part.cost.specialMachinery == 0)
        || part.cost.salvageMaterial > kMaximumResourceAmount || part.cost.specialMachinery > kMaximumResourceAmount) {
        return fail(CatalogError::InvalidCost, "cost");
    }
    if (part.salvageYield.salvageMaterial > part.cost.salvageMaterial
        || part.salvageYield.specialMachinery > part.cost.specialMachinery) {
        return fail(CatalogError::InvalidSalvageYield, "salvageYield");
    }
    for (double color : part.material.linearBaseColor) {
        if (!bounded(color, 0.0, 1.0)) {
            return fail(CatalogError::InvalidMaterial, "material.linearBaseColor");
        }
    }
    if (!bounded(part.material.roughness, 0.0, 1.0) || !bounded(part.material.metallic, 0.0, 1.0)) {
        return fail(CatalogError::InvalidMaterial, "material");
    }
    if (part.visuals.empty() || part.visuals.size() > kMaximumVisualLods) {
        return fail(CatalogError::InvalidVisualLod, "visuals");
    }
    double previousThreshold = std::numeric_limits<double>::infinity();
    for (const auto& lod : part.visuals) {
        if (!bounded(lod.minimumScreenHeightPixels, 0.0, 100000.0) || lod.minimumScreenHeightPixels >= previousThreshold) {
            return fail(CatalogError::InvalidVisualLod, "visuals.minimumScreenHeightPixels");
        }
        previousThreshold = lod.minimumScreenHeightPixels;
        if (const auto* prototype = std::get_if<PrototypeBoxVisual>(&lod.asset)) {
            if (!policy.allowPrototypeVisuals) {
                return fail(CatalogError::PrototypeNotAllowed, "visuals.asset");
            }
            if (part.visuals.size() != 1 || prototype->bounds != part.footprint || !prototypeAssetExists(*prototype)) {
                return fail(CatalogError::InvalidAssetReference, "visuals.asset");
            }
        } else if (const auto* cooked = std::get_if<CookedMeshVisual>(&lod.asset)) {
            if (!isValid(cooked->asset.id) || cooked->asset.id == prototypeBoxAsset().id
                || cooked->asset.version == 0 || !validAssetPath(cooked->path)) {
                return fail(CatalogError::InvalidAssetReference, "visuals.asset");
            }
            if (!resolver || !resolver(*cooked)) {
                return fail(CatalogError::MissingAsset, "visuals.asset");
            }
        } else {
            return fail(CatalogError::InvalidAssetReference, "visuals.asset");
        }
    }
    if (previousThreshold != 0.0) {
        return fail(CatalogError::InvalidVisualLod, "visuals.minimumScreenHeightPixels");
    }
    return {};
}

} // namespace

std::optional<GridBox> boxBounds(const PartBox& box) noexcept {
    if (!isValid(box.frame.rotation) || !isValid(box.frame.translation)
        || box.halfExtents.x <= 0 || box.halfExtents.y <= 0 || box.halfExtents.z <= 0
        || box.halfExtents.x > kMaximumPartExtentTicks / 2
        || box.halfExtents.y > kMaximumPartExtentTicks / 2 || box.halfExtents.z > kMaximumPartExtentTicks / 2) {
        return std::nullopt;
    }
    const auto signedExtents = *rotate(box.frame.rotation, box.halfExtents);
    const GridPosition extents{std::abs(signedExtents.x), std::abs(signedExtents.y), std::abs(signedExtents.z)};
    const auto minimum = checkedSubtract(box.frame.translation, extents);
    const auto maximum = checkedAdd(box.frame.translation, extents);
    if (!minimum || !maximum || !validGridBox({*minimum, *maximum})) {
        return std::nullopt;
    }
    return GridBox{*minimum, *maximum};
}

std::optional<double> boxVolumeCubicMetres(const PartBox& box) noexcept {
    if (!boxBounds(box)) {
        return std::nullopt;
    }
    return 8.0 * static_cast<double>(box.halfExtents.x) * static_cast<double>(box.halfExtents.y)
        * static_cast<double>(box.halfExtents.z) / 125000.0;
}

bool physicallyValidInertia(const InertiaTensor& inertia) noexcept {
    double scale = 0.0;
    for (double value : inertia.elements) {
        if (!std::isfinite(value)) {
            return false;
        }
        scale = std::max(scale, std::abs(value));
    }
    if (!bounded(scale, 1.0e-9, 1.0e12)) {
        return false;
    }
    auto m = inertia.elements;
    if (m[1] != m[3] || m[2] != m[6] || m[5] != m[7]) {
        return false;
    }
    for (double& value : m) {
        value /= scale;
    }
    // Strict positive definiteness with a bounded near-singularity rejection.
    if (m[0] <= 1.0e-12 || m[0] * m[4] - m[1] * m[3] <= 1.0e-12 || determinant(m) <= 1.0e-12) {
        return false;
    }
    // S = trace(I)/2 * Identity - I is the second-moment matrix. Its positive
    // semidefiniteness enforces physical principal-moment triangle inequalities.
    const double halfTrace = (m[0] + m[4] + m[8]) / 2.0;
    for (double& value : m) {
        value = -value;
    }
    m[0] += halfTrace;
    m[4] += halfTrace;
    m[8] += halfTrace;
    constexpr double tolerance = 1.0e-10;
    return m[0] >= -tolerance && m[4] >= -tolerance && m[8] >= -tolerance
        && m[0] * m[4] - m[1] * m[3] >= -tolerance
        && m[0] * m[8] - m[2] * m[6] >= -tolerance
        && m[4] * m[8] - m[5] * m[7] >= -tolerance && determinant(m) >= -tolerance;
}

const SocketDefinition* findSocket(const PartDefinition& part, SocketId id) noexcept {
    const auto found = std::find_if(part.sockets.begin(), part.sockets.end(), [id](const auto& socket) { return socket.id == id; });
    return found == part.sockets.end() ? nullptr : &*found;
}

SocketMatchError matchSockets(const SocketDefinition& a, const SocketDefinition& b, ConnectionKind connection) noexcept {
    if (!validSocket(a) || !validSocket(b)) {
        return SocketMatchError::InvalidSocket;
    }
    if (a.family != b.family) {
        return SocketMatchError::FamilyMismatch;
    }
    if (a.profile != b.profile) {
        return SocketMatchError::ProfileMismatch;
    }
    const bool complementary = (a.role == SocketRole::Plug && b.role == SocketRole::Receptacle)
        || (b.role == SocketRole::Plug && a.role == SocketRole::Receptacle)
        || (a.family == SocketFamily::Structural && a.role == SocketRole::Neutral && b.role == SocketRole::Neutral);
    if (!complementary) {
        return SocketMatchError::RoleMismatch;
    }
    const bool permitted = (connection == ConnectionKind::Weld
                            && (a.family == SocketFamily::Structural || a.family == SocketFamily::DriveShaft))
        || (connection == ConnectionKind::Rope && a.family == SocketFamily::TowLine)
        || (connection == ConnectionKind::Latch && a.family == SocketFamily::CargoLatch);
    return permitted ? SocketMatchError::None : SocketMatchError::ConnectionKindMismatch;
}

std::optional<PartCatalog> PartCatalog::create(
    const PartCatalogDraft& draft, CatalogIssue& issue, const CookedAssetResolver& resolver, CatalogPolicy policy) {
    issue = {};
    if (draft.schemaVersion != kPartCatalogSchemaVersion) {
        issue = {CatalogError::UnsupportedSchema, 0, "schemaVersion", 0};
        return std::nullopt;
    }
    if (draft.definitions.empty() || draft.definitions.size() > kMaximumCatalogDefinitions) {
        issue = {draft.definitions.empty() ? CatalogError::EmptyCatalog : CatalogError::Capacity, 0, "definitions", 0};
        return std::nullopt;
    }
    std::set<ContentKey> keys;
    for (size_t i = 0; i < draft.definitions.size(); ++i) {
        issue = validateDefinition(draft.definitions[i], i, resolver, policy);
        if (issue.error != CatalogError::None) {
            return std::nullopt;
        }
        if (!keys.insert(draft.definitions[i].key).second) {
            issue = {CatalogError::DuplicateDefinition, i, "key", draft.definitions[i].key.id.counter};
            return std::nullopt;
        }
    }
    PartCatalog result;
    result.definitions_.reserve(draft.definitions.size());
    for (const auto& definition : draft.definitions) {
        result.definitions_.emplace_back(definition);
    }
    std::sort(result.definitions_.begin(), result.definitions_.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    for (auto& part : result.definitions_) {
        const auto byId = [](const auto& a, const auto& b) { return a.id < b.id; };
        std::sort(part.collision.begin(), part.collision.end(), byId);
        std::sort(part.solidOccupancy.begin(), part.solidOccupancy.end(), byId);
        std::sort(part.sockets.begin(), part.sockets.end(), byId);
        std::sort(part.buoyancy.begin(), part.buoyancy.end(), [](const auto& a, const auto& b) { return a.box.id < b.box.id; });
    }
    return result;
}

PartLookup PartCatalog::lookup(ContentKey key) const noexcept {
    const auto found = std::lower_bound(definitions_.begin(), definitions_.end(), key,
                                       [](const auto& part, const auto& value) { return part.key < value; });
    if (found != definitions_.end() && found->key == key) {
        return {&*found, CatalogError::None};
    }
    const auto sameId = std::lower_bound(definitions_.begin(), definitions_.end(), key.id,
                                        [](const auto& part, const auto& id) { return part.key.id < id; });
    return {nullptr, sameId != definitions_.end() && sameId->key.id == key.id ? CatalogError::UnknownVersion : CatalogError::UnknownDefinition};
}

ContentKey prototypeBoxAsset() noexcept { return {{kPrototypeNamespace, 1}, 1}; }

bool prototypeAssetExists(const PrototypeBoxVisual& asset) noexcept {
    return asset.asset == prototypeBoxAsset() && validGridBox(asset.bounds);
}

ContentKey starterPartKey(StarterPart part) noexcept {
    const auto counter = static_cast<uint64_t>(part);
    if (counter < 1 || counter > 12) {
        return {};
    }
    return {{kStarterNamespace, counter}, 1};
}

namespace {

[[nodiscard]] InertiaTensor boxInertia(double mass, GridPosition halfExtents) {
    const auto h = *toMetres(halfExtents);
    return {{mass * (h.y * h.y + h.z * h.z) / 3.0, 0.0, 0.0,
             0.0, mass * (h.x * h.x + h.z * h.z) / 3.0, 0.0,
             0.0, 0.0, mass * (h.x * h.x + h.y * h.y) / 3.0}};
}

[[nodiscard]] SocketDefinition socket(
    uint64_t id, SocketFamily family, SocketRole role, GridTransform frame, StrengthLimits strength) {
    const GridBox clearance = role == SocketRole::Plug
        ? GridBox{{-15, 0, -15}, {15, kStudInsertionTicks, 15}}
        : GridBox{{-15, -kStudInsertionTicks, -15}, {15, 0, 15}};
    return {SocketId{id}, family, role, 1, frame, 1, clearance, strength};
}

void addStructuralPair(PartDefinition& part, int32_t x, int32_t z, uint64_t firstId) {
    part.sockets.push_back(socket(firstId, SocketFamily::Structural, SocketRole::Plug,
                                  {{x, part.footprint.maximum.y, z}, {0}}, part.strength));
    part.sockets.push_back(socket(firstId + 1, SocketFamily::Structural, SocketRole::Receptacle,
                                  {{x, part.footprint.minimum.y, z}, {2}}, part.strength));
}

[[nodiscard]] PartDefinition starter(
    StarterPart id, std::string name, GridPosition halfExtents, double mass,
    ResourceAmounts cost, ResourceAmounts yield, PartModule module) {
    PartDefinition part{};
    part.key = starterPartKey(id);
    part.nameKey = "salvage.part." + name;
    part.footprint = {{-halfExtents.x, -halfExtents.y, -halfExtents.z}, halfExtents};
    const PartBox box{ProxyId{1}, {}, halfExtents};
    part.solidOccupancy = {box};
    part.collision = {box};
    part.mass = {mass, {}, boxInertia(mass, halfExtents)};
    // Open non-pontoon parts displace a small authored solid core, not their
    // entire coarse collision box. The pontoon below has a sealed full volume.
    auto solidCore = box;
    solidCore.halfExtents.y = std::min(halfExtents.y, 2);
    part.buoyancy = {{solidCore, BuoyancyKind::SolidMaterial}};
    part.strength = {30000.0, 24000.0, 18000.0, 12000.0};
    part.module = std::move(module);
    part.cost = cost;
    part.salvageYield = yield;
    part.visuals = {{PrototypeBoxVisual{prototypeBoxAsset(), part.footprint}, 0.0}};
    addStructuralPair(part, 0, 0, 1);
    return part;
}

} // namespace

PartCatalogDraft makeStarterCatalogDraft() {
    PartCatalogDraft draft{};
    auto& parts = draft.definitions;
    parts.reserve(12);

    auto beam = starter(StarterPart::Beam, "beam", {100, 24, 25}, 90.0, {12, 0}, {8, 0}, StructureModule{});
    uint64_t socketId = 100;
    for (int32_t x : {-75, -25, 25, 75}) {
        addStructuralPair(beam, x, 0, socketId);
        socketId += 2;
    }
    parts.push_back(std::move(beam));

    auto plate = starter(StarterPart::Plate, "plate", {100, 8, 50}, 80.0, {10, 0}, {7, 0}, StructureModule{});
    socketId = 100;
    for (int32_t x : {-75, -25, 25, 75}) {
        for (int32_t z : {-25, 25}) {
            addStructuralPair(plate, x, z, socketId);
            socketId += 2;
        }
    }
    parts.push_back(std::move(plate));

    auto pontoon = starter(StarterPart::Pontoon, "pontoon", {100, 24, 25}, 120.0, {24, 0}, {16, 0},
                           FlotationModule{{0.9, 1.2, 0.4}});
    pontoon.buoyancy = {{pontoon.collision.front(), BuoyancyKind::SealedCompartment}};
    pontoon.material.linearBaseColor = {0.08, 0.35, 0.32};
    parts.push_back(std::move(pontoon));

    auto engine = starter(StarterPart::Engine, "engine", {25, 24, 25}, 160.0, {30, 1}, {18, 0},
                          EngineModule{SocketId{10}, 18000.0, 500.0});
    engine.mass.localCenterOfMass = {0.0, -0.12, 0.0};
    engine.mass.inertia = boxInertia(engine.mass.dryMassKg, {20, 12, 20});
    engine.buoyancy.front().box.frame.translation.y = -6;
    engine.sockets.push_back(socket(10, SocketFamily::DriveShaft, SocketRole::Plug, {{0, 0, -25}, {3}}, engine.strength));
    engine.material.linearBaseColor = {0.18, 0.22, 0.25};
    engine.material.metallic = 0.5;
    parts.push_back(std::move(engine));

    auto propeller = starter(StarterPart::Propeller, "propeller", {25, 24, 16}, 35.0, {16, 0}, {10, 0},
                             PropellerModule{SocketId{10}, {{0, 0, -16}, {}}, 2500.0, 18000.0});
    propeller.sockets.push_back(socket(10, SocketFamily::DriveShaft, SocketRole::Receptacle,
                                       {{0, 0, 16}, {1}}, propeller.strength));
    propeller.material.metallic = 0.7;
    parts.push_back(std::move(propeller));

    auto helm = starter(StarterPart::Helm, "helm", {25, 24, 25}, 30.0, {10, 0}, {6, 0},
                        HelmModule{{{0, 24, 0}, {}}, 0.6});
    helm.material.linearBaseColor = {0.8, 0.16, 0.09};
    parts.push_back(std::move(helm));

    auto winch = starter(StarterPart::Winch, "winch", {25, 48, 25}, 140.0, {30, 0}, {20, 0},
                         WinchModule{SocketId{10}, 0.5, 40.0, 2.0, 12000.0});
    winch.sockets.push_back(socket(10, SocketFamily::TowLine, SocketRole::Plug, {{0, 48, -25}, {3}}, winch.strength));
    winch.material.linearBaseColor = {0.8, 0.16, 0.09};
    parts.push_back(std::move(winch));

    auto eye = starter(StarterPart::TowEye, "tow_eye", {15, 8, 15}, 8.0, {4, 0}, {2, 0}, TowEyeModule{SocketId{10}});
    eye.sockets.push_back(socket(10, SocketFamily::TowLine, SocketRole::Receptacle, {{0, 8, 0}, {}}, eye.strength));
    eye.material.metallic = 0.7;
    parts.push_back(std::move(eye));

    auto cradle = starter(StarterPart::CargoCradle, "cargo_cradle", {50, 8, 50}, 90.0, {14, 0}, {9, 0},
                          CargoCradleModule{SocketId{10}, 3000.0});
    cradle.sockets.push_back(socket(10, SocketFamily::CargoLatch, SocketRole::Receptacle,
                                    {{0, 8, 0}, {}}, cradle.strength));
    parts.push_back(std::move(cradle));

    parts.push_back(starter(StarterPart::Brace, "brace", {50, 8, 25}, 45.0, {8, 0}, {5, 0}, BraceModule{1.5}));

    auto ballast = starter(StarterPart::Ballast, "ballast", {25, 24, 25}, 600.0, {20, 0}, {14, 0}, BallastModule{});
    ballast.mass.localCenterOfMass = {0.0, -0.24, 0.0};
    ballast.mass.inertia = boxInertia(ballast.mass.dryMassKg, {20, 6, 20});
    ballast.buoyancy.front().box.halfExtents = {20, 6, 20};
    ballast.buoyancy.front().box.frame.translation.y = -12;
    ballast.material.linearBaseColor = {0.12, 0.15, 0.18};
    ballast.material.metallic = 0.8;
    parts.push_back(std::move(ballast));

    auto repair = starter(StarterPart::RepairModule, "repair_module", {25, 24, 25}, 40.0, {18, 0}, {12, 0},
                          RepairModule{3.0, 0.1, 12});
    repair.material.linearBaseColor = {0.7, 0.8, 0.74};
    parts.push_back(std::move(repair));
    return draft;
}

} // namespace voxy::game::construction

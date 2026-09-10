#include "game/construction/assembly_compiler.hpp"

#include <algorithm>
#include <cmath>
#include <new>

namespace voxy::game::construction {
namespace {
using Vec = std::array<double, 3>;
Vec vector(MetresPosition p) noexcept { return {p.x, p.y, p.z}; }
MetresPosition position(Vec p) noexcept { return {p[0], p[1], p[2]}; }

// Neumaier accumulation in canonical part order. All terms are bounded by the
// validated catalog, root extent and 256-part profile before being accumulated.
struct Sum {
    double value = 0.0, correction = 0.0;
    void add(double term) noexcept {
        const double nextValue = value + term;
        correction += std::abs(value) >= std::abs(term) ? (value - nextValue) + term : (term - nextValue) + value;
        value = nextValue;
    }
    [[nodiscard]] double total() const noexcept { return value + correction; }
};

struct OrientedMass { double mass = 0.0; Vec center{}; std::array<double, 9> inertia{}; };
OrientedMass orient(const MassProperties& source, GridTransform frame) noexcept {
    const auto rotation = *rotationMatrix(frame.rotation);
    const auto offset = vector(*toMetres(frame.translation));
    const auto local = vector(source.localCenterOfMass);
    // One nonzero per row avoids summing zeros or rounding a dense R I R^T.
    std::array<size_t, 3> columns{};
    Vec signs{};
    for (size_t row = 0; row < 3; ++row) for (size_t col = 0; col < 3; ++col) {
        const auto entry = rotation.elements[row * 3 + col];
        if (entry != 0) { columns[row] = col; signs[row] = entry; }
    }
    OrientedMass result;
    result.mass = source.dryMassKg;
    for (size_t row = 0; row < 3; ++row) {
        result.center[row] = offset[row] + signs[row] * local[columns[row]];
        for (size_t col = 0; col < 3; ++col)
            result.inertia[row * 3 + col] = signs[row] * signs[col]
                * source.inertia.elements[columns[row] * 3 + columns[col]];
    }
    return result;
}
bool validAggregate(const MassProperties& mass) noexcept {
    if (!std::isfinite(mass.dryMassKg) || mass.dryMassKg <= 0.0) return false;
    for (double value : vector(mass.localCenterOfMass)) if (!std::isfinite(value)) return false;
    double scale = 0.0;
    for (double value : mass.inertia.elements) {
        if (!std::isfinite(value)) return false;
        scale = std::max(scale, std::abs(value));
    }
    if (scale == 0.0) return false;
    auto normalized = mass.inertia;
    for (double& value : normalized.elements) value /= scale;
    return physicallyValidInertia(normalized);
}
bool within(GridBox bounds, int32_t limit) noexcept {
    return bounds.minimum.x >= -limit && bounds.minimum.y >= -limit && bounds.minimum.z >= -limit
        && bounds.maximum.x <= limit && bounds.maximum.y <= limit && bounds.maximum.z <= limit;
}
} // namespace

std::optional<AssemblyMassPlan> AssemblyMassPlan::compile(
    const BuildSnapshot& input, const PartCatalog& catalog, AssemblyIssue& issue, AssemblyMassLimits limits) {
    const auto refuse = [&](AssemblyError error, DurableId object = {}) -> std::optional<AssemblyMassPlan> {
        issue = {error, object, {}}; return std::nullopt;
    };
    if (limits.parts == 0 || limits.parts > kMaximumBuildParts || limits.roots == 0
        || limits.roots > kMaximumAssemblyRoots || limits.radiusTicks <= 0
        || limits.radiusTicks > kMaximumAssemblyRadiusTicks) return refuse(AssemblyError::InvalidProfile);
    if (input.parts.size() > limits.parts || input.connections.size() > kMaximumBuildConnections)
        return refuse(AssemblyError::Capacity, input.id);
    try {
        BuildIssue buildIssue;
        auto model = BuildModel::create(input, catalog, buildIssue);
        if (!model) { issue = {AssemblyError::InvalidBuild, buildIssue.object, buildIssue}; return std::nullopt; }
        const auto& build = model->snapshot();
        if (build.parts.empty()) return refuse(AssemblyError::EmptyBuild, build.id);
        // Model canonicalization establishes sorted distinct part IDs.
        const auto indexOf = [&](DurableId id) {
            return static_cast<size_t>(std::lower_bound(build.parts.begin(), build.parts.end(), id,
                [](const PartInstance& part, DurableId key) { return part.id < key; }) - build.parts.begin());
        };
        std::array<size_t, kMaximumBuildParts> parent{};
        for (size_t i = 0; i < build.parts.size(); ++i) parent[i] = i;
        const auto representative = [&](size_t index) {
            while (parent[index] != index) index = parent[index];
            return index;
        };
        for (const auto& link : build.connections) {
            if (!link.enabled || link.kind != ConnectionKind::Weld) continue;
            const auto a = representative(indexOf(link.a.part)), b = representative(indexOf(link.b.part));
            parent[std::max(a, b)] = std::min(a, b);
        }
        size_t rootCount = 0;
        for (size_t i = 0; i < build.parts.size(); ++i) if (representative(i) == i) ++rootCount;
        if (rootCount > limits.roots) return refuse(AssemblyError::Capacity, build.id);

        AssemblyMassPlan result;
        result.build_ = build.id; result.revision_ = build.revision;
        result.roots_.reserve(rootCount); result.parts_.reserve(build.parts.size());
        std::array<uint32_t, kMaximumBuildParts> rootIndices{};
        for (size_t i = 0; i < build.parts.size(); ++i) if (representative(i) == i) {
            rootIndices[i] = static_cast<uint32_t>(result.roots_.size());
            result.roots_.push_back({build.parts[i].id, {build.parts[i].placement.translation, {}}, 0, {}});
        }
        std::array<OrientedMass, kMaximumBuildParts> masses{};
        for (size_t i = 0; i < build.parts.size(); ++i) {
            const auto& part = build.parts[i];
            const auto rootIndex = rootIndices[representative(i)];
            auto& root = result.roots_[rootIndex];
            const auto translation = checkedSubtract(part.placement.translation, root.buildFromRoot.translation);
            if (!translation) return refuse(AssemblyError::Extent, part.id);
            const GridTransform frame{*translation, part.placement.rotation};
            const auto& definition = *catalog.lookup(part.definition).definition;
            const auto bounds = transformBounds(frame, definition.footprint);
            if (!bounds || !within(*bounds, limits.radiusTicks)) return refuse(AssemblyError::Extent, part.id);
            result.parts_.push_back({part.id, part.definition, rootIndex, frame});
            masses[i] = orient(definition.mass, frame); ++root.partCount;
        }
        // Two passes around a local anchor avoid subtraction of enormous
        // world-space second moments to recover a small COM-space tensor.
        for (size_t rootIndex = 0; rootIndex < result.roots_.size(); ++rootIndex) {
            Sum mass;
            std::array<Sum, 3> weighted{};
            for (size_t i = 0; i < result.parts_.size(); ++i) if (result.parts_[i].root == rootIndex) {
                const auto& part = masses[i]; mass.add(part.mass);
                for (size_t axis = 0; axis < 3; ++axis) weighted[axis].add(part.mass * part.center[axis]);
            }
            auto& output = result.roots_[rootIndex].mass;
            output.dryMassKg = mass.total();
            Vec center{};
            for (size_t axis = 0; axis < 3; ++axis) center[axis] = weighted[axis].total() / output.dryMassKg;
            output.localCenterOfMass = position(center);
            std::array<Sum, 9> tensor{};
            for (size_t i = 0; i < result.parts_.size(); ++i) if (result.parts_[i].root == rootIndex) {
                const auto& part = masses[i]; Vec r{};
                for (size_t axis = 0; axis < 3; ++axis) r[axis] = part.center[axis] - center[axis];
                for (size_t row = 0; row < 3; ++row) for (size_t col = row; col < 3; ++col) {
                    // Sum the other two squared distances explicitly on the
                    // diagonal; dot(r,r)-r[row]^2 needlessly loses small terms.
                    const double shift = row == col
                        ? r[(row + 1) % 3] * r[(row + 1) % 3] + r[(row + 2) % 3] * r[(row + 2) % 3]
                        : -r[row] * r[col];
                    tensor[row * 3 + col].add(part.inertia[row * 3 + col] + part.mass * shift);
                }
            }
            for (size_t row = 0; row < 3; ++row) for (size_t col = row; col < 3; ++col) {
                const double value = tensor[row * 3 + col].total();
                output.inertia.elements[row * 3 + col] = output.inertia.elements[col * 3 + row] = value == 0.0 ? 0.0 : value;
            }
            if (!validAggregate(output)) return refuse(AssemblyError::InvalidMass, result.roots_[rootIndex].key);
        }
        issue = {}; return result;
    } catch (const std::bad_alloc&) { return refuse(AssemblyError::Capacity, input.id); }
}
} // namespace voxy::game::construction

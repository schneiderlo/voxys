#pragma once

#include "geometry/box_union.hpp"
#include "physics/rigid_mass_frame.hpp"

#include <cstddef>
#include <utility>

namespace voxy::physics {

inline constexpr uint32_t kAuthoredShapeFormatVersion = 1;
inline constexpr double kAuthoredShapeTickMetres = 0.02;
inline constexpr double kAuthoredShapePositionErrorMetres = 0.0001;

// Explicit storage-buffer rows, independent of GLM alignment and material bits.
// Geometry stays in canonical root axes. The solver body is COM/principal axes.
struct alignas(16) PackedShapeMass {
    std::array<float, 4> centerInverseMass{}; // Root COM xyz; inverse mass.
    std::array<float, 4> rootFromBodyQuaternion{}; // x,y,z,w.
    std::array<float, 4> inverseInertiaRadius{}; // Principal inverse inertia; COM radius.
};
struct alignas(16) PackedShapeCell {
    std::array<int32_t, 3> minimum{};
    uint32_t source = 0; // Opaque label scoped by this immutable shape.
    std::array<int32_t, 3> maximum{};
    uint32_t firstFace = 0;
    uint32_t faceCount = 0;
    std::array<uint32_t, 3> reserved{};
};
struct alignas(16) PackedShapeFace {
    std::array<int32_t, 3> minimum{};
    uint32_t source = 0;
    std::array<int32_t, 3> maximum{};
    uint32_t cell = 0;
    uint32_t axis = 0;
    int32_t sign = 0;
    std::array<uint32_t, 2> reserved{};
};
struct alignas(16) PackedShapeNode {
    std::array<int32_t, 3> minimum{};
    uint32_t cell = geometry::kUnionInternalNode;
    std::array<int32_t, 3> maximum{};
    uint32_t escape = 0;
};
static_assert(sizeof(PackedShapeMass) == 48 && sizeof(PackedShapeCell) == 48);
static_assert(sizeof(PackedShapeFace) == 48 && sizeof(PackedShapeNode) == 32);
static_assert(offsetof(PackedShapeCell, firstFace) == 28 && offsetof(PackedShapeCell, faceCount) == 32);
static_assert(offsetof(PackedShapeFace, cell) == 28 && offsetof(PackedShapeFace, axis) == 32);
static_assert(offsetof(PackedShapeNode, escape) == 28);

struct AuthoredShapeLimits {
    uint32_t cells = geometry::kMaximumUnionCells;
    uint32_t faces = geometry::kMaximumUnionFaces;
    uint32_t nodes = 2 * geometry::kMaximumUnionCells - 1;
};
enum class AuthoredShapeError : uint8_t {
    None, InvalidProfile, InvalidGeometry, Capacity, Mass, Unrepresentable, Allocation,
};
struct AuthoredShapeIssue {
    AuthoredShapeError error = AuthoredShapeError::None;
    MassFrameError mass = MassFrameError::None;
};
struct ShapeResourceCost {
    uint32_t cells = 0, faces = 0, nodes = 0;
    uint64_t bytes = 0; // Owned payload capacities plus this shape's fixed record.
    [[nodiscard]] bool operator==(const ShapeResourceCost&) const = default;
};

// One immutable root, constructed only from an already validated exact union.
// Material and game/durable identities are deliberately outside this resource.
// A source label maps through the owning compiled assembly at the same revision.
// Backend support MUST honor exterior patches; raw cells are not convex contacts.
class AuthoredShape {
public:
    [[nodiscard]] static std::optional<AuthoredShape> prepare(
        const geometry::BoxUnion&, const RigidMassInput&, AuthoredShapeIssue&,
        AuthoredShapeLimits = {});
    [[nodiscard]] const RigidMassFrame& massFrame() const noexcept { return mass_; }
    [[nodiscard]] const PackedShapeMass& packedMass() const noexcept { return packed_; }
    [[nodiscard]] geometry::GridBox rootBounds() const noexcept { return bounds_; }
    [[nodiscard]] std::span<const PackedShapeCell> cells() const noexcept { return cells_; }
    [[nodiscard]] std::span<const PackedShapeFace> faces() const noexcept { return faces_; }
    [[nodiscard]] std::span<const PackedShapeNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] ShapeResourceCost cost() const noexcept;
private:
    explicit AuthoredShape(RigidMassFrame mass) : mass_(mass) {}
    RigidMassFrame mass_;
    PackedShapeMass packed_{};
    geometry::GridBox bounds_{};
    std::vector<PackedShapeCell> cells_{};
    std::vector<PackedShapeFace> faces_{};
    std::vector<PackedShapeNode> nodes_{};
};
} // namespace voxy::physics

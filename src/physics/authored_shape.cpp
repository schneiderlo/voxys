#include "physics/authored_shape.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace voxy::physics {
namespace {
constexpr double kFloatEpsilon = static_cast<double>(std::numeric_limits<float>::epsilon());
std::array<int32_t, 3> coordinates(geometry::GridPosition p) noexcept { return {p.x, p.y, p.z}; }

bool positiveFloat(double value, float& output) noexcept {
    if (!std::isfinite(value) || value < static_cast<double>(std::numeric_limits<float>::min())
        || value > static_cast<double>(std::numeric_limits<float>::max())) return false;
    output = static_cast<float>(value);
    // Normal positive values survive devices that flush subnormals to zero.
    return std::isnormal(output) && output > 0.0f;
}

std::array<float, 4> quaternion(const MassMatrix& r) noexcept {
    std::array<double, 4> q{};
    // Choose the largest squared component. This is stable at half turns too.
    const std::array<double, 4> diagonal{
        1+r[0]-r[4]-r[8], 1-r[0]+r[4]-r[8], 1-r[0]-r[4]+r[8], 1+r[0]+r[4]+r[8]};
    size_t axis = 0;
    for (size_t i=1; i<4; ++i) if (diagonal[i]>diagonal[axis]) axis=i;
    q[axis]=0.5*std::sqrt(std::max(0.0,diagonal[axis]));
    const double divisor=4*q[axis];
    if (axis==3) { q[0]=(r[7]-r[5])/divisor; q[1]=(r[2]-r[6])/divisor; q[2]=(r[3]-r[1])/divisor; }
    else {
        const size_t j=(axis+1)%3, k=(axis+2)%3;
        q[j]=(r[j*3+axis]+r[axis*3+j])/divisor;
        q[k]=(r[k*3+axis]+r[axis*3+k])/divisor;
        q[3]=(r[k*3+j]-r[j*3+k])/divisor;
    }
    const double length=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    std::array<float,4> packed{};
    for (size_t i=0; i<4; ++i) packed[i]=static_cast<float>(q[i]/length);
    // Canonical sign after quantization: positive w, then first nonzero x/y/z.
    float first=packed[3];
    if (first==0.0f) for (size_t i=0; i<3; ++i) if (packed[i]!=0.0f) { first=packed[i]; break; }
    for (auto& value:packed) { if (first<0.0f) value=-value; if (value==0.0f) value=0.0f; }
    return packed;
}
MassMatrix matrix(const std::array<float,4>& q) noexcept {
    const double x=static_cast<double>(q[0]),y=static_cast<double>(q[1]),z=static_cast<double>(q[2]),w=static_cast<double>(q[3]);
    return {1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),
        2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),
        2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
}
bool packMass(const RigidMassFrame& mass, geometry::GridBox bounds, PackedShapeMass& packed) noexcept {
    float massKg=0.0f;
    if (!positiveFloat(mass.inverseMass(),packed.centerInverseMass[3])
        || !positiveFloat(1.0/mass.inverseMass(),massKg)) return false;
    if (std::abs(static_cast<double>(massKg)*static_cast<double>(packed.centerInverseMass[3])-1.0)>4*kFloatEpsilon) return false;
    const auto lo=coordinates(bounds.minimum),hi=coordinates(bounds.maximum);
    double radiusSquared=0.0;
    for (size_t i=0; i<3; ++i) {
        const double center=mass.center()[i];
        if (std::abs(center)>geometry::kMaximumUnionRadiusTicks*kAuthoredShapeTickMetres) return false;
        packed.centerInverseMass[i]=static_cast<float>(center);
        if (std::abs(static_cast<double>(packed.centerInverseMass[i])-center)>kAuthoredShapePositionErrorMetres) return false;
        // Bounds also cover the quantized COM, not only its double precursor.
        const double extent=std::max(std::abs(lo[i]*kAuthoredShapeTickMetres-static_cast<double>(packed.centerInverseMass[i])),
            std::abs(hi[i]*kAuthoredShapeTickMetres-static_cast<double>(packed.centerInverseMass[i])));
        radiusSquared+=extent*extent;
        float moment=0.0f;
        if (!positiveFloat(mass.principalInertia()[i],moment)
            || !positiveFloat(mass.principalInverseInertia()[i],packed.inverseInertiaRadius[i])) return false;
        // Gyroscopic code reconstructs moments from the stored reciprocal.
        const float reconstructed=1.0f/packed.inverseInertiaRadius[i];
        if (!std::isnormal(reconstructed) || std::abs(static_cast<double>(reconstructed)/mass.principalInertia()[i]-1.0)>4*kFloatEpsilon)
            return false;
    }
    const double paddedRadius=std::sqrt(radiusSquared)*(1+16*kFloatEpsilon)+2*kAuthoredShapePositionErrorMetres;
    if (!positiveFloat(paddedRadius,packed.inverseInertiaRadius[3])) return false;
    packed.inverseInertiaRadius[3]=std::nextafter(packed.inverseInertiaRadius[3],std::numeric_limits<float>::infinity());
    packed.rootFromBodyQuaternion=quaternion(mass.rootFromBodyRotation());
    const auto r=matrix(packed.rootFromBodyQuaternion);
    double norm=0.0;
    for (auto value:packed.rootFromBodyQuaternion) norm+=static_cast<double>(value)*static_cast<double>(value);
    if (std::abs(norm-1.0)>4*kFloatEpsilon) return false;
    MassMatrix inertia{},inverse{};
    const auto& reference=mass.rootFromBodyRotation();
    const double inertiaScale=mass.principalInertia()[2],inverseScale=mass.principalInverseInertia()[0];
    for (size_t i=0; i<3; ++i) for (size_t j=0; j<3; ++j) {
        if (std::abs(r[i*3+j]-reference[i*3+j])>8*kFloatEpsilon) return false;
        double idealInertia=0.0,idealInverse=0.0;
        for (size_t k=0; k<3; ++k) {
            const double inv=static_cast<double>(packed.inverseInertiaRadius[k]);
            inertia[i*3+j]+=r[i*3+k]*r[j*3+k]/inv;
            inverse[i*3+j]+=r[i*3+k]*r[j*3+k]*inv;
            idealInertia+=reference[i*3+k]*reference[j*3+k]*mass.principalInertia()[k];
            idealInverse+=reference[i*3+k]*reference[j*3+k]*mass.principalInverseInertia()[k];
        }
        if (std::abs(inertia[i*3+j]-idealInertia)>64*kFloatEpsilon*inertiaScale
            || std::abs(inverse[i*3+j]-idealInverse)>64*kFloatEpsilon*inverseScale) return false;
    }
    // Reject quantized frames whose condition magnifies basis error too far.
    for (size_t i=0; i<3; ++i) for (size_t j=0; j<3; ++j) {
        double value=0.0;
        for (size_t k=0; k<3; ++k) value+=inertia[i*3+k]*inverse[k*3+j];
        if (!std::isfinite(value) || std::abs(value-(i==j ? 1.0 : 0.0))>0.001) return false;
    }
    return true;
}
} // namespace

std::optional<AuthoredShape> AuthoredShape::prepare(const geometry::BoxUnion& geometry,
    const RigidMassInput& input, AuthoredShapeIssue& issue, AuthoredShapeLimits limits) {
    const auto refuse=[&](AuthoredShapeError error,MassFrameError mass=MassFrameError::None) -> std::optional<AuthoredShape> {
        issue={error,mass}; return std::nullopt;
    };
    if (limits.cells==0 || limits.cells>geometry::kMaximumUnionCells || limits.faces==0
        || limits.faces>geometry::kMaximumUnionFaces || limits.nodes==0
        || limits.nodes>2*geometry::kMaximumUnionCells-1) return refuse(AuthoredShapeError::InvalidProfile);
    if (geometry.cells().empty() || geometry.faces().empty() || geometry.bvh().empty()) return refuse(AuthoredShapeError::InvalidGeometry);
    if (geometry.cells().size()>limits.cells || geometry.faces().size()>limits.faces || geometry.bvh().size()>limits.nodes)
        return refuse(AuthoredShapeError::Capacity);
    MassFrameError massError;
    const auto mass=RigidMassFrame::prepare(input,massError);
    if (!mass) return refuse(AuthoredShapeError::Mass,massError);
    AuthoredShape result{*mass}; result.bounds_=geometry.bvh()[0].bounds;
    if (!packMass(*mass,result.bounds_,result.packed_)) return refuse(AuthoredShapeError::Unrepresentable);
    try {
        result.cells_.reserve(geometry.cells().size()); result.faces_.reserve(geometry.faces().size()); result.nodes_.reserve(geometry.bvh().size());
        for (const auto& cell:geometry.cells()) result.cells_.push_back({coordinates(cell.bounds.minimum),cell.source,coordinates(cell.bounds.maximum),0,0,{}});
        // BoxUnion emits faces grouped by owning cell. Preserve this ordering,
        // including cells that have no exterior patches (fully enclosed cells).
        for (const auto& face:geometry.faces()) {
            auto& cell=result.cells_[face.cell];
            if (cell.faceCount==0) cell.firstFace=static_cast<uint32_t>(result.faces_.size());
            ++cell.faceCount;
            result.faces_.push_back({coordinates(face.bounds.minimum),face.source,coordinates(face.bounds.maximum),face.cell,face.axis,face.sign,{}});
        }
        for (const auto& node:geometry.bvh()) result.nodes_.push_back({coordinates(node.bounds.minimum),node.cell,coordinates(node.bounds.maximum),node.escape});
    } catch (const std::bad_alloc&) { return refuse(AuthoredShapeError::Allocation); }
    issue={}; return result;
}
ShapeResourceCost AuthoredShape::cost() const noexcept {
    return {static_cast<uint32_t>(cells_.size()),static_cast<uint32_t>(faces_.size()),static_cast<uint32_t>(nodes_.size()),
        sizeof(AuthoredShape)+uint64_t{cells_.capacity()}*sizeof(PackedShapeCell)
            +uint64_t{faces_.capacity()}*sizeof(PackedShapeFace)+uint64_t{nodes_.capacity()}*sizeof(PackedShapeNode)};
}
} // namespace voxy::physics

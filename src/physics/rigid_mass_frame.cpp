#include "physics/rigid_mass_frame.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace voxy::physics {
namespace {
constexpr MassMatrix kIdentity{1,0,0,0,1,0,0,0,1};
constexpr double kEpsilon = std::numeric_limits<double>::epsilon();
constexpr double kSmallestRelativeMoment = 1.0e-12;
MassVector product(const MassMatrix& matrix, MassVector vector) noexcept {
    MassVector result{};
    for (size_t row=0;row<3;++row) for (size_t column=0;column<3;++column) result[row] += matrix[row*3+column]*vector[column];
    return result;
}
MassMatrix product(const MassMatrix& a, const MassMatrix& b) noexcept {
    MassMatrix result{};
    for (size_t row=0;row<3;++row) for (size_t column=0;column<3;++column)
        for (size_t k=0;k<3;++k) result[row*3+column] += a[row*3+k]*b[k*3+column];
    return result;
}
MassMatrix transpose(const MassMatrix& matrix) noexcept {
    return {matrix[0],matrix[3],matrix[6],matrix[1],matrix[4],matrix[7],matrix[2],matrix[5],matrix[8]};
}
MassVector add(MassVector a, MassVector b) noexcept { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
MassVector subtract(MassVector a, MassVector b) noexcept { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
MassVector cross(MassVector a, MassVector b) noexcept { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double determinant(const MassMatrix& a) noexcept {
    return a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
}
}

std::optional<RigidMassFrame> RigidMassFrame::prepare(const RigidMassInput& input, MassFrameError& error) noexcept {
    const auto refuse = [&](MassFrameError reason) -> std::optional<RigidMassFrame> { error=reason; return std::nullopt; };
    if (!std::isfinite(input.massKg)) return refuse(MassFrameError::NonFinite);
    if (input.massKg<=0.0 || !std::isfinite(1.0/input.massKg)) return refuse(MassFrameError::InvalidMass);
    for (auto value:input.rootCenterOfMass) if (!std::isfinite(value)) return refuse(MassFrameError::NonFinite);
    double scale=0.0;
    for (auto value:input.inertiaAboutCenter) {
        if (!std::isfinite(value)) return refuse(MassFrameError::NonFinite);
        scale=std::max(scale,std::abs(value));
    }
    if (scale==0.0) return refuse(MassFrameError::IllConditioned);
    MassMatrix original{};
    for (size_t row=0;row<3;++row) for (size_t column=row;column<3;++column) {
        const double a=input.inertiaAboutCenter[row*3+column]/scale, b=input.inertiaAboutCenter[column*3+row]/scale;
        if (std::abs(a-b)>64.0*kEpsilon) return refuse(MassFrameError::Asymmetric);
        original[row*3+column]=original[column*3+row]=(a+b)*0.5;
    }
    auto matrix=original, vectors=kIdentity;
    uint32_t rotations=0;
    constexpr std::array<std::array<size_t,2>,3> pairs{{{0,1},{0,2},{1,2}}};
    // Fixed cyclic pair ordering and at most 48 Jacobi rotations. No dynamic
    // workspace, backend state, sorting allocation or convergence-sized loop.
    for (uint32_t sweep=0;sweep<16;++sweep) {
        bool changed=false;
        for (const auto pair:pairs) {
            const size_t p=pair[0],q=pair[1]; const double off=matrix[p*3+q];
            if (std::abs(off)<=8.0*kEpsilon) continue;
            const double tau=(matrix[q*3+q]-matrix[p*3+p])/(2.0*off);
            const double tangent=std::copysign(1.0,tau)/(std::abs(tau)+std::hypot(1.0,tau));
            const double cosine=1.0/std::sqrt(1.0+tangent*tangent), sine=tangent*cosine;
            matrix[p*3+p]-=tangent*off; matrix[q*3+q]+=tangent*off; matrix[p*3+q]=matrix[q*3+p]=0.0;
            for (size_t k=0;k<3;++k) {
                if (k!=p && k!=q) {
                    const double a=matrix[k*3+p],b=matrix[k*3+q];
                    matrix[k*3+p]=matrix[p*3+k]=cosine*a-sine*b;
                    matrix[k*3+q]=matrix[q*3+k]=sine*a+cosine*b;
                }
                const double a=vectors[k*3+p],b=vectors[k*3+q];
                vectors[k*3+p]=cosine*a-sine*b; vectors[k*3+q]=sine*a+cosine*b;
            }
            changed=true; ++rotations;
        }
        if (!changed) break;
    }
    if (std::max({std::abs(matrix[1]),std::abs(matrix[2]),std::abs(matrix[5])})>32.0*kEpsilon)
        return refuse(MassFrameError::NonConvergent);
    std::array<size_t,3> order{0,1,2};
    // Stable three-element insertion sort makes exact eigenvalue ties explicit.
    for (size_t i=1;i<3;++i) for (size_t j=i;j>0 && matrix[order[j]*4]<matrix[order[j-1]*4];--j) std::swap(order[j],order[j-1]);
    const double minimum=matrix[order[0]*4],middle=matrix[order[1]*4],maximum=matrix[order[2]*4];
    if (minimum<0.0 || maximum<=0.0 || maximum>minimum+middle+128.0*kEpsilon) return refuse(MassFrameError::NonPhysical);
    if (minimum<=maximum*kSmallestRelativeMoment) return refuse(MassFrameError::IllConditioned);
    RigidMassFrame result; result.center_=input.rootCenterOfMass; result.inverseMass_=1.0/input.massKg;
    result.diagnostics_.rotations=rotations;
    for (size_t column=0;column<3;++column) {
        const auto source=order[column];
        result.inertia_[column]=matrix[source*4]*scale;
        result.inverseInertia_[column]=1.0/result.inertia_[column];
        if (!std::isfinite(result.inertia_[column]) || result.inertia_[column]<=0.0
            || !std::isfinite(result.inverseInertia_[column]) || result.inverseInertia_[column]<=0.0)
            return refuse(MassFrameError::IllConditioned);
        size_t largest=0;
        for (size_t row=1;row<3;++row) if (std::abs(vectors[row*3+source])>std::abs(vectors[largest*3+source])) largest=row;
        const double sign=vectors[largest*3+source]<0.0 ? -1.0 : 1.0;
        for (size_t row=0;row<3;++row) result.rotation_[row*3+column]=sign*vectors[row*3+source];
    }
    // The first two canonical axis signs fix the third's handedness.
    if (determinant(result.rotation_)<0.0) for (size_t row=0;row<3;++row) result.rotation_[row*3+2]=-result.rotation_[row*3+2];
    const auto orthogonal=product(transpose(result.rotation_),result.rotation_);
    for (size_t row=0;row<3;++row) for (size_t column=0;column<3;++column) {
        double reconstructed=0.0;
        for (size_t k=0;k<3;++k) reconstructed+=result.rotation_[row*3+k]*matrix[order[k]*4]*result.rotation_[column*3+k];
        result.diagnostics_.normalizedReconstructionError=std::max(result.diagnostics_.normalizedReconstructionError,
            std::abs(reconstructed-original[row*3+column]));
        result.diagnostics_.orthogonalityError=std::max(result.diagnostics_.orthogonalityError,
            std::abs(orthogonal[row*3+column]-kIdentity[row*3+column]));
    }
    if (result.diagnostics_.normalizedReconstructionError>1024.0*kEpsilon || result.diagnostics_.orthogonalityError>1024.0*kEpsilon
        || std::abs(determinant(result.rotation_)-1.0)>1024.0*kEpsilon) return refuse(MassFrameError::NonConvergent);
    error=MassFrameError::None; return result;
}

MassVector RigidMassFrame::rootVector(MassVector value) const noexcept { return product(rotation_,value); }
MassVector RigidMassFrame::bodyVector(MassVector value) const noexcept { return product(transpose(rotation_),value); }
MassVector RigidMassFrame::rootPoint(MassVector value) const noexcept { return add(center_,rootVector(value)); }
MassVector RigidMassFrame::bodyPoint(MassVector value) const noexcept { return bodyVector(subtract(value,center_)); }
MassPose RigidMassFrame::bodyPose(const MassPose& pose) const noexcept {
    return {add(pose.position,product(pose.orientation,center_)),product(pose.orientation,rotation_)};
}
MassPose RigidMassFrame::rootPose(const MassPose& pose) const noexcept {
    const auto orientation=product(pose.orientation,transpose(rotation_));
    return {subtract(pose.position,product(orientation,center_)),orientation};
}
MassVector RigidMassFrame::centerVelocity(MassVector velocity,MassVector angular,const MassMatrix& orientation) const noexcept {
    return add(velocity,cross(angular,product(orientation,center_)));
}
MassVector RigidMassFrame::rootVelocity(MassVector velocity,MassVector angular,const MassMatrix& orientation) const noexcept {
    return subtract(velocity,cross(angular,product(orientation,center_)));
}
MassVector RigidMassFrame::angularMomentumRoot(MassVector velocity) const noexcept {
    auto body=bodyVector(velocity); for (size_t i=0;i<3;++i) body[i]*=inertia_[i]; return rootVector(body);
}
MassVector RigidMassFrame::angularResponseRoot(MassVector torque) const noexcept {
    auto body=bodyVector(torque); for (size_t i=0;i<3;++i) body[i]*=inverseInertia_[i]; return rootVector(body);
}
MassVelocityDelta RigidMassFrame::impulseAtRootPoint(MassVector impulse,MassVector point) const noexcept {
    auto linear=impulse; for (auto& value:linear) value*=inverseMass_;
    return {linear,angularResponseRoot(cross(subtract(point,center_),impulse))};
}
} // namespace voxy::physics

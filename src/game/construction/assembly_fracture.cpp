#include "game/construction/assembly_fracture.hpp"

#include <algorithm>
#include <cmath>
#include <new>

namespace voxy::game::construction {
namespace {
using Vector = physics::MassVector;
using Matrix = physics::MassMatrix;
Vector rotate(const Matrix& matrix, Vector value) noexcept {
    Vector out{};
    for(size_t row=0;row<3;++row)for(size_t col=0;col<3;++col)out[row]+=matrix[3*row+col]*value[col];
    return out;
}
Vector cross(Vector a, Vector b) noexcept {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
bool finite(Vector v) noexcept {return std::all_of(v.begin(),v.end(),[](double n){return std::isfinite(n);});}
bool orientation(const Matrix& m) noexcept {
    if(!std::all_of(m.begin(),m.end(),[](double n){return std::isfinite(n);}))return false;
    for(size_t a=0;a<3;++a)for(size_t b=a;b<3;++b) {
        double dot=0;for(size_t i=0;i<3;++i)dot+=m[3*i+a]*m[3*i+b];
        if(!std::isfinite(dot)||std::abs(dot-(a==b?1.:0.))>1e-9)return false;
    }
    const double determinant=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    return std::isfinite(determinant)&&std::abs(determinant-1.)<=1e-9;
}
bool motion(const AssemblyRootMotion& v) noexcept {
    return finite(v.worldFromRoot.position)&&orientation(v.worldFromRoot.orientation)
        &&finite(v.originVelocity)&&finite(v.angularVelocity);
}
}

std::optional<AssemblyFracturePlan> AssemblyFracturePlan::prepare(const BuildSnapshot& input,
    TopologyRevision expected,std::span<const DurableId> requested,const PartCatalog& catalog,
    AssemblyFractureIssue& issue,AssemblyCompileProfile profile) {
    const auto refuse=[&](AssemblyFractureError error,DurableId object={}) -> std::optional<AssemblyFracturePlan> {
        issue={error,object,{},{}};return {};
    };
    if(requested.empty()||requested.size()>kMaximumBuildConnections)return refuse(AssemblyFractureError::InvalidRequest);
    if(input.revision!=expected)return refuse(AssemblyFractureError::StaleRevision,input.id);
    if(expected.value()==std::numeric_limits<uint64_t>::max())return refuse(AssemblyFractureError::RevisionExhausted,input.id);
    try {
        BuildIssue buildIssue;auto model=BuildModel::create(input,catalog,buildIssue);
        if(!model){issue={AssemblyFractureError::InvalidBuild,buildIssue.object,buildIssue,{}};return {};}
        std::vector<DurableId> cuts(requested.begin(),requested.end());std::sort(cuts.begin(),cuts.end());
        if(std::adjacent_find(cuts.begin(),cuts.end())!=cuts.end())return refuse(AssemblyFractureError::InvalidRequest);
        AssemblyFunctionIssue compileIssue;
        auto before=CompiledAssembly::compile(model->snapshot(),catalog,compileIssue,profile);
        if(!before){issue={AssemblyFractureError::Compilation,input.id,{},compileIssue};return {};}
        buildIssue=model->cutWelds(expected,cuts,catalog);
        if(buildIssue) {
            const auto error=buildIssue.field=="cut.unknown"?AssemblyFractureError::UnknownConnection
                :buildIssue.field=="cut.kind"?AssemblyFractureError::UnsupportedConnection
                :buildIssue.field=="cut.inactive"?AssemblyFractureError::InactiveConnection:AssemblyFractureError::InvalidBuild;
            issue={error,buildIssue.object,buildIssue,{}};return {};
        }
        auto after=CompiledAssembly::compile(model->snapshot(),catalog,compileIssue,profile);
        if(!after){issue={AssemblyFractureError::Compilation,input.id,{},compileIssue};return {};}
        std::vector<AssemblyFragmentBinding> fragments(after->mass().roots().size());
        std::array<bool,kMaximumAssemblyRoots> assigned{};
        const auto oldParts=before->mass().parts();
        for(const auto& part:after->mass().parts()) {
            const auto old=std::lower_bound(oldParts.begin(),oldParts.end(),part.part,
                [](const AssemblyMassPart& p,DurableId id){return p.part<id;});
            if(old==oldParts.end()||old->part!=part.part)return refuse(AssemblyFractureError::InvalidMapping,part.part);
            auto& binding=fragments[part.root];
            if(assigned[part.root]&&binding.parentRoot!=old->root)return refuse(AssemblyFractureError::InvalidMapping,part.part);
            binding.parentRoot=old->root;assigned[part.root]=true;
        }
        for(size_t i=0;i<fragments.size();++i) {
            if(!assigned[i])return refuse(AssemblyFractureError::InvalidMapping);
            const auto old=before->mass().roots()[fragments[i].parentRoot].buildFromRoot;
            const auto next=after->mass().roots()[i].buildFromRoot;
            if(old.rotation!=CubeRotation{}||next.rotation!=CubeRotation{})return refuse(AssemblyFractureError::InvalidMapping);
            const auto offset=checkedSubtract(next.translation,old.translation);
            if(!offset)return refuse(AssemblyFractureError::InvalidMapping);
            fragments[i].parentFromRoot={offset->x*.02,offset->y*.02,offset->z*.02};
        }
        auto build=model->snapshot();issue={};
        return AssemblyFracturePlan(std::move(build),std::move(*before),std::move(*after),std::move(cuts),std::move(fragments));
    }catch(const std::bad_alloc&){return refuse(AssemblyFractureError::Capacity,input.id);}
}

std::optional<std::vector<AssemblyRootMotion>> AssemblyFracturePlan::inheritMotion(
    const AssemblyMotionSource& source,SimulationTick expectedTick,AssemblyFractureIssue& issue) const {
    const auto refuse=[&](AssemblyFractureError error) -> std::optional<std::vector<AssemblyRootMotion>> {
        issue={error,source.build,{},{}};return {};
    };
    if(source.build!=before_.mass().build()||source.revision!=before_.mass().revision()
        ||source.completedTick!=expectedTick)return refuse(AssemblyFractureError::MotionIdentity);
    const auto oldRoots=before_.mass().roots();
    if(source.roots.size()!=oldRoots.size())return refuse(AssemblyFractureError::MotionRoots);
    std::array<const AssemblyRootMotion*,kMaximumAssemblyRoots> roots{};
    for(const auto& state:source.roots) {
        const auto found=std::lower_bound(oldRoots.begin(),oldRoots.end(),state.root,
            [](const AssemblyMassRoot& root,DurableId id){return root.key<id;});
        if(found==oldRoots.end()||found->key!=state.root)return refuse(AssemblyFractureError::MotionRoots);
        const auto index=static_cast<size_t>(found-oldRoots.begin());
        if(roots[index])return refuse(AssemblyFractureError::MotionRoots);
        if(!motion(state))return refuse(AssemblyFractureError::InvalidMotion);
        roots[index]=&state;
    }
    try {
        std::vector<AssemblyRootMotion> result;result.reserve(fragments_.size());
        for(size_t i=0;i<fragments_.size();++i) {
            const auto& binding=fragments_[i];const auto& parent=*roots[binding.parentRoot];
            const auto offset=rotate(parent.worldFromRoot.orientation,binding.parentFromRoot);
            const auto velocity=cross(parent.angularVelocity,offset);
            auto child=parent;child.root=after_.mass().roots()[i].key;
            for(size_t axis=0;axis<3;++axis) {
                child.worldFromRoot.position[axis]+=offset[axis];child.originVelocity[axis]+=velocity[axis];
            }
            if(!finite(offset)||!finite(child.worldFromRoot.position)||!finite(child.originVelocity))
                return refuse(AssemblyFractureError::InvalidMotion);
            result.push_back(child);
        }
        issue={};return result;
    }catch(const std::bad_alloc&){return refuse(AssemblyFractureError::Capacity);}
}
} // namespace voxy::game::construction

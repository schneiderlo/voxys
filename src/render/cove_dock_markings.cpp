#include "render/cove_dock_markings.hpp"
#include "render/mesh_path.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace voxy::render {
namespace {
namespace c = game::construction;
struct Rect { double x0, z0, x1, z1; };
struct Box { glm::dvec3 low, high; };
struct Plate { uint32_t source; Box bounds; std::vector<Rect> sockets; };
using Polygon = std::vector<glm::dvec2>; // X,Z, clockwise when seen from +Y.

bool finite(glm::dvec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
bool overlap(Rect a, Rect b) {
    return a.x0 < b.x1 && a.x1 > b.x0 && a.z0 < b.z1 && a.z1 > b.z0;
}
Rect expanded(Rect a, double d) { return {a.x0-d,a.z0-d,a.x1+d,a.z1+d}; }
bool projectedBox(c::GridTransform frame, c::GridBox box, Box& result) {
    result.low = glm::dvec3(std::numeric_limits<double>::infinity());
    result.high = -result.low;
    for (int i=0;i<8;++i) {
        const c::GridPosition p{(i&1)?box.maximum.x:box.minimum.x,
            (i&2)?box.maximum.y:box.minimum.y,(i&4)?box.maximum.z:box.minimum.z};
        const auto transformed=c::transformPosition(frame,p);
        if (!transformed) return false;
        const auto q=glm::dvec3(transformed->x,transformed->y,transformed->z)/double(c::kTicksPerMetre);
        result.low=glm::min(result.low,q); result.high=glm::max(result.high,q);
    }
    return true;
}
Rect projected(const Box& b) { return {b.low.x,b.low.z,b.high.x,b.high.z}; }

// Sutherland-Hodgman clipping retains complete polygons rather than checking
// vertices only: no triangle can bridge a removed socket or panel channel.
Polygon clip(Polygon p, Rect r) {
    for (int side=0;side<4 && !p.empty();++side) {
        const int axis=side/2;
        const double edge=side==0?r.x0:side==1?r.x1:side==2?r.z0:r.z1;
        const auto inside=[&](glm::dvec2 v){return (side%2==0)?v[axis]>=edge:v[axis]<=edge;};
        Polygon next;
        auto a=p.back(); bool inA=inside(a);
        for (const auto b:p) {
            const bool inB=inside(b);
            if (inA!=inB) next.push_back(a+(b-a)*((edge-a[axis])/(b[axis]-a[axis])));
            if (inB) next.push_back(b);
            a=b; inA=inB;
        }
        p=std::move(next);
    }
    return p;
}
std::vector<Rect> subtract(std::vector<Rect> regions, Rect hole) {
    std::vector<Rect> next;
    for (const auto r:regions) {
        if (!overlap(r,hole)) { next.push_back(r); continue; }
        const Rect i{std::max(r.x0,hole.x0),std::max(r.z0,hole.z0),
            std::min(r.x1,hole.x1),std::min(r.z1,hole.z1)};
        for (const auto piece : {Rect{r.x0,r.z0,i.x0,r.z1},Rect{i.x1,r.z0,r.x1,r.z1},
            Rect{i.x0,r.z0,i.x1,i.z0},Rect{i.x0,i.z1,i.x1,r.z1}})
            if (piece.x1-piece.x0>1e-7 && piece.z1-piece.z0>1e-7) next.push_back(piece);
    }
    return next;
}
Polygon rectangle(Rect r) { return {{r.x0,r.z0},{r.x0,r.z1},{r.x1,r.z1},{r.x1,r.z0}}; }

struct Geometry {
    std::vector<moto::VmeshVertex> vertices;
    std::array<std::vector<uint32_t>,2> indices;
    bool polygon(const Polygon& p, double y, uint32_t material) {
        if (p.size()<3) return true;
        double area=0;
        for (size_t i=0;i<p.size();++i) area+=p[i].x*p[(i+1)%p.size()].y-p[i].y*p[(i+1)%p.size()].x;
        if (std::abs(area)<1e-8) return true;
        // A constant hard cap bounds construction as well as final admission.
        if (vertices.size()+p.size()>200) return false;
        const auto first=static_cast<uint32_t>(vertices.size());
        for (auto v:p) {
            moto::VmeshVertex out{};
            out.position[0]=static_cast<float>(v.x); out.position[1]=static_cast<float>(y);
            out.position[2]=static_cast<float>(v.y); out.normal[1]=1;
            out.tangent[0]=1; out.tangent[3]=1;
            vertices.push_back(out);
        }
        for (uint32_t i=1;i+1<p.size();++i) {
            indices[material].push_back(first);
            indices[material].push_back(first+(area<0?i:i+1));
            indices[material].push_back(first+(area<0?i+1:i));
        }
        return true;
    }
};
} // namespace

bool prepareCoveDockMarkingMesh(const moto::VmeshData& mesh,
    game::assets::RigidPrefab& output, std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    if (mesh.nodes.size()!=1 || mesh.nodes[0].parent!=-1 || mesh.nodes[0].meshIndex!=0
        || mesh.nodes[0].skinIndex!=-1 || mesh.header.meshCount!=1 || !mesh.images.empty()
        || mesh.materials.empty() || mesh.materials.size()>CoveDockMarkings::maximumDraws
        || !mesh.skins.empty() || !mesh.joints.empty() || !mesh.anims.empty()
        || !mesh.animChannels.empty() || !mesh.channelData.empty() || mesh.stringBlob.size()>128)
        return fail("dock markings require one unskinned texture-free mesh");
    const auto& node=mesh.nodes[0];
    for (int i=0;i<3;++i) if (node.translation[i]!=0 || node.scale[i]!=1 || node.rotation[i]!=0)
        return fail("dock marking root must be identity");
    if (node.rotation[3]!=1) return fail("dock marking root must be identity");
    for (const auto& material:mesh.materials) {
        if (material.unlit || material.alphaMode!=moto::VmeshAlphaOpaque || material.baseColorFactor[3]!=1)
            return fail("dock markings must be lit and opaque");
        for (auto texture:material.hasTexture) if (texture) return fail("dock markings cannot add textures");
    }
    game::assets::RigidPrefabLimits limits;
    limits.maximumNodes=limits.maximumMeshes=limits.maximumMeshInstances=1;
    limits.maximumSubmeshes=limits.maximumMaterials=limits.maximumExpandedDraws=CoveDockMarkings::maximumDraws;
    limits.maximumVertices=200; limits.maximumIndices=600;
    limits.maximumGpuBytes=CoveDockMarkings::maximumGpuBytes;
    limits.maximumDecodedBytes=32u*1024u;
    return game::assets::prepareRigidPrefab(mesh,{},limits,output,error);
}

bool makeCoveDockMarkings(const game::assets::LoadedAssetFixture& installed,
    CoveDockMarkings& output, std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    const auto& registry=installed.registry;
    if (!registry.navigation || registry.placements.size()>game::assets::kMaximumFixturePlacements)
        return fail("dock markings need bounded installed Cove navigation");
    const auto& nav=*registry.navigation;
    if (!finite(nav.spawn) || !finite(nav.dockBoarding) || !finite(nav.boatBoarding)
        || std::abs(nav.spawn.y-nav.dockBoarding.y)>1e-6)
        return fail("dock marking navigation is not on one level");
    const auto excluded=[&](uint32_t i){return std::find(nav.boatPlacements.begin(),nav.boatPlacements.end(),i)!=nav.boatPlacements.end()
        || std::find(nav.cargoPlacements.begin(),nav.cargoPlacements.end(),i)!=nav.cargoPlacements.end();};
    std::vector<Plate> plates;
    std::vector<Rect> obstacles;
    for (uint32_t i=0;i<registry.placements.size();++i) {
        const auto& placement=registry.placements[i];
        if (excluded(i) || placement.prototype) continue;
        if (placement.bundleIndex>=installed.bundles.size() || !installed.bundles[placement.bundleIndex])
            return fail("dock marking source references an unavailable canonical bundle");
        const auto& part=installed.bundles[placement.bundleIndex]->sidecar().part;
        Box bounds;
        if (!projectedBox(placement.placement,part.footprint,bounds)) return fail("dock source transform overflow");
        if (part.nameKey=="salvage.part.plate") {
            // Narrow supported surface: canonical four-by-two stud plate.
            // This also keeps the existing molded one-metre panel channels clear.
            if (placement.placement.rotation.value!=0 || glm::any(glm::greaterThan(glm::abs(bounds.high-bounds.low-glm::dvec3(4,.32,2)),glm::dvec3(1e-6)))
                || std::abs(bounds.high.y-nav.spawn.y)>1e-6)
                return fail("dock markings require upright coplanar four-by-two plates");
            Plate plate{i,bounds,{}};
            for (const auto& socket:part.sockets) {
                const auto frame=c::compose(placement.placement,socket.frame); Box clearance;
                if (!frame || !projectedBox(*frame,socket.clearance,clearance)) return fail("dock socket transform overflow");
                if (clearance.high.y>=bounds.high.y-1e-6 && clearance.low.y<=bounds.high.y+.01)
                    plate.sockets.push_back(expanded(projected(clearance),CoveDockMarkings::socketMargin));
            }
            plates.push_back(std::move(plate));
        } else for (const auto& shape:part.collision) {
            const auto frame=c::compose(placement.placement,shape.frame); Box collision;
            const auto h=shape.halfExtents;
            if (!frame || !projectedBox(*frame,{{-h.x,-h.y,-h.z},h},collision)) return fail("dock obstacle transform overflow");
            if (collision.high.y>nav.spawn.y+.01 && collision.low.y<nav.spawn.y+1.7)
                obstacles.push_back(projected(collision));
        }
    }
    if (plates.empty() || plates.size()>16) return fail("dock marking plate count is unsupported");
    // No overlapping source surfaces: otherwise clipping would draw twice.
    for (size_t i=0;i<plates.size();++i) for (size_t j=0;j<i;++j)
        if (overlap(projected(plates[i].bounds),projected(plates[j].bounds))) return fail("dock plates overlap");
    const double direction=nav.dockBoarding.z<nav.spawn.z?-1.0:1.0;
    const double laneX=nav.dockBoarding.x;
    const double turnZ=nav.spawn.z+direction*.5;
    if (std::abs(nav.dockBoarding.z-nav.spawn.z)<3.5 || std::abs(nav.dockBoarding.z-nav.spawn.z)>12
        || std::abs(laneX-nav.spawn.x)<.5 || std::abs(laneX-nav.spawn.x)>3
        || std::abs(nav.boatBoarding.z-nav.dockBoarding.z)>.25)
        return fail("dock marking navigation requires the short side-boarding route");
    const std::array<glm::dvec2,4> route{{{nav.spawn.x,nav.spawn.z},{nav.spawn.x,turnZ},{laneX,turnZ},{laneX,nav.dockBoarding.z}}};
    for (size_t segment=1;segment<route.size();++segment) {
        const auto a=route[segment-1],b=route[segment];
        const Rect swept{std::min(a.x,b.x)-.30,std::min(a.y,b.y)-.30,std::max(a.x,b.x)+.30,std::max(a.y,b.y)+.30};
        for (auto obstacle:obstacles) if (overlap(swept,obstacle)) return fail("dock marking route is blocked by static collision");
        // Exact interval coverage by the union of source plate rectangles;
        // sampling would accept a gap smaller than its sample spacing.
        std::vector<std::pair<double,double>> intervals;
        const bool xAxis=a.x!=b.x;
        for (const auto& plate:plates) {
            const auto r=projected(plate.bounds);
            if (xAxis ? (a.y>=r.z0+.30 && a.y<=r.z1-.30) : (a.x>=r.x0+.30 && a.x<=r.x1-.30))
                intervals.emplace_back(xAxis?r.x0:r.z0,xAxis?r.x1:r.z1);
        }
        std::sort(intervals.begin(),intervals.end());
        double covered=std::min(xAxis?a.x:a.y,xAxis?b.x:b.y);
        const double end=std::max(xAxis?a.x:a.y,xAxis?b.x:b.y);
        for (const auto [lo,hi]:intervals) if (lo<=covered+1e-8 && hi>covered) covered=hi;
        if (covered<end-1e-8) return fail("dock marking route crosses a plate gap");
    }
    std::array<std::vector<Polygon>,2> marks;
    const double endZ=nav.dockBoarding.z-direction*.45;
    marks[0].push_back(rectangle({std::min(nav.spawn.x,laneX)-.07,turnZ-.07,std::max(nav.spawn.x,laneX)+.07,turnZ+.07}));
    marks[0].push_back(rectangle({laneX-.07,std::min(turnZ+direction*.07,endZ),laneX+.07,std::max(turnZ+direction*.07,endZ)}));
    const double z=nav.dockBoarding.z,x=laneX,half=.43,stroke=.07;
    for (const auto r:{Rect{x-half,z-half,x+half,z-half+stroke},Rect{x-half,z+half-stroke,x+half,z+half},
        Rect{x-half,z-half+stroke,x-half+stroke,z+half-stroke},Rect{x+half-stroke,z-half+stroke,x+half,z+half-stroke}})
        marks[1].push_back(rectangle(r));
    for (double progress:{1.0,3.0}) {
        const double center=nav.spawn.z+direction*progress;
        marks[1].push_back({{x-.14,center-direction*.16},{x+.14,center-direction*.16},{x,center+direction*.16}});
    }
    Geometry geometry;
    CoveDockMarkings pending;
    for (const auto& plate:plates) {
        pending.dockPlacements.push_back(plate.source);
        // Inset each one-metre molded panel by 40 mm. This clears its real
        // recessed cross channels and bevels as well as the plate perimeter.
        for (int ix=0;ix<4;++ix) for (int iz=0;iz<2;++iz) {
            const double x0=plate.bounds.low.x+ix,z0=plate.bounds.low.z+iz;
            std::vector<Rect> safe{{x0+CoveDockMarkings::edgeInset,z0+CoveDockMarkings::edgeInset,
                x0+1-CoveDockMarkings::edgeInset,z0+1-CoveDockMarkings::edgeInset}};
            for (const auto socket:plate.sockets) safe=subtract(std::move(safe),socket);
            for (const auto obstacle:obstacles) safe=subtract(std::move(safe),expanded(obstacle,.02));
            for (uint32_t material=0;material<2;++material) {
                auto regions=safe;
                if (material==0) for (const auto& orange:marks[1]) {
                    Rect bounds{orange[0].x,orange[0].y,orange[0].x,orange[0].y};
                    for (const auto p:orange) {
                        bounds.x0=std::min(bounds.x0,p.x); bounds.x1=std::max(bounds.x1,p.x);
                        bounds.z0=std::min(bounds.z0,p.y); bounds.z1=std::max(bounds.z1,p.y);
                    }
                    // Same-plane colors never overlap: a 10 mm neutral gap
                    // around the orange arrow also makes its direction legible.
                    regions=subtract(std::move(regions),expanded(bounds,.01));
                }
                for (const auto& polygon:marks[material]) for (auto region:regions)
                    if (!geometry.polygon(clip(polygon,region),plate.bounds.high.y+CoveDockMarkings::surfaceLift,material))
                        return fail("dock marking geometry exceeds its fixed vertex budget");
            }
        }
    }
    if (geometry.indices[0].empty() || geometry.indices[1].empty()) return fail("dock marking pattern has no safe surface");
    auto& mesh=pending.mesh;
    mesh.header.vertexCount=static_cast<uint32_t>(geometry.vertices.size());
    mesh.header.indexStride=4; mesh.header.submeshCount=mesh.header.materialCount=2;
    mesh.header.meshCount=mesh.header.nodeCount=1;
    mesh.vertices.resize(geometry.vertices.size()*sizeof(moto::VmeshVertex));
    std::memcpy(mesh.vertices.data(),geometry.vertices.data(),mesh.vertices.size());
    for (uint32_t material=0;material<2;++material) {
        const auto& indices=geometry.indices[material];
        mesh.submeshes.push_back({static_cast<uint32_t>(mesh.indices.size()),static_cast<uint32_t>(indices.size()),material,0});
        const auto bytes=indices.size()*sizeof(uint32_t),offset=mesh.indices.size();
        mesh.indices.resize(offset+bytes); std::memcpy(mesh.indices.data()+offset,indices.data(),bytes);
        mesh.header.indexCount+=static_cast<uint32_t>(indices.size());
        auto& m=mesh.materials.emplace_back();
        const auto color=opaqueSrgbPaintOverride(material==0?std::array<uint8_t,4>{25,125,134,255}:std::array<uint8_t,4>{237,121,66,255});
        for (int channel=0;channel<3;++channel) m.baseColorFactor[channel]=color[channel];
        m.roughnessFactor=.42f; m.metallicFactor=0; m.doubleSided=1;
    }
    mesh.nodes.emplace_back(); mesh.nodes[0].meshIndex=0;
    if (!prepareCoveDockMarkingMesh(mesh,pending.prefab,error)) return false;
    output=std::move(pending); error.clear(); return true;
}
} // namespace voxy::render

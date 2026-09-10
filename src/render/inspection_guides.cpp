#include "render/inspection_guides.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::render {
namespace {
namespace construction = game::construction;
constexpr glm::vec4 red{1.0f,.08f,.06f,1}, green{.12f,1.0f,.15f,1}, blue{.08f,.3f,1.0f,1};
constexpr glm::vec4 amber{1.0f,.55f,.06f,1}, white{.95f,.95f,.95f,1};
constexpr double thickness = .012;

glm::dvec3 metres(construction::GridPosition p) {
    return glm::dvec3(p.x,p.y,p.z) / double{construction::kTicksPerMetre};
}
bool finite(const glm::dmat4& matrix) {
    for (glm::length_t c=0; c<4; ++c) for (glm::length_t r=0; r<4; ++r)
        if (!std::isfinite(matrix[c][r])) return false;
    return true;
}
bool finite(glm::dvec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool rigid(const glm::dmat4& matrix) {
    if (!finite(matrix) || matrix[0][3] != 0 || matrix[1][3] != 0
        || matrix[2][3] != 0 || matrix[3][3] != 1) return false;
    const glm::dmat3 basis(matrix), gram = glm::transpose(basis) * basis;
    if (std::abs(glm::determinant(basis) - 1.0) > 1e-10) return false;
    for (glm::length_t c=0; c<3; ++c) for (glm::length_t r=0; r<3; ++r)
        if (std::abs(gram[c][r] - (c==r ? 1.0 : 0.0)) > 1e-10) return false;
    return true;
}
bool gridMatrix(construction::GridTransform value, glm::dmat4& output) {
    const auto rotation = construction::rotationMatrix(value.rotation);
    if (!rotation || !construction::isValid(value.translation)) return false;
    output = glm::dmat4(1.0);
    for (glm::length_t c=0; c<3; ++c) for (glm::length_t r=0; r<3; ++r)
        output[c][r] = rotation->elements[static_cast<size_t>(r*3+c)];
    output[3] = glm::dvec4(metres(value.translation), 1.0);
    return true;
}

struct Builder {
    size_t maximum;
    std::vector<InspectionGuideBox> boxes;
    bool line(const glm::dmat4& frame, glm::dvec3 a, glm::dvec3 b, glm::vec4 color) {
        if (boxes.size() == maximum || !finite(a) || !finite(b)) return false;
        glm::dvec3 scale(thickness);
        const auto delta = glm::abs(b-a);
        glm::length_t axis = 0;
        for (glm::length_t i=1; i<3; ++i) if (delta[i] > delta[axis]) axis=i;
        if (delta[axis] < thickness) return false;
        for (glm::length_t i=0; i<3; ++i) if (i != axis && delta[i] != 0) return false;
        scale[axis] = delta[axis];
        const glm::dmat4 model = glm::scale(glm::translate(frame, (a+b)*.5), scale);
        // Keep all double operations ahead of the sole float conversion. The
        // same near-world envelope as the prefab prevents far-origin jitter.
        if (!finite(model) || glm::any(glm::greaterThan(glm::abs(glm::dvec3(model[3])), glm::dvec3(100'000)))) return false;
        boxes.push_back({glm::mat4(model), color});
        return true;
    }
    bool wire(const glm::dmat4& frame, glm::dvec3 low, glm::dvec3 high, glm::vec4 color) {
        if (!finite(low) || !finite(high) || glm::any(glm::lessThan(high-low, glm::dvec3(thickness)))) return false;
        for (glm::length_t axis=0; axis<3; ++axis) {
            const auto u=(axis+1)%3, v=(axis+2)%3;
            for (int side=0; side<4; ++side) {
                auto a=low, b=low;
                a[u]=b[u]=(side&1) ? high[u] : low[u];
                a[v]=b[v]=(side&2) ? high[v] : low[v];
                b[axis]=high[axis];
                if (!line(frame,a,b,color)) return false;
            }
        }
        return true;
    }
};
} // namespace

moto::VmeshData inspectionGuideMesh() {
    moto::VmeshData mesh;
    std::array<moto::VmeshVertex,24> vertices{};
    std::array<uint32_t,36> indices{};
    // For each outward face choose u,v so cross(u,v)=normal.
    for (int face=0; face<6; ++face) {
        const int axis=face/2;
        const float sign=(face%2==0) ? 1.0f : -1.0f;
        glm::vec3 normal(0),u(0),v(0);
        normal[axis]=sign; u[(axis+1)%3]=1; v[(axis+2)%3]=sign;
        for (int corner=0; corner<4; ++corner) {
            const float x=(corner==1 || corner==2) ? .5f : -.5f;
            const float y=(corner>=2) ? .5f : -.5f;
            const auto p=normal*.5f+u*x+v*y;
            auto& vertex=vertices[static_cast<size_t>(face*4+corner)];
            for (int i=0; i<3; ++i) { vertex.position[i]=p[i]; vertex.normal[i]=normal[i]; vertex.tangent[i]=u[i]; }
            vertex.tangent[3]=1; vertex.texCoord[0]=x+.5f; vertex.texCoord[1]=y+.5f;
        }
        constexpr std::array<uint32_t,6> corners{0,1,2,0,2,3};
        for (size_t i=0; i<6; ++i) indices[static_cast<size_t>(face)*6+i]=static_cast<uint32_t>(face)*4+corners[i];
    }
    mesh.header.vertexCount=24; mesh.header.indexCount=36; mesh.header.indexStride=4;
    mesh.header.submeshCount=mesh.header.materialCount=mesh.header.meshCount=mesh.header.nodeCount=1;
    mesh.vertices.resize(sizeof(vertices)); std::memcpy(mesh.vertices.data(), vertices.data(), sizeof(vertices));
    mesh.indices.resize(sizeof(indices)); std::memcpy(mesh.indices.data(), indices.data(), sizeof(indices));
    mesh.submeshes.push_back({0,36,0,0}); mesh.materials.emplace_back(); mesh.materials[0].unlit=1;
    mesh.nodes.emplace_back(); mesh.nodes[0].meshIndex=0;
    return mesh;
}

bool makeInspectionGuides(const game::construction::PartDefinition& part,
    const game::assets::PrefabBounds& bounds, const glm::dmat4& root,
    construction::GridTransform placement, InspectionGuides mode, size_t maximumBoxes,
    std::vector<InspectionGuideBox>& output, std::string& error,
    std::optional<std::span<const construction::SocketId>> selectedSockets) {
    const auto fail=[&](const char* message) { error=message; return false; };
    if (mode != InspectionGuides::Off && mode != InspectionGuides::Dimensions && mode != InspectionGuides::Sockets)
        return fail("unknown inspection guide mode");
    if (maximumBoxes > 512 || !rigid(root)) return fail("invalid guide root or capacity");
    glm::dmat4 partFrame;
    if (!gridMatrix(placement,partFrame)) return fail("invalid guide part transform");
    partFrame=root*partFrame;
    Builder builder{maximumBoxes,{}};
    builder.boxes.reserve(std::min(maximumBoxes, size_t{128}));
    if (mode == InspectionGuides::Sockets) {
        if (part.sockets.size() > construction::kMaximumPartSockets) return fail("too many guide sockets");
        if (selectedSockets) {
            if (selectedSockets->size() > part.sockets.size()) return fail("too many selected socket guides");
            for (size_t i=0;i<selectedSockets->size();++i) {
                const auto id=(*selectedSockets)[i];
                if (!construction::findSocket(part,id)
                    || std::find(selectedSockets->begin(),selectedSockets->begin()+static_cast<ptrdiff_t>(i),id)
                        != selectedSockets->begin()+static_cast<ptrdiff_t>(i)) return fail("unknown or repeated selected socket guide");
            }
        }
        for (const auto& socket : part.sockets) {
            if (selectedSockets && std::find(selectedSockets->begin(),selectedSockets->end(),socket.id)==selectedSockets->end()) continue;
            glm::dmat4 socketFrame;
            if (!gridMatrix(socket.frame,socketFrame) || !construction::isValid(socket.clearance.minimum)
                || !construction::isValid(socket.clearance.maximum)) return fail("invalid socket guide metadata");
            socketFrame=partFrame*socketFrame;
            if (!builder.line(socketFrame,{}, {.30,0,0},red)
                || !builder.line(socketFrame,{}, {0,.40,0},green)
                || !builder.line(socketFrame,{}, {0,0,.25},blue)
                || !builder.wire(socketFrame,metres(socket.clearance.minimum),metres(socket.clearance.maximum),amber))
                return fail("socket guides exceed capacity or have invalid clearance");
        }
    } else if (mode == InspectionGuides::Dimensions) {
        const auto low=metres(part.footprint.minimum), high=metres(part.footprint.maximum);
        if (!bounds.valid || !construction::isValid(part.footprint.minimum) || !construction::isValid(part.footprint.maximum)
            || glm::any(glm::lessThanEqual(high,low)) || glm::any(glm::greaterThan(high-low,glm::dvec3(200))))
            return fail("invalid dimension guide bounds");
        if (!builder.wire(partFrame,bounds.minimum,bounds.maximum,amber)) return fail("invalid render bound or guide capacity");
        const auto ruler=[&](glm::length_t axis, glm::dvec3 start, double length, double step) {
            auto end=start; end[axis]+=length;
            if (!builder.line(partFrame,start,end,white)) return false;
            const int count=static_cast<int>(std::floor(length/step+1e-9));
            if (static_cast<size_t>(count)+1 > maximumBoxes-builder.boxes.size()) return false;
            for (int i=0; i<=count; ++i) {
                auto a=start; a[axis]+=static_cast<double>(i)*step; auto b=a;
                a.x-=.10; b.x+=.10;
                if (!builder.line(partFrame,a,b,white)) return false;
            }
            return true;
        };
        if (!ruler(2,{low.x-.35,low.y,low.z},high.z-low.z,1.0)
            || !ruler(1,{low.x-.75,low.y,low.z},high.y-low.y,
                      double{construction::kPlateTicks}/construction::kTicksPerMetre))
            return fail("ruler guide capacity exceeded");
        const glm::dvec3 origin{low.x-1.15,low.y,low.z-.5};
        if (!builder.line(partFrame,origin,origin+glm::dvec3(.6,0,0),red)
            || !builder.line(partFrame,origin,origin+glm::dvec3(0,.8,0),green)
            || !builder.line(partFrame,origin,origin+glm::dvec3(0,0,-1),blue)) return fail("axis guide capacity exceeded");
    }
    output=std::move(builder.boxes); error.clear(); return true;
}

bool makePrototypeFixtureMesh(const construction::PartDefinition& part,
    moto::VmeshData& output, game::assets::RigidPrefab& prefab, std::string& error) {
    construction::CatalogIssue issue;
    construction::PartCatalogDraft draft; draft.definitions.push_back(part);
    if (!construction::PartCatalog::create(draft,issue) || part.visuals.size()!=1
        || !std::holds_alternative<construction::PrototypeBoxVisual>(part.visuals[0].asset)) {
        error="invalid prototype-box fixture definition: "+std::string(issue.field); return false;
    }
    auto mesh=inspectionGuideMesh();
    const auto& bounds=std::get<construction::PrototypeBoxVisual>(part.visuals[0].asset).bounds;
    const auto low=metres(bounds.minimum),high=metres(bounds.maximum);
    const auto extent=high-low,center=(high+low)*.5;
    for (size_t i=0;i<mesh.header.vertexCount;++i) {
        moto::VmeshVertex vertex{};
        std::memcpy(&vertex,mesh.vertices.data()+i*sizeof(vertex),sizeof(vertex));
        for (glm::length_t axis=0;axis<3;++axis)
            vertex.position[axis]=static_cast<float>(center[axis]+extent[axis]*static_cast<double>(vertex.position[axis]));
        std::memcpy(mesh.vertices.data()+i*sizeof(vertex),&vertex,sizeof(vertex));
    }
    auto& material=mesh.materials[0]; material.unlit=0;
    for (size_t i=0;i<3;++i) material.baseColorFactor[i]=static_cast<float>(part.material.linearBaseColor[i]);
    material.roughnessFactor=static_cast<float>(part.material.roughness);
    material.metallicFactor=static_cast<float>(part.material.metallic);
    game::assets::RigidPrefab prepared;
    if (!game::assets::prepareRigidPrefab(mesh,{}, {},prepared,error)) return false;
    output=std::move(mesh); prefab=std::move(prepared); error.clear();return true;
}
} // namespace voxy::render

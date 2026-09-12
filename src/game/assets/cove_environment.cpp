#include "game/assets/cove_environment.hpp"
#include "core/sha256.hpp"

#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace voxy::game::assets {
namespace {
using Json=nlohmann::json;
constexpr size_t manifestCap=64u*1024u,meshCap=1024u*1024u;
constexpr std::array<CoveEnvironmentBox,10> gantryBoxes{{
    {{-210,0,-210},{-190,352,-190}},{{190,0,-210},{210,352,-190}},
    {{-210,0,190},{-190,352,210}},{{190,0,190},{210,352,210}},
    {{-210,352,-210},{210,372,-190}},{{-210,352,190},{210,372,210}},
    {{-210,352,-190},{-190,372,190}},{{190,352,-190},{210,372,190}},
    {{-190,352,-43},{190,368,-27}},{{-190,352,27},{190,368,43}}}};
constexpr std::array<std::string_view,5> sceneryNames{
    "cove_workshop","cove_wreck","cove_east_beacon","cove_west_beacon","cove_shore"};

bool hashString(const std::string& hash) {
    return hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==std::string::npos;
}
construction::GridPosition position(const Json& value) {
    if(!value.is_array()||value.size()!=3)throw std::runtime_error("position");
    std::array<int32_t,3> result{};
    for(size_t i=0;i<3;++i) {
        if(!value[i].is_number_integer())throw std::runtime_error("nonlattice position");
        const auto v=value[i].get<int64_t>();
        if(v < -5000 || v > 5000)throw std::runtime_error("position range");
        result[i]=static_cast<int32_t>(v);
    }
    return {result[0],result[1],result[2]};
}
CoveEnvironmentBox box(const Json& value) {
    CoveEnvironmentBox b{position(value.at("minimum")),position(value.at("maximum"))};
    if(b.minimum.x>=b.maximum.x||b.minimum.y>=b.maximum.y||b.minimum.z>=b.maximum.z)
        throw std::runtime_error("empty collision");
    return b;
}
bool same(const CoveEnvironmentBox& a,const CoveEnvironmentBox& b) {
    return a.minimum==b.minimum&&a.maximum==b.maximum;
}
glm::dvec3 metres(construction::GridPosition p) {return glm::dvec3(p.x,p.y,p.z)*.02;}

// The same exact lattice boxes feed the application collision. Check actual
// transformed indexed vertices and support on every authored contact face,
// not just an aggregate AABB that would permit hidden boxes or closed doors.
bool contactGeometry(const moto::VmeshData& mesh,const RigidPrefab& prefab,std::span<const CoveEnvironmentBox> boxes,
                     std::string& error) {
    constexpr double allowance=.02001;
    std::vector<RigidPrefabDraw> draws;
    if(!placeRigidPrefab(prefab,glm::dmat4(1),{},kCoveEnvironmentMaximumInstances,draws,error))return false;
    std::vector<std::array<bool,6>> faces(boxes.size());
    for(const auto& draw:draws)for(const auto& sub:mesh.submeshes) {
        if(sub.meshIndex!=draw.meshIndex)continue;
        for(uint32_t i=0;i<sub.indexCount;++i) {
            uint32_t index=0;
            const auto offset=sub.indexOffset+uint64_t{i}*mesh.header.indexStride;
            if(mesh.header.indexStride==2) {
                uint16_t value=0;std::memcpy(&value,mesh.indices.data()+offset,2);index=value;
            } else std::memcpy(&index,mesh.indices.data()+offset,4);
            moto::VmeshVertex vertex;
            std::memcpy(&vertex,mesh.vertices.data()+uint64_t{index}*sizeof(vertex),sizeof(vertex));
            const glm::dvec3 p=glm::dvec3(glm::dmat4(draw.modelMatrix)*glm::dvec4(
                vertex.position[0],vertex.position[1],vertex.position[2],1));
            bool supported=false;
            for(size_t b=0;b<boxes.size();++b) {
                const auto lo=metres(boxes[b].minimum),hi=metres(boxes[b].maximum);
                if(glm::any(glm::lessThan(p,lo-glm::dvec3(allowance)))
                    ||glm::any(glm::greaterThan(p,hi+glm::dvec3(allowance))))continue;
                supported=true;
                for(size_t axis=0;axis<3;++axis) {
                    const auto a=static_cast<glm::length_t>(axis);
                    faces[b][axis*2]|=std::abs(p[a]-lo[a])<=allowance;
                    faces[b][axis*2+1]|=std::abs(p[a]-hi[a])<=allowance;
                }
            }
            if(!supported){error="Cove scenery exceeds its collision envelope.";return false;}
        }
    }
    for(const auto& face:faces)if(!std::all_of(face.begin(),face.end(),[](bool v){return v;})) {
        error="Cove collision includes an unsupported contact face.";return false;
    }
    return true;
}

bool prepareLod(const moto::VmeshData& mesh,bool scenery,std::span<const CoveEnvironmentBox> collision,
                RigidPrefab& output,std::string& error) {
    if(!mesh.images.empty()||!mesh.anims.empty()||!mesh.skins.empty()) {
        error="Cove scenery must use opaque solid rigid materials.";return false;
    }
    RigidPrefabLimits limits;
    limits.maximumNodes=60;limits.maximumMeshes=60;limits.maximumSubmeshes=90;
    // The five authored scenery roots use twenty separately charged solid slots.
    limits.maximumMaterials=32;limits.maximumVertices=18000;limits.maximumIndices=54000;
    limits.maximumMeshInstances=60;limits.maximumExpandedDraws=90;
    limits.maximumDecodedBytes=2u*1024u*1024u;limits.maximumGpuBytes=meshCap;
    limits.maximumAbsoluteCoordinate=100;
    if(!prepareRigidPrefab(mesh,{12},limits,output,error))return false;
    const size_t expected=scenery?sceneryNames.size():1;
    if(mesh.nodes.size()!=expected||output.meshNodes.size()!=expected) {
        error="Cove scenery root hierarchy is unsupported.";return false;
    }
    std::set<std::string_view> names;
    for(const auto& node:mesh.nodes) {
        const std::string_view name=mesh.name(node.nameOffset);
        if(node.parent!=-1||node.meshIndex==UINT32_MAX||!names.insert(name).second
            ||(scenery?std::find(sceneryNames.begin(),sceneryNames.end(),name)==sceneryNames.end():name!="cove_gantry")) {
            error="Cove scenery named root is invalid.";return false;
        }
    }
    if(!contactGeometry(mesh,output,scenery?collision:std::span<const CoveEnvironmentBox>(gantryBoxes),error))return false;
    // The100m profile above bounds authored vertices/node transforms. Draws
    // use camera-sector coordinates instead: even the dock's negative sector
    // translates this same admitted scenery beyond100m. Keep the established
    // renderer placement allowance without relaxing source/contact admission.
    output.limits.maximumAbsoluteCoordinate=100000;
    return true;
}

bool selectedCapacity(const CoveEnvironmentAsset& asset,std::string& error) {
    for(size_t i=0;i<kCoveEnvironmentLods;++i) {
        const auto& a=asset.scenery[i].prefab.counts;const auto& b=asset.gantry[i].prefab.counts;
        if(a.meshInstances+b.meshInstances>kCoveEnvironmentMaximumInstances
            ||a.expandedDraws+b.expandedDraws>kCoveEnvironmentMaximumDraws) {
            error="Cove scenery exceeds its selected frame capacity.";return false;
        }
    }
    return true;
}
}

bool prepareCoveEnvironment(const CoveEnvironmentAsset& input,CoveEnvironmentAsset& output,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    try {
        if(input.collision.empty()||input.collision.size()>kCoveEnvironmentMaximumCollisionBoxes)
            return fail("Cove scenery collision capacity is invalid.");
        for(const auto& b:input.collision) {
            for(const auto p:{b.minimum,b.maximum})if(p.x < -5000||p.x>5000||p.y < -5000||p.y>5000||p.z < -5000||p.z>5000)
                return fail("Cove scenery collision is outside its local frame.");
            if(b.minimum.x>=b.maximum.x||b.minimum.y>=b.maximum.y||b.minimum.z>=b.maximum.z)
                return fail("Cove scenery collision is empty.");
            if(b.maximum.x>-450&&b.minimum.x<400&&b.maximum.z>-3850&&b.minimum.z<-2350)
                return fail("Cove scenery blocks the protected gameplay berth.");
        }
        CoveEnvironmentAsset candidate;candidate.collision=input.collision;
        for(size_t i=0;i<6;++i) {
            const auto& mesh=(i<3?input.scenery[i]:input.gantry[i-3]).mesh;
            auto& lod=i<3?candidate.scenery[i]:candidate.gantry[i-3];
            // Validate actual buffers before copying: caller caches and claimed
            // sizes never authorize allocation or GPU reservation.
            if(!prepareLod(mesh,i<3,candidate.collision,lod.prefab,error))return false;
            if(lod.prefab.counts.gpuBytes>kCoveEnvironmentMaximumGpuBytes-candidate.gpuBytes)
                return fail("Cove scenery exceeds its complete resident payload budget.");
            candidate.gpuBytes+=lod.prefab.counts.gpuBytes;candidate.decodedBytes+=lod.prefab.counts.decodedBytes;
            lod.mesh=mesh;
        }
        if(!selectedCapacity(candidate,error))return false;
        output=std::move(candidate);error.clear();return true;
    }catch(const std::bad_alloc&){return fail("Not enough memory to admit the Cove scenery.");}
    catch(const std::exception&){return fail("Cove scenery package is malformed.");}
}

std::shared_ptr<const CoveEnvironmentAsset> loadCoveEnvironment(
    const CookedPartByteProvider& read,std::string& error) {
    const auto fail=[&](const char* reason)->std::shared_ptr<const CoveEnvironmentAsset>{error=reason;return {};};
    try {
        const auto bytes=read("manifest.json",manifestCap,error);
        if(!bytes||bytes->empty()||bytes->size()>manifestCap)return fail("Cove scenery manifest is missing or oversized.");
        std::vector<std::set<std::string>> keys;
        const auto* text=reinterpret_cast<const char*>(bytes->data());
        const auto doc=Json::parse(text,text+bytes->size(),[&](int depth,auto event,auto& parsed) {
            using Event=Json::parse_event_t;
            if(depth>8)throw std::runtime_error("manifest nesting");
            if(event==Event::object_start)keys.emplace_back();
            else if(event==Event::key) {
                const auto key=parsed.template get<std::string>();
                if(key.size()>64||keys.empty()||!keys.back().insert(key).second)throw std::runtime_error("duplicate key");
            } else if(event==Event::object_end)keys.pop_back();
            return true;
        });
        if(!doc.is_object()||doc.value("schema",0)!=1||doc.value("profile","")!="salvage-rigid-v1"
            ||doc.value("asset_id","")!="voxys-cove-environment-r01"||doc.value("render_to_canonical",0)!=12)
            return fail("Cove scenery identity or rigid profile is unsupported.");
        auto result=std::make_shared<CoveEnvironmentAsset>();
        const auto& collision=doc.at("collision");
        if(!collision.is_array()||collision.empty()||collision.size()>kCoveEnvironmentMaximumCollisionBoxes)
            return fail("Cove scenery collision capacity is invalid.");
        for(const auto& item:collision) {
            const auto b=box(item);
            // Existing berth, cargo and skiff route stay physically unchanged.
            if(b.maximum.x>-450&&b.minimum.x<400&&b.maximum.z>-3850&&b.minimum.z<-2350)
                return fail("Cove scenery blocks the protected gameplay berth.");
            result->collision.push_back(b);
        }
        const auto& gantry=doc.at("gantry_collision");
        if(!gantry.is_array()||gantry.size()!=gantryBoxes.size())return fail("Cove gantry contact contract is incomplete.");
        for(size_t i=0;i<gantryBoxes.size();++i)if(!same(box(gantry[i]),gantryBoxes[i]))
            return fail("Cove gantry changes the installed physical structure.");
        const auto& entries=doc.at("lods");
        if(!entries.is_array()||entries.size()!=6)return fail("Cove scenery requires three complete LOD pairs.");
        // Validate all filenames/digests before requesting any payload. Never
        // probe an alternate, user-controlled or fallback model location.
        for(size_t i=0;i<6;++i) {
            const std::string kind=i<3?"scenery":"gantry";
            const std::string filename=kind+"-lod"+std::to_string(i%3)+".vmesh";
            if(entries[i].value("kind","")!=kind||entries[i].value("lod",99u)!=i%3
                ||entries[i].value("filename","")!=filename||!hashString(entries[i].at("sha256").get<std::string>()))
                return fail("Cove scenery LOD identity or digest is invalid.");
        }
        for(size_t i=0;i<6;++i) {
            const auto filename=entries[i].at("filename").get<std::string>();
            const auto payload=read(filename,meshCap,error);
            if(!payload||payload->empty()||payload->size()>meshCap)return fail("Cove scenery model is missing or oversized.");
            if(core::sha256Hex(core::sha256(std::as_bytes(std::span(*payload))))!=entries[i].at("sha256"))
                return fail("Cove scenery model differs from its installed digest.");
            auto& lod=i<3?result->scenery[i]:result->gantry[i-3];
            if(!moto::readVmesh(payload->data(),payload->size(),&lod.mesh,&error))return {};
            if(!prepareLod(lod.mesh,i<3,result->collision,lod.prefab,error))return {};
            if(lod.prefab.counts.gpuBytes>kCoveEnvironmentMaximumGpuBytes-result->gpuBytes)
                return fail("Cove scenery exceeds its complete resident payload budget.");
            result->gpuBytes+=lod.prefab.counts.gpuBytes;result->decodedBytes+=lod.prefab.counts.decodedBytes;
        }
        if(!selectedCapacity(*result,error))return {};
        error.clear();return result;
    } catch(const std::bad_alloc&){return fail("Not enough memory to admit the Cove scenery.");}
    catch(const std::exception&){return fail("Cove scenery package is malformed.");}
}
std::shared_ptr<const CoveEnvironmentAsset> loadCoveEnvironment(
    const std::filesystem::path& directory,std::string& error) {
    const auto read=openCookedPartDirectory(directory,error);
    return read?loadCoveEnvironment(*read,error):nullptr;
}
} // namespace voxy::game::assets

#include "game/adventure/imported_assembly.hpp"
#include "gameplay/structural_assembly.hpp"
#include "core/sha256.hpp"
#include <json.hpp>
#include <charconv>
#include <fstream>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::adventure {
namespace {
constexpr double grid=physics::kAuthoredShapeTickMetres;
constexpr uint32_t sourceStride=256;
constexpr ImportedPartCatalogEntry entry(std::string_view id,uint16_t x,uint16_t z,double height=1.2) {
    // Explicit enlarged-toy convention: a full 1x1 brick is 0.125 game kg.
    // This is not physical ABS density at the one-unit-per-stud world scale.
    return {id,x,z,height,.125*double(x)*double(z)*height/1.2};
}
constexpr std::array catalog{
    entry("3001.dat",4,2),entry("3002.dat",3,2),entry("3003.dat",2,2),entry("3004.dat",2,1),
    entry("3005.dat",1,1),entry("3008.dat",8,1),entry("3009.dat",6,1),entry("3010.dat",4,1),entry("2456.dat",6,2),
    entry("3622.dat",3,1),entry("98283.dat",2,1),entry("3020.dat",4,2,.4),entry("3021.dat",3,2,.4),
    entry("3022.dat",2,2,.4),entry("3023.dat",2,1,.4),entry("3024.dat",1,1,.4),
    entry("3623.dat",3,1,.4),entry("3710.dat",4,1,.4),entry("3666.dat",6,1,.4),entry("3460.dat",8,1,.4),
    entry("3795.dat",6,2,.4),entry("3034.dat",8,2,.4)};
bool finite(glm::dvec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool finite(glm::dquat q) { return std::isfinite(q.w)&&std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z); }
bool text(std::string_view value,size_t limit) {
    return !value.empty()&&value.size()<=limit&&std::none_of(value.begin(),value.end(),[](char c){return c=='\0'||c=='\n'||c=='\r';});
}
std::string_view number(std::string_view value) {if(value.ends_with(".dat"))value.remove_suffix(4);return value;}
struct Box {glm::dvec3 minimum{},maximum{};};
std::vector<Box> proxy(const ImportedPartCatalogEntry& part) {
    const double x=double(part.studsX)*.5,z=double(part.studsZ)*.5,h=part.height;
    // Standard brick/plate shell, open underside. Internal tubes, logo lettering,
    // bevels and cosmetic masonry grooves are intentionally outside this proxy.
    std::vector<Box> result{
        {{-x,-.2,-z},{x,0,z}},
        {{-x,-h,-z},{-x+.2,-.2,z}},{{x-.2,-h,-z},{x,-.2,z}},
        {{-x+.2,-h,-z},{x-.2,-.2,-z+.2}},{{-x+.2,-h,z-.2},{x-.2,-.2,z}}};
    for(uint16_t slot=0;slot<part.connectorCount();++slot) {
        const auto p=part.connector(slot,false);
        // LDraw stud.dat radius is 6/20=.3; 3004s01/3005s01 underside
        // half-width is also 6/20=.3. CAD models clutch as exact contact.
        // These inscribed strips never exceed that source cylinder and leave
        // one .02 tick of side clearance. The validated graph owns clutch;
        // separated rigid parts must not begin with an artificial press fit.
        for(const glm::dvec2 half:{glm::dvec2(.28,.1),glm::dvec2(.24,.18),glm::dvec2(.18,.24),glm::dvec2(.1,.28)})
            result.push_back({p+glm::dvec3(-half.x,0,-half.y),p+glm::dvec3(half.x,.2,half.y)});
    }
    return result;
}
geometry::GridPosition ticks(glm::dvec3 value) {
    return {int32_t(std::llround(value.x/grid)),int32_t(std::llround(value.y/grid)),int32_t(std::llround(value.z/grid))};
}
glm::dvec3 point(geometry::GridPosition value) {return glm::dvec3(value.x,value.y,value.z)*grid;}
struct PartPhysical {std::vector<Box> boxes;glm::dvec3 center{};glm::dmat3 inertia{0};double mass=0;};
std::optional<PartPhysical> physical(const ImportedPartCatalogEntry& part) {
    PartPhysical result;result.boxes=proxy(part);result.mass=part.massKg;
    std::vector<geometry::UnionBox> inputs;inputs.reserve(result.boxes.size());
    for(size_t i=0;i<result.boxes.size();++i)inputs.push_back({{ticks(result.boxes[i].minimum),ticks(result.boxes[i].maximum)},uint32_t(i+1)});
    geometry::BoxUnionIssue issue;const auto shape=geometry::BoxUnion::compile(inputs,issue);if(!shape)return {};
    double volume=0;
    for(const auto& cell:shape->cells()) {
        const auto lo=point(cell.bounds.minimum),hi=point(cell.bounds.maximum),d=hi-lo;
        const double v=d.x*d.y*d.z;volume+=v;result.center+=(lo+hi)*(.5*v);
    }
    if(volume<=0)return {};
    result.center/=volume;
    for(const auto& cell:shape->cells()) {
        const auto lo=point(cell.bounds.minimum),hi=point(cell.bounds.maximum),d=hi-lo;
        const double m=part.massKg*(d.x*d.y*d.z)/volume;
        glm::dmat3 inertia(0);inertia[0][0]=m*(d.y*d.y+d.z*d.z)/12;
        inertia[1][1]=m*(d.x*d.x+d.z*d.z)/12;inertia[2][2]=m*(d.x*d.x+d.y*d.y)/12;
        const auto offset=(lo+hi)*.5-result.center;
        result.inertia+=inertia+m*(glm::dmat3(glm::dot(offset,offset))-glm::outerProduct(offset,offset));
    }
    return result;
}
bool orthogonal(glm::dmat3 rotation) {
    for(int column=0;column<3;++column)for(int row=0;row<3;++row)
        if(std::abs(rotation[column][row]-std::round(rotation[column][row]))>2e-5)return false;
    return true;
}
}

glm::dvec3 ImportedPartCatalogEntry::connector(uint16_t slot,bool underside) const noexcept {
    if(!studsX||slot>=connectorCount())return glm::dvec3(std::numeric_limits<double>::quiet_NaN());
    return {double(slot%studsX)-.5*double(studsX-1),underside?-height:0,double(slot/studsX)-.5*double(studsZ-1)};
}
const ImportedPartCatalogEntry* importedPartCatalog(std::string_view partNumber) noexcept {
    for(const auto& part:catalog)if(number(part.partNumber)==number(partNumber))return &part;
    return nullptr;
}

bool validateImportedSource(const ImportedAssemblySource& source,std::string& error) {
    error.clear();const auto fail=[&](const char* reason){error=reason;return false;};
    if(source.schemaVersion!=1||source.catalogVersion!=1||!source.revision)return fail("Unsupported imported assembly version or revision.");
    if(!text(source.assetId,128)||source.sourceSha256.size()!=64
        ||!std::all_of(source.sourceSha256.begin(),source.sourceSha256.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))
        return fail("Imported assembly provenance is missing or invalid.");
    if(source.parts.empty()||source.parts.size()>ImportedAssembly::maximumSourceParts
        ||source.bonds.size()>ImportedAssembly::maximumSourceBonds||source.anchors.size()>source.parts.size())
        return fail("Imported source exceeds its explicit metadata capacity.");
    try {
        std::set<uint64_t> ids,bonds,anchors;std::set<std::string> paths;
        for(const auto& part:source.parts) {
            if(!part.sourceId||!ids.insert(part.sourceId).second||!text(part.sourcePath,1024)
                ||!paths.insert(part.sourcePath).second||!text(part.partNumber,64))return fail("Imported part identity or source path is invalid or duplicated.");
            if(!finite(part.translation)||glm::any(glm::greaterThan(glm::abs(part.translation),glm::dvec3(4096)))
                ||!finite(part.rotation)||std::abs(glm::dot(part.rotation,part.rotation)-1)>1e-6)
                return fail("Imported part transform is not a finite normalized rigid transform.");
        }
        for(const auto& bond:source.bonds)
            if(!bond.id||!bonds.insert(bond.id).second||bond.lowerPart==bond.upperPart
                ||!ids.contains(bond.lowerPart)||!ids.contains(bond.upperPart))return fail("Imported bond identity or endpoint is invalid.");
        for(const auto id:source.anchors)
            if(!ids.contains(id)||!anchors.insert(id).second)return fail("Imported anchor is unknown or duplicated.");
        return true;
    } catch(const std::bad_alloc&) {return fail("Imported source validation ran out of memory.");}
}


std::optional<ImportedAssemblySource> loadImportedSection(const std::filesystem::path& path,
    std::string& error,std::string_view expectedSha256) {
    error.clear();
    try {
        const auto read=[](const std::filesystem::path& file,size_t limit) {
            std::error_code status;const auto size=std::filesystem::file_size(file,status);
            if(status||!size||size>limit)throw std::runtime_error("Imported section file is absent, empty or oversized.");
            std::ifstream input(file,std::ios::binary);std::string bytes(size_t(size),'\0');
            if(!input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))||input.peek()!=std::char_traits<char>::eof())
                throw std::runtime_error("Imported section file could not be read completely.");
            return bytes;
        };
        const auto bytes=read(path,2*1024*1024);
        if(!expectedSha256.empty()&&core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes.data(),bytes.size()))))!=expectedSha256)
            throw std::runtime_error("Imported section byte hash does not match the installed artifact.");
        const auto json=nlohmann::json::parse(bytes);
        if(!json.at("schema").is_number_integer()||json.at("schema")!=1)
            throw std::runtime_error("Imported section schema is unsupported.");
        ImportedAssemblySource result;
        result.assetId=json.at("assemblyAsset").get<std::string>();
        if(json.contains("sourceMPDSha256"))result.sourceSha256=json.at("sourceMPDSha256").get<std::string>();
        else {
            const auto assembly=nlohmann::json::parse(read(path.parent_path()/"assembly.json",16*1024*1024));
            if(assembly.at("schema")!=1||assembly.at("assetId")!=result.assetId)
                throw std::runtime_error("Imported section does not match its source assembly.");
            result.sourceSha256=assembly.at("sourceMPDSha256").get<std::string>();
        }
        const auto id=[](const nlohmann::json& value) {
            const auto text=value.get<std::string>();uint64_t output=0;
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),output);
            if(!output||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())
                throw std::runtime_error("Imported source IDs must be complete nonzero uint64 decimal strings.");
            return output;
        };
        const auto integer=[](const nlohmann::json& value,uint64_t maximum) {
            if(!value.is_number_integer()||(!value.is_number_unsigned()&&value.get<int64_t>()<0))
                throw std::runtime_error("Imported integer is negative or not integral.");
            const auto output=value.get<uint64_t>();
            if(output>maximum)throw std::runtime_error("Imported integer exceeds its declared capacity.");
            return output;
        };
        const auto vector=[](const nlohmann::json& value) {
            if(!value.is_array()||value.size()!=3)throw std::runtime_error("Imported vector must have three components.");
            const glm::dvec3 out(value[0].get<double>(),value[1].get<double>(),value[2].get<double>());
            if(!finite(out))throw std::runtime_error("Imported vector is not finite.");
            return out;
        };
        const auto& parts=json.at("parts");const auto& bonds=json.at("bonds");const auto& anchors=json.at("anchoredParts");
        if(!parts.is_array()||parts.empty()||parts.size()>ImportedAssembly::maximumSectionParts
            ||!bonds.is_array()||bonds.size()>ImportedAssembly::maximumSectionBonds
            ||!anchors.is_array()||anchors.size()>parts.size())throw std::runtime_error("Imported section arrays exceed their capacity.");
        for(const auto& value:parts) {
            ImportedPart part;part.sourceId=id(value.at("sourceId"));part.sourcePath=value.at("sourcePath").get<std::string>();
            part.partNumber=value.at("partNumber").get<std::string>();part.colour=uint32_t(integer(value.at("colour"),UINT32_MAX));
            part.meshNode=uint32_t(integer(value.at("meshNode"),UINT32_MAX));part.translation=vector(value.at("translation"));
            const auto& rotation=value.at("rotation");
            if(!rotation.is_array()||rotation.size()!=4)throw std::runtime_error("Imported rotation requires w,x,y,z.");
            part.rotation={rotation[0].get<double>(),rotation[1].get<double>(),rotation[2].get<double>(),rotation[3].get<double>()};
            if(glm::length(vector(value.at("scale"))-glm::dvec3(1))>2e-5)
                throw std::runtime_error("Imported section contains unsupported part scale.");
            const auto* spec=importedPartCatalog(part.partNumber);
            if(!spec||value.at("connectorType")!="rectangular-stud-v1"||value.at("proxyType")!="rectangular-body-envelope-v1")
                throw std::runtime_error("Imported section uses an unsupported connector/proxy part.");
            if(integer(value.at("studsX"),UINT16_MAX)!=spec->studsX||integer(value.at("studsZ"),UINT16_MAX)!=spec->studsZ
                ||std::abs(value.at("height").get<double>()-spec->height)>2e-5)
                throw std::runtime_error("Imported source dimensions disagree with the part catalog.");
            const auto& bounds=value.at("bodyBounds");
            if(glm::length(vector(bounds.at("minimum"))-glm::dvec3(-.5*spec->studsX,-spec->height,-.5*spec->studsZ))>2e-5
                ||glm::length(vector(bounds.at("maximum"))-glm::dvec3(.5*spec->studsX,0,.5*spec->studsZ))>2e-5)
                throw std::runtime_error("Imported body envelope disagrees with the part catalog.");
            for(bool underside:{false,true}) {
                const auto& connectors=value.at(underside?"antiStuds":"studs");
                if(!connectors.is_array()||connectors.size()!=spec->connectorCount())
                    throw std::runtime_error("Imported connector count disagrees with the part catalog.");
                for(uint16_t slot=0;slot<spec->connectorCount();++slot)
                    if(glm::length(vector(connectors[slot])-spec->connector(slot,underside))>2e-5)
                        throw std::runtime_error("Imported connector position disagrees with the part catalog.");
            }
            result.parts.push_back(std::move(part));
        }
        const auto parseBond=[&](const nlohmann::json& value) {
            return ImportedStudBond{id(value.at("id")),id(value.at("lowerPart")),id(value.at("upperPart")),
                uint16_t(integer(value.at("lowerSlot"),UINT16_MAX)),uint16_t(integer(value.at("upperSlot"),UINT16_MAX)),value.at("active").get<bool>()};
        };
        for(const auto& value:bonds)result.bonds.push_back(parseBond(value));
        for(const auto& value:anchors)result.anchors.push_back(id(value));
        // Each anchor needs explicit selected-to-remainder boundary evidence.
        // The exporter validates the OTHER endpoint against the full catalog;
        // this section loader verifies its owned endpoint and witness location.
        std::set<uint64_t> witnessed,externalBonds;
        const auto& boundaries=json.at("boundaryBonds");
        if(!boundaries.is_array()||boundaries.size()>1024)throw std::runtime_error("Imported boundary bond capacity exceeded.");
        for(const auto& value:boundaries) {
            const auto bond=parseBond(value);
            const auto lower=std::find_if(result.parts.begin(),result.parts.end(),[&](const auto& p){return p.sourceId==bond.lowerPart;});
            const auto upper=std::find_if(result.parts.begin(),result.parts.end(),[&](const auto& p){return p.sourceId==bond.upperPart;});
            if(!bond.active||!externalBonds.insert(bond.id).second||(lower==result.parts.end())==(upper==result.parts.end()))
                throw std::runtime_error("Imported support witness must join one selected and one external part.");
            const bool underside=upper!=result.parts.end();const auto& owned=underside?*upper:*lower;
            const uint16_t slot=underside?bond.upperSlot:bond.lowerSlot;const auto* spec=importedPartCatalog(owned.partNumber);
            if(slot>=spec->connectorCount()||glm::length(owned.translation+glm::normalize(owned.rotation)*spec->connector(slot,underside)-vector(value.at("point")))>.002)
                throw std::runtime_error("Imported support witness is not an owned catalog connector.");
            witnessed.insert(owned.sourceId);
        }
        if(witnessed!=std::set<uint64_t>(result.anchors.begin(),result.anchors.end()))
            throw std::runtime_error("Imported anchors do not match explicit boundary support witnesses.");
        if(!validateImportedSource(result,error))return {};
        return result;
    } catch(const std::exception& exception) {error=std::string("Imported section load refused: ")+exception.what();return {};}
}

std::optional<ImportedAssembly> ImportedAssembly::prepare(const ImportedAssemblySource& source,std::string& error,
    CollisionProfile profile) {
    if(!validateImportedSource(source,error))return {};
    const auto fail=[&](std::string reason)->std::optional<ImportedAssembly>{error=std::move(reason);return {};};
    if(profile!=CollisionProfile::Detailed&&profile!=CollisionProfile::ReleasedShell)
        return fail("Unknown imported collision profile.");
    if(source.parts.size()>maximumSectionParts||source.bonds.size()>maximumSectionBonds)
        return fail("D2 physical sections support at most 128 parts and 1024 explicit bonds; the full source remains separate.");
    try {
        ImportedAssembly result;result.source_=source;result.collisionProfile_=profile;
        std::sort(result.source_.parts.begin(),result.source_.parts.end(),[](const auto& a,const auto& b){return a.sourceId<b.sourceId;});
        std::sort(result.source_.bonds.begin(),result.source_.bonds.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        std::sort(result.source_.anchors.begin(),result.source_.anchors.end());
        std::map<uint64_t,uint32_t> index;
        std::vector<const ImportedPartCatalogEntry*> entries;
        std::vector<PartPhysical> physicals;
        std::vector<gameplay::AssemblyNode> nodes;
        for(size_t i=0;i<result.source_.parts.size();++i) {
            const auto& part=result.source_.parts[i];index[part.sourceId]=uint32_t(i);
            const auto* spec=importedPartCatalog(part.partNumber);
            if(!spec)return fail("Unsupported collision/connector catalog part: "+part.partNumber);
            const auto rotation=glm::mat3_cast(glm::normalize(part.rotation));
            if(!orthogonal(rotation))return fail("D2 collision does not support this source rotation; it was preserved, not rounded: "+part.sourcePath);
            auto mass=physical(*spec);if(!mass)return fail("Imported part proxy preparation failed.");
            const auto center=part.translation+rotation*mass->center;
            std::array<int32_t,3> position{};
            for(int axis=0;axis<3;++axis) {
                if(std::abs(center[axis])>4096)return fail("Imported part mass center exceeds the local section frame.");
                position[size_t(axis)]=int32_t(std::llround(center[axis]*gameplay::kPositionOne));
            }
            const bool anchor=std::binary_search(result.source_.anchors.begin(),result.source_.anchors.end(),part.sourceId);
            nodes.push_back({uint32_t(i+1),position,int32_t(std::llround(spec->massKg*gameplay::kScalarOne)),0,
                anchor?gameplay::AssemblyNodeFoundation:0u});
            entries.push_back(spec);physicals.push_back(std::move(*mass));
        }
        std::vector<gameplay::AssemblyEdge> edges;
        std::set<std::tuple<uint64_t,uint16_t,bool>> occupied;
        for(size_t i=0;i<result.source_.bonds.size();++i) {
            const auto& bond=result.source_.bonds[i];const auto a=index.at(bond.lowerPart),b=index.at(bond.upperPart);
            if(bond.lowerSlot>=entries[a]->connectorCount()||bond.upperSlot>=entries[b]->connectorCount())return fail("Imported stud bond addresses an unavailable connector.");
            const auto& lower=result.source_.parts[a];const auto& upper=result.source_.parts[b];
            const auto qa=glm::normalize(lower.rotation),qb=glm::normalize(upper.rotation);
            const auto pa=lower.translation+qa*entries[a]->connector(bond.lowerSlot,false);
            const auto pb=upper.translation+qb*entries[b]->connector(bond.upperSlot,true);
            if(glm::length(pa-pb)>.002 || glm::dot(qa*glm::dvec3(0,1,0),qb*glm::dvec3(0,-1,0))>-.999999)
                return fail("Imported stud bond does not join coincident opposed catalog connectors.");
            if(!bond.active)continue;
            if(!occupied.emplace(bond.lowerPart,bond.lowerSlot,false).second
                ||!occupied.emplace(bond.upperPart,bond.upperSlot,true).second)return fail("An imported connector has more than one active mate.");
            edges.push_back({uint32_t(i+1),a+1,b+1,gameplay::kScalarOne,1});
        }
        // Reuse the existing deterministic graph. Its sole keel flag is not a
        // support decision: every explicit anchor is checked independently below.
        gameplay::StructuralAssembly graph({uint32_t(maximumSectionParts),uint32_t(maximumSectionBonds),1,gameplay::kScalarOne});
        if(!graph.initialize(nodes,edges))return fail("Imported section graph was refused.");
        if(graph.components().size()>maximumRoots)return fail("Imported cut exceeds the bounded physical root budget.");
        for(const auto& component:graph.components()) {
            const uint32_t first=component.rootNode-1;
            const auto origin=result.source_.parts[first].translation;
            std::vector<uint32_t> members;std::vector<geometry::UnionBox> boxes;
            double mass=0;glm::dvec3 center(0);glm::dmat3 inertia(0);bool anchored=false;
            for(const uint32_t node:component.nodeIds) {
                const uint32_t partIndex=node-1;const auto& part=result.source_.parts[partIndex];const auto& body=physicals[partIndex];
                const auto rotation=glm::mat3_cast(glm::normalize(part.rotation));
                members.push_back(partIndex);mass+=body.mass;center+=body.mass*(part.translation-origin+rotation*body.center);
                anchored=anchored||std::binary_search(result.source_.anchors.begin(),result.source_.anchors.end(),part.sourceId);
                // Always calculate mass/COM/inertia from the complete proxy.
                // Only collision inputs change after the graph-owned fracture.
                const size_t collisionBoxes=profile==CollisionProfile::ReleasedShell?5:body.boxes.size();
                for(size_t boxIndex=0;boxIndex<collisionBoxes;++boxIndex) {
                    if(boxes.size()==geometry::kMaximumUnionInputBoxes)return fail("Imported section proxy exceeds the input box budget.");
                    const auto& box=body.boxes[boxIndex];
                    glm::dvec3 minimum(std::numeric_limits<double>::max()),maximum(-std::numeric_limits<double>::max());
                    for(int corner=0;corner<8;++corner) {
                        const auto p=part.translation-origin+rotation*glm::dvec3(corner&1?box.maximum.x:box.minimum.x,
                            corner&2?box.maximum.y:box.minimum.y,corner&4?box.maximum.z:box.minimum.z);
                        minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);
                    }
                    // Exact lattice representation only. Source translations are
                    // retained verbatim, never silently snapped into a new wall.
                    if(glm::any(glm::greaterThan(glm::abs(minimum-point(ticks(minimum))),glm::dvec3(.0001)))
                        ||glm::any(glm::greaterThan(glm::abs(maximum-point(ticks(maximum))),glm::dvec3(.0001))))
                        return fail("Imported section proxy is off the authored collision lattice.");
                    boxes.push_back({{ticks(minimum),ticks(maximum)},partIndex*sourceStride+uint32_t(boxIndex+1)});
                }
            }
            center/=mass;
            for(const auto partIndex:members) {
                const auto& part=result.source_.parts[partIndex];const auto& body=physicals[partIndex];
                const auto rotation=glm::mat3_cast(glm::normalize(part.rotation));
                const auto offset=part.translation-origin+rotation*body.center-center;
                inertia+=rotation*body.inertia*glm::transpose(rotation)
                    +body.mass*(glm::dmat3(glm::dot(offset,offset))-glm::outerProduct(offset,offset));
            }
            physics::RigidMassInput massInput{mass,{center.x,center.y,center.z},{}};
            for(int row=0;row<3;++row)for(int column=0;column<3;++column)massInput.inertiaAboutCenter[size_t(row*3+column)]=inertia[column][row];
            geometry::BoxUnionIssue unionIssue;const auto volume=geometry::BoxUnion::compile(boxes,unionIssue);
            if(!volume)return fail("Imported section exterior union exceeded its geometry/work budget ("+std::to_string(int(unionIssue.error))+").");
            physics::AuthoredShapeIssue issue;auto shape=physics::AuthoredShape::prepare(*volume,massInput,issue);
            if(!shape)return fail("Imported section mass/collision representation was refused.");
            result.roots_.push_back({result.source_.parts[first].sourceId,anchored,origin,std::move(members),std::move(*shape),massInput});
        }
        error.clear();return result;
    } catch(const std::bad_alloc&) {return fail("Imported section preparation ran out of memory.");}
}

std::optional<ImportedAssembly> ImportedAssembly::prepareCut(uint64_t expectedRevision,
    std::span<const uint64_t> bondIds,std::span<const uint64_t> anchorPartIds,std::string& error) const {
    error.clear();const auto fail=[&](const char* message)->std::optional<ImportedAssembly>{error=message;return {};};
    if(expectedRevision!=source_.revision)return fail("Imported cut has a stale topology revision.");
    if(source_.revision==std::numeric_limits<uint64_t>::max())return fail("Imported topology revision exhausted.");
    if((bondIds.empty()&&anchorPartIds.empty())||bondIds.size()>source_.bonds.size()||anchorPartIds.size()>source_.anchors.size())
        return fail("Imported cut must name existing bonds or external supports.");
    try {
        auto next=source_;std::set<uint64_t> cuts;
        for(const auto id:bondIds) {
            if(!cuts.insert(id).second)return fail("Imported cut repeats a bond.");
            auto bond=std::find_if(next.bonds.begin(),next.bonds.end(),[&](const auto& b){return b.id==id;});
            if(bond==next.bonds.end()||!bond->active)return fail("Imported cut names an unknown or already cut bond.");
            bond->active=false;
        }
        cuts.clear();
        for(const auto id:anchorPartIds) {
            if(!cuts.insert(id).second)return fail("Imported cut repeats an external support.");
            const auto anchor=std::find(next.anchors.begin(),next.anchors.end(),id);
            if(anchor==next.anchors.end())return fail("Imported cut names an unknown external support.");
            next.anchors.erase(anchor);
        }
        ++next.revision;return prepare(next,error,collisionProfile_);
    } catch(const std::bad_alloc&) {return fail("Imported cut preparation ran out of memory.");}
}
std::optional<ImportedAssembly> ImportedAssembly::prepareRemovePart(uint64_t expectedRevision,
    uint64_t sourceId,std::string& error) const {
    error.clear();const auto fail=[&](const char* message)->std::optional<ImportedAssembly>{error=message;return {};};
    if(expectedRevision!=source_.revision)return fail("Imported removal has a stale topology revision.");
    if(source_.revision==std::numeric_limits<uint64_t>::max())return fail("Imported topology revision exhausted.");
    if(!rootForPart(sourceId))return fail("Imported removal names an unknown or already removed part.");
    if(source_.parts.size()==1)return fail("Imported removal cannot erase the entire bounded section.");
    try {
        auto next=source_;
        std::erase_if(next.parts,[&](const auto& part){return part.sourceId==sourceId;});
        std::erase_if(next.bonds,[&](const auto& bond){return bond.lowerPart==sourceId||bond.upperPart==sourceId;});
        std::erase(next.anchors,sourceId);
        ++next.revision;return prepare(next,error,collisionProfile_);
    } catch(const std::bad_alloc&) {return fail("Imported removal preparation ran out of memory.");}
}
std::optional<ImportedAssembly> ImportedAssembly::prepareReleasedShell(uint64_t expectedRevision,std::string& error) const {
    error.clear();
    if(expectedRevision!=source_.revision||source_.revision==UINT64_MAX) {
        error="Imported collision profile has a stale or exhausted revision.";return {};
    }
    if(collisionProfile_!=CollisionProfile::Detailed) {
        error="Imported collision profile is already released.";return {};
    }
    try {
        auto next=source_;++next.revision;
        return prepare(next,error,CollisionProfile::ReleasedShell);
    } catch(const std::bad_alloc&) {error="Imported collision profile preparation ran out of memory.";return {};}
}
std::optional<size_t> ImportedAssembly::rootForPart(uint64_t id) const noexcept {
    for(size_t root=0;root<roots_.size();++root)for(const auto index:roots_[root].partIndices)
        if(source_.parts[index].sourceId==id)return root;
    return {};
}
glm::dmat4 ImportedAssembly::partMatrix(uint32_t index) const noexcept {
    if(index>=source_.parts.size())return glm::dmat4(1);
    const auto& p=source_.parts[index];return glm::translate(glm::dmat4(1),p.translation)*glm::mat4_cast(glm::normalize(p.rotation));
}
std::optional<uint64_t> ImportedAssembly::partForFeature(uint32_t sourceLabel) const noexcept {
    if(!sourceLabel)return {};
    const uint32_t index=(sourceLabel-1)/sourceStride,box=(sourceLabel-1)%sourceStride;
    if(index>=source_.parts.size())return {};
    const auto* spec=importedPartCatalog(source_.parts[index].partNumber);
    const auto boxCount=collisionProfile_==CollisionProfile::ReleasedShell?5u:5u+uint32_t(spec?spec->connectorCount():0)*4u;
    if(!spec||box>=boxCount)return {};
    return source_.parts[index].sourceId;
}
std::optional<std::vector<physics::AuthoredRootMotion>> ImportedAssembly::inheritMotion(
    const ImportedAssembly& before,std::span<const physics::AuthoredRootMotion> motion,std::string& error) const {
    error.clear();const auto fail=[&](const char* message)->std::optional<std::vector<physics::AuthoredRootMotion>>{error=message;return {};};
    if(source_.assetId!=before.source_.assetId||source_.sourceSha256!=before.source_.sourceSha256
        ||before.source_.revision==UINT64_MAX||source_.revision!=before.source_.revision+1
        ||motion.size()!=before.roots_.size()||source_.parts.size()!=before.source_.parts.size())
        return fail("Imported child motion does not match its parent asset/revision.");
    if(before.collisionProfile_==CollisionProfile::ReleasedShell&&collisionProfile_!=before.collisionProfile_)
        return fail("Imported child cannot restore clutch collision without an explicit rebuild.");
    for(size_t i=0;i<source_.parts.size();++i) {
        const auto& a=source_.parts[i];const auto& b=before.source_.parts[i];
        if(a.sourceId!=b.sourceId||a.sourcePath!=b.sourcePath||a.partNumber!=b.partNumber||a.colour!=b.colour
            ||a.meshNode!=b.meshNode||a.translation!=b.translation||a.rotation!=b.rotation)
            return fail("Imported child changes parent part identity or geometry.");
    }
    if(source_.bonds.size()!=before.source_.bonds.size())return fail("Imported child changes parent bond identity.");
    for(size_t i=0;i<source_.bonds.size();++i) {
        const auto& a=source_.bonds[i];const auto& b=before.source_.bonds[i];
        if(a.id!=b.id||a.lowerPart!=b.lowerPart||a.upperPart!=b.upperPart||a.lowerSlot!=b.lowerSlot
            ||a.upperSlot!=b.upperSlot||(a.active&&!b.active))return fail("Imported child adds or retargets a parent bond.");
    }
    for(const auto id:source_.anchors)
        if(!std::binary_search(before.source_.anchors.begin(),before.source_.anchors.end(),id))
            return fail("Imported child adds external support.");
    try {
        std::vector<physics::AuthoredRootMotion> result;result.reserve(roots_.size());
        for(const auto& root:roots_) {
            const auto parent=before.rootForPart(root.key);if(!parent)return fail("Imported child has no parent root.");
            for(const auto index:root.partIndices)
                if(before.rootForPart(source_.parts[index].sourceId)!=parent)return fail("Imported child merges previously separate roots.");
            const auto& state=motion[*parent];
            physics::AuthoredFrameError issue;
            if(!physics::AuthoredBodyFrame(before.roots_[*parent].shape).bodyMotion(state,issue))return fail("Imported parent motion is invalid.");
            const auto offset=glm::dquat(state.orientation)*(root.origin-before.roots_[*parent].origin);
            const auto position=physics::translateAuthoredPosition(state.position,offset,issue);
            if(!position)return fail("Imported child motion exceeds the physical world frame.");
            auto child=state;child.position=*position;
            const auto velocity=glm::dvec3(state.originVelocity)+glm::cross(glm::dvec3(state.angularVelocity),offset);
            if(!finite(velocity)||glm::any(glm::greaterThan(glm::abs(velocity),glm::dvec3(std::numeric_limits<float>::max()))))
                return fail("Imported child velocity cannot be represented.");
            child.originVelocity=glm::vec3(velocity);
            if(!physics::AuthoredBodyFrame(root.shape).bodyMotion(child,issue))return fail("Imported child motion is invalid.");
            result.push_back(child);
        }
        return result;
    }catch(const std::bad_alloc&) {return fail("Imported child motion preparation ran out of memory.");}
}

} // namespace voxy::game::adventure

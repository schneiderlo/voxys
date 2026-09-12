#include "game/assets/cove_environment.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <algorithm>
#include <fstream>
#include <map>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::assets {
namespace {
using Bytes=std::vector<uint8_t>;
using Json=nlohmann::json;
struct Package {
    std::map<std::string,Bytes,std::less<>> files;
    std::vector<std::pair<std::string,size_t>> requests;
    Package() {
        for(const std::string kind:{"scenery","gantry"})for(int i=0;i<3;++i)load(kind+"-lod"+std::to_string(i)+".vmesh");
        load("manifest.json");
    }
    void load(const std::string& name) {
        std::ifstream input("data/salvage/cove-environment-r01/"+name,std::ios::binary);
        if(!input)throw std::runtime_error("missing installed environment test data");
        files[name]=Bytes(std::istreambuf_iterator<char>(input),{});
    }
    CookedPartByteProvider provider() {
        return [this](std::string_view name,size_t cap,std::string&)->std::optional<Bytes> {
            requests.emplace_back(name,cap);
            const auto found=files.find(name);return found==files.end()?std::nullopt:std::optional(found->second);
        };
    }
    Json manifest() const {return Json::parse(files.at("manifest.json"));}
    void manifest(const Json& value) {const auto text=value.dump();files["manifest.json"]={text.begin(),text.end()};}
    void rehash(const std::string& file) {
        auto m=manifest();for(auto& record:m["lods"])if(record["filename"]==file)
            record["sha256"]=core::sha256Hex(core::sha256(std::as_bytes(std::span(files.at(file)))));
        manifest(m);
    }
};

TEST(CoveEnvironment, InstalledThreeLodPairsKeepCollisionEntryAndExactResidentCapacity) {
    Package package;std::string error;
    const auto asset=loadCoveEnvironment(package.provider(),error);ASSERT_TRUE(asset)<<error;
    EXPECT_TRUE(error.empty());EXPECT_EQ(package.requests.size(),7u);
    EXPECT_EQ(package.requests.front(),(std::pair<std::string,size_t>{"manifest.json",64u*1024u}));
    for(size_t i=1;i<package.requests.size();++i)EXPECT_EQ(package.requests[i].second,1024u*1024u);
    EXPECT_EQ(asset->collision.size(),39u);EXPECT_EQ(asset->gpuBytes,1241888u);
    EXPECT_LE(asset->gpuBytes,kCoveEnvironmentMaximumGpuBytes);
    for(size_t i=0;i<kCoveEnvironmentLods;++i) {
        const auto& scene=asset->scenery[i].prefab;const auto& gantry=asset->gantry[i].prefab;
        EXPECT_EQ(scene.counts.meshInstances,5u);EXPECT_EQ(gantry.counts.meshInstances,1u);
        EXPECT_EQ(scene.counts.expandedDraws,i==2?19u:20u);EXPECT_EQ(gantry.counts.expandedDraws,4u);
        EXPECT_EQ(scene.renderToCanonical.value,12u);EXPECT_EQ(gantry.renderToCanonical.value,12u);
        EXPECT_NEAR(gantry.canonicalBounds.maximum.y,7.44,1e-5);
        EXPECT_NEAR(gantry.canonicalBounds.minimum.x,-4.2,1e-5);
        EXPECT_LT(scene.canonicalBounds.minimum.x,-17.6);EXPECT_GT(scene.canonicalBounds.maximum.y,6.7);
    }
    // Actual lattice entry rises from the unchanged1.28m dock in four .32m
    // steps; no box crosses the old navigation/cargo/boat channel.
    for(int32_t i=0;i<4;++i) {
        const auto it=std::find_if(asset->collision.begin(),asset->collision.end(),[&](const auto& box){
            return box.minimum==construction::GridPosition{400+i*25,32,-2100}
                &&box.maximum==construction::GridPosition{425+i*25,80+i*16,-2000};});
        EXPECT_NE(it,asset->collision.end());
    }
    for(const auto& b:asset->collision)EXPECT_TRUE(b.maximum.x<=-450||b.minimum.x>=400||b.maximum.z<=-3850||b.minimum.z>=-2350);
}

TEST(CoveEnvironment, InvalidManifestRefusesBeforeAnyPayloadOrFallbackRequest) {
    for(int mutation=0;mutation<12;++mutation) {
        SCOPED_TRACE(mutation);Package package;auto m=package.manifest();
        switch(mutation) {
        case 0:package.files["manifest.json"]=Bytes(64u*1024u+1u,' ');break;
        case 1:{auto text=m.dump();text.insert(1,"\"schema\":1,");package.files["manifest.json"]={text.begin(),text.end()};break;}
        case 2:m["profile"]="legacy";package.manifest(m);break;
        case 3:m["lods"][5]["filename"]="../other.vmesh";package.manifest(m);break;
        case 4:m["lods"][4]["sha256"]=std::string(64,'G');package.manifest(m);break;
        case 5:m["lods"].erase(2);package.manifest(m);break;
        case 6:m["collision"]=Json::array();package.manifest(m);break;
        case 7:while(m["collision"].size()<=48)m["collision"].push_back(m["collision"][0]);package.manifest(m);break;
        case 8:m["collision"][0]["minimum"][0]=500.5;package.manifest(m);break;
        case 9:m["collision"][0]["minimum"]={0,0,-2700};m["collision"][0]["maximum"]={50,50,-2650};package.manifest(m);break;
        case 10:m["gantry_collision"][0]["maximum"][1]=353;package.manifest(m);break;
        case 11:m["collision"][0]["maximum"]=m["collision"][0]["minimum"];package.manifest(m);break;
        }
        std::string error;EXPECT_FALSE(loadCoveEnvironment(package.provider(),error));EXPECT_FALSE(error.empty());
        ASSERT_EQ(package.requests.size(),1u);
    }
}

TEST(CoveEnvironment, CorruptOversizedAndRehashedDisplacedModelsCannotBecomeScenery) {
    for(int mutation=0;mutation<5;++mutation) {
        SCOPED_TRACE(mutation);Package package;auto& data=package.files.at("scenery-lod0.vmesh");std::string error;
        if(mutation==0)data.back()^=1;
        else if(mutation==1)data.resize(1024u*1024u+1u);
        else if(mutation==2)data.clear();
        else if(mutation==3){data[0]^=0xff;package.rehash("scenery-lod0.vmesh");}
        else {
            moto::VmeshData model;ASSERT_TRUE(moto::readVmesh(data.data(),data.size(),&model,&error))<<error;
            model.nodes[0].translation[0]+=5;ASSERT_TRUE(moto::writeVmesh(model,&data,&error))<<error;
            package.rehash("scenery-lod0.vmesh"); // Digest is correct; real contact geometry still must fail.
        }
        EXPECT_FALSE(loadCoveEnvironment(package.provider(),error));EXPECT_FALSE(error.empty());
        ASSERT_EQ(package.requests.size(),2u);
    }
}

TEST(CoveEnvironment, RehashedCollisionCannotAddInvisibleWallsOrLoseTheGantryOpening) {
    for(int mutation=0;mutation<2;++mutation) {
        SCOPED_TRACE(mutation);Package package;auto m=package.manifest();
        if(mutation==0)m["collision"].push_back({{"minimum",{1800,0,-1900}},{"maximum",{1820,80,-1880}}});
        else {m["gantry_collision"][8]["minimum"][1]=0;}
        package.manifest(m);std::string error;
        EXPECT_FALSE(loadCoveEnvironment(package.provider(),error));EXPECT_FALSE(error.empty());
        if(mutation==0)EXPECT_EQ(package.requests.size(),2u);else EXPECT_EQ(package.requests.size(),1u);
    }
}

TEST(CoveEnvironment, OwnerReadmissionRecountsActualMeshesAndPreservesPriorOutputOnRefusal) {
    Package package;std::string error;const auto installed=loadCoveEnvironment(package.provider(),error);
    ASSERT_TRUE(installed)<<error;
    CoveEnvironmentAsset input=*installed,output;
    input.gpuBytes=0;input.decodedBytes=0;
    for(auto& lod:input.scenery)lod.prefab={};
    for(auto& lod:input.gantry)lod.prefab={};
    ASSERT_TRUE(prepareCoveEnvironment(input,output,error))<<error;
    EXPECT_EQ(output.gpuBytes,1241888u);EXPECT_EQ(output.decodedBytes,installed->decodedBytes);
    EXPECT_EQ(output.scenery[0].prefab.counts.expandedDraws,20u);
    EXPECT_EQ(output.gantry[2].prefab.counts.meshInstances,1u);
    for(int mutation=0;mutation<5;++mutation) {
        SCOPED_TRACE(mutation);input=*installed;
        if(mutation==0)input.scenery[2].mesh.nodes[0].translation[0]+=5;
        else if(mutation==1)input.scenery[0].mesh.indices.clear();
        else if(mutation==2)input.collision.push_back({{1800,0,-1900},{1820,80,-1880}});
        else if(mutation==3)input.collision[0]={{0,0,-2700},{50,50,-2650}};
        else {
            // Individually valid near meshes in every LOD exceed the complete
            // resident budget, even when the public totals claim zero.
            for(size_t i=1;i<3;++i){input.scenery[i]=input.scenery[0];input.gantry[i]=input.gantry[0];}
            input.gpuBytes=0;
        }
        EXPECT_FALSE(prepareCoveEnvironment(input,output,error));EXPECT_FALSE(error.empty());
        EXPECT_EQ(output.gpuBytes,installed->gpuBytes);
        EXPECT_EQ(output.scenery[2].mesh.vertices,installed->scenery[2].mesh.vertices);
        EXPECT_EQ(output.collision.size(),39u);
    }
    EXPECT_TRUE(prepareCoveEnvironment(output,output,error))<<error; // Aliasing stays transactional.
    EXPECT_EQ(output.gpuBytes,1241888u);
}

TEST(CoveEnvironment, StrictLocalGeometryPlacesAcrossNegativeMixedAndDistantCameraSectors) {
    Package package;std::string error;const auto asset=loadCoveEnvironment(package.provider(),error);
    ASSERT_TRUE(asset)<<error;
    const glm::dvec3 worldOrigin{-19,-200,-37};
    for(const auto sector:std::array<glm::dvec3,4>{{{-256,-256,-256},{0,-256,-256},{-256,0,0},{2048,-256,0}}}) {
        SCOPED_TRACE(sector.x);
        SCOPED_TRACE(sector.y);
        SCOPED_TRACE(sector.z);
        for(size_t i=0;i<kCoveEnvironmentLods;++i)for(const auto* lod:{&asset->scenery[i],&asset->gantry[i]}) {
            std::vector<RigidPrefabDraw> original,relative;
            ASSERT_TRUE(placeRigidPrefab(lod->prefab,glm::dmat4(1),{},60,original,error))<<error;
            ASSERT_TRUE(placeRigidPrefab(lod->prefab,glm::translate(glm::dmat4(1),worldOrigin-sector),{},60,relative,error))<<error;
            ASSERT_EQ(original.size(),relative.size());
            for(size_t draw=0;draw<original.size();++draw) {
                EXPECT_EQ(original[draw].meshIndex,relative[draw].meshIndex);
                // Recover the same canonical world point from both sector
                // frames. A second basis flip or origin omission fails here.
                const glm::dvec4 point{.15,.25,-.35,1};
                const auto actual=glm::dvec3(glm::dmat4(relative[draw].modelMatrix)*point)+sector;
                const auto expected=glm::dvec3(glm::dmat4(original[draw].modelMatrix)*point)+worldOrigin;
                EXPECT_LT(glm::length(actual-expected),.001);
            }
        }
    }
    auto input=*asset;CoveEnvironmentAsset output;ASSERT_TRUE(prepareCoveEnvironment(input,output,error))<<error;
    moto::VmeshVertex vertex{};std::memcpy(&vertex,input.scenery[0].mesh.vertices.data(),sizeof(vertex));
    vertex.position[0]=101;std::memcpy(input.scenery[0].mesh.vertices.data(),&vertex,sizeof(vertex));
    // Public cached runtime limits cannot permit out-of-profile source data.
    EXPECT_FALSE(prepareCoveEnvironment(input,output,error));EXPECT_NE(error.find("position/normal"),std::string::npos)<<error;
    EXPECT_EQ(output.gpuBytes,1241888u);EXPECT_EQ(output.scenery[0].mesh.vertices,asset->scenery[0].mesh.vertices);
}
} // namespace
} // namespace voxy::game::assets

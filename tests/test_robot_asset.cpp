#include "game/assets/robot_asset.hpp"
#include "game/adventure/adventure_presentation.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>
#include <map>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::assets {
namespace {
using Bytes=std::vector<uint8_t>;
using Json=nlohmann::json;
struct RobotPackage {
    std::map<std::string,Bytes,std::less<>> files;
    std::vector<std::pair<std::string,size_t>> requests;
    explicit RobotPackage(bool adventure = false)
        :RobotPackage(adventure ? "data/adventure/human-r01/" : "data/salvage/robot-r01/",
                      adventure ? "human.vmesh" : "robot.vmesh") {}
    RobotPackage(std::string_view directory,std::string_view model) {
        for(const auto file:{std::string_view("manifest.json"),model}) {
            std::ifstream input(std::string(directory)+std::string(file),std::ios::binary);
            if(!input)throw std::runtime_error("missing installed robot runfile");
            files[std::string(file)]=Bytes(std::istreambuf_iterator<char>(input),{});
        }
    }
    CookedPartByteProvider provider() {
        return [this](std::string_view name,size_t cap,std::string&)->std::optional<Bytes> {
            requests.emplace_back(name,cap);
            const auto found=files.find(name);if(found==files.end())return {};
            // Deliberately permit bad providers to over-return: loader must
            // independently enforce its byte cap before hashing/parsing.
            return found->second;
        };
    }
    void manifest(const Json& document) {const auto text=document.dump();files["manifest.json"]={text.begin(),text.end()};}
    Json manifest() const {return Json::parse(files.at("manifest.json"));}
};

TEST(RobotAsset, InstalledPackageUsesTwoBoundedImmutableSnapshotsAndStrictRealAnimationAdmission) {
    RobotPackage package;std::string error="old refusal";
    const auto robot=loadRobotAsset(package.provider(),error);
    ASSERT_TRUE(robot)<<error;EXPECT_TRUE(error.empty());
    EXPECT_EQ(package.requests,(std::vector<std::pair<std::string,size_t>>{
        {"manifest.json",16u*1024u},{"robot.vmesh",2u*1024u*1024u}}));
    EXPECT_EQ(robot->mesh.anims.size(),kRobotClips.size());
    EXPECT_LE(robot->prefab.counts.gpuBytes,1024u*1024u);
    EXPECT_LE(robot->prefab.counts.decodedBytes,2u*1024u*1024u);
    RigidAnimationPose pose;
    ASSERT_TRUE(sampleRigidAnimation(*robot,robot->clips[1],.2,true,{},glm::dmat4(1),pose,error))<<error;
    EXPECT_GT(pose.drawCount,0u);EXPECT_TRUE(pose.bounds.valid);
}

TEST(RobotAsset, AdventureBrickPersonAdmitsItsOwnPackageAndPreservesLegacyIdentityBoundary) {
    RobotPackage person(true),legacy;std::string error;
    const auto actor=loadHumanAsset(person.provider(),error);
    ASSERT_TRUE(actor)<<error;
    EXPECT_EQ(person.requests,(std::vector<std::pair<std::string,size_t>>{
        {"manifest.json",16u*1024u},{"human.vmesh",2u*1024u*1024u}}));
    EXPECT_EQ(actor->mesh.anims.size(),kRobotClips.size());
    EXPECT_LE(actor->prefab.counts.gpuBytes,1024u*1024u);
    for(const auto clip:actor->clips) {
        RigidAnimationPose pose;
        ASSERT_TRUE(sampleRigidAnimation(*actor,clip,.2,true,{},glm::dmat4(1),pose,error))<<error;
        EXPECT_TRUE(pose.bounds.valid);EXPECT_GT(pose.drawCount,0u);
    }
    person.requests.clear();
    EXPECT_FALSE(loadRobotAsset(person.provider(),error));
    EXPECT_EQ(person.requests.size(),1u);
    EXPECT_FALSE(loadHumanAsset(legacy.provider(),error));
    EXPECT_EQ(legacy.requests.size(),1u);
}

TEST(RobotAsset, ResidentRolesUseDistinctInstalledIdentitiesAndCannotSubstituteEachOther) {
    constexpr std::array names{"moss","rivet","lumen"};
    for(uint32_t id=1;id<=3;++id) {
        SCOPED_TRACE(id);RobotPackage package;package.files.clear();package.requests.clear();
        for(const char* file:{"manifest.json","character.vmesh"}) {
            std::ifstream input(std::string("data/adventure/residents-r01/")+names[id-1]+"/"+file,std::ios::binary);
            ASSERT_TRUE(input);package.files[file]=Bytes(std::istreambuf_iterator<char>(input),{});
        }
        std::string error;const auto resident=loadResidentAsset(id,package.provider(),error);
        ASSERT_TRUE(resident)<<error;
        EXPECT_EQ(resident->mesh.anims.size(),kRobotClips.size());
        EXPECT_LE(resident->prefab.counts.gpuBytes,1024u*1024u);
        EXPECT_FALSE(loadResidentAsset(id%3+1,package.provider(),error));
        EXPECT_FALSE(loadHumanAsset(package.provider(),error));
        EXPECT_FALSE(loadRobotAsset(package.provider(),error));
        package.requests.clear();
        EXPECT_FALSE(loadResidentAsset(0,package.provider(),error));
        EXPECT_FALSE(loadResidentAsset(4,package.provider(),error));
        EXPECT_TRUE(package.requests.empty());
    }
}

TEST(RobotAsset, WoodlandRaiderAdmitsBoundedInstalledPackageAndAllEightRealClips) {
    RobotPackage package("data/adventure/raider-r01/","character.vmesh");std::string error="old refusal";
    const auto raider=loadRaiderAsset(package.provider(),error);
    ASSERT_TRUE(raider)<<error;EXPECT_TRUE(error.empty());
    EXPECT_EQ(package.requests,(std::vector<std::pair<std::string,size_t>>{
        {"manifest.json",16u*1024u},{"character.vmesh",2u*1024u*1024u}}));
    EXPECT_EQ(raider->mesh.anims.size(),kRobotClips.size());
    EXPECT_LE(raider->prefab.counts.gpuBytes,1024u*1024u);
    for(const auto clip:raider->clips) {
        RigidAnimationPose pose;
        ASSERT_TRUE(sampleRigidAnimation(*raider,clip,.2,true,{},glm::dmat4(1),pose,error))<<error;
        EXPECT_TRUE(pose.bounds.valid);EXPECT_GT(pose.drawCount,0u);
    }
}

TEST(RobotAsset, RaiderAndFriendlyCharacterIdentitiesRejectCrossSubstitutionBeforeModelRead) {
    RobotPackage raider("data/adventure/raider-r01/","character.vmesh");std::string error;
    EXPECT_FALSE(loadRobotAsset(raider.provider(),error));
    EXPECT_FALSE(loadHumanAsset(raider.provider(),error));
    for(uint32_t id=1;id<=3;++id)EXPECT_FALSE(loadResidentAsset(id,raider.provider(),error));
    ASSERT_EQ(raider.requests.size(),5u);
    for(const auto& request:raider.requests)EXPECT_EQ(request.first,"manifest.json");
    for(const bool human:{false,true}) {
        RobotPackage friendly(human);EXPECT_FALSE(loadRaiderAsset(friendly.provider(),error));
        ASSERT_EQ(friendly.requests.size(),1u);EXPECT_FALSE(error.empty());
    }
    for(const char* role:{"moss","rivet","lumen"}) {
        RobotPackage friendly(std::string("data/adventure/residents-r01/")+role+"/","character.vmesh");
        EXPECT_FALSE(loadRaiderAsset(friendly.provider(),error));
        ASSERT_EQ(friendly.requests.size(),1u);EXPECT_FALSE(error.empty());
    }
}

TEST(RobotAsset, ActualWoodBeamPassesThroughPlayerGripWithoutStretchingAcrossAllEightClips) {
    RobotPackage package(true);std::string error;
    const auto player=loadHumanAsset(package.provider(),error);ASSERT_TRUE(player)<<error;
    std::ifstream input("data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh",std::ios::binary);
    ASSERT_TRUE(input);const Bytes bytes(std::istreambuf_iterator<char>(input),{});
    moto::VmeshData kit;ASSERT_TRUE(moto::readVmesh(bytes.data(),bytes.size(),&kit,&error))<<error;
    RigidPrefab prefab;ASSERT_TRUE(prepareRigidPrefab(kit,{0},{},prefab,error))<<error;
    const auto positions=[](const moto::VmeshData& data,uint32_t mesh) {
        std::vector<glm::dvec3> result;
        for(const auto& submesh:data.submeshes)if(submesh.meshIndex==mesh) {
            for(uint32_t i=0;i<submesh.indexCount;++i) {
                const auto* source=data.indices.data()+submesh.indexOffset+i*data.header.indexStride;
                uint32_t index=0;
                if(data.header.indexStride==2) {uint16_t small;std::memcpy(&small,source,2);index=small;}
                else std::memcpy(&index,source,4);
                moto::VmeshVertex vertex;std::memcpy(&vertex,data.vertices.data()+index*data.header.vertexStride,sizeof(vertex));
                result.emplace_back(vertex.position[0],vertex.position[1],vertex.position[2]);
            }
        }
        return result;
    };
    const uint32_t beamIndex=uint32_t(adventure::PieceKind::Beam)-1;
    const auto beam=positions(kit,beamIndex);ASSERT_FALSE(beam.empty());
    const auto handNode=player->mesh.nodes[player->anchors[2]].parent;ASSERT_GE(handNode,0);
    const auto handVertices=positions(player->mesh,player->mesh.nodes[static_cast<size_t>(handNode)].meshIndex);ASSERT_FALSE(handVertices.empty());
    const auto root=glm::translate(glm::dmat4(1),glm::dvec3(37,-5,-43))
        *glm::rotate(glm::dmat4(1),.7,glm::dvec3(0,1,0));
    for(size_t clip=0;clip<player->clips.size();++clip)for(int sample=0;sample<=8;++sample) {
        SCOPED_TRACE(clip);
        SCOPED_TRACE(sample);RigidAnimationPose pose;
        const auto index=player->clips[clip];const double time=static_cast<double>(player->mesh.anims[index].duration)*static_cast<double>(sample)/8.;
        ASSERT_TRUE(sampleRigidAnimation(*player,index,time,false,{},root,pose,error))<<error;
        const auto& hand=pose.anchors[2];const auto model=adventure::trailStaffModel(hand);
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)EXPECT_TRUE(std::isfinite(model[c][r]));
        const auto inverseHand=glm::inverse(hand);
        const auto handDraw=std::find_if(pose.draws.begin(),pose.draws.begin()+pose.drawCount,
            [&](const auto& draw){return draw.nodeIndex==uint32_t(handNode);});
        ASSERT_NE(handDraw,pose.draws.begin()+pose.drawCount);
        double openingRadius=1;
        for(const auto vertex:handVertices) {
            const auto local=glm::dvec3(inverseHand*glm::dmat4(handDraw->modelMatrix)*glm::dvec4(vertex,1));
            openingRadius=std::min(openingRadius,std::hypot(local.x,local.y));
        }
        EXPECT_GT(openingRadius,.044);double minimumZ=1,maximumZ=-1,maximumRadius=0;
        for(const auto vertex:beam) {
            const auto world=model*glm::dvec4(vertex,1);const auto local=glm::dvec3(inverseHand*world);
            for(int axis=0;axis<3;++axis)EXPECT_TRUE(std::isfinite(world[axis]));
            minimumZ=std::min(minimumZ,local.z);maximumZ=std::max(maximumZ,local.z);
            maximumRadius=std::max(maximumRadius,std::hypot(local.x,local.y));
        }
        EXPECT_NEAR(minimumZ,-.074,1e-6);EXPECT_NEAR(maximumZ,.726,1e-6);
        EXPECT_LT(maximumRadius,openingRadius-.01);
        // This actual Beam-space point is where its centre line crosses the
        // exported C-grip plane. It must remain the hand centre after animation.
        const auto grip=glm::dvec3(model*glm::dvec4(-.815,.16,0,1));
        EXPECT_LT(glm::length(grip-glm::dvec3(hand[3])),1e-6);
        const auto back=glm::dvec3(model*glm::dvec4(-1,.16,0,1));
        const auto tip=glm::dvec3(model*glm::dvec4(1,.16,0,1));
        EXPECT_NEAR(glm::length(tip-back),.8,1e-6);
        EXPECT_GT(glm::dot(glm::normalize(tip-back),glm::normalize(glm::dvec3(hand[2]))),.999999);
        if(clip==0&&sample==0) {
            const auto canonical=glm::dvec3(glm::inverse(root)*hand[3]);
            EXPECT_NEAR(canonical.x,.215,1e-6);EXPECT_NEAR(canonical.y,.653,1e-6);EXPECT_NEAR(canonical.z,-.026,1e-6);
        }
    }
}

TEST(RobotAsset, RaiderManifestAndRehashedCorruptMeshRemainStrictlyValidated) {
    for(int mutation=0;mutation<8;++mutation) {
        SCOPED_TRACE(mutation);RobotPackage package("data/adventure/raider-r01/","character.vmesh");
        auto manifest=package.manifest();
        switch(mutation) {
        case 0: manifest["asset_id"]="voxys-adventure-raider-r02";break;
        case 1: manifest["filename"]="../character.vmesh";break;
        case 2: manifest["profile"]="salvage-authored-v1";break;
        case 3: manifest["render_to_canonical"]=0;break;
        case 4: manifest["clips"].erase(0);break;
        case 5: manifest["maximum_draws"]=49;break;
        case 6: package.files["character.vmesh"].back()^=1;break;
        case 7: {
            auto& bytes=package.files["character.vmesh"];bytes[0]^=0xff;
            manifest["sha256"]=core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))));break;
        }
        }
        package.manifest(manifest);std::string error;
        EXPECT_FALSE(loadRaiderAsset(package.provider(),error));EXPECT_FALSE(error.empty());
        EXPECT_EQ(package.requests.size(),mutation<6?1u:2u);
    }
}

TEST(RobotAsset, BadManifestNeverRequestsAModelOrAlternativePath) {
    for(int mutation=0;mutation<8;++mutation) {
        SCOPED_TRACE(mutation);RobotPackage package;auto manifest=package.manifest();
        switch(mutation) {
        case 0: package.files["manifest.json"]=Bytes(16u*1024u+1u,' ');break;
        case 1: package.files["manifest.json"]={'{'};break;
        case 2: {
            auto text=manifest.dump();text.insert(1,"\"schema\":1,");
            package.files["manifest.json"]={text.begin(),text.end()};break;
        }
        case 3: manifest["profile"]="salvage-authored-v1";package.manifest(manifest);break;
        case 4: manifest["filename"]="../robot.vmesh";package.manifest(manifest);break;
        case 5: manifest["clips"].erase(0);package.manifest(manifest);break;
        case 6: manifest["anchors"][0]="invented_anchor";package.manifest(manifest);break;
        case 7: manifest["sha256"]=std::string(64,'G');package.manifest(manifest);break;
        }
        std::string error;EXPECT_FALSE(loadRobotAsset(package.provider(),error));EXPECT_FALSE(error.empty());
        ASSERT_EQ(package.requests.size(),1u);EXPECT_EQ(package.requests[0].first,"manifest.json");
    }
}

TEST(RobotAsset, DigestOversizeAndRehashedCorruptVmeshCannotReachAnAdmittedOwner) {
    for(int mutation=0;mutation<4;++mutation) {
        SCOPED_TRACE(mutation);RobotPackage package;
        auto& bytes=package.files["robot.vmesh"];ASSERT_GT(bytes.size(),8u);
        if(mutation==0)bytes.back()^=1; // Exact original hash must detect change.
        else if(mutation==1)bytes.resize(2u*1024u*1024u+1u);
        else if(mutation==2)bytes.clear();
        else {
            bytes[0]^=0xff;auto manifest=package.manifest();
            manifest["sha256"]=core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))));
            package.manifest(manifest); // A matching digest is not VMESH admission.
        }
        std::string error;EXPECT_FALSE(loadRobotAsset(package.provider(),error));EXPECT_FALSE(error.empty());
        ASSERT_EQ(package.requests.size(),2u);EXPECT_EQ(package.requests[1].first,"robot.vmesh");
    }
}
} // namespace
} // namespace voxy::game::assets

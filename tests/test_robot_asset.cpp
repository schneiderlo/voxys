#include "game/assets/robot_asset.hpp"
#include "core/sha256.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>
#include <map>

namespace voxy::game::assets {
namespace {
using Bytes=std::vector<uint8_t>;
using Json=nlohmann::json;
struct RobotPackage {
    std::map<std::string,Bytes,std::less<>> files;
    std::vector<std::pair<std::string,size_t>> requests;
    RobotPackage() {
        for(const char* file:{"manifest.json","robot.vmesh"}) {
            std::ifstream input(std::string("data/salvage/robot-r01/")+file,std::ios::binary);
            if(!input)throw std::runtime_error("missing installed robot runfile");
            files[file]=Bytes(std::istreambuf_iterator<char>(input),{});
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

#include "game/assets/rigid_animation.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::assets {
namespace {
uint32_t appendName(moto::VmeshData& data, std::string_view text) {
    if (data.stringBlob.empty()) data.stringBlob.push_back('\0');
    const auto result = static_cast<uint32_t>(data.stringBlob.size());
    data.stringBlob.append(text); data.stringBlob.push_back('\0');
    return result;
}
void number(moto::VmeshData& data, float value) {
    const auto offset = data.channelData.size(); data.channelData.resize(offset+4);
    std::memcpy(data.channelData.data()+offset,&value,4);
}
void replaceNumber(moto::VmeshData& data, size_t offset, float value) {
    std::memcpy(data.channelData.data()+offset,&value,4);
}
moto::VmeshData example() {
    moto::VmeshData data;
    data.header.flags = moto::kVmeshHasAnimations;
    data.header.vertexCount=3; data.header.indexCount=3; data.header.submeshCount=1;
    data.header.materialCount=1; data.header.meshCount=1; data.header.nodeCount=8;
    data.header.animCount=8; data.header.animChannelCount=8;
    const std::array<moto::VmeshVertex,3> vertices{{
        {{0,0,0},{0,0,1},{1,0,0,1},{},{},{}},
        {{1,0,0},{0,0,1},{1,0,0,1},{},{},{}},
        {{0,1,0},{0,0,1},{1,0,0,1},{},{},{}},
    }};
    const std::array<uint16_t,3> indices{0,1,2};
    data.vertices.resize(sizeof(vertices)); std::memcpy(data.vertices.data(),vertices.data(),sizeof(vertices));
    data.indices.resize(sizeof(indices)); std::memcpy(data.indices.data(),indices.data(),sizeof(indices));
    data.submeshes.push_back({0,3,0,0}); data.materials.emplace_back(); data.nodes.resize(8);
    data.nodes[0].nameOffset=appendName(data,"robot_root");
    data.nodes[1].nameOffset=appendName(data,"hinge"); data.nodes[1].parent=0; data.nodes[1].meshIndex=0;
    data.nodes[1].translation[1]=.5f;
    for (size_t i=0;i<kRobotAnchors.size();++i) {
        data.nodes[i+2].nameOffset=appendName(data,kRobotAnchors[i]);
        data.nodes[i+2].parent=1; data.nodes[i+2].translation[0]=1;
    }
    for (size_t i=0;i<kRobotClips.size();++i) {
        moto::VmeshAnim clip; clip.duration=1;clip.channelCount=1;
        clip.channelsOffset=data.animChannels.size()*sizeof(moto::VmeshAnimChannel);
        clip.nameOffset=appendName(data,kRobotClips[i]);data.anims.push_back(clip);
        moto::VmeshAnimChannel channel; channel.keysOffset=data.channelData.size();
        channel.nodeIndex=1;channel.path=moto::VmeshAnimPathRotation;channel.keyCount=2;
        data.animChannels.push_back(channel);
        number(data,0);number(data,1);
        for(float v:{0.f,0.f,0.f,1.f}) number(data,v);
        // Walk rotates 180 degrees around source Y. Other clips retain rest.
        for(float v: i==1 ? std::array<float,4>{0,1,0,0} : std::array<float,4>{0,0,0,1}) number(data,v);
    }
    return data;
}
void point(glm::dvec3 actual, glm::dvec3 expected) {
    for(int i=0;i<3;++i) EXPECT_NEAR(actual[i],expected[i],1e-6);
}

TEST(RigidAnimation, ExportedHierarchySamplingBoundsAndAnchorsUseOneBasis) {
    RigidAnimationAsset asset;std::string error;
    ASSERT_TRUE(prepareRigidAnimation(example(),asset,error))<<error;
    EXPECT_EQ(asset.prefab.renderToCanonical.value,12);
    EXPECT_EQ(asset.prefab.counts.gpuBytes,292u);
    EXPECT_EQ(asset.mesh.anims.size(),8u);
    RigidAnimationPose pose;
    const auto root=glm::translate(glm::dmat4(1),glm::dvec3(10,2,3));
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],.5,false,{},root,pose,error))<<error;
    ASSERT_EQ(pose.drawCount,1u);
    point(glm::dvec3(pose.draws[0].modelMatrix*glm::vec4(1,0,0,1)),{10,2.5,4});
    point(glm::dvec3(pose.anchors[3][3]),{10,2.5,4});
    point(pose.bounds.minimum,{10,2.5,3});point(pose.bounds.maximum,{10,3.5,4});
    const glm::dvec3 normal=glm::transpose(glm::inverse(glm::dmat3(pose.draws[0].modelMatrix)))*glm::dvec3(0,0,1);
    point(normal,{-1,0,0}); // Same normal transform consumed by MeshPath.
    RigidAnimationPose repeated;
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],.5,false,{},root,repeated,error))<<error;
    EXPECT_EQ(pose.draws[0].modelMatrix,repeated.draws[0].modelMatrix); // Paused time has no hidden clock.
}

TEST(RigidAnimation, LoopClampStepAndShortestQuaternionBlendAreDeterministic) {
    auto data=example();
    // A sign-flipped identity must never spin during interpolation or blending.
    for(size_t j=0;j<4;++j) replaceNumber(data,8+j*4,-(j==3?1.f:0.f));
    RigidAnimationAsset asset;std::string error;
    ASSERT_TRUE(prepareRigidAnimation(data,asset,error))<<error;
    RigidAnimationPose a,b,c;
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],.5,false,{},glm::dmat4(1),a,error));
    point(glm::dvec3(a.anchors[0][3]),{-1,.5,0});
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],1.5,true,{},glm::dmat4(1),a,error));
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],-.5,true,{},glm::dmat4(1),b,error));
    EXPECT_EQ(a.draws[0].modelMatrix,b.draws[0].modelMatrix);
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],0,false,RigidAnimationBlend{asset.clips[1],1,.5},glm::dmat4(1),c,error));
    EXPECT_EQ(a.draws[0].modelMatrix,c.draws[0].modelMatrix);
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],100,false,{},glm::dmat4(1),b,error));
    point(glm::dvec3(b.anchors[0][3]),{1,.5,0});
    data.animChannels[1].interpolation=moto::VmeshAnimInterpolationStep;
    ASSERT_TRUE(prepareRigidAnimation(data,asset,error));
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],.999,false,{},glm::dmat4(1),a,error));
    point(glm::dvec3(a.anchors[0][3]),{-1,.5,0});
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],1,false,{},glm::dmat4(1),a,error));
    point(glm::dvec3(a.anchors[0][3]),{1,.5,0});
}

TEST(RigidAnimation, OutgoingClipKeepsItsOwnLoopPolicyDuringCrossfade) {
    RigidAnimationAsset asset;std::string error;ASSERT_TRUE(prepareRigidAnimation(example(),asset,error))<<error;
    RigidAnimationPose clamped,looped,blend,legacy;
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],1.25,false,{},glm::dmat4(1),clamped,error));
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[1],1.25,true,{},glm::dmat4(1),looped,error));
    ASSERT_NE(clamped.draws[0].modelMatrix,looped.draws[0].modelMatrix);
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],.1,true,
        RigidAnimationBlend{asset.clips[1],1.25,1,false},glm::dmat4(1),blend,error));
    EXPECT_EQ(blend.draws[0].modelMatrix,clamped.draws[0].modelMatrix);
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],.1,false,
        RigidAnimationBlend{asset.clips[1],1.25,1,true},glm::dmat4(1),blend,error));
    EXPECT_EQ(blend.draws[0].modelMatrix,looped.draws[0].modelMatrix);
    ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],.1,true,
        RigidAnimationBlend{asset.clips[1],1.25,1},glm::dmat4(1),legacy,error));
    EXPECT_EQ(legacy.draws[0].modelMatrix,looped.draws[0].modelMatrix); // Existing callers retain their prior shared policy.
}

TEST(RigidAnimation, InvalidClipAndHierarchyAdmissionPreservesOutput) {
    const auto good=example();
    for (int mutation=0;mutation<14;++mutation) {
        auto bad=good;
        switch(mutation) {
        case 0: replaceNumber(bad,4,0);break; // Duplicate time.
        case 1: replaceNumber(bad,0,-1);break;
        case 2: replaceNumber(bad,8,1);break; // Nonunit quaternion.
        case 3: bad.animChannels[0].path=moto::VmeshAnimPathScale;break;
        case 4: bad.animChannels[0].nodeIndex=0;break; // Gameplay root motion.
        case 5: bad.animChannels[0].keysOffset=UINT64_MAX-3;break;
        case 6: bad.animChannels[0].keyCount=122;break;
        case 7: bad.nodes[1].parent=2;break; // Anchor/hinge cycle.
        case 8: bad.nodes[2].nameOffset=bad.nodes[3].nameOffset;break;
        case 9: bad.nodes[1].scale[0]=2;break;
        case 10: bad.anims[0].duration=std::numeric_limits<float>::quiet_NaN();break;
        case 11: bad.anims[1].channelsOffset=0;break;
        case 12: bad.header.flags|=moto::kVmeshHasSkin;break;
        case 13: bad.nodes[2].meshIndex=0;break;
        }
        RigidAnimationAsset output;output.prefab.counts.gpuBytes=123;
        std::string error;
        EXPECT_FALSE(prepareRigidAnimation(std::move(bad),output,error))<<mutation;
        EXPECT_FALSE(error.empty())<<mutation;EXPECT_EQ(output.prefab.counts.gpuBytes,123u);
    }
}

TEST(RigidAnimation, InvalidSamplePreservesLastSubmittedPose) {
    RigidAnimationAsset asset;std::string error;
    ASSERT_TRUE(prepareRigidAnimation(example(),asset,error));
    RigidAnimationPose pose;pose.drawCount=17;
    EXPECT_FALSE(sampleRigidAnimation(asset,100,0,false,{},glm::dmat4(1),pose,error));
    EXPECT_FALSE(sampleRigidAnimation(asset,0,std::numeric_limits<double>::infinity(),false,{},glm::dmat4(1),pose,error));
    EXPECT_FALSE(sampleRigidAnimation(asset,0,0,false,RigidAnimationBlend{0,0,1.1},glm::dmat4(1),pose,error));
    EXPECT_FALSE(sampleRigidAnimation(asset,0,0,false,{},glm::scale(glm::dmat4(1),glm::dvec3(2)),pose,error));
    EXPECT_EQ(pose.drawCount,17u);
}

TEST(RigidAnimation, InstalledRobotAllLodsHaveRealClipsAndBoundEveryDrawVertex) {
    for(const char* filename:{"robot.vmesh","lod1.vmesh","lod2.vmesh"}) {
        std::ifstream input(std::string("data/salvage/robot-r01/")+filename,std::ios::binary);
        ASSERT_TRUE(input.good())<<filename;
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
        moto::VmeshData mesh;std::string error;
        ASSERT_TRUE(moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))<<error;
        RigidAnimationAsset asset;
        ASSERT_TRUE(prepareRigidAnimation(std::move(mesh),asset,error))<<filename<<": "<<error;
        EXPECT_LE(asset.prefab.counts.gpuBytes,1024u*1024u);
        EXPECT_LE(asset.prefab.counts.expandedDraws,48u);
        EXPECT_LE(asset.prefab.counts.meshInstances,24u);
        RigidAnimationPose rest;
        ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[0],0,false,{},glm::dmat4(1),rest,error));
        EXPECT_GE(rest.bounds.minimum.y,-.001);EXPECT_NEAR(rest.bounds.maximum.y,1.7,.015);
        // Check actual indexed core vertices against the capsule hemispheres,
        // not just horizontal width or conservative mesh-box corners. Hands
        // and stride feet have an explicitly larger animated visual envelope.
        for(uint32_t draw=0;draw<rest.drawCount;++draw) {
            const auto& record=rest.draws[draw];
            const std::string_view nodeName=asset.mesh.name(asset.mesh.nodes[record.nodeIndex].nameOffset);
            if(nodeName!="robot_head"&&nodeName!="robot_torso"&&nodeName!="robot_pelvis")continue;
            for(const auto& submesh:asset.mesh.submeshes) {
                if(submesh.meshIndex!=record.meshIndex)continue;
                for(uint32_t i=0;i<submesh.indexCount;++i) {
                    uint32_t index=0;
                    const auto offset=submesh.indexOffset+uint64_t{i}*asset.mesh.header.indexStride;
                    if(asset.mesh.header.indexStride==2) {
                        uint16_t shortIndex=0;std::memcpy(&shortIndex,asset.mesh.indices.data()+offset,2);index=shortIndex;
                    } else std::memcpy(&index,asset.mesh.indices.data()+offset,4);
                    moto::VmeshVertex vertex;
                    std::memcpy(&vertex,asset.mesh.vertices.data()+uint64_t{index}*sizeof(vertex),sizeof(vertex));
                    const glm::dvec3 p=glm::dvec3(glm::dmat4(record.modelMatrix)*glm::dvec4(
                        vertex.position[0],vertex.position[1],vertex.position[2],1));
                    const glm::dvec3 nearest(0,std::clamp(p.y,.3,1.4),0);
                    EXPECT_LE(glm::length(p-nearest),.300001)<<filename<<" "<<nodeName;
                }
            }
        }
        for(size_t clip=0;clip<kRobotClips.size();++clip) {
            bool moved=false;
            for(int sample=0;sample<=8;++sample) {
                RigidAnimationPose pose;
                const double time=double(asset.mesh.anims[asset.clips[clip]].duration)*double(sample)/8;
                ASSERT_TRUE(sampleRigidAnimation(asset,asset.clips[clip],time,false,{},glm::dmat4(1),pose,error));
                ASSERT_EQ(pose.drawCount,rest.drawCount);
                for(uint32_t draw=0;draw<pose.drawCount;++draw) {
                    moved|=pose.draws[draw].modelMatrix!=rest.draws[draw].modelMatrix;
                    const auto& meshBound=asset.prefab.meshBounds[pose.draws[draw].meshIndex];
                    for(int corner=0;corner<8;++corner) {
                        const glm::dvec3 p=glm::dvec3(glm::dmat4(pose.draws[draw].modelMatrix)*glm::dvec4(
                            corner&1?meshBound.maximum.x:meshBound.minimum.x,
                            corner&2?meshBound.maximum.y:meshBound.minimum.y,
                            corner&4?meshBound.maximum.z:meshBound.minimum.z,1));
                        for(int axis=0;axis<3;++axis) {
                            EXPECT_GE(p[axis],pose.bounds.minimum[axis]-1e-6);
                            EXPECT_LE(p[axis],pose.bounds.maximum[axis]+1e-6);
                        }
                    }
                }
            }
            EXPECT_TRUE(moved)<<filename<<" clip "<<kRobotClips[clip];
        }
    }
}
} // namespace
} // namespace voxy::game::assets

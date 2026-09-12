#include "game/expedition/cove_environment_collision.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>

namespace voxy::game::expedition {
namespace {
TEST(CoveEnvironmentCollision, RealSceneryKeepsSceneFrameOpenApproachAndSharedContactSolids) {
    // Bazel exposes trusted installed files through symlink runfiles. Snapshot
    // those explicit test leaves; production no-follow provider is unchanged.
    const assets::CookedPartByteProvider read=[](std::string_view name,size_t,std::string&)
        ->std::optional<std::vector<uint8_t>> {
        std::ifstream file("data/salvage/cove-environment-r01/"+std::string(name),std::ios::binary);
        if(!file)return {};
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),{});
    };
    std::string error;const auto asset=assets::loadCoveEnvironment(read,error);
    ASSERT_TRUE(asset)<<error;
    const auto collision=CoveEnvironmentCollision::compile(asset->collision,error);ASSERT_TRUE(collision)<<error;
    EXPECT_TRUE(error.empty());EXPECT_EQ(collision->origin(),glm::dvec3(0));
    ASSERT_EQ(collision->boxes().size(),asset->collision.size());
    for(size_t i=0;i<asset->collision.size();++i) {
        EXPECT_EQ(collision->boxes()[i].minimum,asset->collision[i].minimum);
        EXPECT_EQ(collision->boxes()[i].maximum,asset->collision[i].maximum);
    }
    const auto& shape=collision->shape();EXPECT_LE(shape.cells().size(),512u);
    EXPECT_LE(shape.faces().size(),3072u);EXPECT_LE(shape.cost().bytes,256u*1024u);
    EXPECT_EQ(shape.rootBounds().minimum,(geometry::GridPosition{-882,-288,-3250}));
    EXPECT_EQ(shape.rootBounds().maximum,(geometry::GridPosition{1132,336,-1768}));
    const auto inside=[&](geometry::GridPosition p) {
        return std::any_of(shape.cells().begin(),shape.cells().end(),[&](const auto& b){
            return p.x>=b.minimum[0]&&p.x<b.maximum[0]&&p.y>=b.minimum[1]&&p.y<b.maximum[1]&&p.z>=b.minimum[2]&&p.z<b.maximum[2];});};
    EXPECT_TRUE(inside({410,70,-2050})); // First real step volume.
    EXPECT_FALSE(inside({410,90,-2050})); // Above that tread remains open.
    EXPECT_FALSE(inside({550,180,-2050})); // Full character entry into shed.
    EXPECT_TRUE(inside({550,100,-2050})); // Floor supports the same entry.
    EXPECT_FALSE(inside({25,64,-2700})); // Existing boat berth remains empty.
}

TEST(CoveEnvironmentCollision, ExactUnionRemovesInternalFacesWithoutFillingDoorSpace) {
    const std::array<assets::CoveEnvironmentBox,3> boxes{{
        {{500,0,-2000},{550,50,-1950}},{{550,0,-2000},{600,50,-1950}},
        {{500,100,-2000},{600,125,-1950}}}};
    std::string error;const auto result=CoveEnvironmentCollision::compile(boxes,error);ASSERT_TRUE(result)<<error;
    EXPECT_EQ(result->boxes().size(),3u);
    for(const auto& face:result->shape().faces()) {
        // The shared lower cube face atX550 disappears; lintel is separate.
        EXPECT_FALSE(face.axis==0&&face.minimum[0]==550&&face.maximum[1]<=50);
    }
    const auto bad=assets::CoveEnvironmentBox{{0,0,-2700},{50,50,-2650}};
    EXPECT_FALSE(CoveEnvironmentCollision::compile(std::span(&bad,1),error));
    EXPECT_FALSE(CoveEnvironmentCollision::compile({},error));
    const auto invalid=assets::CoveEnvironmentBox{{500,0,-2000},{500,50,-1950}};
    EXPECT_FALSE(CoveEnvironmentCollision::compile(std::span(&invalid,1),error));
    std::vector<assets::CoveEnvironmentBox> excess(49,boxes[0]);
    EXPECT_FALSE(CoveEnvironmentCollision::compile(excess,error));
    EXPECT_FALSE(error.empty());
    // Earlier immutable result is unaffected by every refused subsequent call.
    EXPECT_EQ(result->shape().rootBounds().maximum,(geometry::GridPosition{600,125,-1950}));
}
} // namespace
} // namespace voxy::game::expedition

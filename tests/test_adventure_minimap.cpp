#include "game/adventure/adventure_navigation.hpp"
#include <gtest/gtest.h>
#include <array>
#include <limits>

using namespace voxy::game::adventure;
namespace {
struct MapTerrain {
    std::vector<uint16_t> samples=std::vector<uint16_t>(129*129,28000);
    voxy::terrain::lego::Surface surface{samples,129,129,600.f,16.f};
};
std::array<uint8_t,4> pixel(const std::vector<uint8_t>& rgba,uint32_t x,uint32_t z) {
    const size_t i=(size_t(z)*AdventureNavigationCache::size+x)*4;
    return {rgba[i],rgba[i+1],rgba[i+2],rgba[i+3]};
}
}
TEST(AdventureMinimap, BearingsUseWorldNorthAndRemainFinite) {
    EXPECT_FLOAT_EQ(adventureNavigationBearing({0,-1}),0);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({1,0}),90);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({0,1}),180);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({-1,0}),270);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({-1,-1}),315);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({0,0}),0);
    EXPECT_FLOAT_EQ(adventureNavigationBearing({std::numeric_limits<double>::quiet_NaN(),1}),0);
}
TEST(AdventureMinimap, NorthUpCoastlineUsesActualWaterHeightAndTerrainPalette) {
    MapTerrain scene;
    for(size_t z=0;z<64;++z)for(size_t x=0;x<129;++x)scene.samples[z*129+x]=18000;
    AdventureNavigationCache cache;
    const auto map=cache.rasterize(scene.surface,-200,{0,0},1,{});
    ASSERT_EQ(map.size(),size_t{256}*256*4);
    const auto sea=pixel(map,128,32);
    EXPECT_GT(sea[2],sea[1]);EXPECT_GT(sea[1],sea[0]);EXPECT_EQ(sea[3],255);
    EXPECT_EQ(pixel(map,128,223),(std::array<uint8_t,4>{134,173,101,255}));
    const auto wrongDatum=cache.rasterize(scene.surface,0,{0,0},1,{});
    const auto floodedLand=pixel(wrongDatum,128,223);
    EXPECT_GT(floodedLand[2],floodedLand[1]);EXPECT_NE(floodedLand,pixel(map,128,223));
    EXPECT_FLOAT_EQ(cache.playerUv({0,-384}).y,.25f);
    EXPECT_FLOAT_EQ(cache.playerUv({384,0}).x,.75f);
}
TEST(AdventureMinimap, PlayerTicksReuseRasterAndGeometryChangesOnlyRepaintAcceptedFootprints) {
    MapTerrain scene;AdventureNavigationCache cache;
    const std::array footprints{AdventureNavigationFootprint{{0,0},{1,1},0xe57b66}};
    EXPECT_TRUE(cache.needsUpdate(scene.surface,-200,{0,0},1));
    const auto first=cache.rasterize(scene.surface,-200,{0,0},1,footprints);
    EXPECT_EQ(pixel(first,128,128),(std::array<uint8_t,4>{229,123,102,255}));
    EXPECT_EQ(cache.terrainRasterizations(),1u);
    EXPECT_FALSE(cache.needsUpdate(scene.surface,-200,{20,30},1));
    EXPECT_NEAR(double(cache.playerUv({20,30}).x),.5+20./1536,1e-7);
    EXPECT_TRUE(cache.needsUpdate(scene.surface,-200,{20,30},2));
    const auto removed=cache.rasterize(scene.surface,-200,{20,30},2,{});
    EXPECT_EQ(pixel(removed,128,128),(std::array<uint8_t,4>{134,173,101,255}));
    EXPECT_EQ(cache.terrainRasterizations(),1u);
    EXPECT_FALSE(cache.needsUpdate(scene.surface,-200,{20,30},2));
    EXPECT_TRUE(cache.needsUpdate(scene.surface,-200,{70,30},2));
    const auto moved=cache.rasterize(scene.surface,-200,{70,30},2,{});
    EXPECT_EQ(moved.size(),first.size());EXPECT_EQ(cache.terrainRasterizations(),2u);
    EXPECT_NEAR(double(cache.playerUv({70,30}).x),.5+(70.-128.)/1536,1e-7);
}
TEST(AdventureMinimap, FootprintsClipToMapAndMissingTerrainStaysOpaque) {
    AdventureNavigationCache cache;
    const std::array footprints{
        AdventureNavigationFootprint{{-1000,-1000},{-767,-767},0xf1eee4},
        AdventureNavigationFootprint{{800,800},{900,900},0xff0000},
        AdventureNavigationFootprint{{0,0},{-10,-10},0xff0000}};
    const auto map=cache.rasterize({},-200,{0,0},1,footprints);
    ASSERT_EQ(map.size(),size_t{256}*256*4);
    EXPECT_EQ(pixel(map,0,0),(std::array<uint8_t,4>{241,238,228,255}));
    EXPECT_EQ(pixel(map,255,255),(std::array<uint8_t,4>{58,119,143,255}));
    for(size_t i=3;i<map.size();i+=4)ASSERT_EQ(map[i],255);
}

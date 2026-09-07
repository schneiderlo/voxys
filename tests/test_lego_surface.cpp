#include <gtest/gtest.h>
#include "terrain/lego_surface.hpp"
#include "terrain/lego_layout_cache.hpp"
#include "physics/character/cpu_capsule_mover.hpp"
#include <cmath>
#include <vector>

namespace {
using namespace voxy::terrain::lego;
uint16_t raw(float y, float scale=8.0f) {
    return uint16_t(std::lround((double(y)/scale+1.0)*32767.5));
}

TEST(LegoSurface, QuantizesAllRawSamplesWithinHalfAPlate) {
    constexpr float scale=600.0f;
    float previous=-scale;
    for (uint32_t r=0; r<65536; ++r) {
        const double original=double(r)*1200.0/65535.0-600.0;
        const double expected=std::floor((original+600.0)/.32+.5)*.32-600.0;
        const float y=top(uint16_t(r),scale,1.0f);
        ASSERT_NEAR(y,expected,0.00013) << r;
        ASSERT_LE(std::abs(double(y)-original),.16013) << r;
        ASSERT_GE(y,previous);
        previous=y;
    }
}

TEST(LegoSurface, ChunkOwnershipCoversOnlyEqualLevelsWithoutChangingGround) {
    constexpr uint32_t n=65;
    std::vector<uint16_t> samples(n*n);
    for(uint32_t z=0;z<n;++z) for(uint32_t x=0;x<n;++x)
        samples[z*n+x]=raw(float((x+z)/8)*.32f);
    const auto before=samples;
    const Surface surface{samples,n,n,8,1};
    const auto a=buildLayout(surface,0,2816,7424), b=buildLayout(surface,0,2816,7424);
    EXPECT_EQ(a.cells,b.cells);
    EXPECT_EQ(samples,before);
    EXPECT_EQ(a.chunks,4u);
    EXPECT_EQ(a.cells.size()*sizeof(uint16_t),n*n*2u);
    EXPECT_GT(a.largeBricks,20u);
    uint32_t origins=0;
    for(uint32_t z=0;z<n-1;++z) for(uint32_t x=0;x<n-1;++x) {
        const uint16_t p=a.cells[z*n+x];
        ASSERT_TRUE(p&0x1000u);
        const uint32_t dx=p&3u, dz=(p>>2)&3u;
        const uint32_t w=((p>>4)&3u)+1u, d=((p>>6)&3u)+1u;
        ASSERT_GE(x,dx); ASSERT_GE(z,dz);
        const uint32_t ox=x-dx, oz=z-dz;
        EXPECT_LT(dx,w); EXPECT_LT(dz,d);
        EXPECT_EQ(ox/kChunkCells,(ox+w-1)/kChunkCells);
        EXPECT_EQ(oz/kChunkCells,(oz+d-1)/kChunkCells);
        EXPECT_LT((p>>8)&15u,12u);
        EXPECT_EQ(a.cells[oz*n+ox]&0xff0u,p&0xff0u);
        EXPECT_FLOAT_EQ(surface.cellTop(int(x),int(z)),surface.cellTop(int(ox),int(oz)));
        if(dx==0 && dz==0) ++origins;
    }
    EXPECT_EQ(origins,a.bricks);
    EXPECT_TRUE(buildLayout(Surface{samples,513,513,8,1},0).cells.empty());
}

TEST(LegoSurface, SphereContactsStudCapSideRimAndVerticalStep) {
    std::vector<uint16_t> data(25,raw(0));
    Surface surface{data,5,5,8,1};
    const auto cap=sphereContact(surface,{-.5f,.68f,-.5f},.5f);
    EXPECT_NEAR(cap.distance,0,1e-5); EXPECT_NEAR(cap.normal.y,1,1e-5);
    const auto side=sphereContact(surface,{-.16f,.09f,-.5f},.04f);
    EXPECT_NEAR(side.distance,0,1e-5); EXPECT_NEAR(side.normal.x,1,1e-5);
    const float diagonal=.05f/std::sqrt(2.0f);
    const auto rim=sphereContact(surface,{-.2f+diagonal,.18f+diagonal,-.5f},.05f);
    EXPECT_NEAR(rim.distance,0,1e-5);
    EXPECT_NEAR(rim.normal.x,1/std::sqrt(2.0f),1e-5);
    EXPECT_NEAR(rim.normal.y,1/std::sqrt(2.0f),1e-5);
    for(uint32_t z=0;z<5;++z) for(uint32_t x=2;x<5;++x) data[z*5+x]=raw(1.6f);
    const auto wall=sphereContact(surface,{-.1f,1.0f,-.5f},.1f);
    EXPECT_NEAR(wall.distance,0,1e-5); EXPECT_NEAR(wall.normal.x,-1,1e-5);
    EXPECT_NEAR(wall.normal.y,0,1e-5);
}

TEST(LegoSurface, CapsuleSupportIncludesStudsBetweenHeightProbes) {
    std::vector<uint16_t> data(25,raw(0));
    const Surface surface{data,5,5,8,1};
    EXPECT_NEAR(supportHeight(surface,{-.5f,-.5f},.4f),.18f,1e-5);
    EXPECT_NEAR(supportHeight(surface,{0,-.5f},.4f),.18f+std::sqrt(.16f-.04f)-.4f,1e-5);
    EXPECT_NEAR(supportHeight(surface,{0,0},.4f),0,1e-5);
}

TEST(LegoSurface, PlayerCrossesChunksAndPlateStepsButStopsAtCliff) {
    constexpr uint32_t w=97,h=33;
    std::vector<uint16_t> data(w*h);
    for(uint32_t z=0;z<h;++z) for(uint32_t x=0;x<w;++x)
        data[z*w+x]=raw(x>=72 ? 3.2f : x>=64 ? .64f : x>=32 ? .32f : 0.0f);
    voxy::physics::CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    ASSERT_TRUE(mover.setTerrain(data,w,h,8,1,true));
    voxy::physics::CharacterSettings settings;
    settings.radius=.4f; settings.height=1.8f; settings.stepUp=.5f; settings.stepDown=.5f;
    const auto character=mover.createCharacter({-40,.2f,-.5f},settings);
    ASSERT_NE(character,voxy::physics::InvalidCharacter);
    voxy::physics::CharacterMotion motion;
    for(int tick=0;tick<2400;++tick) {
        motion=mover.moveCharacter(character,{4,0,0},false,6,20,50,1.0f/120.0f);
        ASSERT_TRUE(std::isfinite(motion.position.y));
        ASSERT_LE(motion.position.x,23.61f);
        ASSERT_LT(motion.position.y,1.0f);
    }
    EXPECT_GT(motion.position.x,23.3f);
    EXPECT_GT(motion.position.y,.60f);
}
TEST(LegoSurface, PlayerLandsAndJumpsInNegativeHeightSector) {
    std::vector<uint16_t> data(17*17,raw(-195.0f,600.0f));
    voxy::physics::CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    ASSERT_TRUE(mover.setTerrain(data,17,17,600,1,true));
    voxy::physics::CharacterSettings settings;
    settings.radius=.4f; settings.height=1.8f;
    const auto character=mover.createCharacter({-.5f,-192.0f,-.5f},settings);
    ASSERT_NE(character,voxy::physics::InvalidCharacter);
    voxy::physics::CharacterMotion motion;
    const float support=top(data[0],600,1)+kStudHeight;
    for(int i=0;i<240;++i) motion=mover.moveCharacter(character,{0,0,0},false,6,20,50,1.0f/120);
    ASSERT_TRUE(motion.grounded);
    EXPECT_EQ(motion.sector.y,-1);
    EXPECT_NEAR(motion.position.y+256.0f*motion.sector.y,support,.002f);
    motion=mover.moveCharacter(character,{0,0,0},true,6,20,50,1.0f/120);
    EXPECT_FALSE(motion.grounded);
    EXPECT_GT(motion.velocity.y,0);
    for(int i=0;i<240;++i) motion=mover.moveCharacter(character,{0,0,0},false,6,20,50,1.0f/120);
    EXPECT_TRUE(motion.grounded);
    EXPECT_NEAR(motion.position.y+256.0f*motion.sector.y,support,.002f);
}
TEST(LegoSurface, ChunkBuilderMatchesStudyAndFullMapEdges) {
    constexpr uint32_t n=65;
    std::vector<uint16_t> small(n*n);
    for (uint32_t z=0;z<n;++z) for(uint32_t x=0;x<n;++x)
        small[z*n+x]=raw(float((x+z)/8)*.32f);
    const Surface study{small,n,n,8,1};
    const auto layout=buildLayout(study,0,2816,7424);
    for(uint32_t cz=0;cz<2;++cz) for(uint32_t cx=0;cx<2;++cx) {
        const auto chunk=buildChunk(study,cx,cz,0,2816,7424);
        for(uint32_t z=0;z<32;++z) for(uint32_t x=0;x<32;++x)
            ASSERT_EQ(chunk.cells[z*32+x],layout.cells[(cz*32+z)*n+cx*32+x]);
    }
    // Allocate only the authoritative samples: no full-world layout array.
    std::vector<uint16_t> full(8192u*8192u,raw(-195,600));
    const Surface world{full,8192,8192,600,1};
    EXPECT_TRUE(buildLayout(world,-200).cells.empty());
    const auto chunk=buildChunk(world,255,255,-200);
    EXPECT_GT(chunk.largeBricks,0u);
    for(uint32_t z=0;z<32;++z) for(uint32_t x=0;x<32;++x) {
        const uint16_t cell=chunk.cells[z*32+x];
        if(x==31||z==31) { EXPECT_EQ(cell,0); continue; }
        EXPECT_TRUE(cell&0x1000);
        EXPECT_LT((cell>>8)&15u,12u);
        const auto packed=LayoutCache::pack(cell,65535);
        EXPECT_TRUE(LayoutCache::matches(packed,65535));
        EXPECT_FALSE(LayoutCache::matches(packed,0));
    }
    EXPECT_EQ(buildChunk(world,256,0,-200).bricks,0u);
}

TEST(LegoSurface, LayoutCacheIsBoundedReusesRowsAndRejectsStaleSlots) {
    LayoutCache cache;
    cache.request(8192,8192,{2048,2048});
    EXPECT_EQ(cache.pendingCount(),1024u);
    const auto first=cache.next(); ASSERT_TRUE(first);
    EXPECT_EQ(first->x,64u); EXPECT_EQ(first->z,64u);
    cache.uploaded(*first);
    uint32_t processed=1;
    while(const auto q=cache.next()) {cache.uploaded(*q);++processed;}
    EXPECT_EQ(processed,1024u);
    cache.request(8192,8192,{2049,2050});
    EXPECT_EQ(cache.pendingCount(),0u);
    cache.request(8192,8192,{2080,2048});
    EXPECT_EQ(cache.pendingCount(),32u); // Only the newly visible row.
    while(const auto q=cache.next()) cache.uploaded(*q);
    cache.request(8192,8192,{4096,4096});
    EXPECT_EQ(cache.pendingCount(),1024u);
    const auto next=cache.next(); ASSERT_TRUE(next);
    EXPECT_EQ(first->slot(),next->slot());
    EXPECT_FALSE(cache.contains(*next));
    EXPECT_FALSE(LayoutCache::matches(LayoutCache::pack(0x1000,first->key),next->key));
    cache.uploaded(*next);
    EXPECT_TRUE(cache.contains(*next));
    EXPECT_FALSE(cache.contains(*first));
    EXPECT_FALSE(LayoutCache::matches(0u,0u));
    cache.invalidate();
    cache.request(8192,8192,{8191,8191});
    const auto edge=cache.next(); ASSERT_TRUE(edge);
    EXPECT_EQ(edge->key,65535u);
    EXPECT_LE(sizeof(cache),20u*1024u);
    EXPECT_EQ(kCacheGpuBytes,4u*1024u*1024u);
    EXPECT_EQ(kChunksPerFrame,4u);
}

TEST(LegoSurface, TerrainModeSwitchPreservesLiveCharacter) {
    std::vector<uint16_t> data(65*65,raw(0));
    voxy::physics::CpuCapsuleMoverWorld mover;
    ASSERT_TRUE(mover.initialize());
    ASSERT_TRUE(mover.setTerrain(data,65,65,8,1,false));
    voxy::physics::CharacterSettings settings;
    const auto character=mover.createCharacter({-.5f,2,-.5f},settings);
    ASSERT_NE(character,voxy::physics::InvalidCharacter);
    voxy::physics::CharacterMotion motion;
    for(const bool lego : {true,false,true}) {
        ASSERT_TRUE(mover.setTerrain(data,65,65,8,1,lego));
        for(int i=0;i<240;++i) motion=mover.moveCharacter(character,{0,0,0},false,6,20,50,1.0f/120);
        ASSERT_TRUE(motion.grounded);
        const float y=motion.position.y+256.0f*motion.sector.y;
        EXPECT_NEAR(y,lego ? kStudHeight : (float(data[0])/65535.0f*16.0f-8.0f),.003f);
    }
}
} // namespace

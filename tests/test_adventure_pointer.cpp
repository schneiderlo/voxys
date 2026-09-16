#include "game/adventure/adventure_pointer.hpp"
#include "game/adventure/spatial_queries.hpp"
#include "camera/camera.hpp"
#include "render/adventure_hud.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

using namespace voxy::game::adventure;
namespace {
const glm::dvec2 logicalExtent{1280,720};
// The last backing size includes independent per-axis renderer rounding.
const std::array<glm::uvec2,4> backingExtents{{{1280,720},{1920,1080},{2560,1440},{1919,1079}}};
using Id=voxy::game::construction::DurableId;
Id id(uint64_t counter) {
    voxy::game::construction::WorldNamespace world{};world.bytes[0]=29;
    return {world,counter};
}
const voxy::render::AdventureHudHit* hitAt(const voxy::render::AdventureHudLayout& layout,glm::dvec2 pointer) {
    const auto hit=std::find_if(layout.hits.begin(),layout.hits.end(),[&](const auto& item) {
        const glm::dvec4 bounds(item.bounds);
        return pointer.x>=bounds.x&&pointer.y>=bounds.y&&
               pointer.x<bounds.x+bounds.z&&pointer.y<bounds.y+bounds.w;
    });
    return hit==layout.hits.end()?nullptr:&*hit;
}
}

TEST(AdventurePointer, LogicalCenterAndQuarterPickActualPartsAcrossBackingScales) {
    std::vector<uint16_t> samples(64*64,32768);
    AdventureSpatialQueries queries;
    ASSERT_TRUE(queries.bindTerrain({samples,64,64,8.f,1.f}));
    voxy::Camera camera(glm::vec3{0,8,-10},glm::vec3{0,8,0});
    camera.setAspectRatio(1280,720);
    const glm::dvec3 origin(camera.position());
    const std::array<glm::vec2,2> targetNdc{{{0,0},{-.5f,.5f}}};
    std::array<AdventureSpatialQueries::Solid,2> solids{};
    for(size_t i=0;i<targetNdc.size();++i) {
        const auto center=origin+glm::dvec3(camera.screenToWorldRay(targetNdc[i]))*12.;
        solids[i]={id(1),id(i+2),center-glm::dvec3(.2),center+glm::dvec3(.2)};
    }
    ASSERT_TRUE(queries.publish(solids,1));
    const std::array<glm::dvec2,2> raw{{{640,360},{320,180}}};
    for(const auto backing:backingExtents) {
        SCOPED_TRACE(::testing::Message()<<backing.x<<"x"<<backing.y);
        // Match the real camera's backing aspect. Rounding may slightly change
        // its projection; the reference ray still uses the desired screen NDC.
        camera.setAspectRatio(backing.x,backing.y);
        for(size_t i=0;i<raw.size();++i) {
            const auto pointer=adventurePointer(raw[i],logicalExtent,backing);
            ASSERT_TRUE(pointer);
            const auto ray=glm::dvec3(camera.screenToWorldRay(glm::vec2(pointer->ndc)));
            const auto hit=queries.raycast(origin,ray,25);
            ASSERT_TRUE(hit.complete);ASSERT_TRUE(hit.hit);EXPECT_FALSE(hit.terrain);
            EXPECT_EQ(hit.part,id(i+2));
            const auto expected=queries.raycast(origin,glm::dvec3(camera.screenToWorldRay(targetNdc[i])),25);
            ASSERT_TRUE(expected.complete);ASSERT_TRUE(expected.hit);
            EXPECT_NEAR(glm::length(hit.point-expected.point),0,1e-9);
            if(backing.x!=1280) {
                // This is the former bug: raw CSS pixels divided by backing
                // dimensions miss the actual clicked part on a scaled canvas.
                const auto wrong=raw[i]/glm::dvec2(backing);
                const glm::vec2 wrongNdc{static_cast<float>(wrong.x*2-1),static_cast<float>(1-wrong.y*2)};
                const auto wrongHit=queries.raycast(origin,glm::dvec3(camera.screenToWorldRay(wrongNdc)),25);
                EXPECT_TRUE(!wrongHit.hit||wrongHit.part!=id(i+2));
            }
        }
    }
}

TEST(AdventurePointer, ActualCatalogAndDialogueHitsPreserveIntentAndDisabledStateAtAllTextScales) {
    using namespace voxy::render;
    AdventureHudContent content;
    content.title="Moss";
    content.rows={{"Floor","4 wood",true,10,0,641,2},
                  {"Workbench","8 wood / 4 scrap",false,10,0,642,13},
                  {"Close","",true,10,0,643,0}};
    content.selectedRow=1;
    for(const auto mode:{AdventureHudMode::Catalog,AdventureHudMode::Dialogue}) {
        content.mode=mode;
        for(const float scale:{1.f,1.25f,1.5f}) {
            content.textScale=scale;
            for(const auto backing:backingExtents) {
                SCOPED_TRACE(::testing::Message()<<backing.x<<"x"<<backing.y<<" text "<<scale);
                const auto layout=layoutAdventureHud(content,backing.x,backing.y);
                ASSERT_TRUE(layout.selectedVisible);
                for(size_t row=0;row<content.rows.size();++row) {
                    const auto displayed=std::find_if(layout.hits.begin(),layout.hits.end(),[&](const auto& item){return item.row==row;});
                    ASSERT_NE(displayed,layout.hits.end());
                    const glm::dvec4 bounds(displayed->bounds);
                    const glm::dvec2 center{bounds.x+bounds.z*.5,bounds.y+bounds.w*.5};
                    const auto cssPointer=center*logicalExtent/glm::dvec2(backing);
                    const auto pointer=adventurePointer(cssPointer,logicalExtent,backing);
                    ASSERT_TRUE(pointer);
                    const auto* hit=hitAt(layout,pointer->framebuffer);
                    ASSERT_NE(hit,nullptr);
                    EXPECT_EQ(hit->row,row);EXPECT_EQ(hit->action,10);
                    EXPECT_EQ(hit->intent,content.rows[row].intent);
                    EXPECT_EQ(hit->enabled,content.rows[row].enabled);
                }
            }
        }
    }
}

TEST(AdventurePointer, ResizeKeepsNormalizedAimAndOutsidePointersRemainMisses) {
    const glm::dvec2 raw{320,180};
    for(const auto backing:backingExtents) {
        const auto pointer=adventurePointer(raw,logicalExtent,backing);
        ASSERT_TRUE(pointer);
        EXPECT_EQ(pointer->ndc,glm::dvec2(-.5,.5));
        EXPECT_EQ(pointer->framebuffer,glm::dvec2(backing)*.25);
        voxy::render::AdventureHudContent content;
        content.mode=voxy::render::AdventureHudMode::Pause;
        content.rows={{"Resume","",true,10,0,129,0}};
        const auto layout=voxy::render::layoutAdventureHud(content,backing.x,backing.y);
        ASSERT_FALSE(layout.hits.empty());
        for(const auto outside:std::array<glm::dvec2,3>{{{-1,360},{1281,360},{640,721}}}) {
            const auto miss=adventurePointer(outside,logicalExtent,backing);
            ASSERT_TRUE(miss);
            EXPECT_TRUE(miss->ndc.x<-1||miss->ndc.x>1||miss->ndc.y<-1||miss->ndc.y>1);
            EXPECT_EQ(hitAt(layout,miss->framebuffer),nullptr);
        }
    }
}

TEST(AdventurePointer, InvalidExtentsAndNonfiniteOrOverflowingCoordinatesAreRefused) {
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    const auto infinity=std::numeric_limits<double>::infinity();
    const glm::dvec2 raw{640,360};
    const glm::uvec2 backing{1920,1080};
    for(const auto invalid:std::array<glm::dvec2,8>{{{0,720},{1280,0},{-1,720},{1280,-1},
                                                  {nan,720},{1280,nan},{infinity,720},{1280,infinity}}})
        EXPECT_FALSE(adventurePointer(raw,invalid,backing));
    for(const auto invalid:std::array<glm::dvec2,4>{{{nan,0},{0,nan},{infinity,0},{0,-infinity}}})
        EXPECT_FALSE(adventurePointer(invalid,logicalExtent,backing));
    EXPECT_FALSE(adventurePointer(raw,logicalExtent,{0,1080}));
    EXPECT_FALSE(adventurePointer(raw,logicalExtent,{1920,0}));
    EXPECT_FALSE(adventurePointer({std::numeric_limits<double>::max(),0},{1,1},backing));
    EXPECT_FALSE(adventurePointer(raw,{std::numeric_limits<double>::min(),720},backing));
}

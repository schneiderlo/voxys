#include "game/adventure/building_catalog.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/adventure_player.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace voxy::game::adventure {
namespace {
glm::dvec3 metres(geometry::GridPosition p) {return glm::dvec3(p.x,p.y,p.z)*.02;}
glm::dvec3 rotate(glm::dvec3 p,uint8_t yaw) {for(uint8_t i=0;i<yaw;++i)p={p.z,p.y,-p.x};return p;}
AdventureState onePiece(PieceKind kind,uint8_t yaw=0) {
    AdventureState state;state.world.bytes[0]=19;
    WorldStructure structure;structure.id=1;structure.parts.push_back({2,kind,{0,600,0},yaw,0});
    state.structures.push_back(structure);return state;
}
}
TEST(FrontierBuilding, FrozenLegacyIdentitiesAndAppendOnlyModuleIds) {
    EXPECT_EQ(buildingCatalogFingerprint(),"9059ecdef3c9530d419fdcffbe74ca0501472f73a83f8f901147f52122a50a98");
    EXPECT_EQ(legacyBuildingCatalogFingerprint(),"fc9c763b22b5ef9d81e461608dea0f1b138d8f16a4940e9378442dc18a611150");
    EXPECT_EQ(frontierBuildingCatalogFingerprint().size(),64u);
    EXPECT_EQ(uint8_t(PieceKind::HingedDoor),15);EXPECT_EQ(uint8_t(PieceKind::FrontierFoundation),16);
    EXPECT_EQ(uint8_t(PieceKind::FrontierWorkbench),24);EXPECT_EQ(buildingCatalog().size(),24u);
    for(const auto& definition:buildingCatalog()) {
        EXPECT_EQ(isFrontierPiece(definition.kind),uint8_t(definition.kind)>15);
        EXPECT_LT(uint8_t(buildingIconKind(definition.kind)),15);
        ASSERT_FALSE(buildingVisuals(definition.kind).empty());
    }
    EXPECT_TRUE(buildingVisuals(PieceKind{}).empty());
}
TEST(FrontierBuilding, CompositeMeshBoundsAgreeWithAcceptedGeometry) {
    for(const auto& definition:buildingCatalog())if(isFrontierPiece(definition.kind)) {
        glm::dvec3 minimum(INFINITY),maximum(-INFINITY);
        for(const auto& visual:buildingVisuals(definition.kind)) {
            ASSERT_FALSE(isFrontierPiece(visual.source));const auto* source=buildingDefinition(visual.source);ASSERT_TRUE(source);
            minimum=glm::min(minimum,visual.offset+metres(source->bounds.minimum)*visual.scale);
            maximum=glm::max(maximum,visual.offset+metres(source->bounds.maximum)*visual.scale);
        }
        // Scaled furniture rounds to the nearest integer lattice tick.
        for(int axis=0;axis<3;++axis) {
            EXPECT_NEAR(minimum[axis],metres(definition.bounds.minimum)[axis],.010001)<<definition.name;
            EXPECT_NEAR(maximum[axis],metres(definition.bounds.maximum)[axis],.010001)<<definition.name;
        }
    }
    for(const auto kind:{PieceKind::FrontierFoundation,PieceKind::FrontierDeck,PieceKind::FrontierRoof,PieceKind::FrontierStairs})
        for(const auto& visual:buildingVisuals(kind))EXPECT_EQ(visual.scale,glm::dvec3(1))<<"Repeated walking surfaces retain the original stud size";
}
TEST(FrontierBuilding, ApprovedFigurePassesWideDoorwayAtEveryRotationAndHitsPillars) {
    constexpr double radius=AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale;
    constexpr double height=AdventurePlayer::height*AdventurePlayer::creativeScale;
    for(uint8_t yaw=0;yaw<4;++yaw) {
        auto state=onePiece(PieceKind::FrontierDoorway,yaw);std::vector<AdventureSpatialQueries::Solid> solids;std::string error;
        ASSERT_TRUE(compileSolids(state,solids,error))<<error;
        std::vector<uint16_t> samples(64*64,0);AdventureSpatialQueries queries;
        ASSERT_TRUE(queries.bindTerrain({samples,64,64,8.f,1.f}));ASSERT_TRUE(queries.publish(solids,1));
        const auto feet=[&](glm::dvec3 p){return rotate(p,yaw)+glm::dvec3(0,12.005,0);};
        const auto clear=queries.sweepCapsule(feet({0,0,-2.5}),feet({0,0,2.5}),radius,height);
        EXPECT_TRUE(clear.complete);EXPECT_FALSE(clear.hit)<<"yaw="<<int(yaw);
        const auto pillar=queries.sweepCapsule(feet({1.2,0,-2.5}),feet({1.2,0,2.5}),radius,height);
        EXPECT_TRUE(pillar.complete);EXPECT_TRUE(pillar.hit)<<"yaw="<<int(yaw);
        EXPECT_FALSE(queries.clearCapsule(feet({0,.8,0}),radius,height))<<"The header remains solid";
    }
}
TEST(FrontierBuilding, ApprovedFigureWalksUpTheActualStairGeometry) {
    std::vector<uint16_t> samples(64*64,32768);AdventureSpatialQueries queries;
    ASSERT_TRUE(queries.bindTerrain({samples,64,64,8.f,1.f}));ASSERT_TRUE(queries.publish({},1));
    const double ground=queries.supportHeight({0,5.5},1.12,4);
    auto state=onePiece(PieceKind::FrontierStairs);
    state.structures.front().parts.front().position.y=int32_t(std::lround(ground*50.));
    std::vector<AdventureSpatialQueries::Solid> solids;std::string error;
    ASSERT_TRUE(compileSolids(state,solids,error))<<error;ASSERT_TRUE(queries.publish(solids,2));
    AdventurePlayer player;ASSERT_TRUE(player.initialize(queries,{0,ground+.005,5.5},-200,0,
        AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
    for(int i=0;i<82;++i)player.advance(AdventurePlayer::fixedStep,{{0,-1},false});
    EXPECT_LT(player.feet().z,-2.6);EXPECT_GT(player.feet().z,-3.0);
    EXPECT_NEAR(player.feet().y,ground+2.885,.02);EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
    EXPECT_TRUE(queries.clearCapsule(player.feet(),player.bodyRadius(),player.bodyHeight()));
}
TEST(FrontierBuilding, StairPresentationAndCollisionShareEachTread) {
    const auto* stairs=buildingDefinition(PieceKind::FrontierStairs);ASSERT_TRUE(stairs);
    std::vector<GridBox> rendered;
    for(const auto& visual:buildingVisuals(stairs->kind))for(const auto& box:buildingDefinition(visual.source)->solids) {
        const auto grid=[&](geometry::GridPosition p) {
            const auto point=(metres(p)*visual.scale+visual.offset)*50.;
            return geometry::GridPosition{int32_t(std::lround(point.x)),int32_t(std::lround(point.y)),int32_t(std::lround(point.z))};
        };
        rendered.push_back({grid(box.minimum),grid(box.maximum)});
    }
    ASSERT_EQ(stairs->solids.size(),rendered.size());
    for(size_t i=0;i<rendered.size();++i)EXPECT_EQ(stairs->solids[i],rendered[i]);
    EXPECT_DOUBLE_EQ(metres(stairs->bounds.maximum).y,2.88);
}
} // namespace voxy::game::adventure

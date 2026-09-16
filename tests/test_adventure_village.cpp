#include "game/adventure/village_layout.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/town_residents.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <numbers>

using namespace voxy::game::adventure;
namespace {
struct VillageFixture {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2048,32768);
    voxy::terrain::lego::Surface terrain{samples,256,2048,8,1};
    AdventureState state;AdventureContent content;AdventureSpatialQueries base;
    std::vector<AdventureSpatialQueries::Solid> solids;
    VillageFixture() {
        state.world.bytes[0]=23;const auto spawn=townSpawn(terrain);state.player={spawn.x,spawn.y,spawn.z,0};content.town=state.player;
        for(uint32_t i=0;i<18;++i) {
            const double angle=i*2*std::numbers::pi/18;
            const auto p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
            content.resourceNodes.push_back({i+1,{p.x,.18,p.y,0},{ItemKind::Wood,8}});
        }
        solids.push_back({{state.world,1},{state.world,1},{-63.5,.18,-898.5},{-62.5,2.18,-897.5}});
        EXPECT_TRUE(base.bindTerrain(terrain));EXPECT_TRUE(base.publish(solids,1));
    }
    VillageLayout admit(const VillageLayout* previous=nullptr) {return VillageLayout::admit(state,content,base,solids,previous);}
    bool publish(const VillageLayout& layout,AdventureSpatialQueries& queries) {
        auto all=solids;return layout.appendSolids(state.world,all)&&queries.bindTerrain(terrain)&&queries.publish(all,1);
    }
};
}
TEST(AdventureVillage, TwoSheltersGardensTreesAndPathsShareBoundedRenderedCollision) {
    VillageFixture f;const auto layout=f.admit();size_t pieces=0,props=0,solids=0;
    for(const auto& group:layout.groups()) {
        ASSERT_TRUE(group.available)<<int(group.id)<<" "<<group.name;
        pieces+=group.pieces.size();props+=group.props.size();solids+=group.solids.size();
        for(const auto& piece:group.pieces)EXPECT_EQ(buildingDefinition(piece.kind)->furniture,FurnitureKind::None);
        for(const auto& b:group.solids){EXPECT_EQ(b.structure.counter,villageStructureId(group.id));EXPECT_TRUE(isVillagePartId(b.part.counter));}
    }
    EXPECT_LE(pieces,VillageLayout::maximumPieces);EXPECT_LE(props,VillageLayout::maximumProps);EXPECT_LE(solids,VillageLayout::maximumSolids);
    EXPECT_GT(pieces,25u);EXPECT_GE(props,20u);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publish(layout,queries));
    for(const auto& group:layout.groups())if(!group.route.empty()) {
        AdventurePlayer player;ASSERT_TRUE(player.initialize(queries,townSpawn(f.terrain),-200));
        for(const auto target:group.route) {
            int tick=0;for(;tick<240;++tick){const auto d=glm::dvec2(target.x-player.feet().x,target.z-player.feet().z);if(glm::length(d)<.08)break;player.advance(AdventurePlayer::fixedStep,{glm::normalize(d),false});}
            EXPECT_LT(tick,240)<<group.name;EXPECT_EQ(player.mode(),AdventurePlayer::Mode::Walking);
        }
        EXPECT_TRUE(queries.clearCapsule(player.feet()));
    }
}
TEST(AdventureVillage, ExistingPlayerInsideAProposedHouseDefersTheEntireHouse) {
    VillageFixture f;f.state.player={-71,.185,-901,.7};const auto original=f.state;const auto layout=f.admit();
    EXPECT_EQ(f.state,original);EXPECT_FALSE(layout.groups()[0].available);EXPECT_TRUE(layout.groups()[1].available);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publish(layout,queries));EXPECT_TRUE(queries.clearCapsule({-71,.185,-901}));
    for(const auto& group:layout.groups())if(group.available){EXPECT_NE(group.id,1);}
}
TEST(AdventureVillage, ReloadInsideAnExistingCottageKeepsItsFloorAndExactSavedPose) {
    VillageFixture f;const auto first=f.admit();ASSERT_TRUE(first.groups()[0].available);
    AdventureSpatialQueries queries;ASSERT_TRUE(f.publish(first,queries));
    const auto inside=first.groups()[0].route.back();
    const double floor=queries.supportHeight({inside.x,inside.z},.3,1);
    f.state.player={inside.x,floor+.005,inside.z,.7};const auto saved=f.state;
    ASSERT_TRUE(queries.clearCapsule({f.state.player.x,f.state.player.y,f.state.player.z}));
    const auto restored=f.admit();EXPECT_EQ(f.state,saved);ASSERT_TRUE(restored.groups()[0].available);
    EXPECT_EQ(restored.groups()[0].minimum,first.groups()[0].minimum);
    AdventureSpatialQueries after;ASSERT_TRUE(f.publish(restored,after));
    EXPECT_DOUBLE_EQ(after.supportHeight({inside.x,inside.z},.3,f.state.player.y+.01),floor);
    EXPECT_TRUE(after.clearCapsule({f.state.player.x,f.state.player.y,f.state.player.z}));
}
TEST(AdventureVillage, AnExistingHouseAndLegacyReservedIdsWinWithoutChangingTheirState) {
    VillageFixture f;
    f.state.structures.push_back({3,1,0,{},{{4,PieceKind::Foundation,{-3550,0,-45050},0,0}}});
    f.state.lastIssuedId=4;std::string error;std::vector<AdventureSpatialQueries::Solid> built;
    ASSERT_TRUE(compileSolids(f.state,built,error));f.solids.insert(f.solids.end(),built.begin(),built.end());ASSERT_TRUE(f.base.publish(f.solids,2));
    const auto original=f.state;auto layout=f.admit();EXPECT_EQ(f.state,original);EXPECT_FALSE(layout.groups()[0].available);
    EXPECT_TRUE(layout.validateNewConstruction(f.state,f.state,error))<<error;
    VillageFixture legacy;legacy.state.structures.push_back({3,1,0,{},{{villagePartId(2,1),PieceKind::Foundation,{0,0,0},0,0}}});
    const auto migrated=legacy.admit();EXPECT_FALSE(migrated.groups()[1].available);EXPECT_TRUE(migrated.groups()[0].available);
}
TEST(AdventureVillage, NewBuildingCollisionsRefuseButUnchangedPartsAndRemoteBuildingRemain) {
    VillageFixture f;const auto layout=f.admit();auto after=f.state;std::string error;
    after.structures.push_back({3,1,0,{},{{4,PieceKind::Foundation,{-3550,0,-45050},0,0}}});
    EXPECT_FALSE(layout.validateNewConstruction(f.state,after,error));
    after.structures[0].parts[0].position={0,0,0};EXPECT_TRUE(layout.validateNewConstruction(f.state,after,error));
    after.structures[0].parts[0].id=villagePartId(1,1);EXPECT_FALSE(layout.validateNewConstruction(f.state,after,error));
}
TEST(AdventureVillage, NearbyOldDoorwayKeepsApproachClearanceWithoutDirectOverlap) {
    VillageFixture f;const auto empty=f.admit();ASSERT_TRUE(empty.groups()[0].available);
    f.state.structures.push_back({3,1,0,{},{{4,PieceKind::Foundation,{-3730,0,-45050},0,0},
        {5,PieceKind::Doorway,{-3680,16,-45050},1,0}}});f.state.lastIssuedId=5;
    std::vector<AdventureSpatialQueries::Solid> owned;std::string error;
    ASSERT_TRUE(validateInstalledGeometry(f.state,f.base,error))<<error;
    ASSERT_TRUE(compileSolids(f.state,owned,error));
    double right=-INFINITY;for(const auto& solid:owned)right=std::max(right,solid.maximum.x);
    EXPECT_NEAR(empty.groups()[0].minimum.x-right,.12,1e-7); // No direct overlap.
    f.solids.insert(f.solids.end(),owned.begin(),owned.end());ASSERT_TRUE(f.base.publish(f.solids,2));
    const auto saved=f.state;const auto admitted=f.admit();
    EXPECT_EQ(f.state,saved);EXPECT_FALSE(admitted.groups()[0].available);
    EXPECT_TRUE(admitted.groups()[1].available);
}
TEST(AdventureVillage, NewAdjacentBuildingPreservesFreshAdmissionWhileOldNearBuildingKeepsPriority) {
    VillageFixture f;f.state.player={-60,.185,-905,0};const auto live=f.admit();
    for(const auto& group:live.groups())ASSERT_TRUE(group.available)<<group.name;
    auto near=f.state;
    near.structures.push_back({3,1,0,{},{{4,PieceKind::Foundation,{-2825,0,-45225},0,0}}});
    near.lastIssuedId=4;std::string error;std::vector<AdventureSpatialQueries::Solid> owned;
    // A grounded legal foundation, .20 m behind the market's roof envelope,
    // reproduces the gap between physical overlap and legacy approach space.
    ASSERT_TRUE(validateConstruction(f.state,near,f.base,error))<<error;
    ASSERT_TRUE(compileSolids(near,owned,error));ASSERT_EQ(owned.size(),1u);
    EXPECT_NEAR(live.groups()[1].minimum.z-owned.front().maximum.z,.2,1e-7);
    const auto previous=f.state;
    EXPECT_FALSE(live.validateNewConstruction(f.state,near,error));
    EXPECT_EQ(f.state,previous);

    // The same bytes from an older save still own their position. Fresh
    // scenery admission defers the market, without rewriting the old home.
    auto oldSolids=f.solids;oldSolids.insert(oldSolids.end(),owned.begin(),owned.end());
    AdventureSpatialQueries oldBase;
    ASSERT_TRUE(oldBase.bindTerrain(f.terrain));ASSERT_TRUE(oldBase.publish(oldSolids,1));
    const auto savedNear=near;
    const auto oldLoad=VillageLayout::admit(near,f.content,oldBase,oldSolids);
    EXPECT_FALSE(oldLoad.groups()[1].available);EXPECT_EQ(near,savedNear);
    EXPECT_TRUE(oldLoad.validateNewConstruction(near,near,error))<<error;

    // Move the proposed foundation back to .90 m clearance, then simulate a
    // fresh restore from only player solids (no retained village shortcut).
    auto clear=near;clear.structures.front().parts.front().position.z=-45260;
    ASSERT_TRUE(validateConstruction(f.state,clear,f.base,error))<<error;
    ASSERT_TRUE(live.validateNewConstruction(f.state,clear,error))<<error;
    ASSERT_TRUE(compileSolids(clear,owned,error));
    auto restoredSolids=f.solids;restoredSolids.insert(restoredSolids.end(),owned.begin(),owned.end());
    AdventureSpatialQueries restoredBase;
    ASSERT_TRUE(restoredBase.bindTerrain(f.terrain));ASSERT_TRUE(restoredBase.publish(restoredSolids,1));
    const auto savedClear=clear;
    const auto restored=VillageLayout::admit(clear,f.content,restoredBase,restoredSolids);
    EXPECT_EQ(clear,savedClear);
    for(size_t i=0;i<live.groups().size();++i) {
        EXPECT_EQ(restored.groups()[i].available,live.groups()[i].available)<<live.groups()[i].name;
        EXPECT_EQ(restored.groups()[i].minimum,live.groups()[i].minimum);
        EXPECT_EQ(restored.groups()[i].maximum,live.groups()[i].maximum);
    }
}
TEST(AdventureVillage, RetainedGroupsStayFixedAndCapacityFailureCannotPartiallyAppend) {
    VillageFixture f;const auto first=f.admit();f.state.player={-90,.185,-895,0};const auto next=f.admit(&first);
    for(size_t i=0;i<first.groups().size();++i) {
        ASSERT_EQ(next.groups()[i].available,first.groups()[i].available);
        EXPECT_EQ(next.groups()[i].minimum,first.groups()[i].minimum);EXPECT_EQ(next.groups()[i].maximum,first.groups()[i].maximum);
    }
    std::vector<AdventureSpatialQueries::Solid> full(AdventureSpatialQueries::maximumSolids,f.solids.front());
    const auto original=full.size();EXPECT_FALSE(next.appendSolids(f.state.world,full));EXPECT_EQ(full.size(),original);
}
TEST(AdventureVillage, TownRecoveryNpcApproachesAndResourcePointsRemainUsable) {
    VillageFixture f;const auto layout=f.admit();AdventureSpatialQueries queries;ASSERT_TRUE(f.publish(layout,queries));
    const auto town=TownResidents::admit(f.state,queries);
    auto all=f.solids;ASSERT_TRUE(layout.appendSolids(f.state.world,all));ASSERT_TRUE(town.appendSolids(f.state.world,all));
    ASSERT_TRUE(queries.publish(all,2));EXPECT_TRUE(queries.clearCapsule(townSpawn(f.terrain)));
    for(const auto& npc:town.entries()){ASSERT_TRUE(npc.available);std::string error;const auto p=npc.approach;EXPECT_TRUE(town.interactable(npc.id,{p.x,p.y,p.z,0},queries,error))<<error;}
    for(const auto& node:f.content.resourceNodes)EXPECT_TRUE(queries.clearCapsule({node.position.x,.185,node.position.z}));
}

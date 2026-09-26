#include "game/adventure/frontier_world.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/frontier_interactions.hpp"
#include "game/adventure/adventure_player.hpp"
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <algorithm>
#include <set>

namespace voxy::game::adventure {
namespace {
struct Region {
    // Full coordinate range at the shipping one-stud cell size. A flat test
    // heightfield isolates installed content from the authored terrain survey.
    std::vector<uint16_t> samples=std::vector<uint16_t>(4096*4096,32768);
    terrain::lego::Surface surface{samples,4096,4096,600.f,1.f};
    FrontierWorld world;AdventureContent content;std::string error;
    std::unique_ptr<AdventureSession> session;
    bool initialize() {
        if(!world.initialize(surface,content,error))return false;
        construction::WorldNamespace identity;identity.bytes[0]=49;
        session=AdventureSession::create(identity,content,error);return bool(session);
    }
};
GridPosition grid(glm::dvec3 p){return {int32_t(std::lround(p.x*50)),int32_t(std::lround(p.y*50)),int32_t(std::lround(p.z*50))};}
std::vector<bool> groundedAssemblies(const terrain::lego::Surface& surface,
    const std::vector<AdventureSpatialQueries::Solid>& solids) {
    std::vector<bool> supported(solids.size());
    for(size_t i=0;i<solids.size();++i) {
        const auto center=(solids[i].minimum+solids[i].maximum)*.5;
        supported[i]=solids[i].minimum.y<=double(surface.heightAt(float(center.x),float(center.z)))+.04;
    }
    bool changed=true;
    while(changed) {
        changed=false;
        for(size_t i=0;i<solids.size();++i)if(!supported[i])for(size_t j=0;j<solids.size();++j)if(supported[j]) {
            if(glm::all(glm::lessThanEqual(solids[i].minimum,solids[j].maximum+glm::dvec3(.04)))
                &&glm::all(glm::lessThanEqual(solids[j].minimum,solids[i].maximum+glm::dvec3(.04)))) {
                supported[i]=true;changed=true;break;
            }
        }
    }
    return supported;
}
}
TEST(FrontierWorld, InvalidTerrainCannotCreateAnUnboundRegion) {
    FrontierWorld world;AdventureContent content;std::string error;
    EXPECT_FALSE(world.initialize({},content,error));EXPECT_FALSE(error.empty());
}
TEST(FrontierWorld, InstalledDefinitionsHaveStableIdsAndEnoughDistinctExpeditionContent) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    ASSERT_TRUE(region.content.frontier);EXPECT_FALSE(region.content.freeBuilding);
    EXPECT_EQ(region.world.destinations().size(),5u);EXPECT_EQ(region.content.frontierEnemies.size(),6u);
    EXPECT_EQ(region.content.resourceNodes.size(),60u);
    EXPECT_EQ(std::count_if(region.content.frontierSites.begin(),region.content.frontierSites.end(),
        [](const auto& site){return site.kind==FrontierSiteKind::Beacon;}),3);
    std::set<uint32_t> sources;std::array<uint32_t,4> material{};
    for(const auto& node:region.content.resourceNodes) {
        EXPECT_TRUE(sources.insert(node.id).second);
        if(node.yield.kind==ItemKind::Wood)material[0]+=node.yield.quantity;
        if(node.yield.kind==ItemKind::Stone)material[1]+=node.yield.quantity;
        if(node.yield.kind==ItemKind::Scrap)material[2]+=node.yield.quantity;
        if(node.yield.kind==ItemKind::CutStone)material[3]+=node.yield.quantity;
    }
    EXPECT_GE(material[0],96u);EXPECT_GE(material[1],64u);EXPECT_GE(material[2],48u);EXPECT_GE(material[3],24u);
    AdventureContent repeated;FrontierWorld world;
    ASSERT_TRUE(world.initialize(region.surface,repeated,region.error))<<region.error;
    EXPECT_EQ(repeated.identity,region.content.identity);EXPECT_EQ(repeated.town,region.content.town);
    EXPECT_EQ(repeated.resourceNodes,region.content.resourceNodes);
    EXPECT_EQ(repeated.frontierEnemies,region.content.frontierEnemies);EXPECT_EQ(repeated.frontierSites,region.content.frontierSites);
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(region.session->state(),region.content,bytes,region.error))<<region.error;
    AdventureState restored;ASSERT_TRUE(AdventureSaveCodec::decode(bytes,region.session->state().world,region.content,restored,region.error))<<region.error;
    EXPECT_EQ(restored,region.session->state());EXPECT_LT(bytes.size(),1024u*1024u);
}
TEST(FrontierWorld, DepletedSourceDisappearsFromPresentationAndQueriesAfterRestore) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    auto state=region.session->state();const auto id=region.content.resourceNodes.front().id;
    std::vector<AdventureSpatialQueries::Solid> before;ASSERT_TRUE(region.world.appendSolids(state,before));
    const auto part=FrontierWorld::resourcePart(id);
    ASSERT_TRUE(std::any_of(before.begin(),before.end(),[&](const auto& solid){return solid.part.counter==part;}));
    state.depletedNodes.push_back(id);++state.revision;
    std::vector<std::byte> bytes;ASSERT_TRUE(AdventureSaveCodec::encode(state,region.content,bytes,region.error))<<region.error;
    AdventureState restored;ASSERT_TRUE(AdventureSaveCodec::decode(bytes,state.world,region.content,restored,region.error))<<region.error;
    for(const auto& visual:region.world.visuals()) {
        if(visual.resource==id) {EXPECT_FALSE(region.world.visible(visual,restored));}
    }
    std::vector<AdventureSpatialQueries::Solid> after;ASSERT_TRUE(region.world.appendSolids(restored,after));
    EXPECT_FALSE(std::any_of(after.begin(),after.end(),[&](const auto& solid){return solid.part.counter==part;}));
    EXPECT_LT(after.size(),before.size());
    const auto other=FrontierWorld::resourcePart(region.content.resourceNodes[1].id);
    EXPECT_TRUE(std::any_of(after.begin(),after.end(),[&](const auto& solid){return solid.part.counter==other;}));
}
TEST(FrontierWorld, AuthoredSolidsLeaveSpawnClearAndStayWithinCollisionBudget) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(region.world.appendSolids(region.session->state(),solids));
    ASSERT_FALSE(solids.empty());EXPECT_LT(solids.size(),AdventureSpatialQueries::maximumSolids/2);
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(region.surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_TRUE(queries.clearCapsule(region.world.start(),1.12,4.76));
    EXPECT_TRUE(queries.clearCapsule(region.world.resident(),1.12,4.76));
    for(const auto& enemy:region.content.frontierEnemies)
        EXPECT_TRUE(queries.clearCapsule({enemy.spawn.x,enemy.spawn.y,enemy.spawn.z},1.12,4.76))<<"enemy="<<enemy.id;
}
TEST(FrontierWorld, ServiceSpaceIsProtectedWithoutPrescribingTheCampLayout) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    const auto before=region.session->state();auto after=before;
    const auto& beacon=region.content.frontierSites[1];
    const glm::dvec3 service{beacon.position.x,beacon.position.y,beacon.position.z};
    WorldStructure structure;structure.id=3;
    structure.parts.push_back({4,PieceKind::FrontierWall,grid(service),0,0});
    after.structures.push_back(structure);
    EXPECT_FALSE(region.world.protectedEdit(before,after,region.error));EXPECT_NE(region.error.find("clear"),std::string::npos);
    after.structures.front().parts.front().position=grid(service+glm::dvec3(10,0,0));
    EXPECT_TRUE(region.world.protectedEdit(before,after,region.error));
    after.structures.front().parts.front().yawQuarterTurns=1;
    EXPECT_TRUE(region.world.protectedEdit(before,after,region.error));
}
TEST(FrontierWorld, DepartureAndServiceApproachesRemainClearWithinVisualBudget) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    ASSERT_GT(region.world.approachSamples().size(),40u);
    EXPECT_LT(region.world.visuals().size(),FrontierWorld::maximumVisuals);
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(region.world.appendSolids(region.session->state(),solids));
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(region.surface));ASSERT_TRUE(queries.publish(solids,1));
    for(const auto feet:region.world.approachSamples())
        EXPECT_TRUE(queries.clearCapsule(feet,1.12,4.76))<<feet.x<<","<<feet.y<<","<<feet.z;
}
TEST(FrontierWorld, AuthoredStructuresConnectToGroundInsteadOfFloatingAboveIt) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(region.world.appendSolids(region.session->state(),solids));
    // A connected masonry/wood assembly may cantilever, but each connected
    // component must meet the actual terrain. Decorations without solids are
    // intentionally excluded: cloth, foliage, sparks and shallow ground color.
    const auto supported=groundedAssemblies(region.surface,solids);
    for(size_t i=0;i<solids.size();++i)
        EXPECT_TRUE(supported[i])<<"Disconnected solid at "<<solids[i].minimum.x<<","<<solids[i].minimum.y<<","<<solids[i].minimum.z;
}
// Installed region IDs/yields and real paid commands, with no inventory edits
// or reward injection. Geometry acceptance is isolated here: the separate
// interaction suite and browser journey verify walking, reach and service use.
TEST(FrontierWorld, EarnedThreeBeaconCampaignSurvivesCheckpointsWithExactCosts) {
    Region region;ASSERT_TRUE(region.initialize())<<region.error;
    const CandidateValidator accepted=[](const AdventureState&,const AdventureState&,std::string&){return true;};
    const auto stamp=[&](){const auto& state=region.session->state();return CommandStamp{state.revision,state.lastRequestSequence+1,1};};
    const auto apply=[&](std::optional<AdventureSession::PreparedChange> change) {
        return change&&region.session->commit(std::move(*change),region.error);
    };
    const auto reload=[&]() {
        const auto expected=region.session->state();std::vector<std::byte> bytes;AdventureState restored;
        if(!AdventureSaveCodec::encode(expected,region.content,bytes,region.error)
            ||!AdventureSaveCodec::decode(bytes,expected.world,region.content,restored,region.error))return false;
        EXPECT_EQ(restored,expected);
        region.session=AdventureSession::restore(restored,region.content,region.error);return bool(region.session);
    };
    // The safe starting cluster alone supplies the material deficit for three
    // independent paid foundation/workbench camps and all three restorations.
    for(uint32_t id=1;id<=9;++id)
        ASSERT_TRUE(apply(region.session->prepareGather(stamp(),id,accepted,region.error)))<<region.error;
    EXPECT_EQ(itemCount(region.session->state().backpack,ItemKind::Wood),96u);
    EXPECT_EQ(itemCount(region.session->state().backpack,ItemKind::Stone),68u);
    EXPECT_EQ(itemCount(region.session->state().backpack,ItemKind::Scrap),48u);
    const auto quarry=std::find_if(region.content.resourceNodes.begin(),region.content.resourceNodes.end(),
        [](const auto& node){return node.yield.kind==ItemKind::CutStone;});
    ASSERT_NE(quarry,region.content.resourceNodes.end());
    EXPECT_FALSE(region.session->prepareGather(stamp(),quarry->id,accepted,region.error));
    uint64_t unlock=0;size_t lit=0;std::vector<std::pair<uint32_t,uint64_t>> beacons;
    for(const auto& site:region.content.frontierSites)if(site.kind==FrontierSiteKind::Beacon) {
        const auto base=grid(glm::dvec3(site.position.x+6,site.position.y,site.position.z));
        ASSERT_TRUE(apply(region.session->preparePlace(stamp(),{0,PieceKind::FrontierFoundation,base,0,0},accepted,region.error)))<<region.error;
        const auto structure=region.session->state().structures.back().id;auto top=base;top.y+=16;
        ASSERT_TRUE(apply(region.session->preparePlace(stamp(),{structure,PieceKind::FrontierWorkbench,top,0,0},accepted,region.error)))<<region.error;
        const auto station=region.session->state().components.back().id;beacons.emplace_back(site.id,station);
        if(!site.defenders.empty()) {
            const auto blocked=region.session->state();
            EXPECT_FALSE(region.session->prepareFrontierRestoreBeacon(stamp(),site.id,station,accepted,region.error));
            EXPECT_EQ(region.session->state(),blocked);
            for(const auto id:site.defenders) {
                auto* enemy=frontierEnemy(region.session->state(),id);ASSERT_NE(enemy,nullptr);
                while(enemy->health) {
                    const uint16_t damage=region.session->state().equippedTool.kind==ItemKind::QuarryHammer?36:28;
                    ASSERT_TRUE(apply(region.session->prepareFrontierEnemyHit(stamp(),id,damage,enemy->pose,accepted,region.error)))<<region.error;
                    enemy=frontierEnemy(region.session->state(),id);
                }
                ASSERT_TRUE(reload())<<region.error;
            }
            EXPECT_EQ(frontierRemainingDefenders(region.session->state(),site),0u);
        }
        if(site.requiresQuarryCharge){EXPECT_TRUE(frontierHasQuarryHarvest(region.session->state(),region.content));}
        ASSERT_TRUE(apply(region.session->prepareFrontierRestoreBeacon(stamp(),site.id,station,accepted,region.error)))<<region.error;
        ++lit;ASSERT_TRUE(reload())<<region.error;
        EXPECT_EQ(std::count_if(region.session->state().frontier.sites.begin(),region.session->state().frontier.sites.end(),
            [](const auto& record){return record.restoredRevision!=0;}),static_cast<std::ptrdiff_t>(lit));
        if(lit==1) {
            unlock=region.session->state().frontier.quarryUnlockRevision;
            ASSERT_TRUE(apply(region.session->prepareCraftQuarryHammer(stamp(),station,accepted,region.error)))<<region.error;
            uint8_t slot=0;
            while(slot<kBackpackSlots&&region.session->state().backpack[slot].kind!=ItemKind::QuarryHammer)++slot;
            ASSERT_LT(slot,kBackpackSlots);
            ASSERT_TRUE(apply(region.session->prepareEquipTool(stamp(),slot,region.error)))<<region.error;
            ASSERT_TRUE(reload())<<region.error;
            ASSERT_TRUE(apply(region.session->prepareGather(stamp(),quarry->id,accepted,region.error)))<<region.error;
            EXPECT_EQ(itemCount(region.session->state().backpack,ItemKind::CutStone),16u);
            ASSERT_TRUE(reload())<<region.error;
        }
        EXPECT_EQ(region.session->state().frontier.quarryUnlockRevision,unlock);
    }
    ASSERT_EQ(lit,3u);ASSERT_TRUE(reload())<<region.error;
    const auto completed=region.session->state();
    EXPECT_EQ(completed.structures.size(),3u);
    EXPECT_EQ(itemCount(completed.backpack,ItemKind::Wood),36u);
    EXPECT_EQ(itemCount(completed.backpack,ItemKind::Stone),8u);
    EXPECT_EQ(itemCount(completed.backpack,ItemKind::Scrap),12u);
    EXPECT_EQ(itemCount(completed.backpack,ItemKind::CutStone),8u);
    for(const auto& [id,station]:beacons)EXPECT_FALSE(region.session->prepareFrontierRestoreBeacon(stamp(),id,station,accepted,region.error));
    EXPECT_FALSE(region.session->prepareGather(stamp(),quarry->id,accepted,region.error));
    for(uint32_t id=1;id<=9;++id)EXPECT_FALSE(region.session->prepareGather(stamp(),id,accepted,region.error));
    EXPECT_EQ(region.session->state(),completed);
}
TEST(FrontierWorld, InstalledTerrainLeavesAllAuthoredActorSpawnsClearWhenRequested) {
    const char* raw=std::getenv("VOXY_FRONTIER_TEST_TERRAIN");
    if(!raw||!*raw)GTEST_SKIP()<<"Set VOXY_FRONTIER_TEST_TERRAIN to the installed full .r16 terrain.";
    std::vector<uint16_t> samples(8192*8192);std::ifstream stream(raw,std::ios::binary);
    ASSERT_TRUE(stream.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(stream.peek(),std::char_traits<char>::eof());
    ASSERT_EQ(core::sha256Hex(core::sha256(std::as_bytes(std::span(samples)))),installedWorld().samplesSha256);
    terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    FrontierWorld world;AdventureContent content;std::string error;ASSERT_TRUE(world.initialize(surface,content,error))<<error;
    construction::WorldNamespace identity;identity.bytes[0]=59;
    auto session=AdventureSession::create(identity,content,error);ASSERT_TRUE(session)<<error;
    std::vector<AdventureSpatialQueries::Solid> solids;ASSERT_TRUE(world.appendSolids(session->state(),solids));
    AdventureSpatialQueries queries;ASSERT_TRUE(queries.bindTerrain(surface));ASSERT_TRUE(queries.publish(solids,1));
    EXPECT_TRUE(queries.clearCapsule(world.start(),1.12,4.76))<<"Player spawn";
    EXPECT_TRUE(queries.clearCapsule(world.resident(),1.12,4.76))<<"Resident spawn";
    for(const auto& enemy:content.frontierEnemies)
        EXPECT_TRUE(queries.clearCapsule({enemy.spawn.x,enemy.spawn.y,enemy.spawn.z},1.12,4.76))<<"enemy="<<enemy.id;
    for(const auto& site:content.frontierSites) {
        if(site.kind==FrontierSiteKind::Beacon) {
            EXPECT_TRUE(queries.clearCapsule({site.position.x,site.position.y,site.position.z},1.12,4.76))<<"beacon service="<<site.id;
        }
    }
    EXPECT_LT(world.visuals().size(),FrontierWorld::maximumVisuals);
    for(const auto feet:world.approachSamples())
        EXPECT_TRUE(queries.clearCapsule(feet,1.12,4.76))<<"approach="<<feet.x<<","<<feet.y<<","<<feet.z;
    const auto supported=groundedAssemblies(surface,solids);
    for(size_t i=0;i<solids.size();++i)
        EXPECT_TRUE(supported[i])<<"Actual-terrain disconnected solid at "<<solids[i].minimum.x<<","<<solids[i].minimum.y<<","<<solids[i].minimum.z;
    // A player's staircase route can reach the real cache platform. This
    // exercises actual terrain, authored aqueduct and the scaled controller;
    // payment/support admission is covered separately by session/build tests.
    auto built=session->state();WorldStructure route;route.id=100;
    const auto cache=content.frontierSites.front().position;
    const glm::dvec3 anchor{cache.x,cache.y-5.76,cache.z};
    for(int n=0;n<3;++n)
        route.parts.push_back({uint64_t(101+n),PieceKind::FrontierStairs,
            grid(anchor+glm::dvec3(0,(n-1)*2.88,-18+n*6)),2,0});
    built.structures.push_back(route);std::vector<AdventureSpatialQueries::Solid> routeSolids;
    ASSERT_TRUE(compileSolids(built,routeSolids,error))<<error;
    solids.insert(solids.end(),routeSolids.begin(),routeSolids.end());ASSERT_TRUE(queries.publish(solids,2));
    const glm::dvec2 from{cache.x,cache.z-22};
    AdventurePlayer player;ASSERT_TRUE(player.initialize(queries,{from.x,queries.supportHeight(from,1.12,cache.y)+.005,from.y},-200,0,2.8,.4));
    for(int tick=0;tick<260&&player.feet().z<cache.z-1;++tick)
        player.advance(AdventurePlayer::fixedStep,{{0,1},false});
    EXPECT_GT(player.feet().z,cache.z-2);EXPECT_NEAR(player.feet().y,cache.y,.06);
    built.player={player.feet().x,player.feet().y,player.feet().z,player.facingYaw()};
    const auto target=frontierNearbyTarget(built,content,queries,world.resident());
    EXPECT_EQ(target.kind,FrontierUseKind::Site);EXPECT_EQ(target.id,1u);
}
} // namespace voxy::game::adventure

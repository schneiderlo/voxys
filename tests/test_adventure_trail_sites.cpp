#include "game/adventure/trail_sites.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/town_residents.hpp"
#include "game/adventure/village_layout.hpp"
#include "game/adventure/world_definition.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <numbers>

using namespace voxy::game::adventure;
namespace {
using Solid=AdventureSpatialQueries::Solid;
glm::dvec3 feet(PlayerPose p){return {p.x,p.y,p.z};}
GridPosition grid(glm::dvec3 p){return {int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))};}
struct Scene {
    std::vector<uint16_t> samples=std::vector<uint16_t>(256*2304,32768);
    voxy::terrain::lego::Surface terrain{samples,256,2304,8.f,1.f};
    AdventureSpatialQueries base;
    std::vector<Solid> solids;
    AdventureContent content;
    AdventureState state;
    Scene(){configure();}
    PlayerPose ground(double x,double z) const {
        return {x,double(voxy::terrain::lego::supportHeight(terrain,{float(x),float(z)},.3f))+.005,z,0};
    }
    void configure() {
        state={};state.world.bytes[0]=53;content={};content.identity.bytes[0]=std::byte{89};
        const auto town=townSpawn(terrain);content.town={town.x,town.y,town.z,0};state.player=content.town;
        for(uint32_t i=0;i<18;++i) {
            const double angle=i*2*std::numbers::pi/18;
            const auto p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
            auto pose=ground(p.x,p.y);pose.y-=.005;
            content.resourceNodes.push_back({i+1,pose,{ItemKind(1+i%3),uint16_t(i%3==0?12:8)}});
        }
        solids.clear();const std::array markers{installedWorld().town+glm::dvec2(0,-3),installedWorld().landmark};
        for(size_t i=0;i<markers.size();++i) {
            const auto p=markers[i];const double y=double(voxy::terrain::lego::supportHeight(terrain,glm::vec2(p),.5f));
            solids.push_back({{state.world,i+1},{state.world,i+1},{p.x-.5,y,p.y-.5},{p.x+.5,y+(i?4.:2.),p.y+.5}});
        }
        base=AdventureSpatialQueries{};EXPECT_TRUE(base.bindTerrain(terrain));EXPECT_TRUE(base.publish(solids,1));
        std::string error;EXPECT_TRUE(defaultEncounterContent(base,content.encounters,error))<<error;
        EXPECT_TRUE(defaultTrailContent(base,content.discoveries,content.resourceNodes,error))<<error;
        content.enableTrailProgress=true;auto created=AdventureSession::create(state.world,content,error);
        EXPECT_TRUE(created)<<error;if(created)state=created->state();
    }
    bool populated() {
        const auto village=VillageLayout::admit(state,content,base,solids);
        if(!village.appendSolids(state.world,solids)||!base.publish(solids,base.revision()+1))return false;
        const auto town=TownResidents::admit(state,base);
        if(!town.appendSolids(state.world,solids)||!base.publish(solids,base.revision()+1))return false;
        return std::all_of(village.groups().begin(),village.groups().end(),[](const auto& group){return group.available;})
            &&std::all_of(town.entries().begin(),town.entries().end(),[](const auto& resident){return resident.available;});
    }
    bool published(const TrailSites& sites,AdventureSpatialQueries& out) const {
        auto packet=solids;return sites.appendSolids(state.world,packet)&&out.bindTerrain(terrain)&&out.publish(packet,1);
    }
};
bool walk(const AdventureSpatialQueries& world,AdventurePlayer& player,glm::dvec2 target,int limit=600) {
    for(int tick=0;tick<limit;++tick) {
        const auto delta=target-glm::dvec2(player.feet().x,player.feet().z);const double distance=glm::length(delta);
        if(distance<.008)return player.mode()==AdventurePlayer::Mode::Walking;
        const auto before=player.feet();player.advance(AdventurePlayer::fixedStep,{delta*(std::min(1.,distance/.06)/distance),false});
        if(player.mode()!=AdventurePlayer::Mode::Walking||!world.clearCapsule(player.feet()))return false;
        if(glm::length(player.feet()-before)<1e-8)return false;
    }
    return false;
}
std::unique_ptr<AdventureSession> room(Scene& scene,glm::dvec2 center,std::string& error) {
    scene.state.player=scene.ground(center.x,center.y+7);
    auto session=AdventureSession::restore(scene.state,scene.content,error);if(!session)return {};
    const auto y=terrainPlacementHeight(PieceKind::Foundation,0,center,scene.terrain);if(!y)return {};
    const auto layout=starterRoomLayout(grid({center.x,*y,center.y}),0,error);
    const CandidateValidator validator=[&](const auto& before,const auto& after,std::string& reason){return validateConstruction(before,after,scene.base,reason);};
    auto change=session->prepareBlueprint({session->state().revision,session->state().lastRequestSequence+1,1},layout,validator,error);
    if(!change||!session->commit(std::move(*change),error))return {};
    scene.state=session->state();return session;
}
bool publishOwned(Scene& scene,AdventureSpatialQueries& out) {
    std::string error;std::vector<Solid> all;if(!compileSolids(scene.state,all,error))return false;
    all.insert(all.end(),scene.solids.begin(),scene.solids.end());return out.bindTerrain(scene.terrain)&&out.publish(all,1);
}
}

TEST(AdventureTrailSites, FourBoundedGroupsUseIdenticalCatalogRenderPiecesAndAcceptedSolids) {
    Scene scene;ASSERT_TRUE(scene.populated());const auto before=scene.state;
    const auto sites=TrailSites::admit(scene.state,scene.content,scene.base);AdventureSpatialQueries queries;
    ASSERT_TRUE(scene.published(sites,queries));size_t pieces=0,solids=0;
    for(const auto& group:sites.groups()) {
        ASSERT_TRUE(group.available)<<int(group.id)<<' '<<group.name;
        AdventureState derived;derived.world=scene.state.world;derived.structures.push_back({trailStructureId(group.id),1,0,{}, {}});
        uint8_t id=1;for(const auto& piece:group.pieces)
            derived.structures.front().parts.push_back({trailPartId(group.id,id++),piece.kind,piece.position,piece.yawQuarterTurns,0});
        std::vector<Solid> compiled;std::string error;ASSERT_TRUE(compileSolids(derived,compiled,error));
        ASSERT_EQ(compiled.size(),group.solids.size());
        for(size_t i=0;i<compiled.size();++i) {
            EXPECT_EQ(compiled[i].minimum,group.solids[i].minimum);EXPECT_EQ(compiled[i].maximum,group.solids[i].maximum);
            EXPECT_EQ(compiled[i].part,group.solids[i].part);EXPECT_TRUE(isTrailPartId(compiled[i].part.counter));
        }
        pieces+=group.pieces.size();solids+=group.solids.size();
    }
    EXPECT_EQ(pieces,23u);EXPECT_EQ(solids,23u);
    for(const auto& site:sites.sites())EXPECT_TRUE(site.available)<<int(site.id);
    EXPECT_FALSE(sites.site(TrailSiteId::Relay)->active);EXPECT_EQ(scene.state,before);
    EXPECT_TRUE(queries.clearCapsule(feet(scene.state.player)));EXPECT_TRUE(queries.clearCapsule(feet(scene.content.town)));
    auto activated=scene.state;activated.trail.relayActivationRevision=1;
    const auto lit=TrailSites::admit(activated,scene.content,scene.base,&sites);EXPECT_TRUE(lit.site(TrailSiteId::Relay)->active);
    for(size_t i=0;i<sites.groups().size();++i)EXPECT_EQ(lit.groups()[i].available,sites.groups()[i].available);
}

TEST(AdventureTrailSites, DiscoveryAndRelayUseAdmittedRangeAndSightAndAppendPreservesOldNodes) {
    Scene scene;const auto sites=TrailSites::admit(scene.state,scene.content,scene.base);AdventureSpatialQueries queries;
    ASSERT_TRUE(scene.published(sites,queries));EXPECT_EQ(scene.content.resourceNodes.size(),21u);
    EXPECT_EQ(scene.content.resourceNodes[18].yield,(ItemStack{ItemKind::Stone,12}));
    EXPECT_EQ(scene.content.resourceNodes[19].yield,(ItemStack{ItemKind::Wood,16}));
    EXPECT_EQ(scene.content.resourceNodes[20].yield,(ItemStack{ItemKind::Scrap,4}));
    EXPECT_EQ(scene.content.discoveries[0].reward,(ItemStack{ItemKind::Scrap,4}));
    EXPECT_EQ(scene.content.discoveries[1].reward,(ItemStack{ItemKind::Stone,12}));
    const auto all=scene.content.resourceNodes;auto old=all;old.resize(18);const auto original=old;
    auto discoveries=scene.content.discoveries;std::string error;
    ASSERT_TRUE(defaultTrailContent(scene.base,discoveries,old,error));
    EXPECT_TRUE(std::equal(original.begin(),original.end(),old.begin()));EXPECT_EQ(old,all);
    EXPECT_FALSE(defaultTrailContent(scene.base,discoveries,old,error));EXPECT_EQ(old,all);
    for(uint8_t id:{uint8_t{1},uint8_t{2}}) {
        const auto p=sites.site(TrailSiteId(id))->position;
        EXPECT_TRUE(sites.discoveryReachable(id,p,queries,error))<<error;
        EXPECT_FALSE(sites.discoveryReachable(id,scene.state.player,queries,error));
    }
    auto point=sites.site(TrailSiteId::SignalTerrace)->position;
    auto blocked=std::vector<Solid>(queries.solids().begin(),queries.solids().end());
    blocked.push_back({{scene.state.world,90},{scene.state.world,91},feet(point)+glm::dvec3(-.5,0,.5),feet(point)+glm::dvec3(.5,2,.7)});
    ASSERT_TRUE(queries.publish(blocked,2));const auto approach=scene.ground(point.x,point.z+1.5);
    EXPECT_FALSE(sites.discoveryReachable(1,approach,queries,error));
    EXPECT_TRUE(sites.relayReachable(scene.ground(-61.5,-975),queries,error))<<error;
    EXPECT_FALSE(sites.relayReachable(scene.state.player,queries,error));
}

TEST(AdventureTrailSites, OldPlayerHomeRecoveryAndReservedIdentityDeferWholeGroupsWithoutRewrites) {
    for(int conflict=0;conflict<4;++conflict) {
        Scene scene;const auto sites=TrailSites::admit(scene.state,scene.content,scene.base);const auto& group=sites.groups()[0];
        ASSERT_TRUE(group.available);const auto box=group.solids.front();
        const auto p=scene.ground((box.minimum.x+box.maximum.x)*.5,(box.minimum.z+box.maximum.z)*.5);
        if(conflict==0)scene.state.player=p;
        if(conflict==1){scene.state.registeredBed=17;scene.state.recovery=p;}
        if(conflict==2) {
            scene.state.structures.push_back({9,1,0,{},{{10,PieceKind::Wall,{},0,0}}});
            scene.solids.push_back({{scene.state.world,9},{scene.state.world,10},box.minimum+glm::dvec3(.65,0,0),box.maximum+glm::dvec3(.65,0,0)});
            ASSERT_TRUE(scene.base.publish(scene.solids,scene.base.revision()+1));
        }
        if(conflict==3)scene.state.structures.push_back({9,1,0,{},{{trailPartId(1,1),PieceKind::Wall,{},0,0}}});
        const auto before=scene.state;const auto deferred=TrailSites::admit(scene.state,scene.content,scene.base);
        EXPECT_FALSE(deferred.groups()[0].available);EXPECT_FALSE(deferred.site(TrailSiteId::SignalTerrace)->available);
        EXPECT_TRUE(deferred.groups()[1].available);EXPECT_EQ(scene.state,before);
        std::vector<Solid> bodies=scene.solids;ASSERT_TRUE(deferred.appendSolids(scene.state.world,bodies));
        EXPECT_FALSE(std::any_of(bodies.begin(),bodies.end(),[](const auto& b){return b.structure.counter==trailStructureId(1);}));
        scene.state.player=scene.content.town;
        EXPECT_FALSE(TrailSites::admit(scene.state,scene.content,scene.base,&deferred).groups()[0].available);
    }
    Scene scene;const auto sites=TrailSites::admit(scene.state,scene.content,scene.base);
    auto bodies=scene.solids;ASSERT_TRUE(sites.appendSolids(scene.state.world,bodies));const auto count=bodies.size();
    EXPECT_FALSE(sites.appendSolids(scene.state.world,bodies));EXPECT_EQ(bodies.size(),count);
    std::vector<Solid> full(AdventureSpatialQueries::maximumSolids,bodies.front());
    EXPECT_FALSE(sites.appendSolids(scene.state.world,full));EXPECT_EQ(full.size(),AdventureSpatialQueries::maximumSolids);
}

TEST(AdventureTrailSites, FieldHomeUsesLiveOwnedShelterChestAndBenchWithoutChangingRecoveryRegistration) {
    Scene scene;std::string error;auto session=room(scene,{-75,-980},error);ASSERT_TRUE(session)<<error;
    AdventureSpatialQueries queries;ASSERT_TRUE(publishOwned(scene,queries));
    const auto before=scene.state;auto ready=fieldHomeReadiness(scene.state,queries);
    ASSERT_TRUE(ready.ready())<<ready.message;EXPECT_EQ(ready.structure,scene.state.structures.front().id);
    EXPECT_EQ(scene.state.registeredBed,0u);EXPECT_EQ(scene.state,before);
    const auto chest=std::find_if(scene.state.components.begin(),scene.state.components.end(),[](const auto& c){return c.kind==FurnitureKind::Chest;});
    ASSERT_NE(chest,scene.state.components.end());const auto part=chest->part;
    const CandidateValidator valid=[&](const auto& a,const auto& b,std::string& reason){return validateConstruction(a,b,queries,reason);};
    auto removed=session->prepareRemove({scene.state.revision,scene.state.lastRequestSequence+1,1},part,valid,error);
    ASSERT_TRUE(removed)<<error;ASSERT_TRUE(session->commit(std::move(*removed),error));scene.state=session->state();
    AdventureSpatialQueries changed;ASSERT_TRUE(publishOwned(scene,changed));
    EXPECT_EQ(fieldHomeReadiness(scene.state,changed).nextStep,FirstHomeStep::AddChest);
    scene.state=before;
    for(auto& structure:scene.state.structures)for(auto& p:structure.parts)p.position.z+=4000;
    AdventureSpatialQueries town;ASSERT_TRUE(publishOwned(scene,town));
    EXPECT_FALSE(fieldHomeReadiness(scene.state,town).ready());
    scene.state=before;for(auto& component:scene.state.components)if(component.kind==FurnitureKind::Workbench)component.owner=2;
    EXPECT_EQ(fieldHomeReadiness(scene.state,queries).nextStep,FirstHomeStep::AddWorkbench);
}

TEST(AdventureTrailSites, FullTerrainPaysForThreePierShortcutAndWalksBothDirectionsAndLongDetour) {
    const char* path=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!path||!*path)GTEST_SKIP()<<"Supply the unchanged full raw terrain for the production traversal preflight.";
    Scene scene;scene.samples.resize(8192*8192);std::ifstream input(path,std::ios::binary);
    ASSERT_TRUE(input.read(reinterpret_cast<char*>(scene.samples.data()),static_cast<std::streamsize>(scene.samples.size()*2)));
    ASSERT_EQ(input.peek(),std::char_traits<char>::eof());
    ASSERT_EQ(voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(scene.samples)))),installedWorld().samplesSha256);
    static_assert(std::endian::native==std::endian::little);
    scene.terrain={scene.samples,8192,8192,600.f,1.f};scene.configure();ASSERT_TRUE(scene.populated());
    const auto sites=TrailSites::admit(scene.state,scene.content,scene.base);
    for(const auto& group:sites.groups())ASSERT_TRUE(group.available)<<int(group.id)<<' '<<group.name;
    for(const auto& site:sites.sites())ASSERT_TRUE(site.available)<<int(site.id);
    AdventureSpatialQueries installed;ASSERT_TRUE(scene.published(sites,installed));
    // The first proposed .64 m ledge is already walkable because the actual
    // capsule can step along rounded edge support. Keep this negative finding.
    AdventurePlayer lowerLedge;ASSERT_TRUE(lowerLedge.initialize(installed,feet(scene.ground(21,-923)),-200));
    ASSERT_TRUE(walk(installed,lowerLedge,{22,-923}));
    const auto low=scene.ground(29,-923),high=scene.ground(30,-923);
    ASSERT_NEAR(high.y-low.y,.96,.0001);
    AdventurePlayer blocked;ASSERT_TRUE(blocked.initialize(installed,feet(low),-200));
    EXPECT_FALSE(walk(installed,blocked,{30,-923},120));EXPECT_LT(blocked.feet().x,29.4);
    AdventurePlayer detour;ASSERT_TRUE(detour.initialize(installed,feet(low),-200));
    for(const auto waypoint:std::array<glm::dvec2,3>{{{29,-941},{30,-941},{30,-923}}})ASSERT_TRUE(walk(installed,detour,waypoint));
    EXPECT_NEAR(detour.feet().y,high.y,.025);
    for(const auto waypoint:std::array<glm::dvec2,3>{{{30,-941},{29,-941},{29,-923}}})ASSERT_TRUE(walk(installed,detour,waypoint));
    EXPECT_NEAR(detour.feet().y,low.y,.025);
    scene.state.player=scene.ground(28,-921);std::string error;
    auto session=AdventureSession::restore(scene.state,scene.content,error);ASSERT_TRUE(session)<<error;
    const CandidateValidator validation=[&](const auto& before,const auto& after,std::string& reason) {
        return validateConstruction(before,after,installed,reason)&&sites.validateNewConstruction(before,after,reason);
    };
    const auto before=session->state();auto proposal=session->prepareBlueprint(
        {before.revision,before.lastRequestSequence+1,1},signalTerraceStep(),validation,error);
    ASSERT_TRUE(proposal)<<error;ASSERT_TRUE(session->commit(std::move(*proposal),error));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Stone)+6,itemCount(before.backpack,ItemKind::Stone));
    EXPECT_EQ(itemCount(session->state().backpack,ItemKind::Wood),itemCount(before.backpack,ItemKind::Wood));
    ASSERT_EQ(session->state().structures.size(),1u);ASSERT_EQ(session->state().structures.front().parts.size(),3u);
    std::vector<Solid> solids;ASSERT_TRUE(compileSolids(session->state(),solids,error));
    solids.insert(solids.end(),installed.solids().begin(),installed.solids().end());
    AdventureSpatialQueries built;ASSERT_TRUE(built.bindTerrain(scene.terrain));ASSERT_TRUE(built.publish(solids,1));
    AdventurePlayer walker;ASSERT_TRUE(walker.initialize(built,feet(scene.ground(28,-923)),-200));
    ASSERT_TRUE(walk(built,walker,{29.1,-923}));EXPECT_NEAR(walker.feet().y,-128.955,.025);
    ASSERT_TRUE(walk(built,walker,{30,-923}));EXPECT_NEAR(walker.feet().y,high.y,.025);
    ASSERT_TRUE(walk(built,walker,{29.1,-923}));EXPECT_NEAR(walker.feet().y,-128.955,.025);
    ASSERT_TRUE(walk(built,walker,{28,-923}));
    std::string reason;EXPECT_TRUE(sites.discoveryReachable(1,high,built,reason))<<reason;
    RecordProperty("fullTerrainSha256",std::string(installedWorld().samplesSha256));
    RecordProperty("sceneryGroups",4);RecordProperty("sceneryPieces",23);RecordProperty("paidPiers",3);RecordProperty("stoneCost",6);
    RecordProperty("terraceRiseMetres",.96);RecordProperty("detourEachDirectionMetres",37);
}

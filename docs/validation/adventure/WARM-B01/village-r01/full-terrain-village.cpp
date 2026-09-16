#include "game/adventure/town_residents.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/village_layout.hpp"
#include "game/adventure/construction_policy.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <numbers>
using namespace voxy::game::adventure;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::vector<uint16_t> samples(8192*8192);
    std::ifstream input(argv[1],std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2)))return 3;
    voxy::terrain::lego::Surface surface{samples,8192,8192,600,1};
    const auto spawn=townSpawn(surface);
    AdventureState state;state.world.bytes[0]=17;state.player={spawn.x,spawn.y,spawn.z,0};
    std::vector<AdventureSpatialQueries::Solid> baseSolids;
    const std::array points{installedWorld().town+glm::dvec2(0,-3),installedWorld().landmark};
    for(size_t i=0;i<points.size();++i) {
        const auto xz=points[i];const double y=voxy::terrain::lego::supportHeight(surface,glm::vec2(xz),.5f);
        baseSolids.push_back({{state.world,i+1},{state.world,i+1},{xz.x-.5,y,xz.y-.5},{xz.x+.5,y+(i?4.:2.),xz.y+.5}});
    }
    AdventureSpatialQueries base;
    if(!base.bindTerrain(surface)||!base.publish(baseSolids,1))return 4;
    AdventureContent content;content.town=state.player;
    for(uint32_t i=0;i<18;++i) {
        const double angle=i*2*std::numbers::pi/18;
        const auto p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
        const double y=voxy::terrain::lego::supportHeight(surface,glm::vec2(p),.3f);
        content.resourceNodes.push_back({i+1,{p.x,y,p.y,0},{ItemKind::Wood,8}});
    }
    const auto village=VillageLayout::admit(state,content,base,baseSolids);
    size_t pieces=0,props=0,proxyCount=0;
    for(const auto& group:village.groups()) {
        std::cout<<"group "<<int(group.id)<<" available "<<group.available<<" pieces "<<group.pieces.size()<<" props "<<group.props.size()<<" solids "<<group.solids.size()<<'\n';
        if(!group.available)return 11;
        pieces+=group.pieces.size();props+=group.props.size();proxyCount+=group.solids.size();
    }
    std::cout<<"total pieces "<<pieces<<" props "<<props<<" solids "<<proxyCount<<'\n';
    auto solids=baseSolids;
    AdventureSpatialQueries queries;
    if(!village.appendSolids(state.world,solids)||!queries.bindTerrain(surface)||!queries.publish(solids,1))return 5;
    const auto town=TownResidents::admit(state,queries);
    if(!town.appendSolids(state.world,solids)||!queries.publish(solids,2))return 12;
    for(const auto& group:village.groups())if(!group.route.empty()) {
        AdventurePlayer walker;if(!walker.initialize(queries,spawn,-200))return 13;
        for(const auto target:group.route) {
            int tick=0;for(;tick<240;++tick) {
                const auto delta=glm::dvec2(target.x-walker.feet().x,target.z-walker.feet().z);
                if(glm::length(delta)<.08)break;
                walker.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
            }
            std::cout<<"shelter-route "<<int(group.id)<<" target "<<target.x<<' '<<target.z<<" ticks "<<tick<<" feet "<<walker.feet().x<<' '<<walker.feet().y<<' '<<walker.feet().z<<'\n';
            if(tick==240||walker.mode()!=AdventurePlayer::Mode::Walking)return 14;
        }
        const auto saved=walker.feet();state.player={saved.x,saved.y,saved.z,.7};
        const auto reloaded=VillageLayout::admit(state,content,base,baseSolids);
        auto restoredSolids=baseSolids;AdventureSpatialQueries restoredQueries;
        if(!reloaded.groups()[group.id-1].available||!reloaded.appendSolids(state.world,restoredSolids)
            ||!restoredQueries.bindTerrain(surface)||!restoredQueries.publish(restoredSolids,1)
            ||!restoredQueries.clearCapsule(saved))return 15;
        const auto floor=restoredQueries.supportHeight({saved.x,saved.z},.3,saved.y+.01);
        std::cout<<"reload-in-shelter "<<int(group.id)<<" exact-floor "<<(std::abs(saved.y-floor-.005)<.0001)<<'\n';
        if(std::abs(saved.y-floor-.005)>.0001)return 16;
        state.player=content.town;
    }
    std::cout<<std::setprecision(12)<<"spawn "<<spawn.x<<' '<<spawn.y<<' '<<spawn.z<<'\n';
    for(const auto& resident:town.entries()) {
        std::cout<<"resident "<<resident.id<<" available "<<resident.available<<" feet "<<resident.feet.x<<' '<<resident.feet.y<<' '<<resident.feet.z<<'\n';
        if(!resident.available)return 6;
        AdventurePlayer player;if(!player.initialize(queries,spawn,-200))return 7;
        int tick=0;
        for(;tick<120;++tick) {
            const auto delta=glm::dvec2(resident.approach.x-player.feet().x,resident.approach.z-player.feet().z);
            if(glm::length(delta)<.075)break;
            player.advance(AdventurePlayer::fixedStep,{glm::normalize(delta),false});
        }
        const auto feet=player.feet();std::string error;
        const bool clear=queries.clearCapsule(feet);
        const bool talk=town.interactable(resident.id,{feet.x,feet.y,feet.z,0},queries,error);
        std::cout<<"approach "<<resident.id<<" ticks "<<tick<<" feet "<<feet.x<<' '<<feet.y<<' '<<feet.z<<" clear "<<clear<<" talk "<<talk<<" error "<<error<<'\n';
        if(tick==120||!clear||!talk||player.mode()!=AdventurePlayer::Mode::Walking)return 8;
    }
    for(const auto& definition:residentDefinitions()) {
        const double ground=voxy::terrain::lego::supportHeight(surface,glm::vec2(definition.anchor),.3f);
        state.player={definition.anchor.x,ground+.005,definition.anchor.y,1.2};const auto original=state;
        const auto migrated=TownResidents::admit(state,base);auto migrationSolids=baseSolids;
        AdventureSpatialQueries migratedQueries;
        if(!migrated.appendSolids(state.world,migrationSolids)||!migratedQueries.bindTerrain(surface)||!migratedQueries.publish(migrationSolids,1))return 9;
        const auto saved=glm::dvec3(state.player.x,state.player.y,state.player.z);
        const bool clear=migratedQueries.clearCapsule(saved),recoveryClear=migratedQueries.clearCapsule(spawn);
        std::cout<<"legacy-player "<<definition.id<<" exact "<<(original==state)<<" clear "<<clear<<" recovery-clear "<<recoveryClear<<" actor-available "<<migrated.find(definition.id)->available<<'\n';
        if(original!=state||!clear||!recoveryClear)return 10;
    }
    state.player={-71,double(voxy::terrain::lego::supportHeight(surface,{-71,-901},.3f))+.005,-901,.7};
    const auto oldPlayer=state;const auto playerConflict=VillageLayout::admit(state,content,base,baseSolids);
    if(playerConflict.groups()[0].available||state!=oldPlayer)return 17;
    std::cout<<"legacy-ground-player cottage-deferred exact-state 1\n";
    state.player=content.town;
    state.structures.push_back({3,1,0,{},{{4,PieceKind::Foundation,{-3550,-7310,-45050},0,0}}});
    const auto oldHouse=state;std::vector<AdventureSpatialQueries::Solid> oldBuilt;std::string error;
    if(!compileSolids(state,oldBuilt,error))return 18;
    auto oldBase=baseSolids;oldBase.insert(oldBase.end(),oldBuilt.begin(),oldBuilt.end());
    AdventureSpatialQueries oldQueries;if(!oldQueries.bindTerrain(surface)||!oldQueries.publish(oldBase,1))return 19;
    const auto houseConflict=VillageLayout::admit(state,content,oldQueries,oldBase);
    if(houseConflict.groups()[0].available||state!=oldHouse)return 20;
    std::cout<<"legacy-building cottage-deferred exact-state 1\n";
    return 0;
}

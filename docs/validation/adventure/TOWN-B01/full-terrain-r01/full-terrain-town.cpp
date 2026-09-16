#include "game/adventure/town_residents.hpp"
#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
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
    const auto town=TownResidents::admit(state,base);
    auto solids=baseSolids;
    AdventureSpatialQueries queries;
    if(!town.appendSolids(state.world,solids)||!queries.bindTerrain(surface)||!queries.publish(solids,1))return 5;
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
    return 0;
}

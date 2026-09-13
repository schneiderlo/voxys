#include "game/adventure/adventure_player.hpp"
#include "game/adventure/world_definition.hpp"
#include <fstream>
#include <iostream>
#include <vector>
using namespace voxy::game::adventure;
int main(){
 std::vector<uint16_t> samples(8192*8192);std::ifstream input("/tmp/voxys-adventure-world.r16",std::ios::binary);
 if(!input.read(reinterpret_cast<char*>(samples.data()),samples.size()*2))return 2;
 voxy::terrain::lego::Surface surface{samples,8192,8192,600,1};AdventureSpatialQueries queries;
 if(!queries.bindTerrain(surface)||!queries.publish({},1))return 3;
 AdventurePlayer player;if(!player.initialize(queries,townSpawn(surface),-200))return 4;
 for(const auto goal:installedWorld().route){
   int i=0;for(;i<6000;++i){const glm::dvec2 delta=goal-glm::dvec2(player.feet().x,player.feet().z);if(glm::length(delta)<.09)break;player.advance(1./60,{glm::normalize(delta),false});}
   std::cout<<"goal "<<goal.x<<','<<goal.y<<" ticks "<<i<<" actual "<<player.feet().x<<','<<player.feet().y<<','<<player.feet().z<<" mode "<<int(player.mode())<<'\n';
   if(i==6000)return 5;
 }
 return 0;
}

#include "game/adventure/adventure_save.hpp"
#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/trail_sites.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
#include <cmath>
using namespace voxy::game::adventure;
int main() {
    static_assert(kAdventureSaveSchema==4);
    std::ifstream terrainFile("/tmp/voxys-adventure-world.r16",std::ios::binary);
    std::vector<uint16_t> samples(8192*8192);terrainFile.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*2));
    if(!terrainFile)return 1;
    voxy::terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    if(voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(samples))))!=installedWorld().samplesSha256)return 2;
    AdventureSpatialQueries base;if(!base.bindTerrain(surface)||!base.publish({},1))return 3;
    AdventureContent content;std::string error;content.compatibilityIdentities=installedAdventureCompatibilityIdentities();
    content.legacyIdentity=content.compatibilityIdentities[0];
    voxy::core::Sha256 hash;hash.string(voxy::core::sha256Hex(*content.compatibilityIdentities[1]));
    hash.string("adventure-trail-combat-r01:staff6-wood4-scrap4-damage25-windup12-ready36:dodge12-ready54:raiders1-2-health60-100-damage12-18:notice7-leash12-reach1.6-windup42-recover54:loot8scrap-relaycore7");content.identity=hash.finish();
    const auto spawn=townSpawn(surface);content.town={spawn.x,spawn.y,spawn.z,0};
    if(!defaultEncounterContent(base,content.encounters,error)){std::cerr<<error;return 4;}
    for(uint32_t i=0;i<18;++i){const double angle=i*2*std::numbers::pi/18;
        const glm::dvec2 p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
        content.resourceNodes.push_back({i+1,{p.x,voxy::terrain::lego::supportHeight(surface,glm::vec2(p),.3f),p.y,0},{ItemKind(1+i%3),uint16_t(i%3==0?12:8)}});
    }
    voxy::game::construction::WorldNamespace world{{'s','c','h','e','m','a','3','-','c','o','m','p','-','0','0','1'}};
    if(!defaultTrailContent(base,content.discoveries,content.resourceNodes,error))return 5;
    content.enableTrailProgress=true;
    voxy::core::Sha256 trailHash;trailHash.string(voxy::core::sha256Hex(content.identity));trailHash.string(trailContentFingerprint());content.identity=trailHash.finish();
    if(voxy::core::sha256Hex(content.identity)!="0ca6be8bbc4285f4541aadc74d8cb7aeefb4b2ed078f45f86e7b0a2173ab1e93")return 10;
    std::ifstream priorFile("tests/fixtures/adventure/schema3-component-r01.bin",std::ios::binary);
    std::vector<char> prior((std::istreambuf_iterator<char>(priorFile)),{});AdventureState imported;
    if(!AdventureSaveCodec::decode(std::as_bytes(std::span(prior)),world,content,imported,error)){std::cerr<<error;return 11;}
    auto session=AdventureSession::restore(imported,content,error);if(!session)return 12;
    const CandidateValidator fixture=[](const auto&,const auto&,std::string&){return true;};
    auto stamp=[&]{return CommandStamp{session->state().revision,session->state().lastRequestSequence+1,1};};
    auto publish=[&](std::optional<AdventureSession::PreparedChange> prepared){if(!prepared||!session->commit(std::move(*prepared),error)){std::cerr<<error<<'\n';std::exit(6);}};
    for(uint8_t id=2;id<=3;++id){const uint8_t npc=id==2?2:3;publish(session->prepareAcceptTrailQuest(stamp(),id,npc,fixture,error));publish(session->prepareCompleteTrailQuest(stamp(),id,npc,fixture,error));}
    publish(session->prepareAcceptTrailQuest(stamp(),4,3,fixture,error));
    for(uint8_t id=1;id<=2;++id){publish(session->prepareDiscover(stamp(),id,fixture,error));publish(session->prepareClaimDiscovery(stamp(),id,fixture,error));}
    for(const auto& chest:session->state().components)if(chest.kind==FurnitureKind::Chest){
        uint8_t slot=255;for(uint8_t i=0;i<chest.slots.size();++i)if(chest.slots[i].kind==ItemKind::RelayCore)slot=i;
        if(slot!=255){publish(session->prepareTransfer(stamp(),{chest.id,0,chest.revision,session->state().backpackRevision,slot,1},fixture,error));break;}
    }
    publish(session->prepareActivateRelay(stamp(),fixture,error));
    std::vector<std::byte> bytes;if(!AdventureSaveCodec::encode(session->state(),content,bytes,error)){std::cerr<<error;return 7;}
    AdventureState restored;if(!AdventureSaveCodec::decode(bytes,world,content,restored,error)||restored!=session->state())return 8;
    std::ofstream output("tests/fixtures/adventure/schema4-component-r01.bin",std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    std::cout<<"schema4 component fixture, not physical gameplay\nbytes="<<bytes.size()<<"\nsha256="<<voxy::core::sha256Hex(voxy::core::sha256(bytes))<<"\ncontent="<<voxy::core::sha256Hex(content.identity)<<"\nrevision="<<restored.revision<<"\n";
    std::cout.precision(17);for(const auto& e:content.encounters)std::cout<<"spawn"<<int(e.id)<<'='<<e.spawn.x<<','<<e.spawn.y<<','<<e.spawn.z<<'\n';
}

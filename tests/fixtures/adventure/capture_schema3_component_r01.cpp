#include "game/adventure/adventure_save.hpp"
#include "game/adventure/adventure_encounters.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include <fstream>
#include <iostream>
#include <numbers>
#include <cmath>
using namespace voxy::game::adventure;
int main() {
    static_assert(kAdventureSaveSchema==3);
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
    auto session=AdventureSession::create(world,content,error);if(!session)return 5;
    const CandidateValidator fixture=[](const auto&,const auto&,std::string&){return true;};
    auto stamp=[&]{return CommandStamp{session->state().revision,session->state().lastRequestSequence+1,1};};
    auto publish=[&](std::optional<AdventureSession::PreparedChange> prepared){if(!prepared||!session->commit(std::move(*prepared),error)){std::cerr<<error<<'\n';std::exit(6);}};
    auto layout=starterRoomLayout({-2800,-7250,-44600},0,error);publish(session->prepareBlueprint(stamp(),layout,fixture,error));
    uint64_t bed=0,chest=0,bench=0;for(const auto& c:session->state().components){if(c.kind==FurnitureKind::Bed)bed=c.id;if(c.kind==FurnitureKind::Chest)chest=c.id;if(c.kind==FurnitureKind::Workbench)bench=c.id;}
    publish(session->prepareUseBed(stamp(),bed,content.town,fixture,error));
    publish(session->prepareAcceptHomeQuest(stamp(),1,fixture,error));publish(session->prepareCompleteHomeQuest(stamp(),1,fixture,error));
    publish(session->prepareCraftCompass(stamp(),bench,fixture,error));
    auto slot=[&](ItemKind kind){for(uint8_t i=0;i<session->state().backpack.size();++i)if(session->state().backpack[i].kind==kind)return i;return uint8_t(255);};
    publish(session->prepareEquipUtility(stamp(),slot(ItemKind::TrailCompass),error));
    publish(session->prepareCraftStaff(stamp(),bench,fixture,error));publish(session->prepareEquipTool(stamp(),slot(ItemKind::TrailStaff),error));
    CombatTick tick;tick.tick=1;tick.player=session->state().player;tick.health=47;tick.playerCombat.attackSerial=1;tick.playerCombat.attackReadyTick=37;tick.playerCombat.attackImpactTick=13;tick.playerCombat.dodgeReadyTick=55;tick.playerCombat.dodgeUntilTick=10;tick.playerCombat.invulnerableUntilTick=8;tick.playerCombat.dodgeDirectionX=.6;tick.playerCombat.dodgeDirectionZ=.8;
    for(size_t i=0;i<2;++i){tick.enemies[i]=session->state().combat.encounters[i].checkpoint;tick.enemies[i].positioned=true;}
    tick.enemies[0].health=35;tick.enemies[0].phase=EnemyPhase::Windup;tick.enemies[0].phaseTicks=17;tick.enemies[0].attackSerial=1;
    tick.enemies[1].health=0;tick.enemies[1].phase=EnemyPhase::Dead;tick.enemies[1].lastPlayerAttackSerial=1;
    publish(session->prepareCombatTick(stamp(),tick,fixture,error));publish(session->prepareClaimEncounterLoot(stamp(),2,1,fixture,error));
    const auto chestRevision=AdventureSession::findComponent(session->state(),chest)->revision;
    publish(session->prepareTransfer(stamp(),{0,chest,session->state().backpackRevision,chestRevision,slot(ItemKind::RelayCore),1},fixture,error));
    std::vector<std::byte> bytes;if(!AdventureSaveCodec::encode(session->state(),content,bytes,error)){std::cerr<<error;return 7;}
    AdventureState restored;if(!AdventureSaveCodec::decode(bytes,world,content,restored,error)||restored!=session->state())return 8;
    std::ofstream output("tests/fixtures/adventure/schema3-component-r01.bin",std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    std::cout<<"schema3 component fixture, not physical gameplay\nbytes="<<bytes.size()<<"\nsha256="<<voxy::core::sha256Hex(voxy::core::sha256(bytes))<<"\ncontent="<<voxy::core::sha256Hex(content.identity)<<"\nrevision="<<restored.revision<<"\n";
    std::cout.precision(17);for(const auto& e:content.encounters)std::cout<<"spawn"<<int(e.id)<<'='<<e.spawn.x<<','<<e.spawn.y<<','<<e.spawn.z<<'\n';
}

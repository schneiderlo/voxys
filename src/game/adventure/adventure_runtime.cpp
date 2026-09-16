#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/building_doors.hpp"
#include "game/adventure/adventure_input.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/assets/robot_asset.hpp"
#include "engine/platform/input.hpp"
#include "camera/camera.hpp"
#include "physics/physics_types.hpp"
#include "core/log.hpp"
#include "core/sha256.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cfenv>
#include <locale>
#include <iomanip>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#if defined(VOXY_NATIVE)
#include "engine/platform/native/native_adventure_saves.hpp"
#include "engine/platform/native/adventure_preferences_store.hpp"
#else
#include <emscripten.h>
#endif

namespace voxy::game::adventure {
namespace {
struct Bootstrap {construction::WorldNamespace world;std::vector<std::byte> bytes;};
std::optional<Bootstrap> pendingBootstrap;
glm::dvec3 metres(GridPosition p){return {p.x*.02,p.y*.02,p.z*.02};}
glm::dmat4 model(const WorldPart& p,glm::dvec3 origin) {
    return glm::translate(glm::dmat4(1),metres(p.position)-origin)
        *glm::rotate(glm::dmat4(1),p.yawQuarterTurns*std::numbers::pi/2,glm::dvec3(0,1,0));
}
std::filesystem::path installedPath(std::string_view p) {
#if defined(VOXY_NATIVE)
    if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)
        return std::filesystem::path(workspace)/p;
#endif
    return p;
}
std::optional<moto::VmeshData> installedBuildingMesh(std::string& error) {
    constexpr size_t expectedBytes=1116054;
    constexpr std::string_view expectedDigest="08ce2ff933bdc9d86d68d62e29b15f62d7f22984753f20b51fdb89348fd3236b";
    std::ifstream file(installedPath("data/adventure/building-kit-r01/cooked/building-kit-lod0.vmesh"),std::ios::binary);
    // Read one extra byte to reject truncation and oversized content without
    // trusting filesystem size or allocating from file-controlled lengths.
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed building kit is missing or has changed.";return std::nullopt;
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=expectedDigest) {
        error="The installed building kit does not match this adventure.";return std::nullopt;
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return std::nullopt;
    // These authored names were inspected in the hash-checked building kit.
    // Change its in-memory palette only; the character and Cove assets retain
    // their own materials and the cooked geometry remains byte-identical.
    constexpr std::array<std::pair<std::string_view,uint32_t>,5> palette{{
        {"adventure_cream",0xe8d8b8},{"adventure_teal",0x627052},{"adventure_wood",0xa5784f},
        {"adventure_coral",0xb76343},{"adventure_slate",0x494d48}}};
    for(auto& material:mesh.materials) {
        const auto entry=std::find_if(palette.begin(),palette.end(),[&](const auto& value){return value.first==mesh.name(material.nameOffset);});
        if(entry==palette.end()){error="The building palette has an unknown material.";return std::nullopt;}
        for(uint32_t channel=0;channel<3;++channel) {
            const float srgb=static_cast<float>((entry->second>>((2u-channel)*8u))&255u)/255.f;
            material.baseColorFactor[channel]=srgb<=.04045f?srgb/12.92f:std::pow((srgb+.055f)/1.055f,2.4f);
        }
        material.roughnessFactor=std::max(.6f,material.roughnessFactor);material.metallicFactor=0;
    }
    return mesh;
}
std::optional<moto::VmeshData> installedVillageMesh(std::string& error) {
    constexpr size_t bytesExpected=457370;
    constexpr std::string_view digest="c70c2d5c8762d9e7be260edb2eb884352a82661f794431f57ac271472c39a3b0";
    std::ifstream file(installedPath("data/adventure/village-props-r01/village-props-lod0.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(bytesExpected+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(bytesExpected)) {
        error="The installed village props are missing or oversized.";return {};
    }
    bytes.resize(bytesExpected);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed village props have changed.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=4||mesh.nodes.size()!=4) {error="The village prop layout is unsupported.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedDoorMesh(std::string& error) {
    constexpr size_t expectedBytes=49708;
    constexpr std::string_view digest="be59bb6888aacc94c345fa3aebf0ebf1df1cacc1c3082e55c01eff16753b1e5f";
    std::ifstream file(installedPath("data/adventure/door-r01/cooked/door-leaf-lod0.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed door is missing or oversized.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed door has changed.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=1||mesh.nodes.size()!=1||mesh.materials.size()!=2) {
        error="The installed door layout is unsupported.";return {};
    }
    return mesh;
}
std::array<glm::dvec3,2> markerPositions(const terrain::lego::Surface& surface) {
    const auto& world=installedWorld();const std::array points{world.town+glm::dvec2(0,-3),world.landmark};
    std::array<glm::dvec3,2> result;
    for(size_t i=0;i<points.size();++i)result[i]={points[i].x,terrain::lego::supportHeight(surface,glm::vec2(points[i]),.5f),points[i].y};
    return result;
}
void appendMarkers(construction::WorldNamespace world,const terrain::lego::Surface& terrain,std::vector<AdventureSpatialQueries::Solid>& solids) {
    const auto positions=markerPositions(terrain);
    for(size_t i=0;i<positions.size();++i)solids.push_back({{world,i+1},{world,i+1},positions[i]+glm::dvec3(-.5,0,-.5),positions[i]+glm::dvec3(.5,i?4.:2.,.5)});
}
std::string quote(std::string_view value) {
    constexpr char digits[]="0123456789abcdef";std::string result="\"";
    for(char byte:value) {
        const auto c=static_cast<unsigned char>(byte);
        if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
        else if(c<32){result+="\\u00";result+=digits[c>>4];result+=digits[c&15];}
        else result+=char(c);
    }
    result+='"';return result;
}
std::string costLabel(MaterialCost cost) {
    std::string result;
    const auto add=[&](uint32_t quantity,const char* name) {
        if(!quantity)return;
        if(!result.empty())result+=" / ";
        result+=std::to_string(quantity);result+=' ';result+=name;
    };
    add(cost.wood,"wood");add(cost.stone,"stone");add(cost.scrap,"scrap");
    return result.empty()?"No materials":result;
}
}
AdventureRuntime::AdventureRuntime(bool freeBuild):routingPreferences_(adventureRoutingPreferences(preferences_)),freeBuild_(freeBuild){}
AdventureRuntime::~AdventureRuntime(){
    // An interrupted Application can still have submitted queue work. The
    // commands retain these references; do not Destroy their textures here.
    meshes_.releaseHandles();
}
bool AdventureRuntime::stageWorld(std::string_view world,std::string_view hex) {
    constexpr std::string_view digits="0123456789abcdef";
    if(pendingBootstrap||world.size()!=32||hex.size()%2||hex.size()>2*1024*1024)return false;
    Bootstrap value;
    for(size_t i=0;i<16;++i) {const auto a=digits.find(world[i*2]),b=digits.find(world[i*2+1]);if(a==digits.npos||b==digits.npos)return false;value.world.bytes[i]=uint8_t(a*16+b);}
    if(!construction::isValid(value.world))return false;
    value.bytes.reserve(hex.size()/2);
    for(size_t i=0;i<hex.size();i+=2){const auto a=digits.find(hex[i]),b=digits.find(hex[i+1]);if(a==digits.npos||b==digits.npos)return false;value.bytes.push_back(std::byte(a*16+b));}
    pendingBootstrap=std::move(value);return true;
}
void AdventureRuntime::saveCompleted(std::string status) {
    if(status.starts_with("Saved")){savedRevision_=savingRevision_;migrationDirty_=false;saveFailure_.clear();if(state().revision!=savedRevision_)status="Checkpoint saved. New changes are unsaved.";}
    else saveFailure_=status;
    saveStatus_=std::move(status);
}
bool AdventureRuntime::applyPreferences(const AdventurePreferences& next,bool force) {
    std::string error;
    if(!validateAdventurePreferences(next,error)){preferencesStatus_=std::move(error);return false;}
    if(!force&&next==preferences_)return true;
    if(preferencesRevision_==UINT64_MAX){preferencesStatus_="Reopen the adventure before changing more settings.";return false;}
    preferences_=next;routingPreferences_=adventureRoutingPreferences(preferences_);++preferencesRevision_;
    auto cameraSettings=orbit_.settings();cameraSettings.reducedMotion=preferences_.reducedMotion;(void)orbit_.settings(cameraSettings);
    inputRouter_.reset();buildRouter_.reset();combatInput_.reset();pendingAttack_=false;pendingDodge_=false;
    preferencesStatus_="Settings apply for this visit.";
#if defined(VOXY_NATIVE)
    (void)platform::saveNativeAdventurePreferences(preferencesPath_,preferences_,preferencesStatus_);
#endif
    return true;
}
std::string AdventureRuntime::preferencesAction(int action,std::string_view text) {
    std::string error;
    if(action==1){std::string encoded;if(encodeAdventurePreferences(preferences_,encoded,error))return encoded;return {};}
    if(action==2) {
        AdventurePreferences next;
        if(!parseAdventurePreferences(text,next,error)){preferencesStatus_=error;return error;}
        return applyPreferences(next)?"ok":preferencesStatus_;
    }
    if(action==3)return applyPreferences(AdventurePreferences{},true)?"ok":preferencesStatus_;
    if(action==4){preferencesStatus_="Settings saved on this device.";return "ok";}
    if(action==5){preferencesStatus_=text.empty()?"Settings apply for this visit. Device storage is unavailable.":std::string(text.substr(0,240));return "ok";}
    return "Unknown settings action.";
}
bool AdventureRuntime::initialize(terrain::lego::Surface surface,WGPUDevice device,WGPUQueue queue,
    const std::filesystem::path& shaders,WGPUTextureFormat color,std::string& error) {
    previewResult_.reset();structureJson_.reset();aimRayResult_.reset();
    if(!matchesInstalledTerrain(surface)||!queries_.bindTerrain(surface)) {error="Adventure terrain does not match the installed world.";return false;}
    const auto spawn=townSpawn(surface);
    content_.town={spawn.x,spawn.y,spawn.z,0};
    // Bind the exact terrain recipe and full sample bytes once. No second copy.
    static_assert(std::endian::native==std::endian::little);
    core::Sha256 terrainHash;terrainHash.update(std::as_bytes(surface.samples));
    if(core::sha256Hex(terrainHash.finish())!=installedWorld().samplesSha256) {error="Main landscape content has changed.";return false;}
    core::Sha256 hash;hash.string(installedWorld().profile);hash.string(installedWorld().contentRevision);
    hash.string(installedWorld().terrainRecipe);hash.string(installedWorld().samplesSha256);
    hash.string(legacyBuildingCatalogFingerprint());hash.string("adventure-items-and-homes-r01");
    content_.legacyIdentity=hash.finish();
    core::Sha256 currentHash;currentHash.string(core::sha256Hex(*content_.legacyIdentity));
    currentHash.string("adventure-town-home-quest-compass-r01:quest1-v1:wood2-scrap4:equipped-utility");
    const auto schema2=currentHash.finish();
    content_.compatibilityIdentities=installedAdventureCompatibilityIdentities();
    if(content_.compatibilityIdentities[0]!=content_.legacyIdentity||content_.compatibilityIdentities[1]!=schema2) {
        error="The installed adventure compatibility profile has changed.";return false;
    }
    AdventureSpatialQueries spawnQueries;
    if(!spawnQueries.bindTerrain(surface)||!spawnQueries.publish({},1)
        ||!defaultEncounterContent(spawnQueries,content_.encounters,error))return false;
    core::Sha256 combatHash;combatHash.string(core::sha256Hex(schema2));
    combatHash.string("adventure-trail-combat-r01:staff6-wood4-scrap4-damage25-windup12-ready36:dodge12-ready54:raiders1-2-health60-100-damage12-18:notice7-leash12-reach1.6-windup42-recover54:loot8scrap-relaycore7");
    const auto schema3=combatHash.finish();
    if(content_.compatibilityIdentities[2]!=schema3) {error="The trail combat compatibility profile has changed.";return false;}
    core::Sha256 trailHash;trailHash.string(core::sha256Hex(schema3));trailHash.string(trailContentFingerprint());
    const auto schema4=trailHash.finish();
    if(content_.compatibilityIdentities[3]!=schema4) {error="The trail discovery compatibility profile has changed.";return false;}
    core::Sha256 sideHash;sideHash.string(core::sha256Hex(schema4));sideHash.string(sideQuestContentFingerprint());
    const auto schema5=sideHash.finish();
    if(content_.compatibilityIdentities[4]!=schema5) {error="The building recipe compatibility profile has changed.";return false;}
    core::Sha256 doorHash;doorHash.string(core::sha256Hex(schema5));doorHash.string(adventureDoorContentFingerprint());
    content_.identity=doorHash.finish();
    for(uint32_t i=0;i<18;++i) {
        const double angle=i*2*std::numbers::pi/18;
        const glm::dvec2 p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
        const auto y=terrain::lego::supportHeight(surface,glm::vec2(p),.3f);
        content_.resourceNodes.push_back({i+1,{p.x,y,p.y,0},{ItemKind(1+i%3),uint16_t(i%3==0?12:8)}});
    }
    if(!defaultTrailContent(spawnQueries,content_.discoveries,content_.resourceNodes,error))return false;
    content_.enableTrailProgress=true;
    if(freeBuild_) {
        core::Sha256 creative;creative.string("voxys.free-build.v1");creative.string(installedWorld().samplesSha256);
        creative.string(buildingCatalogFingerprint());creative.string("unlimited-grounded-bricks-r01");
        content_={};content_.freeBuilding=true;content_.identity=creative.finish();content_.town={spawn.x,spawn.y,spawn.z,0};
        building_=true;selected_=PieceKind::Brick2x4;catalogCategory_=1;
        status_="Choose a brick, aim and build.";saveStatus_="Unsaved build";
    }
    construction::WorldNamespace world;std::random_device random;
    for(auto& byte:world.bytes)byte=static_cast<uint8_t>(random());
    world.bytes[6]=(world.bytes[6]&15)|64;world.bytes[8]=(world.bytes[8]&63)|128;
#if defined(VOXY_NATIVE)
    expedition::StoreIssue issue;
    const char* overrideRoot=std::getenv("VOXY_ADVENTURE_ROOT");
    auto root=overrideRoot&&*overrideRoot?std::filesystem::path(overrideRoot):platform::NativeSaveStore::defaultRoot(issue);
    if(issue){error="Adventure save directory is unavailable.";return false;}
    preferencesPath_=root/"adventure-preferences-v1.json";
    if(const char* path=std::getenv("VOXY_ADVENTURE_PREFERENCES");path&&*path)preferencesPath_=path;
    (void)platform::loadNativeAdventurePreferences(preferencesPath_,preferences_,preferencesStatus_);
    routingPreferences_=adventureRoutingPreferences(preferences_);
    auto savedCameraSettings=orbit_.settings();savedCameraSettings.reducedMotion=preferences_.reducedMotion;(void)orbit_.settings(savedCameraSettings);
    std::optional<std::string> selectedWorld;
    if(const char* selected=std::getenv("VOXY_ADVENTURE_WORLD");selected&&*selected)selectedWorld=selected;
    saves_=platform::NativeAdventureSaves::open(root,selectedWorld,std::getenv("VOXY_ADVENTURE_NEW")!=nullptr,content_,error);
    if(!saves_)return false;
    world=saves_->worldIdentity();
#else
    if(pendingBootstrap)world=pendingBootstrap->world;
#endif
    session_=AdventureSession::create(world,content_,error);
    std::vector<AdventureSpatialQueries::Solid> markers;if(!freeBuild_)appendMarkers(world,surface,markers);
    if(!session_||!queries_.publish(markers,session_->state().revision+1)
        ||!player_.initialize(queries_,spawn,installedWorld().waterHeight)) {error="Could not start on safe ground.";return false;}
    robot_=assets::loadHumanAsset(installedPath("data/adventure/human-r01"),error);
    if(!robot_)return false;
    raider_=assets::loadRaiderAsset(installedPath("data/adventure/raider-r01"),error);
    if(!raider_)return false;
    constexpr std::array residentPaths{"data/adventure/residents-r01/moss",
        "data/adventure/residents-r01/rivet","data/adventure/residents-r01/lumen"};
    for(size_t i=0;i<residentAssets_.size();++i) {
        residentAssets_[i]=assets::loadResidentAsset(uint32_t(i+1),installedPath(residentPaths[i]),error);
        if(!residentAssets_[i])return false;
    }
    auto buildingMesh=installedBuildingMesh(error);
    if(!buildingMesh)return false;
    render::MeshPathConfig config;
    config.shaderPath=shaders/"mesh_path.wgsl";config.colorFormat=WGPUTextureFormat_RGBA16Float;
    config.frontFace=WGPUFrontFace_CW;config.linearHdrOutput=true;config.sunShadows=true;
    // Reserve expanded submesh draws once, not merely logical piece count.
    // 1,024 parts at four draws, fixed village (64 pieces/32 props),
    // four animated figures, supplies/markers and a whole-room preview.
    config.maxInstances=6144;config.maxDrawsPerFrame=6144;
    if(!meshes_.init(device,queue,config)
        ||!meshes_.loadMeshData(*buildingMesh)
        ||!meshes_.loadMeshData(robot_->mesh)
        ||!hud_.init(device,queue,color,shaders/"cove_hud.wgsl")) {error="Could not load the adventure building kit.";return false;}
    for(const auto& resident:residentAssets_)if(!meshes_.loadMeshData(resident->mesh)) {
        error="Could not load the town's character appearances.";return false;
    }
    const auto villageMesh=installedVillageMesh(error);
    if(!villageMesh||!meshes_.loadMeshData(*villageMesh)||!meshes_.loadMeshData(raider_->mesh))return false;
    const auto doorMesh=installedDoorMesh(error);
    if(!doorMesh||!meshes_.loadMeshData(*doorMesh))return false;
#if defined(VOXY_NATIVE)
    if(!saves_->loadedBytes().empty()&&!restore(saves_->loadedBytes(),world,error))return false;
#else
    if(pendingBootstrap) {
        const auto bootstrap=std::exchange(pendingBootstrap,{});
        if(!bootstrap->bytes.empty()&&!restore(bootstrap->bytes,world,error))return false;
    }
#endif
    AdventureSpatialQueries populated;TownResidents residents;VillageLayout village;TrailSites sites;
    if(!prepareGeometry(state(),populated,residents,village,sites,error))return false;
    AdventureSpatialQueries actors;AdventureEncounters encounters;
    if(!prepareActors(state(),populated,actors,encounters,error))return false;
    walkQueries_=std::move(populated);queries_=std::move(actors);encounters_=std::move(encounters);
    town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
    if(!freeBuild_&&!combat_.initialize(content_,walkQueries_,installedWorld().waterHeight)) {error="The trail navigation is unavailable.";return false;}
    for(size_t i=0;i<residentFacing_.size();++i)residentFacing_[i]=town_.entries()[i].yaw;
    refreshHud();return true;
}
CommandStamp AdventureRuntime::stamp() const {return {state().revision,state().lastRequestSequence+1,1};}
CandidateValidator AdventureRuntime::validator() const {
    return [this](const AdventureState& before,const AdventureState& after,std::string& error){
        if(after.health>before.health&&!encounters_.safeRest(before.player,walkQueries_)) {
            error="Reach a safe place before resting.";return false;
        }
        return validateConstruction(before,after,queries_,error,freeBuild_)
            &&(freeBuild_||village_.validateNewConstruction(before,after,error))
            &&(freeBuild_||trailSites_.validateNewConstruction(before,after,error))
            &&validateInteractions(before,after,content_,queries_,error);
    };
}
CandidateValidator AdventureRuntime::npcValidator(uint8_t npc,bool requireHome) const {
    return [this,npc,requireHome](const AdventureState& before,const AdventureState& after,std::string& error){
        if(!town_.interactable(npc,before.player,queries_,error))return false;
        if(requireHome) {
            const auto readiness=firstHomeReadiness(before,queries_);
            if(!readiness.ready()){error=readiness.message;return false;}
        }
        return validator()(before,after,error);
    };
}
bool AdventureRuntime::prepareGeometry(const AdventureState& state,AdventureSpatialQueries& out,TownResidents& residents,VillageLayout& village,TrailSites& sites,std::string& error,bool preserveInstalled) const {
    if(state.revision==UINT64_MAX){error="World revision capacity reached.";return false;}
    std::vector<AdventureSpatialQueries::Solid> solids;
    if(!validateInstalledGeometry(state,queries_,error,freeBuild_)||!compileSolids(state,solids,error))return false;
    if(freeBuild_){residents={};village={};sites={};return out.bindTerrain(queries_.terrain())&&out.publish(solids,state.revision+1);}
    appendMarkers(state.world,queries_.terrain(),solids);
    // Scenery admission reserves a built door's complete opening space. The
    // temporary reservation is never published as a walking/picking collider.
    std::vector<AdventureSpatialQueries::Solid> swings;
    if(!compileDoorSwingSolids(state,swings,error))return false;
    const auto admissionSolids=[&] {
        auto result=solids;result.insert(result.end(),swings.begin(),swings.end());return result;
    };
    AdventureSpatialQueries base;
    const auto foundationAdmission=admissionSolids();
    if(!base.bindTerrain(queries_.terrain())||!base.publish(foundationAdmission,state.revision+1))return false;
    village=VillageLayout::admit(state,content_,base,foundationAdmission,preserveInstalled?&village_:nullptr);
    if(!village.appendSolids(state.world,solids)){error="Village collision capacity reached.";return false;}
    AdventureSpatialQueries inhabited;
    if(!inhabited.bindTerrain(queries_.terrain())||!inhabited.publish(admissionSolids(),state.revision+1))return false;
    residents=TownResidents::admit(state,inhabited,preserveInstalled?&town_:nullptr);
    if(!residents.appendSolids(state.world,solids)){error="Town collision capacity reached.";return false;}
    AdventureSpatialQueries settled;
    if(!settled.bindTerrain(queries_.terrain())||!settled.publish(admissionSolids(),state.revision+1))return false;
    sites=TrailSites::admit(state,content_,settled,preserveInstalled?&trailSites_:nullptr);
    if(!sites.appendSolids(state.world,solids)){error="Trail scenery capacity reached.";return false;}
    return out.bindTerrain(queries_.terrain())&&out.publish(solids,state.revision+1);
}
bool AdventureRuntime::commit(std::optional<AdventureSession::PreparedChange> prepared) {
    if(!prepared)return false;
    const bool homeChanged=state().structures!=prepared->state().structures;
    AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;
    if(!prepareGeometry(prepared->state(),next,residents,village,sites,status_))return false;
    AdventureSpatialQueries actors;AdventureEncounters encounters;
    if(!prepareActors(prepared->state(),next,actors,encounters,status_))return false;
    for(const auto& actor:encounters_.entries())if(actor.available) {
        const auto* after=encounters.find(actor.id);
        if(!after||!after->available){status_="That change would block a trail character. Leave room around them.";return false;}
    }
    const auto changed=prepared->changedPart();
    const auto placed=changed&&!AdventureSession::findPart(state(),changed)&&AdventureSession::findPart(prepared->state(),changed)?changed:0;
    if(!session_->commit(std::move(*prepared),status_))return false;
    // The player holds the address of queries_, not its replaceable packet.
    walkQueries_=std::move(next);queries_=std::move(actors);encounters_=std::move(encounters);town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);if(homeChanged)fieldHome_=fieldHomeReadiness(state(),walkQueries_);lastPlaced_=placed?placed:lastPlaced_;
    saveStatus_="Changes not saved";
    return true;
}
bool AdventureRuntime::prepareActors(const AdventureState& state,const AdventureSpatialQueries& base,
    AdventureSpatialQueries& out,AdventureEncounters& encounters,std::string& error) const {
    if(freeBuild_){encounters={};out=base;return true;}
    encounters=AdventureEncounters::admit(state,base);
    std::vector<AdventureSpatialQueries::Solid> solids(base.solids().begin(),base.solids().end());
    if(queries_.revision()==UINT64_MAX||!encounters.appendSolids(state.world,solids)
        ||!out.bindTerrain(base.terrain())||!out.publish(solids,queries_.revision()+1)) {
        error="The trail characters could not be published safely.";return false;
    }
    return true;
}
void AdventureRuntime::advanceCombat(double seconds,AdventureCombat::Input input) {
    if(!std::isfinite(seconds)||seconds<0)return;
    if(freeBuild_) {
        player_.advance(seconds,input.movement);
        const auto p=player_.feet();std::string error;
        if(!session_->updatePlayer({p.x,p.y,p.z,player_.facingYaw()},100,error))status_=error;
        return;
    }
    pendingAttack_=pendingAttack_||input.attack;pendingDodge_=pendingDodge_||input.dodge;
    pendingJump_=pendingJump_||input.movement.jump;combatSeconds_+=std::min(seconds,.25);
    for(int step=0;step<15&&combatSeconds_+1e-12>=AdventurePlayer::fixedStep;++step) {
        combatSeconds_-=AdventurePlayer::fixedStep;
        input.attack=std::exchange(pendingAttack_,false);input.dodge=std::exchange(pendingDodge_,false);
        input.movement.jump=std::exchange(pendingJump_,false);
        const auto oldPlayer=player_.state();const auto oldHealth=state().health;
        std::array<uint16_t,kAdventureEncounterCount> oldEnemyHealth{};
        std::array<PlayerPose,kAdventureEncounterCount> oldEnemyPose{};
        for(size_t i=0;i<oldEnemyHealth.size();++i) {
            oldEnemyHealth[i]=state().combat.encounters[i].checkpoint.health;
            oldEnemyPose[i]=state().combat.encounters[i].checkpoint.pose;
        }
        const auto tick=combat_.step(state(),content_,encounters_,walkQueries_,player_,input);
        const auto check=[this](const AdventureState&,const AdventureState& after,std::string& error) {
            if(!walkQueries_.clearCapsule({after.player.x,after.player.y,after.player.z})) {
                error="Movement is blocked.";return false;
            }
            return true;
        };
        std::string error;auto prepared=session_->prepareCombatTick(stamp(),tick,check,error);
        AdventureSpatialQueries actors;AdventureEncounters encounters;
        bool ready=prepared&&prepareActors(prepared->state(),walkQueries_,actors,encounters,error);
        if(ready)for(const auto& prior:encounters_.entries())if(prior.available&&tick.enemies[prior.id-1].health
            &&!encounters.find(prior.id)->available){ready=false;error="A trail character's movement was refused.";break;}
        if(!ready||!session_->commit(std::move(*prepared),error)) {
            (void)player_.restore(oldPlayer);combat_.reset();combatSeconds_=0;status_=error;break;
        }
        queries_=std::move(actors);encounters_=std::move(encounters);
        if(state().health<oldHealth)status_=state().health?"Hit! Step aside when the staff rises.":"You need a rest. Return home to recover; your belongings are safe.";
        for(size_t i=0;i<oldEnemyHealth.size();++i) {
            const auto& enemy=state().combat.encounters[i].checkpoint;
            if(enemy.health<oldEnemyHealth[i])status_=enemy.health?"Staff hit.":"Trail raider defeated. Approach and use E to collect the loot.";
            const bool gesture=enemy.phase==EnemyPhase::Windup||enemy.phase==EnemyPhase::Attack;
            const double speed=glm::length(glm::dvec2(enemy.pose.x-oldEnemyPose[i].x,enemy.pose.z-oldEnemyPose[i].z))/AdventurePlayer::fixedStep;
            // Cosmetic time follows committed ticks only, including pause.
            enemyCharacters_[i].update(AdventurePlayer::fixedStep,AdventurePlayer::Mode::Walking,
                std::min(speed,3.6),0,gesture);
        }
    }
}
uint8_t AdventureRuntime::nearbyLoot() const {
    if(!state().health)return 0;
    for(const auto& entry:state().combat.encounters)if(entry.deathRevision&&!entry.lootClaimRevision
        &&glm::length(glm::dvec3(entry.checkpoint.pose.x,entry.checkpoint.pose.y,entry.checkpoint.pose.z)-player_.feet())<2.5
        &&AdventureCombat::sight(walkQueries_,state().player,entry.checkpoint.pose))return entry.checkpoint.encounterId;
    return 0;
}
std::string AdventureRuntime::combatLabel() const {
    if(!state().health)return "You need a rest. Use Return home to recover.";
    for(const auto& entry:encounters_.entries())if(entry.available
        &&glm::length(glm::dvec3(entry.pose.x,entry.pose.y,entry.pose.z)-player_.feet())<10) {
        const auto& enemy=state().combat.encounters[entry.id-1].checkpoint;
        return std::string(encounterDefinition(entry.id)->name)+" / "+std::to_string(enemy.health)+" health / "
            +std::string(AdventureCombat::phaseLabel(enemy.phase));
    }
    return interactionLabel();
}
AdventureRuntime::PendingAction AdventureRuntime::observedAction(int action,int value,uint32_t token) const {
    PendingAction pending{action,value,token};
    // Capture the target and desired state for both native and browser buttons.
    // Match the visible Use priority: recovery, loot and residents precede
    // furniture. A nearby door must not steal a labelled conversation/pickup.
    if(action==7&&session_&&state().health&&!nearbyLoot()
        &&!town_.nearestInteractable(state().player,queries_,targetPart_))
        if(const auto* component=AdventureSession::findComponent(state(),nearbyComponent());
        component&&component->kind==FurnitureKind::Door) {
        pending.door=component->id;pending.doorRevision=component->revision;
        pending.doorOpen=!component->doorOpen;
    }
    return pending;
}
void AdventureRuntime::action(int action,int value) {
    if(freeBuild_&&(action==14||action==16||action==17||action==18||action==24||action==27||action==28))return;
    if(action==15){
        const bool owned=value!=0;
        // Canvas pointerdown reaffirms world focus before its mouse event is
        // consumed. A redundant notification must not erase that placement
        // click or a movement key arriving in the same frame.
        if(owned!=pendingUiInputOwned_){pendingUiInputOwned_=owned;uiOwnershipChanged_=true;}
        return;
    }
    if(action==19){domUiAttached_=value!=0;return;}
    // Focus is presentation metadata. Apply its token-validated selection
    // before the next input tick so pad Confirm cannot activate the old row.
    if(action==26){if(const auto row=menuIntents_.resolve(value))menuSelection_=static_cast<int>(*row);return;}
    if(action>=1&&action<=31&&action!=26&&pendingActions_.size()<32) {
        // A queued door use keeps the observed target and desired state.
        // A second queued click cannot reinterpret Open as Close after commit.
        pendingActions_.push_back(observedAction(action,value,menuIntents_.token()));
    }
}
void AdventureRuntime::updateTarget(const Camera& camera,const Input& input,uint32_t width,uint32_t height) {
    glm::dvec2 pointer(input.mousePosition());
    const auto& pad=input.gamepad();
    bool padActive=false;
    for(size_t i=0;i<4;++i)padActive=padActive||std::abs(pad.axis(i))>.01f;
    for(uint8_t i=0;i<uint8_t(PadButton::Count);++i)padActive=padActive||pad.pressed(PadButton(i));
    const bool pointerActive=pointer!=lastAimPointer_
        ||input.wasMouseButtonPressed(MouseButton::Left)||input.wasMouseButtonPressed(MouseButton::Right);
    padAim_=adventurePadOwnsAim(padAim_,pointerActive,pad.connected(),padActive);
    lastAimPointer_=pointer;observedPointer_=pointer;
    observedMouseCaptured_=input.isMouseCaptured();observedPadConnected_=pad.connected();
    glm::dvec2 ndc{};
    if(framePointer_){pointer=framePointer_->framebuffer;ndc=framePointer_->ndc;}
    else if(!input.isMouseCaptured()&&!padAim_&&!uiInputOwned_&&!hudPointerOwned_) {
        hasTarget_=false;targetPart_=0;previewValid_=false;observedRayHit_={};
        previewReason_="The pointer view is unavailable.";return;
    }
    if(input.isMouseCaptured()||padAim_){pointer={double(width)*.5,double(height)*.5};ndc={0,0};}
    if(uiInputOwned_||hudPointerOwned_) {
        ndc=hasWorldAim_?worldAimNdc_:glm::dvec2(0);
        pointer={(ndc.x+1)*double(width)*.5,(1-ndc.y)*double(height)*.5};
    } else {worldAimNdc_=ndc;hasWorldAim_=true;}
    observedAimPointer_=pointer;
    const glm::dvec4 clip(ndc,1,1);
    auto world=glm::dmat4(camera.inverseViewProjectionMatrix())*clip;world/=world.w;
    const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    const auto eye=origin+glm::dvec3(camera.position());
    const auto direction=glm::normalize(origin+glm::dvec3(world)-eye);
    // Match exact IEEE values, including signed zero; restore clears even an
    // equal revision. This is only the fixed 25 m picking query.
    const AimRayKey rayKey{state().world,state().epoch,queries_.revision(),{
        std::bit_cast<uint64_t>(eye.x),std::bit_cast<uint64_t>(eye.y),std::bit_cast<uint64_t>(eye.z),
        std::bit_cast<uint64_t>(direction.x),std::bit_cast<uint64_t>(direction.y),std::bit_cast<uint64_t>(direction.z)}};
    const bool cacheRay=freeBuild_&&std::fegetround()==FE_TONEAREST;
    AdventureSpatialQueries::RayHit hit;
    if(cacheRay&&aimRayResult_&&aimRayResult_->key==rayKey)hit=aimRayResult_->hit;
    else {
        hit=queries_.raycast(eye,direction,25);
        if(cacheRay)aimRayResult_=AimRayResult{rayKey,hit};
    }
    observedRayFrom_=eye;observedRayTo_=eye+direction*25.;observedRayHit_=hit;
    hasTarget_=hit.complete&&hit.hit;targetPart_=hasTarget_?hit.part.counter:0;
    if(!hasTarget_){previewValid_=false;previewReason_="Aim at nearby ground or a building.";return;}
    aimPoint_=hit.point;
    // Exploration still needs picking for interaction, but does not need a
    // speculative copy, material calculation and support graph every frame.
    if(!building_){previewValid_=false;previewReason_.clear();return;}
    const auto* definition=buildingDefinition(selected_);
    const bool fine=input.isKeyDown(Key::LeftShift)||input.isKeyDown(Key::RightShift);
    const auto snap=[fine](double value){const double grid=fine?.02:1.;return std::round(value/grid)*grid;};
    const auto& bounds=definition->bounds;
    glm::dvec3 half=metres(bounds.maximum)-metres(bounds.minimum);half*=.5;
    if(yaw_%2)std::swap(half.x,half.z);
    glm::dvec3 p=hit.point;
    if(hit.terrain) {
        p.x=snap(p.x);p.z=snap(p.z);
        const auto y=terrainPlacementHeight(selected_,yaw_,{p.x,p.z},queries_.terrain());
        if(!y){previewValid_=false;previewReason_="Aim inside the landscape.";return;}
        p.y=*y;
        if(blueprint_!=BlueprintKind::None) {
            std::string error;
            for(const auto& part:buildingBlueprintLayout(blueprint_,{},yaw_,error))if(part.position.y==0) {
                const auto offset=metres(part.position);
                const auto support=terrainPlacementHeight(part.kind,part.yawQuarterTurns,{p.x+offset.x,p.z+offset.z},queries_.terrain());
                if(!support){previewValid_=false;previewReason_="Keep the whole blueprint inside the landscape.";return;}
                p.y=std::max(p.y,*support);
            }
        }
    } else {
        p+=hit.normal*glm::dvec3(half.x,half.y,half.z);p.y-=half.y;
        // Preserve the exact contact face (wall thickness is .32 m). Tangent
        // axes still use the stud grid, so aiming remains predictable.
        p.x=std::abs(hit.normal.x)>.5?std::round(p.x/.02)*.02:snap(p.x);
        p.z=std::abs(hit.normal.z)>.5?std::round(p.z/.02)*.02:snap(p.z);
        p.y=std::round(p.y/.02)*.02;
    }
    p.y+=heightSteps_*.32;
    preview_={0,selected_,{int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))},yaw_,selectedPaint_};
    if(targetPart_)for(const auto& structure:state().structures)
        if(std::any_of(structure.parts.begin(),structure.parts.end(),[&](const auto& part){return part.id==targetPart_;}))preview_.structure=structure.id;
    // Ground additions touching an existing home join its authority. The policy
    // still checks actual support/contact; nearby distance alone cannot admit it.
    if(!preview_.structure)for(const auto& structure:state().structures)
        for(const auto& part:structure.parts)if(glm::length(metres(part.position)-p)<4.1){preview_.structure=structure.id;break;}
    const bool cachePreview=cacheRay&&blueprint_==BlueprintKind::None;
    const PreviewKey key{state().world,state().epoch,state().revision,state().lastRequestSequence,
        queries_.revision(),preview_.structure,preview_.kind,preview_.position,preview_.yawQuarterTurns,preview_.paint};
    if(cachePreview&&previewResult_&&previewResult_->key==key) {
        previewValid_=previewResult_->valid;previewReason_=previewResult_->reason;return;
    }
    previewReason_.clear();
    auto candidate=blueprint_!=BlueprintKind::None?session_->prepareBuildRecipe(stamp(),blueprint_,preview_.position,yaw_,validator(),previewReason_)
        :session_->preparePlace(stamp(),preview_,validator(),previewReason_);
    previewValid_=bool(candidate);
    if(previewValid_)previewReason_="Ready to place";
    if(cachePreview)previewResult_=PreviewResult{key,previewValid_,previewReason_};
}
uint64_t AdventureRuntime::nearbyComponent() const {
    double best=3.25;uint64_t result=0;
    for(const auto& component:state().components)if(!freeBuild_||component.kind==FurnitureKind::Door)if(const auto* part=AdventureSession::findPart(state(),component.part)) {
        double distance=glm::length(metres(part->position)-player_.feet());
        if(component.part==targetPart_)distance-=.25;
        std::string reason;
        if(distance<best&&reachableComponent(state(),component.id,queries_,reason)){best=distance;result=component.id;}
    }
    return result;
}
uint8_t AdventureRuntime::nearbyDiscovery() const {
    for(uint8_t id=1;id<=2;++id) {
        if(state().trail.discoveries[id-1].rewardClaimRevision)continue;
        std::string error;
        if(trailSites_.discoveryReachable(id,state().player,queries_,error))return id;
    }
    return 0;
}
bool AdventureRuntime::nearbyRelay() const {
    std::string error;return trailSites_.relayReachable(state().player,queries_,error);
}
void AdventureRuntime::openJournal() {
    journalQuest_=1;
    if(state().firstHome.phase==QuestPhase::Completed) {
        journalQuest_=4;
        for(uint8_t id=2;id<=4;++id)if(state().trail.quests[id-2].phase!=QuestPhase::Completed){journalQuest_=id;break;}
    }
    menu_=Menu::Journal;menuSelection_=0;player_.discardPendingInput();
}
void AdventureRuntime::openGuide(bool topics,AdventureGuideTopic topic) {
    if(topic>=AdventureGuideTopic::Count)return;
    if(menu_!=Menu::Guide&&menu_!=Menu::GuideTopics) {
        guideReturnMenu_=menu_;guideReturnSelection_=menuSelection_;guideGamepad_=padAim_;
    }
    guideTopic_=topic;menu_=topics?Menu::GuideTopics:Menu::Guide;menuSelection_=0;
    player_.discardPendingInput();combatSeconds_=0;pendingAttack_=false;pendingDodge_=false;pendingJump_=false;
    combatInput_.reset();inputRouter_.reset();buildRouter_.reset();
}
void AdventureRuntime::selectBlueprint(BlueprintKind kind) {
    const auto* definition=buildingBlueprintDefinition(kind);
    if(!definition)return;
    if(kind==BlueprintKind::WideStoneStep&&!wideStoneStepRecipeUnlocked(state())) {
        status_="Help Moss with The Surveyor's Notes to learn the Wide stone step.";return;
    }
    blueprint_=kind;selected_=definition->anchorPiece;heightSteps_=0;building_=true;menu_=Menu::None;
    player_.discardPendingInput();
}
void AdventureRuntime::cycleCompass() {
    for(uint8_t offset=1;offset<=5;++offset) {
        const auto target=static_cast<CompassTarget>((static_cast<uint8_t>(compassTarget_)+offset)%5);
        if(trailCompassReadout(state(),queries_,trailSites_,target).available){compassTarget_=target;return;}
    }
}
std::string AdventureRuntime::interactionLabel() const {
    if(!state().health)return "Return to home / town";
    if(nearbyLoot())return "Collect trail loot";
    if(const auto* resident=residentDefinition(town_.nearestInteractable(state().player,queries_,targetPart_)))
        return "Talk to "+std::string(resident->name);
    if(const auto* component=AdventureSession::findComponent(state(),nearbyComponent())) {
        if(component->kind==FurnitureKind::Chest)return "Open chest";
        if(component->kind==FurnitureKind::Workbench)return "Use workbench";
        if(component->kind==FurnitureKind::Door)return component->doorOpen?"Close door":"Open door";
        return "Rest and register home";
    }
    if(const auto id=nearbyDiscovery())return state().trail.discoveries[id-1].discoveredRevision?"Collect discovery supplies":"Explore this landmark";
    if(nearbyRelay())return state().trail.relayActivationRevision?"Read relay directions":"Repair the relay";
    for(const auto& node:content_.resourceNodes)
        if(!std::binary_search(state().depletedNodes.begin(),state().depletedNodes.end(),node.id)
            &&glm::length(glm::dvec3(node.position.x,node.position.y,node.position.z)-player_.feet())<3)
            return "Gather "+std::string(itemDefinition(node.yield.kind)->name);
    return "Use nearby";
}
void AdventureRuntime::useDoor(uint64_t id,bool open,uint64_t expectedRevision) {
    if(!menuIntents_.claimActivation())return;
    if(commit(session_->prepareSetDoorOpen(stamp(),id,open,expectedRevision,validator(),status_)))
        status_=open?"Door opened.":"Door closed.";
}
void AdventureRuntime::use(bool allowDoor) {
    if(freeBuild_) {
        const auto* component=AdventureSession::findComponent(state(),nearbyComponent());
        if(allowDoor&&component&&component->kind==FurnitureKind::Door)useDoor(component->id,!component->doorOpen,component->revision);
        else status_="Move close to a door to open or close it.";
        return;
    }
    if(!state().health){recover();return;}
    if(const auto id=nearbyLoot()) {
        const auto check=[this,id](const AdventureState&,const AdventureState&,std::string& error) {
            if(nearbyLoot()!=id){error="Move close to the trail loot.";return false;}return true;
        };
        if(commit(session_->prepareClaimEncounterLoot(stamp(),id,content_.encounters[id-1].generation,check,status_)))
            status_="Trail loot collected. Save to keep your progress.";
        return;
    }
    if(const auto npc=town_.nearestInteractable(state().player,queries_,targetPart_)){talk(uint8_t(npc));return;}
    const auto id=nearbyComponent();const auto* component=AdventureSession::findComponent(state(),id);
    if(component) {
        if(!reachableComponent(state(),id,queries_,status_))return;
        if(component->kind==FurnitureKind::Door) {
            if(allowDoor)useDoor(id,!component->doorOpen,component->revision);
            return;
        }
        if(component->kind==FurnitureKind::Chest){chest_=id;menu_=Menu::Chest;menuSelection_=0;status_="Choose items to move. Nothing is discarded.";return;}
        if(component->kind==FurnitureKind::Workbench) {
            bench_=id;menu_=Menu::Workbench;menuSelection_=0;player_.discardPendingInput();
            status_="Choose a recipe. Crafting uses your materials.";
            return;
        }
        PlayerPose recovery;
        if(!encounters_.safeRest(state().player,walkQueries_)) {
            status_="Move away from the trail raiders before resting.";return;
        }
        if(usableBed(state(),id,queries_,recovery,status_)
            &&commit(session_->prepareUseBed(stamp(),id,recovery,validator(),status_)))status_="Rested. This home is your recovery point.";
        return;
    }
    if(const auto discovery=nearbyDiscovery()) {
        const auto check=[this,discovery](const AdventureState& before,const AdventureState& after,std::string& error) {
            return trailSites_.discoveryReachable(discovery,before.player,queries_,error)&&validator()(before,after,error);
        };
        if(!state().trail.discoveries[discovery-1].discoveredRevision
            &&!commit(session_->prepareDiscover(stamp(),discovery,check,status_)))return;
        if(commit(session_->prepareClaimDiscovery(stamp(),discovery,check,status_)))
            status_="Landmark explored. Supplies collected. Save to keep your discovery.";
        return;
    }
    if(nearbyRelay()) {
        if(state().trail.relayActivationRevision){status_="The relay is glowing. Follow your compass toward the Watch Arch.";return;}
        const auto check=[this](const AdventureState& before,const AdventureState& after,std::string& error) {
            if(!trailSites_.relayReachable(before.player,queries_,error))return false;
            const auto& home=fieldHome_;
            if(!home.ready()){error=home.message;return false;}
            return validator()(before,after,error);
        };
        if(commit(session_->prepareActivateRelay(stamp(),check,status_))) {
            compassTarget_=CompassTarget::WatchArch;
            status_="Relay repaired. Your compass now points toward the Watch Arch. Save your progress.";
        }
        return;
    }
    for(const auto& node:content_.resourceNodes)if(!std::binary_search(state().depletedNodes.begin(),state().depletedNodes.end(),node.id)
        &&glm::length(glm::dvec3(node.position.x,node.position.y,node.position.z)-player_.feet())<3) {
        if(commit(session_->prepareGather(stamp(),node.id,validator(),status_)))status_="Materials gathered.";
        return;
    }
    status_="Move close to a resident, furniture, supply pile or landmark.";
}
void AdventureRuntime::talk(uint8_t npc) {
    if(!town_.interactable(npc,state().player,queries_,status_))return;
    if(!(state().metNpcMask&(1u<<(npc-1)))
        &&!commit(session_->prepareGreet(stamp(),npc,npcValidator(npc),status_)))return;
    building_=false;dialogueNpc_=npc;menu_=Menu::Dialogue;menuSelection_=0;player_.discardPendingInput();
    status_="Choose a reply. Your progress is kept when you save.";
}
void AdventureRuntime::equipCompass(uint8_t slot) {
    if(commit(session_->prepareEquipUtility(stamp(),slot,status_)))
        status_=slot==255?"Compass returned to your backpack.":"Compass equipped. Choose a destination in your bag.";
}
void AdventureRuntime::craftAtBench(bool compass) {
    if(!reachableComponent(state(),bench_,queries_,status_))return;
    if(compass) {
        if(commit(session_->prepareCraftCompass(stamp(),bench_,validator(),status_)))status_="Trail Compass crafted. Equip it to find your way.";
    } else if(commit(session_->prepareCraftHammer(stamp(),bench_,validator(),status_))) {
        for(uint8_t i=0;i<state().backpack.size();++i)if(state().backpack[i].kind==ItemKind::FieldHammer){
            if(commit(session_->prepareEquipTool(stamp(),i,status_)))status_="Field hammer equipped. Gather twice as much.";
            break;
        }
    }
}
void AdventureRuntime::recover() {
    if(state().health)for(const auto& enemy:encounters_.entries())if(enemy.available
        &&glm::length(glm::dvec3(enemy.pose.x,enemy.pose.y,enemy.pose.z)-player_.feet())<=encounterDefinition(enemy.id)->noticeRadius) {
        status_="Retreat from the raiders before returning home.";return;
    }
    PlayerPose recovery=content_.town;std::string reason;
    if(state().registeredBed)(void)usableBed(state(),state().registeredBed,queries_,recovery,reason);
    if(!encounters_.safeRest(recovery,walkQueries_))recovery=content_.town;
    const auto safe=[this](const AdventureState&,const AdventureState& after,std::string& error) {
        if(!encounters_.safeRest(after.player,walkQueries_)){error="No safe recovery point is available.";return false;}return true;
    };
    auto prepared=session_->prepareRecover(stamp(),recovery,safe,status_);
    if(!prepared)return;
    auto pose=player_.state();pose.feet={recovery.x,recovery.y,recovery.z};pose.facingYaw=recovery.yaw;
    pose.velocity={};pose.mode=AdventurePlayer::Mode::Walking;
    AdventurePlayer checked;
    if(!checked.initialize(queries_,pose.feet,installedWorld().waterHeight,pose.facingYaw)) {
        status_="No safe recovery point is available.";return;
    }
    if(!commit(std::move(prepared)))return;
    (void)player_.restore(pose);combat_.reset();combatSeconds_=0;
    pendingAttack_=false;pendingDodge_=false;pendingJump_=false;discontinuity_=true;
    status_="Recovered safely. Your buildings and items are kept.";menu_=Menu::None;building_=false;
}
std::string_view AdventureRuntime::mode() const {
    switch(menu_) {
    case Menu::None:return building_?"build":"explore";
    case Menu::Main:return "pause";
    case Menu::Catalog:return "catalog";
    case Menu::Chest:return "chest";
    case Menu::Dialogue:return "dialogue";
    case Menu::Workbench:return "workbench";
    case Menu::Journal:return "journal";
    case Menu::Bag:return "bag";
    case Menu::Settings:return "settings";
    case Menu::Controls:return "controls";
    case Menu::CombatBinding:return "combat-binding";
    case Menu::BindingChoice:return "binding-choice";
    case Menu::GuideTopics:case Menu::Guide:return "guide";
    }
    return "explore";
}
bool AdventureRuntime::closeCurrentMode() {
    if(menu_==Menu::Guide||menu_==Menu::GuideTopics) {
        menu_=guideReturnMenu_;menuSelection_=guideReturnSelection_;
        player_.discardPendingInput();combatInput_.reset();inputRouter_.reset();buildRouter_.reset();
        return true;
    }
    if(menu_==Menu::BindingChoice){menu_=Menu::CombatBinding;menuSelection_=0;return true;}
    if(menu_==Menu::CombatBinding){menu_=Menu::Controls;menuSelection_=0;return true;}
    if(menu_==Menu::Controls){menu_=Menu::Settings;menuSelection_=0;return true;}
    if(menu_==Menu::Settings){menu_=Menu::Main;menuSelection_=0;return true;}
    if(menu_!=Menu::None){menu_=Menu::None;menuSelection_=0;player_.discardPendingInput();return true;}
    if(building_){building_=false;player_.discardPendingInput();return true;}
    return false;
}
void AdventureRuntime::menuRow(int row) {
    if(row<0)return;
    // Capture the displayed choice before refreshing/revalidating its context.
    activateMenuIntent(menuIntents_.intent(static_cast<size_t>(row)));
}
void AdventureRuntime::activateMenuIntent(int intent) {
    refreshHud();
    const auto row=menuIntents_.consume(intent);
    if(!row||*row>=menuCommands_.size())return;
    const auto command=menuCommands_[*row];
    switch(command.operation) {
    case MenuOperation::Close:(void)closeCurrentMode();break;
    case MenuOperation::Catalog:building_=true;menu_=Menu::Catalog;menuSelection_=0;break;
    case MenuOperation::Save:saveRequested_=true;saveStatus_="Saving...";break;
    case MenuOperation::Recover:recover();break;
    case MenuOperation::Settings:menu_=Menu::Settings;menuSelection_=0;break;
    case MenuOperation::GuideTopics:openGuide(true,guideTopic_);break;
    case MenuOperation::GuideTopic:
        if(command.argument<static_cast<uint64_t>(AdventureGuideTopic::Count))
            openGuide(false,static_cast<AdventureGuideTopic>(command.argument));
        break;
    case MenuOperation::GuideExit:(void)closeCurrentMode();break;
    case MenuOperation::Controls:menu_=Menu::Controls;menuSelection_=0;break;
    case MenuOperation::ChooseCombat:
        if(command.argument<2){bindingAction_=static_cast<CombatAction>(command.argument);menu_=Menu::CombatBinding;menuSelection_=0;}break;
    case MenuOperation::BindingDevice:
        if(command.argument<3){bindingDevice_=static_cast<uint8_t>(command.argument);menu_=Menu::BindingChoice;menuSelection_=0;}break;
    case MenuOperation::SetBinding: {
        if(command.argument>512)break;
        auto next=preferences_;auto& binding=next.combat[static_cast<size_t>(bindingAction_)];
        const int value=static_cast<int>(command.argument)-1;
        if(bindingDevice_==0)binding.key=value;else if(bindingDevice_==1)binding.mouse=value;else binding.pad=value;
        if(applyPreferences(next)){menu_=Menu::CombatBinding;menuSelection_=0;}break;
    }
    case MenuOperation::ResetPreferences:(void)applyPreferences(AdventurePreferences{},true);break;
    case MenuOperation::RetryPreferences:(void)applyPreferences(preferences_,true);break;
    case MenuOperation::TextScale:case MenuOperation::Contrast:case MenuOperation::Motion:
    case MenuOperation::InvertX:case MenuOperation::InvertY:case MenuOperation::OrbitToggle:
    case MenuOperation::MouseSensitivity:case MenuOperation::PadSensitivity:
    case MenuOperation::MoveDeadzone:case MenuOperation::LookDeadzone: {
        auto next=preferences_;
        const auto speed=[](double value){return value>=3?.25:std::min(3.,(std::floor(value*4)+1)*.25);};
        const auto deadzone=[](double value){return value>=.45?.05:std::min(.45,(std::floor(value*20+1e-8)+1)*.05);};
        switch(command.operation) {
        case MenuOperation::TextScale:next.textScale=command.argument==0?1:command.argument==1?1.25:1.5;break;
        case MenuOperation::Contrast:next.highContrast=!next.highContrast;break;
        case MenuOperation::Motion:next.reducedMotion=!next.reducedMotion;break;
        case MenuOperation::InvertX:next.invertX=!next.invertX;break;
        case MenuOperation::InvertY:next.invertY=!next.invertY;break;
        case MenuOperation::OrbitToggle:next.orbitToggle=!next.orbitToggle;break;
        case MenuOperation::MouseSensitivity:next.mouseSensitivity=speed(next.mouseSensitivity);break;
        case MenuOperation::PadSensitivity:next.padSensitivity=speed(next.padSensitivity);break;
        case MenuOperation::MoveDeadzone:next.moveDeadzone=deadzone(next.moveDeadzone);break;
        case MenuOperation::LookDeadzone:next.lookDeadzone=deadzone(next.lookDeadzone);break;
        default:break;
        }
        (void)applyPreferences(next);break;
    }
    case MenuOperation::Starter:selectBlueprint(BlueprintKind::StarterRoom);break;
    case MenuOperation::BuildRecipe:selectBlueprint(static_cast<BlueprintKind>(command.argument));break;
    case MenuOperation::Journal:openJournal();break;
    case MenuOperation::Bag:menu_=Menu::Bag;menuSelection_=0;break;
    case MenuOperation::EquipUtility:equipCompass(static_cast<uint8_t>(command.argument));break;
    case MenuOperation::CompassTarget:
        if(command.argument<=static_cast<uint64_t>(CompassTarget::WatchArch)) {
            const auto target=static_cast<CompassTarget>(command.argument);
            if(trailCompassReadout(state(),queries_,trailSites_,target).available)compassTarget_=target;
        }
        break;
    case MenuOperation::SelectQuest:journalQuest_=static_cast<uint8_t>(command.argument);menuSelection_=0;break;
    case MenuOperation::SelectPiece:blueprint_=BlueprintKind::None;selected_=static_cast<PieceKind>(command.argument);heightSteps_=0;building_=true;menu_=Menu::None;break;
    case MenuOperation::AcceptQuest:
    case MenuOperation::CompleteQuest:{
        const auto npc=static_cast<uint8_t>(command.argument);const bool complete=command.operation==MenuOperation::CompleteQuest;
        auto prepared=complete?session_->prepareCompleteHomeQuest(stamp(),npc,npcValidator(npc,true),status_)
            :session_->prepareAcceptHomeQuest(stamp(),npc,npcValidator(npc),status_);
        if(commit(std::move(prepared)))status_=complete?"Quest complete. Trail Compass recipe learned. Save to keep this checkpoint.":"Quest accepted: A Place to Return.";
        menuSelection_=0;break;
    }
    case MenuOperation::AcceptTrailQuest:
    case MenuOperation::CompleteTrailQuest:{
        const auto quest=static_cast<uint8_t>(command.argument);const auto npc=dialogueNpc_;
        const bool complete=command.operation==MenuOperation::CompleteTrailQuest;
        auto prepared=complete?session_->prepareCompleteTrailQuest(stamp(),quest,npc,npcValidator(npc),status_)
            :session_->prepareAcceptTrailQuest(stamp(),quest,npc,npcValidator(npc),status_);
        if(commit(std::move(prepared))) {journalQuest_=quest;status_=complete?"Quest complete. Save to keep your progress.":"Quest accepted. Your journal shows the next step.";}
        menuSelection_=0;break;
    }
    case MenuOperation::CraftHammer:craftAtBench(false);break;
    case MenuOperation::CraftCompass:craftAtBench(true);break;
    case MenuOperation::CraftStaff:
        if(commit(session_->prepareCraftStaff(stamp(),bench_,validator(),status_))) {
            for(uint8_t i=0;i<state().backpack.size();++i)if(state().backpack[i].kind==ItemKind::TrailStaff) {
                if(commit(session_->prepareEquipTool(stamp(),i,status_)))status_="Trail staff equipped. Attack and Dodge are ready.";
                break;
            }
        }
        break;
    case MenuOperation::Transfer:if(commit(session_->prepareTransfer(stamp(),command.transfer,validator(),status_)))status_="Items moved safely.";break;
    case MenuOperation::EquipTool:if(commit(session_->prepareEquipTool(stamp(),static_cast<uint8_t>(command.argument),status_)))status_="Tool equipped.";break;
    }
    refreshHud();
}
void AdventureRuntime::update(double seconds,Input& input,Camera& camera,uint32_t width,uint32_t height,glm::dvec2 logicalPointerExtent) {
    using A=expedition::CoveAction;using C=expedition::CoveInputContext;
    menuIntents_.beginFrame();
    const double residentSeconds=std::isfinite(seconds)?std::clamp(seconds,0.,.25):0.;
    framePointer_=adventurePointer(glm::dvec2(input.mousePosition()),logicalPointerExtent,{width,height});
    bool nativeHudVisible=true;
#if !defined(VOXY_NATIVE)
    nativeHudVisible=!domUiAttached_;
#endif
    hudPointerOwned_=false;bool hudClicked=false;
    if(nativeHudVisible&&framePointer_) {
        const glm::vec2 pointer(framePointer_->framebuffer);
        const auto inside=[&](glm::vec4 bounds){return pointer.x>=bounds.x&&pointer.y>=bounds.y&&pointer.x<bounds.x+bounds.z&&pointer.y<bounds.y+bounds.w;};
        for(const auto panel:hud_.layout().panels)hudPointerOwned_=hudPointerOwned_||inside(panel);
        if(input.wasMouseButtonPressed(MouseButton::Left))for(const auto& hit:hud_.layout().hits) {
            if(!inside(hit.bounds))continue;
            hudClicked=true;
            if(hit.enabled&&hit.intent&&pendingActions_.size()<32)
                pendingActions_.push_back(observedAction(hit.action,hit.action==10?static_cast<int>(hit.intent):hit.value,(hit.intent-1u)/64u));
            break;
        }
    }
    if(uiOwnershipChanged_){
        uiInputOwned_=pendingUiInputOwned_;uiOwnershipChanged_=false;
        const bool focused=input.focused();input.onFocusChanged(false);input.onFocusChanged(focused);
        player_.discardPendingInput();
    }
    ++observationSerial_;
    (void)input.setGamepadDeadzones(static_cast<float>(preferences_.moveDeadzone),static_cast<float>(preferences_.lookDeadzone));
    const auto sample=expedition::sampleCoveInput(input);
    inputRouter_.tick(adventureMovementSample(sample,building_),menu_==Menu::None&&!uiInputOwned_?C::World:C::Menu,routingPreferences_);
    buildRouter_.tick(sample,menu_==Menu::None&&building_&&!uiInputOwned_?C::Workshop:C::Menu,routingPreferences_);
    const auto& pad=input.gamepad();
    auto pressed=[&](Key k){return input.wasKeyPressed(k);};
    bool dismissedMenu=false;
    if(menu_==Menu::Guide||menu_==Menu::GuideTopics) {
        if(sample.padConnected&&std::any_of(sample.padPressed.begin(),sample.padPressed.end(),[](bool p){return p;}))guideGamepad_=true;
        else if(std::any_of(sample.pressed.begin(),sample.pressed.end(),[](bool p){return p;})
            ||input.wasMouseButtonPressed(MouseButton::Left)||input.wasMouseButtonPressed(MouseButton::Right))guideGamepad_=false;
    }
    if(pressed(Key::Escape)||pressed(static_cast<Key>(291))||pad.pressed(PadButton::Menu)) {
        const bool wasInMenu=menu_!=Menu::None;
        if(freeBuild_&&menu_==Menu::None)menu_=Menu::Main;
        else if(!closeCurrentMode())menu_=Menu::Main;
        dismissedMenu=wasInMenu&&menu_==Menu::None;
        menuSelection_=0;input.releaseMouse();player_.discardPendingInput();
    } else if(inputRouter_.pressed(A::Workshop)) {building_=!building_;heightSteps_=0;input.releaseMouse();}
    if(inputRouter_.pressed(A::Save)||pressed(static_cast<Key>(294))) {saveRequested_=true;saveStatus_="Saving...";}
    combatInput_.tick(sample,menu_==Menu::None&&!building_&&!uiInputOwned_&&!hudPointerOwned_&&!hudClicked,preferences_,
        {input.wasMouseButtonPressed(MouseButton::Left),input.wasMouseButtonPressed(MouseButton::Right),input.wasMouseButtonPressed(MouseButton::Middle)});
    // Closing a modal must not also consume raw placement, shoulder or height
    // edges from that frame. Routed actions already require release to rearm.
    if(menu_!=Menu::None||uiInputOwned_||dismissedMenu) {
        player_.discardPendingInput();combatSeconds_=0;pendingAttack_=false;pendingDodge_=false;pendingJump_=false;
        if((nativeHudVisible&&pressed(Key::Up))||pad.navigation(0))menuSelection_=std::max(0,menuSelection_-1);
        if((nativeHudVisible&&pressed(Key::Down))||pad.navigation(1))++menuSelection_;
        if(nativeHudVisible&&(menu_==Menu::Guide||menu_==Menu::GuideTopics)&&std::isfinite(input.scrollDelta())&&input.scrollDelta()!=0) {
            menuSelection_=std::max(0,menuSelection_+(input.scrollDelta()<0?1:-1));guideGamepad_=false;
        }
        if((nativeHudVisible&&pressed(Key::Enter))||pad.pressed(PadButton::Confirm))menuRow(menuSelection_);
        if(pad.pressed(PadButton::Back)){(void)closeCurrentMode();input.resetState();}
    } else {
        const auto movement=inputRouter_.movement();const double yaw=orbit_.pose().valid?orbit_.pose().yaw:player_.facingYaw();
        const bool attack=combatInput_.pressed(CombatAction::Attack);
        const bool dodge=combatInput_.pressed(CombatAction::Dodge);
        advanceCombat(seconds,{{adventureCameraRelativeMovement(movement,yaw),inputRouter_.pressed(A::Jump)},attack,dodge});
        const auto p=player_.feet();
        expedition::CoveCamera::Input orbitInput;
        const auto look=inputRouter_.look();
        const auto mouse=inputRouter_.mouseGesturesAllowed()
            ?(preferences_.orbitToggle?(inputRouter_.orbitDrag()?input.mouseDelta():glm::vec2(0)):input.mouseDragDelta(MouseButton::Right))
            :glm::vec2(0);
        orbitInput.orbitRadians={double(mouse.x)*.004*preferences_.mouseSensitivity*(preferences_.invertX?-1:1)+look[0]*seconds*2,
            double(mouse.y)*.004*preferences_.mouseSensitivity*(preferences_.invertY?-1:1)+look[1]*seconds*2};
        const bool brickWheel=freeBuild_&&building_&&!input.isKeyDown(Key::LeftControl)&&!input.isKeyDown(Key::RightControl);
        if(brickWheel&&std::isfinite(input.scrollDelta())) {
            // One detent selects one item; accumulate smooth trackpad deltas.
            // The order matches the pictured creative hotbar, starting with bricks.
            constexpr std::array<uint8_t,15> hotbar{8,9,10,2,3,5,4,15,6,7,1,14,11,12,13};
            brickScroll_+=input.scrollDelta();
            if(std::abs(brickScroll_)>=.5f) {
                const auto found=std::find(hotbar.begin(),hotbar.end(),uint8_t(selected_));
                const auto index=static_cast<size_t>(found-hotbar.begin());
                selected_=PieceKind(hotbar[(index+(brickScroll_<0?1:hotbar.size()-1))%hotbar.size()]);
                blueprint_=BlueprintKind::None;heightSteps_=0;brickScroll_=0;
            }
        } else brickScroll_=0;
        orbitInput.zoomSteps=brickWheel?0:input.scrollDelta();orbitInput.active=input.focused();
        orbitInput.recenter=inputRouter_.pressed(A::Recenter);
        camera.setAspectRatio(width,height);
        const expedition::CoveCamera::Target target{p+glm::dvec3(0,1.2,0),player_.facingYaw(),queries_.revision(),{},discontinuity_,glm::length(player_.worldVelocity())>.1};
        (void)orbit_.update(target,orbitInput,{camera.fovY(),camera.aspectRatio(),camera.nearPlane()},queries_.cameraSweep(),seconds);
        if(orbit_.pose().valid) {
            const auto pose=orbit_.pose();const auto cameraPosition=physics::worldPositionFromAbsolute(pose.eye);
            const auto origin=glm::dvec3(cameraPosition.sector)*double(physics::kWorldSectorSize);
            camera.setWorldPosition(cameraPosition.sector,cameraPosition.local);camera.lookAt(glm::vec3(pose.viewTarget-origin));discontinuity_=false;
        }
        character_.update(seconds,player_.mode(),glm::length(glm::dvec2(player_.worldVelocity().x,player_.worldVelocity().z)),player_.worldVelocity().y,state().combat.player.attackImpactTick!=0);
        updateTarget(camera,input,width,height);
        if(building_) {
            if(pressed(Key::Tab)){menu_=Menu::Catalog;menuSelection_=int(selected_)-1;}
            if(buildRouter_.pressed(A::RotateY))yaw_=uint8_t((yaw_+1)%4);
            if(buildRouter_.pressed(A::PreviousPart)) {
                selected_=PieceKind((int(selected_)+int(kBuildingPieceCount)-2)%int(kBuildingPieceCount)+1);blueprint_=BlueprintKind::None;heightSteps_=0;
            }
            if(pad.pressed(PadButton::RightShoulder)) {
                selected_=PieceKind(int(selected_)%int(kBuildingPieceCount)+1);blueprint_=BlueprintKind::None;heightSteps_=0;
            }
            if(pressed(static_cast<Key>(266))||pad.pressed(PadButton::Up))++heightSteps_;
            if(pressed(static_cast<Key>(267))||pad.pressed(PadButton::Down))--heightSteps_;
            if(buildRouter_.pressed(A::Keep)||(input.wasMouseButtonPressed(MouseButton::Left)&&!hudClicked&&!hudPointerOwned_))action(4);
            if(buildRouter_.pressed(A::Remove)||pad.pressed(PadButton::Back))action(5);
            if(buildRouter_.pressed(A::Undo))action(6);
        } else if(inputRouter_.pressed(A::Interact))use();
    }
    const auto commands=std::exchange(pendingActions_,{});
    if(!commands.empty())refreshHud();
    for(const auto& pending:commands) {
        const auto command=pending.action,value=pending.value;
        // A queued control belongs to the menu/mode that displayed it. Save is
        // global; every other control must still have that published context.
        if(command!=8&&pending.menuToken!=menuIntents_.token())continue;
        switch(command) {
        case 1:building_=!building_;menu_=Menu::None;break;
        case 2:if(value>0&&pieceKindValid(uint32_t(value))){blueprint_=BlueprintKind::None;selected_=PieceKind(value);building_=true;menu_=Menu::None;heightSteps_=0;}break;
        case 3:yaw_=uint8_t((yaw_+1)%4);break;
        case 4:if(building_&&menu_==Menu::None) {
            updateTarget(camera,input,width,height);
            auto candidate=previewValid_?(blueprint_!=BlueprintKind::None?session_->prepareBuildRecipe(stamp(),blueprint_,preview_.position,yaw_,validator(),status_)
                :session_->preparePlace(stamp(),preview_,validator(),status_)):std::nullopt;
            const auto structure=candidate?candidate->changedStructure():0;
            if(commit(std::move(candidate))) {
                lastBlueprint_=blueprint_!=BlueprintKind::None?structure:0;
                status_=blueprint_==BlueprintKind::StarterRoom?"Room built. Step through the doorway and use its bed."
                    :blueprint_==BlueprintKind::WideStoneStep?"Wide stone step built. Finish building to walk across it.":(freeBuild_?"Placed.":"Placed. Remove returns its materials.");
                if(blueprint_==BlueprintKind::StarterRoom)building_=false;
            } else if(!previewValid_)status_=previewReason_;
        }break;
        case 5:
            if(isVillagePartId(targetPart_))status_="Village scenery belongs to the town. Build beside it.";
            else if(isTrailPartId(targetPart_))status_="This landmark belongs to the trail. Build beside it.";
            else if(targetPart_&&commit(session_->prepareRemove(stamp(),targetPart_,validator(),status_)))status_=freeBuild_?"Removed.":"Removed. Materials returned.";
            break;
        case 6:if(lastBlueprint_?commit(session_->prepareRemoveStructure(stamp(),lastBlueprint_,validator(),status_))
            :lastPlaced_&&commit(session_->prepareRemove(stamp(),lastPlaced_,validator(),status_))) {
            lastPlaced_=0;lastBlueprint_=0;status_=freeBuild_?"Last placement undone.":"Last placement undone. Materials returned.";
        }break;
        case 7:if(!building_&&menu_==Menu::None) {
            if(pending.door)useDoor(pending.door,pending.doorOpen,pending.doorRevision);
            else use(false);
        }break;
        case 8:saveRequested_=true;saveStatus_="Saving...";break;
        case 9:if(!closeCurrentMode())menu_=Menu::Main;menuSelection_=0;break;
        case 10:activateMenuIntent(value);break;
        case 11:recover();break;
        case 12:heightSteps_=std::clamp(heightSteps_+std::clamp(value,-1,1),-32,32);break;
        case 13:if(value>=1&&value<=3)catalogCategory_=static_cast<uint8_t>(value-1);building_=true;menu_=Menu::Catalog;menuSelection_=0;break;
        case 14:selectBlueprint(BlueprintKind::StarterRoom);break;
        case 16:openJournal();break;
        case 17:if(((value>=0&&value<int(kBackpackSlots))||value==255)&&menuIntents_.claimActivation())equipCompass(uint8_t(value));break;
        case 18:cycleCompass();break;
        case 20:(void)closeCurrentMode();break;
        case 21:building_=true;menu_=Menu::None;player_.discardPendingInput();break;
        case 22:building_=false;menu_=Menu::None;player_.discardPendingInput();break;
        case 23:building_=true;menu_=menu_==Menu::Catalog?Menu::None:Menu::Catalog;menuSelection_=0;player_.discardPendingInput();break;
        case 24:menu_=Menu::Bag;menuSelection_=0;player_.discardPendingInput();break;
        case 25:menuSelection_=std::clamp(menuSelection_+std::clamp(value,-1,1),0,std::max(0,int(menuCommands_.size())-1));break;
        case 27:if(!building_&&menu_==Menu::None)pendingAttack_=true;break;
        case 28:if(!building_&&menu_==Menu::None)pendingDodge_=true;break;
        case 30:
            if(freeBuild_&&building_&&menu_==Menu::None&&value>=0&&value<=0xffffff)selectedPaint_=uint32_t(value);
            break;
        case 31:
            if(freeBuild_&&menu_==Menu::None){menu_=Menu::Main;menuSelection_=0;player_.discardPendingInput();}
            break;
        case 29:
            if((menu_==Menu::Main||menu_==Menu::None)&&(value==0||value==1))
                openGuide(value==0,value==1?AdventureGuideTopic::Building:AdventureGuideTopic::Movement);
            break;
        }
        refreshHud();
    }
    if(!commands.empty()&&menu_==Menu::None)updateTarget(camera,input,width,height);
    if(menu_==Menu::None||menu_==Menu::Dialogue)for(size_t i=0;i<town_.entries().size();++i) {
        const auto& resident=town_.entries()[i];if(!resident.available)continue;
        const auto delta=player_.feet()-resident.feet;
        const double target=glm::length(delta)<5?std::atan2(-delta.x,-delta.z):resident.yaw;
        const double angle=std::remainder(target-residentFacing_[i],2*std::numbers::pi);
        residentFacing_[i]+=std::clamp(angle,-residentSeconds*1.8,residentSeconds*1.8);
        residentCharacters_[i].update(residentSeconds,expedition::CovePlayer::Mode::Walking,0,0,
            menu_==Menu::Dialogue&&dialogueNpc_==resident.id);
    }
    // Only a deferred newly installed actor needs a retry. Existing residents
    // keep their admitted pose. Publishing this derived packet never edits a save.
    residentRetrySeconds_+=residentSeconds;
    if(!freeBuild_&&menu_==Menu::None&&residentRetrySeconds_>=.25&&glm::length(player_.feet()-residentRetryPlayer_)>.25
        &&std::any_of(town_.entries().begin(),town_.entries().end(),[](const auto& resident){return !resident.available;})) {
        residentRetrySeconds_=0;residentRetryPlayer_=player_.feet();
        AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;std::string error;
        if(prepareGeometry(state(),next,residents,village,sites,error)) {
            AdventureSpatialQueries actors;AdventureEncounters encounters;
            if(prepareActors(state(),next,actors,encounters,error)) {
                walkQueries_=std::move(next);queries_=std::move(actors);encounters_=std::move(encounters);
                town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
            }
        }
    }
#if defined(VOXY_NATIVE)
    if(saves_) {
        if(auto completion=saves_->poll())saveCompleted(completion->saved?(freeBuild_?"Saved build":"Saved adventure"):completion->message);
        if(!saves_->busy()&&consumeSaveRequest()) {
            std::vector<std::byte> bytes;std::string error;
            if(!snapshot(bytes,error)||!saves_->requestSave(std::move(bytes),error))saveCompleted(error);
            else saveStatus_="Saving...";
        }
    }
#else
    // The host page is outside Closure's compilation unit. Preserve its public
    // property name instead of allowing the optimized bundle to rename it.
    if(consumeSaveRequest())EM_ASM({if(typeof window['voxyAdventureSaveRequested']==='function')window['voxyAdventureSaveRequested']();});
#endif
    observedEye_=orbit_.pose().eye;observedTarget_=orbit_.pose().viewTarget;
    observedOrigin_=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    observedViewProjection_=glm::dmat4(camera.projectionMatrix()*camera.viewMatrix());observedWidth_=width;observedHeight_=height;
    refreshHud();
#if defined(VOXY_NATIVE)
    observationSeconds_+=seconds;
    if(const char* path=std::getenv("VOXY_ADVENTURE_OBSERVE");path&&*path&&observationSeconds_>=.25) {
        observationSeconds_=0;const auto temporary=std::string(path)+".tmp";
        {std::ofstream stream(temporary);stream<<json();}
        std::error_code ignored;std::filesystem::rename(temporary,path,ignored);
    }
#endif
}
void AdventureRuntime::refreshHud() {
    render::AdventureHudContent hud;
    using H=render::AdventureHudMode;using O=MenuOperation;
    switch(menu_) {
    case Menu::None:hud.mode=building_?H::Build:H::Explore;break;
    case Menu::Main:hud.mode=H::Pause;break;
    case Menu::Catalog:hud.mode=H::Catalog;break;
    case Menu::Chest:hud.mode=H::Chest;break;
    case Menu::Dialogue:hud.mode=H::Dialogue;break;
    case Menu::Workbench:hud.mode=H::Workbench;break;
    case Menu::Journal:hud.mode=H::Journal;break;
    case Menu::Bag:hud.mode=H::Bag;break;
    case Menu::Settings:case Menu::Controls:case Menu::CombatBinding:case Menu::BindingChoice:hud.mode=H::Pause;break;
    case Menu::GuideTopics:case Menu::Guide:hud.mode=H::Guide;break;
    }
    hud.title="Health "+std::to_string(state().health)+" / 100";
    const auto* blueprint=buildingBlueprintDefinition(blueprint_);
    hud.selected=std::string(blueprint?blueprint->name:buildingDefinition(selected_)->name);
    hud.status=building_?previewReason_:status_;
    hud.context="Health "+std::to_string(state().health)+" / 100. "+(saveFailure_.empty()?combatLabel():saveFailure_);hud.pieceKind=blueprint_==BlueprintKind::StarterRoom?0:static_cast<uint8_t>(selected_);
    hud.paletteCategory=static_cast<render::AdventurePaletteCategory>(catalogCategory_);hud.pickerOpen=menu_==Menu::Catalog;
    const auto cost=blueprint?blueprint->cost:buildingDefinition(selected_)->cost;
    hud.cost=freeBuild_?"Unlimited pieces":costLabel(cost);
    const auto readiness=firstHomeReadiness(state(),queries_);
    const auto compass=trailCompassReadout(state(),queries_,trailSites_,compassTarget_);
    const auto fieldHome=fieldHome_;
    hud.objective=freeBuild_?"":trailObjective(state(),readiness,fieldHome);
    if(freeBuild_){hud.title="Free build";hud.context=saveFailure_.empty()?"B: Build / walk":saveFailure_;}
    if(!freeBuild_&&compass.available)hud.compass=compass.label+"  "+compass.direction+"  "+std::to_string(int(std::round(compass.distance)))+" m";
    hud.tone=building_&&!previewValid_?render::CoveHudTone::Blocked:render::CoveHudTone::Ready;
    hud.textScale=float(preferences_.textScale);hud.highContrast=preferences_.highContrast;
    menuCommands_.clear();menuTitle_.clear();menuText_.clear();menuStatus_.clear();
    std::vector<MenuIntentChoice> identities;
    const auto add=[&](std::string label,bool enabled,MenuCommand command,uint8_t piece=0) {
        const auto& transfer=command.transfer;
        std::string identity=std::to_string(static_cast<int>(command.operation))+":"+std::to_string(command.argument)+":"+label
            +":"+std::to_string(transfer.source)+":"+std::to_string(transfer.destination)
            +":"+std::to_string(transfer.sourceRevision)+":"+std::to_string(transfer.destinationRevision)
            +":"+std::to_string(transfer.sourceSlot)+":"+std::to_string(transfer.quantity);
        identities.push_back({std::move(identity),enabled});menuCommands_.push_back(command);
        hud.rows.push_back({std::move(label),"",enabled,10,0,0,piece});
    };
    const auto equipRow=[&] {
        if(compass.equipped) {
            auto backpack=state().backpack;
            add("Put compass in bag",addItems(backpack,state().equippedUtility),{O::EquipUtility,255});
        } else add("Equip trail compass",compass.backpackSlot.has_value(),{O::EquipUtility,compass.backpackSlot.value_or(255)});
    };
    if(menu_==Menu::Main) {
        menuTitle_="Paused";menuText_=saveStatus_;menuStatus_=status_;
        add(freeBuild_?"Return to building":"Return to adventure",true,{O::Close});add("Building pieces",true,{O::Catalog});
        add(freeBuild_?"Save build":"Save adventure",true,{O::Save});add(freeBuild_?"Return to start":"Return to home / town",true,{O::Recover});
        add("Comfort and controls",true,{O::Settings});
        add("How to play",true,{O::GuideTopics});
        if(!freeBuild_){add("Starter room",true,{O::Starter});add("Quest journal",true,{O::Journal});add("Open bag",true,{O::Bag});}
    } else if(freeBuild_&&(menu_==Menu::GuideTopics||menu_==Menu::Guide)) {
        menuTitle_="Build at your own pace";
        menuText_="Choose a piece, aim and place. R rotates; Ctrl+Z undoes the last placement. ";
        menuText_+=preferences_.orbitToggle?"Right-click to start or stop looking. ":"Right-drag to look. ";
        menuText_+="Scroll chooses a piece; Ctrl+scroll zooms while building. B switches between building and walking. Save from Menu.";
        add(guideReturnMenu_==Menu::Main?"Back to menu":"Return to building",true,{O::GuideExit});
    } else if(menu_==Menu::GuideTopics||menu_==Menu::Guide) {
        const auto exitLabel=guideReturnMenu_==Menu::Main?"Back to menu":building_?"Return to building":"Return to adventure";
        if(menu_==Menu::GuideTopics) {
            menuTitle_="How to play";menuText_="Choose a short tip. Read at your own pace.";
            for(uint64_t i=0;i<static_cast<uint64_t>(AdventureGuideTopic::Count);++i) {
                const auto card=adventureGuideCard(static_cast<AdventureGuideTopic>(i),preferences_,guideGamepad_);
                add(card.title,true,{O::GuideTopic,i});
            }
        } else {
            const auto card=adventureGuideCard(guideTopic_,preferences_,guideGamepad_);
            menuTitle_=card.title;menuText_=card.text;
            const auto topic=static_cast<uint64_t>(guideTopic_);
            if(topic+1<static_cast<uint64_t>(AdventureGuideTopic::Count))add("Next tip",true,{O::GuideTopic,topic+1});
            if(topic>0)add("Previous tip",true,{O::GuideTopic,topic-1});
            add("All topics",true,{O::GuideTopics});
        }
        add(exitLabel,true,{O::GuideExit});
    } else if(menu_==Menu::Settings) {
        menuTitle_="Comfort and controls";menuText_="Settings stay on this device. Your world save is separate.";menuStatus_=preferencesStatus_;
        const uint64_t nextScale=preferences_.textScale==1?1:preferences_.textScale==1.25?2:0;
        add("Text size: "+std::to_string(int(preferences_.textScale*100))+"%",true,{O::TextScale,nextScale});
        const auto toggle=[&](const char* label,bool enabled,O operation){add(std::string(label)+(enabled?": on":": off"),true,{operation});};
        toggle("High contrast",preferences_.highContrast,O::Contrast);toggle("Reduced motion",preferences_.reducedMotion,O::Motion);
        toggle("Invert horizontal look",preferences_.invertX,O::InvertX);toggle("Invert vertical look",preferences_.invertY,O::InvertY);
        add(preferences_.orbitToggle?"Mouse look: click to toggle":"Mouse look: hold right button",true,{O::OrbitToggle});
        const auto percentage=[](double value){return std::to_string(int(std::round(value*100)))+"%";};
        add("Mouse look speed: "+percentage(preferences_.mouseSensitivity),true,{O::MouseSensitivity});
        add("Controller look speed: "+percentage(preferences_.padSensitivity),true,{O::PadSensitivity});
        add("Movement deadzone: "+percentage(preferences_.moveDeadzone),true,{O::MoveDeadzone});
        add("Look deadzone: "+percentage(preferences_.lookDeadzone),true,{O::LookDeadzone});
        if(!freeBuild_)add("Attack and dodge controls",true,{O::Controls});
        add(freeBuild_?"Reset comfort settings":"Reset comfort and combat controls",true,{O::ResetPreferences});
        add("Save settings again",true,{O::RetryPreferences});add("Back",true,{O::Close});
    } else if(menu_==Menu::Controls) {
        menuTitle_="Attack and dodge controls";menuText_="Choose an action. Movement, building and menu controls keep their current bindings.";menuStatus_=preferencesStatus_;
        for(uint64_t id=0;id<2;++id) {
            const auto action=static_cast<CombatAction>(id);add(id==0?"Attack":"Dodge",true,{O::ChooseCombat,id});
            hud.rows.back().detail=combatBindingLabel(preferences_,action,false)+" / "+combatBindingLabel(preferences_,action,true);
        }
        add("Back",true,{O::Close});
    } else if(menu_==Menu::CombatBinding) {
        menuTitle_=bindingAction_==CombatAction::Attack?"Attack controls":"Dodge controls";
        menuText_="Choose a key or button. Conflicting choices are marked. On-screen actions stay available.";menuStatus_=preferencesStatus_;
        const auto& binding=preferences_.combat[static_cast<size_t>(bindingAction_)];
        add("Keyboard: "+expedition::coveKeyLabel(binding.key),true,{O::BindingDevice,0});
        add(binding.mouse==0?"Mouse: left button":binding.mouse==2?"Mouse: middle button":"Mouse: unbound",true,{O::BindingDevice,1});
        add("Controller: "+expedition::covePadLabel(binding.pad),true,{O::BindingDevice,2});add("Back",true,{O::Close});
    } else if(menu_==Menu::BindingChoice) {
        menuTitle_=bindingDevice_==0?"Choose a keyboard key":bindingDevice_==1?"Choose a mouse button":"Choose a controller button";
        menuText_=bindingAction_==CombatAction::Attack?"Change Attack":"Change Dodge";menuStatus_=preferencesStatus_;
        const auto choices=bindingDevice_==0?combatKeyChoices():bindingDevice_==1?combatMouseChoices():combatPadChoices();
        for(int value:choices) {
            auto candidate=preferences_;auto& binding=candidate.combat[static_cast<size_t>(bindingAction_)];
            if(bindingDevice_==0)binding.key=value;else if(bindingDevice_==1)binding.mouse=value;else binding.pad=value;
            std::string reason;const bool valid=validateAdventurePreferences(candidate,reason);
            const auto label=bindingDevice_==0?expedition::coveKeyLabel(value):bindingDevice_==2?expedition::covePadLabel(value)
                :value==0?std::string("Left mouse"):value==2?std::string("Middle mouse"):std::string("Unbound");
            add(label,valid,{O::SetBinding,static_cast<uint64_t>(value+1)});hud.rows.back().detail=valid?(candidate==preferences_?"Current binding":""):reason;
        }
        add("Back",true,{O::Close});
    } else if(menu_==Menu::Catalog) {
        menuTitle_="Building pieces";menuText_="Choose a piece, then aim at a suitable place.";
        // The first home blueprint must be discoverable without scrolling
        // through individual pieces or leaving Build for the pause menu.
        if(!freeBuild_){add("Starter room",true,{O::Starter});hud.rows.back().detail=costLabel(buildingBlueprintDefinition(BlueprintKind::StarterRoom)->cost);}
        if(!freeBuild_&&catalogCategory_==0) {
            const auto& recipe=*buildingBlueprintDefinition(BlueprintKind::WideStoneStep);
            const bool unlocked=wideStoneStepRecipeUnlocked(state());
            add(std::string(recipe.name),unlocked,{O::BuildRecipe,static_cast<uint64_t>(recipe.kind)},static_cast<uint8_t>(recipe.anchorPiece));
            hud.rows.back().detail=unlocked?"3 Piers / "+costLabel(recipe.cost):"Help Moss: The Surveyor's Notes";
        }
        for(const auto& definition:buildingCatalog()) {
            const auto kind=definition.kind;
            const uint8_t category=kind==PieceKind::Bed||kind==PieceKind::Chest||kind==PieceKind::Workbench?2
                :kind==PieceKind::Brick1x2||kind==PieceKind::Brick2x2||kind==PieceKind::Brick2x4?1:0;
            if(category!=catalogCategory_)continue;
            add(std::string(definition.name),true,{O::SelectPiece,static_cast<uint64_t>(kind)},static_cast<uint8_t>(kind));
            hud.rows.back().detail=freeBuild_?"Unlimited":costLabel(definition.cost);
        }
    } else if(menu_==Menu::Dialogue) {
        const auto* resident=residentDefinition(dialogueNpc_);const auto dialogue=homeDialogue(dialogueNpc_,state(),readiness);
        menuTitle_=resident?std::string(resident->name)+" / "+std::string(resident->role):"Resident";
        const auto trail=trailDialogue(dialogueNpc_,state(),fieldHome);
        menuText_=trail?trail->text:dialogue.text;std::string reason;
        const bool reachable=town_.interactable(dialogueNpc_,state().player,queries_,reason);
        const auto operation=dialogue.choice==HomeDialogueChoice::Accept?O::AcceptQuest:dialogue.choice==HomeDialogueChoice::Complete?O::CompleteQuest:O::Close;
        if(trail) {
            const auto trailOperation=trail->choice==TrailDialogueChoice::Accept?O::AcceptTrailQuest
                :trail->choice==TrailDialogueChoice::Complete?O::CompleteTrailQuest:O::Close;
            add(trail->choiceLabel,trailOperation==O::Close||reachable,{trailOperation,trail->questId});
        } else add(dialogue.choiceLabel,operation==O::Close||reachable,{operation,dialogueNpc_});
        add("Leave conversation",true,{O::Close});
        if(!reachable)menuStatus_=reason;
    } else if(menu_==Menu::Workbench) {
        menuTitle_="Workbench";menuText_="Choose what to craft from your supplies.";menuStatus_=status_;
        std::string reason;const bool reachable=reachableComponent(state(),bench_,queries_,reason);
        const auto canCraft=[&](MaterialCost ingredients,ItemKind output) {
            auto backpack=state().backpack;return reachable&&consumeMaterials(backpack,ingredients)&&addItems(backpack,{output,1});
        };
        add("Craft trail staff",canCraft({4,0,4},ItemKind::TrailStaff),{O::CraftStaff,bench_});hud.rows.back().detail="4 wood / 4 scrap";
        add("Craft field hammer",canCraft({4,0,2},ItemKind::FieldHammer),{O::CraftHammer,bench_});hud.rows.back().detail="4 wood / 2 scrap";
        const bool unlocked=trailCompassRecipeUnlocked(state().firstHome);
        add(unlocked?"Craft trail compass":"Trail compass: help Moss first",unlocked&&canCraft({2,0,4},ItemKind::TrailCompass),{O::CraftCompass,bench_});
        hud.rows.back().detail="2 wood / 4 scrap";equipRow();add("Close workbench",true,{O::Close});
        if(!reachable)menuStatus_=reason;
    } else if(menu_==Menu::Journal) {
        menuTitle_="Your journal";menuText_=homeObjective(state(),readiness);
        menuStatus_=state().firstHome.phase==QuestPhase::Completed?"Completed / Trail Compass recipe learned"
            :state().firstHome.phase==QuestPhase::Active?"Active / Different home layouts count":"Talk to Moss in the square";
        add("A Place to Return",true,{O::SelectQuest,1});
        if(const auto selected=trailQuestReadout(journalQuest_,state(),fieldHome)) {
            menuTitle_=selected->title;menuText_=selected->objective;menuStatus_=selected->status;
            if(selected->waypoint) {
                const auto direction=trailCompassReadout(state(),queries_,trailSites_,*selected->waypoint);
                add("Track "+direction.label,direction.available,{O::CompassTarget,static_cast<uint64_t>(*selected->waypoint)});
            }
        }
        for(const auto& quest:trailJournal(state(),fieldHome))if(quest.unlocked) {
            add(quest.title,true,{O::SelectQuest,quest.questId});hud.rows.back().detail=quest.status;
        }
        if(const auto side=trailSideQuest(state())) {
            add(side->title,true,{O::SelectQuest,side->questId});hud.rows.back().detail="Optional / "+side->status;
        }
        for(const auto& discovery:trailDiscoveries(state(),content_.discoveries,trailSites_)) {
            const auto direction=trailCompassReadout(state(),queries_,trailSites_,discovery.waypoint);
            if(!direction.revealed)continue;
            add(discovery.title,direction.available,{O::CompassTarget,static_cast<uint64_t>(discovery.waypoint)});
            hud.rows.back().detail=discovery.objective+(discovery.reward.empty()?"":" / "+discovery.reward);
        }
        add("Return to adventure",true,{O::Close});
    } else if(menu_==Menu::Chest) {
        menuTitle_="Chest";menuText_="Move up to 10 items at a time.";menuStatus_=status_;
        if(const auto* chest=AdventureSession::findComponent(state(),chest_)) {
            std::string reason;const bool reachable=reachableComponent(state(),chest_,queries_,reason);
            for(uint8_t i=0;i<state().backpack.size();++i)if(const auto stack=state().backpack[i];stack.quantity) {
                const auto quantity=static_cast<uint16_t>(std::min<int>(10,stack.quantity));auto destination=chest->slots;
                add("Store "+std::string(itemDefinition(stack.kind)->name)+" ("+std::to_string(stack.quantity)+")",
                    reachable&&addItems(destination,{stack.kind,quantity}),{O::Transfer,0,{0,chest_,state().backpackRevision,chest->revision,i,quantity}});
            }
            for(uint8_t i=0;i<chest->slots.size();++i)if(const auto stack=chest->slots[i];stack.quantity) {
                const auto quantity=static_cast<uint16_t>(std::min<int>(10,stack.quantity));auto destination=state().backpack;
                add("Take "+std::string(itemDefinition(stack.kind)->name)+" ("+std::to_string(stack.quantity)+")",
                    reachable&&addItems(destination,{stack.kind,quantity}),{O::Transfer,0,{chest_,0,chest->revision,state().backpackRevision,i,quantity}});
            }
            if(!reachable)menuStatus_=reason;
        }
        add("Close chest",true,{O::Close});
    } else if(menu_==Menu::Bag) {
        menuTitle_="Your bag";menuText_="Supplies stay with you. Equip tools for their abilities.";menuStatus_=status_;
        if(state().equippedTool.kind!=ItemKind::None)add("Equipped: "+std::string(itemDefinition(state().equippedTool.kind)->name),false,{O::Close});
        if(compass.equipped)equipRow();
        for(uint8_t i=0;i<state().backpack.size();++i)if(const auto stack=state().backpack[i];stack.quantity) {
            const bool tool=stack.kind==ItemKind::FieldHammer||stack.kind==ItemKind::TrailStaff,utility=stack.kind==ItemKind::TrailCompass;
            add(std::string(tool||utility?"Equip ":"")+std::string(itemDefinition(stack.kind)->name)+" ("+std::to_string(stack.quantity)+")",
                tool||utility,{utility?O::EquipUtility:O::EquipTool,i});
        }
        if(compass.equipped)for(uint8_t value=0;value<=static_cast<uint8_t>(CompassTarget::WatchArch);++value) {
            const auto target=static_cast<CompassTarget>(value);const auto direction=trailCompassReadout(state(),queries_,trailSites_,target);
            if(!direction.revealed)continue;
            add("Point to "+direction.label,direction.available,{O::CompassTarget,value});
            hud.rows.back().detail=direction.available?(compassTarget_==target?"Current destination":"Choose this destination"):direction.reason;
        }
        add("Close bag",true,{O::Close});
    }
    const auto context=std::string(mode())+":"+std::to_string(dialogueNpc_)+":"+std::to_string(bench_)+":"+std::to_string(chest_)+":"+std::to_string(catalogCategory_)+":"+std::to_string(journalQuest_)
        +":"+std::to_string(static_cast<int>(bindingAction_))+":"+std::to_string(bindingDevice_)+":"+std::to_string(preferencesRevision_)
        +":"+std::to_string(static_cast<int>(menu_))+":"+std::to_string(static_cast<int>(guideTopic_))+":"+std::to_string(static_cast<int>(guideReturnMenu_));
    if(!menuIntents_.publish(context,std::move(identities)))menuStatus_="Menu unavailable. Save and reopen the adventure.";
    for(size_t i=0;i<hud.rows.size();++i)hud.rows[i].intent=static_cast<uint32_t>(menuIntents_.intent(i));
    menuSelection_=std::clamp(menuSelection_,0,std::max(0,int(hud.rows.size())-1));hud.selectedRow=static_cast<size_t>(menuSelection_);
    if(menu_!=Menu::None)hud.title=menuTitle_;
    hud.menuText=menuText_;hud.menuStatus=menuStatus_;
    const uint32_t contextIntent=menuIntents_.token()*64u+1u;
    const auto button=[&](const char* label,int action,int value=0){return render::AdventureHudRow{label,"",true,action,value,contextIntent,0};};
    hud.quickActions=state().health?std::vector<render::AdventureHudRow>{button("Use",7),button("Build",21),button("Bag",24),button("Attack",27),button("Dodge",28),button("Pause",9)}
        :std::vector<render::AdventureHudRow>{button("Return home",11),button("Save",8)};
    if(freeBuild_)hud.quickActions={button("Use",7),button("Build",21),button("Pause",9)};
    for(auto& row:hud.quickActions) {
        if(row.action==27)row.enabled=state().equippedTool.kind==ItemKind::TrailStaff&&state().combat.tick>=state().combat.player.attackReadyTick;
        if(row.action==28)row.enabled=state().combat.tick>=state().combat.player.dodgeReadyTick;
    }
    hud.buildControls={button("Pieces",23),button("Rotate",3),button("Raise",12,1),button("Lower",12,-1),button("Remove",5),button("Undo",6),button("Help",29,1),button("Done",22)};
    if(menu_!=Menu::None)hud.buildControls={button("Previous",25,-1),button("Next",25,1),button("Close",20)};
    if(menu_==Menu::Guide||menu_==Menu::GuideTopics)hud.buildControls.clear();
    hud.categories={button("Structure",13,1),button("Bricks",13,2),button("Furniture",13,3)};
    hudContent_=hud;hud_.setContent(std::move(hud));
}
bool AdventureRuntime::render(WGPUCommandEncoder encoder,WGPUTextureView color,WGPUTextureView depth,
    WGPUTextureView linearDepth,WGPUTextureView environment,WGPUTextureView rayDepth,
    const Camera& camera,const render::PrimitiveLighting& lighting,uint32_t width,uint32_t height,render::SceneShadowConsumer background) {
    if(!meshes_.setSceneTextures(environment,rayDepth))return false;
    meshes_.clearInstances();const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    const auto drawBuilding=[&](const WorldPart& part,bool doorOpen,glm::vec4 tint=glm::vec4(1),bool preview=false) {
        const bool door=part.kind==PieceKind::HingedDoor;
        glm::vec4 paint(0);
        if(part.paint) {
            const auto linear=[](uint32_t byte){const float c=float(byte)/255.f;return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
            paint={linear((part.paint>>16)&255),linear((part.paint>>8)&255),linear(part.paint&255),1};
        }
        const auto transform=model(part,origin);
        render::MeshDrawInstance instance{.assetIndex=0,
            .meshIndex=uint32_t(door?PieceKind::Doorway:part.kind)-1,
            .modelMatrix=glm::mat4(transform),.tintColor=tint,
            .emissiveBoost=preview?.12f:0.f,.castsSunShadow=!preview,.baseColorOverride=paint,
            .surface=preview?glm::vec4(0):glm::vec4(0,0,1,0)};
        meshes_.addInstance(instance);
        if(door) {
            instance.assetIndex=7;instance.meshIndex=0;
            instance.modelMatrix=glm::mat4(transform*doorLeafTransform(doorOpen));
            meshes_.addInstance(instance);
        }
    };
    for(const auto& structure:state().structures)for(const auto& part:structure.parts) {
        bool open=false;
        if(part.kind==PieceKind::HingedDoor)for(const auto& c:state().components)
            if(c.part==part.id&&c.kind==FurnitureKind::Door){open=c.doorOpen;break;}
        drawBuilding(part,open);
    }
    if(!freeBuild_) {
    for(const auto& group:village_.groups())if(group.available) {
        for(const auto& piece:group.pieces) {
            const WorldPart part{0,piece.kind,piece.position,piece.yawQuarterTurns,0};
            meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(piece.kind)-1,
                .modelMatrix=glm::mat4(model(part,origin)),.surface={0,0,1,0}});
        }
        for(const auto& prop:group.props) {
            const auto transform=glm::translate(glm::dmat4(1),prop.feet-origin)
                *glm::rotate(glm::dmat4(1),double(prop.yawQuarterTurns)*std::numbers::pi/2,glm::dvec3(0,1,0));
            meshes_.addInstance({.assetIndex=5,.meshIndex=uint32_t(prop.kind),
                .modelMatrix=glm::mat4(transform),.surface={0,0,1,0}});
        }
    }
    for(const auto& group:trailSites_.groups())if(group.available)for(const auto& piece:group.pieces) {
        const WorldPart part{0,piece.kind,piece.position,piece.yawQuarterTurns,0};
        meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(piece.kind)-1,
            .modelMatrix=glm::mat4(model(part,origin)),.surface={0,0,1,0}});
    }
    const auto markers=markerPositions(queries_.terrain());
    for(size_t i=0;i<markers.size();++i) {
        const auto root=glm::translate(glm::dmat4(1),markers[i]-origin)*glm::scale(glm::dmat4(1),glm::dvec3(.5,(i?4.:2.)/.96,.5));
        meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Brick2x2)-1,.modelMatrix=glm::mat4(root),.tintColor=i?glm::vec4(.9,.72,.3,1):glm::vec4(.72,.52,.28,1),.emissiveBoost=i?(state().trail.relayActivationRevision?1.5f:0.f):.15f,.surface={0,0,1,0}});
    }
    // Resource stacks are installed gatherable objects. Their persistent depletion
    // controls presentation; these never masquerade as player-owned house parts.
    for(const auto& node:content_.resourceNodes) {
        if(std::binary_search(state().depletedNodes.begin(),state().depletedNodes.end(),node.id))continue;
        auto root=glm::translate(glm::dmat4(1),glm::dvec3(node.position.x,node.position.y,node.position.z)-origin);
        root=glm::scale(root,glm::dvec3(.35,.5,.35));
        meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Brick2x2)-1,.modelMatrix=glm::mat4(root),
            .tintColor=node.yield.kind==ItemKind::Wood?glm::vec4(.65,.4,.2,1):node.yield.kind==ItemKind::Stone?glm::vec4(.7,.75,.8,1):glm::vec4(.3,.85,.85,1),.surface={0,0,1,0}});
    }
    for(size_t i=0;i<town_.entries().size();++i) {
        const auto& resident=town_.entries()[i];if(!resident.available)continue;
        const auto* definition=residentDefinition(resident.id);if(!definition)continue;
        assets::RigidAnimationPose pose;std::string error;
        const auto root=glm::translate(glm::dmat4(1),resident.feet-origin)
            *glm::rotate(glm::dmat4(1),residentFacing_[i],glm::dvec3(0,1,0));
        if(!residentCharacters_[i].sample(*residentAssets_[i],root,pose,error))return false;
        for(uint32_t draw=0;draw<pose.drawCount;++draw)meshes_.addInstance({.assetIndex=uint32_t(i+2),.meshIndex=pose.draws[draw].meshIndex,
            .modelMatrix=pose.draws[draw].modelMatrix,.tintColor=glm::vec4(1),.surface={0,0,1,0}});
    }
    for(size_t i=0;i<encounters_.entries().size();++i) {
        const auto& encounter=encounters_.entries()[i];
        const auto& progress=state().combat.encounters[i];
        if(!encounter.available) {
            if(progress.deathRevision&&!progress.lootClaimRevision) {
                const auto p=progress.checkpoint.pose;
                const auto root=glm::translate(glm::dmat4(1),glm::dvec3(p.x,p.y,p.z)-origin)
                    *glm::scale(glm::dmat4(1),glm::dvec3(.22,.25,.22));
                meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Brick2x2)-1,
                    .modelMatrix=glm::mat4(root),.tintColor=glm::vec4(.95,.75,.36,1),.emissiveBoost=.1f,.surface={0,0,1,0}});
            }
            continue;
        }
        assets::RigidAnimationPose pose;std::string error;
        const auto p=encounter.pose;
        const auto root=glm::translate(glm::dmat4(1),glm::dvec3(p.x,p.y,p.z)-origin)
            *glm::rotate(glm::dmat4(1),p.yaw,glm::dvec3(0,1,0));
        if(!enemyCharacters_[i].sample(*raider_,root,pose,error))return false;
        for(uint32_t draw=0;draw<pose.drawCount;++draw)meshes_.addInstance({.assetIndex=6,.meshIndex=pose.draws[draw].meshIndex,
            .modelMatrix=pose.draws[draw].modelMatrix,.tintColor=glm::vec4(1),.surface={0,0,1,0}});
        if(progress.checkpoint.phase==EnemyPhase::Windup) {
            // Three small lit bricks mark the frozen strike direction. Text
            // also names the windup, so avoiding it never depends on colour.
            for(int marker=1;marker<=3;++marker) {
                const double distance=double(marker)*.45;
                const glm::dvec2 xz(p.x-std::sin(p.yaw)*distance,p.z-std::cos(p.yaw)*distance);
                const double y=walkQueries_.supportHeight(xz,.06,p.y+.4);
                if(!std::isfinite(y))continue;
                const auto mark=glm::translate(glm::dmat4(1),glm::dvec3(xz.x,y+.015,xz.y)-origin)
                    *glm::scale(glm::dmat4(1),glm::dvec3(.1,.05,.1));
                meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Brick2x2)-1,.modelMatrix=glm::mat4(mark),
                    .tintColor=glm::vec4(1,.72,.2,1),.emissiveBoost=.8f,.castsSunShadow=false});
            }
        }
    }
    } // Legacy adventure scenery and actors.
    if(building_&&hasTarget_&&menu_==Menu::None) {
        std::string error;
        const auto ghosts=blueprint_!=BlueprintKind::None?buildingBlueprintLayout(blueprint_,preview_.position,yaw_,error):std::vector<PlacePart>{preview_};
        for(const auto& source:ghosts) {
            const WorldPart ghost{0,source.kind,source.position,source.yawQuarterTurns,source.paint};
            // A valid creative ghost previews the chosen material. Invalid ghosts
            // keep the shared red refusal tint and the text explanation.
            drawBuilding(ghost,false,previewValid_?(freeBuild_?glm::vec4(1,1,1,.45):glm::vec4(.3,1,.55,.45)):glm::vec4(1,.24,.15,.45),true);
        }
    }
    if(orbit_.pose().valid&&!orbit_.pose().hideAvatar) {
        assets::RigidAnimationPose pose;std::string error;
        const auto root=glm::translate(glm::dmat4(1),player_.feet()-origin)*glm::rotate(glm::dmat4(1),player_.facingYaw(),glm::dvec3(0,1,0));
        if(!character_.sample(*robot_,root,pose,error))return false;
        for(uint32_t i=0;i<pose.drawCount;++i)meshes_.addInstance({.assetIndex=1,.meshIndex=pose.draws[i].meshIndex,.modelMatrix=pose.draws[i].modelMatrix,.surface={0,0,1,0}});
        if(state().equippedTool.kind==ItemKind::TrailStaff) {
            // Attach a small wooden beam to the actual exported right-hand
            // anchor. It follows the accepted tool clip; hits use the resolver.
            const auto staff=trailStaffModel(pose.anchors[2]);
            meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Beam)-1,.modelMatrix=glm::mat4(staff),
                .tintColor=glm::vec4(1),.surface={0,0,1,0}});
        }
    }
    return meshes_.render(encoder,color,depth,camera.viewMatrix(),camera.projectionMatrix(),camera.position(),lighting,width,height,true,linearDepth,background,glm::vec3(origin));
}
bool AdventureRuntime::renderHud(WGPUCommandEncoder encoder,WGPUTextureView view,uint32_t width,uint32_t height){
#if !defined(VOXY_NATIVE)
    if(domUiAttached_){hud_.clearEncodedObservation();return true;}
#endif
    return hud_.render(encoder,view,width,height);
}
std::string AdventureRuntime::json() const {
    size_t parts=0;for(const auto& s:state().structures)parts+=s.parts.size();
    const auto* blueprint=buildingBlueprintDefinition(blueprint_);
    const auto cost=blueprint?blueprint->cost:buildingDefinition(selected_)->cost;
    std::ostringstream out;out<<std::setprecision(17)<<"{\"costText\":"<<quote(freeBuild_?"Unlimited pieces":costLabel(cost))<<",\"creative\":"<<(freeBuild_?"true":"false")<<",\"build\":"<<(building_?"true":"false")<<",\"selected\":"<<quote(blueprint?blueprint->name:buildingDefinition(selected_)->name)
        <<",\"blueprintKind\":"<<int(blueprint_)<<",\"piece\":"<<(blueprint_==BlueprintKind::StarterRoom?0:int(selected_))<<",\"status\":"<<quote(status_)<<",\"previewReason\":"<<quote(previewReason_)<<",\"interaction\":"<<quote(interactionLabel())
        <<",\"valid\":"<<(previewValid_?"true":"false")<<",\"wood\":"<<itemCount(state().backpack,ItemKind::Wood)
        <<",\"stone\":"<<itemCount(state().backpack,ItemKind::Stone)<<",\"scrap\":"<<itemCount(state().backpack,ItemKind::Scrap)
        <<",\"parts\":"<<parts<<",\"menu\":"<<quote(menu_==Menu::None?"":menu_==Menu::Main?"Adventure paused":menuTitle_)
        <<",\"saveFailure\":"<<quote(saveFailure_)<<",\"saveStatus\":"<<quote(saveStatus_)<<",\"dirty\":"<<(migrationDirty_||state().revision!=savedRevision_?"true":"false")
        <<",\"textScale\":"<<preferences_.textScale<<",\"highContrast\":"<<(preferences_.highContrast?"true":"false")<<",\"rows\":[";
    for(size_t i=0;i<hudContent_.rows.size();++i){if(i)out<<',';
        const auto& row=hudContent_.rows[i];
        const auto& command=menuCommands_[i];
        const auto rowBlueprint=command.operation==MenuOperation::Starter?BlueprintKind::StarterRoom
            :command.operation==MenuOperation::BuildRecipe?static_cast<BlueprintKind>(command.argument):BlueprintKind::None;
        out<<"{\"label\":"<<quote(row.label)<<",\"enabled\":"<<(row.enabled?"true":"false")<<",\"intent\":"<<row.intent<<",\"pieceKind\":"<<int(row.pieceKind)
            <<",\"blueprintKind\":"<<int(rowBlueprint)<<",\"detail\":"<<quote(row.detail)<<'}';}
    out<<"],\"mode\":"<<quote(mode())<<",\"menuTitle\":"<<quote(menuTitle_)<<",\"menuText\":"<<quote(menuText_)<<",\"menuStatus\":"<<quote(menuStatus_)
        <<",\"menuToken\":"<<menuIntents_.token()<<",\"catalogCategory\":"<<int(catalogCategory_);
    out<<",\"preferencesRevision\":"<<quote(std::to_string(preferencesRevision_))<<",\"preferencesStatus\":"<<quote(preferencesStatus_)
        <<",\"attackControl\":"<<quote(combatBindingLabel(preferences_,CombatAction::Attack,padAim_))
        <<",\"dodgeControl\":"<<quote(combatBindingLabel(preferences_,CombatAction::Dodge,padAim_))
        <<",\"lookControl\":"<<quote(preferences_.orbitToggle?"Click the right mouse button to start or stop looking. The right stick looks around.":"Hold the right mouse button to look around, or use the right stick.");
    out<<",\"paint\":"<<selectedPaint_<<",\"colourAvailable\":"<<(freeBuild_?"true":"false")
        <<",\"canUndo\":"<<(AdventureSession::findPart(state(),lastPlaced_)||lastBlueprint_?"true":"false")
        <<",\"canRemove\":"<<(targetPart_&&!isVillagePartId(targetPart_)&&!isTrailPartId(targetPart_)?"true":"false");
    out<<",\"observation\":"<<quote(std::to_string(observationSerial_))<<",\"revision\":"<<quote(std::to_string(state().revision))<<",\"menuSelected\":"<<menuSelection_;
    const auto p=player_.feet();out<<",\"player\":{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"z\":"<<p.z<<",\"yaw\":"<<player_.facingYaw()<<",\"tick\":"<<quote(std::to_string(player_.tick()))<<"}";
    out<<",\"camera\":{\"yaw\":"<<orbit_.pose().yaw<<",\"width\":"<<observedWidth_<<",\"height\":"<<observedHeight_;
    const auto vector=[&](const char* name,glm::dvec3 v){out<<",\""<<name<<"\":["<<v.x<<','<<v.y<<','<<v.z<<']';};
    vector("eye",observedEye_);vector("viewTarget",observedTarget_);vector("origin",observedOrigin_);
    out<<",\"viewProjection\":[";for(int i=0;i<16;++i){if(i)out<<',';out<<observedViewProjection_[i/4][i%4];}out<<"]}";
    out<<",\"aimInput\":{\"pointer\":["<<observedPointer_.x<<','<<observedPointer_.y<<"],\"usedPointer\":["<<observedAimPointer_.x<<','<<observedAimPointer_.y
        <<"],\"mouseCaptured\":"<<(observedMouseCaptured_?"true":"false")<<",\"gamepadConnected\":"<<(observedPadConnected_?"true":"false")<<",\"padOwnsAim\":"<<(padAim_?"true":"false")<<'}';
    out<<",\"aimRay\":{\"from\":["<<observedRayFrom_.x<<','<<observedRayFrom_.y<<','<<observedRayFrom_.z<<"],\"to\":["<<observedRayTo_.x<<','<<observedRayTo_.y<<','<<observedRayTo_.z
        <<"],\"complete\":"<<(observedRayHit_.complete?"true":"false")<<",\"hit\":"<<(observedRayHit_.hit?"true":"false")<<",\"distance\":"<<observedRayHit_.distance<<'}';
    const auto preview=metres(preview_.position);out<<",\"preview\":{\"x\":"<<preview.x<<",\"y\":"<<preview.y<<",\"z\":"<<preview.z<<",\"kind\":"<<int(preview_.kind)<<",\"yaw\":"<<int(yaw_)<<"}";
    out<<",\"world\":\"";constexpr char digits[]="0123456789abcdef";for(auto b:state().world.bytes)out<<digits[b>>4]<<digits[b&15];out<<"\",\"registeredBed\":"<<quote(std::to_string(state().registeredBed));
    out<<",\"health\":"<<state().health<<",\"combatLabel\":"<<quote(combatLabel())
        <<",\"staffEquipped\":"<<(state().equippedTool.kind==ItemKind::TrailStaff?"true":"false")
        <<",\"attackReady\":"<<(state().health&&state().combat.tick>=state().combat.player.attackReadyTick?"true":"false")
        <<",\"dodgeReady\":"<<(state().health&&state().combat.tick>=state().combat.player.dodgeReadyTick?"true":"false")
        <<",\"combatTick\":"<<quote(std::to_string(state().combat.tick))<<",\"enemies\":[";
    for(size_t i=0;i<state().combat.encounters.size();++i) {
        if(i)out<<',';
        const auto& entry=state().combat.encounters[i];const auto& enemy=entry.checkpoint;
        out<<"{\"id\":"<<int(enemy.encounterId)<<",\"health\":"<<enemy.health<<",\"phase\":"<<quote(AdventureCombat::phaseLabel(enemy.phase))
            <<",\"phaseCode\":"<<int(enemy.phase)<<",\"phaseTicks\":"<<enemy.phaseTicks<<",\"attackSerial\":"<<quote(std::to_string(enemy.attackSerial))
            <<",\"available\":"<<(encounters_.entries()[i].available?"true":"false")
            <<",\"x\":"<<enemy.pose.x<<",\"y\":"<<enemy.pose.y<<",\"z\":"<<enemy.pose.z<<",\"yaw\":"<<enemy.pose.yaw
            <<",\"deathRevision\":"<<quote(std::to_string(entry.deathRevision))<<",\"lootClaimRevision\":"<<quote(std::to_string(entry.lootClaimRevision))<<'}';
    }
    out<<']';
    out<<",\"hammerEquipped\":"<<(state().equippedTool.kind==ItemKind::FieldHammer?"true":"false");
    const auto readiness=firstHomeReadiness(state(),queries_);const auto compass=trailCompassReadout(state(),queries_,trailSites_,compassTarget_);
    out<<",\"quest\":{\"phase\":"<<quote(state().firstHome.phase==QuestPhase::Completed?"completed":state().firstHome.phase==QuestPhase::Active?"active":"not-accepted")
        <<",\"ready\":"<<(readiness.ready()?"true":"false")<<",\"objective\":"<<quote(homeObjective(state(),readiness))
        <<",\"recipeUnlocked\":"<<(trailCompassRecipeUnlocked(state().firstHome)?"true":"false")
        <<",\"rewardRevision\":"<<quote(std::to_string(state().firstHome.rewardRevision))<<'}';
    out<<",\"objective\":"<<quote(trailObjective(state(),readiness,fieldHome_));
    out<<",\"trailQuests\":[";
    const auto trailRows=trailJournal(state(),fieldHome_);
    for(size_t i=0;i<trailRows.size();++i) {
        if(i)out<<',';
        const auto& row=trailRows[i];const auto& progress=state().trail.quests[i];
        out<<"{\"id\":"<<int(row.questId)<<",\"phase\":"<<int(row.phase)<<",\"title\":"<<quote(row.title)
            <<",\"objective\":"<<quote(row.objective)<<",\"ready\":"<<(row.ready?"true":"false")
            <<",\"rewardRevision\":"<<quote(std::to_string(progress.rewardRevision))<<'}';
    }
    out<<"],\"sideQuest\":";
    if(const auto side=trailSideQuest(state()))out<<"{\"id\":"<<int(side->questId)<<",\"title\":"<<quote(side->title)<<",\"phase\":"<<int(side->phase)
        <<",\"objective\":"<<quote(side->objective)<<",\"ready\":"<<(side->ready?"true":"false")
        <<",\"rewardRevision\":"<<quote(std::to_string(state().trail.quests[3].rewardRevision))<<'}';
    else out<<"null";
    out<<",\"wideStoneStepUnlocked\":"<<(wideStoneStepRecipeUnlocked(state())?"true":"false")<<",\"discoveries\":[";
    for(size_t i=0;i<state().trail.discoveries.size();++i) {
        if(i)out<<',';
        const auto& discovery=state().trail.discoveries[i];
        out<<"{\"id\":"<<i+1<<",\"discoveredRevision\":"<<quote(std::to_string(discovery.discoveredRevision))
            <<",\"rewardClaimRevision\":"<<quote(std::to_string(discovery.rewardClaimRevision))<<'}';
    }
    out<<"],\"trailSites\":[";
    for(size_t i=0;i<trailSites_.sites().size();++i) {
        if(i)out<<',';
        const auto& site=trailSites_.sites()[i];
        out<<"{\"id\":"<<int(site.id)<<",\"available\":"<<(site.available?"true":"false")
            <<",\"active\":"<<(site.active?"true":"false")<<",\"x\":"<<site.position.x<<",\"y\":"<<site.position.y<<",\"z\":"<<site.position.z<<'}';
    }
    out<<"],\"relayActivationRevision\":"<<quote(std::to_string(state().trail.relayActivationRevision));
    out<<",\"equippedUtility\":{\"kind\":"<<int(state().equippedUtility.kind)<<",\"quantity\":"<<state().equippedUtility.quantity<<'}';
    out<<",\"compass\":{\"equipped\":"<<(compass.equipped?"true":"false")<<",\"target\":"<<quote(compass.target==CompassTarget::Home?"home":compass.target==CompassTarget::Relay?"relay":compass.target==CompassTarget::SignalTerrace?"signal-terrace":compass.target==CompassTarget::SurveyOverlook?"survey-overlook":"watch-arch")
        <<",\"label\":"<<quote(compass.label)<<",\"bearing\":"<<compass.bearing<<",\"distance\":"<<compass.distance
        <<",\"direction\":"<<quote(compass.direction)<<",\"available\":"<<(compass.available?"true":"false")
        <<",\"reason\":"<<quote(compass.reason)<<",\"backpackSlot\":"<<(compass.backpackSlot?std::to_string(*compass.backpackSlot):"null")<<'}';
    out<<",\"dialogue\":";
    if(menu_==Menu::Dialogue) {
        const auto* npc=residentDefinition(dialogueNpc_);const auto dialogue=homeDialogue(dialogueNpc_,state(),readiness);
        const auto trail=trailDialogue(dialogueNpc_,state(),fieldHome_);
        out<<"{\"npcId\":"<<int(dialogueNpc_)<<",\"name\":"<<quote(npc?npc->name:"")<<",\"role\":"<<quote(npc?npc->role:"")<<",\"text\":"<<quote(trail?trail->text:dialogue.text)<<'}';
    } else out<<"null";
    out<<",\"residents\":[";
    for(size_t i=0;i<town_.entries().size();++i) {
        if(i)out<<',';
        const auto& npc=town_.entries()[i];const auto* definition=residentDefinition(npc.id);
        out<<"{\"id\":"<<npc.id<<",\"name\":"<<quote(definition?definition->name:"")<<",\"role\":"<<quote(definition?definition->role:"")
            <<",\"x\":"<<npc.feet.x<<",\"y\":"<<npc.feet.y<<",\"z\":"<<npc.feet.z<<",\"available\":"<<(npc.available?"true":"false")<<'}';
    }
    out<<"],\"metNpcMask\":"<<int(state().metNpcMask)<<",\"saveSchema\":"<<kAdventureSaveSchema<<",\"migrationDirty\":"<<(migrationDirty_?"true":"false");
    const auto writeStructures=[&](std::ostream& stream) {
        stream<<",\"structures\":[";for(size_t i=0;i<state().structures.size();++i){if(i)stream<<',';
            const auto& structure=state().structures[i];stream<<"{\"id\":"<<quote(std::to_string(structure.id))<<",\"parts\":[";
            for(size_t j=0;j<structure.parts.size();++j){if(j)stream<<',';const auto& part=structure.parts[j];const auto point=metres(part.position);stream<<"{\"id\":"<<quote(std::to_string(part.id))<<",\"kind\":"<<int(part.kind)<<",\"x\":"<<point.x<<",\"y\":"<<point.y<<",\"z\":"<<point.z<<",\"yaw\":"<<int(part.yawQuarterTurns)<<'}';}stream<<"]}";}stream<<']';
    };
    // Preserve arbitrary locale facets/rounding behaviour on the direct path.
    const bool cacheStructures=freeBuild_&&out.getloc()==std::locale::classic()
        &&std::fegetround()==FE_TONEAREST;
    if(cacheStructures) {
        if(!structureJson_||structureJson_->world!=state().world
            ||structureJson_->epoch!=state().epoch||structureJson_->geometryRevision!=queries_.revision()) {
            std::ostringstream fragment;fragment.copyfmt(out);writeStructures(fragment);
            structureJson_=StructureJson{state().world,state().epoch,queries_.revision(),fragment.str()};
        }
        out<<structureJson_->bytes;
    } else writeStructures(out);
    out<<",\"components\":[";for(size_t i=0;i<state().components.size();++i){if(i)out<<',';
        const auto& c=state().components[i];out<<"{\"id\":"<<quote(std::to_string(c.id))<<",\"part\":"<<quote(std::to_string(c.part))<<",\"kind\":"<<int(c.kind)<<",\"doorOpen\":"<<(c.doorOpen?"true":"false")<<",\"wood\":"<<itemCount(c.slots,ItemKind::Wood)<<",\"stone\":"<<itemCount(c.slots,ItemKind::Stone)<<",\"scrap\":"<<itemCount(c.slots,ItemKind::Scrap)<<'}';}out<<"]}";return out.str();
}
bool AdventureRuntime::snapshot(std::vector<std::byte>& bytes,std::string& error) const {const bool ok=AdventureSaveCodec::encode(state(),content_,bytes,error);if(ok)savingRevision_=state().revision;return ok;}
bool AdventureRuntime::restore(std::span<const std::byte> bytes,construction::WorldNamespace world,std::string& error) {
    AdventureState restored;AdventureSaveLoadMetadata metadata;
    if(!AdventureSaveCodec::decode(bytes,world,content_,restored,error,&metadata))return false;
    auto next=AdventureSession::restore(restored,content_,error);AdventureSpatialQueries geometry;TownResidents residents;VillageLayout village;TrailSites sites;
    if(!next||!prepareGeometry(restored,geometry,residents,village,sites,error,false))return false;
    AdventureSpatialQueries actors;AdventureEncounters encounters;
    if(!prepareActors(restored,geometry,actors,encounters,error))return false;
    const auto p=restored.player;auto pose=player_.state();pose.feet={p.x,p.y,p.z};pose.velocity={};pose.facingYaw=p.yaw;
    pose.mode=AdventurePlayer::Mode::Airborne;
    if(!actors.clearCapsule(pose.feet)){error="Saved player position is blocked.";return false;}
    AdventurePlayer checkedPlayer;
    if(!checkedPlayer.initialize(actors,townSpawn(actors.terrain()),installedWorld().waterHeight)
        ||!checkedPlayer.restore(pose)) {error="Saved player position is unavailable.";return false;}
    session_=std::move(next);walkQueries_=std::move(geometry);queries_=std::move(actors);encounters_=std::move(encounters);
    previewResult_.reset();structureJson_.reset();aimRayResult_.reset(); // A restored checkpoint can reuse the same revision.
    town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
    combat_.reset();combatSeconds_=0;pendingAttack_=false;pendingDodge_=false;pendingJump_=false;
    if(!player_.restore(pose)){error="Saved player position is unavailable.";return false;}
    // Builder previews, undo targets and queued menu choices belong to the
    // previous checkpoint. Invalidate their tokens without recycling IDs.
    selectedPaint_=0;brickScroll_=0;building_=freeBuild_;blueprint_=BlueprintKind::None;selected_=freeBuild_?PieceKind::Brick2x4:PieceKind::Foundation;
    menu_=Menu::None;menuSelection_=0;heightSteps_=0;lastPlaced_=0;lastBlueprint_=0;
    guideReturnMenu_=Menu::None;guideReturnSelection_=0;guideTopic_=AdventureGuideTopic::Movement;guideGamepad_=false;
    bench_=0;chest_=0;dialogueNpc_=0;journalQuest_=1;targetPart_=0;
    pendingActions_.clear();previewValid_=false;hasTarget_=false;previewReason_.clear();
    player_.discardPendingInput();inputRouter_.reset();buildRouter_.reset();combatInput_.reset();
    (void)menuIntents_.publish("restored-checkpoint",{});
    discontinuity_=true;status_=freeBuild_?"Your build is restored.":"Welcome home. Your adventure is restored.";
    migrationDirty_=metadata.migrated;
    saveStatus_=migrationDirty_?"World updated. Save to keep the new format.":(freeBuild_?"Saved build loaded":"Saved adventure loaded");
    savedRevision_=state().revision;refreshHud();return true;
}
}

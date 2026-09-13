#include "game/adventure/adventure_runtime.hpp"
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
#include <iomanip>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#if defined(VOXY_NATIVE)
#include "engine/platform/native/native_adventure_saves.hpp"
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
}
AdventureRuntime::AdventureRuntime():preferences_(adventureInputDefaults()){}
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
    if(status.starts_with("Saved")){savedRevision_=savingRevision_;if(state().revision!=savedRevision_)status="Checkpoint saved. New changes are unsaved.";}
    saveStatus_=std::move(status);
}
bool AdventureRuntime::initialize(terrain::lego::Surface surface,WGPUDevice device,WGPUQueue queue,
    const std::filesystem::path& shaders,WGPUTextureFormat color,std::string& error) {
    if(!matchesInstalledTerrain(surface)||!queries_.bindTerrain(surface)) {error="Adventure terrain does not match the installed world.";return false;}
    const auto spawn=townSpawn(surface);
    content_.town={spawn.x,spawn.y,spawn.z,0};
    // Bind the exact terrain recipe and full sample bytes once. No second copy.
    static_assert(std::endian::native==std::endian::little);
    core::Sha256 terrainHash;terrainHash.update(std::as_bytes(surface.samples));
    if(core::sha256Hex(terrainHash.finish())!=installedWorld().samplesSha256) {error="Main landscape content has changed.";return false;}
    core::Sha256 hash;hash.string(installedWorld().profile);hash.string(installedWorld().contentRevision);
    hash.string(installedWorld().terrainRecipe);hash.string(installedWorld().samplesSha256);
    hash.string(buildingCatalogFingerprint());hash.string("adventure-items-and-homes-r01");
    content_.identity=hash.finish();
    for(uint32_t i=0;i<18;++i) {
        const double angle=i*2*std::numbers::pi/18;
        const glm::dvec2 p=installedWorld().town+glm::dvec2(std::cos(angle),std::sin(angle))*(12.+(i%3)*4);
        const auto y=terrain::lego::supportHeight(surface,glm::vec2(p),.3f);
        content_.resourceNodes.push_back({i+1,{p.x,y,p.y,0},{ItemKind(1+i%3),uint16_t(i%3==0?12:8)}});
    }
    construction::WorldNamespace world;std::random_device random;
    for(auto& byte:world.bytes)byte=static_cast<uint8_t>(random());
    world.bytes[6]=(world.bytes[6]&15)|64;world.bytes[8]=(world.bytes[8]&63)|128;
#if defined(VOXY_NATIVE)
    expedition::StoreIssue issue;
    const char* overrideRoot=std::getenv("VOXY_ADVENTURE_ROOT");
    auto root=overrideRoot&&*overrideRoot?std::filesystem::path(overrideRoot):platform::NativeSaveStore::defaultRoot(issue);
    if(issue){error="Adventure save directory is unavailable.";return false;}
    std::optional<std::string> selectedWorld;
    if(const char* selected=std::getenv("VOXY_ADVENTURE_WORLD");selected&&*selected)selectedWorld=selected;
    saves_=platform::NativeAdventureSaves::open(root,selectedWorld,std::getenv("VOXY_ADVENTURE_NEW")!=nullptr,content_,error);
    if(!saves_)return false;
    world=saves_->worldIdentity();
#else
    if(pendingBootstrap)world=pendingBootstrap->world;
#endif
    session_=AdventureSession::create(world,content_,error);
    std::vector<AdventureSpatialQueries::Solid> markers;appendMarkers(world,surface,markers);
    if(!session_||!queries_.publish(markers,session_->state().revision+1)
        ||!player_.initialize(queries_,spawn,installedWorld().waterHeight)) {error="Could not start on safe ground.";return false;}
    robot_=assets::loadRobotAsset(installedPath("data/salvage/robot-r01"),error);
    if(!robot_)return false;
    auto buildingMesh=installedBuildingMesh(error);
    if(!buildingMesh)return false;
    render::MeshPathConfig config;
    config.shaderPath=shaders/"mesh_path.wgsl";config.colorFormat=WGPUTextureFormat_RGBA16Float;
    config.frontFace=WGPUFrontFace_CW;config.linearHdrOutput=true;config.sunShadows=true;
    // Reserve expanded submesh draws once, not merely logical piece count.
    config.maxInstances=4608;config.maxDrawsPerFrame=4608;
    if(!meshes_.init(device,queue,config)
        ||!meshes_.loadMeshData(*buildingMesh)
        ||!meshes_.loadMeshData(robot_->mesh)
        ||!hud_.init(device,queue,color,shaders/"cove_hud.wgsl")) {error="Could not load the adventure building kit.";return false;}
#if defined(VOXY_NATIVE)
    if(!saves_->loadedBytes().empty()&&!restore(saves_->loadedBytes(),world,error))return false;
#else
    if(pendingBootstrap) {
        const auto bootstrap=std::exchange(pendingBootstrap,{});
        if(!bootstrap->bytes.empty()&&!restore(bootstrap->bytes,world,error))return false;
    }
#endif
    refreshHud();return true;
}
CommandStamp AdventureRuntime::stamp() const {return {state().revision,state().lastRequestSequence+1,1};}
CandidateValidator AdventureRuntime::validator() const {
    return [this](const AdventureState& before,const AdventureState& after,std::string& error){
        return validateConstruction(before,after,queries_,error)
            &&validateInteractions(before,after,content_,queries_,error);
    };
}
bool AdventureRuntime::prepareGeometry(const AdventureState& state,AdventureSpatialQueries& out,std::string& error) const {
    if(state.revision==UINT64_MAX){error="World revision capacity reached.";return false;}
    std::vector<AdventureSpatialQueries::Solid> solids;
    if(!validateInstalledGeometry(state,queries_,error)||!compileSolids(state,solids,error))return false;
    appendMarkers(state.world,queries_.terrain(),solids);
    return out.bindTerrain(queries_.terrain())&&out.publish(solids,state.revision+1);
}
bool AdventureRuntime::commit(std::optional<AdventureSession::PreparedChange> prepared) {
    if(!prepared)return false;
    AdventureSpatialQueries next;
    if(!prepareGeometry(prepared->state(),next,status_))return false;
    const auto changed=prepared->changedPart();
    const auto placed=changed&&!AdventureSession::findPart(state(),changed)&&AdventureSession::findPart(prepared->state(),changed)?changed:0;
    if(!session_->commit(std::move(*prepared),status_))return false;
    // The player holds the address of queries_, not its replaceable packet.
    queries_=std::move(next);lastPlaced_=placed?placed:lastPlaced_;
    saveStatus_="Changes not saved";
    return true;
}
void AdventureRuntime::action(int action,int value) {
    if(action==15){pendingUiInputOwned_=value!=0;uiOwnershipChanged_=true;return;}
    if(action>=1&&action<=14&&pendingActions_.size()<32)pendingActions_.emplace_back(action,value);
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
    if(input.isMouseCaptured()||padAim_)pointer={double(width)*.5,double(height)*.5};
    observedAimPointer_=pointer;
    const glm::dvec4 clip(2*pointer.x/double(std::max(1u,width))-1,1-2*pointer.y/double(std::max(1u,height)),1,1);
    auto world=glm::dmat4(camera.inverseViewProjectionMatrix())*clip;world/=world.w;
    const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    const auto eye=origin+glm::dvec3(camera.position());
    const auto direction=glm::normalize(origin+glm::dvec3(world)-eye);
    const auto hit=queries_.raycast(eye,direction,25);
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
        if(starter_)for(double dx:{-1.,1.})for(double dz:{-1.,1.}) {
            const auto corner=terrainPlacementHeight(PieceKind::Foundation,0,{p.x+dx,p.z+dz},queries_.terrain());
            if(corner)p.y=std::max(p.y,*corner);
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
    preview_={0,selected_,{int32_t(std::round(p.x*50)),int32_t(std::round(p.y*50)),int32_t(std::round(p.z*50))},yaw_,0};
    if(targetPart_)for(const auto& structure:state().structures)
        if(std::any_of(structure.parts.begin(),structure.parts.end(),[&](const auto& part){return part.id==targetPart_;}))preview_.structure=structure.id;
    // Ground additions touching an existing home join its authority. The policy
    // still checks actual support/contact; nearby distance alone cannot admit it.
    if(!preview_.structure)for(const auto& structure:state().structures)
        for(const auto& part:structure.parts)if(glm::length(metres(part.position)-p)<4.1){preview_.structure=structure.id;break;}
    previewReason_.clear();
    auto candidate=starter_?session_->prepareBlueprint(stamp(),starterRoomLayout(preview_.position,yaw_,previewReason_),validator(),previewReason_)
        :session_->preparePlace(stamp(),preview_,validator(),previewReason_);
    previewValid_=bool(candidate);
    if(previewValid_)previewReason_="Ready to place";
}
uint64_t AdventureRuntime::nearbyComponent() const {
    double best=3.25;uint64_t result=0;
    for(const auto& component:state().components)if(const auto* part=AdventureSession::findPart(state(),component.part)) {
        double distance=glm::length(metres(part->position)-player_.feet());
        if(component.part==targetPart_)distance-=.25;
        std::string reason;
        if(distance<best&&reachableComponent(state(),component.id,queries_,reason)){best=distance;result=component.id;}
    }
    return result;
}
std::string AdventureRuntime::interactionLabel() const {
    if(const auto* component=AdventureSession::findComponent(state(),nearbyComponent())) {
        if(component->kind==FurnitureKind::Chest)return "Open chest";
        if(component->kind==FurnitureKind::Workbench)return "Craft field hammer";
        return "Rest and register home";
    }
    for(const auto& node:content_.resourceNodes)
        if(!std::binary_search(state().depletedNodes.begin(),state().depletedNodes.end(),node.id)
            &&glm::length(glm::dvec3(node.position.x,node.position.y,node.position.z)-player_.feet())<3)
            return "Gather "+std::string(itemDefinition(node.yield.kind)->name);
    return "Use nearby";
}
void AdventureRuntime::use() {
    const auto id=nearbyComponent();const auto* component=AdventureSession::findComponent(state(),id);
    if(component) {
        if(!reachableComponent(state(),id,queries_,status_))return;
        if(component->kind==FurnitureKind::Chest){chest_=id;menu_=Menu::Chest;menuSelection_=0;status_="Choose items to move. Nothing is discarded.";return;}
        if(component->kind==FurnitureKind::Workbench) {
            if(commit(session_->prepareCraftHammer(stamp(),id,validator(),status_))) {
                for(uint8_t i=0;i<state().backpack.size();++i)if(state().backpack[i].kind==ItemKind::FieldHammer){
                    if(commit(session_->prepareEquipTool(stamp(),i,status_)))status_="Field hammer equipped. Gather twice as much.";
                    break;
                }
            }
            return;
        }
        PlayerPose recovery;
        if(usableBed(state(),id,queries_,recovery,status_)
            &&commit(session_->prepareUseBed(stamp(),id,recovery,validator(),status_)))status_="Rested. This home is your recovery point.";
        return;
    }
    for(const auto& node:content_.resourceNodes)if(!std::binary_search(state().depletedNodes.begin(),state().depletedNodes.end(),node.id)
        &&glm::length(glm::dvec3(node.position.x,node.position.y,node.position.z)-player_.feet())<3) {
        if(commit(session_->prepareGather(stamp(),node.id,validator(),status_)))status_="Materials gathered.";
        return;
    }
    status_="Move close to a bed, chest, workbench or supply pile.";
}
void AdventureRuntime::recover() {
    PlayerPose recovery=content_.town;std::string reason;
    if(state().registeredBed)(void)usableBed(state(),state().registeredBed,queries_,recovery,reason);
    auto pose=player_.state();pose.feet={recovery.x,recovery.y,recovery.z};pose.velocity={};pose.mode=AdventurePlayer::Mode::Walking;
    if(!player_.restore(pose)) {
        recovery=content_.town;pose.feet={recovery.x,recovery.y,recovery.z};
        if(!player_.restore(pose)){status_="No safe recovery point is available.";return;}
    }
    (void)session_->updatePlayer(recovery,100,status_);discontinuity_=true;
    status_="Recovered safely. Your buildings and items are kept.";menu_=Menu::None;
}
void AdventureRuntime::menuRow(int row) {
    if(row<0)return;
    if(menu_==Menu::Main) {
        if(row==0)menu_=Menu::None;
        else if(row==1){building_=true;menu_=Menu::Catalog;menuSelection_=0;}
        else if(row==2){saveRequested_=true;saveStatus_="Saving...";}
        else if(row==3)recover();
        else if(row==4){preferences_.textScale=preferences_.textScale==1?1.25:preferences_.textScale==1.25?1.5:1;}
        else if(row==5)preferences_.highContrast=!preferences_.highContrast;
        else if(row==6){auto settings=orbit_.settings();settings.reducedMotion=!settings.reducedMotion;(void)orbit_.settings(settings);}
        else if(row==7){starter_=true;selected_=PieceKind::Foundation;building_=true;menu_=Menu::None;heightSteps_=0;}
    } else if(menu_==Menu::Catalog&&row<int(kBuildingPieceCount)) {
        starter_=false;selected_=PieceKind(row+1);heightSteps_=0;building_=true;menu_=Menu::None;
    } else if(menu_==Menu::Chest) {
        const auto* chest=AdventureSession::findComponent(state(),chest_);if(!chest)return;
        std::vector<TransferItems> choices;
        for(uint8_t i=0;i<state().backpack.size();++i)if(state().backpack[i].quantity)
            choices.push_back({0,chest_,state().backpackRevision,chest->revision,i,uint16_t(std::min<int>(10,state().backpack[i].quantity))});
        for(uint8_t i=0;i<chest->slots.size();++i)if(chest->slots[i].quantity)
            choices.push_back({chest_,0,chest->revision,state().backpackRevision,i,uint16_t(std::min<int>(10,chest->slots[i].quantity))});
        if(size_t(row)<choices.size()&&commit(session_->prepareTransfer(stamp(),choices[size_t(row)],validator(),status_)))status_="Items moved safely.";
    }
}
void AdventureRuntime::update(double seconds,Input& input,Camera& camera,uint32_t width,uint32_t height) {
    using A=expedition::CoveAction;using C=expedition::CoveInputContext;
    if(uiOwnershipChanged_){
        uiInputOwned_=pendingUiInputOwned_;uiOwnershipChanged_=false;
        const bool focused=input.focused();input.onFocusChanged(false);input.onFocusChanged(focused);
        player_.discardPendingInput();
    }
    ++observationSerial_;
    const auto sample=expedition::sampleCoveInput(input);
    inputRouter_.tick(adventureMovementSample(sample,building_),menu_==Menu::None&&!uiInputOwned_?C::World:C::Menu,preferences_);
    buildRouter_.tick(sample,menu_==Menu::None&&building_&&!uiInputOwned_?C::Workshop:C::Menu,preferences_);
    const auto& pad=input.gamepad();
    auto pressed=[&](Key k){return input.wasKeyPressed(k);};
    if(pressed(Key::Escape)||pressed(static_cast<Key>(291))||pad.pressed(PadButton::Menu)) {
        menu_=menu_==Menu::None?Menu::Main:Menu::None;menuSelection_=0;input.releaseMouse();player_.discardPendingInput();
    } else if(inputRouter_.pressed(A::Workshop)) {building_=!building_;heightSteps_=0;input.releaseMouse();}
    if(inputRouter_.pressed(A::Save)||pressed(static_cast<Key>(294))) {saveRequested_=true;saveStatus_="Saving...";}
    if(menu_!=Menu::None||uiInputOwned_) {
        player_.discardPendingInput();
        if(pressed(Key::Up)||pad.navigation(0))menuSelection_=std::max(0,menuSelection_-1);
        if(pressed(Key::Down)||pad.navigation(1))++menuSelection_;
        if(pressed(Key::Enter)||pad.pressed(PadButton::Confirm))menuRow(menuSelection_);
        if(pad.pressed(PadButton::Back)){menu_=Menu::None;input.resetState();}
        if(input.wasMouseButtonPressed(MouseButton::Left))for(const auto& hit:hud_.layout().menuHits) {
            const auto p=input.mousePosition();
            if(p.x>=hit.bounds.x&&p.y>=hit.bounds.y&&p.x<hit.bounds.x+hit.bounds.z&&p.y<hit.bounds.y+hit.bounds.w){menuRow(hit.row);break;}
        }
    } else {
        const auto movement=inputRouter_.movement();const double yaw=orbit_.pose().valid?orbit_.pose().yaw:player_.facingYaw();
        const glm::dvec2 forward(-std::sin(yaw),-std::cos(yaw)),right(std::cos(yaw),-std::sin(yaw));
        player_.advance(seconds,{right*movement[0]+forward*movement[1],inputRouter_.pressed(A::Jump)});
        const auto p=player_.feet();
        std::string movementError;
        if(!session_->updatePlayer({p.x,p.y,p.z,player_.facingYaw()},state().health,movementError))status_=movementError;
        expedition::CoveCamera::Input orbitInput;
        const auto look=inputRouter_.look();
        const auto mouse=input.mouseDragDelta(MouseButton::Right);
        orbitInput.orbitRadians={double(mouse.x)*.004*preferences_.mouseSensitivity+look[0]*seconds*2,
            double(mouse.y)*.004*preferences_.mouseSensitivity+look[1]*seconds*2};
        orbitInput.zoomSteps=input.scrollDelta();orbitInput.active=input.focused();
        orbitInput.recenter=inputRouter_.pressed(A::Recenter);
        camera.setAspectRatio(width,height);
        const expedition::CoveCamera::Target target{p+glm::dvec3(0,1.2,0),player_.facingYaw(),queries_.revision(),{},discontinuity_,glm::length(player_.worldVelocity())>.1};
        (void)orbit_.update(target,orbitInput,{camera.fovY(),camera.aspectRatio(),camera.nearPlane()},queries_.cameraSweep(),seconds);
        if(orbit_.pose().valid) {
            const auto pose=orbit_.pose();const auto cameraPosition=physics::worldPositionFromAbsolute(pose.eye);
            const auto origin=glm::dvec3(cameraPosition.sector)*double(physics::kWorldSectorSize);
            camera.setWorldPosition(cameraPosition.sector,cameraPosition.local);camera.lookAt(glm::vec3(pose.viewTarget-origin));discontinuity_=false;
        }
        character_.update(seconds,player_.mode(),glm::length(glm::dvec2(player_.worldVelocity().x,player_.worldVelocity().z)),player_.worldVelocity().y,false);
        updateTarget(camera,input,width,height);
        if(building_) {
            if(pressed(Key::Tab)){menu_=Menu::Catalog;menuSelection_=int(selected_)-1;}
            if(buildRouter_.pressed(A::RotateY))yaw_=uint8_t((yaw_+1)%4);
            if(buildRouter_.pressed(A::PreviousPart))selected_=PieceKind((int(selected_)+12)%14+1);
            if(pad.pressed(PadButton::RightShoulder))selected_=PieceKind(int(selected_)%14+1);
            if(pressed(static_cast<Key>(266))||pad.pressed(PadButton::Up))++heightSteps_;
            if(pressed(static_cast<Key>(267))||pad.pressed(PadButton::Down))--heightSteps_;
            if(buildRouter_.pressed(A::Keep)||input.wasMouseButtonPressed(MouseButton::Left))action(4);
            if(buildRouter_.pressed(A::Remove)||pad.pressed(PadButton::Back))action(5);
            if(buildRouter_.pressed(A::Undo))action(6);
        } else if(inputRouter_.pressed(A::Interact))use();
    }
    const auto commands=std::exchange(pendingActions_,{});
    for(const auto [command,value]:commands) {
        switch(command) {
        case 1:building_=!building_;menu_=Menu::None;break;
        case 2:if(value>0&&pieceKindValid(uint32_t(value))){starter_=false;selected_=PieceKind(value);building_=true;menu_=Menu::None;heightSteps_=0;}break;
        case 3:yaw_=uint8_t((yaw_+1)%4);break;
        case 4:if(building_&&menu_==Menu::None) {
            updateTarget(camera,input,width,height);
            auto candidate=previewValid_?(starter_?session_->prepareBlueprint(stamp(),starterRoomLayout(preview_.position,yaw_,status_),validator(),status_)
                :session_->preparePlace(stamp(),preview_,validator(),status_)):std::nullopt;
            const auto structure=candidate?candidate->changedStructure():0;
            if(commit(std::move(candidate))) {
                lastBlueprint_=starter_?structure:0;
                status_=starter_?"Room built. Finish building, then step through the doorway.":"Placed. Remove returns its materials.";
                if(starter_)building_=false;
            } else if(!previewValid_)status_=previewReason_;
        }break;
        case 5:if(targetPart_&&commit(session_->prepareRemove(stamp(),targetPart_,validator(),status_)))status_="Removed. Materials returned.";break;
        case 6:if(lastBlueprint_?commit(session_->prepareRemoveStructure(stamp(),lastBlueprint_,validator(),status_))
            :lastPlaced_&&commit(session_->prepareRemove(stamp(),lastPlaced_,validator(),status_))) {
            lastPlaced_=0;lastBlueprint_=0;status_="Last placement undone. Materials returned.";
        }break;
        case 7:if(!building_&&menu_==Menu::None)use();break;
        case 8:saveRequested_=true;saveStatus_="Saving...";break;
        case 9:menu_=menu_==Menu::None?Menu::Main:Menu::None;menuSelection_=0;break;
        case 10:menuRow(value);break;
        case 11:recover();break;
        case 12:heightSteps_=std::clamp(heightSteps_+std::clamp(value,-1,1),-32,32);break;
        case 13:menu_=Menu::Catalog;menuSelection_=int(selected_)-1;break;
        case 14:starter_=true;selected_=PieceKind::Foundation;building_=true;menu_=Menu::None;heightSteps_=0;break;
        }
    }
    if(!commands.empty()&&menu_==Menu::None)updateTarget(camera,input,width,height);
#if defined(VOXY_NATIVE)
    if(saves_) {
        if(auto completion=saves_->poll())saveCompleted(completion->saved?"Saved adventure":completion->message);
        if(!saves_->busy()&&consumeSaveRequest()) {
            std::vector<std::byte> bytes;std::string error;
            if(!snapshot(bytes,error)||!saves_->requestSave(std::move(bytes),error))saveStatus_=error;
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
    render::CoveHudContent hud;
    hud.title="VOXYS  /  A place to call home";
    hud.selected=building_?(starter_?"Starter room":std::string(buildingDefinition(selected_)->name)):"Explore the main landscape";
    hud.economy="Wood "+std::to_string(itemCount(state().backpack,ItemKind::Wood))+"   Stone "+std::to_string(itemCount(state().backpack,ItemKind::Stone))+"   Scrap "+std::to_string(itemCount(state().backpack,ItemKind::Scrap));
    hud.status=building_?previewReason_:status_;
    hud.objective=state().registeredBed?"Home registered. Explore and improve your place.":"Build a room, add a roof, then use a bed inside.";
    hud.hints=building_?std::array<std::string,3>{"Click / E: place   R: rotate   Tab: pieces","Page Up / Down: height   Delete: remove   Ctrl+Z: undo","WASD: walk   Right-drag: look   B: finish building"}
        :std::array<std::string,3>{"WASD / left stick: walk   Space / A: jump","Right-drag: look   E / X: "+interactionLabel(),"B / View: build   F2 / Menu: options   F5: save"};
    hud.tone=building_&&!previewValid_?render::CoveHudTone::Blocked:render::CoveHudTone::Ready;
    hud.textScale=float(preferences_.textScale);hud.highContrast=preferences_.highContrast;
    if(menu_!=Menu::None) {
        render::CoveHudMenu menu;menu.status=status_;menu.selected=size_t(menuSelection_);
        if(menu_==Menu::Main) {
            menu.title="Adventure paused";menu.subtitle=saveStatus_;
            for(const auto& name:{"Resume","Choose building pieces","Save adventure","Recover at home / town","Text size","High contrast","Reduced camera motion","Starter room blueprint"})menu.rows.push_back({name,true});
        } else if(menu_==Menu::Catalog) {
            menu.title="Building pieces";menu.subtitle="Choose a piece. Walk to any suitable site.";
            for(const auto& definition:buildingCatalog())menu.rows.push_back({std::string(definition.name)+"  |  "+std::to_string(definition.cost.wood)+" wood, "+std::to_string(definition.cost.stone)+" stone, "+std::to_string(definition.cost.scrap)+" scrap",true});
        } else {
            menu.title="Chest";menu.subtitle="Move up to 10 items per selection.";
            for(const auto stack:state().backpack)if(stack.quantity)menu.rows.push_back({"Store "+std::string(itemDefinition(stack.kind)->name)+" ("+std::to_string(stack.quantity)+" in pack)",true});
            if(const auto* chest=AdventureSession::findComponent(state(),chest_))for(const auto stack:chest->slots)if(stack.quantity)
                menu.rows.push_back({"Take "+std::string(itemDefinition(stack.kind)->name)+" ("+std::to_string(stack.quantity)+" stored)",true});
        }
        menuSelection_=std::clamp(menuSelection_,0,std::max(0,int(menu.rows.size())-1));menu.selected=size_t(menuSelection_);hud.menu=std::move(menu);
    }
    hudContent_=hud;hud_.setContent(std::move(hud));
}
bool AdventureRuntime::render(WGPUCommandEncoder encoder,WGPUTextureView color,WGPUTextureView depth,
    WGPUTextureView linearDepth,WGPUTextureView environment,WGPUTextureView rayDepth,
    const Camera& camera,const render::PrimitiveLighting& lighting,uint32_t width,uint32_t height,render::SceneShadowConsumer background) {
    if(!meshes_.setSceneTextures(environment,rayDepth))return false;
    meshes_.clearInstances();const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    for(const auto& structure:state().structures)for(const auto& part:structure.parts)
        meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(part.kind)-1,.modelMatrix=glm::mat4(model(part,origin)),.surface={0,0,1,0}});
    const auto markers=markerPositions(queries_.terrain());
    for(size_t i=0;i<markers.size();++i) {
        const auto root=glm::translate(glm::dmat4(1),markers[i]-origin)*glm::scale(glm::dmat4(1),glm::dvec3(.5,(i?4.:2.)/.96,.5));
        meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Brick2x2)-1,.modelMatrix=glm::mat4(root),.tintColor=i?glm::vec4(.9,.72,.3,1):glm::vec4(.3,.9,.85,1),.emissiveBoost=.15f,.surface={0,0,1,0}});
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
    if(building_&&hasTarget_&&menu_==Menu::None) {
        std::string error;
        const auto ghosts=starter_?starterRoomLayout(preview_.position,yaw_,error):std::vector<PlacePart>{preview_};
        for(const auto& source:ghosts) {
            const WorldPart ghost{0,source.kind,source.position,source.yawQuarterTurns,0};
            meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(ghost.kind)-1,.modelMatrix=glm::mat4(model(ghost,origin)),
                .tintColor=previewValid_?glm::vec4(.3,1,.55,.45):glm::vec4(1,.24,.15,.45),.emissiveBoost=.12f,.castsSunShadow=false});
        }
    }
    if(orbit_.pose().valid&&!orbit_.pose().hideAvatar) {
        assets::RigidAnimationPose pose;std::string error;
        const auto root=glm::translate(glm::dmat4(1),player_.feet()-origin)*glm::rotate(glm::dmat4(1),player_.facingYaw(),glm::dvec3(0,1,0));
        if(!character_.sample(*robot_,root,pose,error))return false;
        for(uint32_t i=0;i<pose.drawCount;++i)meshes_.addInstance({.assetIndex=1,.meshIndex=pose.draws[i].meshIndex,.modelMatrix=pose.draws[i].modelMatrix,.surface={0,0,1,0}});
    }
    return meshes_.render(encoder,color,depth,camera.viewMatrix(),camera.projectionMatrix(),camera.position(),lighting,width,height,true,linearDepth,background,glm::vec3(origin));
}
bool AdventureRuntime::renderHud(WGPUCommandEncoder encoder,WGPUTextureView view,uint32_t width,uint32_t height){return hud_.render(encoder,view,width,height);}
std::string AdventureRuntime::json() const {
    size_t parts=0;for(const auto& s:state().structures)parts+=s.parts.size();
    auto cost=buildingDefinition(selected_)->cost;
    if(starter_) {cost={};std::string error;for(const auto& part:starterRoomLayout({},0,error)){const auto c=buildingDefinition(part.kind)->cost;cost.wood+=c.wood;cost.stone+=c.stone;cost.scrap+=c.scrap;}}
    std::ostringstream out;out<<std::setprecision(17)<<"{\"costText\":"<<quote(std::to_string(cost.wood)+" wood, "+std::to_string(cost.stone)+" stone, "+std::to_string(cost.scrap)+" scrap")<<",\"build\":"<<(building_?"true":"false")<<",\"selected\":"<<quote(starter_?"Starter room":buildingDefinition(selected_)->name)
        <<",\"piece\":"<<(starter_?0:int(selected_))<<",\"status\":"<<quote(status_)<<",\"previewReason\":"<<quote(previewReason_)<<",\"interaction\":"<<quote(interactionLabel())
        <<",\"valid\":"<<(previewValid_?"true":"false")<<",\"wood\":"<<itemCount(state().backpack,ItemKind::Wood)
        <<",\"stone\":"<<itemCount(state().backpack,ItemKind::Stone)<<",\"scrap\":"<<itemCount(state().backpack,ItemKind::Scrap)
        <<",\"parts\":"<<parts<<",\"menu\":"<<quote(menu_==Menu::None?"":menu_==Menu::Main?"Adventure paused":menu_==Menu::Catalog?"Building pieces":"Chest")
        <<",\"saveStatus\":"<<quote(saveStatus_)<<",\"dirty\":"<<(state().revision!=savedRevision_?"true":"false")
        <<",\"textScale\":"<<preferences_.textScale<<",\"highContrast\":"<<(preferences_.highContrast?"true":"false")<<",\"rows\":[";
    if(hudContent_.menu)for(size_t i=0;i<hudContent_.menu->rows.size();++i){if(i)out<<',';out<<"{\"label\":"<<quote(hudContent_.menu->rows[i].label)<<",\"enabled\":true}";}
    out<<"],\"observation\":"<<quote(std::to_string(observationSerial_))<<",\"revision\":"<<quote(std::to_string(state().revision))<<",\"menuSelected\":"<<menuSelection_;
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
    out<<",\"hammerEquipped\":"<<(state().equippedTool.kind==ItemKind::FieldHammer?"true":"false");
    out<<",\"structures\":[";for(size_t i=0;i<state().structures.size();++i){if(i)out<<',';const auto& structure=state().structures[i];out<<"{\"id\":"<<quote(std::to_string(structure.id))<<",\"parts\":[";
        for(size_t j=0;j<structure.parts.size();++j){if(j)out<<',';const auto& part=structure.parts[j];const auto point=metres(part.position);out<<"{\"id\":"<<quote(std::to_string(part.id))<<",\"kind\":"<<int(part.kind)<<",\"x\":"<<point.x<<",\"y\":"<<point.y<<",\"z\":"<<point.z<<",\"yaw\":"<<int(part.yawQuarterTurns)<<'}';}out<<"]}";}out<<']';
    out<<",\"components\":[";for(size_t i=0;i<state().components.size();++i){if(i)out<<',';const auto& c=state().components[i];out<<"{\"id\":"<<quote(std::to_string(c.id))<<",\"part\":"<<quote(std::to_string(c.part))<<",\"kind\":"<<int(c.kind)<<",\"wood\":"<<itemCount(c.slots,ItemKind::Wood)<<",\"stone\":"<<itemCount(c.slots,ItemKind::Stone)<<",\"scrap\":"<<itemCount(c.slots,ItemKind::Scrap)<<'}';}out<<"]}";return out.str();
}
bool AdventureRuntime::snapshot(std::vector<std::byte>& bytes,std::string& error) const {const bool ok=AdventureSaveCodec::encode(state(),content_,bytes,error);if(ok)savingRevision_=state().revision;return ok;}
bool AdventureRuntime::restore(std::span<const std::byte> bytes,construction::WorldNamespace world,std::string& error) {
    AdventureState restored;
    if(!AdventureSaveCodec::decode(bytes,world,content_,restored,error))return false;
    auto next=AdventureSession::restore(restored,content_,error);AdventureSpatialQueries geometry;
    if(!next||!prepareGeometry(restored,geometry,error))return false;
    const auto p=restored.player;auto pose=player_.state();pose.feet={p.x,p.y,p.z};pose.velocity={};pose.facingYaw=p.yaw;
    pose.mode=AdventurePlayer::Mode::Airborne;
    if(!geometry.clearCapsule(pose.feet)){error="Saved player position is blocked.";return false;}
    AdventurePlayer checkedPlayer;
    if(!checkedPlayer.initialize(geometry,townSpawn(geometry.terrain()),installedWorld().waterHeight)
        ||!checkedPlayer.restore(pose)) {error="Saved player position is unavailable.";return false;}
    session_=std::move(next);queries_=std::move(geometry);
    if(!player_.restore(pose)){error="Saved player position is unavailable.";return false;}
    discontinuity_=true;lastPlaced_=0;status_="Welcome home. Your adventure is restored.";saveStatus_="Saved adventure loaded";savedRevision_=state().revision;refreshHud();return true;
}
}

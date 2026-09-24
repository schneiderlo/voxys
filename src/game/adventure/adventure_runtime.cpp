#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/building_doors.hpp"
#include "game/adventure/ldraw_blacksmith_ground_remainder.hpp"
#include "game/adventure/cannon_physics_scene.hpp"
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
#include <chrono>
#include <cfenv>
#include <locale>
#include <limits>
#include <iomanip>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#include <unordered_map>
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
constexpr std::array<std::pair<const char*,uint32_t>,7> creativeColours{{
    {"Original",0},{"Red",0xe53b33},{"Yellow",0xffd83d},{"Green",0x3ba85c},
    {"Blue",0x2d91cc},{"White",0xf3f2eb},{"Graphite",0x354450}}};
AdventureGuideCard creativeGuideCard(AdventureGuideTopic topic,const AdventurePreferences& preferences,bool gamepad) {
    // Creative uses the same six-card menu machinery, with its own short tips.
    // Keeping each body small preserves the full text on narrow, enlarged HUDs.
    switch(topic) {
    case AdventureGuideTopic::Movement:return {"Move and look",gamepad
        ?"Left stick moves; right stick looks. A jumps. In water, triggers rise and dive."
        :preferences.orbitToggle?"WASD moves; Shift runs. Right-click toggles look. In water, Space rises and X dives."
        :"WASD moves; Shift runs. Right-drag looks. Space jumps. In water, Space rises and X dives."};
    case AdventureGuideTopic::Building:return {"Build with bricks",gamepad
        ?"View builds. Y picks pieces; shoulders change groups. A places, X rotates, B removes."
        :"B builds. Tab picks pieces; Q/E changes groups. Click places. R rotates; Ctrl+Z undoes."};
    case AdventureGuideTopic::Home:return {"Colour and height",gamepad
        ?"L3 opens colours. D-pad chooses; A applies. While building, D-pad up/down changes height."
        :"P opens colours. Arrows choose; Enter applies. Page Up/Down changes placement height."};
    case AdventureGuideTopic::Quests:return {"Ride a motorbike",gamepad
        ?"Menu: Ride motorbike. Left stick drives and steers; A brakes. Stop before getting off."
        :"M rides or gets off. W/S drives and brakes. A/D steers. Space brakes. Start on dry ground."};
    case AdventureGuideTopic::Combat:return {"Try the cannon",gamepad
        ?"Menu: Use cannon. Left stick aims. A fires. Menu leaves. Wait for each shot to land."
        :"C enters or leaves. A/D turns; W/S aims. Click or Space fires. Wait for each shot to land."};
    case AdventureGuideTopic::Saving:case AdventureGuideTopic::Count:return {"Save your build",gamepad
        ?"Menu pauses. Choose Save build. Comfort and controls has text size and contrast."
        :"Save from Menu or press F5. Escape pauses. Comfort and controls has text size and contrast."};
    }
    return {};
}
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
std::optional<moto::VmeshData> installedMotorbikeMesh(std::string& error) {
    constexpr size_t expectedBytes=573429;
    constexpr std::string_view digest="bcd29e0eb7037c483301d5cd6127a6bea7bc1de3b912166a2ed0cef6fbeb7e4e";
    std::ifstream file(installedPath("data/adventure/motorbike-r01/motorbike.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed motorbike is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed motorbike does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=4||mesh.nodes.size()!=4) {error="Unsupported motorbike layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedCreativePropsMesh(std::string& error) {
    constexpr size_t expectedBytes=1076559;
    constexpr std::string_view digest="a09287c7f880885f244b1fa3ed7d9cf7b589b88695e64774b239db251ec3b736";
    std::ifstream file(installedPath("data/adventure/creative-props-r01/creative-props.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed creative scenery is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed creative scenery does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=6||mesh.nodes.size()!=6) {error="Unsupported creative scenery layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedCreativeVillageMesh(std::string& error) {
    constexpr size_t expectedBytes=15529402;
    constexpr std::string_view digest="7c3703c12d6b5bcf1a384c21a98de5376e36ff24033dfeceb7d621bdc8f3242f";
    std::ifstream file(installedPath("data/adventure/creative-village-r01/creative-village.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed creative scenery is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed creative scenery does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=15||mesh.nodes.size()!=15) {error="Unsupported creative scenery layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedCreativePropsFarMesh(std::string& error) {
    constexpr size_t expectedBytes=384279;
    constexpr std::string_view digest="2e8440bbc5aab754440b60e035ffa1af8a1127ecffa6026f6202e052cc68b061";
    std::ifstream file(installedPath("data/adventure/creative-props-far-r01/creative-props.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed creative scenery is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed creative scenery does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=6||mesh.nodes.size()!=6) {error="Unsupported creative scenery layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedCreativePropsHorizonMesh(std::string& error) {
    constexpr size_t expectedBytes=204351;
    constexpr std::string_view digest="dfcd25fb7b3dbb7d419caaf62ba0b0efc64021ffe6c24b07b2f6eddda3dcd8fa";
    std::ifstream file(installedPath("data/adventure/creative-props-horizon-r01/creative-props.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed creative scenery is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed creative scenery does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=6||mesh.nodes.size()!=6) {error="Unsupported creative scenery layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedForestMesh(uint32_t lod,std::string& error) {
    constexpr std::array<size_t,4> sizes{6641643,310227,348059,61198};
    constexpr std::array<std::string_view,4> digests{"212969ccf052eb54f281028a57c9f93d3d1b9fb1b70133a32a99782cc95a91f6","fa793839518dfd7abbfefc02223b3d79087a8caf751b77e3203dcf082cead04b","9ca6371dc633831762b198da312ceeb3e96707b5a53848ff10385544e06c7aed","f9435df0496fde14b5d70f9f2e7b68b487a7ef56839415a1d8eeda88710d5083"};
    if(lod>=sizes.size())return {};
    std::ifstream file(installedPath("data/adventure/forest-r02/forest-lod"+std::to_string(lod)+".vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(sizes[lod]+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(sizes[lod])) {
        error="The installed forest is missing or has changed.";return {};
    }
    bytes.resize(sizes[lod]);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digests[lod]) {
        error="The installed forest does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=6||mesh.nodes.size()!=6) {error="Unsupported forest layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedShadowProxyMesh(std::string& error) {
    constexpr size_t expectedBytes=7907323;
    constexpr std::string_view digest="591c0667bc3cde0ffe5e0c8800ad078a6386e8b5565e022c408595a49957ec62";
    std::ifstream file(installedPath("data/adventure/shadow-proxies-r01/shadow-proxies.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed distant building shadows are missing or have changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed distant building shadows do not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=16||mesh.nodes.size()!=16) {error="Unsupported distant building shadow layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedLdrawBlacksmithMesh(std::string& error) {
    // Pin the inspected material-batched derivative of the original LDraw set.
    constexpr size_t expectedBytes=19606015;
    constexpr std::string_view digest="870af9c0eac96fe04795e5b5b1848c3de8265fde57dfe879020786c7c78aa817";
    std::ifstream file(installedPath("data/adventure/ldraw-blacksmith-ground-r01/wall-parts.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed Blacksmith set is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed Blacksmith set does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=11||mesh.nodes.size()!=11) {error="Unsupported Blacksmith set layout.";return {};}
    return mesh;
}
std::optional<moto::VmeshData> installedLdrawCannonMesh(std::string& error) {
    // Pin the inspected material-batched derivative of the original LDraw set.
    constexpr size_t expectedBytes=182911;
    constexpr std::string_view digest="b325847338285be34266cb6f02b0363f113852c9ba78e9cb12db1a102aea1170";
    std::ifstream file(installedPath("data/adventure/ldraw-cannon-r01/cannon.vmesh"),std::ios::binary);
    std::vector<uint8_t> bytes(expectedBytes+1);
    if(!file||!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())).eof()
        ||file.bad()||file.gcount()!=static_cast<std::streamsize>(expectedBytes)) {
        error="The installed cannon is missing or has changed.";return {};
    }
    bytes.resize(expectedBytes);
    if(core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))))!=digest) {
        error="The installed cannon does not match this build.";return {};
    }
    moto::VmeshData mesh;
    if(!moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))return {};
    if(mesh.header.meshCount!=2||mesh.nodes.size()!=2) {error="Unsupported cannon layout.";return {};}
    return mesh;
}
// Separate ID range from village groups (1,500,000+) and world props (<1,050,000).
constexpr uint64_t blacksmithPartId=creativeSceneryPartBase-2000000u;
constexpr uint64_t blacksmithWallPartId=creativeSceneryPartBase-2000002u;
constexpr uint64_t cannonPartId=creativeSceneryPartBase-2000001u;
constexpr glm::dvec2 cannonPlot(1240,-1027);
constexpr glm::dvec3 cannonMinimum(-4.4,0,-4.4),cannonMaximum(4.4,5.3,4.4);
constexpr glm::dvec3 blacksmithMinimum(-20.250372,0,-19.394726);
constexpr glm::dvec3 blacksmithMaximum(20.250372,34.799988,19.394724);
constexpr glm::dvec2 blacksmithPlot(1208,-1032);
bool blacksmithSolid(const AdventureSpatialQueries::Solid& solid) {
    return solid.structure.counter==creativeSceneryStructureId&&(solid.part.counter==blacksmithPartId||solid.part.counter==blacksmithWallPartId);
}
bool intersects(const AdventureSpatialQueries::Solid& a,const AdventureSpatialQueries::Solid& b,double margin=0) {
    return glm::all(glm::lessThan(a.minimum,b.maximum+glm::dvec3(margin)))
        &&glm::all(glm::greaterThan(a.maximum,b.minimum-glm::dvec3(margin)));
}
AdventureSpatialQueries::Solid blacksmithBox(glm::dvec3 feet,glm::dvec3 lo,glm::dvec3 hi,
    construction::WorldNamespace world={}) {
    // The original set faces +Z. Turn its front toward the arrival path (-Z).
    return {{world,creativeSceneryStructureId},{world,blacksmithPartId},
        feet+glm::dvec3(-hi.x,lo.y,-hi.z),feet+glm::dvec3(-lo.x,hi.y,-lo.z)};
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
// Existing creative archives can place the old smaller figure under a low
// roof or beside a stud. Keep every brick; find a nearby supported position
// for the larger body only when its original position no longer fits.
std::optional<glm::dvec3> creativeStandingPoint(const AdventureSpatialQueries& queries,glm::dvec3 preferred) {
    constexpr double radius=AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale;
    constexpr double height=AdventurePlayer::height*AdventurePlayer::creativeScale;
    for(int ring=0;ring<=4;++ring) {
        const int count=ring?8:1;
        for(int i=0;i<count;++i) {
            const double angle=double(i)*std::numbers::pi/4;
            const auto xz=glm::dvec2(preferred.x,preferred.z)
                +glm::dvec2(std::cos(angle),std::sin(angle))*double(ring*2);
            std::array<double,64> levels{};
            const auto found=queries.walkableFeet(xz,preferred.y-2,preferred.y+16,levels,radius,height);
            if(!found.complete||!found.count)continue;
            auto closest=levels[0];
            for(size_t j=1;j<found.count;++j)
                if(std::abs(levels[j]-preferred.y)<std::abs(closest-preferred.y))closest=levels[j];
            return glm::dvec3(xz.x,closest,xz.y);
        }
    }
    return {};
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
AdventureRuntime::AdventureRuntime(bool freeBuild):routingPreferences_(adventureRoutingPreferences(preferences_)),freeBuild_(freeBuild){
    if(freeBuild_) {
        auto settings=orbit_.settings();settings.distanceLimit=expedition::CoveCamera::extendedMaximumDistance;
        (void)orbit_.settings(settings);(void)orbit_.setUserDistance(10.5);
    }
}
AdventureRuntime::~AdventureRuntime(){
    if(physics_){cannon_.clear(*physics_);brickThrower_.retire(*physics_,true);playerPhysics_.clear(*physics_);}
    // Application destroys its PhysicsWorld later in the same shutdown, with
    // no intervening simulation. It owns the pool and all body references;
    // dropping these plain bridge handles is abandonment, not certified retirement.
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
    saveStatus_=std::move(status);saveFeedbackSeconds_=6;
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
    auto spawn=townSpawn(surface);
    if(freeBuild_)spawn.y=double(terrain::lego::supportHeight(surface,glm::vec2(spawn.x,spawn.z),
        float(AdventurePlayer::radius*AdventurePlayer::creativeScale)))+.005;
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
        // Keep the existing recovery/content contract so old saves still load
        // exactly. Only a newly created world's player starts at the new vista.
        spawn=creativeSpawn(surface,AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale);
        resetQuickSlots();catalogCategory_=1;
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
    if(session_&&freeBuild_&&!session_->updatePlayer({spawn.x,spawn.y,spawn.z,creativeStartYaw},100,error))return false;
    std::vector<AdventureSpatialQueries::Solid> markers;if(!freeBuild_)appendMarkers(world,surface,markers);
    if(!session_||!queries_.publish(markers,session_->state().revision+1)
        ||!player_.initialize(queries_,spawn,installedWorld().waterHeight,freeBuild_?creativeStartYaw:0,
            freeBuild_?AdventurePlayer::creativeScale:1,freeBuild_?AdventurePlayer::creativeRadius:AdventurePlayer::radius)) {error="Could not start on safe ground.";return false;}
    robot_=freeBuild_?assets::loadBuilderAsset(installedPath("data/adventure/builder-r01"),error)
        :assets::loadHumanAsset(installedPath("data/adventure/human-r01"),error);
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
    config.frontFace=WGPUFrontFace_CW;config.linearHdrOutput=true;config.sunShadows=true;config.farSunShadows=freeBuild_;config.toySkyGroundFill=freeBuild_;
    // Reserve expanded submesh instance records once, not logical piece count.
    // 1,024 parts (furniture capped at32), the authored village,
    // four animated figures, supplies/markers and a whole-room preview.
    config.maxInstances=freeBuild_?131072u:8192u;
    // Denser eight-stud forest cells still use shared meshes and batched draws.
    // Expanded material records grow only to the actual view's requirement.
    config.maxDrawsPerFrame=freeBuild_?524288u:8192u;
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
    if(freeBuild_) {
        const auto bike=installedMotorbikeMesh(error);
        if(!bike||!meshes_.loadMeshData(*bike))return false;
        const auto sceneryMesh=installedCreativePropsMesh(error);
        if(!sceneryMesh||!meshes_.loadMeshData(*sceneryMesh))return false;
        const auto farScenery=installedCreativePropsFarMesh(error);
        if(!farScenery||!meshes_.loadMeshData(*farScenery))return false;
        const auto villageScenery=installedCreativeVillageMesh(error);
        if(!villageScenery||!meshes_.loadMeshData(*villageScenery))return false;
        const auto blacksmith=installedLdrawBlacksmithMesh(error);
        if(!blacksmith||!meshes_.loadMeshData(*blacksmith))return false;
        double low=INFINITY,high=-INFINITY;
        for(int z=-20;z<=20;++z)for(int x=-21;x<=21;++x) {
            const double y=surface.heightAt(float(blacksmithPlot.x+x),float(blacksmithPlot.y+z));
            low=std::min(low,y);high=std::max(high,y);
        }
        if(!std::isfinite(high)||high-low>.33||low<=double(installedWorld().waterHeight)+2) {
            error="The Blacksmith set's installed plot is not level dry ground.";return false;
        }
        blacksmithFeet_=glm::dvec3(blacksmithPlot.x,high+.005,blacksmithPlot.y);
        wallSource_=loadImportedSection(installedPath("data/adventure/ldraw-blacksmith-ground-r01/wall.json"),error,
            "ab8965411b228bd78c9becf16288c09e50a139f6e4ef02ecc282346887f357ab");
        wall_=std::make_unique<ImportedWallPhysics>();
        if(!wallSource_||!wall_->initialize(*wallSource_,*blacksmithFeet_,glm::angleAxis(std::numbers::pi_v<float>,glm::vec3(0,1,0)),error))return false;
        const auto cannonMesh=installedLdrawCannonMesh(error);
        if(!cannonMesh||!meshes_.loadMeshData(*cannonMesh))return false;
        const auto horizon=installedCreativePropsHorizonMesh(error);
        if(!horizon||!meshes_.loadMeshData(*horizon))return false;
        for(uint32_t lod=0;lod<4;++lod) {
            const auto forest=installedForestMesh(lod,error);
            if(!forest||!meshes_.loadMeshData(*forest))return false;
        }
        const auto shadows=installedShadowProxyMesh(error);
        if(!shadows||!meshes_.loadMeshData(*shadows))return false;
        double cannonLow=INFINITY,cannonHigh=-INFINITY;
        for(int z=-4;z<=4;++z)for(int x=-4;x<=4;++x) {
            const double y=surface.heightAt(float(cannonPlot.x+x),float(cannonPlot.y+z));
            cannonLow=std::min(cannonLow,y);cannonHigh=std::max(cannonHigh,y);
        }
        if(std::isfinite(cannonHigh)&&cannonHigh-cannonLow<.4&&cannonLow>double(installedWorld().waterHeight)+2)
            cannonFeet_=glm::dvec3(cannonPlot.x,cannonHigh+.005,cannonPlot.y);
        // A clear shot at the source side wall, including gravity and muzzle
        // offset; the porch overhang obstructs the original front-wall setup.
        cannon_.heading=-std::numbers::pi/2;
        cannon_.yaw=-.0360332748563;cannon_.elevation=.0726767584712;
    }
#if defined(VOXY_NATIVE)
    if(!saves_->loadedBytes().empty()&&!restore(saves_->loadedBytes(),world,error))return false;
#else
    if(pendingBootstrap) {
        const auto bootstrap=std::exchange(pendingBootstrap,{});
        if(!bootstrap->bytes.empty()&&!restore(bootstrap->bytes,world,error))return false;
    }
#endif
    AdventureSpatialQueries populated;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;
    if(!prepareGeometry(state(),populated,residents,village,sites,scenery,error))return false;
    AdventureSpatialQueries actors;AdventureEncounters encounters;
    if(!prepareActors(state(),populated,actors,encounters,error))return false;
    ++staticGeometryEpoch_;walkQueries_=std::move(populated);queries_=std::move(actors);encounters_=std::move(encounters);
    town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);scenery_=std::move(scenery);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
    if(!freeBuild_&&!combat_.initialize(content_,walkQueries_,installedWorld().waterHeight)) {error="The trail navigation is unavailable.";return false;}
    for(size_t i=0;i<residentFacing_.size();++i)residentFacing_[i]=town_.entries()[i].yaw;
    if(freeBuild_) {
        const auto p=player_.feet()+glm::dvec3(std::cos(player_.facingYaw())*3.2,0,-std::sin(player_.facingYaw())*3.2);
        (void)motorbike_.place(queries_,p,player_.facingYaw(),installedWorld().waterHeight);
    }
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
bool AdventureRuntime::prepareGeometry(const AdventureState& state,AdventureSpatialQueries& out,TownResidents& residents,VillageLayout& village,TrailSites& sites,CreativeScenery& scenery,std::string& error,bool preserveInstalled,bool validate) const {
    if(state.revision==UINT64_MAX){error="World revision capacity reached.";return false;}
    std::vector<AdventureSpatialQueries::Solid> solids;
    if((validate&&!validateInstalledGeometry(state,queries_,error,freeBuild_))||!compileSolids(state,solids,error))return false;
    if(freeBuild_) {
        residents={};village={};sites={};
        std::vector<AdventureSpatialQueries::Solid> swings;
        if(!compileDoorSwingSolids(state,swings,error))return false;
        auto reserved=solids;reserved.insert(reserved.end(),swings.begin(),swings.end());
        std::vector<AdventureSpatialQueries::Solid> blacksmithSolids;
        appendBlacksmith(state,blacksmithSolids,reserved,preserveInstalled);
        appendCannon(state,blacksmithSolids,reserved,preserveInstalled);
        std::vector<AdventureSpatialQueries::Solid> clearance;
        if(preserveInstalled&&motorbike_.available()) {
            const auto feet=motorbike_.state().feet;
            clearance.push_back({{},{},feet-glm::dvec3(3.5,.5,3.5),feet+glm::dvec3(3.5,6,3.5)});
        }
        scenery=CreativeScenery::admit(state,queries_.terrain(),reserved,preserveInstalled?&scenery_:nullptr,clearance);
        if(!scenery.appendSolids(state.world,solids)){error="Scenery collision capacity reached.";return false;}
        if(solids.size()+blacksmithSolids.size()>AdventureSpatialQueries::maximumSolids) {
            error="Blacksmith collision capacity reached.";return false;
        }
        // Append after the scenery's user-ID alias guard; this is another
        // installed member of that namespace, not a conflicting saved build.
        solids.insert(solids.end(),blacksmithSolids.begin(),blacksmithSolids.end());
        return out.bindTerrain(queries_.terrain())&&out.publish(solids,state.revision+1);
    }
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
bool AdventureRuntime::blacksmithVisible() const noexcept {
    return blacksmithFeet_&&std::any_of(walkQueries_.solids().begin(),walkQueries_.solids().end(),blacksmithSolid);
}
void AdventureRuntime::appendBlacksmith(const AdventureState& state,std::vector<AdventureSpatialQueries::Solid>& solids,
    std::vector<AdventureSpatialQueries::Solid>& reserved,bool preserveInstalled) const {
    if(!blacksmithFeet_)return;
    // Keep a displaced landmark absent until reload, including scenery refresh
    // and deletion of the build which displaced it. Never respawn through actors.
    if(preserveInstalled&&walkQueries_.revision()!=0&&!blacksmithVisible())return;
    const auto aliases=[](uint64_t id){return id==creativeSceneryStructureId||id==blacksmithPartId||id==blacksmithWallPartId;};
    for(const auto& structure:state.structures) {
        if(aliases(structure.id))return;
        for(const auto& part:structure.parts)if(aliases(part.id))return;
    }
    for(const auto& component:state.components)if(aliases(component.id))return;
    const auto footprint=blacksmithBox(*blacksmithFeet_,blacksmithMinimum,blacksmithMaximum,state.world);
    for(const auto& other:reserved)if(intersects(footprint,other,.5))return;
    // Physical surfaces come from the imported part geometry, including bases,
    // stair treads, walls, ceilings and roofs. Room/garden air stays traversable.
    const auto& boxes=blacksmithRemainderSolids;
    if(solids.size()+boxes.size()>AdventureSpatialQueries::maximumSolids)return;
    std::vector<AdventureSpatialQueries::Solid> collision(boxes.size());
    const bool newlyAdmitted=!preserveInstalled||walkQueries_.revision()==0;
    const glm::dvec3 player(state.player.x,state.player.y,state.player.z);
    const AdventureSpatialQueries::Solid playerClearance{{},{},player-glm::dvec3(1.12,0,1.12),player+glm::dvec3(1.12,5.7,1.12)};
    for(size_t i=0;i<boxes.size();++i) {
        collision[i]=blacksmithBox(*blacksmithFeet_,boxes[i].minimum,boxes[i].maximum,state.world);
        if(newlyAdmitted&&intersects(collision[i],playerClearance))return;
        if(newlyAdmitted&&preserveInstalled&&motorbike_.available()) {
            const auto feet=motorbike_.state().feet;
            if(intersects(collision[i],{{},{},feet-glm::dvec3(3.5,.5,3.5),feet+glm::dvec3(3.5,6,3.5)}))return;
        }
    }
    if(wall_) {
        const auto cells=wall_->ready()?wall_->settledSolids():wall_->initialSolids();
        if(solids.size()+collision.size()+cells.size()>AdventureSpatialQueries::maximumSolids)return;
        for(const auto& cell:cells) {
            AdventureSpatialQueries::Solid solid{{state.world,creativeSceneryStructureId},{state.world,blacksmithWallPartId},cell.minimum,cell.maximum};
            if(newlyAdmitted&&intersects(solid,playerClearance))return;
            if(newlyAdmitted&&preserveInstalled&&motorbike_.available()) {
                const auto feet=motorbike_.state().feet;
                if(intersects(solid,{{},{},feet-glm::dvec3(3.5,.5,3.5),feet+glm::dvec3(3.5,6,3.5)}))return;
            }
            collision.push_back(solid);
        }
    }
    solids.insert(solids.end(),collision.begin(),collision.end());
    // Also reserve their capacity before the scenery admission fills the packet.
    reserved.insert(reserved.end(),collision.begin(),collision.end());
    reserved.push_back(footprint);
}
bool AdventureRuntime::commit(std::optional<AdventureSession::PreparedChange> prepared) {
    if(!prepared)return false;
    const bool homeChanged=state().structures!=prepared->state().structures;
    AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;
    if(!prepareGeometry(prepared->state(),next,residents,village,sites,scenery,status_))return false;
    if(wall_&&wall_->released()&&!std::any_of(next.solids().begin(),next.solids().end(),blacksmithSolid)) {status_="Rebuild the wall before changing this plot.";return false;}
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
    ++staticGeometryEpoch_;walkQueries_=std::move(next);queries_=std::move(actors);encounters_=std::move(encounters);town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);scenery_=std::move(scenery);if(homeChanged)fieldHome_=fieldHomeReadiness(state(),walkQueries_);lastPlaced_=placed?placed:lastPlaced_;
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
    if(action==5) {
        // Closing Pause defers its Remove command to the next input frame.
        // Keep the displayed durable target even if the pointer then moves.
        pending.removePart=targetPart_;
        pending.removeScenery=freeBuild_&&isCreativeScenerySolid({observedRayHit_.structure,observedRayHit_.part,{},{}});
    }
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
    if(action>=1&&action<=39&&action!=26&&pendingActions_.size()<32) {
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
        previewReason_=building_?"The pointer view is unavailable.":"";return;
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
    // Preserve reach around the figure when the camera backs away. Short
    // contiguous segments retain the terrain query's bounded work per sweep.
    const double rayDistance=25.+(freeBuild_?std::max(0.,orbit_.pose().distance-10.5):0.);
    // Match exact IEEE values, including signed zero; restore clears even an
    // equal revision, and a change of zoom/reach invalidates this query too.
    const AimRayKey rayKey{state().world,state().epoch,queries_.revision(),{
        std::bit_cast<uint64_t>(eye.x),std::bit_cast<uint64_t>(eye.y),std::bit_cast<uint64_t>(eye.z),
        std::bit_cast<uint64_t>(direction.x),std::bit_cast<uint64_t>(direction.y),std::bit_cast<uint64_t>(direction.z),
        std::bit_cast<uint64_t>(rayDistance)}};
    const bool cacheRay=freeBuild_&&std::fegetround()==FE_TONEAREST;
    AdventureSpatialQueries::RayHit hit;
    if(cacheRay&&aimRayResult_&&aimRayResult_->key==rayKey)hit=aimRayResult_->hit;
    else {
        for(double travelled=0;travelled<rayDistance;travelled+=25.) {
            hit=queries_.raycast(eye+direction*travelled,direction,std::min(25.,rayDistance-travelled));
            hit.distance+=travelled;
            if(!hit.complete||hit.hit)break;
        }
        if(cacheRay)aimRayResult_=AimRayResult{rayKey,hit};
    }
    observedRayFrom_=eye;observedRayTo_=eye+direction*rayDistance;observedRayHit_=hit;
    hasTarget_=hit.complete&&hit.hit;targetPart_=hasTarget_?hit.part.counter:0;
    if(!hasTarget_){previewValid_=false;previewReason_=building_?"Aim at nearby ground or a building.":"";return;}
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
    if(hit.terrain||(freeBuild_&&isCreativeScenerySolid({hit.structure,hit.part,{},{}}))) {
        // Decorative props yield to supported building; never anchor a brick
        // to scenery that will disappear when the placement is accepted.
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
    double best=3.25*player_.bodyScale();uint64_t result=0;
    for(const auto& component:state().components)if(!freeBuild_||component.kind==FurnitureKind::Door)if(const auto* part=AdventureSession::findPart(state(),component.part)) {
        double distance=glm::length(metres(part->position)-player_.feet());
        if(component.part==targetPart_)distance-=.25;
        std::string reason;
        if(distance<best&&reachableComponent(state(),component.id,queries_,reason,freeBuild_)){best=distance;result=component.id;}
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
        if(!reachableComponent(state(),id,queries_,status_,freeBuild_))return;
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
    if(!reachableComponent(state(),bench_,queries_,status_,freeBuild_))return;
    if(compass) {
        if(commit(session_->prepareCraftCompass(stamp(),bench_,validator(),status_)))status_="Trail Compass crafted. Equip it to find your way.";
    } else if(commit(session_->prepareCraftHammer(stamp(),bench_,validator(),status_))) {
        for(uint8_t i=0;i<state().backpack.size();++i)if(state().backpack[i].kind==ItemKind::FieldHammer){
            if(commit(session_->prepareEquipTool(stamp(),i,status_)))status_="Field hammer equipped. Gather twice as much.";
            break;
        }
    }
}
void AdventureRuntime::attachPhysics(physics::PhysicsWorld& world) {
    if(!freeBuild_||physics_)return;
    physics_=&world;cannonPhysics_=std::make_unique<CannonPhysicsScene>();
    cannonEventsReady_=world.setEventReadbackEnabled(true);
    cannonEventsThrough_=world.encodedTick();
    if(!cannonEventsReady_)cannonPhysicsError_="Impact detection could not start.";
}
void AdventureRuntime::throwBricks(uint32_t count) {
    ++interactionStatusSerial_;
    if(!physics_||physics_->backendType()!=physics::BackendType::WebGpuSoft) {
        status_="Throwing bricks needs GPU physics.";return;
    }
    const auto hand=player_.feet()+glm::dvec3(0,1.2*player_.bodyScale(),0);
    const auto yaw=orbit_.pose().yaw,elevation=orbit_.pose().elevation;
    // Throw from the figure without requiring a picked surface or clear ray.
    // The GPU solver handles contacts with the surrounding scene.
    const auto spawned=brickThrower_.launch(*physics_,hand,yaw,elevation,count);
    status_=spawned==count?(count==1?"Threw a 2×1 brick.":"Threw 100 2×1 bricks.")
        :"Threw "+std::to_string(spawned)+" bricks. Wait for space in the physics world.";
}
bool AdventureRuntime::physicsWaiting() const noexcept {
    return (cannonPhysics_&&cannonPhysics_->needsQuiescentBoundary())||(wall_&&wall_->needsQuiescentBoundary());
}
bool AdventureRuntime::cannonVisible() const noexcept {
    return cannonFeet_&&std::any_of(walkQueries_.solids().begin(),walkQueries_.solids().end(),[](const auto& solid){
        return solid.structure.counter==creativeSceneryStructureId&&solid.part.counter==cannonPartId;
    });
}
void AdventureRuntime::appendCannon(const AdventureState& state,std::vector<AdventureSpatialQueries::Solid>& solids,
    std::vector<AdventureSpatialQueries::Solid>& reserved,bool preserveInstalled) const {
    if(!cannonFeet_||(preserveInstalled&&walkQueries_.revision()!=0&&!cannonVisible()))return;
    for(const auto& structure:state.structures) {
        if(structure.id==cannonPartId||structure.id==creativeSceneryStructureId)return;
        for(const auto& part:structure.parts)if(part.id==cannonPartId||part.id==creativeSceneryStructureId)return;
    }
    const AdventureSpatialQueries::Solid footprint{{state.world,creativeSceneryStructureId},{state.world,cannonPartId},
        *cannonFeet_+cannonMinimum,*cannonFeet_+cannonMaximum};
    for(const auto& other:reserved)if(intersects(footprint,other,.3))return;
    const glm::dvec3 player(state.player.x,state.player.y,state.player.z);
    if((!preserveInstalled||walkQueries_.revision()==0)&&intersects(footprint,{{},{},
        player-glm::dvec3(1.12,0,1.12),player+glm::dvec3(1.12,5.7,1.12)}))return;
    solids.push_back(footprint);reserved.push_back(footprint);
}
void AdventureRuntime::updateCannonPhysics() {
    if(!physics_||!cannonPhysics_||!cannonFeet_)return;
    updateCannonImpacts();
    const auto activeShots=cannon_.liveShots();
    cannon_.retire(*physics_);
    if(activeShots&&!cannon_.liveShots()&&(!wall_||!wall_->busy())) {
        ++interactionStatusSerial_;status_="Shot finished. Adjust your aim and fire again.";
    }
    if(!cannonVisible())usingCannon_=false;
    if(cannonObservedEpoch_!=staticGeometryEpoch_) {
        std::vector<AdventureSpatialQueries::Solid> region;
        for(const auto& solid:walkQueries_.solids()) {
            if(solid.part.counter==cannonPartId||solid.part.counter==blacksmithWallPartId)continue;
            const auto closest=glm::clamp(*cannonFeet_,solid.minimum,solid.maximum);
            if(glm::length(glm::dvec2(closest.x-cannonFeet_->x,closest.z-cannonFeet_->z))<=140.)region.push_back(solid);
        }
        const bool same=region.size()==cannonRegion_.size()&&std::equal(region.begin(),region.end(),cannonRegion_.begin(),[](const auto& a,const auto& b){
            return a.minimum==b.minimum&&a.maximum==b.maximum&&a.structure==b.structure&&a.part==b.part;
        });
        if(!same){cannonRegion_=std::move(region);++cannonGeometryEpoch_;}
        cannonObservedEpoch_=staticGeometryEpoch_;
    }
    if(cannonPreparedEpoch_!=cannonGeometryEpoch_&&cannonFailedEpoch_!=cannonGeometryEpoch_) {
        if(!cannon_.clear(*physics_))return;
        // Finish any submitted transition before changing its ownership.
        if(!cannonPhysics_->pendingRevision()) {
            cannonPhysicsError_.clear();
            if(cannonPhysics_->prepare(cannonRegion_,*cannonFeet_,cannonGeometryEpoch_,cannonPhysicsError_))cannonPreparedEpoch_=cannonGeometryEpoch_;
            else cannonFailedEpoch_=cannonGeometryEpoch_;
        }
    }
    if(cannonFailedEpoch_==cannonGeometryEpoch_) {
        if(usingCannon_)status_="Cannon unavailable: "+cannonPhysicsError_;
        return;
    }
    const auto progress=cannonPhysics_->update(*physics_,cannonPhysicsError_);
    if(progress==CannonPhysicsScene::Progress::Failed) {
        cannonFailedEpoch_=cannonGeometryEpoch_;
        if(usingCannon_)status_="Cannon unavailable: "+cannonPhysicsError_;
    }
}
void AdventureRuntime::updateCannonImpacts() {
    if(!physics_||!cannonEventsReady_)return;
    while(auto batch=physics_->pollEvents()) {
        const auto frontier=physics_->tickFrontier();
        if(batch->overflow||!batch->confirmedIncarnation||batch->confirmedIncarnation!=frontier.incarnation
            ||batch->tick!=cannonEventsThrough_+1||batch->tick>frontier.completed) {
            cannonEventsReady_=false;wallQueryFailure_=true;
            status_="Impact detection stopped safely. Reload the world.";return;
        }
        cannonEventsThrough_=batch->tick;
        for(const auto& event:batch->events)if(event.tick==batch->tick)
            player_.addContactImpulse(playerPhysics_.contactImpulse(event,brickThrower_));
        if(!wall_||!wall_->ready()||wallQueryFailure_)continue;
        // A compound can produce several exterior patches for the same shot.
        // Prefer its strongest patch, then transfer ownership of the shot once.
        std::stable_sort(batch->events.begin(),batch->events.end(),[](const auto& a,const auto& b){
            const auto strength=[](const auto& e){return std::isfinite(e.impulse)?e.impulse:0.f;};
            return strength(a)>strength(b);
        });
        for(const auto& event:batch->events) {
            if(event.type==physics::PhysicsEventType::ContactHit)for(const auto& shot:cannon_.shots) {
                const bool a=shot.body.valid()&&shot.body==event.bodyHandleA();
                if(a||(shot.body.valid()&&shot.body==event.bodyHandleB())) {
                    ++cannonContacts_;cannonContactFeature_=a?event.otherFeatureId:event.featureId;
                    cannonContactBody_=a?event.bodyB:event.bodyA;
                    cannonContactSpeed_=event.impactSpeed;cannonContactImpulse_=event.impulse;
                }
            }
            if(event.type!=physics::PhysicsEventType::ContactHit||event.tick!=batch->tick
                ||!std::isfinite(event.impactSpeed)||event.impactSpeed<8
                ||!std::isfinite(event.impulse)||event.impulse<=0)continue;
            for(auto& shot:cannon_.shots) {
                if(!shot.body.valid())continue;
                const bool ballA=shot.body==event.bodyHandleA();
                if(!ballA&&shot.body!=event.bodyHandleB())continue;
                const auto target=ballA?event.bodyHandleB():event.bodyHandleA();
                const auto feature=ballA?event.otherFeatureId:event.featureId;
                if(!wall_->contactPart(target,feature))continue;
                ImportedWallPhysics::Impact impact;
                impact.geometryRevision=wall_->geometryRevision();impact.target=target;
                impact.projectile=shot.body;impact.targetFeature=feature;
                impact.targetLocalPoint=ballA?event.localAnchorB:event.localAnchorA;
                impact.direction=ballA?event.normalAtoB:-event.normalAtoB;
                impact.normalImpulse=event.impulse;impact.closingSpeed=event.impactSpeed;
                impact.projectileMass=2.;
                impact.projectileEnergy=BuilderCannon::shotSpeed*BuilderCannon::shotSpeed;
                std::string error;
                if(wall_->impact(impact,error)) {
                    // The wall's atomic replacement now owns retirement. Never
                    // expire or destroy this projectile twice via the shot list.
                    shot={};++cannonImpacts_;++interactionStatusSerial_;
                    wallInspectionPoint_=wall_->impactStats().worldPoint;
                    inspectWall_=true;status_="Hit! Bricks are breaking free.";
                } else if(!error.empty()) {++interactionStatusSerial_;status_=error;}
                break;
            }
            if(!wall_->ready())break;
        }
    }
}
bool AdventureRuntime::wallLocked() const noexcept {
    return wall_&&(wallQueryFailure_||(wall_->released()&&wall_->busy()));
}
void AdventureRuntime::updateImportedWall() {
    if(!physics_||!wall_||!wallSource_||!blacksmithFeet_)return;
    if(physics_->tickFrontier().failed) {
        std::string failure;
        static_cast<void>(wall_->update(*physics_,failure));
        wallQueryFailure_=true;
        status_=failure.empty()?"Physics stopped safely. Reload the world to restore the house.":failure;
        return;
    }
    if(!blacksmithVisible()) {
        if(!wall_->empty())wall_->requestClear();
    } else if(wall_->empty()) {
        wall_=std::make_unique<ImportedWallPhysics>();wallGeometryRevision_=0;
        if(!wall_->initialize(*wallSource_,*blacksmithFeet_,glm::angleAxis(std::numbers::pi_v<float>,glm::vec3(0,1,0)),status_))return;
    }
    std::string error;
    if(!wall_->update(*physics_,error)) {if(!error.empty())status_=error;return;}
    if(wall_->ready()&&wall_->geometryRevision()!=wallGeometryRevision_) {
        AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;
        if(!prepareGeometry(state(),next,residents,village,sites,scenery,error)) {wallQueryFailure_=true;status_=error;return;}
        // Scenery admission may refuse a complete house for capacity/clearance.
        // Never publish that omission as if it were a successful wall update.
        if(!std::any_of(next.solids().begin(),next.solids().end(),blacksmithSolid)) {
            wallQueryFailure_=true;status_="Wall collision could not be installed. Rebuild the wall.";return;
        }
        walkQueries_=next;queries_=std::move(next);scenery_=std::move(scenery);++staticGeometryEpoch_;
        aimRayResult_.reset();previewResult_.reset();wallGeometryRevision_=wall_->geometryRevision();wallQueryFailure_=false;
        if(wall_->released())status_="The pieces have settled. C: leave the cannon.";
    }
}
void AdventureRuntime::changeImportedWall(bool rebuild, bool removeSupport) {
    ++interactionStatusSerial_;
    if(!usingCannon_||!wall_||!blacksmithVisible()||!physics_)return;
    if(physics_->tickFrontier().failed) {
        status_="Physics stopped safely. Reload the world to restore the house.";
        return;
    }
    if(!cannon_.clear(*physics_)){status_="Wait for the shot to finish.";return;}
    if(rebuild) {
        // Never restore source masonry through a player-built part.
        std::vector<AdventureSpatialQueries::Solid> owned;
        if(!compileSolids(state(),owned,status_))return;
        for(const auto& cell:wall_->initialSolids())for(const auto& solid:owned)
            if(intersects(solid,{{},{},cell.minimum,cell.maximum})) {status_="Move your bricks away from the wall before rebuilding it.";return;}
        if(wall_->reset(status_)){inspectWall_=false;status_="Rebuilding the wall…";}
    } else if(cannonPhysics_&&cannonPhysics_->ready(cannonGeometryEpoch_)) {
        // Stable source floor plate supporting side-wall masonry. Removing a
        // shared meshNode would hide unrelated instances of the same part.
        constexpr uint64_t supportPart=30700057810174424ull;
        if(wall_->release(status_,removeSupport?std::optional<uint64_t>(supportPart):std::nullopt)) {
            glm::dvec3 localCenter{};
            for(const auto& part:wallSource_->parts)localCenter+=part.translation;
            localCenter/=double(wallSource_->parts.size());
            wallInspectionPoint_=*blacksmithFeet_+glm::dvec3(-localCenter.x,localCenter.y,-localCenter.z);
            inspectWall_=true;
            status_=removeSupport?"Support brick removed. Wait while the pieces settle.":"Connections released. Bearing bricks may remain standing.";
        }
    }
}
void AdventureRuntime::toggleCannon() {
    ++interactionStatusSerial_;
    if(usingCannon_&&cannon_.liveShots()){status_="Wait for the cannonball to land.";return;}
    if(usingCannon_&&wallLocked()){status_="Wait for the pieces to settle, or rebuild the wall.";return;}
    if(usingCannon_){usingCannon_=false;inspectWall_=false;discontinuity_=true;status_="Left the cannon. B: build · M: ride";return;}
    if(!freeBuild_||!cannonVisible()){status_="There is no cannon on this plot.";return;}
    if(!physics_){status_="The cannon needs the GPU physics world.";return;}
    if(riding_){status_="Press M to get off before using the cannon.";return;}
    if(glm::length(player_.feet()-*cannonFeet_)>=12) {
        // The distant HUD action is explicitly labelled Visit cannon. Free
        // Build travel still requires a supported, unobstructed landing spot.
        const auto heading=cannon_.heading+cannon_.yaw;
        const glm::dvec3 back(-std::sin(heading),0,-std::cos(heading));
        bool landed=false;
        for(const auto offset:{back*8.,glm::dvec3(8,0,0),glm::dvec3(-8,0,0),glm::dvec3(0,0,-8)}) {
            auto feet=*cannonFeet_+offset;
            feet.y=queries_.supportHeight({feet.x,feet.z},AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,cannonFeet_->y+3)+.005;
            if(!queries_.clearCapsule(feet,AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,1.7*AdventurePlayer::creativeScale))continue;
            const auto before=player_.state();auto pose=before;pose.feet=feet;pose.velocity={};pose.mode=AdventurePlayer::Mode::Walking;
            if(!player_.restore(pose))continue;
            std::string error;
            if(!session_->updatePlayer({feet.x,feet.y,feet.z,pose.facingYaw},state().health,error)) {
                (void)player_.restore(before);continue;
            }
            landed=true;discontinuity_=true;break;
        }
        if(!landed){status_="There is no clear place beside the cannon. Walk closer.";return;}
    }
    if(player_.mode()!=AdventurePlayer::Mode::Walking){status_="Stand on the ground to use the cannon.";return;}
    usingCannon_=true;building_=false;hasTarget_=false;player_.discardPendingInput();
    status_=hudHoverFromMouse_?"A/D: turn · W/S: elevation · Click or Space: fire · C: leave"
        :"Left stick: aim · A: fire · Menu: leave";
}
void AdventureRuntime::fireCannon() {
    ++interactionStatusSerial_;
    if(!cannonEventsReady_){status_="Impact detection is unavailable. Reload the world.";return;}
    if(cannon_.liveShots()){status_="Wait for the cannonball to land.";return;}
    if(wall_&&(!wall_->ready()||wallQueryFailure_)){status_="Wait for the wall to settle, or rebuild it.";return;}
    if(!usingCannon_||!physics_||!cannonFeet_||!cannonPhysics_||!cannonPhysics_->ready(cannonGeometryEpoch_)) {
        status_=cannonPhysicsError_.empty()?"Preparing cannon collisions…":"Cannon unavailable: "+cannonPhysicsError_;return;
    }
    const auto desc=cannon_.projectile(*cannonFeet_);
    const auto center=physics::worldPositionToAbsolute({desc.sector,desc.position});
    // Ignore only the conservative cannon guard; actual source muzzle lies
    // outside its barrel. Everything else must leave room for the whole ball.
    for(const auto& solid:walkQueries_.solids()) {
        if(solid.part.counter==cannonPartId)continue;
        const auto nearest=glm::clamp(center,solid.minimum,solid.maximum);
        if(glm::length(center-nearest)<double(BuilderCannon::ballRadius)){status_="The muzzle is blocked.";return;}
    }
    if(center.y-double(BuilderCannon::ballRadius)<double(walkQueries_.terrain().heightAt(float(center.x),float(center.z)))) {
        status_="Raise the barrel clear of the ground.";return;
    }
    const bool fired=cannon_.fire(*physics_,*cannonFeet_);
    if(fired)inspectWall_=false;
    status_=fired?"Fired."
        :physics_->encodedTick()<cannon_.nextShotTick?"Reloading…":"Wait for a cannonball to clear.";
}
void AdventureRuntime::toggleMotorbike() {
    ++interactionStatusSerial_;
    if(!freeBuild_)return;
    if(usingCannon_){status_="Press C to leave the cannon first.";return;}
    if(riding_) {
        const auto landing=motorbike_.dismount(queries_,installedWorld().waterHeight);
        if(!landing){status_=std::abs(motorbike_.state().speed)>2?"Brake before getting off.":"Move to clear, level ground to get off.";return;}
        auto next=player_.state();next.feet=*landing;next.velocity={};next.mode=AdventurePlayer::Mode::Walking;
        if(!player_.restore(next)){status_="There is no safe place to get off here.";return;}
        motorbike_.pause();riding_=false;character_.reset();
        (void)orbit_.setUserDistance(10.5);status_="Off the motorbike. B: build · M: ride";
    } else {
        if(player_.mode()!=AdventurePlayer::Mode::Walking){status_="Stand on dry ground to ride.";return;}
        auto next=motorbike_;
        if(!next.place(queries_,player_.feet(),player_.facingYaw(),installedWorld().waterHeight)) {
            status_="The motorbike needs clear, dry ground with room ahead and behind.";return;
        }
        auto proxy=player_.state();proxy.feet=next.state().feet;proxy.velocity={};proxy.mode=AdventurePlayer::Mode::Airborne;
        if(!player_.restore(proxy)){status_="Move to clearer ground to ride.";return;}
        motorbike_=next;riding_=true;building_=false;hasTarget_=false;
        (void)orbit_.setUserDistance(12.);status_="W/S: accelerate / brake and reverse · A/D: steer · Space: brake · M: get off";
    }
    player_.discardPendingInput();discontinuity_=true;
}
void AdventureRuntime::recover() {
    if(cannon_.liveShots()){status_="Wait for the cannonball to land.";return;}
    if(wallLocked()){status_="Rebuild the wall before returning home.";return;}
    if(usingCannon_)toggleCannon();
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
    if(!checked.initialize(queries_,pose.feet,installedWorld().waterHeight,pose.facingYaw,player_.bodyScale(),player_.bodyRadius()/player_.bodyScale())) {
        status_="No safe recovery point is available.";return;
    }
    if(!commit(std::move(prepared)))return;
    (void)player_.restore(pose);swimAnimation_.reset();motorbike_.reset();riding_=false;combat_.reset();combatSeconds_=0;
    pendingAttack_=false;pendingDodge_=false;pendingJump_=false;discontinuity_=true;
    status_="Recovered safely. Your buildings and items are kept.";menu_=Menu::None;building_=false;
}
std::string_view AdventureRuntime::mode() const {
    switch(menu_) {
    case Menu::None:return building_?"build":"explore";
    case Menu::Main:return "pause";
    case Menu::Catalog:return "catalog";
    case Menu::Colours:return "colours";
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
    if(building_){building_=false;if(freeBuild_)status_.clear();player_.discardPendingInput();return true;}
    return false;
}
void AdventureRuntime::toggleColours() {
    if(!freeBuild_||!building_||riding_||usingCannon_||wallLocked())return;
    if(menu_==Menu::Colours){(void)closeCurrentMode();return;}
    if(menu_!=Menu::None)return;
    menu_=Menu::Colours;menuSelection_=0;
    for(size_t i=0;i<creativeColours.size();++i)
        if(creativeColours[i].second==selectedPaint_)menuSelection_=static_cast<int>(i);
    player_.discardPendingInput();
}
void AdventureRuntime::resetQuickSlots() {
    // These are session presentation preferences. Placed parts still serialize
    // their real kind/paint; loading a world starts a fresh six-slot tray.
    quickSlots_={QuickSlot{PieceKind::Brick2x2,0xe53b33},QuickSlot{PieceKind::Brick2x4,0xf3f2eb},
        QuickSlot{PieceKind::Floor,0},QuickSlot{PieceKind::Foundation,0x3ba85c},
        QuickSlot{PieceKind::Doorway,0xf3f2eb},QuickSlot{PieceKind::HingedDoor,0}};
    selectQuickSlot(0);
}
void AdventureRuntime::selectQuickSlot(size_t slot) {
    if(slot>=quickSlots_.size())return;
    activeQuickSlot_=slot;selected_=quickSlots_[slot].kind;selectedPaint_=quickSlots_[slot].paint;
    blueprint_=BlueprintKind::None;heightSteps_=0;building_=true;menu_=Menu::None;
}
void AdventureRuntime::rememberQuickSlot() {
    if(freeBuild_)quickSlots_[activeQuickSlot_]={selected_,selectedPaint_};
}
void AdventureRuntime::undoLastPlacement() {
    if(lastBlueprint_?commit(session_->prepareRemoveStructure(stamp(),lastBlueprint_,validator(),status_))
        :lastPlaced_&&commit(session_->prepareRemove(stamp(),lastPlaced_,validator(),status_))) {
        lastPlaced_=0;lastBlueprint_=0;status_=freeBuild_?"Last placement undone.":"Last placement undone. Materials returned.";
    }
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
    case MenuOperation::Catalog:if(wallLocked()){status_="Rebuild the wall before building.";break;}if(usingCannon_)toggleCannon();if(riding_){status_="Press M to get off before building.";break;}building_=true;menu_=Menu::Catalog;menuSelection_=0;break;
    case MenuOperation::Save:saveRequested_=true;saveStatus_="Saving...";break;
    case MenuOperation::Recover:recover();break;
    case MenuOperation::Settings:menu_=Menu::Settings;menuSelection_=0;break;
    case MenuOperation::GuideTopics:openGuide(true,guideTopic_);break;
    case MenuOperation::GuideTopic:
        if(command.argument<static_cast<uint64_t>(AdventureGuideTopic::Count))
            openGuide(false,static_cast<AdventureGuideTopic>(command.argument));
        break;
    case MenuOperation::GuideExit:(void)closeCurrentMode();break;
    case MenuOperation::Paint:
        if(freeBuild_&&menu_==Menu::Colours&&command.argument<=0xffffff) {
            selectedPaint_=static_cast<uint32_t>(command.argument);rememberQuickSlot();(void)closeCurrentMode();
        }
        break;
    case MenuOperation::Motorbike:menu_=Menu::None;toggleMotorbike();break;
    case MenuOperation::Cannon:menu_=Menu::None;toggleCannon();break;
    case MenuOperation::Undo:undoLastPlacement();break;
    case MenuOperation::Rotate:case MenuOperation::Raise:case MenuOperation::Lower:
    case MenuOperation::Remove:case MenuOperation::Colours:case MenuOperation::FinishBuilding: {
        if(!freeBuild_||!building_||riding_||usingCannon_||wallLocked())break;
        if(command.operation==MenuOperation::Remove&&targetPart_!=command.argument)break;
        const int control=command.operation==MenuOperation::Rotate?3:command.operation==MenuOperation::Remove?5
            :command.operation==MenuOperation::Colours?38:command.operation==MenuOperation::FinishBuilding?22:12;
        const int value=command.operation==MenuOperation::Raise?1:command.operation==MenuOperation::Lower?-1:0;
        (void)closeCurrentMode();refreshHud();action(control,value);
        break;
    }
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
    case MenuOperation::Starter:if(wallLocked()){status_="Rebuild the wall before building.";break;}if(usingCannon_)toggleCannon();selectBlueprint(BlueprintKind::StarterRoom);break;
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
    case MenuOperation::SelectPiece:blueprint_=BlueprintKind::None;selected_=static_cast<PieceKind>(command.argument);rememberQuickSlot();heightSteps_=0;building_=true;menu_=Menu::None;break;
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
    // Stream before movement. Draws/colliders change together, using the next
    // accepted geometry revision; cached picks/preview keys therefore expire.
    // Accepted buildings need no repeated terrain/support graph validation.
    if(freeBuild_&&scenery_.needsRefresh({state().player.x,state().player.z})) {
        AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;std::string error;
        if(prepareGeometry(state(),next,residents,village,sites,scenery,error,true,false)) {
            scenery_=std::move(scenery);++staticGeometryEpoch_;walkQueries_=next;queries_=std::move(next);
            aimRayResult_.reset();previewResult_.reset();structureJson_.reset();
        } else status_=error;
    }
    menuIntents_.beginFrame();
    const double residentSeconds=std::isfinite(seconds)?std::clamp(seconds,0.,.25):0.;
    const auto previousInteraction=interactionStatusSerial_;
    saveFeedbackSeconds_=std::max(0.,saveFeedbackSeconds_-residentSeconds);
    interactionFeedbackSeconds_=std::max(0.,interactionFeedbackSeconds_-residentSeconds);
    hudPixelScale_=1;
    if(logicalPointerExtent.x>0&&logicalPointerExtent.y>0
        &&std::isfinite(logicalPointerExtent.x)&&std::isfinite(logicalPointerExtent.y)) {
        // Layout is authored in window/CSS pixels; the renderer emits physical
        // framebuffer coordinates. Use the larger ratio so small independent
        // dimension rounding cannot make either target axis smaller than 44px.
        const double scale=std::max(double(width)/logicalPointerExtent.x,double(height)/logicalPointerExtent.y);
        if(scale>0&&std::isfinite(scale)&&scale<=static_cast<double>(std::numeric_limits<float>::max()))hudPixelScale_=static_cast<float>(scale);
    }
    framePointer_=adventurePointer(glm::dvec2(input.mousePosition()),logicalPointerExtent,{width,height});
    bool sharedHudVisible=true;
#if !defined(VOXY_NATIVE)
    // Creative play uses the same GPU HUD and hit regions on both platforms.
    // Only the legacy adventure UI replaces the rendered controls with DOM.
    sharedHudVisible=freeBuild_||!domUiAttached_;
#endif
    hudPointerOwned_=false;hudHover_.reset();bool hudClicked=false;
    if(sharedHudVisible&&framePointer_) {
        // The last encoded HUD is still the visible image during a resize.
        // Hit-test its extent; world picking independently uses the new frame.
        const auto hudPointer=adventurePointer(glm::dvec2(input.mousePosition()),logicalPointerExtent,
            {hudWidth_?hudWidth_:width,hudHeight_?hudHeight_:height});
        const glm::vec2 pointer(hudPointer?hudPointer->framebuffer:framePointer_->framebuffer);
        const auto inside=[&](glm::vec4 bounds){return pointer.x>=bounds.x&&pointer.y>=bounds.y&&pointer.x<bounds.x+bounds.z&&pointer.y<bounds.y+bounds.w;};
        // Navigation has its own cached GPU pass. Its visible map/portrait
        // surfaces still consume pointer input just like ordinary HUD panels.
        hudPointerOwned_=hud_.navigationContains(pointer);
        hudClicked=hudPointerOwned_&&input.wasMouseButtonPressed(MouseButton::Left);
        for(const auto panel:hud_.layout().panels)hudPointerOwned_=hudPointerOwned_||inside(panel);
        // Floating creative controls have no enclosing panel. Their hit boxes
        // still own hover, wheel and drag input, even when currently disabled.
        for(const auto& hit:hud_.layout().hits)if(inside(hit.bounds)) {
            hudPointerOwned_=true;if(!hudHover_)hudHover_=hit;
        }
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
    const glm::dvec2 hoverPointer(input.mousePosition());
    if(hoverPointer!=hudHoverPointer_||input.wasMouseButtonPressed(MouseButton::Left)||input.wasMouseButtonPressed(MouseButton::Right)
        ||std::any_of(sample.pressed.begin(),sample.pressed.end(),[](bool v){return v;}))hudHoverFromMouse_=true;
    else if(sample.padConnected&&(std::any_of(sample.padPressed.begin(),sample.padPressed.end(),[](bool v){return v;})
        ||std::any_of(sample.axes.begin(),sample.axes.end(),[](float v){return std::abs(v)>.01f;})))hudHoverFromMouse_=false;
    hudHoverPointer_=hoverPointer;
    auto movementSample=adventureMovementSample(sample,building_,freeBuild_);
    if(hudPointerOwned_){movementSample.rightPressed=false;movementSample.mouseRight=false;}
    inputRouter_.tick(movementSample,menu_==Menu::None&&!uiInputOwned_?C::World:C::Menu,routingPreferences_);
    buildRouter_.tick(sample,menu_==Menu::None&&building_&&!uiInputOwned_?C::Workshop:C::Menu,routingPreferences_);
    const auto& pad=input.gamepad();
    auto pressed=[&](Key k){return input.wasKeyPressed(k);};
    if(freeBuild_&&building_&&menu_==Menu::None&&input.focused()&&!uiInputOwned_&&sample.modifiers==0) {
        // The displayed window can change with selection, text size or viewport.
        // Consume its exact published slot; never maintain a second slot order.
        for(const auto& hit:hud_.layout().hits)if(hit.enabled&&hit.shortcutKey>=uint32_t('1')&&hit.shortcutKey<=uint32_t('6')
            &&hit.intent&&(hit.intent-1u)/64u==menuIntents_.token()&&pressed(static_cast<Key>(hit.shortcutKey))) {
            if(pendingActions_.size()<32)pendingActions_.push_back(observedAction(hit.action,hit.value,(hit.intent-1u)/64u));
            hudClicked=true;break;
        }
    }
    bool dismissedMenu=false;
    if(menu_==Menu::Guide||menu_==Menu::GuideTopics) {
        if(sample.padConnected&&std::any_of(sample.padPressed.begin(),sample.padPressed.end(),[](bool p){return p;}))guideGamepad_=true;
        else if(std::any_of(sample.pressed.begin(),sample.pressed.end(),[](bool p){return p;})
            ||input.wasMouseButtonPressed(MouseButton::Left)||input.wasMouseButtonPressed(MouseButton::Right))guideGamepad_=false;
    }
    if(pressed(Key::Escape)||pressed(static_cast<Key>(291))||pad.pressed(PadButton::Menu)) {
        const bool wasInMenu=menu_!=Menu::None;
        if(usingCannon_&&menu_==Menu::None){toggleCannon();dismissedMenu=true;}
        else if(freeBuild_&&menu_==Menu::None)menu_=Menu::Main;
        else if(!closeCurrentMode())menu_=Menu::Main;
        dismissedMenu=dismissedMenu||(wasInMenu&&menu_==Menu::None);
        menuSelection_=0;input.releaseMouse();player_.discardPendingInput();
    } else if(inputRouter_.pressed(A::Workshop)) {
        if(usingCannon_)status_="Press C to leave the cannon first.";
        else if(riding_)status_="Press M to get off before building.";
        else {building_=!building_;if(freeBuild_&&!building_)status_.clear();heightSteps_=0;input.releaseMouse();}
    }
    if(freeBuild_&&input.focused()&&!uiInputOwned_&&!dismissedMenu
        &&(pressed(Key::P)||pad.pressed(PadButton::LeftStick))) {
        const bool wasOpen=menu_==Menu::Colours;
        toggleColours();dismissedMenu=wasOpen&&menu_==Menu::None;
    }
    if(building_&&!riding_&&!usingCannon_&&menu_==Menu::None&&input.focused()&&!uiInputOwned_&&!dismissedMenu
        &&(pressed(Key::Tab)||(freeBuild_&&pad.pressed(PadButton::Alternate)))) {
        menu_=Menu::Catalog;menuSelection_=freeBuild_?0:int(selected_)-1;player_.discardPendingInput();
    }
    if(freeBuild_&&pressed(Key::M)&&input.focused()&&!uiInputOwned_&&menu_==Menu::None&&!dismissedMenu)toggleMotorbike();
    if(freeBuild_&&pressed(Key::C)&&input.focused()&&!uiInputOwned_&&menu_==Menu::None&&!dismissedMenu)toggleCannon();
    updateImportedWall();
    updateCannonPhysics();
    if(physics_)brickThrower_.retire(*physics_);
    if(!usingCannon_&&menu_==Menu::None&&input.focused())cannon_.aim(0,0,seconds);
    if(inputRouter_.pressed(A::Save)||pressed(static_cast<Key>(294))) {saveRequested_=true;saveStatus_="Saving...";}
    combatInput_.tick(sample,!freeBuild_&&menu_==Menu::None&&!building_&&!uiInputOwned_&&!hudPointerOwned_&&!hudClicked,preferences_,
        {input.wasMouseButtonPressed(MouseButton::Left),input.wasMouseButtonPressed(MouseButton::Right),input.wasMouseButtonPressed(MouseButton::Middle)});
    const auto throwCount=brickThrower_.input(freeBuild_&&!building_&&!usingCannon_&&!wallLocked()
        &&menu_==Menu::None&&!uiInputOwned_&&!hudPointerOwned_&&!hudClicked&&!dismissedMenu
        &&input.focused()&&inputRouter_.mouseGesturesAllowed()&&!inputRouter_.pressed(A::Workshop),
        input.wasMouseButtonPressed(MouseButton::Left),input.wasMouseButtonPressed(MouseButton::Right),
        input.wasMouseButtonReleased(MouseButton::Right),input.mouseDragDelta(MouseButton::Right));
    // Closing a modal must not also consume raw placement, shoulder or height
    // edges from that frame. Routed actions already require release to rearm.
    if(menu_!=Menu::None||uiInputOwned_||dismissedMenu||!input.focused()) {
        if(riding_)motorbike_.pause();
        player_.discardPendingInput();combatSeconds_=0;pendingAttack_=false;pendingDodge_=false;pendingJump_=false;
        const bool horizontal=menu_==Menu::Colours||menu_==Menu::Catalog;
        const bool hudKeyboard=sharedHudVisible&&!uiInputOwned_;
        if((hudKeyboard&&(pressed(Key::Up)||(horizontal&&pressed(Key::Left))))||pad.navigation(0)||(horizontal&&pad.navigation(2)))
            menuSelection_=std::max(0,menuSelection_-1);
        if((hudKeyboard&&(pressed(Key::Down)||(horizontal&&pressed(Key::Right))))||pad.navigation(1)||(horizontal&&pad.navigation(3)))++menuSelection_;
        const bool previousCategory=pad.pressed(PadButton::LeftShoulder)||(hudKeyboard&&pressed(Key::Q));
        const bool nextCategory=pad.pressed(PadButton::RightShoulder)||(hudKeyboard&&pressed(Key::E));
        const bool changedCategory=menu_==Menu::Catalog&&(previousCategory||nextCategory);
        if(changedCategory) {
            catalogCategory_=static_cast<uint8_t>((catalogCategory_+(nextCategory?1:2))%3);menuSelection_=0;
            refreshHud();
        }
        if(sharedHudVisible&&(menu_==Menu::Guide||menu_==Menu::GuideTopics)&&std::isfinite(input.scrollDelta())&&input.scrollDelta()!=0) {
            menuSelection_=std::max(0,menuSelection_+(input.scrollDelta()<0?1:-1));guideGamepad_=false;
        }
        if(!changedCategory&&((hudKeyboard&&pressed(Key::Enter))||pad.pressed(PadButton::Confirm)))menuRow(menuSelection_);
        if(pad.pressed(PadButton::Back)){(void)closeCurrentMode();input.resetState();}
    } else if(usingCannon_&&cannonFeet_) {
        player_.discardPendingInput();
        const auto aim=inputRouter_.movement();
        const double turn=-aim[0],elevate=aim[1];
        if(turn!=0||elevate!=0)inspectWall_=false;
        cannon_.aim(turn,elevate,seconds);
        if(pressed(Key::Space)||pad.pressed(PadButton::Confirm)
            ||(!hudClicked&&!hudPointerOwned_&&input.wasMouseButtonPressed(MouseButton::Left)))fireCannon();
        camera.setAspectRatio(width,height);
        const auto direction=cannon_.direction(),muzzle=cannon_.muzzle(*cannonFeet_);
        const auto cameraAnchor=*cannonFeet_+glm::dvec3(0,6,0);
        const auto horizontal=glm::dvec3(std::sin(cannon_.heading+cannon_.yaw),0,std::cos(cannon_.heading+cannon_.yaw));
        auto desiredEye=*cannonFeet_-horizontal*6.+glm::dvec3(0,4.3,0);
        auto viewTarget=muzzle+direction*40.;
        if(inspectWall_&&blacksmithFeet_) {
            // Frame the actual hit or selected section, not a hardcoded point
            // elsewhere on the house. The camera still respects intact scenery.
            viewTarget=wallInspectionPoint_;
            desiredEye=viewTarget-horizontal*16.+glm::dvec3(0,3,0);
        }
        const auto sweep=queries_.sweepSphere(cameraAnchor,desiredEye,.25);
        if(!sweep.complete||sweep.startOverlapped)desiredEye=cameraAnchor;
        else if(sweep.hit)desiredEye=cameraAnchor+glm::normalize(desiredEye-cameraAnchor)*std::max(0.,sweep.distance-.05);
        const auto eye=physics::worldPositionFromAbsolute(desiredEye);
        camera.setWorldPosition(eye.sector,eye.local);
        camera.lookAt(glm::vec3(viewTarget-glm::dvec3(eye.sector)*double(physics::kWorldSectorSize)));
        character_.update(seconds,player_.mode(),0,0,false);
    } else if(wallLocked()) {
        // A shot can land after C has left the cannon. Do not let either actor
        // traverse stale static cells while the accepted debris is moving.
        player_.discardPendingInput();motorbike_.pause();
        character_.update(seconds,player_.mode(),0,0,false);
    } else {
        const auto movement=inputRouter_.movement();const double yaw=orbit_.pose().valid?orbit_.pose().yaw:player_.facingYaw();
        const bool attack=combatInput_.pressed(CombatAction::Attack);
        const bool dodge=combatInput_.pressed(CombatAction::Dodge);
        if(riding_) {
            const auto before=motorbike_;
            motorbike_.advance(queries_,seconds,{movement[1],movement[0],input.isKeyDown(Key::Space)||pad.down(PadButton::Confirm)},installedWorld().waterHeight);
            const auto& bike=motorbike_.state();auto proxy=player_.state();
            proxy.feet=bike.feet;proxy.facingYaw=bike.yaw;
            proxy.velocity={-std::sin(bike.yaw)*bike.speed,bike.verticalSpeed,-std::cos(bike.yaw)*bike.speed};
            proxy.tick=bike.tick;proxy.mode=AdventurePlayer::Mode::Airborne;
            if(!player_.restore(proxy)){motorbike_=before;motorbike_.pause();}
            const auto p=player_.feet();std::string error;
            if(!session_->updatePlayer({p.x,p.y,p.z,player_.facingYaw()},100,error))status_=error;
        } else {
            // Hold either Shift to run on foot. Fine-placement still reads
            // Shift independently; the bike keeps its own throttle controls.
            const double pace=freeBuild_&&(input.isKeyDown(Key::LeftShift)||input.isKeyDown(Key::RightShift))?1.75:1.;
            AdventurePlayer::Input motion{adventureCameraRelativeMovement(movement,yaw),inputRouter_.pressed(A::Jump),pace};
            if(player_.mode()==AdventurePlayer::Mode::Swimming||player_.feet().y<=player_.swimSurfaceHeight()) {
                const bool steer=inputRouter_.orbitDrag()||(padAim_&&sample.axes[1]!=0);
                const auto swim=adventureSwimmingMovement(movement,yaw,orbit_.pose().elevation,steer,
                    inputRouter_.down(A::Jump)||inputRouter_.down(A::ReelIn),inputRouter_.down(A::PayOut));
                motion.movement={swim.x,swim.z};motion.swimVertical=swim.y;
            }
            advanceCombat(seconds,{motion,attack,dodge});
        }
        const auto p=player_.feet();
        expedition::CoveCamera::Input orbitInput;
        const auto look=inputRouter_.look();
        const auto mouse=inputRouter_.mouseGesturesAllowed()&&!hudPointerOwned_
            ?(preferences_.orbitToggle?(inputRouter_.orbitDrag()?input.mouseDelta():glm::vec2(0)):input.mouseDragDelta(MouseButton::Right))
            :glm::vec2(0);
        orbitInput.orbitRadians={double(mouse.x)*.004*preferences_.mouseSensitivity*(preferences_.invertX?-1:1)+look[0]*seconds*2,
            double(mouse.y)*.004*preferences_.mouseSensitivity*(preferences_.invertY?-1:1)+look[1]*seconds*2};
        const bool brickWheel=freeBuild_&&building_&&!input.isKeyDown(Key::LeftControl)&&!input.isKeyDown(Key::RightControl);
        if(brickWheel&&!hudPointerOwned_&&std::isfinite(input.scrollDelta())) {
            // One detent selects one item; accumulate smooth trackpad deltas.
            // Each pictured slot includes both its actual piece and paint.
            brickScroll_+=input.scrollDelta();
            if(std::abs(brickScroll_)>=.5f) {
                selectQuickSlot((activeQuickSlot_+(brickScroll_<0?1:quickSlots_.size()-1))%quickSlots_.size());
                brickScroll_=0;
            }
        } else brickScroll_=0;
        orbitInput.zoomSteps=brickWheel||hudPointerOwned_?0:input.scrollDelta();orbitInput.active=input.focused();
        orbitInput.recenter=inputRouter_.pressed(A::Recenter);
        camera.setAspectRatio(width,height);
        const expedition::CoveCamera::Target target{p+glm::dvec3(0,1.2*player_.bodyScale()+(riding_?motorbike_.state().groundOffset:0.),0),player_.facingYaw(),queries_.revision(),{},discontinuity_,glm::length(player_.worldVelocity())>.1};
        (void)orbit_.update(target,orbitInput,{camera.fovY(),camera.aspectRatio(),camera.nearPlane()},queries_.cameraSweep(),seconds);
        if(orbit_.pose().valid) {
            const auto pose=orbit_.pose();const auto cameraPosition=physics::worldPositionFromAbsolute(pose.eye);
            const auto origin=glm::dvec3(cameraPosition.sector)*double(physics::kWorldSectorSize);
            camera.setWorldPosition(cameraPosition.sector,cameraPosition.local);camera.lookAt(glm::vec3(pose.viewTarget-origin));discontinuity_=false;
        }
        character_.update(seconds,player_.mode(),glm::length(glm::dvec2(player_.worldVelocity().x,player_.worldVelocity().z))/player_.bodyScale(),player_.worldVelocity().y,state().combat.player.attackImpactTick!=0);
        swimAnimation_.update(seconds,player_.mode()==AdventurePlayer::Mode::Swimming,
            player_.worldVelocity(),player_.bodyScale(),preferences_.reducedMotion);
        updateTarget(camera,input,width,height);
        if(throwCount)throwBricks(throwCount);
        if(building_) {
            if(buildRouter_.pressed(A::RotateY))yaw_=uint8_t((yaw_+1)%4);
            if(buildRouter_.pressed(A::PreviousPart)) {
                if(freeBuild_)selectQuickSlot((activeQuickSlot_+quickSlots_.size()-1)%quickSlots_.size());
                else {selected_=PieceKind((int(selected_)+int(kBuildingPieceCount)-2)%int(kBuildingPieceCount)+1);blueprint_=BlueprintKind::None;heightSteps_=0;}
            }
            if(pad.pressed(PadButton::RightShoulder)) {
                if(freeBuild_)selectQuickSlot((activeQuickSlot_+1)%quickSlots_.size());
                else {selected_=PieceKind(int(selected_)%int(kBuildingPieceCount)+1);blueprint_=BlueprintKind::None;heightSteps_=0;}
            }
            if(pressed(static_cast<Key>(266))||pad.pressed(PadButton::Up))++heightSteps_;
            if(pressed(static_cast<Key>(267))||pad.pressed(PadButton::Down))--heightSteps_;
            if(buildRouter_.pressed(A::Keep)||(input.wasMouseButtonPressed(MouseButton::Left)&&!hudClicked&&!hudPointerOwned_))action(4);
            if(buildRouter_.pressed(A::Remove)||pad.pressed(PadButton::Back))action(5);
            if(buildRouter_.pressed(A::Undo))action(6);
        } else if(!riding_&&inputRouter_.pressed(A::Interact))use();
    }
    const auto commands=std::exchange(pendingActions_,{});
    if(!commands.empty())refreshHud();
    for(const auto& pending:commands) {
        const auto command=pending.action,value=pending.value;
        // A queued control belongs to the menu/mode that displayed it. Save is
        // global; every other control must still have that published context.
        if(command!=8&&pending.menuToken!=menuIntents_.token())continue;
        if(usingCannon_&&command!=33&&command!=34&&command!=35&&command!=36&&command!=37&&command!=8&&command!=9&&command!=10&&command!=20&&command!=25&&command!=31) {status_="Press C to leave the cannon first.";continue;}
        if(riding_&&(command==1||command==2||command==4||command==5||command==6||command==7||command==13||command==21||command==23||command==38||command==39)) {
            status_="Press M to get off before building.";continue;
        }
        switch(command) {
        case 32:if(freeBuild_&&menu_==Menu::None)toggleMotorbike();break;
        case 33:if(freeBuild_&&menu_==Menu::None)toggleCannon();break;
        case 34:if(usingCannon_&&menu_==Menu::None)fireCannon();break;
        case 35:if(usingCannon_&&menu_==Menu::None)changeImportedWall(false);break;
        case 36:if(usingCannon_&&menu_==Menu::None)changeImportedWall(true);break;
        case 37:if(usingCannon_&&menu_==Menu::None)changeImportedWall(false,true);break;
        case 1:building_=!building_;if(freeBuild_&&!building_)status_.clear();menu_=Menu::None;break;
        case 2:if(value>0&&pieceKindValid(uint32_t(value))){blueprint_=BlueprintKind::None;selected_=PieceKind(value);rememberQuickSlot();building_=true;menu_=Menu::None;heightSteps_=0;}break;
        case 39:
            if(freeBuild_&&!wallLocked()&&value>=1&&value<=int(quickSlots_.size()))selectQuickSlot(static_cast<size_t>(value-1));
            break;
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
            if(pending.removeScenery)status_="Place a brick here to make room for your build.";
            else if(isVillagePartId(pending.removePart))status_="Village scenery belongs to the town. Build beside it.";
            else if(isTrailPartId(pending.removePart))status_="This landmark belongs to the trail. Build beside it.";
            else if(pending.removePart&&commit(session_->prepareRemove(stamp(),pending.removePart,validator(),status_)))status_=freeBuild_?"Removed.":"Removed. Materials returned.";
            break;
        case 6:undoLastPlacement();break;
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
        case 22:building_=false;if(freeBuild_)status_.clear();menu_=Menu::None;player_.discardPendingInput();break;
        case 23:building_=true;menu_=menu_==Menu::Catalog?Menu::None:Menu::Catalog;menuSelection_=0;player_.discardPendingInput();break;
        case 24:menu_=Menu::Bag;menuSelection_=0;player_.discardPendingInput();break;
        case 25:menuSelection_=std::clamp(menuSelection_+std::clamp(value,-1,1),0,std::max(0,int(menuCommands_.size())-1));break;
        case 27:if(!building_&&menu_==Menu::None)pendingAttack_=true;break;
        case 28:if(!building_&&menu_==Menu::None)pendingDodge_=true;break;
        case 30:
            if(freeBuild_&&building_&&(menu_==Menu::None||menu_==Menu::Colours)&&value>=0&&value<=0xffffff) {
                selectedPaint_=uint32_t(value);
                rememberQuickSlot();
                if(menu_==Menu::Colours)(void)closeCurrentMode();
            }
            break;
        case 38:toggleColours();break;
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
        AdventureSpatialQueries next;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;std::string error;
        if(prepareGeometry(state(),next,residents,village,sites,scenery,error)) {
            AdventureSpatialQueries actors;AdventureEncounters encounters;
            if(prepareActors(state(),next,actors,encounters,error)) {
                ++staticGeometryEpoch_;walkQueries_=std::move(next);queries_=std::move(actors);encounters_=std::move(encounters);
                town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);scenery_=std::move(scenery);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
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
    if(physics_)playerPhysics_.sync(*physics_,player_.feet(),player_.bodyRadius(),player_.bodyHeight(),!riding_);
    observedOrigin_=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    observedEye_=observedOrigin_+glm::dvec3(camera.position());
    observedTarget_=orbit_.pose().viewTarget;
    if(usingCannon_&&cannonFeet_)observedTarget_=inspectWall_&&blacksmithFeet_
        ?wallInspectionPoint_
        :cannon_.muzzle(*cannonFeet_)+cannon_.direction()*40.;
    observedViewProjection_=glm::dmat4(camera.projectionMatrix()*camera.viewMatrix());observedWidth_=width;observedHeight_=height;
    if(interactionStatusSerial_!=previousInteraction)interactionFeedbackSeconds_=6;
    refreshHud();
    refreshNavigation();
#if defined(VOXY_NATIVE)
    observationSeconds_+=seconds;
    if(const char* path=std::getenv("VOXY_ADVENTURE_OBSERVE");path&&*path&&observationSeconds_>=.25) {
        observationSeconds_=0;const auto temporary=std::string(path)+".tmp";
        {std::ofstream stream(temporary);stream<<json();}
        std::error_code ignored;std::filesystem::rename(temporary,path,ignored);
    }
#endif
}
void AdventureRuntime::refreshNavigation() {
    if(!freeBuild_)return;
    const glm::dvec2 position(player_.feet().x,player_.feet().z);
    // Match the renderer's logical visibility before touching terrain or
    // compiling footprints. Small windows and paused menus need no map work.
    const float density=std::clamp(hudPixelScale_,.125f,16.f);
    hudNavigation_.visible=(menu_==Menu::None||menu_==Menu::Catalog||menu_==Menu::Colours)
        &&float(observedWidth_)/density>=760&&float(observedHeight_)/density>=540;
    const auto direction=observedTarget_-observedEye_;
    hudNavigation_.cameraBearingDegrees=adventureNavigationBearing({direction.x,direction.z});
    hudNavigation_.playerBearingDegrees=adventureNavigationBearing({-std::sin(player_.facingYaw()),-std::cos(player_.facingYaw())});
    if(navigationCountEpoch_!=staticGeometryEpoch_) {
        hudNavigation_.placedPieces=0;
        for(const auto& structure:state().structures)hudNavigation_.placedPieces+=static_cast<uint32_t>(structure.parts.size());
        navigationCountEpoch_=staticGeometryEpoch_;
    }
    hudNavigation_.playerUv=hudNavigation_.map?navigationCache_.playerUv(position):glm::vec2(.5f);
    if(!hudNavigation_.visible){hud_.setNavigation(hudNavigation_);return;}
    const auto& terrain=queries_.terrain();
    const float water=installedWorld().waterHeight;
    if(navigationCache_.needsUpdate(terrain,water,position,staticGeometryEpoch_)) {
        std::vector<AdventureNavigationFootprint> footprints;
        // The admitted village is derived scenery. Suppressed groups are absent
        // here, so the map cannot resurrect a building displaced by a player.
        for(const auto& group:scenery_.village().groups())
            footprints.push_back({{group.minimum.x,group.minimum.z},{group.maximum.x,group.maximum.z},0xb89568});
        // Reuse accepted collision bounds for the installed blacksmith. Dynamic
        // fragments are deliberately not represented as a still-standing house.
        if(blacksmithVisible())for(const auto& solid:walkQueries_.solids())
            if(solid.part.counter==blacksmithPartId||solid.part.counter==blacksmithWallPartId)
                footprints.push_back({{solid.minimum.x,solid.minimum.z},{solid.maximum.x,solid.maximum.z},0xa68163});
        std::unordered_map<uint64_t,uint32_t> paints;paints.reserve(hudNavigation_.placedPieces);
        for(const auto& structure:state().structures)for(const auto& part:structure.parts)
            paints.emplace(part.id,part.paint?part.paint:0xe8c98c);
        std::vector<AdventureSpatialQueries::Solid> owned;std::string ignored;
        if(compileSolids(state(),owned,ignored))for(const auto& solid:owned) {
            const auto paint=paints.find(solid.part.counter);
            footprints.push_back({{solid.minimum.x,solid.minimum.z},{solid.maximum.x,solid.maximum.z},paint!=paints.end()?paint->second:0xe8c98c});
        }
        auto map=std::make_shared<render::AdventureHudMap>();
        map->revision=hudNavigation_.map?hudNavigation_.map->revision+1:1;
        map->width=AdventureNavigationCache::size;map->height=AdventureNavigationCache::size;
        map->rgba=navigationCache_.rasterize(terrain,water,position,staticGeometryEpoch_,footprints);
        hudNavigation_.map=std::move(map);
    }
    // Only tiny uniforms change while walking or orbiting; the raster and the
    // main HUD's glyph/brick layout remain cached independently of player ticks.
    hudNavigation_.playerUv=navigationCache_.playerUv(position);
    hud_.setNavigation(hudNavigation_);
}
void AdventureRuntime::refreshHud() {
    render::AdventureHudContent hud;
    hud.creative=freeBuild_;hud.paint=selectedPaint_;hud.quickSlot=freeBuild_?static_cast<uint8_t>(activeQuickSlot_+1):0;
    hud.colourPickerOpen=menu_==Menu::Colours;hud.pixelScale=freeBuild_?hudPixelScale_:1;
    using H=render::AdventureHudMode;using O=MenuOperation;
    switch(menu_) {
    case Menu::None:hud.mode=building_?H::Build:H::Explore;break;
    case Menu::Main:hud.mode=H::Pause;break;
    case Menu::Catalog:hud.mode=H::Catalog;break;
    case Menu::Colours:hud.mode=H::Build;break;
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
    if(freeBuild_) {
        hud.title="Free build";
        if(interactionFeedbackSeconds_>0&&!status_.empty())hud.status=status_;
        hud.context=saveFailure_.empty()?(player_.mode()==AdventurePlayer::Mode::Swimming
            ?"Space: Rise   X: Dive   Right-drag: Steer":"B: Build / walk"):saveFailure_;
    }
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
        add(freeBuild_?(building_?"Return to building":"Return to exploring"):"Return to adventure",true,{O::Close});add("Building pieces",true,{O::Catalog});
        add(freeBuild_?"Save build":"Save adventure",true,{O::Save});add(freeBuild_?"Return to start":"Return to home / town",true,{O::Recover});
        add("Comfort and controls",true,{O::Settings});
        add("How to play",true,{O::GuideTopics});
        if(freeBuild_) {
            if(building_) {
                const bool canBuild=!riding_&&!usingCannon_&&!wallLocked();
                add("Rotate piece",canBuild,{O::Rotate});
                add("Raise piece",canBuild&&heightSteps_<32,{O::Raise});
                add("Lower piece",canBuild&&heightSteps_>-32,{O::Lower});
                add("Remove aimed piece",canBuild&&targetPart_&&AdventureSession::findPart(state(),targetPart_),{O::Remove,targetPart_});
                add("Choose colour",canBuild,{O::Colours});
                add("Finish building",canBuild,{O::FinishBuilding});
            }
            add("Undo last placement",!riding_&&!usingCannon_&&!wallLocked()&&(AdventureSession::findPart(state(),lastPlaced_)||lastBlueprint_),{O::Undo});
            add(riding_?"Get off motorbike":"Ride motorbike",!usingCannon_&&!wallLocked(),{O::Motorbike});
            add(usingCannon_?"Leave cannon":"Use cannon",!riding_&&cannonVisible()&&!wallLocked()&&!cannon_.liveShots(),{O::Cannon});
        }
        if(!freeBuild_){add("Starter room",true,{O::Starter});add("Quest journal",true,{O::Journal});add("Open bag",true,{O::Bag});}
    } else if(menu_==Menu::GuideTopics||menu_==Menu::Guide) {
        const auto exitLabel=guideReturnMenu_==Menu::Main?"Back to menu":building_?"Return to building":freeBuild_?"Return to exploring":"Return to adventure";
        const auto guideCard=[&](AdventureGuideTopic topic){return freeBuild_?creativeGuideCard(topic,preferences_,guideGamepad_):adventureGuideCard(topic,preferences_,guideGamepad_);};
        if(menu_==Menu::GuideTopics) {
            menuTitle_="How to play";menuText_="Choose a short tip. Read at your own pace.";
            for(uint64_t i=0;i<static_cast<uint64_t>(AdventureGuideTopic::Count);++i) {
                const auto card=guideCard(static_cast<AdventureGuideTopic>(i));
                add(card.title,true,{O::GuideTopic,i});
            }
        } else {
            const auto card=guideCard(guideTopic_);
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
    } else if(menu_==Menu::Colours) {
        menuTitle_="Brick colours";menuText_="Colour your next bricks.";
        for(const auto& [name,paint]:creativeColours) {
            add(name,true,{O::Paint,paint});hud.rows.back().value=static_cast<int>(paint);
        }
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
        std::string reason;const bool reachable=reachableComponent(state(),bench_,queries_,reason,freeBuild_);
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
            std::string reason;const bool reachable=reachableComponent(state(),chest_,queries_,reason,freeBuild_);
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
    auto context=std::string(mode())+":"+std::to_string(usingCannon_)+":"+std::to_string(riding_)+":"+std::to_string(dialogueNpc_)+":"+std::to_string(bench_)+":"+std::to_string(chest_)+":"+std::to_string(catalogCategory_)+":"+std::to_string(journalQuest_)
        +":"+std::to_string(static_cast<int>(bindingAction_))+":"+std::to_string(bindingDevice_)+":"+std::to_string(preferencesRevision_)
        +":"+std::to_string(static_cast<int>(menu_))+":"+std::to_string(static_cast<int>(guideTopic_))+":"+std::to_string(static_cast<int>(guideReturnMenu_));
    // A visible slot's identity includes its real payload. Editing a preset
    // invalidates old card/key events instead of reinterpreting an old picture.
    if(freeBuild_)for(const auto& slot:quickSlots_)context+=":"+std::to_string(int(slot.kind))+":"+std::to_string(slot.paint);
    if(!menuIntents_.publish(context,std::move(identities)))menuStatus_="Menu unavailable. Save and reopen the adventure.";
    for(size_t i=0;i<hud.rows.size();++i)hud.rows[i].intent=static_cast<uint32_t>(menuIntents_.intent(i));
    menuSelection_=std::clamp(menuSelection_,0,std::max(0,int(hud.rows.size())-1));hud.selectedRow=static_cast<size_t>(menuSelection_);
    if(menu_!=Menu::None)hud.title=menuTitle_;
    hud.menuText=menuText_;hud.menuStatus=menuStatus_;
    const uint32_t contextIntent=menuIntents_.token()?menuIntents_.token()*64u+1u:0;
    const auto button=[&](std::string_view label,int action,int value=0){return render::AdventureHudRow{std::string(label),"",true,action,value,contextIntent,0};};
    hud.quickActions=state().health?std::vector<render::AdventureHudRow>{button("Use",7),button("Build",21),button("Bag",24),button("Attack",27),button("Dodge",28),button("Pause",9)}
        :std::vector<render::AdventureHudRow>{button("Return home",11),button("Save",8)};
    if(freeBuild_)hud.quickActions={button("Use",7),button("Build",21),button("Pause",9)};
    for(auto& row:hud.quickActions) {
        if(row.action==27)row.enabled=state().equippedTool.kind==ItemKind::TrailStaff&&state().combat.tick>=state().combat.player.attackReadyTick;
        if(row.action==28)row.enabled=state().combat.tick>=state().combat.player.dodgeReadyTick;
    }
    if(usingCannon_) {
        hud.context=hudHoverFromMouse_?"A/D: turn · W/S: elevation · Click/Space: fire · C: leave"
            :"Left stick: aim · A: fire · Menu: leave";
        auto fire=button("Fire",34);
        fire.enabled=cannonPhysics_&&cannonPhysics_->ready(cannonGeometryEpoch_)
            &&(!wall_||wall_->ready())&&!wallQueryFailure_&&cannonEventsReady_&&!cannon_.liveShots();
        hud.quickActions={std::move(fire)};
        if(wall_&&(wall_->released()||wall_->phase()==ImportedWallPhysics::Phase::Failed))
            hud.quickActions.push_back(button("Rebuild wall",36));
        auto leave=button("Leave cannon",33);leave.enabled=!wallLocked()&&!cannon_.liveShots();
        hud.quickActions.push_back(std::move(leave));hud.quickActions.push_back(button("Pause",9));
    }
    hud.buildControls={button("Pieces",23),button("Rotate",3),button("Raise",12,1),button("Lower",12,-1),button("Remove",5),button("Undo",6),button("Help",29,1),button("Done",22)};
    if(menu_!=Menu::None)hud.buildControls={button("Previous",25,-1),button("Next",25,1),button("Close",20)};
    if(menu_==Menu::Guide||menu_==Menu::GuideTopics)hud.buildControls.clear();
    hud.categories={button("Structure",13,1),button("Bricks",13,2),button("Furniture",13,3)};
    if(freeBuild_) {
        const bool canBuild=!riding_&&!usingCannon_&&!wallLocked();
        hud.topActions={button(riding_?"Get off":"Motorbike",32),button(usingCannon_?"Leave cannon":"Cannon",33),
            button(saveStatus_=="Saving..."?"Saving...":"Save",8),button("Menu",31)};
        hud.topActions[0].enabled=!usingCannon_&&!wallLocked();
        hud.topActions[1].enabled=!riding_&&cannonVisible()&&!wallLocked()&&!cannon_.liveShots();
        if(saveStatus_=="Saving..."||!saveFailure_.empty()||saveFeedbackSeconds_>0)hud.topActions[2].detail=saveStatus_;
        hud.topActions[2].enabled=saveStatus_!="Saving..."&&(!wall_||!wall_->released());
        for(size_t i=0;i<quickSlots_.size();++i) {
            const auto& slot=quickSlots_[i];
            auto row=button(buildingDefinition(slot.kind)->name,39,static_cast<int>(i+1));
            row.pieceKind=static_cast<uint8_t>(slot.kind);row.paint=slot.paint;row.enabled=canBuild&&menu_==Menu::None;
            hud.hotbar.push_back(std::move(row));
        }
        for(size_t i=0;i<creativeColours.size();++i) {
            const auto& [name,paint]=creativeColours[i];
            hud.colours[i]=menu_==Menu::Colours?hud.rows[i]:button(name,30,static_cast<int>(paint));
            hud.colours[i].enabled=canBuild&&building_&&(menu_==Menu::None||menu_==Menu::Colours);
        }
        if(menu_==Menu::None) {
            hud.buildControls={button("Pieces",23),button("Rotate",3),button("Undo",6),button("Colour",38),
                button("Raise",12,1),button("Lower",12,-1),button("Remove",5),button("Help",29,1),button("Done",22)};
            hud.buildControls[0].detail=padAim_?"Y":"Tab";
            hud.buildControls[1].detail=padAim_?"X":"R";
            hud.buildControls[2].detail="Ctrl+Z";
            hud.buildControls[3].detail=padAim_?"L3":"P";
            hud.buildControls[8].detail=padAim_?"View":"B";
            for(auto& row:hud.buildControls)row.enabled=canBuild;
            hud.buildControls[2].enabled=canBuild&&(AdventureSession::findPart(state(),lastPlaced_)||lastBlueprint_);
            hud.buildControls[6].enabled=canBuild&&targetPart_&&targetPart_!=blacksmithPartId&&targetPart_!=blacksmithWallPartId
                &&!isVillagePartId(targetPart_)&&!isTrailPartId(targetPart_);
        }
        if(!usingCannon_) {
            hud.quickActions.clear();
            const auto useLabel=interactionLabel();
            if(!riding_&&!useLabel.empty()) {
                auto useAction=button("Use",7);useAction.detail=useLabel;hud.quickActions.push_back(std::move(useAction));
            }
            auto build=button("Build",21);build.enabled=canBuild;build.detail=padAim_?"View":"B";
            hud.quickActions.push_back(std::move(build));
        }
        if(hudHoverFromMouse_&&hudHover_&&hudHover_->intent&&(hudHover_->intent-1u)/64u==menuIntents_.token()
            &&hudWidth_==observedWidth_&&hudHeight_==observedHeight_) {
            hud.hoverLabel=hudHover_->label;hud.hoverBounds=hudHover_->bounds;
        }
    }
    hudContent_=hud;hud_.setContent(std::move(hud));
}
bool AdventureRuntime::render(WGPUCommandEncoder encoder,WGPUTextureView color,WGPUTextureView depth,
    WGPUTextureView linearDepth,WGPUTextureView environment,WGPUTextureView rayDepth,
    const Camera& camera,const render::PrimitiveLighting& lighting,uint32_t width,uint32_t height,render::SceneShadowConsumer background) {
    if(!meshes_.setSceneTextures(environment,rayDepth))return false;
    meshes_.clearInstances();const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
    render::FootContacts footContacts{};
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
    if(freeBuild_)for(const auto& group:scenery_.village().groups())for(const auto& piece:group.pieces) {
        const double distance=glm::length(glm::dvec2(piece.feet.x-state().player.x,piece.feet.z-state().player.z));
        if(distance>420)continue;
        const auto transform=glm::translate(glm::dmat4(1),piece.feet-origin)
            *glm::rotate(glm::dmat4(1),double(piece.yaw)*std::numbers::pi/2,glm::dvec3(0,1,0))
            *glm::scale(glm::dmat4(1),piece.scale);
        meshes_.addInstance({.assetIndex=11,.meshIndex=piece.mesh,.modelMatrix=glm::mat4(transform),.shadowRegions=1,.surface={0,0,1,0}});
        meshes_.addInstance({.assetIndex=19,.meshIndex=piece.mesh,.modelMatrix=glm::mat4(transform),.colorVisible=false,.shadowRegions=2});
    }
    if(freeBuild_&&blacksmithVisible()
        &&glm::length(glm::dvec2(blacksmithFeet_->x-state().player.x,blacksmithFeet_->z-state().player.z))<220) {
        const auto transform=glm::translate(glm::dmat4(1),*blacksmithFeet_-origin)
            *glm::rotate(glm::dmat4(1),std::numbers::pi,glm::dvec3(0,1,0));
        meshes_.addInstance({.assetIndex=12,.meshIndex=0,.modelMatrix=glm::mat4(transform),.shadowRegions=1,.surface={0,0,1,0}});
        meshes_.addInstance({.assetIndex=19,.meshIndex=15,.modelMatrix=glm::mat4(transform),.colorVisible=false,.shadowRegions=2});
        if(wall_&&!wall_->bindingsForEncodedTick(physics_?physics_->encodedTick():0).empty()) {
            for(const auto& binding:wall_->bindingsForEncodedTick(physics_?physics_->encodedTick():0))meshes_.addInstance({.assetIndex=12,.meshIndex=binding.meshNode,
                .modelMatrix=glm::mat4(binding.localMatrix),.physicsBody=binding.body,.surface={0,0,1,0}});
        } else if(wallSource_)for(const auto& part:wallSource_->parts) {
            const auto model=transform*glm::translate(glm::dmat4(1),part.translation)*glm::mat4_cast(part.rotation);
            meshes_.addInstance({.assetIndex=12,.meshIndex=part.meshNode,.modelMatrix=glm::mat4(model),.surface={0,0,1,0}});
        }
    }
    if(freeBuild_&&cannonVisible()) {
        meshes_.addInstance({.assetIndex=13,.meshIndex=0,.modelMatrix=glm::mat4(cannon_.baseMatrix(*cannonFeet_-origin)),.surface={0,0,1,0}});
        meshes_.addInstance({.assetIndex=13,.meshIndex=1,.modelMatrix=glm::mat4(cannon_.barrelMatrix(*cannonFeet_-origin)),.surface={0,0,1,0}});
    }
    if(freeBuild_) {
        const auto forestBegin=std::chrono::steady_clock::now();
        forestVisited_=forestVisible_=0;
        const auto eye=origin+glm::dvec3(camera.position());
        const auto rows=glm::transpose(camera.projectionMatrix()*camera.viewMatrix());
        const std::array planes{rows[3]+rows[0],rows[3]-rows[0],rows[3]+rows[1],rows[3]-rows[1],rows[2],rows[3]-rows[2]};
        const auto visible=[&](glm::dvec3 minimum,glm::dvec3 maximum) {
            const auto lo=glm::vec3(minimum-origin),hi=glm::vec3(maximum-origin);
            for(const auto& plane:planes) {
                const glm::vec3 corner(plane.x>=0?hi.x:lo.x,plane.y>=0?hi.y:lo.y,plane.z>=0?hi.z:lo.z);
                if(glm::dot(glm::vec3(plane),corner)+plane.w<0)return false;
            }
            return true;
        };
        if(forestDrawEpoch_!=staticGeometryEpoch_ || forestDrawOrigin_!=origin) {
            // Compare exact membership, not a probabilistic hash. Near/far
            // collision reclassification need not rebuild immutable tree data.
            constexpr size_t identityWords=(1024u*1024u+1024u+63u)/64u;
            std::vector<uint64_t> admitted(identityWords,0);
            const auto mark=[&](const CreativeProp& p){if(p.forestVariant<6 && p.id/64<admitted.size())admitted[p.id/64]|=uint64_t(1)<<(p.id%64);};
            for(const auto& prop:scenery_.props())mark(prop);
            for(const auto& prop:scenery_.distantTrees())mark(prop);
            if(forestDrawOrigin_==origin && forestDrawSource_==scenery_.forestSource() && admitted==forestAdmissionMask_)forestDrawEpoch_=staticGeometryEpoch_;
            else forestAdmissionMask_=std::move(admitted);
        }
        if(forestDrawEpoch_!=staticGeometryEpoch_ || forestDrawOrigin_!=origin) {
            forestDraws_.clear();forestTiles_.clear();
            const auto cache=[&](const CreativeProp& prop) {
                if(prop.forestVariant>=6)return;
                auto transform=glm::translate(glm::dmat4(1),prop.feet-origin)
                    *glm::rotate(glm::dmat4(1),double(prop.yawQuarterTurns)*std::numbers::pi/2,glm::dvec3(0,1,0));
                const float tone=.94f+.01f*float((prop.id*2654435761u>>24)%13u);
                forestDraws_.push_back({prop,{.assetIndex=18,.meshIndex=prop.forestVariant,
                    .modelMatrix=glm::mat4(transform),.tintColor={tone,tone,tone,1},.castsSunShadow=false,.surface={0,0,1,0}}});
            };
            for(const auto& prop:scenery_.props())cache(prop);
            for(const auto& prop:scenery_.distantTrees())cache(prop);
            const auto tileKey=[](const ForestDraw& tree){return std::pair(int(std::floor(tree.prop.feet.z/64)),int(std::floor(tree.prop.feet.x/64)));};
            std::sort(forestDraws_.begin(),forestDraws_.end(),[&](const auto& a,const auto& b){
                const auto ka=tileKey(a),kb=tileKey(b);return ka!=kb ? ka<kb : a.prop.id<b.prop.id;
            });
            std::vector<render::MeshDrawInstance> retained;retained.reserve(forestDraws_.size());
            for(uint32_t i=0;i<forestDraws_.size();++i) {
                const auto& tree=forestDraws_[i];retained.push_back(tree.instance);
                if(i==0 || tileKey(tree)!=tileKey(forestDraws_[i-1]))forestTiles_.push_back({tree.prop.minimum,tree.prop.maximum,i,0});
                auto& tile=forestTiles_.back();++tile.count;
                tile.minimum=glm::min(tile.minimum,tree.prop.minimum);tile.maximum=glm::max(tile.maximum,tree.prop.maximum);
            }
            if(!meshes_.setStaticInstances(retained)) {
                LOG_ERROR("Forest static instance preparation failed for {} trees",retained.size());
                return false;
            }
            forestDrawEpoch_=staticGeometryEpoch_;forestDrawOrigin_=origin;forestDrawSource_=scenery_.forestSource();
        }
        forestSelection_.clear();
        for(const auto& tile:forestTiles_) {
            const auto nearest=glm::clamp(eye,tile.minimum,tile.maximum)-eye;
            const double nearestSquared=nearest.x*nearest.x+nearest.z*nearest.z;
            if(nearestSquared>CreativeScenery::forestDrawDistance*CreativeScenery::forestDrawDistance)continue;
            if(nearestSquared>280.*280. && !visible(tile.minimum,tile.maximum))continue;
            for(uint32_t i=tile.first;i<tile.first+tile.count;++i) {
                ++forestVisited_;const auto& tree=forestDraws_[i];const auto& prop=tree.prop;
                const auto delta=prop.feet-eye;const double squared=delta.x*delta.x+delta.z*delta.z;
                if(squared>CreativeScenery::forestDrawDistance*CreativeScenery::forestDrawDistance)continue;
                if(squared>400.*400.) {
                    // Stable per-tree rank keeps the distant canopy coherent
                    // while its projected detail shrinks with distance.
                    const double distance=std::sqrt(squared);
                    const double keep=std::max(.10,1.-(distance-400.)*.00060);
                    const double rank=double((prop.id*2654435761u)>>8)*(1./16777216.);
                    if(rank>keep)continue;
                }
                const bool shadow=squared<280.*280.;
                if(!shadow && !visible(prop.minimum,prop.maximum))continue;
                ++forestVisible_;
                const double nearLimit=48.+double(prop.id%21u),horizonLimit=260.+double(prop.id%81u);
                const double distantLimit=500.+double(prop.id%101u);
                if(!shadow && squared>distantLimit*distantLimit)forestSelection_.push_back(i);
                else {
                    auto instance=tree.instance;
                    instance.assetIndex=squared>horizonLimit*horizonLimit?17u:squared>nearLimit*nearLimit?16u:15u;
                    instance.castsSunShadow=shadow;meshes_.addInstance(instance);
                }
            }
        }
        if(!meshes_.selectStaticInstances(forestSelection_)) {
            LOG_ERROR("Forest static instance selection failed for {} trees",forestSelection_.size());
            return false;
        }
        forestSelectionMs_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-forestBegin).count();
        const auto drawProp=[&](const CreativeProp& prop) {
            if(prop.forestVariant<6)return;
            const bool tree=prop.kind==CreativePropKind::Broadleaf||prop.kind==CreativePropKind::Pine;
            const double range=tree?CreativeScenery::forestDrawDistance:prop.kind==CreativePropKind::Flowers?80:prop.kind==CreativePropKind::Rocks?140:280;
            const glm::dvec2 delta(prop.feet.x-eye.x,prop.feet.z-eye.z);
            const double squared=glm::dot(delta,delta);
            if(squared>range*range)return;
            const bool shadow=squared<280.*280.;
            if(!shadow) {
                // Cull distant instances before constructing matrices or
                // expanding material records. Near offscreen casters stay.
                const auto lo=glm::vec3(prop.minimum-origin),hi=glm::vec3(prop.maximum-origin);
                for(const auto& plane:planes) {
                    const glm::vec3 corner(plane.x>=0?hi.x:lo.x,plane.y>=0?hi.y:lo.y,plane.z>=0?hi.z:lo.z);
                    if(glm::dot(glm::vec3(plane),corner)+plane.w<0)return;
                }
            }
            const double distance=std::sqrt(squared);
            // Distribute switches over a short band so a grove never changes
            // its entire silhouette in one frame. IDs make the bands stable.
            const bool forest=prop.forestVariant<6;
            const double nearLimit=(forest?48.:70.)+double(prop.id%21u);
            const double horizonLimit=260.+double(prop.id%81u);
            const uint32_t asset=forest?(distance>horizonLimit?17u:distance>nearLimit?16u:15u):
                tree&&distance>horizonLimit?14u:distance>nearLimit?10u:9u;
            const auto transform=glm::translate(glm::dmat4(1),prop.feet-origin)
                *glm::rotate(glm::dmat4(1),double(prop.yawQuarterTurns)*std::numbers::pi/2,glm::dvec3(0,1,0));
            const float tone=tree?.94f+.01f*float((prop.id*2654435761u>>24)%13u):1.f;
            meshes_.addInstance({.assetIndex=asset,.meshIndex=forest?uint32_t(prop.forestVariant):uint32_t(prop.kind),
                .modelMatrix=glm::mat4(transform),.tintColor={tone,tone, tone,1},.castsSunShadow=shadow,.surface={0,0,1,0}});
        };
        for(const auto& prop:scenery_.props())drawProp(prop);
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
    glm::dmat4 bikeRoot(1);
    if(freeBuild_&&motorbike_.available()) {
        const auto& bike=motorbike_.state();
        bikeRoot=glm::translate(glm::dmat4(1),bike.feet-origin+glm::dvec3(0,bike.groundOffset,0))
            *glm::rotate(glm::dmat4(1),bike.yaw,glm::dvec3(0,1,0))
            *glm::translate(glm::dmat4(1),glm::dvec3(0,BuilderMotorbike::wheelRadius,0))
            *glm::rotate(glm::dmat4(1),bike.pitch,glm::dvec3(1,0,0))
            *glm::rotate(glm::dmat4(1),bike.lean,glm::dvec3(0,0,1))
            *glm::translate(glm::dmat4(1),glm::dvec3(0,-BuilderMotorbike::wheelRadius,0));
        const auto around=[](glm::dvec3 p,double angle,glm::dvec3 axis) {
            return glm::translate(glm::dmat4(1),p)*glm::rotate(glm::dmat4(1),angle,axis)*glm::translate(glm::dmat4(1),-p);
        };
        const glm::dvec3 front(0,BuilderMotorbike::wheelRadius,-BuilderMotorbike::halfWheelbase);
        const glm::dvec3 rear(0,BuilderMotorbike::wheelRadius,BuilderMotorbike::halfWheelbase);
        const auto steering=around(front,bike.steering,{0,1,0});
        const std::array transforms{bikeRoot,bikeRoot*steering,
            bikeRoot*around(rear,-bike.spin,{1,0,0}),bikeRoot*steering*around(front,-bike.spin,{1,0,0})};
        for(uint32_t i=0;i<4;++i)meshes_.addInstance({.assetIndex=8,.meshIndex=i,.modelMatrix=glm::mat4(transforms[i]),.surface={0,0,1,0}});
        if(riding_)for(size_t i=0;i<2;++i) {
            if(!bike.wheelGrounded[i])continue;
            const auto p=bikeRoot*glm::dvec4(i?front:rear,1);
            footContacts[i]={float(p.x),float(p.y-BuilderMotorbike::wheelRadius),float(p.z),.55f};
        }
    }
    if(orbit_.pose().valid&&!orbit_.pose().hideAvatar) {
        assets::RigidAnimationPose pose;std::string error;
        const auto root=glm::translate(glm::dmat4(1),player_.feet()-origin)*glm::rotate(glm::dmat4(1),player_.facingYaw(),glm::dvec3(0,1,0));
        if(riding_) {
            if(!assets::sampleRigidAnimation(*robot_,robot_->clips[5],0,true,{},glm::dmat4(1),pose,error))return false;
            const auto seated=bikeRoot*glm::translate(glm::dmat4(1),glm::dvec3(0,.26*2.8,.15*2.8))
                *glm::scale(glm::dmat4(1),glm::dvec3(2.8));
            // Read the molded leg's real axle from the sampled asset. A visual
            // proportion revision must not rotate the rider about an old hip.
            std::array<glm::dvec3,2> hipPivots{},shoulderPivots{};
            for(uint32_t i=0;i<pose.drawCount;++i) {
                const auto& draw=pose.draws[i];
                const std::string_view name=robot_->mesh.name(robot_->mesh.nodes[draw.nodeIndex].nameOffset);
                if(name=="robot_thigh_l"||name=="robot_thigh_r")
                    hipPivots[name.ends_with("_l")?0:1]=glm::dvec3(draw.modelMatrix[3]);
                if(name=="robot_arm_l"||name=="robot_arm_r")
                    shoulderPivots[name.ends_with("_l")?0:1]=glm::dvec3(draw.modelMatrix[3]);
            }
            for(uint32_t i=0;i<pose.drawCount;++i) {
                auto& draw=pose.draws[i];const std::string_view name=robot_->mesh.name(robot_->mesh.nodes[draw.nodeIndex].nameOffset);
                glm::dmat4 adjust(1);
                if(name.find("thigh")!=std::string_view::npos||name.find("shin")!=std::string_view::npos||name.find("foot")!=std::string_view::npos) {
                    const double side=name.ends_with("_l")?-1.:1.;
                    const auto hip=hipPivots[name.ends_with("_l")?0:1];
                    adjust=glm::translate(glm::dmat4(1),hip+glm::dvec3(side*.12,0,0))
                        *glm::rotate(glm::dmat4(1),.85,glm::dvec3(1,0,0))*glm::translate(glm::dmat4(1),-hip);
                }
                if(name.find("arm_")!=std::string_view::npos||name.find("hand_")!=std::string_view::npos) {
                    const bool left=name.ends_with("_l");const double side=left?-1.:1.;
                    const auto shoulder=shoulderPivots[left?0:1];
                    const glm::dvec3 axis(0,1,0),front(0,.28,-.63);
                    const auto steering=glm::rotate(glm::dmat4(1),motorbike_.state().steering,axis);
                    const auto grip=front+glm::dvec3(steering*glm::dvec4(glm::dvec3(side*.462,1.089,-.234)-front,0))-glm::dvec3(0,.26,.15);
                    const auto from=glm::normalize(glm::dvec3(pose.anchors[left?1:2][3])-shoulder);
                    const auto to=glm::normalize(grip-shoulder);const auto cross=glm::cross(from,to);
                    if(glm::length(cross)>1e-8)adjust=glm::translate(glm::dmat4(1),shoulder)
                        *glm::rotate(glm::dmat4(1),std::acos(std::clamp(glm::dot(from,to),-1.,1.)),glm::normalize(cross))
                        *glm::translate(glm::dmat4(1),-shoulder);
                }
                draw.modelMatrix=glm::mat4(seated*adjust*glm::dmat4(draw.modelMatrix));
            }
            for(auto& anchor:pose.anchors)anchor=seated*anchor;
        } else {
            // Use the neutral toy pose for swimming, then animate the actual
            // shoulder/hip pivots. The head and hand anchors follow the same
            // body/stroke transforms as their visible meshes.
            if(player_.mode()==AdventurePlayer::Mode::Swimming) {
                if(!assets::sampleRigidAnimation(*robot_,robot_->clips[0],0,true,{},glm::dmat4(1),pose,error))return false;
            } else if(!character_.sample(*robot_,glm::dmat4(1),pose,error))return false;
            std::array<glm::dvec3,2> shoulders{},hips{};
            if(swimAnimation_.active())for(uint32_t i=0;i<pose.drawCount;++i) {
                const auto& draw=pose.draws[i];
                const std::string_view name=robot_->mesh.name(robot_->mesh.nodes[draw.nodeIndex].nameOffset);
                const size_t side=name.ends_with("_l")?0:1;
                if(name=="robot_arm_l"||name=="robot_arm_r")shoulders[side]=glm::dvec3(draw.modelMatrix[3]);
                if(name=="robot_thigh_l"||name=="robot_thigh_r")hips[side]=glm::dvec3(draw.modelMatrix[3]);
            }
            const auto body=root*swimAnimation_.body();
            for(uint32_t i=0;i<pose.drawCount;++i) {
                auto& draw=pose.draws[i];const std::string_view name=robot_->mesh.name(robot_->mesh.nodes[draw.nodeIndex].nameOffset);
                glm::dmat4 limb(1);const bool left=name.ends_with("_l");const size_t side=left?0:1;
                if(swimAnimation_.active()) {
                    if(name.find("arm_")!=std::string_view::npos||name.find("hand_")!=std::string_view::npos)
                        limb=swimAnimation_.arm(shoulders[side],left);
                    if(name.find("thigh_")!=std::string_view::npos||name.find("shin_")!=std::string_view::npos||name.find("foot_")!=std::string_view::npos)
                        limb=swimAnimation_.leg(hips[side],left);
                }
                draw.modelMatrix=glm::mat4(body*limb*glm::dmat4(draw.modelMatrix));
            }
            for(size_t i=0;i<pose.anchors.size();++i) {
                auto limb=glm::dmat4(1);
                if(swimAnimation_.active()&&(i==1||i==2||i==3)) {
                    const bool left=i==1;limb=swimAnimation_.arm(shoulders[left?0:1],left);
                }
                pose.anchors[i]=body*limb*pose.anchors[i];
            }
        }
        // Sample the trusted rigid animation unchanged, then scale its complete
        // presentation around the feet. Colour and shadow draws use this matrix.
        if(freeBuild_&&!riding_) {
            const auto feet=player_.feet()-origin;
            const auto scale=glm::translate(glm::dmat4(1),feet)
                *glm::scale(glm::dmat4(1),glm::dvec3(player_.bodyScale()))
                *glm::translate(glm::dmat4(1),-feet);
            for(uint32_t i=0;i<pose.drawCount;++i)pose.draws[i].modelMatrix=glm::mat4(scale*glm::dmat4(pose.draws[i].modelMatrix));
            for(auto& anchor:pose.anchors)anchor=scale*anchor;
            // Follow the actual animated boot soles in the same rebased frame
            // as mesh receivers. Do not attach a shadow to a hidden/swimming body.
            if(player_.mode()!=AdventurePlayer::Mode::Swimming)for(uint32_t i=0;i<pose.drawCount;++i) {
                const auto& draw=pose.draws[i];
                const std::string_view name=robot_->mesh.name(robot_->mesh.nodes[draw.nodeIndex].nameOffset);
                if(name!="robot_foot_l"&&name!="robot_foot_r")continue;
                const auto& bounds=robot_->prefab.meshBounds[draw.meshIndex];
                glm::vec3 lower(std::numeric_limits<float>::max()),upper(std::numeric_limits<float>::lowest());
                for(int corner=0;corner<8;++corner) {
                    const glm::vec3 p((corner&1)?bounds.maximum.x:bounds.minimum.x,
                        (corner&2)?bounds.maximum.y:bounds.minimum.y,(corner&4)?bounds.maximum.z:bounds.minimum.z);
                    const auto point=glm::vec3(draw.modelMatrix*glm::vec4(p,1));
                    lower=glm::min(lower,point);upper=glm::max(upper,point);
                }
                const auto center=(lower+upper)*.5f;
                footContacts[name=="robot_foot_l"?0:1]={center.x,lower.y,center.z,.7f};
            }
        }
        for(uint32_t i=0;i<pose.drawCount;++i)meshes_.addInstance({.assetIndex=1,.meshIndex=pose.draws[i].meshIndex,.modelMatrix=pose.draws[i].modelMatrix,.surface={0,0,1,0}});
        if(state().equippedTool.kind==ItemKind::TrailStaff) {
            // Attach a small wooden beam to the actual exported right-hand
            // anchor. It follows the accepted tool clip; hits use the resolver.
            const auto staff=trailStaffModel(pose.anchors[2]);
            meshes_.addInstance({.assetIndex=0,.meshIndex=uint32_t(PieceKind::Beam)-1,.modelMatrix=glm::mat4(staff),
                .tintColor=glm::vec4(1),.surface={0,0,1,0}});
        }
    }
    const auto bodyView=physics_&&wall_&&!wall_->bindingsForEncodedTick(physics_?physics_->encodedTick():0).empty()
        ?physics_->renderView():physics::PhysicsRenderView{};
    if(!meshes_.setAuthoredBodyView(bodyView,{camera.worldSector(),camera.position()}))return false;
    const bool rendered=meshes_.render(encoder,color,depth,camera.viewMatrix(),camera.projectionMatrix(),camera.position(),lighting,width,height,true,linearDepth,background,glm::vec3(origin),footContacts);
    if(!rendered)LOG_ERROR("Adventure mesh scene failed to render");
    return rendered;
}
bool AdventureRuntime::renderHud(WGPUCommandEncoder encoder,WGPUTextureView view,uint32_t width,uint32_t height){
#if !defined(VOXY_NATIVE)
    if(domUiAttached_&&!freeBuild_){hud_.clearEncodedObservation();return true;}
#endif
    const bool rendered=hud_.render(encoder,view,width,height);
    if(rendered){hudWidth_=width;hudHeight_=height;}
    return rendered;
}
std::string AdventureRuntime::json() const {
    size_t parts=0;for(const auto& s:state().structures)parts+=s.parts.size();
    const auto* blueprint=buildingBlueprintDefinition(blueprint_);
    const auto cost=blueprint?blueprint->cost:buildingDefinition(selected_)->cost;
    std::ostringstream out;out<<std::setprecision(17)<<"{\"costText\":"<<quote(freeBuild_?"Unlimited pieces":costLabel(cost))<<",\"creative\":"<<(freeBuild_?"true":"false")<<",\"build\":"<<(building_?"true":"false")<<",\"selected\":"<<quote(blueprint?blueprint->name:buildingDefinition(selected_)->name)
        <<",\"blueprintKind\":"<<int(blueprint_)<<",\"piece\":"<<(blueprint_==BlueprintKind::StarterRoom?0:int(selected_))<<",\"status\":"<<quote(status_)<<",\"previewReason\":"<<quote(previewReason_)<<",\"interaction\":"<<quote(interactionLabel())
        <<",\"statusEvent\":"<<quote(std::to_string(interactionStatusSerial_))
        <<",\"valid\":"<<(previewValid_?"true":"false")<<",\"wood\":"<<itemCount(state().backpack,ItemKind::Wood)
        <<",\"stone\":"<<itemCount(state().backpack,ItemKind::Stone)<<",\"scrap\":"<<itemCount(state().backpack,ItemKind::Scrap)
        <<",\"parts\":"<<parts<<",\"menu\":"<<quote(menu_==Menu::None?"":menu_==Menu::Main?"Adventure paused":menuTitle_)
        <<",\"saveFailure\":"<<quote(saveFailure_)<<",\"saveStatus\":"<<quote(saveStatus_)<<",\"dirty\":"<<(migrationDirty_||state().revision!=savedRevision_||(wall_&&wall_->released())?"true":"false")
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
    if(freeBuild_)out<<",\"forest\":{\"seed\":"<<CreativeScenery::forestSeed
        <<",\"recipe\":"<<CreativeScenery::forestRecipeVersion<<",\"drawDistance\":"<<CreativeScenery::forestDrawDistance
        <<",\"nearProps\":"<<scenery_.props().size()<<",\"distantTrees\":"<<scenery_.distantTrees().size()
        <<",\"horizonDraws\":"<<(meshes_.lastEncodedDrawCountForAsset(14)+meshes_.lastEncodedDrawCountForAsset(17)+meshes_.lastEncodedDrawCountForAsset(18))
        <<",\"visitedTrees\":"<<forestVisited_<<",\"visibleTrees\":"<<forestVisible_
        <<",\"selectionMs\":"<<forestSelectionMs_<<",\"instanceUploadBytes\":"<<meshes_.lastInstanceUploadBytes()
        <<",\"meshColorTriangles\":"<<meshes_.lastColorTriangles()<<",\"meshShadowTriangles\":"<<meshes_.lastShadowTriangles()<<'}';
    out<<",\"riding\":"<<(riding_?"true":"false")<<",\"bikeSpeed\":"<<motorbike_.state().speed;
    const double cannonDistance=cannonFeet_?glm::length(player_.feet()-*cannonFeet_):0;
    // Event admission is attempted synchronously by attachPhysics. Its failure
    // cannot be repaired by a later successful wall rebuild/query publication.
    const bool impactDetectionFailed=physics_&&!cannonEventsReady_;
    const bool wallFailed=impactDetectionFailed||wallQueryFailure_||(wall_&&wall_->phase()==ImportedWallPhysics::Phase::Failed);
    std::string wallFailureMessage=wall_?wall_->message():std::string{};
    if(impactDetectionFailed)wallFailureMessage="Impact detection stopped safely. Reload the world.";
    else if(wallQueryFailure_&&wallFailureMessage.empty())wallFailureMessage="Wall collision could not be installed. Rebuild the wall.";
    if(freeBuild_)out<<",\"cannon\":{\"available\":"<<(cannonVisible()?"true":"false")
        <<",\"x\":"<<(cannonFeet_?cannonFeet_->x:0)<<",\"y\":"<<(cannonFeet_?cannonFeet_->y:0)<<",\"z\":"<<(cannonFeet_?cannonFeet_->z:0)
        <<",\"distanceStuds\":"<<(std::isfinite(cannonDistance)?std::ceil(cannonDistance):0)
        <<",\"active\":"<<(usingCannon_?"true":"false")<<",\"ready\":"<<(cannonPhysics_&&cannonPhysics_->ready(cannonGeometryEpoch_)&&(!wall_||wall_->ready())&&!wallQueryFailure_&&cannonEventsReady_?"true":"false")
        <<",\"error\":"<<quote(cannonPhysicsError_)<<",\"shots\":"<<cannon_.shotsFired<<",\"impacts\":"<<cannonImpacts_<<",\"live\":"<<cannon_.liveShots()<<",\"yaw\":"<<cannon_.yaw<<",\"elevation\":"<<cannon_.elevation
        <<",\"awaitingHit\":"<<(cannon_.liveShots()>0?"true":"false")
        <<",\"contacts\":"<<cannonContacts_<<",\"contactFeature\":"<<cannonContactFeature_
        <<",\"contactBody\":"<<cannonContactBody_<<",\"contactSpeed\":"<<cannonContactSpeed_<<",\"contactImpulse\":"<<cannonContactImpulse_
        <<",\"releasedParts\":"<<(wall_?wall_->impactStats().releasedParts:0)
        <<",\"sourceParts\":"<<(wall_?wall_->bindings().size():0)
        <<",\"partDisplacement\":"<<(wall_?wall_->maximumPartDisplacement():0)
        <<",\"impactDirection\":["<<(wall_?wall_->impactStats().worldDirection.x:0)<<','<<(wall_?wall_->impactStats().worldDirection.y:0)<<','<<(wall_?wall_->impactStats().worldDirection.z:0)<<']'
        <<",\"impactSource\":"<<quote(std::to_string(wall_?wall_->impactStats().sourceId:0))
        <<",\"impactEnergy\":"<<(wall_?wall_->impactStats().addedEnergy:0)
        <<",\"wallReady\":"<<(wall_&&wall_->ready()?"true":"false")<<",\"wallReleased\":"<<(wall_&&wall_->released()?"true":"false")
        <<",\"inspectingWall\":"<<(inspectWall_?"true":"false")
        <<",\"wallBusy\":"<<(wallLocked()?"true":"false")
        <<",\"wallPhase\":"<<(wall_?int(wall_->phase()):0)<<",\"wallMessage\":"<<quote(wallFailureMessage)
        <<",\"wallFailed\":"<<(wallFailed?"true":"false")
        <<",\"nearby\":"<<(cannonVisible()&&glm::length(player_.feet()-*cannonFeet_)<12?"true":"false")<<'}';
    if(freeBuild_&&blacksmithFeet_)out<<",\"blacksmith\":{\"available\":"<<(blacksmithVisible()?"true":"false")
        <<",\"x\":"<<blacksmithFeet_->x<<",\"y\":"<<blacksmithFeet_->y<<",\"z\":"<<blacksmithFeet_->z<<'}';
    out<<",\"paint\":"<<selectedPaint_<<",\"quickSlot\":"<<(freeBuild_?activeQuickSlot_+1:0)<<",\"quickSlots\":[";
    if(freeBuild_)for(size_t i=0;i<quickSlots_.size();++i) {
        if(i)out<<',';
        out<<"{\"piece\":"<<int(quickSlots_[i].kind)<<",\"paint\":"<<quickSlots_[i].paint<<'}';
    }
    out<<"],\"colourAvailable\":"<<(freeBuild_?"true":"false")
        <<",\"colourPickerOpen\":"<<(menu_==Menu::Colours?"true":"false")
        <<",\"canUndo\":"<<(AdventureSession::findPart(state(),lastPlaced_)||lastBlueprint_?"true":"false")
        <<",\"canRemove\":"<<(targetPart_&&targetPart_!=blacksmithPartId&&targetPart_!=blacksmithWallPartId&&!isVillagePartId(targetPart_)&&!isTrailPartId(targetPart_)?"true":"false");
    if(freeBuild_) {
        out<<",\"hud\":{\"width\":"<<hudWidth_<<",\"height\":"<<hudHeight_<<",\"controls\":[";
        bool first=true;
        for(const auto& hit:hud_.layout().hits) {
            // A frame may publish new state before its GPU HUD is encoded.
            // Never expose a keyboard peer for a previously rendered context.
            if(!hit.intent||(hit.intent-1u)/64u!=menuIntents_.token())continue;
            if(!first)out<<',';
            first=false;
            out<<"{\"x\":"<<hit.bounds.x<<",\"y\":"<<hit.bounds.y<<",\"width\":"<<hit.bounds.z<<",\"height\":"<<hit.bounds.w
                <<",\"action\":"<<hit.action<<",\"value\":"<<hit.value<<",\"intent\":"<<hit.intent
                <<",\"row\":"<<(hit.row==SIZE_MAX?-1:static_cast<int64_t>(hit.row))
                <<",\"enabled\":"<<(hit.enabled?"true":"false")<<",\"label\":"<<quote(hit.label)<<",\"shortcutKey\":"<<hit.shortcutKey<<'}';
        }
        out<<"]}";
        out<<",\"navigation\":{\"visible\":"<<(hudNavigation_.visible?"true":"false")
            <<",\"mapRevision\":"<<(hudNavigation_.map?hudNavigation_.map->revision:0)
            <<",\"terrainRasterizations\":"<<navigationCache_.terrainRasterizations()
            <<",\"cameraBearingDegrees\":"<<hudNavigation_.cameraBearingDegrees
            <<",\"playerBearingDegrees\":"<<hudNavigation_.playerBearingDegrees
            <<",\"playerUv\":["<<hudNavigation_.playerUv.x<<','<<hudNavigation_.playerUv.y<<']'
            <<",\"placedPieces\":"<<hudNavigation_.placedPieces<<'}';
    }
    out<<",\"observation\":"<<quote(std::to_string(observationSerial_))<<",\"revision\":"<<quote(std::to_string(state().revision))<<",\"menuSelected\":"<<menuSelection_;
    const auto p=player_.feet();out<<",\"player\":{\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"z\":"<<p.z<<",\"yaw\":"<<player_.facingYaw()<<",\"tick\":"<<quote(std::to_string(player_.tick()))<<"}";
    out<<",\"swimming\":"<<(player_.mode()==AdventurePlayer::Mode::Swimming?"true":"false");
    out<<",\"thrownBricks\":{\"total\":"<<brickThrower_.thrown<<",\"live\":"<<brickThrower_.live()
        <<",\"playerCollider\":"<<(playerPhysics_.body.valid()&&!playerPhysics_.retiring?"true":"false")
        <<",\"playerContacts\":"<<playerPhysics_.contacts<<'}';
    out<<",\"camera\":{\"yaw\":"<<orbit_.pose().yaw<<",\"distance\":"<<orbit_.pose().distance
        <<",\"requestedDistance\":"<<orbit_.pose().requestedDistance<<",\"width\":"<<observedWidth_<<",\"height\":"<<observedHeight_;
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
bool AdventureRuntime::snapshot(std::vector<std::byte>& bytes,std::string& error) const {if(wall_&&wall_->released()){error="Rebuild the wall before saving. Wall changes are session-only for now.";return false;}const bool ok=AdventureSaveCodec::encode(state(),content_,bytes,error);if(ok)savingRevision_=state().revision;return ok;}
bool AdventureRuntime::restore(std::span<const std::byte> bytes,construction::WorldNamespace world,std::string& error) {
    if(wall_&&wall_->released()){error="Rebuild the wall before loading another save.";return false;}
    AdventureState restored;AdventureSaveLoadMetadata metadata;
    if(!AdventureSaveCodec::decode(bytes,world,content_,restored,error,&metadata))return false;
    auto next=AdventureSession::restore(restored,content_,error);AdventureSpatialQueries geometry;TownResidents residents;VillageLayout village;TrailSites sites;CreativeScenery scenery;
    if(!next||!prepareGeometry(restored,geometry,residents,village,sites,scenery,error,false))return false;
    AdventureSpatialQueries actors;AdventureEncounters encounters;
    if(!prepareActors(restored,geometry,actors,encounters,error))return false;
    const auto p=restored.player;auto pose=player_.state();pose.feet={p.x,p.y,p.z};pose.velocity={};pose.facingYaw=p.yaw;
    bool resizedRecovery=false;
    if(!actors.clearCapsule(pose.feet,player_.bodyRadius(),player_.bodyHeight())) {
        if(!freeBuild_){error="Saved player position is blocked.";return false;}
        const auto point=creativeStandingPoint(actors,pose.feet);
        if(!point){error="No nearby space fits the resized figure. Your saved build is unchanged.";return false;}
        pose.feet=*point;resizedRecovery=true;
        if(!next->updatePlayer({point->x,point->y,point->z,pose.facingYaw},restored.health,error))return false;
    }
    AdventurePlayer checkedPlayer;
    pose.mode=pose.feet.y<=player_.swimSurfaceHeight()?AdventurePlayer::Mode::Swimming:AdventurePlayer::Mode::Airborne;
    auto start=pose.mode==AdventurePlayer::Mode::Swimming?std::optional(pose.feet)
        :freeBuild_?creativeStandingPoint(actors,pose.feet)
        :std::optional<glm::dvec3>(townSpawn(actors.terrain()));
    if(freeBuild_&&!start)start=creativeStandingPoint(actors,
        {content_.town.x,content_.town.y,content_.town.z});
    if(!start||!checkedPlayer.initialize(actors,*start,installedWorld().waterHeight,pose.facingYaw,player_.bodyScale(),player_.bodyRadius()/player_.bodyScale())
        ||!checkedPlayer.restore(pose)) {error="Saved player position is unavailable.";return false;}
    session_=std::move(next);++staticGeometryEpoch_;walkQueries_=std::move(geometry);queries_=std::move(actors);encounters_=std::move(encounters);
    previewResult_.reset();structureJson_.reset();aimRayResult_.reset(); // A restored checkpoint can reuse the same revision.
    town_=std::move(residents);village_=std::move(village);trailSites_=std::move(sites);scenery_=std::move(scenery);fieldHome_=fieldHomeReadiness(state(),walkQueries_);
    combat_.reset();combatSeconds_=0;pendingAttack_=false;pendingDodge_=false;pendingJump_=false;
    if(!player_.restore(pose)){error="Saved player position is unavailable.";return false;}
    swimAnimation_.reset();
    // Builder previews, undo targets and queued menu choices belong to the
    // previous checkpoint. Invalidate their tokens without recycling IDs.
    motorbike_.reset();riding_=false;usingCannon_=false;inspectWall_=false;
    if(physics_){cannon_.clear(*physics_);brickThrower_.retire(*physics_,true);playerPhysics_.clear(*physics_);}
    (void)brickThrower_.input(false,false,false,false,{});
    saveFeedbackSeconds_=0;interactionFeedbackSeconds_=0;
    selectedPaint_=0;brickScroll_=0;building_=freeBuild_;blueprint_=BlueprintKind::None;selected_=freeBuild_?PieceKind::Brick2x4:PieceKind::Foundation;
    if(freeBuild_)resetQuickSlots();
    menu_=Menu::None;menuSelection_=0;heightSteps_=0;lastPlaced_=0;lastBlueprint_=0;
    guideReturnMenu_=Menu::None;guideReturnSelection_=0;guideTopic_=AdventureGuideTopic::Movement;guideGamepad_=false;
    bench_=0;chest_=0;dialogueNpc_=0;journalQuest_=1;targetPart_=0;
    pendingActions_.clear();previewValid_=false;hasTarget_=false;previewReason_.clear();
    player_.discardPendingInput();inputRouter_.reset();buildRouter_.reset();combatInput_.reset();
    (void)menuIntents_.publish("restored-checkpoint",{});
    discontinuity_=true;status_=resizedRecovery?"Your build is restored. The resized figure moved to nearby clear ground.":freeBuild_?"Your build is restored.":"Welcome home. Your adventure is restored.";
    migrationDirty_=metadata.migrated||resizedRecovery;
    saveStatus_=migrationDirty_?"World updated. Save to keep the new format.":(freeBuild_?"Saved build loaded":"Saved adventure loaded");
    savedRevision_=state().revision;refreshHud();return true;
}
}

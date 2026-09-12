#include "game/assets/robot_asset.hpp"
#include "core/sha256.hpp"
#include <json.hpp>
#include <set>

namespace voxy::game::assets {
std::shared_ptr<const RigidAnimationAsset> loadRobotAsset(const CookedPartByteProvider& read,std::string& error) {
    const auto fail=[&](const char* reason)->std::shared_ptr<const RigidAnimationAsset>{error=reason;return {};};
    try {
        const auto manifest=read("manifest.json",16u*1024u,error);
        if(!manifest||manifest->empty()||manifest->size()>16u*1024u)return fail("Robot manifest is missing or oversized.");
        std::vector<std::set<std::string>> keys;
        const auto* manifestText=reinterpret_cast<const char*>(manifest->data());
        const auto document=nlohmann::json::parse(manifestText,manifestText+manifest->size(),[&](int depth,auto event,auto& parsed){
            using Event=nlohmann::json::parse_event_t;
            if(depth>8)throw std::runtime_error("Robot manifest nesting");
            if(event==Event::object_start)keys.emplace_back();
            else if(event==Event::key) {
                const auto key=parsed.template get<std::string>();
                if(key.size()>64||keys.empty()||!keys.back().insert(key).second)throw std::runtime_error("Robot manifest duplicate key");
            } else if(event==Event::object_end)keys.pop_back();
            return true;
        });
        if(!document.is_object()||document.value("schema",0)!=1||document.value("profile","")!="salvage-animated-rigid-v1"
            ||document.value("asset_id","")!="voxys-cove-robot-r01"||document.value("render_to_canonical",0)!=12
            ||document.value("filename","")!="robot.vmesh"||document.value("maximum_nodes",0)!=32
            ||document.value("maximum_meshes",0)!=24||document.value("maximum_draws",0)!=48)
            return fail("Robot manifest has an unsupported identity or profile.");
        for(const auto& [name,expected]:std::array<std::pair<std::string_view,std::span<const std::string_view>>,2>{
            std::pair{"clips",std::span<const std::string_view>(kRobotClips)},std::pair{"anchors",std::span<const std::string_view>(kRobotAnchors)}}) {
            const auto& entries=document.at(std::string(name));
            if(!entries.is_array()||entries.size()!=expected.size())return fail("Robot clips or anchors are incomplete.");
            for(size_t i=0;i<expected.size();++i)if(entries[i]!=expected[i])return fail("Robot clips or anchors do not match this runtime.");
        }
        const auto hash=document.at("sha256").get<std::string>();
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)return fail("Robot hash is invalid.");
        const auto bytes=read("robot.vmesh",2u*1024u*1024u,error);
        if(!bytes||bytes->empty()||bytes->size()>2u*1024u*1024u)return fail("Robot model is missing or oversized.");
        if(core::sha256Hex(core::sha256(std::as_bytes(std::span(*bytes))))!=hash)return fail("Robot model does not match its installed manifest.");
        moto::VmeshData mesh;
        if(!moto::readVmesh(bytes->data(),bytes->size(),&mesh,&error))return {};
        auto result=std::make_shared<RigidAnimationAsset>();
        if(!prepareRigidAnimation(std::move(mesh),*result,error))return {};
        error.clear();return result;
    }catch(const std::bad_alloc&){return fail("Not enough memory to load the robot.");}
    catch(const std::exception&){return fail("Robot package is malformed.");}
}
std::shared_ptr<const RigidAnimationAsset> loadRobotAsset(const std::filesystem::path& directory,std::string& error) {
    const auto read=openCookedPartDirectory(directory,error);
    return read?loadRobotAsset(*read,error):nullptr;
}
}

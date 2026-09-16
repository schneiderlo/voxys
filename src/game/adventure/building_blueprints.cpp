#include "game/adventure/building_blueprints.hpp"
#include "game/adventure/adventure_session.hpp"
#include "game/adventure/construction_policy.hpp"
#include <array>
#include <limits>

namespace voxy::game::adventure {
namespace {
constexpr std::array<BuildingBlueprintDefinition,2> definitions{{
    {BlueprintKind::StarterRoom,"Starter room","A sheltered room with a bed, chest and workbench.",{70,16,8},PieceKind::Foundation},
    {BlueprintKind::WideStoneStep,"Wide stone step","Three stone piers form a broad step. Adjust its height to fit the ground.",{0,6,0},PieceKind::Pier},
}};
}
std::span<const BuildingBlueprintDefinition> buildingBlueprints() noexcept {return definitions;}
const BuildingBlueprintDefinition* buildingBlueprintDefinition(BlueprintKind kind) noexcept {
    const auto id=uint8_t(kind);return id>=1&&id<=definitions.size()?&definitions[id-1]:nullptr;
}
std::vector<PlacePart> buildingBlueprintLayout(BlueprintKind kind,GridPosition anchor,uint8_t yaw,std::string& error) {
    if(!buildingBlueprintDefinition(kind)||yaw>3||!construction::isValid(anchor)) {
        error="Choose a valid blueprint placement.";return {};
    }
    if(kind==BlueprintKind::StarterRoom)return starterRoomLayout(anchor,yaw,error);
    std::vector<PlacePart> parts;parts.reserve(3);
    for(const int32_t z:{-24,0,24}) {
        int64_t dx=0,dz=z;
        switch(yaw) {
        case 1:dx=z;dz=0;break;
        case 2:dz=-int64_t(z);break;
        case 3:dx=-int64_t(z);dz=0;break;
        default:break;
        }
        const int64_t x=int64_t(anchor.x)+dx,worldZ=int64_t(anchor.z)+dz;
        if(x<=INT32_MIN||x>INT32_MAX||worldZ<=INT32_MIN||worldZ>INT32_MAX) {
            error="The blueprint lies outside the construction lattice.";return {};
        }
        parts.push_back({0,PieceKind::Pier,{int32_t(x),anchor.y,int32_t(worldZ)},yaw,0});
    }
    error.clear();return parts;
}
} // namespace voxy::game::adventure

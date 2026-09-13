#include "game/adventure/item_catalog.hpp"

#include <algorithm>

namespace voxy::game::adventure {
namespace {
constexpr std::array<ItemDefinition,4> definitions{{
    {ItemKind::Wood,"adventure.wood.v1","Wood",999},
    {ItemKind::Stone,"adventure.stone.v1","Stone",999},
    {ItemKind::Scrap,"adventure.scrap.v1","Scrap",999},
    {ItemKind::FieldHammer,"adventure.field-hammer.v1","Field hammer",1},
}};
constexpr size_t maximumSlots = 32;
}

const ItemDefinition* itemDefinition(ItemKind kind) noexcept {
    const auto index=static_cast<size_t>(kind);
    return index && index<=definitions.size()? &definitions[index-1]:nullptr;
}
bool validStack(ItemStack stack) noexcept {
    if(stack.kind==ItemKind::None) return stack.quantity==0;
    const auto* definition=itemDefinition(stack.kind);
    return definition && stack.quantity>0 && stack.quantity<=definition->maximumStack;
}
uint32_t itemCount(std::span<const ItemStack> slots,ItemKind kind) noexcept {
    uint32_t count=0;
    for(const auto slot:slots) if(slot.kind==kind) count+=slot.quantity;
    return count;
}
bool canAfford(std::span<const ItemStack> slots,MaterialCost cost) noexcept {
    return itemCount(slots,ItemKind::Wood)>=cost.wood &&
        itemCount(slots,ItemKind::Stone)>=cost.stone &&
        itemCount(slots,ItemKind::Scrap)>=cost.scrap;
}
bool addItems(std::span<ItemStack> slots,ItemStack addition) noexcept {
    if(slots.size()>maximumSlots || !validStack(addition) || addition.kind==ItemKind::None) return false;
    if(!std::all_of(slots.begin(),slots.end(),validStack)) return false;
    const auto bound=itemDefinition(addition.kind)->maximumStack;
    uint32_t capacity=0;
    for(const auto slot:slots) {
        if(slot.kind==addition.kind) capacity+=bound-slot.quantity;
        else if(slot.kind==ItemKind::None) capacity+=bound;
    }
    if(capacity<addition.quantity) return false;
    auto remaining=addition.quantity;
    for(auto& slot:slots) {
        if(slot.kind!=addition.kind) continue;
        const auto put=static_cast<uint16_t>(std::min<uint32_t>(bound-slot.quantity,remaining));
        slot.quantity+=put;remaining-=put;
    }
    for(auto& slot:slots) {
        if(!remaining) break;
        if(slot.kind!=ItemKind::None) continue;
        const auto put=static_cast<uint16_t>(std::min<uint32_t>(bound,remaining));
        slot={addition.kind,put};remaining-=put;
    }
    return remaining==0;
}
bool takeItems(std::span<ItemStack> slots,ItemStack removal) noexcept {
    if(slots.size()>maximumSlots || !validStack(removal) || removal.kind==ItemKind::None) return false;
    if(!std::all_of(slots.begin(),slots.end(),validStack) || itemCount(slots,removal.kind)<removal.quantity) return false;
    auto remaining=removal.quantity;
    for(auto& slot:slots) {
        if(slot.kind!=removal.kind) continue;
        const auto take=std::min(slot.quantity,remaining);
        slot.quantity-=take;remaining-=take;
        if(!slot.quantity) slot={};
    }
    return true;
}
bool consumeMaterials(std::span<ItemStack> slots,MaterialCost cost) noexcept {
    if(slots.size()>maximumSlots || !canAfford(slots,cost)) return false;
    std::array<ItemStack,maximumSlots> copy{};
    std::copy(slots.begin(),slots.end(),copy.begin());
    auto candidate=std::span(copy).first(slots.size());
    for(const auto [kind,count]:std::array<ItemStack,3>{{{ItemKind::Wood,cost.wood},{ItemKind::Stone,cost.stone},{ItemKind::Scrap,cost.scrap}}})
        if(count && !takeItems(candidate,{kind,count})) return false;
    std::copy(candidate.begin(),candidate.end(),slots.begin());return true;
}
bool refundMaterials(std::span<ItemStack> slots,MaterialCost cost) noexcept {
    if(slots.size()>maximumSlots) return false;
    std::array<ItemStack,maximumSlots> copy{};
    std::copy(slots.begin(),slots.end(),copy.begin());
    auto candidate=std::span(copy).first(slots.size());
    for(const auto [kind,count]:std::array<ItemStack,3>{{{ItemKind::Wood,cost.wood},{ItemKind::Stone,cost.stone},{ItemKind::Scrap,cost.scrap}}})
        if(count && !addItems(candidate,{kind,count})) return false;
    std::copy(candidate.begin(),candidate.end(),slots.begin());return true;
}
} // namespace voxy::game::adventure

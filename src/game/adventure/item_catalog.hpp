#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace voxy::game::adventure {

// Version 1 IDs are serialized. Append new IDs only with a schema/content migration.
enum class ItemKind : uint8_t { None, Wood, Stone, Scrap, FieldHammer, Count };
struct ItemDefinition {
    ItemKind kind = ItemKind::None;
    std::string_view id;
    std::string_view name;
    uint16_t maximumStack = 0;
};
struct ItemStack {
    ItemKind kind = ItemKind::None;
    uint16_t quantity = 0;
    bool operator==(const ItemStack&) const = default;
};
struct MaterialCost {
    uint16_t wood = 0, stone = 0, scrap = 0;
    bool operator==(const MaterialCost&) const = default;
};

[[nodiscard]] const ItemDefinition* itemDefinition(ItemKind kind) noexcept;
[[nodiscard]] bool validStack(ItemStack stack) noexcept;
[[nodiscard]] uint32_t itemCount(std::span<const ItemStack>, ItemKind) noexcept;
[[nodiscard]] bool canAfford(std::span<const ItemStack>, MaterialCost) noexcept;
// Callers pass private transaction copies. These helpers still leave the entire
// destination unchanged on refusal, including partially filled stacks.
[[nodiscard]] bool addItems(std::span<ItemStack>, ItemStack) noexcept;
[[nodiscard]] bool takeItems(std::span<ItemStack>, ItemStack) noexcept;
[[nodiscard]] bool consumeMaterials(std::span<ItemStack>, MaterialCost) noexcept;
[[nodiscard]] bool refundMaterials(std::span<ItemStack>, MaterialCost) noexcept;

} // namespace voxy::game::adventure

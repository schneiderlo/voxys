#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace voxy::game::expedition {

using BrickPaint = std::array<uint8_t,4>;
// The existing canonical default remains authored appearance for old designs.
// A deliberate Original choice differs from an absent brush preference: the
// latter keeps a stored part's color, while Original explicitly repaints it.
inline constexpr BrickPaint kOriginalBrickPaint{255,255,255,255};
struct BrickPaintChoice { std::string_view name; BrickPaint rgba; };
inline constexpr std::array<BrickPaintChoice,8> kBrickPaintPalette{{
    {"Original",kOriginalBrickPaint},
    {"Cream",{239,235,217,255}},
    {"Teal",{35,145,137,255}},
    {"Blue",{50,108,190,255}},
    {"Yellow",{239,191,54,255}},
    {"Red",{195,55,47,255}},
    {"Orange",{234,119,39,255}},
    {"Charcoal",{56,65,72,255}},
}};

[[nodiscard]] constexpr bool isPaintableBrick(std::string_view nameKey) noexcept {
    return nameKey=="salvage.part.brick_1x2"
        || nameKey=="salvage.part.brick_2x2"
        || nameKey=="salvage.part.brick_2x4";
}
[[nodiscard]] constexpr std::optional<uint32_t> brickPaintIndex(BrickPaint paint) noexcept {
    for(uint32_t i=0;i<kBrickPaintPalette.size();++i)
        if(kBrickPaintPalette[i].rgba==paint)return i;
    return {};
}
}

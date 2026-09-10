#pragma once
#include <cstdint>

namespace voxy::game::assets {
// Scene/catalogue budgets are independent of physics body and water-driver
// budgets. 96 placements leave room for 64 added bricks plus the Cove kit
// and fixed scenery. The compiler still checks every concrete shape budget.
inline constexpr uint32_t kMaximumFixtureBundles = 16;
inline constexpr uint32_t kMaximumFixturePlacements = 96;
inline constexpr uint32_t kMaximumFixtureConnections = 1024;
} // namespace voxy::game::assets

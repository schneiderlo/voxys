#pragma once

#include "game/expedition/cove_boat.hpp"
#include <array>

namespace voxy::game::expedition {

// Own every installed mission load, including banked loads still visible in
// the harbor. Durable mission roles belong to the session; GPU handles belong
// only to this runtime. The host drains bodies before retiring their shapes.
class CoveCargoSet {
public:
    static constexpr size_t maximumLoads=2;
    struct Load {
        // Borrowed stable role bindings; only GameSession allocates/owns them.
        construction::DurableId cargoId{},jobId{};
        std::unique_ptr<CoveBoatAssembly> assembly;
        physics::ShapeHandle shape{};
        physics::BodyHandle body{},replacedBody{};
        physics::AuthoredRootMotion spawn{},observed{},deliveredPose{};
        uint64_t admissionTick=0,observedTick=0,replacementDeadTick=0;
        bool retired=false,banked=false,securing=false,durable=false;
        glm::vec3 towPoint{};
        float towStrength=0;
    };

    [[nodiscard]] static std::optional<CoveCargoSet> prepare(
        const assets::LoadedAssetFixture&,std::string& error);
    [[nodiscard]] std::span<Load> loads() noexcept {return {loads_.data(),count_};}
    [[nodiscard]] std::span<const Load> loads() const noexcept {return {loads_.data(),count_};}
    // Empty inspection scenes still have an inert first slot, so ordinary
    // nullable-cargo checks need not manufacture a gameplay owner.
    [[nodiscard]] Load& primary() noexcept {return loads_[0];}
    [[nodiscard]] const Load& primary() const noexcept {return loads_[0];}
    [[nodiscard]] bool allAdmitted() const noexcept;
    [[nodiscard]] bool allRetired() const noexcept;
    [[nodiscard]] uint64_t joinedTick() const noexcept;
    [[nodiscard]] bool anySecuring() const noexcept;
    void includeBodyRange(uint32_t& first,uint32_t& last) const noexcept;

private:
    std::array<Load,maximumLoads> loads_{};
    size_t count_=0;
};

} // namespace voxy::game::expedition

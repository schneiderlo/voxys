#pragma once
#include "game/expedition/cove_boat.hpp"

namespace voxy::game::expedition {

// Bounded design editing for the starter workshop. These are blueprint commands,
// not GameSession transactions: they cannot buy parts, create durable identities,
// mutate the sailing body, or claim persistence. Live launch is a separate adapter.
class CoveWorkshop {
public:
    enum class Action { Previous, Next, Left, Right, Forward, Back, Raise, Lower,
                        Rotate, Snap, Keep, Undo, Revert, Remove };
    static constexpr size_t maximumHistory=32;
    [[nodiscard]] static std::unique_ptr<CoveWorkshop> create(
        const assets::LoadedAssetFixture&, std::string& error, uint32_t fixedPlacements = 0,
        std::span<const uint32_t> originalBoatSlots = {}, bool acceptedSeparated = false);
    [[nodiscard]] bool command(Action);
    [[nodiscard]] std::vector<std::byte> blueprintBytes(std::string& error) const;
    [[nodiscard]] bool loadBlueprint(std::span<const std::byte>,std::string& error);
    [[nodiscard]] bool selectCatalog(int direction) noexcept;
    [[nodiscard]] bool addPart(const construction::PartInstance* stored = nullptr);
    [[nodiscard]] bool canAdd() const noexcept;
    enum class SettingAction { Toggle, CycleLimit, Reverse };
    [[nodiscard]] bool configure(SettingAction);
    [[nodiscard]] bool configurable() const noexcept;
    [[nodiscard]] bool hasOutputLimit() const noexcept;
    [[nodiscard]] bool canReverse() const noexcept;
    [[nodiscard]] construction::ModuleSettings selectedSettings() const noexcept;
    [[nodiscard]] std::string_view catalogName() const noexcept;
    [[nodiscard]] construction::ResourceAmounts catalogCost() const noexcept;
    [[nodiscard]] construction::ContentKey catalogDefinition() const noexcept;
    [[nodiscard]] uint32_t catalogIndex() const noexcept { return catalogIndex_; }
    [[nodiscard]] size_t catalogCount() const noexcept { return catalog_.size(); }
    [[nodiscard]] const assets::LoadedAssetFixture& preview() const noexcept { return preview_; }
    [[nodiscard]] const assets::LoadedAssetFixture& design() const noexcept { return design_; }
    [[nodiscard]] uint32_t selected() const noexcept { return selected_; }
    [[nodiscard]] bool valid() const noexcept { return compiled_ && compiled_->roots().size()==1; }
    [[nodiscard]] bool changed() const noexcept;
    [[nodiscard]] bool matchesDesign(const assets::LoadedAssetFixture&) const noexcept;
    [[nodiscard]] size_t undoCount() const noexcept { return history_.size(); }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] std::string_view selectedName() const noexcept;
    [[nodiscard]] double massKg() const noexcept;
private:
    explicit CoveWorkshop(const assets::LoadedAssetFixture& scene):design_(scene),preview_(scene){}
    [[nodiscard]] const construction::PartDefinition& definition(uint32_t placement) const;
    [[nodiscard]] const construction::PartDefinition& definition(const assets::LoadedAssetFixture&,uint32_t placement) const;
    [[nodiscard]] bool reconnect(assets::LoadedAssetFixture&) const;
    void evaluate();
    [[nodiscard]] bool snap();
    assets::LoadedAssetFixture design_,preview_;
    std::vector<assets::AssetFixtureRegistry> history_;
    std::unique_ptr<CoveBoatAssembly> compiled_;
    uint32_t selected_=0;
    uint32_t fixedPlacements_=0,catalogIndex_=0;
    std::vector<assets::FixturePartPlacement> catalog_;
    std::vector<uint32_t> acceptedSlots_;
    std::vector<uint32_t> collisionBoatSlots_;
    uint64_t revision_=0;
    std::string message_;
};
} // namespace voxy::game::expedition

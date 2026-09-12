#pragma once
#include "game/expedition/cove_boat.hpp"
#include "game/expedition/workshop_camera.hpp"
#include "game/expedition/brick_paint.hpp"

namespace voxy::game::expedition {

// Bounded design editing for the starter workshop. These are blueprint commands,
// not GameSession transactions: they cannot buy parts, create durable identities,
// mutate the sailing body, or claim persistence. Live launch is a separate adapter.
class CoveWorkshop {
public:
    enum class Action { Previous, Next, Left, Right, Forward, Back, Raise, Lower,
                        Rotate, Snap, Keep, Undo, Revert, Remove, Redo };
    enum class SelectionMode { Replace, Toggle, Add };
    enum class Axis { X, Y, Z };
    enum class Problem { None, PendingEdit, InvalidSelection, UnsupportedOperation,
        NoFreeSlots, OutOfBounds, UnsupportedRotation, SocketCapacity, SolidOverlap,
        ConnectorClearance, IncompatibleSocket, NotConnected, InvalidSettings,
        PhysicalLimit, HistoryLimit };
    struct PlacementIssue {
        Problem problem=Problem::None;
        std::optional<uint32_t> placement;
    };
    static constexpr size_t maximumHistory=32;
    [[nodiscard]] static std::unique_ptr<CoveWorkshop> create(
        const assets::LoadedAssetFixture&, std::string& error, uint32_t fixedPlacements = 0,
        std::span<const uint32_t> originalBoatSlots = {}, bool acceptedSeparated = false);
    [[nodiscard]] bool command(Action);
    [[nodiscard]] std::vector<std::byte> blueprintBytes(std::string& error) const;
    [[nodiscard]] bool loadBlueprint(std::span<const std::byte>,std::string& error);
    [[nodiscard]] bool selectCatalog(int direction) noexcept;
    [[nodiscard]] bool selectCatalogAt(uint32_t index) noexcept;
    [[nodiscard]] std::string_view catalogNameAt(uint32_t index) const noexcept;
    [[nodiscard]] construction::ResourceAmounts catalogCostAt(uint32_t index) const noexcept;
    [[nodiscard]] std::optional<uint32_t> catalogBundleAt(uint32_t index) const noexcept {
        return index<catalog_.size()?std::optional<uint32_t>(catalog_[index].bundleIndex):std::nullopt;
    }
    [[nodiscard]] bool selectPart(uint32_t placement, SelectionMode mode=SelectionMode::Replace);
    [[nodiscard]] bool selectAll();
    [[nodiscard]] bool selectOnlyPrimary() { return selectPart(selected_); }
    [[nodiscard]] std::span<const uint32_t> selectedParts() const noexcept { return selection_; }
    [[nodiscard]] bool isSelected(uint32_t placement) const noexcept;
    // Checked grid edits form one preview and one Keep/Undo transaction. A
    // refused input leaves every member unchanged; invalid fits remain visible.
    [[nodiscard]] bool moveSelection(construction::GridPosition deltaTicks);
    [[nodiscard]] bool rotateSelection(Axis, int quarterTurns=1);
    [[nodiscard]] bool duplicateSelection(construction::GridPosition deltaTicks);
    // Explicit copy across canonical X/Z = planeTicks. Only the three admitted
    // reflection-symmetric bricks support mirroring; no negative-scale meshes.
    [[nodiscard]] bool mirrorSelection(Axis, int32_t planeTicks=0);
    // A replacement removes the old design member and allocates a distinct
    // dynamic slot. Existing physical definitions/IDs are never changed.
    [[nodiscard]] bool replaceSelection(uint32_t catalogIndex);
    struct Pick { uint32_t placement; glm::dvec3 point; };
    // Rays/points use metres in the canonical build frame, before the
    // workshop display offset. Picking follows the actual collision shells.
    [[nodiscard]] std::optional<Pick> pick(glm::dvec3 origin,glm::dvec3 direction,bool excludeSelected=false) const;
    // True means this target resolved a compatible mating transform, including
    // an unchanged transform. A false result must never accept an older ghost.
    [[nodiscard]] bool aimAt(const Pick&);
    [[nodiscard]] std::optional<WorkshopBounds> viewBounds(bool wholeBuild) const;
    [[nodiscard]] bool addPart(const construction::PartInstance* stored = nullptr);
    [[nodiscard]] bool canAdd() const noexcept;
    // The brick tool owns one unplaced preview. Only placeBrickTool/Keep adds
    // it to the design; stopping, switching or launching never buys the ghost.
    [[nodiscard]] bool beginBrickTool(uint32_t catalogIndex, const construction::PartInstance* stored = nullptr);
    [[nodiscard]] bool placeBrickTool(const construction::PartInstance* nextStored = nullptr);
    [[nodiscard]] bool stopBrickTool();
    [[nodiscard]] bool brickToolActive() const noexcept { return brickToolActive_; }
    [[nodiscard]] bool canChooseBrick() const noexcept;
    [[nodiscard]] bool canPaint() const noexcept;
    [[nodiscard]] std::optional<uint32_t> paintIndex() const noexcept;
    [[nodiscard]] BrickPaint currentPaint() const noexcept;
    [[nodiscard]] const std::optional<BrickPaint>& brushPaint() const noexcept { return brushPaint_; }
    // A preview edit; Keep/Launch remain authoritative. Explicit preference
    // carries through the brush. No preference preserves stored part paint.
    [[nodiscard]] bool setPaint(uint32_t index);
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
    [[nodiscard]] size_t redoCount() const noexcept { return future_.size(); }
    [[nodiscard]] PlacementIssue placementIssue() const noexcept { return issue_; }
    [[nodiscard]] uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] std::string_view selectedName() const noexcept;
    [[nodiscard]] double massKg() const noexcept;
private:
    explicit CoveWorkshop(const assets::LoadedAssetFixture& scene):design_(scene),preview_(scene){}
    [[nodiscard]] const construction::PartDefinition& definition(uint32_t placement) const;
    [[nodiscard]] const construction::PartDefinition& definition(const assets::LoadedAssetFixture&,uint32_t placement) const;
    [[nodiscard]] bool reconnect(assets::LoadedAssetFixture&) const;
    [[nodiscard]] bool reconnect(assets::LoadedAssetFixture&, std::span<const uint32_t> affected) const;
    [[nodiscard]] std::vector<uint32_t> affectedParts(const assets::LoadedAssetFixture&) const;
    [[nodiscard]] bool refuse(Problem, std::string_view, std::optional<uint32_t> = {});
    [[nodiscard]] bool cleanSelection();
    void normalizeSelection();
    void rememberSelection();
    [[nodiscard]] bool copySelection(construction::GridPosition, std::optional<Axis>, int32_t);
    [[nodiscard]] std::optional<std::vector<uint32_t>> freeSlots(size_t count) const;
    void evaluate();
    [[nodiscard]] bool snap();
    [[nodiscard]] bool hasFreePartSlot() const noexcept;
    [[nodiscard]] bool addPreview(uint32_t catalogIndex, const construction::PartInstance*, construction::GridTransform from);
    assets::LoadedAssetFixture design_,preview_;
    struct HistoryEntry {
        assets::AssetFixtureRegistry registry;
        std::vector<uint32_t> selection;
        uint32_t primary=0;
        bool separated=false;
    };
    [[nodiscard]] HistoryEntry currentHistory() const { return {design_.registry,keptSelection_,keptPrimary_,designSeparated_}; }
    std::vector<HistoryEntry> history_,future_;
    std::unique_ptr<CoveBoatAssembly> compiled_;
    uint32_t selected_=0;
    std::vector<uint32_t> selection_,keptSelection_;
    uint32_t keptPrimary_=0;
    uint32_t fixedPlacements_=0,catalogIndex_=0;
    std::vector<assets::FixturePartPlacement> catalog_;
    std::vector<uint32_t> acceptedSlots_;
    std::vector<uint32_t> collisionBoatSlots_;
    uint64_t revision_=0;
    bool brickToolActive_=false;
    bool designSeparated_=false;
    uint32_t brickToolAnchor_=0;
    std::optional<BrickPaint> brushPaint_;
    PlacementIssue issue_;
    std::string message_;
};
} // namespace voxy::game::expedition

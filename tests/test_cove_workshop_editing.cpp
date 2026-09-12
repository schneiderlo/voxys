#include "game/expedition/cove_workshop.hpp"
#include "game/expedition/cove_build.hpp"
#include "game/expedition/cove_save.hpp"
#include "game/construction/assembly_fracture.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

namespace voxy::game::expedition {
namespace {
using namespace construction;
using Action=CoveWorkshop::Action;
using Mode=CoveWorkshop::SelectionMode;
using Axis=CoveWorkshop::Axis;
using Problem=CoveWorkshop::Problem;

// Use admitted production brick definitions, collision and sockets. Small
// connected builds isolate editor transactions from the navigation fixture.
class CoveWorkshopEditing : public testing::Test {
protected:
    static inline std::unique_ptr<const assets::LoadedAssetFixture> installed;
    std::string error;
    static void SetUpTestSuite() {
        std::string reason;
        const auto base=assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),reason);
        ASSERT_TRUE(base)<<reason;
        installed=assets::appendAssetFixtureCatalog(*base,std::filesystem::canonical("data/salvage/cove-bricks-r02.json"),reason);
        ASSERT_TRUE(installed)<<reason;
    }
    static void TearDownTestSuite() { installed.reset(); }
    uint32_t bundle(std::string_view name) const {
        for(uint32_t i=0;i<installed->bundles.size();++i)
            if(installed->bundles[i]->sidecar().part.nameKey==name)return i;
        return UINT32_MAX;
    }
    uint32_t catalog(const CoveWorkshop& editor,std::string_view name) const {
        for(uint32_t i=0;i<editor.catalogCount();++i)if(editor.catalogNameAt(i)==name)return i;
        return UINT32_MAX;
    }
    static std::vector<uint32_t> selected(const CoveWorkshop& editor) {
        return {editor.selectedParts().begin(),editor.selectedParts().end()};
    }
    static auto edges(const assets::AssetFixtureRegistry& registry) {
        std::vector<std::tuple<uint32_t,uint32_t,uint64_t,uint64_t>> result;
        for(const auto& c:registry.connections)result.emplace_back(c.aPlacement,c.bPlacement,c.aSocket.value(),c.bSocket.value());
        return result;
    }
    static auto frames(const assets::AssetFixtureRegistry& registry) {
        std::vector<GridTransform> result;
        for(const auto& p:registry.placements)result.push_back(p.placement);
        return result;
    }
    assets::LoadedAssetFixture scene(std::vector<assets::FixturePartPlacement> parts) const {
        auto result=*installed;result.registry.placements=std::move(parts);
        result.registry.connections.clear();auto& nav=*result.registry.navigation;
        nav.boatPlacements.resize(result.registry.placements.size());
        std::iota(nav.boatPlacements.begin(),nav.boatPlacements.end(),0u);nav.cargoPlacements.clear();
        for(uint32_t a=0;a<result.registry.placements.size();++a)for(uint32_t b=a+1;b<result.registry.placements.size();++b) {
            const auto& pa=result.registry.placements[a];const auto& pb=result.registry.placements[b];
            for(const auto& sa:result.bundles[pa.bundleIndex]->sidecar().part.sockets)
                for(const auto& sb:result.bundles[pb.bundleIndex]->sidecar().part.sockets) {
                    const auto af=compose(pa.placement,sa.frame),bf=compose(pb.placement,sb.frame);
                    if(af&&bf&&af->translation==bf->translation&&compose(af->rotation,CubeRotation{2})==bf->rotation
                        &&matchSockets(sa,sb,ConnectionKind::Weld)==SocketMatchError::None)
                        result.registry.connections.push_back({a,b,sa.id,sb.id});
                }
        }
        return result;
    }
    assets::LoadedAssetFixture stack(uint32_t count) const {
        std::vector<assets::FixturePartPlacement> parts;
        for(uint32_t i=0;i<count;++i)parts.push_back({bundle("salvage.part.brick_2x4"),{{0,static_cast<int32_t>(48*i),0},{}}});
        return scene(std::move(parts));
    }
};

TEST_F(CoveWorkshopEditing, SelectionIsNonemptySortedAndCannotDiscardAnOrdinaryEdit) {
    const auto source=stack(3);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(2));ASSERT_TRUE(editor->selectPart(0,Mode::Add));
    ASSERT_TRUE(editor->selectPart(2,Mode::Add));EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0,2}));
    EXPECT_EQ(editor->selected(),2u);EXPECT_TRUE(editor->isSelected(0));EXPECT_FALSE(editor->isSelected(1));
    ASSERT_TRUE(editor->selectPart(2,Mode::Toggle));EXPECT_EQ(editor->selected(),0u);
    EXPECT_FALSE(editor->selectPart(0,Mode::Toggle));EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0}));
    EXPECT_FALSE(editor->selectPart(96));ASSERT_TRUE(editor->selectAll());EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0,1,2}));
    EXPECT_FALSE(editor->command(Action::Remove));EXPECT_FALSE(editor->changed());
    const auto original=editor->blueprintBytes(error);ASSERT_FALSE(original.empty());
    ASSERT_TRUE(editor->moveSelection({50,0,0}));ASSERT_TRUE(editor->valid())<<editor->message();
    const auto pending=frames(editor->preview().registry);
    EXPECT_FALSE(editor->selectPart(1));EXPECT_FALSE(editor->selectAll());EXPECT_FALSE(editor->command(Action::Next));
    EXPECT_FALSE(editor->command(Action::Undo));EXPECT_FALSE(editor->command(Action::Redo));
    EXPECT_EQ(editor->placementIssue().problem,Problem::PendingEdit);EXPECT_EQ(frames(editor->preview().registry),pending);
    EXPECT_EQ(editor->undoCount(),0u);EXPECT_EQ(editor->redoCount(),0u);
    ASSERT_TRUE(editor->command(Action::Revert));EXPECT_EQ(editor->blueprintBytes(error),original);
    EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0,1,2}));ASSERT_TRUE(editor->selectOnlyPrimary());
    EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0}));
}

TEST_F(CoveWorkshopEditing, GroupMotionPreservesRelativeFramesAndReturningRestoresExactWelds) {
    const auto source=stack(3);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(1));ASSERT_TRUE(editor->selectPart(2,Mode::Add));
    const auto original=frames(editor->preview().registry);const auto welds=edges(source.registry);
    ASSERT_TRUE(editor->moveSelection({0,100,0}));EXPECT_FALSE(editor->valid());
    EXPECT_EQ(editor->placementIssue().problem,Problem::NotConnected);
    EXPECT_EQ(editor->preview().registry.placements[0].placement,original[0]);
    EXPECT_EQ(editor->preview().registry.placements[2].placement.translation.y-editor->preview().registry.placements[1].placement.translation.y,48);
    EXPECT_FALSE(editor->command(Action::Keep));EXPECT_EQ(editor->undoCount(),0u);
    ASSERT_TRUE(editor->moveSelection({0,-100,0}));ASSERT_TRUE(editor->valid())<<editor->message();
    EXPECT_FALSE(editor->changed());EXPECT_EQ(edges(editor->preview().registry),welds);
    ASSERT_TRUE(editor->selectAll());const auto primary=editor->selected();const auto pivot=original[primary].translation;
    ASSERT_TRUE(editor->rotateSelection(Axis::Z));ASSERT_TRUE(editor->valid())<<editor->message();
    EXPECT_EQ(editor->preview().registry.placements[primary].placement.translation,pivot);
    for(size_t i=0;i<original.size();++i) {
        const auto next=editor->preview().registry.placements[i].placement;
        EXPECT_EQ(next.translation.x,pivot.x-(original[i].translation.y-pivot.y));
        EXPECT_EQ(next.translation.y,pivot.y+(original[i].translation.x-pivot.x));
    }
    ASSERT_TRUE(editor->rotateSelection(Axis::Z,-1));EXPECT_EQ(frames(editor->preview().registry),original);
    EXPECT_EQ(edges(editor->preview().registry),welds);EXPECT_FALSE(editor->changed());
    for(const auto axis:{Axis::X,Axis::Y,Axis::Z}) {
        ASSERT_TRUE(editor->rotateSelection(axis));ASSERT_TRUE(editor->rotateSelection(axis,3));
        EXPECT_EQ(frames(editor->preview().registry),original);EXPECT_FALSE(editor->changed());
    }
    const auto bounds=editor->viewBounds(false);ASSERT_TRUE(bounds);EXPECT_GT(bounds->maximum.y-bounds->minimum.y,2.8);
    EXPECT_FALSE(editor->pick({0,8,0},{0,-1,0},true));
}

TEST_F(CoveWorkshopEditing, GroupAimResolvesPrimaryButMovesEverySelectedPart) {
    const auto source=stack(3);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(2));ASSERT_TRUE(editor->selectPart(1,Mode::Add)); // Lower member is the primary.
    ASSERT_TRUE(editor->moveSelection({0,96,0}));EXPECT_FALSE(editor->valid());
    const auto relative=checkedSubtract(editor->preview().registry.placements[2].placement.translation,
        editor->preview().registry.placements[1].placement.translation);ASSERT_TRUE(relative);
    EXPECT_FALSE(editor->aimAt({2,{0,0,0}}));
    ASSERT_TRUE(editor->aimAt({0,{0,.96,0}}));ASSERT_TRUE(editor->valid())<<editor->message();
    EXPECT_EQ(checkedSubtract(editor->preview().registry.placements[2].placement.translation,
        editor->preview().registry.placements[1].placement.translation),relative);
    EXPECT_EQ(frames(editor->preview().registry),frames(source.registry));EXPECT_FALSE(editor->changed());
    EXPECT_TRUE(editor->aimAt({0,{0,.96,0}})); // Unchanged compatible target remains a successful resolution.
}

TEST_F(CoveWorkshopEditing, RefusedOverflowAndOverlapNeverPartiallyChangeTheKeptDesign) {
    const auto source=stack(2);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectAll());const auto original=frames(editor->preview().registry);
    EXPECT_FALSE(editor->moveSelection({0,std::numeric_limits<int32_t>::max(),0}));
    EXPECT_EQ(editor->placementIssue().problem,Problem::OutOfBounds);EXPECT_EQ(frames(editor->preview().registry),original);
    EXPECT_FALSE(editor->duplicateSelection({std::numeric_limits<int32_t>::min(),0,0}));
    EXPECT_EQ(frames(editor->preview().registry),original);EXPECT_FALSE(editor->changed());
    ASSERT_TRUE(editor->duplicateSelection({}));EXPECT_FALSE(editor->valid());
    EXPECT_EQ(editor->placementIssue().problem,Problem::SolidOverlap);EXPECT_TRUE(editor->placementIssue().placement);
    EXPECT_FALSE(editor->command(Action::Keep));EXPECT_EQ(frames(editor->design().registry),original);
    EXPECT_EQ(editor->undoCount(),0u);ASSERT_TRUE(editor->command(Action::Revert));
    EXPECT_FALSE(editor->mirrorSelection(Axis::X,std::numeric_limits<int32_t>::max()));
    EXPECT_EQ(editor->placementIssue().problem,Problem::OutOfBounds);EXPECT_EQ(frames(editor->preview().registry),original);
}

TEST_F(CoveWorkshopEditing, GroupPaintRemoveAndHistoryRestoreExactSelectionAndBlueprint) {
    const auto source=stack(3);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(1));ASSERT_TRUE(editor->selectPart(2,Mode::Add));
    const auto original=editor->blueprintBytes(error);ASSERT_FALSE(original.empty());
    ASSERT_TRUE(editor->setPaint(2));ASSERT_TRUE(editor->command(Action::Keep));
    const auto painted=editor->blueprintBytes(error);ASSERT_NE(painted,original);
    EXPECT_FALSE(editor->design().registry.placements[0].paint);
    EXPECT_EQ(editor->design().registry.placements[1].paint,kBrickPaintPalette[2].rgba);
    EXPECT_EQ(editor->design().registry.placements[2].paint,kBrickPaintPalette[2].rgba);
    ASSERT_TRUE(editor->command(Action::Remove));ASSERT_TRUE(editor->valid())<<editor->message();
    ASSERT_TRUE(editor->command(Action::Keep));const auto removed=editor->blueprintBytes(error);
    EXPECT_EQ(editor->design().registry.navigation->boatPlacements,(std::vector<uint32_t>{0}));
    ASSERT_TRUE(editor->command(Action::Undo));EXPECT_EQ(editor->blueprintBytes(error),painted);
    EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{1,2}));EXPECT_EQ(editor->selected(),2u);
    ASSERT_TRUE(editor->command(Action::Redo));EXPECT_EQ(editor->blueprintBytes(error),removed);
    EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{0}));
    ASSERT_TRUE(editor->command(Action::Undo));ASSERT_TRUE(editor->command(Action::Undo));
    EXPECT_EQ(editor->blueprintBytes(error),original);EXPECT_EQ(editor->redoCount(),2u);
    ASSERT_TRUE(editor->setPaint(3));ASSERT_TRUE(editor->command(Action::Keep));EXPECT_EQ(editor->redoCount(),0u);
    EXPECT_FALSE(editor->command(Action::Redo));EXPECT_EQ(editor->undoCount(),1u);
}

TEST_F(CoveWorkshopEditing, HistoryBoundAppliesAcrossUndoAndRedoAndLoadBranches) {
    const auto source=stack(1);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    const auto initial=editor->blueprintBytes(error);
    for(size_t i=0;i<CoveWorkshop::maximumHistory+3;++i) {
        ASSERT_TRUE(editor->moveSelection({50,0,0}));ASSERT_TRUE(editor->command(Action::Keep));
    }
    const auto latest=editor->blueprintBytes(error);ASSERT_FALSE(latest.empty());
    EXPECT_EQ(editor->undoCount(),CoveWorkshop::maximumHistory);
    for(size_t i=0;i<CoveWorkshop::maximumHistory;++i) {
        ASSERT_TRUE(editor->command(Action::Undo));EXPECT_EQ(editor->undoCount()+editor->redoCount(),CoveWorkshop::maximumHistory);
    }
    EXPECT_FALSE(editor->command(Action::Undo));EXPECT_EQ(editor->design().registry.placements[0].placement.translation.x,150);
    for(size_t i=0;i<CoveWorkshop::maximumHistory;++i)ASSERT_TRUE(editor->command(Action::Redo));
    EXPECT_EQ(editor->blueprintBytes(error),latest);ASSERT_TRUE(editor->command(Action::Undo));
    ASSERT_TRUE(editor->loadBlueprint(initial,error))<<error;EXPECT_EQ(editor->redoCount(),0u);
    EXPECT_EQ(editor->blueprintBytes(error),initial);EXPECT_EQ(editor->undoCount(),CoveWorkshop::maximumHistory);
}

TEST_F(CoveWorkshopEditing, MirrorUsesProperRotationsAndARealCompatibleStudPlacement) {
    const auto large=bundle("salvage.part.brick_2x4"),small=bundle("salvage.part.brick_1x2");
    const auto source=scene({{large,{}},{small,{{-50,48,-25},{}}}});
    auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(1));ASSERT_TRUE(editor->mirrorSelection(Axis::X));
    ASSERT_TRUE(editor->valid())<<editor->message();const auto copy=editor->selected();EXPECT_NE(copy,1u);
    EXPECT_EQ(editor->preview().registry.placements[copy].placement,(GridTransform{{50,48,-25},{}}));
    ASSERT_TRUE(editor->command(Action::Keep));EXPECT_EQ(editor->design().registry.navigation->boatPlacements.size(),3u);
    ASSERT_TRUE(editor->command(Action::Undo));EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{1}));
    // Every proper input orientation remains proper after world/local double
    // reflection, including orientations whose local up is not world up.
    for(uint8_t rotation=0;rotation<24;++rotation)for(const auto axis:{Axis::X,Axis::Z}) {
        const auto single=scene({{small,{{75,48,-25},CubeRotation{rotation}}}});
        auto rotated=CoveWorkshop::create(single,error);ASSERT_TRUE(rotated)<<error;
        ASSERT_TRUE(rotated->mirrorSelection(axis,10));
        const auto next=rotated->preview().registry.placements[rotated->selected()].placement;
        const auto a=rotationMatrix(CubeRotation{rotation}),b=rotationMatrix(next.rotation);ASSERT_TRUE(a);ASSERT_TRUE(b);
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            EXPECT_EQ(b->elements[static_cast<size_t>(row*3+col)],a->elements[static_cast<size_t>(row*3+col)]
                *(row==(axis==Axis::X?0:2)?-1:1)*(col==0?-1:1));
        EXPECT_EQ(next.translation.x,axis==Axis::X?-55:75);
        EXPECT_EQ(next.translation.z,axis==Axis::Z?45:-25);
        EXPECT_EQ(rotated->design().registry.placements.size(),1u);
    }
    auto machinery=CoveWorkshop::create(*installed,error);ASSERT_TRUE(machinery)<<error;
    EXPECT_EQ(machinery->selectedName(),"Winch");const auto before=frames(machinery->preview().registry);
    EXPECT_FALSE(machinery->mirrorSelection(Axis::X));EXPECT_EQ(machinery->placementIssue().problem,Problem::UnsupportedOperation);
    EXPECT_EQ(frames(machinery->preview().registry),before);EXPECT_FALSE(machinery->changed());
}

TEST_F(CoveWorkshopEditing, ReplacementUsesNewSlotsAndKeepsOwnedDefinitionsImmutable) {
    const auto source=stack(2);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectPart(1));ASSERT_TRUE(editor->setPaint(4));ASSERT_TRUE(editor->command(Action::Keep));
    const auto old=editor->design().registry.placements[1];
    ASSERT_TRUE(editor->replaceSelection(catalog(*editor,"Brick 1 x 2")));
    const auto fresh=editor->selected();EXPECT_NE(fresh,1u);EXPECT_GE(fresh,source.registry.placements.size());
    EXPECT_EQ(editor->preview().registry.placements[1].bundleIndex,old.bundleIndex);
    EXPECT_EQ(editor->preview().registry.placements[fresh].placement,old.placement);
    EXPECT_EQ(editor->preview().registry.placements[fresh].paint,old.paint);
    EXPECT_EQ(editor->preview().registry.placements[fresh].settings,
        defaultModuleSettings(installed->bundles[bundle("salvage.part.brick_1x2")]->sidecar().part));
    EXPECT_EQ(editor->preview().registry.navigation->boatPlacements,(std::vector<uint32_t>{0,fresh}));
    // Different stud patterns may need one ordinary group nudge before Keep.
    ASSERT_TRUE(editor->moveSelection({0,0,25}));ASSERT_TRUE(editor->valid())<<editor->message();
    ASSERT_TRUE(editor->command(Action::Keep));EXPECT_FALSE(editor->replaceSelection(catalog(*editor,"Brick 1 x 2")));
    ASSERT_TRUE(editor->command(Action::Undo));EXPECT_EQ(selected(*editor),(std::vector<uint32_t>{1}));
    EXPECT_EQ(editor->design().registry.placements[1].bundleIndex,old.bundleIndex);
    ASSERT_TRUE(editor->command(Action::Redo));EXPECT_EQ(editor->selected(),fresh);
}

TEST_F(CoveWorkshopEditing, CopyAndReplacementUseActualPaidOrStoredAuthorityWithoutLoanRefunds) {
    const auto source=stack(1);const WorldNamespace world{{'e','d','i','t','o','r','-','o','w','n','e','r'}};
    const auto seed=prepareCoveBuild(source,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto owned=CoveBoatAssembly::compileBuild(seed->build,seed->catalog,seed->placements,error);ASSERT_TRUE(owned)<<error;
    auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->duplicateSelection({0,48,0}));ASSERT_TRUE(editor->valid())<<editor->message();
    ASSERT_TRUE(editor->command(Action::Keep));
    const auto quote=quoteCoveDesign(editor->design(),*owned,seed->catalog);ASSERT_TRUE(quote);
    EXPECT_EQ(quote->debit.salvageMaterial,5u);EXPECT_EQ(quote->credit,ResourceAmounts{});
    PartInstance stored=seed->build.parts.front();stored.id.counter=1000;stored.provenance={};stored.health=4321;stored.paint={1,2,3,255};
    const std::array stock{stored};
    const auto request=prepareCoveRefit(editor->design(),*owned,seed->catalog,error,source.registry.navigation->boatPlacements,stock);ASSERT_TRUE(request)<<error;
    ASSERT_EQ(request->design->parts().size(),2u);
    EXPECT_EQ(request->design->parts()[0].source,seed->build.parts[0].id);EXPECT_EQ(request->design->parts()[1].source,stored.id);
    BuildIssue issue;const auto model=BuildModel::create(seed->build,seed->catalog,issue);ASSERT_TRUE(model);uint64_t issued=1000;
    const auto paid=prepareBuildRefit(*model,*request->design,seed->catalog,[&]{return DurableId{world,++issued};},issue,{stock,{}});ASSERT_TRUE(paid)<<issue.field;
    EXPECT_EQ(paid->debit,ResourceAmounts{});EXPECT_TRUE(paid->storedPartsAfter.empty());
    EXPECT_EQ(paid->after.parts[0],seed->build.parts[0]);EXPECT_EQ(paid->after.parts[1].id,stored.id);
    EXPECT_EQ(paid->after.parts[1].health,stored.health);EXPECT_EQ(paid->after.parts[1].provenance,PartProvenance{});
    // Replacing the sole original loan cannot mutate its definition or issue
    // salvage credit. Authority purchases a distinct smaller brick.
    auto replace=CoveWorkshop::create(source,error);ASSERT_TRUE(replace)<<error;
    ASSERT_TRUE(replace->replaceSelection(catalog(*replace,"Brick 1 x 2")));ASSERT_TRUE(replace->command(Action::Keep));
    const auto replacement=prepareCoveRefit(replace->design(),*owned,seed->catalog,error,source.registry.navigation->boatPlacements);ASSERT_TRUE(replacement)<<error;
    ASSERT_EQ(replacement->design->parts().size(),1u);EXPECT_EQ(replacement->design->parts()[0].source,DurableId{});
    const auto bought=prepareBuildRefit(*model,*replacement->design,seed->catalog,[&]{return DurableId{world,++issued};},issue);ASSERT_TRUE(bought)<<issue.field;
    EXPECT_EQ(bought->debit.salvageMaterial,2u);EXPECT_EQ(bought->credit,ResourceAmounts{});
    EXPECT_NE(bought->after.parts[0].id,seed->build.parts[0].id);EXPECT_NE(bought->after.parts[0].definition,seed->build.parts[0].definition);
    EXPECT_EQ(bought->after.parts[0].provenance,PartProvenance{});
}

TEST_F(CoveWorkshopEditing, MixedConfigurationIsAtomicAndUsesThePrimaryValue) {
    auto editor=CoveWorkshop::create(*installed,error);ASSERT_TRUE(editor)<<error;
    uint32_t helm=UINT32_MAX,propeller=UINT32_MAX,winch=UINT32_MAX;
    for(const auto slot:installed->registry.navigation->boatPlacements) {
        const auto& module=installed->bundles[installed->registry.placements[slot].bundleIndex]->sidecar().part.module;
        if(std::holds_alternative<HelmModule>(module))helm=slot;
        if(std::holds_alternative<PropellerModule>(module))propeller=slot;
        if(std::holds_alternative<WinchModule>(module))winch=slot;
    }
    ASSERT_NE(helm,UINT32_MAX);ASSERT_NE(propeller,UINT32_MAX);ASSERT_NE(winch,UINT32_MAX);
    ASSERT_TRUE(editor->selectPart(helm));ASSERT_TRUE(editor->selectPart(propeller,Mode::Add));
    EXPECT_TRUE(editor->configurable());EXPECT_TRUE(editor->hasOutputLimit());EXPECT_FALSE(editor->canReverse());
    EXPECT_FALSE(editor->configure(CoveWorkshop::SettingAction::Reverse));EXPECT_FALSE(editor->changed());
    ASSERT_TRUE(editor->configure(CoveWorkshop::SettingAction::CycleLimit));
    for(const auto slot:{helm,propeller})EXPECT_EQ(editor->preview().registry.placements[slot].settings->limitPermille,750);
    ASSERT_TRUE(editor->command(Action::Keep));ASSERT_TRUE(editor->selectPart(winch,Mode::Add));
    EXPECT_FALSE(editor->hasOutputLimit());EXPECT_FALSE(editor->configure(CoveWorkshop::SettingAction::CycleLimit));EXPECT_FALSE(editor->changed());
    ASSERT_TRUE(editor->configure(CoveWorkshop::SettingAction::Toggle));
    for(const auto slot:{helm,propeller,winch})EXPECT_FALSE(editor->preview().registry.placements[slot].settings->enabled);
    ASSERT_TRUE(editor->command(Action::Keep));ASSERT_TRUE(editor->selectPart(0,Mode::Add));
    EXPECT_FALSE(editor->configure(CoveWorkshop::SettingAction::Toggle));EXPECT_FALSE(editor->setPaint(2));EXPECT_FALSE(editor->changed());
}

TEST_F(CoveWorkshopEditing, SixtyFourBricksRemainEditableAndGroupCapacityRefusesBeforeMutation) {
    auto source=stack(64);auto editor=CoveWorkshop::create(source,error);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectAll());ASSERT_TRUE(editor->setPaint(5));ASSERT_TRUE(editor->command(Action::Keep));
    const auto painted=editor->blueprintBytes(error);ASSERT_FALSE(painted.empty());
    EXPECT_EQ(editor->selectedParts().size(),64u);EXPECT_LE(painted.size(),kMaximumCoveRecoveryDesignBytes);
    EXPECT_FALSE(editor->duplicateSelection({0,64*48,0}));EXPECT_EQ(editor->placementIssue().problem,Problem::NoFreeSlots);
    EXPECT_EQ(editor->preview().registry.placements.size(),64u);EXPECT_FALSE(editor->changed());
    EXPECT_FALSE(editor->replaceSelection(catalog(*editor,"Brick 1 x 2")));EXPECT_EQ(editor->placementIssue().problem,Problem::NoFreeSlots);
    EXPECT_FALSE(editor->changed());EXPECT_EQ(editor->blueprintBytes(error),painted);
    ASSERT_TRUE(editor->command(Action::Undo));ASSERT_TRUE(editor->command(Action::Redo));EXPECT_EQ(editor->blueprintBytes(error),painted);
    // Only one free scene slot cannot admit even the first member of a pair.
    source.registry.placements.resize(assets::kMaximumFixturePlacements-1,source.registry.placements.front());
    auto crowded=CoveWorkshop::create(source,error);ASSERT_TRUE(crowded)<<error;
    ASSERT_TRUE(crowded->selectPart(62));ASSERT_TRUE(crowded->selectPart(63,Mode::Add));
    EXPECT_FALSE(crowded->duplicateSelection({0,96,0}));EXPECT_FALSE(crowded->replaceSelection(catalog(*crowded,"Brick 1 x 2")));
    EXPECT_EQ(crowded->preview().registry.placements.size(),95u);EXPECT_EQ(crowded->preview().registry.navigation->boatPlacements.size(),64u);
    EXPECT_FALSE(crowded->changed());EXPECT_EQ(selected(*crowded),(std::vector<uint32_t>{62,63}));
}

TEST_F(CoveWorkshopEditing, HistoryRestoresTrustedSeparatedOwnedStateWithoutRepairingIt) {
    const WorldNamespace world{{'e','d','i','t','-','c','u','t','-','h','i','s','t'}};
    const auto seed=prepareCoveBuild(*installed,{world,1},4,error);ASSERT_TRUE(seed)<<error;
    const auto intact=makeCoveRecoveryDesign(seed->build,seed->catalog);ASSERT_FALSE(intact.empty());
    const auto part=seed->build.parts.front().id;std::vector<DurableId> cuts;
    for(const auto& weld:seed->build.connections)if(weld.a.part==part||weld.b.part==part)cuts.push_back(weld.id);
    AssemblyFractureIssue issue;const auto split=AssemblyFracturePlan::prepare(seed->build,seed->build.revision,cuts,seed->catalog,issue);ASSERT_TRUE(split);
    const auto broken=prepareCoveExpandedLaunchDesign(*installed,*installed,split->afterBuild(),seed->placements,{},error,CoveSceneTopology::AcceptedRoots);ASSERT_TRUE(broken)<<error;
    auto editor=CoveWorkshop::create(broken->scene,error,0,{},true);ASSERT_TRUE(editor)<<error;
    ASSERT_TRUE(editor->selectAll());const auto selection=selected(*editor);EXPECT_FALSE(editor->valid());
    ASSERT_TRUE(editor->loadBlueprint(intact,error))<<error;EXPECT_TRUE(editor->valid());
    const auto loaded=editor->blueprintBytes(error);ASSERT_FALSE(loaded.empty());
    ASSERT_TRUE(editor->command(Action::Undo));EXPECT_FALSE(editor->valid());EXPECT_FALSE(editor->changed());
    EXPECT_EQ(editor->placementIssue().problem,Problem::NotConnected);
    EXPECT_TRUE(editor->matchesDesign(broken->scene));EXPECT_EQ(selected(*editor),selection);
    EXPECT_EQ(edges(editor->design().registry),edges(broken->scene.registry));EXPECT_DOUBLE_EQ(editor->massKg(),1035);
    ASSERT_TRUE(editor->command(Action::Redo));EXPECT_TRUE(editor->valid());EXPECT_EQ(editor->blueprintBytes(error),loaded);
}
} // namespace
} // namespace voxy::game::expedition

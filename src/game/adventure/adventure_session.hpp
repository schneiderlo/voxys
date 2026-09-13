#pragma once

#include "core/sha256.hpp"
#include "game/adventure/building_catalog.hpp"
#include "game/adventure/item_catalog.hpp"
#include "game/construction/construction_types.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace voxy::game::adventure {
inline constexpr size_t kBackpackSlots=24, kChestSlots=32;
inline constexpr size_t kMaximumStructures=4, kMaximumParts=1024, kMaximumComponents=32;
inline constexpr size_t kMaximumResourceNodes=256;
inline constexpr size_t kMaximumBlueprintParts=64;
using GridPosition=construction::GridPosition;

struct PlayerPose {
    double x=0,y=0,z=0,yaw=0;
    bool operator==(const PlayerPose&) const = default;
};
struct WorldPart {
    uint64_t id=0;
    PieceKind kind=PieceKind::Foundation;
    GridPosition position{}; // Absolute world lattice, X/Z center and Y bottom.
    uint8_t yawQuarterTurns=0;
    uint32_t paint=0; // 0 authored; otherwise packed opaque RGB <= 0xffffff.
    bool operator==(const WorldPart&) const = default;
};
struct WorldStructure {
    uint64_t id=0, owner=1, revision=0;
    GridPosition origin{}; // First anchor defines one region owner; parts may cross it.
    std::vector<WorldPart> parts; // Increasing durable ID order, never GPU slots.
    bool operator==(const WorldStructure&) const = default;
};
struct StructureComponent {
    uint64_t id=0, structure=0, part=0, owner=1, revision=0;
    FurnitureKind kind=FurnitureKind::None;
    std::array<ItemStack,kChestSlots> slots{}; // Empty for beds/benches.
    bool operator==(const StructureComponent&) const = default;
};
struct AdventureState {
    construction::WorldNamespace world{};
    core::Sha256Digest content{};
    uint64_t epoch=1, revision=0, lastRequestSequence=0, lastIssuedId=2;
    bool starterGranted=false;
    PlayerPose player{};
    uint16_t health=100;
    uint64_t backpackRevision=0;
    std::array<ItemStack,kBackpackSlots> backpack{};
    ItemStack equippedTool{};
    uint64_t registeredBed=0;
    PlayerPose recovery{};
    std::vector<WorldStructure> structures;
    std::vector<StructureComponent> components;
    std::vector<uint32_t> depletedNodes; // Installed content node IDs, ascending.
    bool operator==(const AdventureState&) const = default;
};
struct ResourceNode {
    uint32_t id=0;
    PlayerPose position{};
    ItemStack yield{ItemKind::Wood,8};
    bool operator==(const ResourceNode&) const = default;
};
struct AdventureContent {
    core::Sha256Digest identity{};
    PlayerPose town{};
    std::vector<ResourceNode> resourceNodes;
};
struct CommandStamp { uint64_t expectedRevision=0, sequence=0, caller=1; };
struct PlacePart {
    uint64_t structure=0; // 0 creates a structure; only a terrain anchor may start one.
    PieceKind kind=PieceKind::Foundation;
    GridPosition position{};
    uint8_t yawQuarterTurns=0;
    uint32_t paint=0;
};
struct TransferItems {
    uint64_t source=0,destination=0; // 0 is backpack, otherwise stable chest component ID.
    uint64_t sourceRevision=0,destinationRevision=0;
    uint8_t sourceSlot=0;
    uint16_t quantity=0;
};

// The runtime validator is a trusted adapter, not a player-provided callback.
// It checks the complete candidate using installed terrain + admitted solids,
// reach, protected access, support and (for bed use) usable shelter/clearance.
// The session always validates ownership, inventory and structural references.
using CandidateValidator=std::function<bool(const AdventureState&,const AdventureState&,std::string&)>;

class AdventureSession {
public:
    class PreparedChange {
    public:
        PreparedChange(PreparedChange&&) noexcept=default;
        PreparedChange& operator=(PreparedChange&&) noexcept=default;
        PreparedChange(const PreparedChange&)=delete;
        PreparedChange& operator=(const PreparedChange&)=delete;
        [[nodiscard]] const AdventureState& state() const noexcept {return candidate_;}
        [[nodiscard]] uint64_t changedPart() const noexcept {return changedPart_;}
        [[nodiscard]] uint64_t changedStructure() const noexcept {return changedStructure_;}
    private:
        friend class AdventureSession;
        PreparedChange()=default;
        AdventureState candidate_;
        uint64_t baseRevision_=0,changedPart_=0,changedStructure_=0;
        const AdventureSession* owner_=nullptr;
    };
    [[nodiscard]] static std::unique_ptr<AdventureSession> create(
        construction::WorldNamespace,const AdventureContent&,std::string&);
    [[nodiscard]] static std::unique_ptr<AdventureSession> restore(
        const AdventureState&,const AdventureContent&,std::string&);
    [[nodiscard]] static bool validate(const AdventureState&,const AdventureContent&,std::string&);
    [[nodiscard]] const AdventureState& state() const noexcept {return state_;}
    [[nodiscard]] const AdventureContent& content() const noexcept {return content_;}
    [[nodiscard]] std::optional<PreparedChange> preparePlace(CommandStamp,PlacePart,const CandidateValidator&,std::string&) const;
    // One bounded layout, one request/revision and one final geometry check.
    // The first terrain anchor chooses/creates its structure; following zero
    // structure IDs join that structure. Explicit different structures refuse.
    [[nodiscard]] std::optional<PreparedChange> prepareBlueprint(CommandStamp,std::span<const PlacePart>,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareRemove(CommandStamp,uint64_t part,const CandidateValidator&,std::string&) const;
    // Whole-layout undo uses one compensating refund, never an inventory rewind.
    [[nodiscard]] std::optional<PreparedChange> prepareRemoveStructure(CommandStamp,uint64_t structure,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareTransfer(CommandStamp,TransferItems,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareCraftHammer(CommandStamp,uint64_t bench,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareUseBed(CommandStamp,uint64_t bed,PlayerPose recovery,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareGather(CommandStamp,uint32_t node,const CandidateValidator&,std::string&) const;
    [[nodiscard]] std::optional<PreparedChange> prepareEquipTool(CommandStamp,uint8_t backpackSlot,std::string&) const;
    // Geometry/renderer must be ready before commit. Commit performs no work that
    // can fail after its validation; old state is untouched on stale/refused input.
    [[nodiscard]] bool commit(PreparedChange&&,std::string&);
    // Trusted accepted locomotion/checkpoint boundary. Bounded, finite pose;
    // advances revision so a pending edit cannot overwrite newer movement.
    [[nodiscard]] bool updatePlayer(PlayerPose,uint16_t health,std::string&);
    [[nodiscard]] static bool validPose(PlayerPose) noexcept;
    [[nodiscard]] static const WorldPart* findPart(const AdventureState&,uint64_t) noexcept;
    [[nodiscard]] static const StructureComponent* findComponent(const AdventureState&,uint64_t) noexcept;
private:
    AdventureSession()=default;
    [[nodiscard]] std::optional<PreparedChange> begin(CommandStamp,std::string&) const;
    [[nodiscard]] bool finish(PreparedChange&,const CandidateValidator&,std::string&) const;
    [[nodiscard]] bool addPart(PreparedChange&,PlacePart,std::string&) const;
    AdventureState state_;
    AdventureContent content_;
};
} // namespace voxy::game::adventure

#pragma once

#include "game/expedition/cove_save.hpp"
#include "game/expedition/cove_workshop.hpp"

namespace voxy::game::expedition {

// RAM-only practice boundary. This is not a second GameSession, a save slot,
// or an authority grant. The caller retains the original editor/physical CPU
// owners and freezes the existing session until both replacement and return
// have crossed their joined physics barriers. No adapter or storage host is
// called here; temporary geometry uses the inspection compiler's identities.
class CoveTestSession {
public:
    enum class Phase { Prepared, Entering, Running, Returning, Complete };
    [[nodiscard]] static std::unique_ptr<CoveTestSession> prepare(
        std::span<const std::byte> rollback, const CoveSaveContext&,
        const construction::PartCatalog&, const CoveWorkshop&, std::string& error);

    [[nodiscard]] const CovePhysicalSave& physical() const noexcept { return archive_->physical; }
    [[nodiscard]] const assets::LoadedAssetFixture& draft() const noexcept { return draft_; }
    [[nodiscard]] std::unique_ptr<CoveBoatAssembly> takeDraftBoat() noexcept { return std::move(draftBoat_); }
    [[nodiscard]] std::span<const std::byte> rollbackBytes() const noexcept { return rollback_; }
    [[nodiscard]] std::span<const std::byte> blueprint() const noexcept { return blueprint_; }
    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] uint64_t executionTick() const noexcept { return executionTick_; }

    // Exact canonical proof includes balances, owned IDs, history, receipts,
    // allocator watermarks and logical tick. Physical practice must not even
    // advance that tick; normal confirmation resumes after successful Return.
    [[nodiscard]] bool canonicalUnchanged(const GameSession&, std::string& error) const;
    [[nodiscard]] bool stage(uint64_t incarnation, uint64_t executionTick, bool returning) noexcept;
    [[nodiscard]] bool confirm(uint64_t incarnation, uint64_t joinedTick,
        const GameSession&, std::string& error);
private:
    std::unique_ptr<CoveSaveArchive> archive_;
    CoveSaveContext context_;
    const construction::PartCatalog* catalog_ = nullptr;
    assets::LoadedAssetFixture draft_;
    std::unique_ptr<CoveBoatAssembly> draftBoat_;
    std::vector<std::byte> rollback_, canonical_, blueprint_;
    Phase phase_ = Phase::Prepared;
    uint64_t incarnation_ = 0, executionTick_ = 0, enteredTick_ = 0;
};

} // namespace voxy::game::expedition

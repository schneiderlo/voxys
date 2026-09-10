#pragma once
#include "game/expedition/session_recovery.hpp"

namespace voxy::game::expedition {
inline constexpr uint32_t kSessionSaveSchema=3; // Cut records use v3; legacy states retain exact v1/v2 bytes.
inline constexpr size_t kMaximumSessionSaveBytes=4*1024*1024;
inline constexpr size_t kMaximumJournalSaveBytes=2*1024*1024;
enum class SaveCodecError:uint8_t { None,Capacity,InvalidEncoding,UnsupportedSchema,Checksum,NonCanonical,InvalidState,InvalidJournal };
struct SaveCodecIssue {
    SaveCodecError error=SaveCodecError::None;
    size_t offset=0;
    RecoveryIssue recovery{};
    [[nodiscard]] explicit operator bool() const noexcept {return error!=SaveCodecError::None;}
};

// Trusted storage boundary, never a player intent. Checked canonical LE bytes,
// not native struct memory. SHA-256 detects damage; it is not an authority token.
// Expected world/content/writer identities come from the storage host. Decoding
// does not create a live session, issue IDs, call an adapter or acknowledge disk.
class SessionSaveCodec {
public:
    [[nodiscard]] static bool encodeCheckpoint(const ValidatedRecoveryCheckpoint&,std::vector<std::byte>&,SaveCodecIssue&);
    [[nodiscard]] static std::unique_ptr<ValidatedRecoveryCheckpoint> decodeCheckpoint(
        std::span<const std::byte>,ExpectedRecoveryIdentity,const PartCatalog&,SaveCodecIssue&);
    // Immutable, independently checksummed batches; the storage backend must
    // preserve older acknowledged batches while writing a new one. No partial
    // batch, overlap, hole or unknown tag is silently accepted as a prefix.
    [[nodiscard]] static bool encodeJournal(JournalIdentity,std::span<const JournalRecord>,std::vector<std::byte>&,SaveCodecIssue&);
    [[nodiscard]] static bool decodeJournal(std::span<const std::byte>,JournalIdentity,std::vector<JournalRecord>&,SaveCodecIssue&);
};
} // namespace voxy::game::expedition

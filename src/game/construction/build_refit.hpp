#pragma once
#include "game/construction/build_model.hpp"
#include <functional>
#include <memory>

namespace voxy::game::construction {

// A source ID retains an already owned part. Zero requests a new paid part;
// only authority supplies its physical ID, condition and provenance.
struct RefitPart {
    DurableId source{};
    DesignPart design{};
    [[nodiscard]] bool operator==(const RefitPart&) const = default;
};
struct RefitWeld {
    DesignEndpoint a{},b{};
    [[nodiscard]] auto operator<=>(const RefitWeld&) const = default;
};
inline constexpr size_t kMaximumRefitRequestBytes=16*1024;
inline constexpr size_t kMaximumRefitDeltaBytes=24*1024;

// Immutable owned request. Factories bound counts/bytes before copying. The
// journal can retain this object without allocating at canonical publication.
// Value equality, not pointer equality, identifies a repeated request.
class BuildRefitRequest {
public:
    BuildRefitRequest(const BuildRefitRequest&) = delete;
    BuildRefitRequest& operator=(const BuildRefitRequest&) = delete;
    [[nodiscard]] static std::shared_ptr<const BuildRefitRequest> create(
        std::span<const RefitPart>,std::span<const RefitWeld>,BuildIssue&);
    [[nodiscard]] std::span<const RefitPart> parts() const noexcept {return parts_;}
    [[nodiscard]] std::span<const RefitWeld> welds() const noexcept {return welds_;}
    [[nodiscard]] size_t retainedBytes() const noexcept;
    [[nodiscard]] bool operator==(const BuildRefitRequest&) const = default;
private:
    BuildRefitRequest(std::vector<RefitPart> parts,std::vector<RefitWeld> welds)
        :parts_(std::move(parts)),welds_(std::move(welds)){}
    std::vector<RefitPart> parts_;
    std::vector<RefitWeld> welds_;
};
struct RefitPartChange {
    std::optional<PartInstance> before{},after{};
    [[nodiscard]] bool operator==(const RefitPartChange&) const = default;
};
struct RefitWeldChange {
    std::optional<Connection> before{},after{};
    [[nodiscard]] bool operator==(const RefitWeldChange&) const noexcept;
};
class BuildRefitDelta {
public:
    BuildRefitDelta(const BuildRefitDelta&) = delete;
    BuildRefitDelta& operator=(const BuildRefitDelta&) = delete;
    [[nodiscard]] DurableId build() const noexcept { return build_; }
    [[nodiscard]] static std::shared_ptr<const BuildRefitDelta> between(
        const BuildSnapshot&,const BuildSnapshot&,BuildIssue&);
    [[nodiscard]] std::span<const RefitPartChange> parts() const noexcept {return parts_;}
    [[nodiscard]] std::span<const RefitWeldChange> welds() const noexcept {return welds_;}
    [[nodiscard]] size_t retainedBytes() const noexcept;
    [[nodiscard]] bool operator==(const BuildRefitDelta&) const noexcept;
    // Exact predecessor check and full BuildModel validation. Header, owner and
    // revision are supplied by the authority; applying a delta never advances it.
    [[nodiscard]] BuildIssue apply(BuildSnapshot&,const PartCatalog&,bool forward=true) const;
private:
    BuildRefitDelta(DurableId build,std::vector<RefitPartChange> parts,std::vector<RefitWeldChange> welds)
        :build_(build),parts_(std::move(parts)),welds_(std::move(welds)){}
    DurableId build_;
    std::vector<RefitPartChange> parts_;
    std::vector<RefitWeldChange> welds_;
};
struct BuildRefitPlan {
    BuildSnapshot after;
    std::shared_ptr<const BuildRefitDelta> delta;
    ResourceAmounts debit{},credit{}; // Gross trusted costs/yields; authority nets them atomically.
    std::vector<DurableId> createdIds;
    std::vector<PartInstance> storedPartsAfter;
    std::vector<DurableId> consumedStoredParts;
};
inline constexpr size_t kMaximumStoredParts=64;
// Trusted authority inputs, never fields in a player-supplied design. Stored
// parts remain exact owned instances; source IDs can withdraw only from this
// build's stock. A starter replacement must use a registered zero-source kit,
// retain its entitlement and store paid parts instead of paying salvage refunds.
struct RefitOwnership {
    std::span<const PartInstance> storedParts{};
    std::optional<DurableId> starterEntitlement{};
};
// Pure preparation, never publication. The exclusive authority owns allocateId
// and must retain/burn every supplied ID even if later validation fails.
// Only welded starter topology is supported here; rope/hinge/latch activation
// belongs to the corresponding mechanics transactions, never an invented weld.
[[nodiscard]] std::optional<BuildRefitPlan> prepareBuildRefit(
    const BuildModel&,const BuildRefitRequest&,const PartCatalog&,
    const std::function<std::optional<DurableId>()>& allocateId,BuildIssue&,
    RefitOwnership ownership={});
[[nodiscard]] bool sameConnection(const Connection&,const Connection&) noexcept;

} // namespace voxy::game::construction

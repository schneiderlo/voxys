#pragma once
#include "game/expedition/cove_harbor_lift.hpp"
#include "physics/physics_world.hpp"

namespace voxy::game::expedition {
// Application-thread owner. GameSession/storage/input authorization stays at
// the call site; no method can manufacture a banked job or storage receipt.
class CoveHarborRuntime {
public:
    enum class Stage { Absent,Requested,Uploading,Admitting,Ready,Closing,Drained,Failed };
    enum class Action { Attach,Raise,Lower,Stop,Release };
    [[nodiscard]] static std::unique_ptr<CoveHarborRuntime> create(
        const assets::CoveNavigation&,glm::dvec3 origin,const CoveBoatAssembly&,
        CoveHarborLiftState saved,std::string& error);
    [[nodiscard]] Stage stage() const noexcept {return stage_;}
    [[nodiscard]] const CoveHarborLift& structure() const noexcept {return *structure_;}
    [[nodiscard]] const CoveHarborLift* rig() const noexcept {return rig_.get();}
    [[nodiscard]] physics::BodyHandle body() const noexcept {return body_;}
    [[nodiscard]] physics::ShapeHandle shape() const noexcept {return shape_;}
    [[nodiscard]] const physics::AuthoredRootMotion& motion() const noexcept {return motion_;}
    [[nodiscard]] const auto& ropes() const noexcept {return ropes_;}
    [[nodiscard]] const auto& observedRopes() const noexcept {return observed_;}
    [[nodiscard]] bool installed() const noexcept {return saved_.profile!=0;}
    [[nodiscard]] bool durable() const noexcept {return durable_;}
    [[nodiscard]] bool installationPending() const noexcept {return installing_;}
    [[nodiscard]] bool hasRopes() const noexcept;
    [[nodiscard]] bool attachmentPending() const noexcept {return attachRequested_;}
    [[nodiscard]] bool pollAttachment(physics::PhysicsWorld&,physics::BodyHandle,
        const physics::AuthoredRootMotion&,uint64_t observedPoseTick);
    [[nodiscard]] bool busy() const noexcept {return changing_||stage_==Stage::Requested||stage_==Stage::Uploading||stage_==Stage::Admitting;}
    [[nodiscard]] uint64_t observedTick() const noexcept {return observedTick_;}
    [[nodiscard]] std::string_view message() const noexcept {return message_;}
    [[nodiscard]] std::string_view rigIssue() const noexcept {return rigIssue_;}
    [[nodiscard]] float motor() const noexcept {return motor_;}
    [[nodiscard]] CoveHarborLiftState state() const noexcept {return saved_;}
    [[nodiscard]] bool requestInstall();
    [[nodiscard]] std::optional<physics::AuthoredRootMotion> prepareCargoParking(
        const CoveBoatAssembly& cargo,const CoveBoatAssembly& boat,const physics::AuthoredRootMotion& boatPose,
        const CoveSceneryCollision&,glm::dvec3 origin,glm::dvec3 playerFeet,const CovePlayer::Ground& ground);
    void cancelInstallation(std::string_view reason);
    // Called only at a fully joined Pause for new installation. Existing fixed
    // scenery is permitted to meet posts, but boat/cargo/player occupancy is not.
    [[nodiscard]] bool checkInstallationSpace(const CoveBoatAssembly&,const physics::AuthoredRootMotion&,
        const CoveBoatAssembly&,const physics::AuthoredRootMotion&,glm::dvec3 playerFeet);
    // Upload/admit on the owning world. Returns false on a terminal error.
    // A new body/constraint command requires a joined neutral tick from the app.
    [[nodiscard]] bool update(physics::PhysicsWorld&,physics::BodyHandle boat,bool& needsTick);
    [[nodiscard]] bool observe(const physics::DebugSnapshot&);
    [[nodiscard]] bool observe(const physics::PhysicsEventBatch&);
    [[nodiscard]] bool action(Action,physics::PhysicsWorld&,physics::BodyHandle,
        const physics::AuthoredRootMotion& boat);
    [[nodiscard]] bool stop(physics::PhysicsWorld&);
    [[nodiscard]] bool release(physics::PhysicsWorld&);
    [[nodiscard]] bool close(physics::PhysicsWorld&);
    [[nodiscard]] bool joined(uint64_t tick) const noexcept;
    [[nodiscard]] std::optional<CoveHarborLiftState> capture(uint64_t tick) const noexcept;
    [[nodiscard]] bool acknowledgeInstallation();
    [[nodiscard]] bool configureBoat(const CoveBoatAssembly&);
    void includeAttachmentRange(uint32_t& first,uint32_t& last) const noexcept;
private:
    [[nodiscard]] bool fail(std::string_view);
    std::unique_ptr<CoveHarborLift> structure_,rig_;
    assets::CoveNavigation navigation_;
    CoveHarborLiftState saved_{};
    Stage stage_=Stage::Absent;
    physics::ShapeHandle shape_{};
    physics::BodyHandle body_{},boat_{};
    physics::AuthoredRootMotion motion_{};
    std::array<physics::AttachmentHandle,kCoveHarborLiftLines> ropes_{};
    std::array<physics::DebugAttachmentState,kCoveHarborLiftLines> observed_{};
    std::array<uint64_t,kCoveHarborLiftLines> ropeTicks_{};
    uint64_t observedTick_=0,changedTick_=0,bodyChangedTick_=0;
    uint64_t attachRequestTick_=0,attachAttemptTick_=0;
    bool attachRequested_=false;
    bool installing_=false,durable_=false,changing_=false,releasing_=false,retired_=false,restoreRopes_=false;
    float motor_=0;
    std::string message_,rigIssue_;
    core::Sha256Digest rigIdentity_{};
};
} // namespace voxy::game::expedition

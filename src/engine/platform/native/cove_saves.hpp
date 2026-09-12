#pragma once
#include "engine/platform/native/save_worker.hpp"
#include <chrono>
#include <string>

namespace voxy {
class Application;
// Native composition root. All game calls stay on the application thread;
// only owned archives and storage results cross the disk-worker boundary.
class NativeCoveSaves {
public:
    struct Options {std::optional<std::string> root,world,observation;};
    static std::optional<Options> parse(int argc,char** argv,std::string& error);
    static std::unique_ptr<NativeCoveSaves> create(const Options&,std::string& error);
    ~NativeCoveSaves();
    // Before app.init. May wait for initial disk read; never in a live frame.
    bool prepare(Application&,std::string& error);
    bool initialized(Application&,std::string& error);
    void update(Application&);
    void close(Application&);
    // A menu-selected slot is read on its own worker before the old game is
    // retired. Ownership of its lock and staged archive moves together.
    std::pair<std::unique_ptr<Application>,std::unique_ptr<NativeCoveSaves>> takeTransition(Application&);
private:
    enum class Pending {None,OpenForSave,PublishSave,PublishRecovery};
    explicit NativeCoveSaves(std::filesystem::path root,std::optional<std::string> world);
    void message(Application&,std::string);
    void failure(Application&,platform::StoreIssue,bool loading=false);
    // Explicit diagnostic mode only: latest bounded state, no game mutations,
    // images, additional GPU readbacks or default-frame I/O.
    void observe(const Application&);
    void refreshWorlds(Application&);
    void updateTransition(Application&);
    std::filesystem::path root_;
    std::optional<std::filesystem::path> observation_;
    std::chrono::steady_clock::time_point observedAt_{};
    std::optional<std::string> selected_;
    std::string world_,recoveryDigest_,message_,capturedState_,capturedDigest_;
    game::construction::WorldNamespace identity_{};
    uint64_t generation_=0;
    bool opened_=false,closed_=false,waitingPhysical_=false,revoked_=false;
    bool capturedCheckpoint_=false,capturedHarbor_=false,capturedRescue_=false,capturedWorkshop_=false,checkpointAttempted_=false;
    Pending pending_=Pending::None;
    std::vector<std::byte> captured_;
    platform::NativeSaveWorker worker_;
    std::unique_ptr<Application> nextApplication_;
    std::unique_ptr<NativeCoveSaves> nextSaves_;
    bool nextLoading_=false,transitionPrepared_=false;
};
} // namespace voxy

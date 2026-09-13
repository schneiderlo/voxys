#pragma once
#include "engine/platform/native/save_worker.hpp"
#include "game/adventure/adventure_save.hpp"

namespace voxy::platform {
// Separate adventure-v1 directory, same proven dual-copy transport/worker.
// Opening may wait before gameplay starts. Publishing only enqueues immutable
// bytes; poll never writes save data on the application thread.
class NativeAdventureSaves {
public:
    struct Completion {
        bool saved=false,recoveryRequired=false;
        uint64_t generation=0;
        std::string message;
    };
    [[nodiscard]] static std::unique_ptr<NativeAdventureSaves> open(
        const std::filesystem::path& root,std::optional<std::string> selectedWorld,
        bool fresh,const game::adventure::AdventureContent&,std::string& error,SaveIoObserver* observer=nullptr);
    ~NativeAdventureSaves();
    [[nodiscard]] const std::string& world()const noexcept{return world_;}
    [[nodiscard]] game::construction::WorldNamespace worldIdentity()const noexcept{return identity_;}
    [[nodiscard]] const std::vector<std::byte>& loadedBytes()const noexcept{return loaded_;}
    [[nodiscard]] uint64_t generation()const noexcept{return generation_;}
    [[nodiscard]] bool busy()const noexcept{return busy_;}
    [[nodiscard]] bool requestSave(std::vector<std::byte>,std::string& error);
    [[nodiscard]] std::optional<Completion> poll();
private:
    NativeAdventureSaves()=default;
    std::filesystem::path directory_;
    std::string world_;
    game::construction::WorldNamespace identity_{};
    game::adventure::AdventureContent content_;
    // Optional test fault observer must outlive this owner, as with NativeSaveWorker.
    SaveIoObserver* observer_=nullptr;
    std::unique_ptr<NativeSaveWorker> worker_;
    std::vector<std::byte> loaded_,pending_;
    uint64_t generation_=0;
    bool busy_=false,reconciling_=false,unavailable_=false;
};
} // namespace voxy::platform

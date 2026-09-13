#include "engine/platform/native/native_adventure_saves.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace voxy::platform {
namespace {
using game::construction::WorldNamespace;
using game::expedition::StoreError;
constexpr size_t maximumWorlds=32;
bool parseWorld(std::string_view text,WorldNamespace& world) {
    if(text.size()!=32)return false;
    auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<16;++i) {
        const auto a=digit(text[i*2]),b=digit(text[i*2+1]);if(a<0||b<0)return false;
        world.bytes[i]=static_cast<uint8_t>(a*16+b);
    }
    return game::construction::isValid(world);
}
std::string randomWorld() {
    std::random_device random;
    constexpr char hex[]="0123456789abcdef";
    std::string result(32,'0');for(auto& c:result)c=hex[random()&15];
    if(std::all_of(result.begin(),result.end(),[](char c){return c=='0';}))result.back()='1';
    return result;
}
std::string issueText(StoreIssue issue) {
    switch(issue.error) {
        case StoreError::Busy:return "This adventure is open in another game window.";
        case StoreError::NoSpace:return "Storage is full. Free space, then save again.";
        case StoreError::Permission:return "Adventure storage is unavailable. Check folder permissions.";
        case StoreError::UnsupportedPlatform:return "Adventure saving is not supported on this platform yet.";
        case StoreError::Conflict:return "Adventure save ownership changed. Reopen the saved world.";
        default:return "Adventure save failed. Your current progress is still in memory.";
    }
}
}
NativeAdventureSaves::~NativeAdventureSaves()=default;
std::unique_ptr<NativeAdventureSaves> NativeAdventureSaves::open(const std::filesystem::path& root,
    std::optional<std::string> selectedWorld,bool fresh,const game::adventure::AdventureContent& content,std::string& error,SaveIoObserver* observer) {
    try {
        if(!root.is_absolute() || (fresh&&selectedWorld)){error="Choose an absolute save folder and either a new or saved adventure.";return nullptr;}
        const auto profile=root/"adventure-v1";
        std::error_code ec;std::filesystem::create_directories(profile,ec);
        if(ec){error="The adventure save folder could not be opened.";return nullptr;}
        std::vector<std::pair<std::filesystem::file_time_type,std::string>> candidates;
        size_t worlds=0,entries=0;
        for(const auto& entry:std::filesystem::directory_iterator(profile)) {
            if(++entries>256){error="The adventure folder has too many entries.";return nullptr;}
            WorldNamespace candidate{};const auto name=entry.path().filename().string();
            if(!parseWorld(name,candidate))continue;
            if(!std::filesystem::is_directory(entry.symlink_status()))continue;
            // Either durable replica establishes a saved world. Empty directories
            // left by unsaved starts do not exhaust the 32-save library.
            std::optional<std::filesystem::file_time_type> newest;
            for(const auto* replica:{"current","mirror"}) {
                const auto path=entry.path()/replica;
                const auto status=std::filesystem::symlink_status(path,ec);
                if(!ec && std::filesystem::exists(status)) {
                    if(!std::filesystem::is_regular_file(status)){error="An adventure save copy has an invalid file type.";return nullptr;}
                    const auto stamp=std::filesystem::last_write_time(path,ec);
                    if(ec){error="An adventure save copy could not be inspected.";return nullptr;}
                    if(!newest || stamp>*newest)newest=stamp;
                } else if(ec && ec!=std::errc::no_such_file_or_directory) {
                    error="An adventure save copy could not be inspected.";return nullptr;
                }
                ec.clear();
            }
            if(newest) {
                if(++worlds>maximumWorlds){error="Adventure world limit: 32 saved worlds.";return nullptr;}
                candidates.emplace_back(*newest,name);
            }
        }
        bool selectedExisting=selectedWorld.has_value();
        if(!fresh&&!selectedWorld&&!candidates.empty()) {
            std::sort(candidates.begin(),candidates.end());selectedWorld=candidates.back().second;selectedExisting=true;
        }
        if(!selectedWorld) {
            if(worlds>=maximumWorlds){error="Adventure world limit: 32 saved worlds.";return nullptr;}
            for(int attempt=0;attempt<8;++attempt) {
                auto candidate=randomWorld();
                if(!std::filesystem::exists(profile/candidate)){selectedWorld=candidate;break;}
            }
            if(!selectedWorld){error="A new adventure identity could not be created.";return nullptr;}
        }
        auto result=std::unique_ptr<NativeAdventureSaves>(new NativeAdventureSaves);
        if(!parseWorld(*selectedWorld,result->identity_)){error="The adventure world identity is invalid.";return nullptr;}
        result->world_=*selectedWorld;result->directory_=profile/result->world_;result->content_=content;result->observer_=observer;
        result->worker_=std::make_unique<NativeSaveWorker>(observer);
        if(!result->worker_->open(result->directory_,result->identity_)){error="Adventure storage is busy.";return nullptr;}
        std::optional<NativeSaveWorker::Result> opened;
        while(!(opened=result->worker_->poll()))std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(opened->issue){error=issueText(opened->issue);return nullptr;}
        result->generation_=opened->stored.generation;result->loaded_=std::move(opened->stored.payload);
        if(selectedExisting&&!result->generation_){error="The selected adventure has no confirmed save.";return nullptr;}
        if(!result->loaded_.empty()) {
            game::adventure::AdventureState state;
            if(!game::adventure::AdventureSaveCodec::decode(result->loaded_,result->identity_,content,state,error))return nullptr;
        }
        error.clear();return result;
    }catch(const std::exception&){error="The adventure save folder could not be read.";return nullptr;}
}
bool NativeAdventureSaves::requestSave(std::vector<std::byte> bytes,std::string& error) {
    if(busy_){error="An adventure save is already running.";return false;}
    if(unavailable_){error="Reopen this adventure before saving again.";return false;}
    game::adventure::AdventureState state;
    if(!game::adventure::AdventureSaveCodec::decode(bytes,identity_,content_,state,error))return false;
    pending_=bytes;
    if(!worker_->publish(generation_,std::move(bytes))){pending_.clear();error="Adventure storage cannot accept this save yet.";return false;}
    busy_=true;error.clear();return true;
}
std::optional<NativeAdventureSaves::Completion> NativeAdventureSaves::poll() {
    if(!busy_)return std::nullopt;
    auto result=worker_->poll();if(!result)return std::nullopt;
    if(reconciling_) {
        reconciling_=false;busy_=false;
        if(result->issue){unavailable_=true;return Completion{false,true,generation_,issueText(result->issue)};}
        game::adventure::AdventureState state;std::string error;
        if(result->stored.generation && !game::adventure::AdventureSaveCodec::decode(result->stored.payload,identity_,content_,state,error)) {
            unavailable_=true;return Completion{false,true,generation_,error};
        }
        generation_=result->stored.generation;
        // Reopening flushes the directory, but a recovered new primary does
        // not fulfill the paired-copy contract while its mirror is still old.
        const bool confirmed=!pending_.empty()&&result->stored.payload==pending_&&!result->stored.needsRepair;
        loaded_=std::move(result->stored.payload);pending_.clear();
        return Completion{confirmed,false,generation_,confirmed?"Adventure saved.":"Save not confirmed. Try Save again."};
    }
    if(result->issue) {
        // Reconcile on a newly owned worker. A competitor taking the lock wins
        // visibly; this helper never overwrites their generation to recover.
        worker_->shutdown();worker_=std::make_unique<NativeSaveWorker>(observer_);
        reconciling_=true;
        if(!worker_->open(directory_,identity_)){busy_=false;unavailable_=true;return Completion{false,true,generation_,"Adventure save recovery could not start."};}
        return std::nullopt;
    }
    busy_=false;generation_=result->stored.generation;loaded_=std::move(pending_);
    return Completion{true,false,generation_,"Adventure saved."};
}
} // namespace voxy::platform

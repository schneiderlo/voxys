#include "engine/platform/native/cove_saves.hpp"
#include "app/application.hpp"
#if defined(None)
#undef None
#endif
#include "core/log.hpp"
#include "core/sha256.hpp"
#include "game/expedition/cove_save.hpp"
#include <json.hpp>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <utility>

namespace voxy {
namespace {
constexpr std::string_view digits="0123456789abcdef";
std::vector<std::byte> fromHex(std::string_view text,size_t maximum){
    if(text.empty() || text.size()%2 || text.size()/2>maximum)return {};
    std::vector<std::byte> bytes(text.size()/2);
    for(size_t i=0;i<bytes.size();++i){
        const auto a=digits.find(text[2*i]),b=digits.find(text[2*i+1]);
        if(a==digits.npos || b==digits.npos)return {};
        bytes[i]=static_cast<std::byte>(a*16+b);
    }
    return bytes;
}
bool worldIdentity(std::string_view text,game::construction::WorldNamespace& output){
    if(text.size()!=32)return false;
    const auto bytes=fromHex(text,16);if(bytes.size()!=16)return false;
    for(size_t i=0;i<16;++i)output.bytes[i]=std::to_integer<uint8_t>(bytes[i]);
    return game::construction::isValid(output);
}
const char* explanation(game::expedition::StoreError value){
    using E=game::expedition::StoreError;
    switch(value){
    case E::NoSpace:return "Storage is full.";
    case E::Permission:return "Save folder is not writable.";
    case E::Busy:return "This expedition is open in another process.";
    case E::InvalidPath:return "Save folder is invalid.";
    case E::InvalidData:return "Saved expedition is damaged or missing.";
    case E::UnsupportedSchema:return "This save needs a different game version.";
    case E::Capacity:return "Save capacity was reached.";
    case E::Conflict:return "The stored expedition changed. Restart to load it.";
    case E::RecoveryRequired:return "Save was interrupted. Restart to recover it.";
    case E::UnsupportedPlatform:return "Saving is unavailable on this platform.";
    default:return "Storage could not complete the save.";
    }
}
}
NativeCoveSaves::NativeCoveSaves(std::filesystem::path root,std::optional<std::string> world)
    :root_(std::move(root)),selected_(std::move(world)){}
NativeCoveSaves::~NativeCoveSaves()=default;
std::optional<NativeCoveSaves::Options> NativeCoveSaves::parse(int argc,char** argv,std::string& error){
    Options result;error.clear();
    for(int i=1;i<argc;++i){
        const std::string_view arg=argv[i]?argv[i]:"";
        if(arg!="--expedition-root" && arg!="--expedition-world" && arg!="--expedition-observe")continue;
        auto& target=arg=="--expedition-root"?result.root:arg=="--expedition-world"?result.world:result.observation;
        if(target || i+1>=argc || !argv[i+1] || !*argv[i+1]){error="Supply each expedition option once, with a value.";return {};}
        target=argv[++i];
    }
    game::construction::WorldNamespace world;
    if(result.world && !worldIdentity(*result.world,world)){error="Expedition world must be 32 lowercase hex digits and nonzero.";return {};}
    if(result.root){
        const std::filesystem::path root=*result.root;
        if(!root.is_absolute() || root==root.root_path()){error="Expedition root must be an absolute save folder.";return {};}
        for(const auto& part:root)if(part==".."){error="Expedition root cannot contain parent traversal.";return {};}
    }
    if(result.observation){
        const std::filesystem::path path=*result.observation;
        if(!path.is_absolute() || path==path.root_path()){error="Observation needs an absolute new directory.";return {};}
        for(const auto& part:path)if(part==".."){error="Observation path cannot contain parent traversal.";return {};}
    }
    return result;
}
std::unique_ptr<NativeCoveSaves> NativeCoveSaves::create(const Options& options,std::string& error){
    try {
        platform::StoreIssue issue;
        auto root=options.root?std::filesystem::path(*options.root):platform::NativeSaveStore::defaultRoot(issue);
        if(issue){error=explanation(issue.error);return {};}
        auto result=std::unique_ptr<NativeCoveSaves>(new NativeCoveSaves(std::move(root),options.world));
        if(options.observation){
            const std::filesystem::path path=*options.observation;
            if(!std::filesystem::create_directory(path)){error="Observation directory must not exist.";return {};}
            result->observation_=path;
        }
        error.clear();return result;
    }catch(const std::exception& failure){error=failure.what();return {};}
}
bool NativeCoveSaves::prepare(Application& app,std::string& error){
    if(opened_)return true; // Already staged by the in-game slot picker.
    if(!selected_)return true;
    world_=*selected_;if(!worldIdentity(world_,identity_)){error="Invalid selected expedition.";return false;}
    if(!worker_.open(root_/world_,identity_)){error="Could not start reading the expedition.";return false;}
    // Only startup waits. Application frame processing never waits for disk.
    std::optional<platform::NativeSaveWorker::Result> result;
    while(!(result=worker_.poll()))std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(result->issue){error=explanation(result->issue.error);return false;}
    if(result->stored.generation==0){error="The selected expedition is missing. No new world was created.";return false;}
    opened_=true;generation_=result->stored.generation;
    if(!app.stageCoveResume(identity_.bytes,result->stored.payload)){error="The selected expedition archive is damaged or incompatible.";return false;}
    return true;
}
bool NativeCoveSaves::initialized(Application& app,std::string& error){
    try {
        const auto state=nlohmann::json::parse(app.salvagePreviewJson());
        if(!state.contains("world")){error="Expedition saving requires the playable cove.";return false;}
        const auto actual=state.at("world").get<std::string>();
        if(selected_ && actual!=world_){error="Loaded world identity does not match the selected slot.";return false;}
        world_=actual;if(!worldIdentity(world_,identity_)){error="The cove world identity is invalid.";return false;}
        refreshWorlds(app);
        if(app.salvageExpeditionAction(6,"")!="ok"){error="The cove could not register its save host.";return false;}
        LOG_INFO("Expedition save folder: {}",(root_/world_).string());
        LOG_INFO("To resume: --config salvage_cove.cfg --expedition-root '{}' --expedition-world {}",root_.string(),world_);
        if(selected_){
            if(state.at("restore").at("phase")!="awaiting-storage"){error="Saved world did not wait for disk ownership.";return false;}
            const auto hex=app.salvageExpeditionAction(3,"");
            auto bytes=fromHex(hex,game::expedition::kMaximumCoveSaveBytes);
            if(bytes.empty() || app.salvageExpeditionAction(2,hex)!="ok"){error="Recovered expedition could not be validated.";return false;}
            recoveryDigest_=core::sha256Hex(core::sha256(bytes));
            if(!worker_.publish(generation_,std::move(bytes))){error="Could not publish recovered expedition ownership.";return false;}
            pending_=Pending::PublishRecovery;message(app,"Loading expedition...");
        }else message(app,"P: Pause | F10: Save expedition (manual)");
        return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
void NativeCoveSaves::message(Application& app,std::string text){
    message_=std::move(text);app.setSalvageSaveStatus(message_);LOG_INFO("{}",message_);
}
void NativeCoveSaves::failure(Application& app,platform::StoreIssue issue,bool loading){
    captured_.clear();pending_=Pending::None;
    if(loading || issue.publicationMayHaveHappened || issue.error==game::expedition::StoreError::RecoveryRequired
        || issue.error==game::expedition::StoreError::Conflict){
        revoked_=true;(void)app.salvageExpeditionAction(5,"");
        message(app,std::string("Expedition paused. ")+explanation(issue.error)+" Restart to recover.");
    }else message(app,std::string("Save failed. ")+explanation(issue.error)+" Pause, then F10 to retry.");
    LOG_ERROR("Expedition storage error {} (system {}, publication uncertain {})",static_cast<int>(issue.error),issue.systemError,issue.publicationMayHaveHappened);
}
void NativeCoveSaves::update(Application& app){
    observe(app);
    if(closed_ || revoked_)return;
    app.setCoveHostBusy(pending_!=Pending::None||waitingPhysical_||nextLoading_);
    updateTransition(app);
    try {
        if(auto result=worker_.poll()){
            const auto operation=std::exchange(pending_,Pending::None);
            if(result->issue){failure(app,result->issue,operation==Pending::PublishRecovery);return;}
            if(operation==Pending::OpenForSave){
                opened_=true;
                if(result->stored.generation!=0){failure(app,{game::expedition::StoreError::Conflict});return;}
                if(!worker_.publish(0,std::move(captured_))){failure(app,{game::expedition::StoreError::Capacity});return;}
                pending_=Pending::PublishSave;
            }else if(operation==Pending::PublishSave || operation==Pending::PublishRecovery){
                generation_=result->stored.generation;
                if(operation==Pending::PublishRecovery){
                    if(app.salvageExpeditionAction(4,recoveryDigest_)!="ok"){failure(app,{game::expedition::StoreError::InvalidData},true);return;}
                    waitingPhysical_=true;message(app,"Restoring saved boat...");
                }else {
                    if(capturedCheckpoint_ && app.salvageExpeditionAction(7,capturedDigest_)!="ok") {
                        failure(app,{game::expedition::StoreError::InvalidData},true);return;
                    }
                    message(app,capturedWorkshop_?"Boat and owned parts saved | P: Resume":capturedRescue_?"Boat recovered and saved | P: Resume":capturedHarbor_?"Harbor lift powered and saved | P: Resume":capturedCheckpoint_?"Delivery saved. +60 material | P: Resume":"Expedition saved. F10: Save again | P: Resume");
                    LOG_INFO("Expedition checkpoint state: {}",capturedState_);capturedState_.clear();
                    app.noteCoveSaveCompleted();refreshWorlds(app);
                    if(capturedCheckpoint_)LOG_INFO("Progress durable state: {}",app.salvagePreviewJson());
                }
                LOG_INFO("Expedition checkpoint committed: world {}, generation {}",world_,generation_);
            }
        }
        if(waitingPhysical_){
            const auto state=nlohmann::json::parse(app.salvagePreviewJson());
            if(state.value("failed",false)){failure(app,{game::expedition::StoreError::InvalidData},true);return;}
            if(state.value("ready",false) && state.at("restore").at("phase")=="ready"){
                waitingPhysical_=false;message(app,"Expedition loaded. P: Resume | F10: Save");
                LOG_INFO("Expedition restored state: {}",state.dump());
            }
        }
        const bool delivery=app.salvageCheckpointNeedsSave();
        if(!delivery)checkpointAttempted_=false;
        const bool automaticDelivery=delivery&&!checkpointAttempted_;
        const bool manualRequested=app.consumeCoveSaveRequest();
        if(pending_!=Pending::None || waitingPhysical_ || (!automaticDelivery
            && !manualRequested))return;
        if(delivery)checkpointAttempted_=true;
        auto payload=fromHex(app.salvageExpeditionAction(1,""),game::expedition::kMaximumCoveSaveBytes);
        if(payload.empty()){message(app,"Close the workshop and Pause before saving. F10: Save");return;}
        capturedCheckpoint_=delivery;capturedDigest_=delivery?core::sha256Hex(core::sha256(payload)):"";
        capturedState_=app.salvagePreviewJson();
        const auto harbor=nlohmann::json::parse(capturedState_).value("harbor",nlohmann::json::object());
        capturedHarbor_=delivery&&harbor.value("pending",false);
        capturedRescue_=delivery&&nlohmann::json::parse(capturedState_).value("rescue",nlohmann::json::object()).value("savePending",false);
        capturedWorkshop_=delivery&&nlohmann::json::parse(capturedState_).value("workshop",nlohmann::json::object()).value("savePending",false);
        if(!opened_){
            captured_=std::move(payload);
            if(!worker_.open(root_/world_,identity_)){failure(app,{game::expedition::StoreError::Busy});return;}
            pending_=Pending::OpenForSave;
        }else {
            if(!worker_.publish(generation_,std::move(payload))){failure(app,{game::expedition::StoreError::Capacity});return;}
            pending_=Pending::PublishSave;
        }
        message(app,capturedWorkshop_?"Saving boat and owned parts...":capturedRescue_?"Saving recovered boat...":capturedHarbor_?"Saving powered harbor lift...":delivery?"Saving delivery...":"Saving expedition...");
    }catch(const std::bad_alloc&){failure(app,{game::expedition::StoreError::Capacity},pending_!=Pending::None);}
    catch(...){failure(app,{game::expedition::StoreError::InvalidData},true);}
}
void NativeCoveSaves::refreshWorlds(Application& app) {
    // Optional menu discovery. The selected store is still strictly admitted
    // by NativeSaveWorker and stageCoveResume before the current world closes.
    std::vector<std::pair<std::string,std::string>> rows;
    try {
        std::error_code ec;
        if(std::filesystem::is_directory(root_,ec)) {
            size_t scanned=0;
            for(const auto& entry:std::filesystem::directory_iterator(root_,ec)) {
                if(++scanned>4096||rows.size()>=64)break;
                game::construction::WorldNamespace id;
                const auto name=entry.path().filename().string();
                if(entry.is_symlink(ec)||!entry.is_directory(ec)||!worldIdentity(name,id))continue;
                if(!std::filesystem::is_regular_file(entry.path()/"current",ec)
                    &&!std::filesystem::is_regular_file(entry.path()/"mirror",ec))continue;
                rows.emplace_back(name,"");
            }
        }
        std::sort(rows.begin(),rows.end());
        for(size_t i=0;i<rows.size();++i)rows[i].second="Saved Cove "+std::to_string(i+1)+(rows[i].first==world_?" (current)":"");
    }catch(...){rows.clear();}
    app.setCoveSavedWorlds(std::move(rows),world_);
}
void NativeCoveSaves::updateTransition(Application& app) {
    if(transitionPrepared_)return;
    const auto fail=[&](std::string text) {
        nextSaves_.reset();nextApplication_.reset();nextLoading_=false;
        app.cancelCoveWorldPreparation(std::move(text));
    };
    try {
        if(auto request=app.consumeCoveWorldRequest()) {
            if(pending_!=Pending::None||waitingPhysical_||nextSaves_){fail("Finish the current save before changing expeditions.");return;}
            nextApplication_=std::make_unique<Application>();
            nextSaves_=std::unique_ptr<NativeCoveSaves>(new NativeCoveSaves(root_,request->empty()?std::nullopt:std::optional(*request)));
            nextSaves_->observation_=observation_;
            if(request->empty()) {
                if(!app.completeCoveWorldPreparation()){fail("The current expedition cannot close yet.");return;}
                transitionPrepared_=true;return;
            }
            auto& next=*nextSaves_;next.world_=*request;
            if(!worldIdentity(*request,next.identity_)||!next.worker_.open(root_ / *request,next.identity_)) {
                fail("This saved Cove could not be opened. Your current game is still paused.");return;
            }
            nextLoading_=true;
        }
        if(nextLoading_)if(auto result=nextSaves_->worker_.poll()) {
            nextLoading_=false;
            if(result->issue||result->stored.generation==0) {
                fail(result->issue?explanation(result->issue.error):"This saved Cove is missing. Your current game is unchanged.");return;
            }
            nextSaves_->opened_=true;nextSaves_->generation_=result->stored.generation;
            std::string validationError;
            if(!app.preflightCoveWorld(nextSaves_->identity_.bytes,result->stored.payload,validationError)) {
                fail(validationError.empty()?"This saved Cove is incompatible. Your current game is unchanged.":std::move(validationError));return;
            }
            if(!nextApplication_->stageCoveResume(nextSaves_->identity_.bytes,result->stored.payload)) {
                fail("This saved Cove is damaged or incompatible. Your current game is unchanged.");return;
            }
            if(!app.completeCoveWorldPreparation()){fail("The current expedition cannot close yet.");return;}
            transitionPrepared_=true;
        }
    }catch(...){fail("The saved Cove could not be prepared. Your current game is unchanged.");}
}
std::pair<std::unique_ptr<Application>,std::unique_ptr<NativeCoveSaves>> NativeCoveSaves::takeTransition(Application& app) {
    if(!transitionPrepared_||!app.coveWorldTransitionDrained())return {};
    return {std::move(nextApplication_),std::move(nextSaves_)};
}
void NativeCoveSaves::observe(const Application& app){
    if(!observation_)return;
    const auto now=std::chrono::steady_clock::now();
    if(now-observedAt_<std::chrono::milliseconds(100))return;
    observedAt_=now;
    try {
        const auto state=app.salvagePreviewJson();
        if(state.size()>65536)throw std::runtime_error("Cove observation exceeds 64 KiB");
        // Atomic latest-sample replacement bounds disk use at two 64 KiB
        // files regardless of run duration. This opt-in I/O is diagnostic
        // overhead, never performance evidence or an expedition checkpoint.
        const auto next=*observation_/"next.json";
        std::ofstream file(next,std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::badbit|std::ios::failbit);file<<state;file.close();
        std::filesystem::rename(next,*observation_/"state.json");
    }catch(const std::exception& error){
        LOG_ERROR("Cove observation stopped: {}",error.what());observation_.reset();
    }
}
void NativeCoveSaves::close(Application& app){
    if(closed_)return;
    closed_=true;
    (void)app.salvageExpeditionAction(5,"");worker_.shutdown();
}
} // namespace voxy

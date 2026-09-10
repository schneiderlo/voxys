#include "engine/platform/native/save_worker.hpp"
#include <cerrno>
#include <limits>
#include <utility>

namespace voxy::platform {
using game::expedition::StoreError;
NativeSaveWorker::NativeSaveWorker(SaveIoObserver* observer):observer_(observer),thread_([this]{run();}){}
NativeSaveWorker::~NativeSaveWorker(){shutdown();}
bool NativeSaveWorker::enqueue(Work work){
    std::lock_guard lock(mutex_);
    if(stopping_ || occupied_)return false;
    pending_=std::move(work);occupied_=true;wake_.notify_one();return true;
}
bool NativeSaveWorker::open(std::filesystem::path directory,game::construction::WorldNamespace world){
    return enqueue({Operation::Open,std::move(directory),world,0,{}});
}
bool NativeSaveWorker::publish(uint64_t expected,std::vector<std::byte> payload){
    if(payload.empty() || payload.size()>game::expedition::kMaximumStoredPayloadBytes
        || expected==std::numeric_limits<uint64_t>::max())return false;
    return enqueue({Operation::Publish,{},{},expected,std::move(payload)});
}
std::optional<NativeSaveWorker::Result> NativeSaveWorker::poll(){
    std::lock_guard lock(mutex_);
    if(!completed_ || stopping_)return {};
    auto result=std::move(completed_);completed_.reset();occupied_=false;return result;
}
void NativeSaveWorker::shutdown(){
    {std::lock_guard lock(mutex_);stopping_=true;wake_.notify_one();}
    if(thread_.joinable())thread_.join();
}
void NativeSaveWorker::run(){
    std::unique_ptr<NativeSaveStore> store;
    for(;;){
        Work work;
        {
            std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stopping_ || pending_.has_value();});
            if(!pending_)break;
            work=std::move(*pending_);pending_.reset();
        }
        Result result;result.operation=work.operation;
        try {
            if(work.operation==Operation::Open){
                if(store)result.issue.error=StoreError::Busy;
                else {
                    store=NativeSaveStore::open(work.directory,work.world,result.issue,observer_);
                    if(store && !store->load(result.stored,result.issue))store.reset();
                }
            }else if(!store)result.issue.error=StoreError::InvalidPath;
            else if(store->publish(work.generation,work.payload,result.issue))result.stored.generation=work.generation+1;
        }catch(const std::bad_alloc&){result.issue={StoreError::Capacity};}
        catch(const std::filesystem::filesystem_error& error){result.issue={StoreError::Io,error.code().value()};}
        catch(...){result.issue={StoreError::Io,EIO};}
        {std::lock_guard lock(mutex_);completed_=std::move(result);}
    }
    // store's descriptors and process lock die here, never on the game thread.
}
} // namespace voxy::platform

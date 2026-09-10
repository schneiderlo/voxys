#include "engine/platform/native/save_worker.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cerrno>

namespace {
using namespace voxy::platform;
using namespace voxy::game::expedition;
using namespace voxy::game::construction;
constexpr WorldNamespace world{{'s','a','v','e','-','w','o','r','k','e','r','-','0','0','0','1'}};
struct Directory {
    std::filesystem::path path;
    Directory(){
        const auto* base=std::getenv("VOXY_STORE_TEST_ROOT");
        auto name=(std::filesystem::path(base?base:"/tmp")/"voxys-worker-XXXXXX").string();
        const auto* result=::mkdtemp(name.data());if(!result)throw std::runtime_error("mkdtemp");path=result;
    }
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
NativeSaveWorker::Result completion(NativeSaveWorker& worker){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<end){
        if(auto result=worker.poll())return std::move(*result);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("worker completion timed out");
}
struct DelayedFlush:SaveIoObserver {
    std::mutex mutex;
    std::condition_variable changed;
    bool waiting=false,released=false;
    std::thread::id thread;
    int before(SaveIo operation)noexcept override{
        if(operation==SaveIo::FlushFile){
            std::unique_lock lock(mutex);thread=std::this_thread::get_id();waiting=true;changed.notify_all();
            changed.wait(lock,[&]{return released;});
        }
        return 0;
    }
    bool wait(){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(5),[&]{return waiting;});}
    std::thread::id threadId(){std::lock_guard lock(mutex);return thread;}
    void release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
    ~DelayedFlush(){release();}
};
TEST(NativeSaveWorker, CompletionWaitsForDiskAndOnlyOneOwnedOperationCanBePending){
    Directory directory;DelayedFlush barrier;NativeSaveWorker worker(&barrier);
    ASSERT_TRUE(worker.open(directory.path/"slot",world));
    const auto opened=completion(worker);ASSERT_FALSE(opened.issue);EXPECT_EQ(opened.stored.generation,0u);
    std::vector<std::byte> bytes(131073,std::byte{41});
    ASSERT_TRUE(worker.publish(0,bytes));
    const bool waiting=barrier.wait();
    // Release before fatal assertions/destruction so a failing check cannot hang.
    const bool invisible=!worker.poll().has_value();
    const bool refused=!worker.publish(0,{std::byte{2}});
    const bool offThread=barrier.threadId()!=std::this_thread::get_id();
    barrier.release();
    ASSERT_TRUE(waiting);EXPECT_TRUE(invisible);EXPECT_TRUE(refused);EXPECT_TRUE(offThread);
    auto saved=completion(worker);EXPECT_FALSE(saved.issue);EXPECT_EQ(saved.stored.generation,1u);
    StoreIssue issue;
    EXPECT_FALSE(NativeSaveStore::open(directory.path/"slot",world,issue));EXPECT_EQ(issue.error,StoreError::Busy);
    worker.shutdown();worker.shutdown();EXPECT_FALSE(worker.publish(1,bytes));EXPECT_FALSE(worker.poll());
    auto reopened=NativeSaveStore::open(directory.path/"slot",world,issue);ASSERT_TRUE(reopened);
    StoredGeneration result;ASSERT_TRUE(reopened->load(result,issue));EXPECT_EQ(result.payload,bytes);EXPECT_FALSE(result.needsRepair);
}
TEST(NativeSaveWorker, ShutdownDrainsAcceptedWriteAndReleasesExclusiveWorldOwner){
    Directory directory;DelayedFlush barrier;NativeSaveWorker worker(&barrier);
    ASSERT_TRUE(worker.open(directory.path/"slot",world));ASSERT_FALSE(completion(worker).issue);
    ASSERT_TRUE(worker.publish(0,{std::byte{42}}));const bool waiting=barrier.wait();
    std::atomic<bool> stopped=false;std::thread closer([&]{worker.shutdown();stopped=true;});
    StoreIssue issue;auto competitor=NativeSaveStore::open(directory.path/"slot",world,issue);
    const bool notStopped=!stopped.load();barrier.release();closer.join();
    EXPECT_TRUE(waiting);EXPECT_TRUE(notStopped);EXPECT_FALSE(competitor);EXPECT_EQ(issue.error,StoreError::Busy);
    auto reopened=NativeSaveStore::open(directory.path/"slot",world,issue);ASSERT_TRUE(reopened);
    StoredGeneration result;ASSERT_TRUE(reopened->load(result,issue));EXPECT_EQ(result.generation,1u);
    EXPECT_EQ(result.payload,std::vector{std::byte{42}});EXPECT_FALSE(worker.poll());
}
struct Failure:SaveIoObserver {
    std::atomic<int> error{0};SaveIo boundary=SaveIo::WriteChunk;
    int before(SaveIo operation)noexcept override{return operation==boundary?error.load():0;}
};
TEST(NativeSaveWorker, DiskFullPreservesEarlierSaveAndRetryPublishesOnlyAfterSuccess){
    Directory directory;Failure fault;NativeSaveWorker worker(&fault);
    ASSERT_TRUE(worker.open(directory.path/"slot",world));ASSERT_FALSE(completion(worker).issue);
    ASSERT_TRUE(worker.publish(0,{std::byte{1}}));ASSERT_FALSE(completion(worker).issue);
    fault.error=ENOSPC;ASSERT_TRUE(worker.publish(1,{std::byte{2}}));const auto failed=completion(worker);
    EXPECT_EQ(failed.issue.error,StoreError::NoSpace);EXPECT_FALSE(failed.issue.publicationMayHaveHappened);EXPECT_EQ(failed.stored.generation,0u);
    fault.error=0;ASSERT_TRUE(worker.publish(1,{std::byte{3}}));const auto saved=completion(worker);
    EXPECT_FALSE(saved.issue);EXPECT_EQ(saved.stored.generation,2u);
}
TEST(NativeSaveWorker, UncertainPublicationCannotAcknowledgeOrReuseThePoisonedWriter){
    Directory directory;Failure fault;fault.boundary=SaveIo::FlushDirectory;NativeSaveWorker worker(&fault);
    ASSERT_TRUE(worker.open(directory.path/"slot",world));ASSERT_FALSE(completion(worker).issue);
    ASSERT_TRUE(worker.publish(0,{std::byte{1}}));ASSERT_FALSE(completion(worker).issue);
    fault.error=EIO;ASSERT_TRUE(worker.publish(1,{std::byte{2}}));const auto failed=completion(worker);
    EXPECT_TRUE(failed.issue.publicationMayHaveHappened);EXPECT_EQ(failed.stored.generation,0u);
    fault.error=0;ASSERT_TRUE(worker.publish(1,{std::byte{3}}));const auto poisoned=completion(worker);
    EXPECT_EQ(poisoned.issue.error,StoreError::RecoveryRequired);EXPECT_TRUE(poisoned.issue.publicationMayHaveHappened);
    worker.shutdown();StoreIssue issue;auto reopened=NativeSaveStore::open(directory.path/"slot",world,issue);ASSERT_TRUE(reopened);
    StoredGeneration value;ASSERT_TRUE(reopened->load(value,issue));EXPECT_EQ(value.generation,2u);
    EXPECT_EQ(value.payload,std::vector{std::byte{2}});EXPECT_TRUE(value.needsRepair);
}
} // namespace

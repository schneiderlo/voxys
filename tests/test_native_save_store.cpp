#include "engine/platform/native/save_store.hpp"
#include "game/expedition/session_save.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace voxy::platform;
using namespace voxy::game::expedition;
using namespace voxy::game::construction;
constexpr WorldNamespace kWorld{{'s','t','o','r','e','-','f','i','x','t','u','r','e','-','0','1'}};
struct Folder {
    std::filesystem::path path;
    Folder(){
        const auto* base=std::getenv("VOXY_STORE_TEST_ROOT");
        const auto root=base?std::filesystem::path(base):std::filesystem::path("/tmp");
        auto name=(root/"voxys-native-save-test-XXXXXX").string();
        const auto* result=::mkdtemp(name.data());if(!result)throw std::runtime_error("mkdtemp");path=result;
    }
    ~Folder(){std::error_code error;std::filesystem::remove_all(path,error);}
};
const std::vector<std::byte> kOld(131073,std::byte{11}),kNew(131074,std::byte{22});
std::unique_ptr<NativeSaveStore> openStore(const Folder& folder,SaveIoObserver* observer=nullptr) {
    StoreIssue issue;auto store=NativeSaveStore::open(folder.path/"world",kWorld,issue,observer);
    if(!store)throw std::runtime_error("open store: "+std::to_string(static_cast<int>(issue.error))+" errno "+std::to_string(issue.systemError));
    return store;
}
void writeFile(const std::filesystem::path& path,std::span<const std::byte> bytes) {
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(stream.good());
}
struct Observe final:SaveIoObserver {
    size_t calls=0,afterCalls=0,failAt=0,killAt=0;
    int code=EIO;
    int before(SaveIo)noexcept override{return ++calls==failAt?code:0;}
    void after(SaveIo)noexcept override{if(++afterCalls==killAt)::kill(::getpid(),SIGKILL);}
};
TEST(NativeSaveStore, CreatesNestedSlotAndPublishesTwoIdenticalCopiesWithRevisionChecks) {
    Folder folder;StoreIssue issue;StoredGeneration loaded;
    auto store=NativeSaveStore::open(folder.path/"nested"/"slot",kWorld,issue);ASSERT_TRUE(store);
    ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.generation,0u);
    ASSERT_TRUE(store->publish(0,kOld,issue));ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.generation,1u);EXPECT_EQ(loaded.payload,kOld);EXPECT_FALSE(loaded.needsRepair);
    EXPECT_FALSE(store->publish(0,kNew,issue));EXPECT_EQ(issue.error,StoreError::Conflict);
    ASSERT_TRUE(store->publish(1,kNew,issue));store.reset();
    store=NativeSaveStore::open(folder.path/"nested"/"slot",kWorld,issue);ASSERT_TRUE(store);
    ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.generation,2u);EXPECT_EQ(loaded.payload,kNew);EXPECT_FALSE(loaded.needsRepair);
    EXPECT_FALSE(std::filesystem::exists(folder.path/"nested"/"slot"/"next"));
}
TEST(NativeSaveStore, RecoversLatestAcknowledgedCopyAndRefusesBothDamagedOrFutureFormat) {
    for(const auto* damaged:{"current","mirror"}) {
        Folder folder;auto store=openStore(folder);StoreIssue issue;StoredGeneration loaded;
        ASSERT_TRUE(store->publish(0,kOld,issue));ASSERT_TRUE(store->publish(1,kNew,issue));
        writeFile(folder.path/"world"/damaged,std::span(kOld).first(3));
        ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.generation,2u);EXPECT_EQ(loaded.payload,kNew);EXPECT_TRUE(loaded.needsRepair);
        ASSERT_TRUE(store->publish(2,kNew,issue));ASSERT_TRUE(store->load(loaded,issue));EXPECT_FALSE(loaded.needsRepair);
        std::vector<std::byte> future;ASSERT_TRUE(encodeStoredGeneration(kWorld,4,kNew,future,issue));future.at(4)=std::byte{2};
        writeFile(folder.path/"world"/damaged,future);
        EXPECT_FALSE(store->load(loaded,issue));EXPECT_EQ(issue.error,StoreError::UnsupportedSchema);EXPECT_EQ(loaded.generation,3u);
        writeFile(folder.path/"world"/"current",{});writeFile(folder.path/"world"/"mirror",{});
        EXPECT_FALSE(store->load(loaded,issue));EXPECT_EQ(issue.error,StoreError::InvalidData);EXPECT_EQ(loaded.generation,3u);
        EXPECT_FALSE(store->publish(0,kOld,issue));
    }
}
TEST(NativeSaveStore, ExclusiveWriterIsReleasedByCloseAndProcessExit) {
    Folder folder;StoreIssue issue;auto store=openStore(folder);
    EXPECT_FALSE(NativeSaveStore::open(folder.path/"world",kWorld,issue));EXPECT_EQ(issue.error,StoreError::Busy);
    const auto pid=::fork();ASSERT_GE(pid,0);
    if(pid==0){auto other=NativeSaveStore::open(folder.path/"world",kWorld,issue);::_exit(!other&&issue.error==StoreError::Busy?0:2);}
    int status=0;ASSERT_EQ(::waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFEXITED(status));EXPECT_EQ(WEXITSTATUS(status),0);
    store.reset();EXPECT_TRUE(openStore(folder));
}
TEST(NativeSaveStore, RefusesSymlinksAndHardlinkedStagingWithoutTouchingTheirTargets) {
    Folder folder;auto store=openStore(folder);StoreIssue issue;ASSERT_TRUE(store->publish(0,kOld,issue));
    const auto target=folder.path/"external";writeFile(target,std::span(kNew).first(3));
    for(bool hard:{false,true}) {
        if(hard)std::filesystem::create_hard_link(target,folder.path/"world"/"next");
        else std::filesystem::create_symlink(target,folder.path/"world"/"next");
        EXPECT_FALSE(store->publish(1,kNew,issue));EXPECT_FALSE(issue.publicationMayHaveHappened);
        EXPECT_EQ(std::filesystem::file_size(target),3u);std::filesystem::remove(folder.path/"world"/"next");
    }
    std::filesystem::create_directory_symlink(folder.path/"world",folder.path/"alias");
    EXPECT_FALSE(NativeSaveStore::open(folder.path/"alias",kWorld,issue));
    EXPECT_FALSE(NativeSaveStore::open("relative",kWorld,issue));EXPECT_EQ(issue.error,StoreError::InvalidPath);
}
TEST(NativeSaveStore, EveryInjectedIoFailurePreservesACompleteGenerationAndReportsAmbiguity) {
    Observe baseline;size_t boundaries=0;
    {Folder folder;auto store=openStore(folder,&baseline);StoreIssue issue;ASSERT_TRUE(store->publish(0,kNew,issue));boundaries=baseline.calls;}
    ASSERT_EQ(boundaries,16u);
    for(size_t cut=1;cut<=boundaries;++cut) {
        Folder folder;StoreIssue issue;{auto store=openStore(folder);ASSERT_TRUE(store->publish(0,kOld,issue));}
        Observe fault;fault.failAt=cut;auto store=openStore(folder,&fault);
        EXPECT_FALSE(store->publish(1,kNew,issue))<<cut;EXPECT_EQ(issue.error,StoreError::Io)<<cut;
        const bool uncertain=issue.publicationMayHaveHappened;
        if(uncertain){EXPECT_FALSE(store->publish(1,kNew,issue));EXPECT_EQ(issue.error,StoreError::RecoveryRequired);}
        store.reset();store=openStore(folder);StoredGeneration loaded;ASSERT_TRUE(store->load(loaded,issue))<<cut;
        EXPECT_TRUE((loaded.generation==1&&loaded.payload==kOld)||(loaded.generation==2&&loaded.payload==kNew))<<cut;
        ASSERT_TRUE(store->publish(loaded.generation,kNew,issue));ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.payload,kNew);EXPECT_FALSE(loaded.needsRepair);
    }
}
TEST(NativeSaveStore, ActualProcessKillAtEveryWriteBoundaryLeavesAnIntactRecoverableSave) {
    constexpr size_t boundaries=16;
    for(size_t cut=1;cut<=boundaries;++cut) {
        Folder folder;StoreIssue issue;{auto store=openStore(folder);ASSERT_TRUE(store->publish(0,kOld,issue));}
        const auto pid=::fork();ASSERT_GE(pid,0);
        if(pid==0){Observe observer;observer.killAt=cut;auto store=openStore(folder,&observer);(void)store->publish(1,kNew,issue);::_exit(2);}
        int status=0;ASSERT_EQ(::waitpid(pid,&status,0),pid);ASSERT_TRUE(WIFSIGNALED(status))<<cut;EXPECT_EQ(WTERMSIG(status),SIGKILL);
        auto store=openStore(folder);StoredGeneration loaded;ASSERT_TRUE(store->load(loaded,issue))<<cut;
        EXPECT_TRUE((loaded.generation==1&&loaded.payload==kOld)||(loaded.generation==2&&loaded.payload==kNew))<<cut;
        ASSERT_TRUE(store->publish(loaded.generation,kNew,issue));ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.payload,kNew);EXPECT_FALSE(loaded.needsRepair);
    }
}
TEST(NativeSaveStore, ClassifiesFullDiskAndPermissionErrorsWithoutAcknowledging) {
    for(int error:{ENOSPC,EDQUOT,EACCES,EROFS}) {
        Folder folder;StoreIssue issue;{auto store=openStore(folder);ASSERT_TRUE(store->publish(0,kOld,issue));}
        Observe fault;fault.failAt=2;fault.code=error;auto store=openStore(folder,&fault);
        EXPECT_FALSE(store->publish(1,kNew,issue));EXPECT_EQ(issue.error,error==ENOSPC||error==EDQUOT?StoreError::NoSpace:StoreError::Permission);
        EXPECT_EQ(issue.systemError,error);EXPECT_FALSE(issue.publicationMayHaveHappened);
        StoredGeneration loaded;ASSERT_TRUE(store->load(loaded,issue));EXPECT_EQ(loaded.generation,1u);EXPECT_EQ(loaded.payload,kOld);
    }
}
TEST(NativeSaveStore, DoesNotReplaceConflictingReplicasOrAnotherWorld) {
    Folder folder;StoreIssue issue;auto store=openStore(folder);ASSERT_TRUE(store->publish(0,kOld,issue));
    std::vector<std::byte> conflict;ASSERT_TRUE(encodeStoredGeneration(kWorld,1,kNew,conflict,issue));writeFile(folder.path/"world"/"mirror",conflict);
    StoredGeneration loaded{9,{std::byte{42}},false};EXPECT_FALSE(store->load(loaded,issue));EXPECT_EQ(issue.error,StoreError::Conflict);EXPECT_EQ(loaded.generation,9u);
    EXPECT_FALSE(store->publish(1,kNew,issue));store.reset();auto other=kWorld;other.bytes[0]^=1;
    store=NativeSaveStore::open(folder.path/"world",other,issue);ASSERT_TRUE(store);EXPECT_FALSE(store->load(loaded,issue));EXPECT_EQ(issue.error,StoreError::InvalidData);
}

class StoreAdapter final:public PreparationAdapter {
public:
    PreparationResult begin(const PreparationRequest& request)override{return {request.ticket,PreparationState::Ready};}
    PreparationResult poll(PreparationTicket ticket)noexcept override{return {ticket,PreparationState::Ready};}
    bool canActivate(PreparationTicket)const noexcept override{return true;}
    void activate(PreparationTicket,SimulationTick)noexcept override{}
    void discard(PreparationTicket)noexcept override{}
};
TEST(NativeSaveStore, ActualSessionCheckpointReloadKeepsPurchasedPartAndRejectsOldRequest) {
    Folder folder;StoreIssue storeIssue;CatalogIssue catalogIssue;const auto catalog=PartCatalog::create(makeStarterCatalogDraft(),catalogIssue);ASSERT_TRUE(catalog);
    const auto id=[](uint64_t counter){return DurableId{kWorld,counter};};
    SessionBootstrap boot;boot.world=kWorld;boot.caller={id(1),id(2)};boot.lastIssuedId=100;boot.inventory={10000,10000};boot.workshopEnabled=true;
    BuildSnapshot build;build.id=id(3);build.owner=id(1);boot.builds={build};
    StoreAdapter adapter;SessionIssue sessionIssue;auto session=GameSession::create(boot,EventStreamIncarnation{{'o','l','d'}},*catalog,adapter,sessionIssue);ASSERT_TRUE(session);
    const Command buy{AuthorityEpoch{1},RequestSequence{1},SessionRevision{},AddPart{{id(3),{}},starterPartKey(StarterPart::Plate),{{-50,29,137},{}}}};
    ASSERT_EQ(session->submit(boot.caller,buy).state,ReceiptState::PendingPreparation);ASSERT_TRUE(session->advanceOneTick());
    const auto balance=session->snapshot().inventory;const auto bought=session->snapshot().builds.at(0).parts.at(0);
    RecoveryContentIdentity content;content.manifest={id(9000),1};content.manifestDigest.fill(std::byte{7});RecoveryIssue recovery;
    const auto checkpoint=SessionRecovery::capture(*session,content,recovery);ASSERT_TRUE(checkpoint);
    const auto admitted=SessionRecovery::admit(*checkpoint,{kWorld,content},*catalog,recovery);ASSERT_TRUE(admitted);
    SaveCodecIssue codec;std::vector<std::byte> bytes;ASSERT_TRUE(SessionSaveCodec::encodeCheckpoint(*admitted,bytes,codec));
    auto store=openStore(folder);ASSERT_TRUE(store->publish(0,bytes,storeIssue));store.reset();session.reset();
    // Close and reopen real files before constructing a fresh local authority.
    store=openStore(folder);StoredGeneration disk;ASSERT_TRUE(store->load(disk,storeIssue));EXPECT_FALSE(disk.needsRepair);
    auto decoded=SessionSaveCodec::decodeCheckpoint(disk.payload,{kWorld,content},*catalog,codec);ASSERT_TRUE(decoded);
    StoreAdapter fresh;const auto restored=SessionRecovery::restore(decoded,EventStreamIncarnation{{'n','e','w'}},fresh,recovery);ASSERT_TRUE(restored);
    EXPECT_EQ(restored->session->snapshot().inventory,balance);EXPECT_EQ(restored->session->snapshot().builds.at(0).parts.at(0),bought);
    EXPECT_EQ(restored->session->submit(boot.caller,buy).issue.error,SessionError::WrongToken);
    EXPECT_EQ(restored->session->snapshot().inventory,balance);
    // Fresh lineage and physical scene are intentionally not published here;
    // those require the integrated session-store/SAVE-04 transaction.
}

} // namespace

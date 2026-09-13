#include "engine/platform/native/native_adventure_saves.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <atomic>
#include <cerrno>
#include <unistd.h>

namespace {
using namespace voxy::platform;
using namespace voxy::game::adventure;
struct AdventureFolder {
    std::filesystem::path root;
    AdventureFolder(){std::string pattern="/tmp/voxys-adventure-storage-XXXXXX";const auto* p=::mkdtemp(pattern.data());if(!p)throw std::runtime_error("mkdtemp");root=p;}
    ~AdventureFolder(){std::error_code error;std::filesystem::remove_all(root,error);}
};
AdventureContent testContent(){AdventureContent content;content.identity.bytes[0]=std::byte{97};content.town={1,12,3,0};return content;}
std::optional<NativeAdventureSaves::Completion> wait(NativeAdventureSaves& owner) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline){if(auto result=owner.poll())return result;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return std::nullopt;
}
std::vector<std::byte> freshBytes(NativeAdventureSaves& owner,const AdventureContent& content) {
    std::string error;auto session=AdventureSession::create(owner.worldIdentity(),content,error);if(!session)throw std::runtime_error(error);
    std::vector<std::byte> bytes;if(!AdventureSaveCodec::encode(session->state(),content,bytes,error))throw std::runtime_error(error);return bytes;
}
TEST(NativeAdventureSavesTest, SeparateNamespaceAsyncPublicationAndExactRestart) {
    AdventureFolder folder;const auto content=testContent();std::string error;
    auto owner=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(owner)<<error;
    EXPECT_TRUE(owner->loadedBytes().empty());const auto world=owner->world();auto bytes=freshBytes(*owner,content);
    ASSERT_TRUE(owner->requestSave(bytes,error))<<error;EXPECT_TRUE(owner->busy());EXPECT_FALSE(owner->requestSave(bytes,error));
    const auto saved=wait(*owner);ASSERT_TRUE(saved);EXPECT_TRUE(saved->saved)<<saved->message;EXPECT_EQ(saved->generation,1u);
    EXPECT_TRUE(std::filesystem::is_regular_file(folder.root/"adventure-v1"/world/"current"));
    EXPECT_TRUE(std::filesystem::is_regular_file(folder.root/"adventure-v1"/world/"mirror"));owner.reset();
    auto reopened=NativeAdventureSaves::open(folder.root,{},false,content,error);ASSERT_TRUE(reopened)<<error;
    EXPECT_EQ(reopened->world(),world);EXPECT_EQ(reopened->loadedBytes(),bytes);EXPECT_EQ(reopened->generation(),1u);
}
TEST(NativeAdventureSavesTest, MissingForeignAndBusyWorldsCannotSilentlyStartFresh) {
    AdventureFolder folder;const auto content=testContent();std::string error;
    EXPECT_FALSE(NativeAdventureSaves::open(folder.root,"0102030405060708090a0b0c0d0e0f10",false,content,error));
    auto owner=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(owner)<<error;
    EXPECT_FALSE(NativeAdventureSaves::open(folder.root,owner->world(),false,content,error));EXPECT_NE(error.find("another game window"),std::string::npos);
    const auto bytes=freshBytes(*owner,content);auto bad=bytes;bad[0]=std::byte{};EXPECT_FALSE(owner->requestSave(bad,error));EXPECT_FALSE(owner->busy());
    auto foreign=AdventureSession::create({{'d','i','f','f','e','r','e','n','t'}},content,error);ASSERT_TRUE(foreign);
    std::vector<std::byte> wrong;ASSERT_TRUE(AdventureSaveCodec::encode(foreign->state(),content,wrong,error));EXPECT_FALSE(owner->requestSave(wrong,error));
}
TEST(NativeAdventureSavesTest, NewWorldKeepsOldSaveAndDefaultUsesConfirmedWorld) {
    AdventureFolder folder;const auto content=testContent();std::string error;
    auto first=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(first);const auto firstId=first->world();const auto firstBytes=freshBytes(*first,content);
    ASSERT_TRUE(first->requestSave(firstBytes,error));ASSERT_TRUE(wait(*first)->saved);first.reset();
    auto second=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(second);EXPECT_NE(second->world(),firstId);second.reset();
    auto lastConfirmed=NativeAdventureSaves::open(folder.root,{},false,content,error);ASSERT_TRUE(lastConfirmed)<<error;
    EXPECT_EQ(lastConfirmed->world(),firstId);EXPECT_EQ(lastConfirmed->loadedBytes(),firstBytes);
}
TEST(NativeAdventureSavesTest, DamagedCurrentRecoversMirrorAndRejectsContentMismatch) {
    AdventureFolder folder;const auto content=testContent();std::string error;
    auto owner=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(owner);const auto world=owner->world();const auto bytes=freshBytes(*owner,content);
    ASSERT_TRUE(owner->requestSave(bytes,error));ASSERT_TRUE(wait(*owner)->saved);owner.reset();
    {std::ofstream damaged(folder.root/"adventure-v1"/world/"current",std::ios::binary|std::ios::trunc);damaged<<"bad";}
    auto recovered=NativeAdventureSaves::open(folder.root,world,false,content,error);ASSERT_TRUE(recovered)<<error;EXPECT_EQ(recovered->loadedBytes(),bytes);recovered.reset();
    std::filesystem::remove(folder.root/"adventure-v1"/world/"current");
    auto mirrorOnly=NativeAdventureSaves::open(folder.root,{},false,content,error);ASSERT_TRUE(mirrorOnly)<<error;
    EXPECT_EQ(mirrorOnly->world(),world);EXPECT_EQ(mirrorOnly->loadedBytes(),bytes);mirrorOnly.reset();
    auto changed=content;changed.identity.bytes[0]^=std::byte{1};EXPECT_FALSE(NativeAdventureSaves::open(folder.root,world,false,changed,error));
}
TEST(NativeAdventureSavesTest, FailedDiskPublicationRemainsRetryableWithoutLosingPriorSave) {
    AdventureFolder folder;const auto content=testContent();std::string error;
    auto owner=NativeAdventureSaves::open(folder.root,{},true,content,error);ASSERT_TRUE(owner);auto bytes=freshBytes(*owner,content);
    ASSERT_TRUE(owner->requestSave(bytes,error));ASSERT_TRUE(wait(*owner)->saved);
    const auto next=folder.root/"adventure-v1"/owner->world()/"next";std::filesystem::create_directory(next);
    auto session=AdventureSession::create(owner->worldIdentity(),content,error);ASSERT_TRUE(session);ASSERT_TRUE(session->updatePlayer({2,12,3,0},100,error));
    std::vector<std::byte> newer;ASSERT_TRUE(AdventureSaveCodec::encode(session->state(),content,newer,error));ASSERT_TRUE(owner->requestSave(newer,error));
    const auto failed=wait(*owner);ASSERT_TRUE(failed);EXPECT_FALSE(failed->saved);EXPECT_EQ(owner->generation(),1u);EXPECT_EQ(owner->loadedBytes(),bytes);
    std::filesystem::remove(next);ASSERT_TRUE(owner->requestSave(newer,error))<<error;const auto saved=wait(*owner);ASSERT_TRUE(saved);EXPECT_TRUE(saved->saved)<<saved->message;EXPECT_EQ(owner->generation(),2u);
}
TEST(NativeAdventureSavesTest, NewPrimaryWithoutMatchingMirrorNeverAcknowledgesUntilRetryRepairsBoth) {
    struct MirrorFailure:SaveIoObserver {
        std::atomic<bool> armed=false;
        std::atomic<unsigned> stageOpens=0;
        int before(SaveIo operation) noexcept override {
            if(armed&&operation==SaveIo::OpenStage&&++stageOpens==2)return ENOSPC;
            return 0;
        }
    } fault;
    AdventureFolder folder;const auto content=testContent();std::string error;
    auto owner=NativeAdventureSaves::open(folder.root,{},true,content,error,&fault);ASSERT_TRUE(owner)<<error;
    auto initial=freshBytes(*owner,content);ASSERT_TRUE(owner->requestSave(initial,error));
    auto saved=wait(*owner);ASSERT_TRUE(saved);ASSERT_TRUE(saved->saved);ASSERT_EQ(saved->generation,1u);
    auto state=AdventureSession::create(owner->worldIdentity(),content,error);ASSERT_TRUE(state);
    ASSERT_TRUE(state->updatePlayer({2,12,3,0},100,error));
    std::vector<std::byte> newer;ASSERT_TRUE(AdventureSaveCodec::encode(state->state(),content,newer,error));
    fault.armed=true;ASSERT_TRUE(owner->requestSave(newer,error));
    const auto incomplete=wait(*owner);ASSERT_TRUE(incomplete);EXPECT_FALSE(incomplete->saved);
    EXPECT_FALSE(incomplete->recoveryRequired);EXPECT_EQ(incomplete->generation,2u);
    EXPECT_EQ(incomplete->message,"Save not confirmed. Try Save again.");
    const auto copy=[&](const char* name) {
        const auto path=folder.root/"adventure-v1"/owner->world()/name;
        std::ifstream stream(path,std::ios::binary);std::vector<char> raw((std::istreambuf_iterator<char>(stream)),{});
        voxy::game::expedition::StoredGeneration generation;StoreIssue issue;
        EXPECT_TRUE(voxy::game::expedition::decodeStoredGeneration(std::as_bytes(std::span(raw)),owner->worldIdentity(),generation,issue));
        return generation;
    };
    EXPECT_EQ(copy("current").generation,2u);EXPECT_EQ(copy("current").payload,newer);
    EXPECT_EQ(copy("mirror").generation,1u);EXPECT_EQ(copy("mirror").payload,initial);
    fault.armed=false;ASSERT_TRUE(owner->requestSave(newer,error));
    saved=wait(*owner);ASSERT_TRUE(saved);EXPECT_TRUE(saved->saved);EXPECT_EQ(saved->generation,3u);
    EXPECT_EQ(copy("current").generation,3u);EXPECT_EQ(copy("mirror").generation,3u);
    EXPECT_EQ(copy("current").payload,newer);EXPECT_EQ(copy("mirror").payload,newer);
}

}

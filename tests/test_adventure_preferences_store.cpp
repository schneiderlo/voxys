#include "engine/platform/native/adventure_preferences_store.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using namespace voxy::platform;
using namespace voxy::game::adventure;
#if defined(__linux__)
class AdventurePreferencesStore:public testing::Test {
protected:
    std::filesystem::path directory;
    void SetUp() override {
        std::string pattern="/tmp/voxys-adventure-prefs-XXXXXX";
        char* created=::mkdtemp(pattern.data());ASSERT_NE(created,nullptr);directory=created;
    }
    void TearDown() override {std::error_code error;std::filesystem::remove_all(directory,error);}
    static AdventurePreferences changed() {
        AdventurePreferences p;p.textScale=1.5;p.highContrast=true;p.reducedMotion=true;
        p.mouseSensitivity=1.75;p.padSensitivity=2;p.invertY=true;p.orbitToggle=true;
        return p;
    }
    static void write(const std::filesystem::path& path,const std::string& text) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);ASSERT_TRUE(file);
        file.write(text.data(),static_cast<std::streamsize>(text.size()));ASSERT_TRUE(file);
    }
    static std::string bytes(const std::filesystem::path& path) {
        std::ifstream file(path,std::ios::binary);
        return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    }
    static size_t entryCount(const std::filesystem::path& path) {
        size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(path)){(void)entry;++count;}return count;
    }
};

TEST_F(AdventurePreferencesStore, CreatesMissingParentsAndFreshObjectReopensExactAtomicReplacement) {
    const auto path=directory/"settings"/"local"/"adventure-preferences-v1.json";
    const auto world=directory/"world.bin",cove=directory/"cove-preferences.json";
    write(world,"owned world remains exact");write(cove,"separate Cove preferences remain exact");
    auto active=changed();const auto initial=active;std::string message;
    EXPECT_EQ(loadNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Missing);EXPECT_EQ(active,initial);
    ASSERT_EQ(saveNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Saved)<<message;
    std::string canonical;ASSERT_TRUE(encodeAdventurePreferences(active,canonical,message));EXPECT_EQ(bytes(path),canonical);
    EXPECT_LE(canonical.size(),kMaximumAdventurePreferencesBytes);
    struct stat status{};ASSERT_EQ(::stat(path.c_str(),&status),0);EXPECT_EQ(status.st_mode&0777,0600u);
    AdventurePreferences reopened;
    ASSERT_EQ(loadNativeAdventurePreferences(path,reopened,message),AdventurePreferencesStatus::Loaded)<<message;EXPECT_EQ(reopened,active);
    // The previous descriptors are already closed. A new settings object reads
    // the complete replacement; this is file reopen, not a process-restart claim.
    active.textScale=1.25;active.mouseSensitivity=.75;active.invertX=true;
    ASSERT_EQ(saveNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Saved)<<message;
    AdventurePreferences replacement;
    ASSERT_EQ(loadNativeAdventurePreferences(path,replacement,message),AdventurePreferencesStatus::Loaded)<<message;EXPECT_EQ(replacement,active);
    EXPECT_NE(replacement,initial);EXPECT_EQ(entryCount(path.parent_path()),1u);
    EXPECT_EQ(bytes(world),"owned world remains exact");EXPECT_EQ(bytes(cove),"separate Cove preferences remain exact");
}

TEST_F(AdventurePreferencesStore, EmptyCorruptOversizedAndMissingReadsPreserveActiveSettings) {
    const auto path=directory/"adventure-preferences-v1.json";auto active=changed();const auto initial=active;std::string message;
    for(const auto& payload:std::array<std::string,3>{{"","{broken",std::string(kMaximumAdventurePreferencesBytes+1,' ')}}) {
        write(path,payload);EXPECT_EQ(loadNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Invalid);
        EXPECT_EQ(active,initial);EXPECT_EQ(bytes(path),payload);EXPECT_FALSE(message.empty());
    }
    ASSERT_TRUE(std::filesystem::remove(path));
    EXPECT_EQ(loadNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Missing);EXPECT_EQ(active,initial);
    const auto blocked=directory/"parent-is-file";write(blocked,"retained parent bytes");
    EXPECT_EQ(loadNativeAdventurePreferences(blocked/"settings.json",active,message),AdventurePreferencesStatus::Unavailable);
    EXPECT_EQ(active,initial);EXPECT_EQ(bytes(blocked),"retained parent bytes");
}

TEST_F(AdventurePreferencesStore, SymlinkFileAndDirectParentAreNeverFollowedOrReplaced) {
    const auto target=directory/"target.json",link=directory/"adventure-preferences-v1.json";
    const auto saved=changed();std::string message;
    ASSERT_EQ(saveNativeAdventurePreferences(target,saved,message),AdventurePreferencesStatus::Saved);
    const auto original=bytes(target);std::filesystem::create_symlink(target,link);
    AdventurePreferences active;active.padSensitivity=1.5;const auto before=active;
    EXPECT_EQ(loadNativeAdventurePreferences(link,active,message),AdventurePreferencesStatus::Unavailable);EXPECT_EQ(active,before);
    EXPECT_EQ(saveNativeAdventurePreferences(link,active,message),AdventurePreferencesStatus::Unavailable);EXPECT_EQ(active,before);
    EXPECT_TRUE(std::filesystem::is_symlink(link));EXPECT_EQ(bytes(target),original);
    const auto real=directory/"real-parent",linked=directory/"linked-parent";
    std::filesystem::create_directory(real);
    ASSERT_EQ(saveNativeAdventurePreferences(real/"settings.json",saved,message),AdventurePreferencesStatus::Saved);
    const auto parentBytes=bytes(real/"settings.json");std::filesystem::create_directory_symlink(real,linked);
    EXPECT_EQ(loadNativeAdventurePreferences(linked/"settings.json",active,message),AdventurePreferencesStatus::Unavailable);
    EXPECT_EQ(saveNativeAdventurePreferences(linked/"settings.json",active,message),AdventurePreferencesStatus::Unavailable);
    EXPECT_EQ(active,before);EXPECT_EQ(bytes(real/"settings.json"),parentBytes);EXPECT_TRUE(std::filesystem::is_symlink(linked));
}

TEST_F(AdventurePreferencesStore, DirectoryAndFifoRefuseWithoutBlockingOrReplacingTheirEntries) {
    const auto folder=directory/"directory.json",fifo=directory/"pipe.json";
    std::filesystem::create_directory(folder);ASSERT_EQ(::mkfifo(fifo.c_str(),0600),0);
    auto active=changed();const auto original=active;std::string message;
    for(const auto& path:std::array<std::filesystem::path,2>{{folder,fifo}}) {
        EXPECT_EQ(loadNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Invalid);EXPECT_EQ(active,original);
        EXPECT_EQ(saveNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Unavailable);EXPECT_EQ(active,original);
    }
    EXPECT_TRUE(std::filesystem::is_directory(folder));struct stat metadata{};ASSERT_EQ(::lstat(fifo.c_str(),&metadata),0);EXPECT_TRUE(S_ISFIFO(metadata.st_mode));
    EXPECT_EQ(entryCount(directory),2u);
}

TEST_F(AdventurePreferencesStore, InvalidSettingsAndUnavailableDestinationsPreserveConfirmedBytes) {
    const auto path=directory/"adventure-preferences-v1.json";const auto active=changed();std::string message;
    ASSERT_EQ(saveNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Saved);const auto confirmed=bytes(path);
    auto invalid=active;invalid.mouseSensitivity=0;const auto rejected=invalid;
    EXPECT_EQ(saveNativeAdventurePreferences(path,invalid,message),AdventurePreferencesStatus::Invalid);
    EXPECT_EQ(invalid,rejected);EXPECT_EQ(bytes(path),confirmed);EXPECT_EQ(entryCount(directory),1u);
    const auto blocker=directory/"not-a-directory";write(blocker,"do not replace");
    EXPECT_EQ(saveNativeAdventurePreferences(blocker/"settings.json",active,message),AdventurePreferencesStatus::Unavailable);
    EXPECT_EQ(bytes(blocker),"do not replace");EXPECT_EQ(bytes(path),confirmed);EXPECT_EQ(entryCount(directory),2u);
    AdventurePreferences reloaded;ASSERT_EQ(loadNativeAdventurePreferences(path,reloaded,message),AdventurePreferencesStatus::Loaded);
    EXPECT_EQ(reloaded,active);
}

TEST_F(AdventurePreferencesStore, InvalidPathsCannotRedirectReadsOrWrites) {
    const auto active=changed();std::string message;
    const std::array<std::filesystem::path,5> paths{{{},"relative-settings.json",directory/".",directory/std::string(97,'x'),
        std::filesystem::path(directory.string()+std::string("\0ignored",8)+"/settings.json")}};
    for(const auto& path:paths) {
        auto retained=active;EXPECT_EQ(loadNativeAdventurePreferences(path,retained,message),AdventurePreferencesStatus::Invalid);EXPECT_EQ(retained,active);
        EXPECT_EQ(saveNativeAdventurePreferences(path,active,message),AdventurePreferencesStatus::Invalid);
    }
    EXPECT_EQ(entryCount(directory),0u);
}
#else
TEST(AdventurePreferencesStore, UnsupportedHostKeepsCurrentSettingsAndReportsItsLimit) {
    AdventurePreferences active;active.highContrast=true;const auto before=active;std::string message;
    EXPECT_EQ(loadNativeAdventurePreferences("/adventure-preferences-v1.json",active,message),AdventurePreferencesStatus::Unsupported);
    EXPECT_EQ(saveNativeAdventurePreferences("/adventure-preferences-v1.json",active,message),AdventurePreferencesStatus::Unsupported);
    EXPECT_EQ(active,before);EXPECT_FALSE(message.empty());
}
#endif
}

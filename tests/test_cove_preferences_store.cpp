#include "engine/platform/native/cove_preferences_store.hpp"
#include <gtest/gtest.h>
#include <fstream>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
using namespace voxy::platform;
using voxy::game::expedition::CoveInputPreferences;
#if defined(__linux__)
class CovePreferencesStore : public ::testing::Test {
protected:
    std::filesystem::path directory;
    void SetUp() override {
        std::string name="/tmp/voxys-preferences-XXXXXX";char* result=::mkdtemp(name.data());ASSERT_NE(result,nullptr);directory=result;
    }
    void TearDown() override {std::error_code error;std::filesystem::remove_all(directory,error);}
};
TEST_F(CovePreferencesStore, AtomicReplacementLoadsExactSettingsAndDoesNotTouchWorld) {
    const auto path=directory/"Settings"/"preferences.json";CoveInputPreferences p,loaded;std::string message;
    EXPECT_EQ(loadNativeCovePreferences(path,loaded,message),CovePreferencesStatus::Missing);
    std::ofstream(directory/"world.svce")<<"owned world remains";
    p.reelToggle=true;p.textScale=1.5;p.highContrast=true;
    ASSERT_EQ(saveNativeCovePreferences(path,p,message),CovePreferencesStatus::Saved)<<message;
    ASSERT_EQ(loadNativeCovePreferences(path,loaded,message),CovePreferencesStatus::Loaded)<<message;EXPECT_EQ(p,loaded);
    p.invertY=true;p.padSensitivity=2;ASSERT_EQ(saveNativeCovePreferences(path,p,message),CovePreferencesStatus::Saved);
    ASSERT_EQ(loadNativeCovePreferences(path,loaded,message),CovePreferencesStatus::Loaded);EXPECT_EQ(p,loaded);
    size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())){(void)entry;++count;}EXPECT_EQ(count,1u);
    std::ifstream world(directory/"world.svce");std::string content;std::getline(world,content);EXPECT_EQ(content,"owned world remains");
}
TEST_F(CovePreferencesStore, CorruptOversizedAndUnavailableMetadataPreserveActiveControls) {
    const auto path=directory/"preferences.json";CoveInputPreferences p;p.mouseSensitivity=2;const auto initial=p;std::string message;
    std::ofstream(path)<<"{bad";EXPECT_EQ(loadNativeCovePreferences(path,p,message),CovePreferencesStatus::Invalid);EXPECT_EQ(p,initial);
    std::ofstream(path)<<std::string(voxy::game::expedition::kMaximumCovePreferencesBytes+1,' ');
    EXPECT_EQ(loadNativeCovePreferences(path,p,message),CovePreferencesStatus::Invalid);EXPECT_EQ(p,initial);
    std::filesystem::create_directory(directory/"blocked");
    EXPECT_EQ(saveNativeCovePreferences(directory/"blocked",p,message),CovePreferencesStatus::Unavailable);EXPECT_EQ(p,initial);
    auto invalid=p;invalid.textScale=.5;EXPECT_EQ(saveNativeCovePreferences(path,invalid,message),CovePreferencesStatus::Invalid);
}
TEST_F(CovePreferencesStore, SymlinkRefusesWithoutReadingOrReplacingTarget) {
    const auto target=directory/"target.json",link=directory/"preferences.json";CoveInputPreferences p;std::string message;
    std::ofstream(target)<<"retained target";std::filesystem::create_symlink(target,link);
    EXPECT_EQ(loadNativeCovePreferences(link,p,message),CovePreferencesStatus::Unavailable);
    EXPECT_EQ(saveNativeCovePreferences(link,p,message),CovePreferencesStatus::Unavailable);
    std::ifstream file(target);std::string content;std::getline(file,content);EXPECT_EQ(content,"retained target");EXPECT_TRUE(std::filesystem::is_symlink(link));
}
#else
TEST(CovePreferencesStore, UnsupportedHostIsExplicit) {
    CoveInputPreferences p;std::string message;
    EXPECT_EQ(loadNativeCovePreferences("/preferences.json",p,message),CovePreferencesStatus::Unsupported);
}
#endif
}

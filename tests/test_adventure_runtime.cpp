#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/construction_policy.hpp"
#include "engine/platform/input.hpp"
#include "camera/camera.hpp"
#include "gpu/context.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace voxy::game::adventure {
namespace {
struct EnvironmentValue {
    std::string name;
    std::optional<std::string> previous;
    EnvironmentValue(const char* key,const char* value):name(key) {
        if(const char* old=std::getenv(key))previous=old;
        if((value ? ::setenv(key,value,1) : ::unsetenv(key))!=0)throw std::runtime_error("Could not isolate test environment");
    }
    ~EnvironmentValue(){if(previous)(void)::setenv(name.c_str(),previous->c_str(),1);else (void)::unsetenv(name.c_str());}
};
struct TemporaryWorld {
    std::filesystem::path path;
    TemporaryWorld() {
        std::string pattern="/tmp/voxys-adventure-runtime-XXXXXX";
        const char* created=::mkdtemp(pattern.data());if(!created)throw std::runtime_error("Could not create isolated runtime test folder");
        path=created;
    }
    ~TemporaryWorld(){std::error_code error;std::filesystem::remove_all(path,error);}
};
struct GpuSignals {
    std::atomic<unsigned> errors=0;
    std::atomic<int> queue=0;
    std::mutex mutex;
    std::string message;
};
glm::dvec3 position(PlayerPose p){return {p.x,p.y,p.z};}
struct RuntimeMenuRow {
    std::string json;
    int intent=0;
    bool enabled=false;
};
// Inspect the published menu contract, rather than depending on private row
// indices. These fixed labels contain no JSON escapes or nested objects.
std::optional<RuntimeMenuRow> menuRow(std::string_view json,std::string_view label) {
    const auto rows=json.find("\"rows\":[");
    const auto end=json.find("],\"mode\":",rows);
    const auto begin=json.find("{\"label\":\""+std::string(label)+"\"",rows);
    if(rows==std::string_view::npos||end==std::string_view::npos||begin>=end)return std::nullopt;
    const auto close=json.find('}',begin);
    if(close==std::string_view::npos||close>=end)return std::nullopt;
    RuntimeMenuRow row{std::string(json.substr(begin,close-begin+1)),0,false};
    const auto key=row.json.find("\"intent\":");
    if(key==std::string::npos)return std::nullopt;
    const char* first=row.json.data()+key+9;
    const auto parsed=std::from_chars(first,row.json.data()+row.json.size(),row.intent);
    if(parsed.ec!=std::errc{}||parsed.ptr==row.json.data()+row.json.size()||*parsed.ptr!=',')return std::nullopt;
    row.enabled=row.json.find("\"enabled\":true")!=std::string::npos;
    return row;
}
}

// This drives library Input events and the real runtime, with no Window or
// platform event attachment. It is component integration, not evidence of
// ordinary desktop controls, controller hardware or a rendered playable fight.
TEST(AdventureRuntimeIntegration, FullTerrainStartupIdleInputAndSnapshotRestoreRemainAuthoritative) {
#if !defined(VOXY_NATIVE)
    GTEST_SKIP()<<"This headless component integration target requires the native runtime.";
#else
    const char* raw=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!raw||!*raw)GTEST_SKIP()<<"Opt in with VOXY_ADVENTURE_TEST_TERRAIN pointing to the installed full raw terrain.";
    std::vector<uint16_t> samples(8192*8192);std::ifstream terrainFile(raw,std::ios::binary);
    ASSERT_TRUE(terrainFile.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(terrainFile.peek(),std::char_traits<char>::eof());
    static_assert(std::endian::native==std::endian::little,"The native installed runtime requires little endian samples");
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    // Runtime initialization itself checks the full installed SHA; no second
    // test-only decoder, altered heightmap or collision proxy is used here.
    std::filesystem::path resources=std::filesystem::current_path();
    if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)resources=workspace;
    if(const char* workspace=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");workspace&&*workspace)resources=workspace;
    resources=std::filesystem::absolute(resources);
    ASSERT_TRUE(std::filesystem::is_regular_file(resources/"shaders/mesh_path.wgsl"));
    ASSERT_TRUE(std::filesystem::is_regular_file(resources/"data/adventure/raider-r01/manifest.json"));
    TemporaryWorld folder;
    EnvironmentValue saveRoot("VOXY_ADVENTURE_ROOT",folder.path.c_str());
    EnvironmentValue preferenceOverride("VOXY_ADVENTURE_PREFERENCES",nullptr);
    EnvironmentValue newWorld("VOXY_ADVENTURE_NEW","1");
    EnvironmentValue selectedWorld("VOXY_ADVENTURE_WORLD",nullptr);
    EnvironmentValue observation("VOXY_ADVENTURE_OBSERVE",nullptr);
    EnvironmentValue assetRoot("BUILD_WORKSPACE_DIRECTORY",resources.c_str());
    const auto signals=std::make_shared<GpuSignals>();
    gpu::Context context;
    if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter is available for the runtime startup check.";
    EXPECT_FALSE(context.hasSurface());
    context.setErrorCallback([signals](WGPUErrorType,const char* message){
        ++signals->errors;std::lock_guard guard(signals->mutex);signals->message=message?message:"Unspecified GPU error";
    });
    RecordProperty("adapter",context.getAdapterInfo().description);
    RecordProperty("scope","headless runtime component integration; synthetic library input; no UI or screenshots");
    {
        AdventureRuntime runtime;std::string error;
        ASSERT_TRUE(runtime.initialize(surface,context.getDevice(),context.getQueue(),resources/"shaders",WGPUTextureFormat_RGBA8Unorm,error))<<error;
        const auto initial=runtime.state();ASSERT_EQ(initial.health,100);ASSERT_EQ(initial.combat.tick,0u);
        EXPECT_EQ(initial.player,runtime.content().town);
        ASSERT_TRUE(runtime.content().enableTrailProgress);
        ASSERT_EQ(runtime.content().resourceNodes.size(),21u);
        EXPECT_EQ(runtime.content().discoveries[0].id,1);EXPECT_EQ(runtime.content().discoveries[1].id,2);
        // Startup must admit the actual landmarks alongside village, markers
        // and raiders. An isolated scenery unit test cannot prove this wiring.
        const auto startupObservation=runtime.json();
        for(unsigned site=1;site<=4;++site)EXPECT_NE(startupObservation.find("\"id\":"+std::to_string(site)+",\"available\":true,\"active\":false"),std::string::npos)<<startupObservation;
        Input input;input.onFocusChanged(true);input.onMouseMove(640,400);Camera camera;
        const auto frame=[&] {
            input.beginFrame();input.computeDeltas();
            runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});
            input.endFrame();
        };
        for(uint64_t tick=1;tick<=1201;++tick) {
            frame();ASSERT_EQ(runtime.state().combat.tick,tick)<<runtime.json();
            ASSERT_EQ(runtime.state().health,100);EXPECT_EQ(runtime.state().player,initial.player);
            if(tick%60==0)context.tick();
        }
        for(const auto& encounter:runtime.state().combat.encounters) {
            EXPECT_TRUE(encounter.checkpoint.positioned);EXPECT_EQ(encounter.checkpoint.phase,EnemyPhase::Idle);
            EXPECT_EQ(encounter.checkpoint.phaseTicks,kMaximumCombatDeadlineTicks);
        }
        // South from town leaves the sign and village houses behind. This is a
        // buffered Input key held across updates, not a pose or action setter.
        const auto beforeWalk=runtime.state().player;input.onKeyDown(static_cast<int>(Key::S));
        for(uint64_t frameIndex=1;frameIndex<=90;++frameIndex) {
            frame();ASSERT_EQ(runtime.state().combat.tick,1201+frameIndex)<<runtime.json();
            ASSERT_EQ(runtime.state().health,100);
        }
        input.onKeyUp(static_cast<int>(Key::S));frame();
        ASSERT_GT(glm::length(position(runtime.state().player)-position(beforeWalk)),2.);
        const auto saved=runtime.state();std::vector<std::byte> bytes;
        ASSERT_TRUE(runtime.snapshot(bytes,error))<<error;ASSERT_FALSE(bytes.empty());
        AdventureState decoded;AdventureSaveLoadMetadata metadata;
        ASSERT_TRUE(AdventureSaveCodec::decode(bytes,saved.world,runtime.content(),decoded,error,&metadata))<<error;
        EXPECT_EQ(metadata,(AdventureSaveLoadMetadata{kAdventureSaveSchema,false}));EXPECT_EQ(decoded,saved);
        ASSERT_TRUE(runtime.restore(bytes,saved.world,error))<<error;EXPECT_EQ(runtime.state(),saved);
        std::vector<std::byte> after;ASSERT_TRUE(runtime.snapshot(after,error))<<error;EXPECT_EQ(after,bytes);
        frame();EXPECT_EQ(runtime.state().combat.tick,saved.combat.tick+1);EXPECT_EQ(runtime.state().health,saved.health);
        EXPECT_EQ(runtime.state().backpack,saved.backpack);EXPECT_EQ(runtime.state().structures,saved.structures);
        EXPECT_EQ(runtime.state().components,saved.components);EXPECT_EQ(runtime.state().equippedTool,saved.equippedTool);
        RecordProperty("acceptedIdleTicks",1201);RecordProperty("syntheticWalkingFrames",90);
        RecordProperty("snapshotBytes",static_cast<int>(bytes.size()));
        RecordProperty("fullTerrainSha256",std::string(installedWorld().samplesSha256));
        // Exercise the same public menu intents consumed by browser and native
        // HUDs. No recipe selector, stock or runtime state setter is used.
        const auto locked=runtime.state();std::vector<std::byte> lockedBytes;
        ASSERT_TRUE(runtime.snapshot(lockedBytes,error))<<error;
        runtime.action(13,1);frame(); // Structure catalog.
        auto row=menuRow(runtime.json(),"Wide stone step");ASSERT_TRUE(row)<<runtime.json();
        EXPECT_FALSE(row->enabled);EXPECT_EQ(row->intent,0);
        runtime.action(10,row->intent);frame();
        EXPECT_NE(runtime.json().find("\"blueprintKind\":0,\"piece\":"),std::string::npos);
        EXPECT_FALSE(wideStoneStepRecipeUnlocked(runtime.state()));
        EXPECT_EQ(runtime.state().backpack,locked.backpack);EXPECT_EQ(runtime.state().structures,locked.structures);

        // Component-only prior-receipt fixture, not an invented played journey.
        // Permanent first-home receipts remain valid after a home is removed.
        // Add only coherent receipts; preserve the actual terrain world, player,
        // inventory and live combat checkpoints from this initialized runtime.
        auto survey=locked;
        ASSERT_GE(survey.revision,4u);
        survey.firstHome={QuestPhase::Completed,1};survey.metNpcMask|=1;
        survey.trail.discoveries[0]={2,0};survey.trail.discoveries[1]={3,0};
        survey.trail.quests[3]={QuestPhase::Completed,4};
        ASSERT_TRUE(AdventureSession::validate(survey,runtime.content(),error))<<error;
        std::vector<std::byte> surveyBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(survey,runtime.content(),surveyBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(surveyBytes,survey.world,error))<<error;
        EXPECT_EQ(runtime.state(),survey);EXPECT_EQ(runtime.state().combat,locked.combat);
        EXPECT_EQ(runtime.state().structures,locked.structures);EXPECT_EQ(runtime.state().components,locked.components);
        EXPECT_EQ(runtime.state().backpack,locked.backpack);

        runtime.action(16);frame();
        row=menuRow(runtime.json(),"The Surveyor's Notes");ASSERT_TRUE(row)<<runtime.json();
        ASSERT_TRUE(row->enabled);ASSERT_GT(row->intent,0);
        EXPECT_NE(row->json.find("Optional / Completed"),std::string::npos);
        runtime.action(10,row->intent);frame();
        EXPECT_NE(runtime.json().find("\"menuTitle\":\"The Surveyor's Notes\""),std::string::npos)<<runtime.json();
        runtime.action(13,1);frame();
        row=menuRow(runtime.json(),"Wide stone step");ASSERT_TRUE(row)<<runtime.json();
        ASSERT_TRUE(row->enabled);ASSERT_GT(row->intent,0);
        EXPECT_NE(row->json.find("\"pieceKind\":14,\"blueprintKind\":2"),std::string::npos);
        EXPECT_NE(row->json.find("\"detail\":\"3 Piers / 6 stone\""),std::string::npos);
        const int unlockedIntent=row->intent;
        runtime.action(10,unlockedIntent);frame();
        EXPECT_NE(runtime.json().find("\"blueprintKind\":2,\"piece\":14"),std::string::npos)<<runtime.json();
        EXPECT_NE(runtime.json().find("\"mode\":\"build\""),std::string::npos);
        EXPECT_NE(runtime.json().find("\"costText\":\"6 stone\""),std::string::npos);
        runtime.action(3);frame();
        EXPECT_NE(runtime.json().find("\"blueprintKind\":2,\"piece\":14"),std::string::npos);
        EXPECT_NE(runtime.json().find("\"kind\":14,\"yaw\":1}"),std::string::npos)<<runtime.json();
        EXPECT_EQ(runtime.state().backpack,locked.backpack);EXPECT_EQ(runtime.state().structures,locked.structures);

        // Inject a standard library gamepad sample after platform polling.
        // Input itself is nonconst; this accesses its actual GamepadInput, not a
        // production test hook or an assertion about physical controller input.
        input.beginFrame();input.computeDeltas();
        auto& pad=const_cast<GamepadInput&>(input.gamepad());
        GamepadSample neutral;neutral.connected=true;neutral.device=7;
        pad.update(neutral,true,0.);
        auto nextPiece=neutral;nextPiece.buttons[static_cast<size_t>(PadButton::RightShoulder)]=true;
        pad.update(nextPiece,true,AdventurePlayer::fixedStep);
        ASSERT_TRUE(pad.pressed(PadButton::RightShoulder));
        runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
        EXPECT_NE(runtime.json().find("\"blueprintKind\":0,\"piece\":15"),std::string::npos)<<runtime.json();

        // Restore must clear both a selected recipe and a queued choice from
        // the previous checkpoint. No old entitlement survives this boundary.
        runtime.action(13,1);frame();
        row=menuRow(runtime.json(),"Wide stone step");ASSERT_TRUE(row);ASSERT_GT(row->intent,0);
        runtime.action(10,row->intent);frame();
        ASSERT_NE(runtime.json().find("\"blueprintKind\":2,\"piece\":14"),std::string::npos);
        runtime.action(13,1);frame();
        row=menuRow(runtime.json(),"Wide stone step");ASSERT_TRUE(row);ASSERT_GT(row->intent,0);
        const int queuedRecipeIntent=row->intent;
        runtime.action(10,queuedRecipeIntent);
        ASSERT_TRUE(runtime.restore(lockedBytes,locked.world,error))<<error;
        EXPECT_EQ(runtime.state(),locked);
        frame();
        EXPECT_NE(runtime.json().find("\"mode\":\"explore\""),std::string::npos)<<runtime.json();
        EXPECT_NE(runtime.json().find("\"blueprintKind\":0,\"piece\":1"),std::string::npos);
        // Previously enabled intents cannot be reinterpreted even when the
        // same catalog row is subsequently visible in the locked checkpoint.
        runtime.action(13,1);frame();
        row=menuRow(runtime.json(),"Wide stone step");ASSERT_TRUE(row)<<runtime.json();
        EXPECT_FALSE(row->enabled);EXPECT_EQ(row->intent,0);
        runtime.action(10,queuedRecipeIntent);frame();
        runtime.action(10,unlockedIntent);frame();
        runtime.action(10,2);frame(); // An unscoped row number is not an intent.
        EXPECT_FALSE(wideStoneStepRecipeUnlocked(runtime.state()));
        EXPECT_NE(runtime.json().find("\"blueprintKind\":0,\"piece\":1"),std::string::npos)<<runtime.json();
        EXPECT_EQ(runtime.state().backpack,locked.backpack);EXPECT_EQ(runtime.state().structures,locked.structures);
        EXPECT_EQ(runtime.state().components,locked.components);
        RecordProperty("surveyMenuFixture","component-only canonical checkpoint; coherent prior receipts; no played quest claim");
        RecordProperty("syntheticRecipeCycle","standard GamepadInput RightShoulder sample; no controller hardware");

        // Settings share the actual published menu contract, and persist in a
        // separate isolated sidecar. Freeze the world only after entering Pause.
        runtime.action(22);frame();runtime.action(9);frame();
        ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"pause");
        const auto preferenceWorld=runtime.state();std::vector<std::byte> preferenceWorldBytes,checkedBytes;
        ASSERT_TRUE(runtime.snapshot(preferenceWorldBytes,error))<<error;
        for(const auto label:{"Comfort and controls","Text size: 100%","Attack and dodge controls",
            "Attack","Keyboard: Unbound","C"}) {
            SCOPED_TRACE(label);
            row=menuRow(runtime.json(),label);ASSERT_TRUE(row)<<runtime.json();
            ASSERT_TRUE(row->enabled);ASSERT_GT(row->intent,0);
            runtime.action(10,row->intent);frame();
            EXPECT_EQ(runtime.state(),preferenceWorld);
            ASSERT_TRUE(runtime.snapshot(checkedBytes,error))<<error;EXPECT_EQ(checkedBytes,preferenceWorldBytes);
        }
        const auto changedSettings=runtime.preferencesAction(1,{});
        AdventurePreferences activeSettings;
        ASSERT_TRUE(parseAdventurePreferences(changedSettings,activeSettings,error))<<error;
        EXPECT_DOUBLE_EQ(activeSettings.textScale,1.25);EXPECT_EQ(activeSettings.combat[0].key,static_cast<int>(Key::C));
        ASSERT_TRUE(menuRow(runtime.json(),"Keyboard: C"));
        const auto settingsObservation=nlohmann::json::parse(runtime.json());
        EXPECT_EQ(settingsObservation.at("textScale"),1.25);
        EXPECT_EQ(settingsObservation.at("attackControl"),combatBindingLabel(activeSettings,CombatAction::Attack,false));
        EXPECT_EQ(settingsObservation.at("dodgeControl"),combatBindingLabel(activeSettings,CombatAction::Dodge,false));
        EXPECT_FALSE(settingsObservation.at("lookControl").get<std::string>().empty());
        const auto sidecar=folder.path/"adventure-preferences-v1.json";
        ASSERT_TRUE(std::filesystem::is_regular_file(sidecar));
        ASSERT_LE(std::filesystem::file_size(sidecar),kMaximumAdventurePreferencesBytes);
        std::ifstream preferencesFile(sidecar,std::ios::binary);
        const std::string storedSettings((std::istreambuf_iterator<char>(preferencesFile)),{});
        AdventurePreferences independentlyLoaded;
        ASSERT_TRUE(parseAdventurePreferences(storedSettings,independentlyLoaded,error))<<error;
        EXPECT_EQ(independentlyLoaded,activeSettings);EXPECT_EQ(storedSettings,changedSettings);

        // Applying malformed/conflicting bytes cannot partially change either
        // controls or world ownership. Storage status is not a settings revision.
        const auto settingsRevision=settingsObservation.at("preferencesRevision").get<std::string>();
        auto conflict=nlohmann::json::parse(changedSettings);
        conflict["combat"][1]["key"]=static_cast<int>(Key::C);
        for(const auto& rejected:std::array<std::string,2>{{"{bad settings",conflict.dump()}}) {
            EXPECT_NE(runtime.preferencesAction(2,rejected),"ok");
            EXPECT_EQ(runtime.preferencesAction(1,{}),changedSettings);
            EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("preferencesRevision"),settingsRevision);
            EXPECT_EQ(runtime.state(),preferenceWorld);
        }
        std::ifstream unchangedSidecar(sidecar,std::ios::binary);
        EXPECT_EQ(std::string((std::istreambuf_iterator<char>(unchangedSidecar)),{}),storedSettings);
        ASSERT_EQ(runtime.preferencesAction(4,{}),"ok");
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("preferencesStatus"),"Settings saved on this device.");
        ASSERT_EQ(runtime.preferencesAction(5,{}),"ok");
        EXPECT_NE(nlohmann::json::parse(runtime.json()).at("preferencesStatus").get<std::string>().find("this visit"),std::string::npos);
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("preferencesRevision"),settingsRevision);
        for(unsigned reset=1;reset<=2;++reset) {
            ASSERT_EQ(runtime.preferencesAction(3,{}),"ok");frame();
            EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("preferencesRevision"),std::to_string(std::stoull(settingsRevision)+reset));
            AdventurePreferences resetSettings;ASSERT_TRUE(parseAdventurePreferences(runtime.preferencesAction(1,{}),resetSettings,error));
            EXPECT_EQ(resetSettings,AdventurePreferences{});EXPECT_EQ(runtime.state(),preferenceWorld);
            ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,preferenceWorldBytes);
        }
        ASSERT_TRUE(menuRow(runtime.json(),"Keyboard: Unbound"));
        for(const auto label:{"Back","Back","Back","Return to adventure"}) {
            row=menuRow(runtime.json(),label);ASSERT_TRUE(row)<<runtime.json();ASSERT_TRUE(row->enabled);
            runtime.action(10,row->intent);frame();
        }
        ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"explore");

        // Real library mouse events, including a complete drag between display
        // frames. Compare yaw direction with wrapping, not a guessed sensitivity
        // magnitude. No window, pointer injection hook or fight is involved.
        const auto lookPlayer=runtime.state().player;
        for(const bool inverted:{false,true}) {
            SCOPED_TRACE(inverted);
            auto settings=nlohmann::json::parse(runtime.preferencesAction(1,{}));settings["invertX"]=inverted;
            ASSERT_EQ(runtime.preferencesAction(2,settings.dump()),"ok");
            input.onMouseUp(static_cast<int>(MouseButton::Right));input.onMouseMove(640,400);frame();
            const double beforeMove=nlohmann::json::parse(runtime.json()).at("camera").at("yaw").get<double>();
            input.onMouseMove(660,400);frame();
            EXPECT_DOUBLE_EQ(nlohmann::json::parse(runtime.json()).at("camera").at("yaw").get<double>(),beforeMove);
            input.onMouseDown(static_cast<int>(MouseButton::Right));input.onMouseMove(672,400);frame();
            const double afterHeld=nlohmann::json::parse(runtime.json()).at("camera").at("yaw").get<double>();
            const double heldDelta=std::remainder(afterHeld-beforeMove,2*std::numbers::pi);
            EXPECT_GT(heldDelta*(inverted?-1.:1.),0.);
            input.onMouseUp(static_cast<int>(MouseButton::Right));frame();
            EXPECT_DOUBLE_EQ(nlohmann::json::parse(runtime.json()).at("camera").at("yaw").get<double>(),afterHeld);
            input.onMouseMove(692,400);input.onMouseDown(static_cast<int>(MouseButton::Right));
            input.onMouseMove(704,400);input.onMouseUp(static_cast<int>(MouseButton::Right));frame();
            const double afterCompleted=nlohmann::json::parse(runtime.json()).at("camera").at("yaw").get<double>();
            EXPECT_GT(std::remainder(afterCompleted-afterHeld,2*std::numbers::pi)*(inverted?-1.:1.),0.);
            EXPECT_FALSE(input.isMouseButtonDown(MouseButton::Right));EXPECT_EQ(runtime.state().player,lookPlayer);
        }
        RecordProperty("preferenceMenuChoices",6);
        RecordProperty("preferencesWorldInvariant","full AdventureState and archive bytes unchanged while paused");
        RecordProperty("mouseLookGestures","four library RMB drags, held and completed, normal and inverted yaw; unheld movement ignored");

        // Guide navigation uses the published choices, freezes the whole world
        // and never records tutorial completion or changes the world archive.
        AdventurePreferences guideSettings;
        ASSERT_TRUE(parseAdventurePreferences(runtime.preferencesAction(1,{}),guideSettings,error))<<error;
        runtime.action(29,0);frame();
        ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"guide");
        ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("menuTitle"),"How to play");
        const auto guideWorld=runtime.state();std::vector<std::byte> guideBytes;
        ASSERT_TRUE(runtime.snapshot(guideBytes,error))<<error;
        const auto guideUnchanged=[&] {
            EXPECT_EQ(runtime.state(),guideWorld);
            ASSERT_TRUE(runtime.snapshot(checkedBytes,error))<<error;EXPECT_EQ(checkedBytes,guideBytes);
        };
        for(unsigned topic=0;topic<static_cast<unsigned>(AdventureGuideTopic::Count);++topic) {
            const auto card=adventureGuideCard(static_cast<AdventureGuideTopic>(topic),guideSettings,false);
            SCOPED_TRACE(card.title);
            row=menuRow(runtime.json(),card.title);ASSERT_TRUE(row)<<runtime.json();ASSERT_TRUE(row->enabled);
            runtime.action(10,row->intent);frame();
            const auto cardObservation=nlohmann::json::parse(runtime.json());
            EXPECT_EQ(cardObservation.at("menuTitle"),card.title);EXPECT_EQ(cardObservation.at("menuText"),card.text);
            EXPECT_EQ(menuRow(runtime.json(),"Previous tip").has_value(),topic>0);
            EXPECT_EQ(menuRow(runtime.json(),"Next tip").has_value(),topic+1<static_cast<unsigned>(AdventureGuideTopic::Count));
            ASSERT_TRUE(menuRow(runtime.json(),"Return to adventure"));guideUnchanged();
            row=menuRow(runtime.json(),"All topics");ASSERT_TRUE(row);runtime.action(10,row->intent);frame();guideUnchanged();
        }
        row=menuRow(runtime.json(),adventureGuideCard(AdventureGuideTopic::Movement,guideSettings,false).title);ASSERT_TRUE(row);
        runtime.action(10,row->intent);frame();
        row=menuRow(runtime.json(),"Next tip");ASSERT_TRUE(row);const int oldNextGuideIntent=row->intent;
        runtime.action(10,oldNextGuideIntent);runtime.action(10,oldNextGuideIntent);frame();
        const auto buildingCard=adventureGuideCard(AdventureGuideTopic::Building,guideSettings,false);
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("menuTitle"),buildingCard.title);guideUnchanged();
        runtime.action(10,oldNextGuideIntent);frame();
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("menuTitle"),buildingCard.title);guideUnchanged();
        row=menuRow(runtime.json(),"Previous tip");ASSERT_TRUE(row);runtime.action(10,row->intent);frame();guideUnchanged();

        // A standard neutral-to-pressed gamepad sample changes only the guide's
        // device wording. This is not a physical-controller acceptance claim.
        input.beginFrame();input.computeDeltas();pad.update(neutral,true,0.);
        auto guidePad=neutral;guidePad.buttons[static_cast<size_t>(PadButton::RightStick)]=true;
        pad.update(guidePad,true,AdventurePlayer::fixedStep);ASSERT_TRUE(pad.pressed(PadButton::RightStick));
        runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("menuText"),adventureGuideCard(AdventureGuideTopic::Movement,guideSettings,true).text);
        guideUnchanged();
        input.onKeyDown(static_cast<int>(Key::G));frame();input.onKeyUp(static_cast<int>(Key::G));frame();
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("menuText"),adventureGuideCard(AdventureGuideTopic::Movement,guideSettings,false).text);
        guideUnchanged();runtime.action(20);frame();
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"explore");guideUnchanged();

        // A guide opened from Pause returns to that same menu. Neither the
        // encoded menu entry nor the browser's topic-picker entry edits a save.
        runtime.action(9);frame();ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"pause");
        const auto guidePauseWorld=runtime.state();std::vector<std::byte> guidePauseBytes;
        ASSERT_TRUE(runtime.snapshot(guidePauseBytes,error));
        for(const bool menuEntry:{true,false}) {
            if(menuEntry){row=menuRow(runtime.json(),"How to play");ASSERT_TRUE(row);runtime.action(10,row->intent);}
            else runtime.action(29,0);
            frame();ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"guide");
            ASSERT_TRUE(menuRow(runtime.json(),"Back to menu"));EXPECT_EQ(runtime.state(),guidePauseWorld);
            runtime.action(20);frame();EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"pause");
            EXPECT_EQ(runtime.state(),guidePauseWorld);
            ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,guidePauseBytes);
        }
        runtime.action(20);frame();runtime.action(14);frame();runtime.action(3);frame();
        const auto buildBeforeGuide=nlohmann::json::parse(runtime.json());
        ASSERT_EQ(buildBeforeGuide.at("mode"),"build");ASSERT_EQ(buildBeforeGuide.at("blueprintKind"),1);
        ASSERT_EQ(buildBeforeGuide.at("piece"),0);
        const auto buildWorldBeforeGuide=runtime.state();
        runtime.action(29,1);frame();ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("menuTitle"),buildingCard.title);
        ASSERT_TRUE(menuRow(runtime.json(),"Return to building"));
        const auto buildGuideWorld=runtime.state();std::vector<std::byte> buildGuideBytes;
        ASSERT_TRUE(runtime.snapshot(buildGuideBytes,error));
        row=menuRow(runtime.json(),"Next tip");ASSERT_TRUE(row);runtime.action(10,row->intent);frame();
        EXPECT_EQ(runtime.state(),buildGuideWorld);
        ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,buildGuideBytes);
        runtime.action(20);frame();const auto buildAfterGuide=nlohmann::json::parse(runtime.json());
        EXPECT_EQ(buildAfterGuide.at("mode"),"build");
        for(const auto key:{"selected","piece","blueprintKind"})EXPECT_EQ(buildAfterGuide.at(key),buildBeforeGuide.at(key));
        EXPECT_EQ(buildAfterGuide.at("preview").at("yaw"),buildBeforeGuide.at("preview").at("yaw"));
        // Entry can advance an ordinary simulation tick; ownership and the
        // selected paid blueprint must survive without placing any geometry.
        EXPECT_EQ(runtime.state().structures,buildWorldBeforeGuide.structures);
        EXPECT_EQ(runtime.state().components,buildWorldBeforeGuide.components);
        EXPECT_EQ(runtime.state().backpack,buildWorldBeforeGuide.backpack);
        EXPECT_EQ(runtime.state().backpackRevision,buildWorldBeforeGuide.backpackRevision);
        EXPECT_EQ(runtime.state().lastIssuedId,buildWorldBeforeGuide.lastIssuedId);
        EXPECT_EQ(runtime.state().equippedTool,buildWorldBeforeGuide.equippedTool);
        EXPECT_EQ(runtime.state().equippedUtility,buildWorldBeforeGuide.equippedUtility);
        EXPECT_EQ(runtime.state().firstHome,buildWorldBeforeGuide.firstHome);EXPECT_EQ(runtime.state().trail,buildWorldBeforeGuide.trail);

        // Dismissing help consumes this whole input boundary. A raw shoulder
        // edge can clear a blueprint even if the simultaneous click cannot
        // place anything; this is not evidence of a successfully placed room.
        runtime.action(29,1);frame();
        const auto dismissalBefore=nlohmann::json::parse(runtime.json());
        ASSERT_EQ(dismissalBefore.at("mode"),"guide");ASSERT_EQ(dismissalBefore.at("blueprintKind"),1);
        const auto dismissalWorld=runtime.state();std::vector<std::byte> dismissalBytes;
        ASSERT_TRUE(runtime.snapshot(dismissalBytes,error));
        // (4,4) is outside the guide sheet's 12 px minimum viewport margin.
        input.onMouseMove(4,4);input.onMouseDown(static_cast<int>(MouseButton::Left));
        input.onKeyDown(static_cast<int>(Key::Escape));input.beginFrame();input.computeDeltas();
        pad.update(neutral,true,0.);
        auto dismissalPad=neutral;dismissalPad.buttons[static_cast<size_t>(PadButton::RightShoulder)]=true;
        pad.update(dismissalPad,true,AdventurePlayer::fixedStep);
        ASSERT_TRUE(input.wasKeyPressed(Key::Escape));ASSERT_TRUE(input.wasMouseButtonPressed(MouseButton::Left));
        ASSERT_TRUE(pad.pressed(PadButton::RightShoulder));
        runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
        const auto dismissalAfter=nlohmann::json::parse(runtime.json());EXPECT_EQ(dismissalAfter.at("mode"),"build");
        for(const auto key:{"selected","piece","blueprintKind"})EXPECT_EQ(dismissalAfter.at(key),dismissalBefore.at(key));
        EXPECT_EQ(dismissalAfter.at("preview").at("yaw"),dismissalBefore.at("preview").at("yaw"));
        EXPECT_EQ(runtime.state(),dismissalWorld);
        ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,dismissalBytes);
        input.onKeyUp(static_cast<int>(Key::Escape));input.onMouseUp(static_cast<int>(MouseButton::Left));
        input.beginFrame();input.computeDeltas();pad.update(neutral,true,AdventurePlayer::fixedStep*2);
        runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
        const auto dismissalNeutral=nlohmann::json::parse(runtime.json());EXPECT_EQ(dismissalNeutral.at("mode"),"build");
        for(const auto key:{"selected","piece","blueprintKind"})EXPECT_EQ(dismissalNeutral.at(key),dismissalBefore.at(key));
        EXPECT_EQ(dismissalNeutral.at("preview").at("yaw"),dismissalBefore.at("preview").at("yaw"));
        EXPECT_EQ(runtime.state().structures,dismissalWorld.structures);EXPECT_EQ(runtime.state().components,dismissalWorld.components);
        EXPECT_EQ(runtime.state().backpack,dismissalWorld.backpack);EXPECT_EQ(runtime.state().backpackRevision,dismissalWorld.backpackRevision);
        EXPECT_EQ(runtime.state().lastIssuedId,dismissalWorld.lastIssuedId);
        RecordProperty("guideDismissalBoundary","same library-input frame: Escape, fresh RightShoulder and outside-sheet left click; no placement claim");

        runtime.action(29,1);frame();row=menuRow(runtime.json(),"Next tip");ASSERT_TRUE(row);
        const int queuedGuideIntent=row->intent;runtime.action(10,queuedGuideIntent);
        ASSERT_TRUE(runtime.restore(guideBytes,guideWorld.world,error))<<error;EXPECT_EQ(runtime.state(),guideWorld);
        ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,guideBytes);
        frame();EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"explore");
        runtime.action(10,queuedGuideIntent);frame();
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("mode"),"explore");
        EXPECT_EQ(runtime.state().backpack,guideWorld.backpack);EXPECT_EQ(runtime.state().structures,guideWorld.structures);
        RecordProperty("guideIntegration","six authoritative cards; paused full state/archive equality; stale/duplicate intents; entry-mode and paid preview preservation; restore clears queued guide choices");
        RecordProperty("guideDeviceText","synthetic neutral/RightStick GamepadInput and keyboard G sample; no controller hardware");
        // Build an isolated paid door fixture through the real session and
        // construction policy on the installed terrain. This is component
        // integration, not an ordinary mouse-built home or a rendered journey.
        auto doorSession=AdventureSession::restore(runtime.state(),runtime.content(),error);ASSERT_TRUE(doorSession)<<error;
        const auto site=installedWorld().homeSuggestion;
        const auto anchorHeight=terrainPlacementHeight(PieceKind::Foundation,0,site,surface);ASSERT_TRUE(anchorHeight);
        // Stay within handle reach in both the closed and open poses.
        const glm::dvec2 doorApproach=site+glm::dvec2(0,-1.5);
        const PlayerPose doorPlayer{doorApproach.x,terrain::lego::supportHeight(surface,glm::vec2(doorApproach),.3f),doorApproach.y,0};
        ASSERT_TRUE(doorSession->updatePlayer(doorPlayer,doorSession->state().health,error))<<error;
        AdventureSpatialQueries doorGeometry;ASSERT_TRUE(doorGeometry.bindTerrain(surface));ASSERT_TRUE(doorGeometry.publish({},1));
        const auto doorValidator=[&](const AdventureState& before,const AdventureState& candidate,std::string& reason) {
            return validateConstruction(before,candidate,doorGeometry,reason)
                &&validateInteractions(before,candidate,runtime.content(),doorGeometry,reason);
        };
        const auto doorStamp=[&]{const auto& s=doorSession->state();return CommandStamp{s.revision,s.lastRequestSequence+1,1};};
        const GridPosition doorFoundation{int32_t(std::round(site.x*50)),int32_t(std::round(*anchorHeight*50)),int32_t(std::round(site.y*50))};
        const auto doorSupplies=doorSession->state().backpack;
        auto foundationChange=doorSession->preparePlace(doorStamp(),{0,PieceKind::Foundation,doorFoundation,0,0},doorValidator,error);
        ASSERT_TRUE(foundationChange)<<error;const auto doorStructure=foundationChange->changedStructure();
        ASSERT_TRUE(doorSession->commit(std::move(*foundationChange),error))<<error;
        std::vector<AdventureSpatialQueries::Solid> doorSolids;
        ASSERT_TRUE(compileSolids(doorSession->state(),doorSolids,error));ASSERT_TRUE(doorGeometry.publish(doorSolids,doorSession->state().revision+1));
        auto doorPosition=doorFoundation;doorPosition.y+=16;
        auto doorChange=doorSession->preparePlace(doorStamp(),{doorStructure,PieceKind::HingedDoor,doorPosition,0,0},doorValidator,error);
        ASSERT_TRUE(doorChange)<<error;const auto doorPart=doorChange->changedPart();
        ASSERT_TRUE(doorSession->commit(std::move(*doorChange),error))<<error;
        uint64_t doorId=0;
        for(const auto& c:doorSession->state().components)if(c.part==doorPart)doorId=c.id;
        ASSERT_NE(doorId,0u);
        EXPECT_EQ(itemCount(doorSession->state().backpack,ItemKind::Wood),itemCount(doorSupplies,ItemKind::Wood)-6);
        EXPECT_EQ(itemCount(doorSession->state().backpack,ItemKind::Stone),itemCount(doorSupplies,ItemKind::Stone)-4);
        EXPECT_EQ(itemCount(doorSession->state().backpack,ItemKind::Scrap),itemCount(doorSupplies,ItemKind::Scrap)-2);
        std::vector<std::byte> closedDoorBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(doorSession->state(),runtime.content(),closedDoorBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(closedDoorBytes,doorSession->state().world,error))<<error;
        frame();ASSERT_EQ(nlohmann::json::parse(runtime.json()).at("interaction"),"Open door");
        const auto paidDoorBackpack=runtime.state().backpack;
        runtime.action(7);runtime.action(7);frame();
        ASSERT_TRUE(AdventureSession::findComponent(runtime.state(),doorId)->doorOpen)<<runtime.json();
        EXPECT_EQ(runtime.state().backpack,paidDoorBackpack);
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("interaction"),"Close door");
        std::vector<std::byte> openDoorBytes;ASSERT_TRUE(runtime.snapshot(openDoorBytes,error));
        const auto openDoorState=runtime.state();
        ASSERT_TRUE(runtime.restore(openDoorBytes,openDoorState.world,error))<<error;
        EXPECT_EQ(runtime.state(),openDoorState);
        ASSERT_TRUE(runtime.snapshot(checkedBytes,error));EXPECT_EQ(checkedBytes,openDoorBytes);
        frame();runtime.action(7);frame();
        EXPECT_FALSE(AdventureSession::findComponent(runtime.state(),doorId)->doorOpen)<<runtime.json();
        EXPECT_EQ(runtime.state().backpack,paidDoorBackpack);
        EXPECT_EQ(nlohmann::json::parse(runtime.json()).at("interaction"),"Open door");
        RecordProperty("doorIntegration","paid foundation+door session fixture on full terrain; actual runtime Use; duplicate queued Open remains Open; exact open-state archive restore; Close preserves supplies; no ordinary placement claim");
        // Retire startup uploads while the actual runtime still owns its GPU
        // assets. No presentation surface or screenshot readback is created.
        wgpuQueueSubmit(context.getQueue(),0,nullptr);
        auto* callback=new std::shared_ptr<GpuSignals>(signals);
        wgpuQueueOnSubmittedWorkDone(context.getQueue(),[](WGPUQueueWorkDoneStatus status,void* data){
            std::unique_ptr<std::shared_ptr<GpuSignals>> owned(static_cast<std::shared_ptr<GpuSignals>*>(data));
            (*owned)->queue=status==WGPUQueueWorkDoneStatus_Success?1:2;
        },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(signals->queue.load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_EQ(signals->queue.load(),1);
    }
    context.tick();
    {std::lock_guard guard(signals->mutex);EXPECT_EQ(signals->errors.load(),0u)<<signals->message;}
    context.setErrorCallback({});
#endif
}
TEST(FreeBuildRuntimeIntegration, CreativeStartupPlacementAndExactRestore) {
#if !defined(VOXY_NATIVE)
    GTEST_SKIP()<<"Native headless runtime only.";
#else
    const char* raw=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!raw||!*raw)GTEST_SKIP()<<"Opt in with VOXY_ADVENTURE_TEST_TERRAIN pointing to the installed full raw terrain.";
    std::vector<uint16_t> samples(8192*8192);std::ifstream terrainFile(raw,std::ios::binary);
    ASSERT_TRUE(terrainFile.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(terrainFile.peek(),std::char_traits<char>::eof());
    static_assert(std::endian::native==std::endian::little,"The native installed runtime requires little endian samples");
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    // Runtime initialization itself checks the full installed SHA; no second
    // test-only decoder, altered heightmap or collision proxy is used here.
    std::filesystem::path resources=std::filesystem::current_path();
    if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)resources=workspace;
    if(const char* workspace=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");workspace&&*workspace)resources=workspace;
    resources=std::filesystem::absolute(resources);
    ASSERT_TRUE(std::filesystem::is_regular_file(resources/"shaders/mesh_path.wgsl"));
    ASSERT_TRUE(std::filesystem::is_regular_file(resources/"data/adventure/raider-r01/manifest.json"));
    TemporaryWorld folder;
    EnvironmentValue saveRoot("VOXY_ADVENTURE_ROOT",folder.path.c_str());
    EnvironmentValue preferenceOverride("VOXY_ADVENTURE_PREFERENCES",nullptr);
    EnvironmentValue newWorld("VOXY_ADVENTURE_NEW","1");
    EnvironmentValue selectedWorld("VOXY_ADVENTURE_WORLD",nullptr);
    EnvironmentValue observation("VOXY_ADVENTURE_OBSERVE",nullptr);
    EnvironmentValue assetRoot("BUILD_WORKSPACE_DIRECTORY",resources.c_str());
    const auto signals=std::make_shared<GpuSignals>();
    gpu::Context context;
    if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter is available for the runtime startup check.";
    EXPECT_FALSE(context.hasSurface());
    context.setErrorCallback([signals](WGPUErrorType,const char* message){
        ++signals->errors;std::lock_guard guard(signals->mutex);signals->message=message?message:"Unspecified GPU error";
    });
    RecordProperty("adapter",context.getAdapterInfo().description);
    RecordProperty("scope","headless runtime component integration; synthetic library input; no UI or screenshots");

    {
        AdventureRuntime runtime(true);std::string error;
        ASSERT_TRUE(runtime.initialize(surface,context.getDevice(),context.getQueue(),resources/"shaders",WGPUTextureFormat_RGBA8Unorm,error))<<error;
        ASSERT_TRUE(runtime.content().freeBuilding);EXPECT_TRUE(runtime.content().resourceNodes.empty());EXPECT_FALSE(runtime.content().enableTrailProgress);
        auto read=[&]{return nlohmann::json::parse(runtime.json());};
        auto startup=read();EXPECT_EQ(startup.at("mode"),"build");EXPECT_EQ(startup.at("piece"),10);EXPECT_EQ(startup.at("costText"),"Unlimited pieces");
        EXPECT_TRUE(startup.at("creative"));
        for(const auto& resident:startup.at("residents"))EXPECT_FALSE(resident.at("available"));
        for(const auto& site:startup.at("trailSites"))EXPECT_FALSE(site.at("available"));
        const auto empty=runtime.state().backpack;
        Input input;input.onFocusChanged(true);input.onMouseMove(640,400);Camera camera;
        const auto frame=[&]{input.beginFrame();input.computeDeltas();runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();};
        for(int i=0;i<10;++i)frame();
        EXPECT_EQ(runtime.state().combat.tick,0u);
        // Wheel selects a pictured piece without rotating or zooming the camera.
        input.onScroll(-1);frame();EXPECT_EQ(read().at("piece"),2);
        input.onScroll(1);frame();EXPECT_EQ(read().at("piece"),10);
        input.onKeyDown(int(Key::LeftControl));input.onScroll(-1);frame();EXPECT_EQ(read().at("piece"),10);
        input.onKeyUp(int(Key::LeftControl));frame();
        runtime.action(30,0x86a789);frame();EXPECT_EQ(read().at("paint"),0x86a789);
        runtime.action(30,-1);frame();EXPECT_EQ(read().at("paint"),0x86a789);
        runtime.action(30,0x1000000);frame();EXPECT_EQ(read().at("paint"),0x86a789);
        runtime.action(31);frame();EXPECT_EQ(read().at("mode"),"pause");
        runtime.action(20);frame();EXPECT_EQ(read().at("mode"),"build");
        bool aim=false;
        for(int y:{560,640,480,400}) {
            for(int x:{640,480,800,320,960}) {
                input.onMouseMove(float(x),float(y));frame();
                if(read().at("valid")==true){aim=true;break;}
            }
            if(aim)break;
        }
        ASSERT_TRUE(aim)<<runtime.json();
        runtime.action(3);frame(); // Quarter-turn retains a real placement preview.
        ASSERT_TRUE(read().at("valid"))<<runtime.json();
        runtime.action(4);frame();
        ASSERT_EQ(runtime.state().structures.size(),1u)<<runtime.json();
        ASSERT_EQ(runtime.state().structures[0].parts.size(),1u);
        EXPECT_EQ(runtime.state().structures[0].parts[0].kind,PieceKind::Brick2x4);
        EXPECT_EQ(runtime.state().structures[0].parts[0].yawQuarterTurns,1);
        EXPECT_EQ(runtime.state().structures[0].parts[0].paint,0x86a789u);
        EXPECT_TRUE(read().at("canUndo"));
        EXPECT_EQ(runtime.state().backpack,empty);
        const auto saved=runtime.state();std::vector<std::byte> bytes;
        ASSERT_TRUE(runtime.snapshot(bytes,error))<<error;
        runtime.action(6);frame();EXPECT_TRUE(runtime.state().structures.empty());EXPECT_EQ(runtime.state().backpack,empty);
        ASSERT_TRUE(runtime.restore(bytes,saved.world,error))<<error;EXPECT_EQ(runtime.state(),saved);
        std::vector<std::byte> after;ASSERT_TRUE(runtime.snapshot(after,error));EXPECT_EQ(after,bytes);
        runtime.action(13,2);frame();const auto catalog=read();EXPECT_EQ(catalog.at("mode"),"catalog");
        for(const auto& row:catalog.at("rows"))EXPECT_NE(row.at("label"),"Starter room");
        // Loading another checkpoint may retain its world/revision but change
        // placement authority. An exhausted ID allocator must refuse even when
        // the previous checkpoint admitted exactly the same aimed piece.
        ASSERT_TRUE(runtime.restore(bytes,saved.world,error))<<error;
        const auto stillFrame=[&]{input.beginFrame();input.computeDeltas();runtime.update(0,input,camera,1280,800,{1280,800});input.endFrame();};
        bool restoredAim=false;
        for(int y:{560,640,480,400}) {
            for(int x:{640,480,800,320,960}) {
                input.onMouseMove(float(x),float(y));stillFrame();
                if(read().at("valid")==true){restoredAim=true;break;}
            }
            if(restoredAim)break;
        }
        ASSERT_TRUE(restoredAim)<<runtime.json();
        stillFrame();const auto admitted=read();const auto admittedState=runtime.state();
        auto exhausted=admittedState;exhausted.lastIssuedId=UINT64_MAX;
        std::vector<std::byte> exhaustedBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(exhausted,runtime.content(),exhaustedBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(exhaustedBytes,exhausted.world,error))<<error;
        stillFrame();const auto refused=read();
        EXPECT_EQ(runtime.state().revision,admittedState.revision);
        EXPECT_EQ(runtime.state().lastRequestSequence,admittedState.lastRequestSequence);
        EXPECT_EQ(refused.at("preview"),admitted.at("preview"));
        EXPECT_EQ(refused.at("aimRay"),admitted.at("aimRay"));
        EXPECT_FALSE(refused.at("valid").get<bool>());
        EXPECT_EQ(refused.at("previewReason"),"Building identity capacity reached.");
        const auto beforeRefusedPlace=runtime.state();runtime.action(4);stillFrame();
        EXPECT_EQ(runtime.state(),beforeRefusedPlace);
        ASSERT_TRUE(runtime.restore(bytes,saved.world,error))<<error;stillFrame();
        EXPECT_TRUE(read().at("valid"))<<runtime.json();
        wgpuQueueSubmit(context.getQueue(),0,nullptr);
        auto* callback=new std::shared_ptr<GpuSignals>(signals);
        wgpuQueueOnSubmittedWorkDone(context.getQueue(),[](WGPUQueueWorkDoneStatus status,void* data){
            std::unique_ptr<std::shared_ptr<GpuSignals>> owned(static_cast<std::shared_ptr<GpuSignals>*>(data));
            (*owned)->queue=status==WGPUQueueWorkDoneStatus_Success?1:2;
        },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(signals->queue.load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_EQ(signals->queue.load(),1);
    }
    context.tick();{std::lock_guard guard(signals->mutex);EXPECT_EQ(signals->errors.load(),0u)<<signals->message;}
#endif
}

} // namespace voxy::game::adventure

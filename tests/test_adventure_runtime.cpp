#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/construction_policy.hpp"
#include "engine/platform/input.hpp"
#include "camera/camera.hpp"
#include "gpu/context.hpp"
#include "physics/authored_shape_resources.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cfenv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <locale>
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
        auto startup=read();EXPECT_EQ(startup.at("mode"),"build");EXPECT_EQ(startup.at("piece"),9);EXPECT_EQ(startup.at("costText"),"Unlimited pieces");
        EXPECT_EQ(startup.at("paint"),0xe53b33);EXPECT_EQ(startup.at("quickSlot"),1);
        ASSERT_EQ(startup.at("quickSlots").size(),6u);
        const std::array<std::pair<int,int>,6> presetDefaults{{{9,0xe53b33},{10,0xf3f2eb},{2,0},{1,0x3ba85c},{4,0xf3f2eb},{15,0}}};
        for(size_t i=0;i<presetDefaults.size();++i) {
            EXPECT_EQ(startup.at("quickSlots")[i].at("piece"),presetDefaults[i].first);
            EXPECT_EQ(startup.at("quickSlots")[i].at("paint"),presetDefaults[i].second);
        }
        EXPECT_TRUE(startup.at("creative"));
        EXPECT_EQ(startup.at("forest").at("drawDistance"),2000);
        EXPECT_GT(startup.at("forest").at("distantTrees").get<size_t>(),1000u);
        ASSERT_TRUE(startup.at("cannon").at("available"));
        EXPECT_FALSE(startup.at("cannon").at("active"));
        ASSERT_TRUE(startup.at("blacksmith").at("available"));
        EXPECT_DOUBLE_EQ(startup.at("blacksmith").at("x"),1208);
        EXPECT_DOUBLE_EQ(startup.at("blacksmith").at("z"),-1032);
        // Probe the accepted runtime packet against independently identified
        // original LDraw surfaces, not another copy of the baked box array.
        // The installed model is centered on its base and rotated 180 degrees.
        const double setFloor=startup.at("blacksmith").at("y").get<double>();
        const auto setPoint=[&](double x,double y,double z){return glm::dvec3(1208-x,setFloor+y,-1032-z);};
        const auto& geometry=runtime.spatialQueries();
        constexpr double figureRadius=AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale;
        constexpr double figureHeight=1.7*AdventurePlayer::creativeScale;
        EXPECT_GE(geometry.solidCount(),4054u);
        EXPECT_LE(geometry.solidCount(),AdventureSpatialQueries::maximumSolids);
        EXPECT_TRUE(geometry.clearCapsule(setPoint(-10,.185,-3),figureRadius,figureHeight)); // Open courtyard.
        EXPECT_TRUE(geometry.clearCapsule(setPoint(3,1.01,-2),figureRadius,figureHeight)); // Air inside the room.
        const auto interior=setPoint(3,1.01,-2);
        EXPECT_NEAR(geometry.supportHeight({interior.x,interior.z},figureRadius,setFloor+1.4),setFloor+1.,.001);
        EXPECT_FALSE(geometry.clearCapsule(setPoint(.6,1,-7),figureRadius,figureHeight)); // Actual masonry.
        const auto wallSweep=geometry.sweepCapsule(setPoint(3,1.01,-2),setPoint(-1,1.01,-2),figureRadius,figureHeight);
        ASSERT_TRUE(wallSweep.complete);EXPECT_TRUE(wallSweep.hit);EXPECT_FALSE(wallSweep.startOverlapped);
        EXPECT_GT(wallSweep.distance,0);EXPECT_LT(wallSweep.distance,4);
        // Each tile is a separate stair tread, 24 LDraw units (1.2 studs) higher.
        for(int step=0;step<5;++step) {
            const double height=2.4+1.2*step;
            const auto tread=setPoint(-2.2,height,1.5-2*step);
            EXPECT_NEAR(geometry.supportHeight({tread.x,tread.z},.05,tread.y+.2),tread.y,.001)<<step;
        }
        // Traverse the actual imported staircase with the full-size figure.
        // Start near the front edge: the center of the narrow first tread is
        // already within the rounded capsule's reach of the following riser.
        for(const double pace:{1.,1.75}) {
            SCOPED_TRACE(pace);
            AdventurePlayer stairWalker;
            const auto firstTread=setPoint(-2.2,2.405,3);
            ASSERT_TRUE(stairWalker.initialize(geometry,firstTread,installedWorld().waterHeight,0,
                AdventurePlayer::creativeScale,AdventurePlayer::creativeRadius));
            double highest=firstTread.y;
            for(int tick=0;tick<150;++tick) {
                stairWalker.advance(AdventurePlayer::fixedStep,{{0,1},false,pace});
                const auto feet=stairWalker.feet();
                highest=std::max(highest,feet.y);
                EXPECT_TRUE(geometry.clearCapsule(feet,figureRadius,figureHeight))<<tick;
                EXPECT_EQ(stairWalker.mode(),AdventurePlayer::Mode::Walking)<<tick;
                EXPECT_NEAR(feet.y,geometry.supportHeight({feet.x,feet.z},figureRadius,feet.y+.01)+.005,.001)<<tick;
            }
            EXPECT_GE(highest,setFloor+6.005); // At least three full brick risers.
            EXPECT_GT(stairWalker.feet().z-firstTread.z,5.);
        }
        const auto roofRay=geometry.raycast(setPoint(6,40,-3),{0,-1,0},20);
        ASSERT_TRUE(roofRay.complete);ASSERT_TRUE(roofRay.hit);EXPECT_FALSE(roofRay.terrain);
        EXPECT_GT(roofRay.point.y,setFloor+30);EXPECT_LT(roofRay.point.y,setFloor+33);
        const auto roofCamera=geometry.sweepSphere(setPoint(6,40,-3),setPoint(6,25,-3),.3,geometry.revision());
        ASSERT_TRUE(roofCamera.complete);EXPECT_TRUE(roofCamera.hit);EXPECT_FALSE(roofCamera.startOverlapped);
        const auto opening=runtime.state();
        EXPECT_DOUBLE_EQ(opening.player.x,creativeStart.x);EXPECT_DOUBLE_EQ(opening.player.z,creativeStart.y);
        EXPECT_DOUBLE_EQ(opening.player.yaw,creativeStartYaw);
        EXPECT_GT(opening.player.y,double(installedWorld().waterHeight)+150);
        // The meadow behind the overlook offers a 32-stud building area with
        // less than two plates of terrain relief, away from the descending shore.
        double low=1e9,high=-1e9;
        for(int z=-16;z<=16;++z)for(int x=-16;x<=16;++x) {
            const double top=surface.heightAt(float(creativeStart.x-20+x),float(creativeStart.y+50+z));
            low=std::min(low,top);high=std::max(high,top);
        }
        EXPECT_LE(high-low,.65);
        // New-world presentation must never teleport an existing saved player
        // or invalidate the old recovery/content contract.
        auto oldLocation=opening;oldLocation.player=runtime.content().town;
        std::vector<std::byte> oldLocationBytes,openingBytes;
        ASSERT_TRUE(runtime.snapshot(openingBytes,error))<<error;
        // The imported set is scenery: adding it must not eject a saved
        // player standing inside its new masonry footprint. Collision and
        // rendering suppress together; the original set returns on clear load.
        auto occupiedPlot=opening;
        occupiedPlot.player={1202,.185,-1026,0};
        std::vector<std::byte> occupiedPlotBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(occupiedPlot,runtime.content(),occupiedPlotBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(occupiedPlotBytes,occupiedPlot.world,error))<<error;
        EXPECT_EQ(runtime.state(),occupiedPlot);
        EXPECT_FALSE(read().at("blacksmith").at("available"));
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        EXPECT_TRUE(read().at("blacksmith").at("available"));
        // A saved actor in real interior air does not suppress the whole set.
        // The former solid building-wide proxy incorrectly rejected this room.
        auto roomSave=opening;
        roomSave.player={interior.x,interior.y,interior.z,0};
        std::vector<std::byte> roomBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(roomSave,runtime.content(),roomBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(roomBytes,roomSave.world,error))<<error;
        EXPECT_EQ(runtime.state(),roomSave);
        EXPECT_TRUE(read().at("blacksmith").at("available"));
        EXPECT_TRUE(runtime.spatialQueries().clearCapsule(interior,figureRadius,figureHeight));
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        ASSERT_TRUE(AdventureSaveCodec::encode(oldLocation,runtime.content(),oldLocationBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(oldLocationBytes,oldLocation.world,error))<<error;
        EXPECT_EQ(runtime.state(),oldLocation);
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        EXPECT_EQ(runtime.state(),opening);
        for(const auto& resident:startup.at("residents"))EXPECT_FALSE(resident.at("available"));
        for(const auto& site:startup.at("trailSites"))EXPECT_FALSE(site.at("available"));
        const auto empty=runtime.state().backpack;
        Input input;input.onFocusChanged(true);input.onMouseMove(640,400);Camera camera;
        const auto frame=[&]{input.beginFrame();input.computeDeltas();runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();};
        input.beginFrame();input.computeDeltas();runtime.update(AdventurePlayer::fixedStep,input,camera,640,400,{640,400});input.endFrame();
        EXPECT_FALSE(read().at("navigation").at("visible"));EXPECT_EQ(read().at("navigation").at("mapRevision"),0);
        EXPECT_EQ(read().at("navigation").at("terrainRasterizations"),0);
        for(int i=0;i<10;++i)frame();
        EXPECT_EQ(runtime.state().combat.tick,0u);
        const auto scaledView=read();
        const auto navigation=scaledView.at("navigation");
        EXPECT_TRUE(navigation.at("visible"));EXPECT_GT(navigation.at("mapRevision").get<uint64_t>(),0u);
        EXPECT_EQ(navigation.at("placedPieces"),0);
        EXPECT_NEAR(navigation.at("playerBearingDegrees").get<double>(),360-creativeStartYaw*180/std::numbers::pi,.001);
        for(int i=0;i<5;++i)frame();
        EXPECT_EQ(read().at("navigation").at("mapRevision"),navigation.at("mapRevision"));
        EXPECT_EQ(read().at("navigation").at("terrainRasterizations"),navigation.at("terrainRasterizations"));
        EXPECT_NEAR(scaledView.at("camera").at("viewTarget")[1].get<double>()
            -scaledView.at("player").at("y").get<double>(),1.2*AdventurePlayer::creativeScale,.01);
        // Real Shift events alter on-foot pace, including when the brick
        // palette is open. Releasing Shift immediately restores walking.
        const auto travel=[&](std::optional<Key> sprint,bool building) {
            EXPECT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
            runtime.action(building?21:22);frame();
            const auto start=runtime.state().player;
            if(sprint)input.onKeyDown(int(*sprint));
            input.onKeyDown(int(Key::D));for(int i=0;i<24;++i)frame();
            input.onKeyUp(int(Key::D));if(sprint)input.onKeyUp(int(*sprint));frame();
            const auto finish=runtime.state().player;
            return glm::length(glm::dvec2(finish.x-start.x,finish.z-start.z));
        };
        const double walk=travel(std::nullopt,false);
        EXPECT_GT(walk,2.);
        EXPECT_NEAR(travel(Key::LeftShift,false)/walk,1.75,.06);
        EXPECT_NEAR(travel(Key::RightShift,false)/walk,1.75,.06);
        EXPECT_NEAR(travel(Key::LeftShift,true)/walk,1.75,.06);
        EXPECT_NEAR(travel(std::nullopt,false)/walk,1.,.03);
        // Jump edges carry the Shift state at key-down, independently of the
        // held-key modifier used for movement. Exercise the real input path.
        for(const auto sprint:{Key::LeftShift,Key::RightShift})for(bool building:{false,true}) {
            SCOPED_TRACE(::testing::Message()<<"shift="<<int(sprint)<<" building="<<building);
            ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
            runtime.action(building?21:22);frame();
            input.onKeyDown(int(sprint));input.onKeyDown(int(Key::D));
            for(int i=0;i<6;++i)frame();
            const auto takeoff=runtime.state().player;
            input.onKeyDown(int(Key::Space));frame();
            const auto jumped=runtime.state().player;
            EXPECT_GT(jumped.y-takeoff.y,.1);
            EXPECT_NEAR(glm::length(glm::dvec2(jumped.x-takeoff.x,jumped.z-takeoff.z)),
                3.6*std::sqrt(AdventurePlayer::creativeScale)*1.75*AdventurePlayer::fixedStep,.01);
            input.onKeyUp(int(Key::Space));
            for(int i=0;i<6;++i)frame();
            EXPECT_GT(runtime.state().player.y-takeoff.y,.7);
            input.onKeyUp(int(Key::D));input.onKeyUp(int(sprint));frame();
        }
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        for(int i=0;i<10;++i)frame();
        // Exercise swimming through real held Input events on the installed
        // terrain, including a submerged save and the menu ownership boundary.
        auto waterSave=opening;
        const double swimSurface=double(installedWorld().waterHeight)-double(AdventurePlayer::swimImmersion)*double(AdventurePlayer::creativeScale);
        bool foundWater=false;
        for(int z=-3840;z<=3840&&!foundWater;z+=512)for(int x=-3840;x<=3840&&!foundWater;x+=512) {
            if(double(surface.heightAt(float(x),float(z)))>swimSurface-20)continue;
            const glm::dvec3 feet(double(x),swimSurface,double(z));
            if(!runtime.spatialQueries().clearCapsule(feet,figureRadius,figureHeight))continue;
            waterSave.player={feet.x,feet.y,feet.z,0};foundWater=true;
        }
        ASSERT_TRUE(foundWater);
        std::vector<std::byte> waterBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(waterSave,runtime.content(),waterBytes,error))<<error;
        for(bool building:{false,true}) {
            SCOPED_TRACE(::testing::Message()<<"swim building="<<building);
            ASSERT_TRUE(runtime.restore(waterBytes,waterSave.world,error))<<error;
            runtime.action(building?21:22);frame();frame();
            EXPECT_TRUE(read().at("swimming"));
            input.onKeyDown(int(Key::X));for(int i=0;i<60;++i)frame();
            input.onKeyUp(int(Key::X));for(int i=0;i<30;++i)frame();
            const auto depth=runtime.state().player.y;
            EXPECT_LT(depth,swimSurface-3.);
            for(int i=0;i<20;++i)frame();
            EXPECT_NEAR(runtime.state().player.y,depth,1e-6);
            std::vector<std::byte> underwaterBytes;
            ASSERT_TRUE(runtime.snapshot(underwaterBytes,error))<<error;
            ASSERT_TRUE(runtime.restore(underwaterBytes,waterSave.world,error))<<error;
            frame();EXPECT_NEAR(runtime.state().player.y,depth,1e-6);EXPECT_TRUE(read().at("swimming"));
            input.onKeyDown(int(Key::Space));for(int i=0;i<150;++i)frame();
            input.onKeyUp(int(Key::Space));frame();
            EXPECT_NEAR(runtime.state().player.y,swimSurface,1e-6);
            const auto beforeSwim=runtime.state().player;
            input.onKeyDown(int(Key::W));for(int i=0;i<60;++i)frame();
            input.onKeyUp(int(Key::W));for(int i=0;i<30;++i)frame();
            EXPECT_GT(glm::length(glm::dvec2(runtime.state().player.x-beforeSwim.x,runtime.state().player.z-beforeSwim.z)),3.);
            EXPECT_NEAR(runtime.state().player.y,swimSurface,1e-6);
            input.onKeyDown(int(Key::Escape));frame();input.onKeyUp(int(Key::Escape));frame();
            input.onKeyDown(int(Key::X));for(int i=0;i<20;++i)frame();
            EXPECT_NEAR(runtime.state().player.y,swimSurface,1e-6);
            input.onKeyDown(int(Key::Escape));frame();input.onKeyUp(int(Key::Escape));
            for(int i=0;i<20;++i)frame();
            EXPECT_NEAR(runtime.state().player.y,swimSurface,1e-6);
            input.onKeyUp(int(Key::X));frame();
        }
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        for(int i=0;i<10;++i)frame();
        // Real M key edges mount/dismount; held M never repeatedly toggles.
        input.onKeyDown(int(Key::M));frame();
        ASSERT_TRUE(read().at("riding"))<<runtime.json();
        EXPECT_EQ(read().at("mode"),"explore");
        for(int i=0;i<5;++i)frame();
        EXPECT_TRUE(read().at("riding"));
        input.onKeyUp(int(Key::M));frame();
        const auto bikeStart=runtime.state().player;
        input.onKeyDown(int(Key::W));for(int i=0;i<60;++i)frame();
        input.onKeyUp(int(Key::W));frame();
        EXPECT_GT(glm::length(position(runtime.state().player)-position(bikeStart)),1.);
        input.onKeyDown(int(Key::Space));for(int i=0;i<60;++i)frame();
        input.onKeyUp(int(Key::Space));frame();
        EXPECT_NEAR(read().at("bikeSpeed").get<double>(),0,1e-6);
        input.onKeyDown(int(Key::M));frame();input.onKeyUp(int(Key::M));frame();
        EXPECT_FALSE(read().at("riding"))<<runtime.json();
        ASSERT_TRUE(runtime.restore(openingBytes,opening.world,error))<<error;
        for(int i=0;i<10;++i)frame();
        // Wheel selects a pictured piece without rotating or zooming the camera.
        runtime.action(39,2);frame();EXPECT_EQ(read().at("piece"),10);EXPECT_EQ(read().at("paint"),0xf3f2eb);
        input.onScroll(-1);frame();EXPECT_EQ(read().at("piece"),2);EXPECT_EQ(read().at("paint"),0);
        input.onScroll(1);frame();EXPECT_EQ(read().at("piece"),10);EXPECT_EQ(read().at("paint"),0xf3f2eb);
        input.onKeyDown(int(Key::LeftControl));input.onScroll(-1);frame();EXPECT_EQ(read().at("piece"),10);
        input.onKeyUp(int(Key::LeftControl));frame();
        runtime.action(30,0x3ba85c);frame();EXPECT_EQ(read().at("paint"),0x3ba85c);
        runtime.action(30,-1);frame();EXPECT_EQ(read().at("paint"),0x3ba85c);
        runtime.action(30,0x1000000);frame();EXPECT_EQ(read().at("paint"),0x3ba85c);
        // The colour picker is an engine menu on both platforms. Keyboard and
        // controller confirm consume the same displayed, scoped intent.
        runtime.action(38);frame();ASSERT_EQ(read().at("mode"),"colours");
        EXPECT_TRUE(read().at("colourPickerOpen"));EXPECT_TRUE(runtime.isPaused());
        ASSERT_EQ(read().at("rows").size(),7u);EXPECT_EQ(read().at("menuSelected"),3);
        const auto red=menuRow(runtime.json(),"Red");ASSERT_TRUE(red);
        input.onKeyDown(int(Key::Right));frame();input.onKeyUp(int(Key::Right));frame();
        EXPECT_EQ(read().at("menuSelected"),4);EXPECT_EQ(read().at("paint"),0x3ba85c);
        input.onKeyDown(int(Key::Enter));frame();input.onKeyUp(int(Key::Enter));frame();
        EXPECT_EQ(read().at("mode"),"build");EXPECT_EQ(read().at("paint"),0x2d91cc);
        runtime.action(10,red->intent);frame();EXPECT_EQ(read().at("paint"),0x2d91cc);
        EXPECT_TRUE(runtime.state().structures.empty());

        input.onKeyDown(int(Key::P));frame();input.onKeyUp(int(Key::P));frame();
        ASSERT_EQ(read().at("mode"),"colours");
        // Closing the picker and clicking in the same frame cannot place.
        input.onKeyDown(int(Key::P));input.onMouseDown(int(MouseButton::Left));frame();
        EXPECT_EQ(read().at("mode"),"build");EXPECT_TRUE(runtime.state().structures.empty());
        input.onKeyUp(int(Key::P));input.onMouseUp(int(MouseButton::Left));frame();
        auto& colourPad=const_cast<GamepadInput&>(input.gamepad());
        GamepadSample neutralColourPad;neutralColourPad.connected=true;neutralColourPad.device=17;
        double colourPadSeconds=0;
        const auto padFrame=[&](std::optional<PadButton> button,std::optional<PadButton> second=std::nullopt) {
            input.beginFrame();input.computeDeltas();
            colourPad.update(neutralColourPad,true,colourPadSeconds+=AdventurePlayer::fixedStep);
            if(button) {
                auto sample=neutralColourPad;sample.buttons[static_cast<size_t>(*button)]=true;
                if(second)sample.buttons[static_cast<size_t>(*second)]=true;
                colourPad.update(sample,true,colourPadSeconds+=AdventurePlayer::fixedStep);
            }
            runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
        };
        padFrame(PadButton::LeftStick);ASSERT_EQ(read().at("mode"),"colours");
        padFrame(PadButton::Left);EXPECT_EQ(read().at("menuSelected"),3);
        padFrame(PadButton::Confirm);EXPECT_EQ(read().at("mode"),"build");EXPECT_EQ(read().at("paint"),0x3ba85c);
        EXPECT_TRUE(runtime.state().structures.empty());
        padFrame(PadButton::Alternate);EXPECT_EQ(read().at("mode"),"catalog");
        const int previousCategory=read().at("catalogCategory");
        const int oldCategoryIntent=read().at("rows")[0].at("intent");
        padFrame(PadButton::RightShoulder,PadButton::Confirm);EXPECT_EQ(read().at("catalogCategory"),(previousCategory+1)%3);
        EXPECT_EQ(read().at("mode"),"catalog");EXPECT_EQ(read().at("piece"),10);
        runtime.action(10,oldCategoryIntent);frame();EXPECT_EQ(read().at("mode"),"catalog");EXPECT_EQ(read().at("piece"),10);
        padFrame(PadButton::Back);EXPECT_EQ(read().at("mode"),"build");
        colourPad.update({},true,colourPadSeconds+=AdventurePlayer::fixedStep);frame();
        // Catalog edits replace only the active preset. Switching away/back
        // must preserve that slot's exact piece and paint, including duplicates.
        runtime.action(13,3);frame();const auto workbench=menuRow(runtime.json(),"Workbench");ASSERT_TRUE(workbench);
        runtime.action(10,workbench->intent);frame();EXPECT_EQ(read().at("piece"),13);EXPECT_EQ(read().at("paint"),0x3ba85c);
        EXPECT_EQ(read().at("quickSlots")[1].at("piece"),13);EXPECT_EQ(read().at("quickSlot"),2);
        runtime.action(39,1);frame();EXPECT_EQ(read().at("piece"),9);EXPECT_EQ(read().at("paint"),0xe53b33);
        runtime.action(39,2);frame();EXPECT_EQ(read().at("piece"),13);EXPECT_EQ(read().at("paint"),0x3ba85c);
        runtime.action(2,10);frame();EXPECT_EQ(read().at("quickSlots")[1].at("piece"),10);
        runtime.action(39,1);frame();runtime.action(2,10);frame();runtime.action(30,0x3ba85c);frame();
        EXPECT_EQ(read().at("quickSlots")[0],read().at("quickSlots")[1]);EXPECT_EQ(read().at("quickSlot"),1);
        runtime.action(39,2);frame();EXPECT_EQ(read().at("quickSlot"),2);
        runtime.action(39,1);frame();runtime.action(2,9);frame();runtime.action(30,0xe53b33);frame();
        runtime.action(39,2);frame();
        runtime.action(39,0);runtime.action(39,7);frame();EXPECT_EQ(read().at("quickSlot"),2);EXPECT_EQ(read().at("piece"),10);
        RecordProperty("creativeHudNavigation","real runtime keyboard events and synthetic standard controller samples; no controller hardware");

        // Hit boxes come from the actual GPU HUD, including its disabled
        // controls. The browser accessibility adapter mirrors this same JSON.
        struct HudTarget {
            WGPUTexture texture=nullptr;
            WGPUTextureView view=nullptr;
            ~HudTarget(){if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
        } hudTarget;
        WGPUTextureDescriptor hudTexture{};
        hudTexture.usage=WGPUTextureUsage_RenderAttachment;hudTexture.dimension=WGPUTextureDimension_2D;
        hudTexture.size={1280,800,1};hudTexture.format=WGPUTextureFormat_RGBA8Unorm;
        hudTexture.mipLevelCount=1;hudTexture.sampleCount=1;
        hudTarget.texture=wgpuDeviceCreateTexture(context.getDevice(),&hudTexture);ASSERT_TRUE(hudTarget.texture);
        hudTarget.view=wgpuTextureCreateView(hudTarget.texture,nullptr);ASSERT_TRUE(hudTarget.view);
        const auto drawHud=[&] {
            WGPUCommandEncoderDescriptor descriptor{};
            const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&descriptor);
            const bool drawn=runtime.renderHud(encoder,hudTarget.view,1280,800);
            WGPUCommandBufferDescriptor commandDescriptor{};
            const auto command=wgpuCommandEncoderFinish(encoder,&commandDescriptor);wgpuCommandEncoderRelease(encoder);
            if(command){wgpuQueueSubmit(context.getQueue(),1,&command);wgpuCommandBufferRelease(command);}
            return drawn&&command;
        };
        runtime.action(19,1);ASSERT_TRUE(drawHud());
        auto controls=read().at("hud").at("controls");ASSERT_FALSE(controls.empty());
        EXPECT_EQ(read().at("hud").at("width"),1280);EXPECT_EQ(read().at("hud").at("height"),800);
        // The digits select the cards actually drawn, including a shifted
        // window after selection. Hidden/disabled slots never become shortcuts.
        for(const auto key:{Key::Num1,Key::Num6}) {
            const auto slot=std::find_if(controls.begin(),controls.end(),[&](const auto& row){return row.at("shortcutKey")==uint32_t(key);});
            ASSERT_NE(slot,controls.end());const int selectedSlot=slot->at("value");
            const auto preset=read().at("quickSlots")[size_t(selectedSlot-1)];
            input.onKeyDown(int(key));frame();input.onKeyUp(int(key));frame();
            EXPECT_EQ(read().at("piece"),preset.at("piece"));EXPECT_EQ(read().at("paint"),preset.at("paint"));
            EXPECT_EQ(read().at("quickSlot"),selectedSlot);EXPECT_TRUE(runtime.state().structures.empty());
            ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        }
        runtime.action(39,2);frame();ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        const auto colourControl=std::find_if(controls.begin(),controls.end(),[](const auto& row){return row.at("action")==38;});
        ASSERT_NE(colourControl,controls.end());EXPECT_EQ(colourControl->at("label"),"Colour");
        const auto pointAt=[&](const auto& row,float pointerScale=1) {
            input.onMouseMove((row.at("x").template get<float>()+row.at("width").template get<float>()*.5f)*pointerScale,
                (row.at("y").template get<float>()+row.at("height").template get<float>()*.5f)*pointerScale);
        };
        render::AdventureHudContent navigationContent;navigationContent.creative=true;navigationContent.mode=render::AdventureHudMode::Build;
        render::AdventureHudNavigation navigationBounds;navigationBounds.visible=true;navigationBounds.map=std::make_shared<render::AdventureHudMap>();
        const auto navigationLayout=render::layoutAdventureNavigation(navigationBounds,navigationContent,1280,800);
        ASSERT_EQ(navigationLayout.menuHits.size(),2u);
        for(const auto& surfaceHit:navigationLayout.menuHits) {
            const auto bounds=surfaceHit.bounds;
            input.onMouseMove(bounds.x+bounds.z*.5f,bounds.y+bounds.w*.5f);
            input.onScroll(-1);input.onMouseDown(int(MouseButton::Left));frame();
            input.onMouseUp(int(MouseButton::Left));frame();
            EXPECT_EQ(read().at("piece"),10);EXPECT_TRUE(runtime.state().structures.empty());
        }
        // The old 1280x800 image is still visible while the next framebuffer
        // becomes 960x600 inside a 640x400 logical window. The rendered control
        // must retain its identity across both scale and resize boundaries.
        pointAt(*colourControl,.5f);input.onMouseDown(int(MouseButton::Left));
        input.beginFrame();input.computeDeltas();
        runtime.update(AdventurePlayer::fixedStep,input,camera,960,600,{640,400});input.endFrame();
        input.onMouseUp(int(MouseButton::Left));frame();
        ASSERT_EQ(read().at("mode"),"colours");EXPECT_TRUE(runtime.state().structures.empty());
        ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        const auto greenControl=std::find_if(controls.begin(),controls.end(),[](const auto& row){return row.at("label")=="Green";});
        ASSERT_NE(greenControl,controls.end());EXPECT_EQ(greenControl->at("row"),3);
        pointAt(*greenControl);input.onMouseDown(int(MouseButton::Left));frame();
        input.onMouseUp(int(MouseButton::Left));frame();
        EXPECT_EQ(read().at("mode"),"build");EXPECT_EQ(read().at("paint"),0x3ba85c);
        EXPECT_TRUE(runtime.state().structures.empty());
        runtime.action(23);frame();ASSERT_EQ(read().at("mode"),"catalog");
        ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        const auto disabledSlot=std::find_if(controls.begin(),controls.end(),[](const auto& row){return row.at("action")==39;});
        ASSERT_NE(disabledSlot,controls.end());EXPECT_FALSE(disabledSlot->at("enabled"));
        pointAt(*disabledSlot);input.onScroll(-1);frame();EXPECT_EQ(read().at("piece"),10);
        input.onMouseDown(int(MouseButton::Left));frame();input.onMouseUp(int(MouseButton::Left));frame();
        input.onKeyDown(int(Key::Num1));frame();input.onKeyUp(int(Key::Num1));frame();
        EXPECT_EQ(read().at("piece"),10);EXPECT_EQ(read().at("mode"),"catalog");
        EXPECT_TRUE(runtime.state().structures.empty());
        runtime.action(20);frame();ASSERT_TRUE(drawHud());
        const auto retinaFrame=[&] {
            input.beginFrame();input.computeDeltas();
            runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{640,400});input.endFrame();
        };
        retinaFrame();ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        ASSERT_FALSE(controls.empty());
        for(const auto& control:controls) {
            EXPECT_GE(control.at("width").get<double>()/2,44.-1e-4)<<control.at("label");
            EXPECT_GE(control.at("height").get<double>()/2,44.-1e-4)<<control.at("label");
        }
        const auto retinaColour=std::find_if(controls.begin(),controls.end(),[](const auto& row){return row.at("action")==38;});
        ASSERT_NE(retinaColour,controls.end());
        pointAt(*retinaColour,.5f);input.onMouseDown(int(MouseButton::Left));retinaFrame();
        input.onMouseUp(int(MouseButton::Left));retinaFrame();
        EXPECT_EQ(read().at("mode"),"colours");EXPECT_TRUE(runtime.state().structures.empty());
        ASSERT_TRUE(drawHud());controls=read().at("hud").at("controls");
        for(const auto& control:controls) {
            EXPECT_GE(control.at("width").get<double>()/2,44.-1e-4)<<control.at("label");
            EXPECT_GE(control.at("height").get<double>()/2,44.-1e-4)<<control.at("label");
        }
        runtime.action(20);frame();ASSERT_TRUE(drawHud());
        input.onMouseMove(640,400);frame();
        RecordProperty("creativeHudPointer","encoded offscreen shared HUD; authoritative hit boxes; no presented surface or screenshot");
        runtime.action(29,0);frame();ASSERT_EQ(read().at("mode"),"guide");
        const auto guideStructures=runtime.state().structures;
        for(const bool controller:{false,true}) {
            if(controller)padFrame(PadButton::RightStick);
            else {input.onKeyDown(int(Key::G));frame();input.onKeyUp(int(Key::G));frame();}
            for(size_t topic=0;topic<6;++topic) {
                const auto topics=read().at("rows");ASSERT_EQ(topics.size(),7u);
                runtime.action(10,topics[topic].at("intent"));frame();
                const auto card=read();render::AdventureHudContent guide;
                guide.creative=true;guide.mode=render::AdventureHudMode::Guide;
                guide.title=card.at("menuTitle");guide.menuText=card.at("menuText");
                for(const auto& row:card.at("rows"))guide.rows.push_back({row.at("label"),"",true,10,0,row.at("intent"),0});
                for(float scale:{1.f,1.25f,1.5f})for(auto size:{glm::uvec2(320,568),glm::uvec2(480,480),glm::uvec2(1280,800)}) {
                    SCOPED_TRACE(::testing::Message()<<guide.title<<" pad "<<controller<<" scale "<<scale<<" "<<size.x<<"x"<<size.y);
                    guide.textScale=scale;const auto layout=render::layoutAdventureHud(guide,size.x,size.y);
                    EXPECT_TRUE(layout.guideBodyComplete);EXPECT_FALSE(layout.canvas.truncated);EXPECT_TRUE(layout.selectedVisible);
                }
                const auto allTopics=menuRow(runtime.json(),"All topics");ASSERT_TRUE(allTopics);
                runtime.action(10,allTopics->intent);frame();
            }
        }
        colourPad.update({},true,colourPadSeconds+=AdventurePlayer::fixedStep);
        runtime.action(20);frame();EXPECT_EQ(read().at("mode"),"build");
        EXPECT_EQ(runtime.state().structures,guideStructures);
        runtime.action(31);frame();EXPECT_EQ(read().at("mode"),"pause");
        for(const auto* label:{"Rotate piece","Raise piece","Lower piece","Choose colour","Finish building"}) {
            const auto row=menuRow(runtime.json(),label);ASSERT_TRUE(row)<<label;EXPECT_TRUE(row->enabled)<<label;
        }
        const auto removeRow=menuRow(runtime.json(),"Remove aimed piece");ASSERT_TRUE(removeRow);EXPECT_FALSE(removeRow->enabled);
        // Menu height actions use the same commands as the former toolbar.
        const auto raise=menuRow(runtime.json(),"Raise piece");ASSERT_TRUE(raise);
        runtime.action(10,raise->intent);frame();frame();EXPECT_EQ(read().at("mode"),"build");
        runtime.action(31);frame();const auto lower=menuRow(runtime.json(),"Lower piece");ASSERT_TRUE(lower);
        runtime.action(10,lower->intent);frame();frame();EXPECT_EQ(read().at("mode"),"build");
        runtime.action(31);frame();
        runtime.action(20);frame();EXPECT_EQ(read().at("mode"),"build");
        // Pick a safe location for the rotated brick: rotation near the wider
        // character can correctly invalidate a previously clear long edge.
        runtime.action(3);frame();
        bool aim=false;
        for(int y:{560,640,480,400}) {
            for(int x:{640,480,800,320,960}) {
                input.onMouseMove(float(x),float(y));frame();
                if(read().at("valid")==true){aim=true;break;}
            }
            if(aim)break;
        }
        ASSERT_TRUE(aim)<<runtime.json();
        ASSERT_TRUE(read().at("valid"))<<runtime.json();
        runtime.action(4);frame();
        ASSERT_EQ(runtime.state().structures.size(),1u)<<runtime.json();
        ASSERT_EQ(runtime.state().structures[0].parts.size(),1u);
        EXPECT_EQ(runtime.state().structures[0].parts[0].kind,PieceKind::Brick2x4);
        EXPECT_EQ(runtime.state().structures[0].parts[0].yawQuarterTurns,1);
        EXPECT_EQ(runtime.state().structures[0].parts[0].paint,0x3ba85cu);
        EXPECT_EQ(read().at("navigation").at("placedPieces"),1);
        EXPECT_TRUE(read().at("canUndo"));
        EXPECT_EQ(runtime.state().backpack,empty);
        const auto saved=runtime.state();std::vector<std::byte> bytes;
        ASSERT_TRUE(runtime.snapshot(bytes,error))<<error;
        runtime.action(31);frame();
        const auto pausedMapRevision=read().at("navigation").at("mapRevision");
        EXPECT_FALSE(read().at("navigation").at("visible"));
        const auto undoRow=menuRow(runtime.json(),"Undo last placement");ASSERT_TRUE(undoRow);EXPECT_TRUE(undoRow->enabled);
        runtime.action(10,undoRow->intent);frame();EXPECT_TRUE(runtime.state().structures.empty());
        EXPECT_EQ(read().at("navigation").at("placedPieces"),0);
        EXPECT_EQ(read().at("navigation").at("mapRevision"),pausedMapRevision);
        runtime.action(10,undoRow->intent);frame();EXPECT_TRUE(runtime.state().structures.empty());
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
        // UI data must change after a same-revision restore, even if geometry
        // has the same shape and only durable identities differ.
        const auto oldStructures=read().at("structures");
        auto renumbered=runtime.state();
        renumbered.structures[0].id+=10;renumbered.structures[0].parts[0].id+=10;renumbered.lastIssuedId+=10;
        std::vector<std::byte> renumberedBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(renumbered,runtime.content(),renumberedBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(renumberedBytes,renumbered.world,error))<<error;
        const auto newStructures=read().at("structures");
        EXPECT_NE(newStructures,oldStructures);
        EXPECT_EQ(newStructures[0].at("id"),std::to_string(renumbered.structures[0].id));
        EXPECT_EQ(newStructures[0].at("parts")[0].at("id"),std::to_string(renumbered.structures[0].parts[0].id));
        // The existing serializer follows the stream locale. A warm classic
        // fragment must not suppress a subsequently installed numeric facet.
        const auto structureText=[](const std::string& json) {
            const auto begin=json.find("\"structures\":");
            return json.substr(begin,json.find(",\"components\":",begin)-begin);
        };
        const auto classic=structureText(runtime.json());
        struct GroupedNumbers:std::numpunct<char> {
            char do_thousands_sep() const override{return '|';}
            std::string do_grouping() const override{return "\1";}
        };
        struct RestoreLocale {std::locale old=std::locale();~RestoreLocale(){std::locale::global(old);}};
        {
            RestoreLocale restoreLocale;
            std::locale::global(std::locale(std::locale::classic(),new GroupedNumbers));
            EXPECT_NE(structureText(runtime.json()),classic);
        }
        EXPECT_EQ(structureText(runtime.json()),classic);
        // Directed rounding changes the metre conversion/number formatting.
        // The cache is eligible only under the default nearest mode.
        struct RestoreRounding {int old=std::fegetround();~RestoreRounding(){(void)std::fesetround(old);}};
        {
            RestoreRounding restoreRounding;
            ASSERT_EQ(std::fesetround(FE_DOWNWARD),0);
            EXPECT_NE(structureText(runtime.json()),classic);
        }
        EXPECT_EQ(structureText(runtime.json()),classic);
        // Same-shaped restored geometry can carry new IDs. Picking must use
        // the new identity even when the camera ray and revision are unchanged.
        stillFrame();
        const auto& aimedPart=runtime.state().structures[0].parts[0];
        const auto* aimedDefinition=buildingDefinition(aimedPart.kind);
        auto center=glm::dvec3(aimedPart.position.x,aimedPart.position.y,aimedPart.position.z)*.02;
        center.y+=(aimedDefinition->bounds.minimum.y+aimedDefinition->bounds.maximum.y)*.01;
        const auto origin=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize);
        const auto clip=glm::dmat4(camera.viewProjectionMatrix())*glm::dvec4(center-origin,1);
        ASSERT_GT(clip.w,0);
        input.onMouseMove(float((clip.x/clip.w+1)*640),float((1-clip.y/clip.w)*400));
        stillFrame();ASSERT_TRUE(read().at("canRemove"))<<runtime.json();
        stillFrame();const auto oldRay=read().at("aimRay");
        auto retargeted=runtime.state();
        retargeted.structures[0].id+=10;retargeted.structures[0].parts[0].id+=10;retargeted.lastIssuedId+=10;
        std::vector<std::byte> retargetedBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(retargeted,runtime.content(),retargetedBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(retargetedBytes,retargeted.world,error))<<error;
        stillFrame();EXPECT_EQ(read().at("aimRay"),oldRay);
        EXPECT_EQ(runtime.state().revision,retargeted.revision);
        runtime.action(5);stillFrame();
        EXPECT_TRUE(runtime.state().structures.empty())<<runtime.json();
        EXPECT_FALSE(read().at("canRemove"));
        // An old small figure can stand between studs where the larger body
        // cannot. Restore relocates only that pose, keeping every saved brick,
        // then a newly saved checkpoint must round-trip exactly at the new size.
        auto oldScale=saved;
        AdventureSpatialQueries savedGeometry;std::vector<AdventureSpatialQueries::Solid> savedSolids;
        ASSERT_TRUE(compileSolids(oldScale,savedSolids,error))<<error;
        ASSERT_TRUE(savedGeometry.bindTerrain(surface));ASSERT_TRUE(savedGeometry.publish(savedSolids,1));
        bool foundOldPose=false;
        for(int z=-3;z<=3&&!foundOldPose;++z)for(int x=-3;x<=3;++x) {
            const double px=std::floor(saved.player.x)+.5+x,pz=std::floor(saved.player.z)+.5+z;
            const double py=double(terrain::lego::supportHeight(surface,{float(px),float(pz)},float(AdventurePlayer::radius)))+.005;
            const glm::dvec3 point(px,py,pz);
            if(savedGeometry.clearCapsule(point)&&!savedGeometry.clearCapsule(point,
                AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,AdventurePlayer::height*AdventurePlayer::creativeScale)) {
                oldScale.player={px,py,pz,saved.player.yaw};foundOldPose=true;break;
            }
        }
        ASSERT_TRUE(foundOldPose);
        std::vector<std::byte> oldScaleBytes;
        ASSERT_TRUE(AdventureSaveCodec::encode(oldScale,runtime.content(),oldScaleBytes,error))<<error;
        ASSERT_TRUE(runtime.restore(oldScaleBytes,oldScale.world,error))<<error;
        EXPECT_EQ(runtime.state().structures,oldScale.structures);EXPECT_EQ(runtime.state().components,oldScale.components);
        const auto resized=runtime.state();
        EXPECT_NE(resized.player,oldScale.player);EXPECT_TRUE(read().at("dirty"));
        EXPECT_TRUE(savedGeometry.clearCapsule({resized.player.x,resized.player.y,resized.player.z},
            AdventurePlayer::creativeRadius*AdventurePlayer::creativeScale,AdventurePlayer::height*AdventurePlayer::creativeScale));
        std::vector<std::byte> resizedBytes,resizedAgain;
        ASSERT_TRUE(runtime.snapshot(resizedBytes,error));
        ASSERT_TRUE(runtime.restore(resizedBytes,resized.world,error))<<error;
        EXPECT_EQ(runtime.state(),resized);ASSERT_TRUE(runtime.snapshot(resizedAgain,error));EXPECT_EQ(resizedAgain,resizedBytes);
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

// This is full-world component integration with synthetic Input events. It
// exercises the actual runtime and GPU authority, not a rendered browser or
// platform keyboard route. A normal build skips it without the explicit terrain.
class ImportedWallRuntimeIntegration: public testing::TestWithParam<int> {};
TEST_P(ImportedWallRuntimeIntegration, CannonInputAndFullWorldGpuAdmission) {
    const bool removeSupport=GetParam()==1;
    const bool cannonImpact=GetParam()==2;
#if !defined(VOXY_NATIVE)
    GTEST_SKIP()<<"Native headless runtime only.";
#else
    const char* raw=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!raw||!*raw)GTEST_SKIP()<<"Set VOXY_ADVENTURE_TEST_TERRAIN to the installed full raw terrain.";
    std::vector<uint16_t> samples(8192*8192);std::ifstream terrainFile(raw,std::ios::binary);
    ASSERT_TRUE(terrainFile.read(reinterpret_cast<char*>(samples.data()),static_cast<std::streamsize>(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(terrainFile.peek(),std::char_traits<char>::eof());
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    auto resources=std::filesystem::current_path();
    if(const char* workspace=std::getenv("BUILD_WORKSPACE_DIRECTORY");workspace&&*workspace)resources=workspace;
    if(const char* workspace=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");workspace&&*workspace)resources=workspace;
    resources=std::filesystem::absolute(resources);
    TemporaryWorld folder;
    EnvironmentValue saveRoot("VOXY_ADVENTURE_ROOT",folder.path.c_str());
    EnvironmentValue preferences("VOXY_ADVENTURE_PREFERENCES",nullptr);
    EnvironmentValue fresh("VOXY_ADVENTURE_NEW","1");
    EnvironmentValue selected("VOXY_ADVENTURE_WORLD",nullptr);
    EnvironmentValue observation("VOXY_ADVENTURE_OBSERVE",nullptr);
    EnvironmentValue assetRoot("BUILD_WORKSPACE_DIRECTORY",resources.c_str());
    gpu::Context context;if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter.";
    const auto signals=std::make_shared<GpuSignals>();
    context.setErrorCallback([signals](WGPUErrorType,const char* message){
        ++signals->errors;std::lock_guard lock(signals->mutex);signals->message=message?message:"GPU error";
    });
    RecordProperty("adapter",context.getAdapterInfo().description);
    RecordProperty("scope","actual full-world runtime and borrowed GPU PhysicsWorld; synthetic input; no rendered/browser evidence");
    physics::PhysicsWorld world;
    physics::PhysicsInitContext init;init.requestedBackend=physics::BackendType::WebGpuSoft;
    init.device=context.getDevice();init.queue=context.getQueue();init.maxBodies=256;init.maxActiveBodies=256;
    init.maxPairs=8192;init.maxContacts=4096;init.maxManifolds=8192;
    init.gpu.authoredContactPatches=8;
    init.gpu.substeps=16;
    init.gpu.commandCapacity=4096;init.gpu.attachmentCapacity=8;init.gpu.attachmentCommandCapacity=16;
    init.gpu.asyncQueryCapacity=64;init.gpu.debugReadbackBodyCapacity=256;
    ASSERT_TRUE(world.initialize(init));
    ASSERT_TRUE(world.setLegoTerrain(samples,8192,8192,600.f,1.f));
    {
        AdventureRuntime runtime(true);std::string error;
        ASSERT_TRUE(runtime.initialize(surface,context.getDevice(),context.getQueue(),resources/"shaders",WGPUTextureFormat_RGBA8Unorm,error))<<error;
        auto read=[&]{return nlohmann::json::parse(runtime.json());};
        ASSERT_TRUE(read().at("cannon").at("available"));
        ASSERT_TRUE(read().at("blacksmith").at("available"));
        // Exercise the user-facing Visit cannon action from the normal start.
        // It must choose a supported clear landing without injecting a pose.
        auto save=runtime.state();
        runtime.attachPhysics(world);
        Input input;input.onFocusChanged(true);input.onMouseMove(640,400);Camera camera;
        auto frame=[&](bool updateRuntime=true) {
            context.tick();
            if(updateRuntime) {
                input.beginFrame();input.computeDeltas();
                runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
            }
            world.update(runtime.isPaused()||runtime.physicsWaiting()?0.f:float(AdventurePlayer::fixedStep));
            auto* pool=world.authoredShapeResources();
            if(!pool||pool->stats().phase!=physics::ShapeResourcePhase::Ready)return true;
            physics::ShapeResourceError status;const auto ticket=world.prepareGpuSubmission(status);
            if(!ticket.valid())return status==physics::ShapeResourceError::Busy||status==physics::ShapeResourceError::NotReady;
            WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
            if(!encoder){(void)world.discardGpuSubmission(ticket);return false;}
            const auto encoded=world.encodeGpuStepChecked(encoder);
            WGPUCommandBufferDescriptor cd{};const auto command=encoded.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
            wgpuCommandEncoderRelease(encoder);
            if(!command){(void)world.discardGpuSubmission(ticket);return false;}
            status=world.submitGpuSubmission(ticket,std::span(&command,1));wgpuCommandBufferRelease(command);
            if(status!=physics::ShapeResourceError::None)return false;
            const auto submitted=world.tickFrontier().submitted;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(world.tickFrontier().completed<submitted&&std::chrono::steady_clock::now()<deadline) {
                context.tick();world.update(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return !world.tickFrontier().failed&&world.tickFrontier().completed==submitted;
        };
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
        while(!read().at("cannon").at("ready").get<bool>()&&read().at("cannon").at("error").get<std::string>().empty()
            &&std::chrono::steady_clock::now()<deadline) {
            ASSERT_TRUE(frame())<<runtime.json();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(read().at("cannon").at("ready"))<<runtime.json();
        EXPECT_GE(world.stats().residentBodies,32u);
        RecordProperty("sceneBodies",std::to_string(world.stats().residentBodies));
        ASSERT_NE(world.authoredShapeResources(),nullptr);
        const auto admittedCost=world.authoredShapeResources()->stats().cpu.charged;
        RecordProperty("sceneShapeCells",std::to_string(admittedCost.cells));
        RecordProperty("sceneShapeFaces",std::to_string(admittedCost.faces));
        RecordProperty("sceneShapeNodes",std::to_string(admittedCost.nodes));
        RecordProperty("sceneShapeBytes",std::to_string(admittedCost.bytes));
        RecordProperty("retainedNearbyProps",read().at("forest").at("nearProps").get<int>());
        const auto press=[&](Key key) {
            input.onKeyDown(int(key));const bool down=frame();input.onKeyUp(int(key));return frame()&&down;
        };
        if(GetParam()==0) {
            input.onKeyDown(int(Key::LeftControl));input.onScroll(-100);ASSERT_TRUE(frame());
            input.onKeyUp(int(Key::LeftControl));ASSERT_TRUE(frame());
            EXPECT_DOUBLE_EQ(read().at("camera").at("requestedDistance").get<double>(),64.);
            ASSERT_TRUE(press(Key::B));ASSERT_EQ(read().at("mode"),"explore");
            // Throw with the pointer above the landscape: no picked surface
            // is needed, and building-only aim hints must not leak into play.
            input.onMouseMove(640,1);ASSERT_TRUE(frame());
            EXPECT_FALSE(read().at("aimRay").at("hit").get<bool>());
            EXPECT_EQ(read().at("previewReason"),"");
            input.onMouseDown(int(MouseButton::Left));ASSERT_TRUE(frame());
            ASSERT_EQ(read().at("thrownBricks").at("total"),1)<<runtime.json();
            ASSERT_TRUE(frame());EXPECT_EQ(read().at("thrownBricks").at("total"),1);
            input.onMouseUp(int(MouseButton::Left));ASSERT_TRUE(frame());
            input.onMouseDown(int(MouseButton::Right));ASSERT_TRUE(frame());
            EXPECT_EQ(read().at("thrownBricks").at("total"),1);
            input.onMouseUp(int(MouseButton::Right));ASSERT_TRUE(frame());
            ASSERT_EQ(read().at("thrownBricks").at("total"),101)<<runtime.json();
            EXPECT_EQ(read().at("thrownBricks").at("live"),101);
            EXPECT_EQ(runtime.thrownBrickBodyIds().size(),101u);
            EXPECT_TRUE(read().at("thrownBricks").at("playerCollider").get<bool>());
            input.onMouseDown(int(MouseButton::Right));input.onMouseMove(660,400);ASSERT_TRUE(frame());
            input.onMouseUp(int(MouseButton::Right));ASSERT_TRUE(frame());
            EXPECT_EQ(read().at("thrownBricks").at("total"),101);
            ASSERT_TRUE(press(Key::Escape));
            input.onMouseDown(int(MouseButton::Left));ASSERT_TRUE(frame());
            input.onMouseUp(int(MouseButton::Left));ASSERT_TRUE(frame());
            EXPECT_EQ(read().at("thrownBricks").at("total"),101);
            ASSERT_TRUE(press(Key::Escape));ASSERT_TRUE(press(Key::B));
            input.onMouseDown(int(MouseButton::Right));input.onMouseUp(int(MouseButton::Right));ASSERT_TRUE(frame());
            EXPECT_EQ(read().at("thrownBricks").at("total"),101);
            std::vector<std::byte> restoreBytes;
            ASSERT_TRUE(AdventureSaveCodec::encode(save,runtime.content(),restoreBytes,error));
            ASSERT_TRUE(runtime.restore(restoreBytes,save.world,error));ASSERT_TRUE(frame());
            EXPECT_EQ(read().at("thrownBricks").at("live"),0);
            EXPECT_TRUE(runtime.thrownBrickBodyIds().empty());
        }
        ASSERT_TRUE(press(Key::C));ASSERT_TRUE(read().at("cannon").at("active"))<<runtime.json();
        ASSERT_TRUE(read().at("cannon").at("nearby"))<<runtime.json();
        EXPECT_TRUE(runtime.spatialQueries().clearCapsule({runtime.state().player.x,runtime.state().player.y,runtime.state().player.z},1.12,4.76));
        RecordProperty("visitCannonSupportedLanding",true);
        const auto player=runtime.state().player;
        const double yaw=read().at("cannon").at("yaw").get<double>();
        const double elevation=read().at("cannon").at("elevation").get<double>();
        input.onKeyDown(int(Key::A));input.onKeyDown(int(Key::W));
        for(int i=0;i<2;++i)ASSERT_TRUE(frame());
        input.onKeyUp(int(Key::A));input.onKeyUp(int(Key::W));ASSERT_TRUE(frame());
        EXPECT_GT(read().at("cannon").at("yaw").get<double>(),yaw);
        EXPECT_GT(read().at("cannon").at("elevation").get<double>(),elevation);
        input.onKeyDown(int(Key::D));input.onKeyDown(int(Key::S));
        for(int i=0;i<2;++i)ASSERT_TRUE(frame());
        input.onKeyUp(int(Key::D));input.onKeyUp(int(Key::S));ASSERT_TRUE(frame());
        EXPECT_NEAR(read().at("cannon").at("yaw").get<double>(),yaw,1e-6);
        EXPECT_NEAR(read().at("cannon").at("elevation").get<double>(),elevation,1e-6);
        EXPECT_EQ(runtime.state().player,player); // Aiming does not walk/jump.
        auto& cannonPad=const_cast<GamepadInput&>(input.gamepad());
        GamepadSample cannonNeutral;cannonNeutral.connected=true;cannonNeutral.device=27;
        double cannonPadSeconds=0;
        const auto cannonPadFrame=[&](const GamepadSample& sample) {
            input.beginFrame();input.computeDeltas();
            cannonPad.update(sample,true,cannonPadSeconds+=AdventurePlayer::fixedStep);
            runtime.update(AdventurePlayer::fixedStep,input,camera,1280,800,{1280,800});input.endFrame();
            return frame(false);
        };
        ASSERT_TRUE(cannonPadFrame(cannonNeutral));ASSERT_TRUE(cannonPadFrame(cannonNeutral));
        auto turnUp=cannonNeutral;turnUp.axes[0]=-.6f;turnUp.axes[1]=-.6f;
        ASSERT_TRUE(cannonPadFrame(turnUp));ASSERT_TRUE(cannonPadFrame(turnUp));
        EXPECT_GT(read().at("cannon").at("yaw").get<double>(),yaw);
        EXPECT_GT(read().at("cannon").at("elevation").get<double>(),elevation);
        auto turnDown=cannonNeutral;turnDown.axes[0]=.6f;turnDown.axes[1]=.6f;
        ASSERT_TRUE(cannonPadFrame(turnDown));ASSERT_TRUE(cannonPadFrame(turnDown));ASSERT_TRUE(cannonPadFrame(cannonNeutral));
        EXPECT_NEAR(read().at("cannon").at("yaw").get<double>(),yaw,1e-6);
        EXPECT_NEAR(read().at("cannon").at("elevation").get<double>(),elevation,1e-6);
        EXPECT_EQ(runtime.state().player,player);
        const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(40);
        while(!read().at("cannon").at("ready").get<bool>()&&std::chrono::steady_clock::now()<readyDeadline)
            ASSERT_TRUE(frame());
        ASSERT_TRUE(read().at("cannon").at("ready"))<<runtime.json();
        ASSERT_TRUE(cannonPadFrame(cannonNeutral));ASSERT_TRUE(cannonPadFrame(cannonNeutral));
        auto padFire=cannonNeutral;padFire.buttons[static_cast<size_t>(PadButton::Confirm)]=true;
        ASSERT_TRUE(cannonPadFrame(padFire));ASSERT_TRUE(cannonPadFrame(cannonNeutral));
        ASSERT_EQ(read().at("cannon").at("shots"),1)<<runtime.json();
        EXPECT_EQ(read().at("cannon").at("live"),1);
        ASSERT_TRUE(press(Key::Space));EXPECT_EQ(read().at("cannon").at("shots"),1);
        EXPECT_EQ(runtime.state().player,player);
        // Real published pause action uses the same application scheduling gate.
        runtime.action(31);ASSERT_TRUE(frame());ASSERT_TRUE(runtime.isPaused());
        const auto pausedTick=world.encodedTick();
        ASSERT_TRUE(press(Key::Space));for(int i=0;i<4;++i)ASSERT_TRUE(frame());
        EXPECT_EQ(world.encodedTick(),pausedTick);EXPECT_EQ(read().at("cannon").at("shots"),1);
        runtime.action(20);ASSERT_TRUE(frame());ASSERT_FALSE(runtime.isPaused());
        // The actual imported wall uses the same borrowed world and completed
        // submission frontier. Release goes through its public HUD command.
        ASSERT_TRUE(read().at("cannon").at("wallReady"));
        EXPECT_FALSE(read().at("cannon").at("wallReleased"));
        const auto originalSpan=runtime.spatialQueries().solids();
        const std::vector<AdventureSpatialQueries::Solid> intactSolids(originalSpan.begin(),originalSpan.end());
        std::vector<std::byte> intactSave;
        ASSERT_TRUE(runtime.snapshot(intactSave,error))<<error;
        if(cannonImpact) {
            const auto impactDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            const auto beforeImpactTick=world.encodedTick();
            while(!read().at("cannon").at("wallReleased").get<bool>()
                &&world.encodedTick()-beforeImpactTick<180
                &&std::chrono::steady_clock::now()<impactDeadline) {
                ASSERT_TRUE(frame())<<runtime.json();
            }
            ASSERT_EQ(read().at("cannon").at("impacts"),1)<<runtime.json();
            RecordProperty("impactReleasedParts",read().at("cannon").at("releasedParts").get<int>());
            RecordProperty("impactEnergy",std::to_string(read().at("cannon").at("impactEnergy").get<double>()));
            RecordProperty("certifiedCannonImpacts",1);
            RecordProperty("impactDetails",read().at("cannon").dump());
        } else {runtime.action(removeSupport?37:35);ASSERT_TRUE(frame());}
        ASSERT_TRUE(read().at("cannon").at("wallReleased"))<<runtime.json();
        ASSERT_TRUE(read().at("cannon").at("wallBusy"))<<runtime.json();
        EXPECT_TRUE(read().at("dirty"));
        const auto releaseTick=world.encodedTick();
        ASSERT_TRUE(frame());
        EXPECT_TRUE(read().at("cannon").at("inspectingWall"));
        const auto closeView=read().at("camera").at("viewTarget");
        EXPECT_LT(std::abs(closeView[0].get<double>()-read().at("blacksmith").at("x").get<double>()),22.);
        EXPECT_GT(closeView[1].get<double>(),read().at("blacksmith").at("y").get<double>());
        const glm::dvec3 cameraAbsolute=glm::dvec3(camera.worldSector())*double(physics::kWorldSectorSize)+glm::dvec3(camera.position());
        EXPECT_LT(glm::length(cameraAbsolute-glm::dvec3(closeView[0].get<double>(),closeView[1].get<double>(),closeView[2].get<double>())),25.);
        ASSERT_TRUE(press(Key::C));EXPECT_TRUE(read().at("cannon").at("active"));
        ASSERT_TRUE(press(Key::Space));EXPECT_EQ(read().at("cannon").at("shots"),1);
        EXPECT_EQ(read().at("cannon").at("live"),0);
        EXPECT_EQ(runtime.state().player,player);
        std::vector<std::byte> refusedSave;
        EXPECT_FALSE(runtime.snapshot(refusedSave,error));EXPECT_FALSE(error.empty());
        EXPECT_FALSE(runtime.restore(intactSave,save.world,error));
        EXPECT_TRUE(read().at("cannon").at("wallReleased"));
        // Pausing also pauses falling pieces; a paused frame cannot certify
        // new simulation or silently restore the intact wall.
        runtime.action(31);ASSERT_TRUE(frame());ASSERT_TRUE(runtime.isPaused());
        const auto fallingPausedTick=world.encodedTick();
        for(int i=0;i<4;++i)ASSERT_TRUE(frame());
        EXPECT_EQ(world.encodedTick(),fallingPausedTick);
        EXPECT_TRUE(read().at("cannon").at("wallReleased"));
        runtime.action(20);ASSERT_TRUE(frame());
        if(std::getenv("VOXY_WALL_ACTIVITY_TRACE")) {
            // Bounded opt-in diagnostics borrow the shared body readback while
            // the wall is falling. The owner resumes normally afterward.
            for(int i=0;i<100&&read().at("cannon").at("wallPhase")!=4;++i)ASSERT_TRUE(frame());
            ASSERT_EQ(read().at("cannon").at("wallPhase"),4);
            auto trace=nlohmann::json::array();
            for(int tick=0;tick<120;++tick) {
                world.requestDebugSnapshot({1,256});ASSERT_TRUE(frame(false));
                const auto requested=world.encodedTick();
                const auto readDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                bool captured=false;
                while(std::chrono::steady_clock::now()<readDeadline) {
                    context.tick();world.update(0);
                    auto snapshot=world.pollDebugSnapshot();
                    if(snapshot&&snapshot->tick>=requested) {
                        for(const auto& body:snapshot->bodies)if(body.alive&&glm::length(body.inverseInertia)>0) {
                            const auto p=physics::worldPositionToAbsolute({body.sector,body.position});
                            const auto* shape=world.authoredShapeResources()->get(body.authoredShape);
                            trace.push_back({{"tick",snapshot->tick},{"body",body.handle.index},{"awake",body.awake},
                                {"sourceLabel",shape&&!shape->cells().empty()?shape->cells().front().source:0},
                                {"p",{p.x,p.y,p.z}},
                                {"q",{body.orientation.w,body.orientation.x,body.orientation.y,body.orientation.z}},
                                {"v",{body.linearVelocity.x,body.linearVelocity.y,body.linearVelocity.z}},
                                {"w",{body.angularVelocity.x,body.angularVelocity.y,body.angularVelocity.z}}});
                        }
                        captured=true;break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                ASSERT_TRUE(captured);
            }
            RecordProperty("wallEarlyActivityTrace",trace.dump());
        }
        const auto settleDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
        while(!read().at("cannon").at("wallReady").get<bool>()&&world.encodedTick()-releaseTick<1200
            &&std::chrono::steady_clock::now()<settleDeadline) {
            ASSERT_TRUE(frame())<<runtime.json();
            // Readback and retirement are asynchronous, so service frames are
            // not simulation ticks and must not consume the 1200-tick budget.
            if(runtime.physicsWaiting())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        RecordProperty("wallSettlementTicks",std::to_string(world.encodedTick()-releaseTick));
        std::string unsettledBodies;
        if(!read().at("cannon").at("wallReady").get<bool>()) {
            // Failure-only diagnostics deliberately bypass the runtime owner
            // for one submission so it cannot consume this shared debug readback.
            world.requestDebugSnapshot({1,256});ASSERT_TRUE(frame(false));
            const auto target=world.encodedTick();
            const auto diagnosticDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(std::chrono::steady_clock::now()<diagnosticDeadline) {
                context.tick();world.update(0);
                const auto capturedBodies=world.pollDebugSnapshot();
                if(capturedBodies&&capturedBodies->tick>=target) {
                    auto bodies=nlohmann::json::array();
                    for(const auto& body:capturedBodies->bodies)if(body.alive&&glm::length(body.inverseInertia)>0) {
                        const auto p=physics::worldPositionToAbsolute({body.sector,body.position});
                        bodies.push_back({{"body",body.handle.index},{"awake",body.awake},
                            {"position",{p.x,p.y,p.z}},
                            {"linearVelocity",{body.linearVelocity.x,body.linearVelocity.y,body.linearVelocity.z}},
                            {"angularVelocity",{body.angularVelocity.x,body.angularVelocity.y,body.angularVelocity.z}},
                            {"contacts",body.staticContactCount},{"flags",body.runtimeFlags}});
                    }
                    unsettledBodies=bodies.dump();RecordProperty("unsettledBodies",unsettledBodies);break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const bool physicallySettled=read().at("cannon").at("wallReady").get<bool>();
        EXPECT_TRUE(physicallySettled)<<"encoded="<<world.encodedTick()<<" release="<<releaseTick
            <<" physicsWaiting="<<runtime.physicsWaiting()<<" bodies="<<unsettledBodies<<' '<<runtime.json();
        EXPECT_TRUE(read().at("cannon").at("wallReleased"));
        EXPECT_EQ(runtime.state().player,player);
        EXPECT_FALSE(runtime.snapshot(refusedSave,error)); // Settling is still session-only.
        if(physicallySettled) {
        EXPECT_FALSE(read().at("cannon").at("wallBusy"));
        const auto settledSolids=runtime.spatialQueries().solids();
        size_t changedCells=intactSolids.size()>settledSolids.size()?intactSolids.size()-settledSolids.size():settledSolids.size()-intactSolids.size();
        for(size_t i=0;i<std::min(intactSolids.size(),settledSolids.size());++i)
            if(intactSolids[i].part!=settledSolids[i].part||glm::length(intactSolids[i].minimum-settledSolids[i].minimum)>.001
                ||glm::length(intactSolids[i].maximum-settledSolids[i].maximum)>.001)++changedCells;
        EXPECT_GT(changedCells,0u)<<"Settled walking geometry must not keep the old intact wall.";
        RecordProperty("wallChangedQueryCells",std::to_string(changedCells));
        size_t vacatedCenters=0, vacatedUpperCenters=0;
        const auto house=read().at("blacksmith");
        const glm::dvec3 upperBrick(house.at("x").get<double>()+4.399569,
            house.at("y").get<double>()+1.999990,house.at("z").get<double>()+1.038921);
        for(const auto& old:intactSolids) {
            const auto center=(old.minimum+old.maximum)*.5;
            const bool occupied=std::any_of(settledSolids.begin(),settledSolids.end(),[&](const auto& now) {
                return glm::all(glm::greaterThanEqual(center,now.minimum-glm::dvec3(.005)))
                    &&glm::all(glm::lessThanEqual(center,now.maximum+glm::dvec3(.005)));
            });
            if(!occupied) {
                ++vacatedCenters;
                // Only the surviving brick ABOVE the removed support qualifies.
                // The removed brick's own empty cells cannot prove gravity motion.
                if(std::abs(center.x-upperBrick.x)<.51&&std::abs(center.z-upperBrick.z)<3.01
                    &&center.y>upperBrick.y-1.15&&center.y<upperBrick.y-.05)++vacatedUpperCenters;
            }
        }
        // A different cell partition/order alone is not evidence of motion:
        // require previously occupied volume to become physically vacant.
        if(cannonImpact) {
            EXPECT_GT(vacatedCenters,4u)<<"A cannon hit must leave visibly empty source volume.";
            // Stud collision removal alone must not satisfy visual acceptance.
            const auto displacement=read().at("cannon").at("partDisplacement").get<double>();
            EXPECT_GT(displacement,.5)<<"An authentic rendered part must move visibly.";
            RecordProperty("maximumSourcePartDisplacement",displacement);
        }
        if(removeSupport) {
            // This ground-floor plate has neighboring bearing masonry; unlike
            // the earlier isolated upper 1x1, an upper drop is not guaranteed.
            EXPECT_EQ(read().at("cannon").at("sourceParts"),19);
            EXPECT_GT(vacatedCenters,0u)<<"The deleted source plate must leave empty collision volume.";
        }
        // A pure bond cut does not remove the source house's bearing masonry.
        // Supported pieces are allowed to settle at their original poses.
        RecordProperty("supportRemoved",removeSupport);
        RecordProperty("wallVacatedUpperBrickCenters",std::to_string(vacatedUpperCenters));
        RecordProperty("wallVacatedCellCenters",std::to_string(vacatedCenters));
        }
        // Restoring the source assembly must complete before walking or saves
        // become normal again. No fixture teleport is used during this change.
        runtime.action(36);ASSERT_TRUE(frame());
        const auto rebuildDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!read().at("cannon").at("wallReady").get<bool>()&&std::chrono::steady_clock::now()<rebuildDeadline) {
            ASSERT_TRUE(frame());
            if(runtime.physicsWaiting())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(read().at("cannon").at("wallReady"))<<runtime.json();
        EXPECT_FALSE(read().at("cannon").at("wallReleased"));
        ASSERT_TRUE(runtime.snapshot(refusedSave,error))<<error;
        RecordProperty("wallRebuildSaveRecovered",true);
        EXPECT_FALSE(read().at("cannon").at("inspectingWall"));
        const auto rebuiltSolids=runtime.spatialQueries().solids();
        ASSERT_EQ(rebuiltSolids.size(),intactSolids.size());
        for(size_t i=0;i<intactSolids.size();++i) {
            EXPECT_EQ(rebuiltSolids[i].part,intactSolids[i].part);
            EXPECT_LT(glm::length(rebuiltSolids[i].minimum-intactSolids[i].minimum),.0001);
            EXPECT_LT(glm::length(rebuiltSolids[i].maximum-intactSolids[i].maximum),.0001);
        }
        ASSERT_TRUE(press(Key::C));EXPECT_FALSE(read().at("cannon").at("active"));
        ASSERT_TRUE(press(Key::B));EXPECT_EQ(read().at("mode"),"build");
        bool aim=false;
        for(int py:{560,640,480,400}) {
            for(int px:{640,480,800,320,960}) {
                input.onMouseMove(float(px),float(py));ASSERT_TRUE(frame());
                if(read().at("valid").get<bool>()){aim=true;break;}
            }
            if(aim)break;
        }
        ASSERT_TRUE(aim)<<runtime.json();const auto count=runtime.state().structures.size();
        runtime.action(4);ASSERT_TRUE(frame());
        EXPECT_EQ(runtime.state().structures.size(),count+1)<<runtime.json();
        EXPECT_EQ(read().at("cannon").at("shots"),1);
        RecordProperty("shotsFired",1);
        RecordProperty("physicsTicks",std::to_string(world.encodedTick()));
        // No submission is unresolved. Runtime releases its handles first;
        // immediate world shutdown below owns final pool abandonment, exactly
        // as Application shutdown. No continued simulation reuses those handles.
    }
    world.shutdown();context.tick();
    {std::lock_guard lock(signals->mutex);EXPECT_EQ(signals->errors.load(),0u)<<signals->message;}
#endif
}

INSTANTIATE_TEST_SUITE_P(SourceHouse,ImportedWallRuntimeIntegration,testing::Values(0,1,2),
    [](const testing::TestParamInfo<int>& variant){return variant.param==2?"CannonImpact":variant.param==1?"RemoveSupport":"ReleaseConnections";});


} // namespace voxy::game::adventure

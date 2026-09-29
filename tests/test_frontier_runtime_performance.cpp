#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/world_definition.hpp"
#include "game/adventure/frontier_interactions.hpp"
#include "engine/platform/input.hpp"
#include "camera/camera.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include <gtest/gtest.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace voxy::game::adventure {
namespace {
struct SavedEnvironment {
    std::string key;
    std::optional<std::string> old;
    SavedEnvironment(const char* name,const char* value):key(name) {
        if(const char* previous=std::getenv(name))old=previous;
        if((value ? ::setenv(name,value,1) : ::unsetenv(name))!=0)
            throw std::runtime_error("Could not isolate Frontier performance test environment");
    }
    ~SavedEnvironment(){if(old)(void)::setenv(key.c_str(),old->c_str(),1);else (void)::unsetenv(key.c_str());}
};
struct IsolatedFrontierWorld {
    std::filesystem::path path;
    IsolatedFrontierWorld() {
        std::string pattern="/tmp/voxys-frontier-performance-XXXXXX";
        const char* created=::mkdtemp(pattern.data());
        if(!created)throw std::runtime_error("Could not isolate Frontier performance test save directory");
        path=created;
    }
    ~IsolatedFrontierWorld(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct HudTarget {
    WGPUTexture texture=nullptr;
    WGPUTextureView view=nullptr;
    WGPUBuffer readback=nullptr;
    static constexpr uint32_t width=1280,height=800,stride=width*4;
    ~HudTarget(){if(readback)wgpuBufferRelease(readback);if(view)wgpuTextureViewRelease(view);if(texture)wgpuTextureRelease(texture);}
    bool initialize(WGPUDevice device) {
        auto descriptor=gpu::TextureDesc::renderTarget(width,height,WGPUTextureFormat_RGBA8Unorm,"frontier_hud_cache_test");
        descriptor.usage|=WGPUTextureUsage_CopySrc;
        texture=gpu::createTexture(device,descriptor);
        if(texture)view=gpu::createTextureView(texture);
        readback=gpu::createBuffer(device,{"frontier_hud_cache_readback",uint64_t{stride}*height,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst});
        return texture&&view&&readback;
    }
    bool draw(AdventureRuntime& runtime,gpu::Context& context,bool submit=true,uint32_t drawWidth=width,uint32_t drawHeight=height) {
        const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),nullptr);
        if(!encoder)return false;
        WGPURenderPassColorAttachment attachment{};attachment.view=view;
        attachment.loadOp=WGPULoadOp_Clear;attachment.storeOp=WGPUStoreOp_Store;attachment.clearValue={.04,.05,.06,1};
        WGPURenderPassDescriptor descriptor{};descriptor.colorAttachmentCount=1;descriptor.colorAttachments=&attachment;
        const auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&descriptor);
        if(!pass){wgpuCommandEncoderRelease(encoder);return false;}
        wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
        const bool drawn=runtime.renderHud(encoder,view,drawWidth,drawHeight);
        if(!submit){wgpuCommandEncoderRelease(encoder);return drawn;}
        const auto commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuCommandEncoderRelease(encoder);
        if(commands){wgpuQueueSubmit(context.getQueue(),1,&commands);wgpuCommandBufferRelease(commands);}
        return drawn&&commands;
    }
    std::vector<uint8_t> pixels(gpu::Context& context) {
        const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),nullptr);
        if(!encoder){ADD_FAILURE()<<"No HUD readback encoder";return {};}
        WGPUImageCopyTexture source{};source.texture=texture;source.aspect=WGPUTextureAspect_All;
        WGPUImageCopyBuffer destination{};destination.buffer=readback;destination.layout.bytesPerRow=stride;destination.layout.rowsPerImage=height;
        const WGPUExtent3D extent{width,height,1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        const auto commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuCommandEncoderRelease(encoder);
        if(!commands){ADD_FAILURE()<<"No HUD readback commands";return {};}
        wgpuQueueSubmit(context.getQueue(),1,&commands);wgpuCommandBufferRelease(commands);
        auto completion=std::make_shared<std::atomic<int>>(0);
        auto* callback=new std::shared_ptr<std::atomic<int>>(completion);
        wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,uint64_t{stride}*height,
            [](WGPUBufferMapAsyncStatus status,void* data){
                std::unique_ptr<std::shared_ptr<std::atomic<int>>> result(static_cast<std::shared_ptr<std::atomic<int>>*>(data));
                (*result)->store(status==WGPUBufferMapAsyncStatus_Success?1:2);
            },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        while(completion->load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(completion->load()!=1){ADD_FAILURE()<<"HUD readback failed or timed out";return {};}
        const auto* data=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readback,0,uint64_t{stride}*height));
        if(!data){ADD_FAILURE()<<"HUD readback mapping was empty";return {};}
        std::vector<uint8_t> result(data,data+size_t{stride}*height);
        wgpuBufferUnmap(readback);return result;
    }
};
nlohmann::json sampleDistribution(std::vector<double> samples) {
    std::sort(samples.begin(),samples.end());
    const auto at=[&](double fraction){return samples.at(static_cast<size_t>(std::ceil(fraction*double(samples.size())))-1);};
    return {{"count",samples.size()},{"p50_us",at(.5)},{"p95_us",at(.95)},{"p99_us",at(.99)}};
}
}

// The complete runtime owns the content publication here. A standalone HUD
// equality test cannot catch a generic packet published before every final
// Frontier packet. Real installed geometry and actual rendered HUD work are
// required; synthetic library input does not imply physical controller coverage.
TEST(FrontierRuntimePerformance, IdleHudReusesUploadsAndVisibleChangesRemainAuthoritative) {
#if !defined(VOXY_NATIVE)
    GTEST_SKIP()<<"Requires native runtime component integration.";
#else
    const char* terrainPath=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!terrainPath||!*terrainPath)GTEST_SKIP()<<"Set VOXY_ADVENTURE_TEST_TERRAIN to the installed full raw terrain.";
    std::vector<uint16_t> samples(8192u*8192u);
    std::ifstream terrainFile(terrainPath,std::ios::binary);
    ASSERT_TRUE(terrainFile.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(terrainFile.peek(),std::char_traits<char>::eof());
    static_assert(std::endian::native==std::endian::little);
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    auto workspace=std::filesystem::current_path();
    if(const char* root=std::getenv("BUILD_WORKSPACE_DIRECTORY");root&&*root)workspace=root;
    if(const char* root=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");root&&*root)workspace=root;
    workspace=std::filesystem::absolute(workspace);
    IsolatedFrontierWorld folder;
    SavedEnvironment saveRoot("VOXY_ADVENTURE_ROOT",folder.path.c_str());
    SavedEnvironment preferenceRoot("VOXY_ADVENTURE_PREFERENCES",nullptr);
    SavedEnvironment newWorld("VOXY_ADVENTURE_NEW","1");
    SavedEnvironment selectedWorld("VOXY_ADVENTURE_WORLD",nullptr);
    SavedEnvironment observe("VOXY_ADVENTURE_OBSERVE",nullptr);
    SavedEnvironment resources("BUILD_WORKSPACE_DIRECTORY",workspace.c_str());
    gpu::Context context;
    if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter.";
    auto gpuErrors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([gpuErrors](WGPUErrorType,const char*){++*gpuErrors;});
    RecordProperty("adapter",context.getAdapterInfo().description);
    RecordProperty("scope","actual Frontier runtime; HUD CPU preparation and uploads; no world rendering or displayed FPS");
    {
        AdventureRuntime runtime(true,true);std::string error;
        ASSERT_TRUE(runtime.initialize(surface,context.getDevice(),context.getQueue(),workspace/"shaders",WGPUTextureFormat_RGBA8Unorm,error))<<error;
        ASSERT_TRUE(runtime.content().frontier);
        HudTarget target;ASSERT_TRUE(target.initialize(context.getDevice()));
        Input input;input.onFocusChanged(true);input.onMouseMove(640,360);Camera camera;
        const auto update=[&](double seconds=AdventurePlayer::fixedStep,uint32_t width=HudTarget::width,uint32_t height=HudTarget::height) {
            input.beginFrame();input.computeDeltas();
            runtime.update(seconds,input,camera,width,height,{width,height});
            input.endFrame();
        };
        const auto draw=[&](bool submit=true,uint32_t width=HudTarget::width,uint32_t height=HudTarget::height) {
            return target.draw(runtime,context,submit,width,height);
        };
        const auto read=[&]{return nlohmann::json::parse(runtime.json());};
        const auto pixels=[&]{return target.pixels(context);};
        // Let camera damping and startup feedback expire before the cache guard.
        for(int frame=0;frame<720;++frame)update();
        ASSERT_TRUE(draw());update();ASSERT_TRUE(draw());
        const auto before=read();const auto initialPixels=pixels();ASSERT_FALSE(initialPixels.empty());
        if(const char* output=std::getenv("VOXY_FRONTIER_QUERY_FIXTURE");output&&*output) {
            std::ofstream fixture(output);fixture<<std::setprecision(17);
            const auto& accepted=runtime.state();
            fixture<<"POINT "<<accepted.player.x<<' '<<accepted.player.y<<' '<<accepted.player.z<<'\n';
            for(const auto& enemy:runtime.content().frontierEnemies)
                fixture<<"POINT "<<enemy.spawn.x<<' '<<enemy.spawn.y<<' '<<enemy.spawn.z<<'\n';
            for(const auto& box:runtime.spatialQueries().solids())
                fixture<<"BOX "<<box.structure.counter<<' '<<box.part.counter<<' '
                    <<box.minimum.x<<' '<<box.minimum.y<<' '<<box.minimum.z<<' '
                    <<box.maximum.x<<' '<<box.maximum.y<<' '<<box.maximum.z<<'\n';
            ASSERT_TRUE(fixture);
        }
        const auto uploads=runtime.hudUploadCount(),triangles=runtime.hudTriangleUploadCount();
        for(int frame=0;frame<120;++frame){update();ASSERT_TRUE(draw());if(frame%30==0)context.tick();}
        EXPECT_EQ(runtime.hudUploadCount(),uploads);
        EXPECT_EQ(runtime.hudTriangleUploadCount(),triangles);
        EXPECT_EQ(read().at("hud").at("controls"),before.at("hud").at("controls"));
        EXPECT_EQ(pixels(),initialPixels);
        EXPECT_EQ(runtime.state().health,100);
        EXPECT_GT(runtime.state().player.x,1100);
        EXPECT_GT(std::stoull(read().at("player").at("tick").get<std::string>()),std::stoull(before.at("player").at("tick").get<std::string>()));

        runtime.action(31);update();ASSERT_TRUE(draw());EXPECT_TRUE(runtime.isPaused());
        EXPECT_GT(runtime.hudUploadCount(),uploads);EXPECT_NE(pixels(),initialPixels);
        const auto paused=runtime.hudUploadCount();
        for(int frame=0;frame<12;++frame){update();ASSERT_TRUE(draw());}
        EXPECT_EQ(runtime.hudUploadCount(),paused);
        runtime.action(20);update();ASSERT_TRUE(draw());EXPECT_FALSE(runtime.isPaused());
        runtime.action(21);update(0);ASSERT_TRUE(draw());EXPECT_EQ(read().at("mode"),"build");
        const auto buildUploads=runtime.hudUploadCount(),buildTriangles=runtime.hudTriangleUploadCount();
        const auto buildPixels=pixels();EXPECT_GT(runtime.hudThumbnailCount(),0u);
        runtime.action(39,2);update(0);ASSERT_TRUE(draw());
        EXPECT_EQ(read().at("quickSlot"),2);EXPECT_GT(runtime.hudUploadCount(),buildUploads);
        const auto selectedPixels=pixels();EXPECT_NE(selectedPixels,buildPixels);
        // Frontier thumbnails are atlas quads. Empty triangle buffers are
        // valid in exploration, building, catalogue and the colour picker.
        EXPECT_GT(runtime.hudThumbnailCount(),0u);EXPECT_EQ(runtime.hudTriangleUploadCount(),buildTriangles);
        const auto selectedCount=runtime.hudUploadCount();
        runtime.action(13,1);update(0);ASSERT_TRUE(draw());
        EXPECT_GT(runtime.hudThumbnailCount(),0u);EXPECT_GT(runtime.hudUploadCount(),selectedCount);
        const auto catalogPixels=pixels();EXPECT_NE(catalogPixels,selectedPixels);
        const auto catalogCount=runtime.hudUploadCount();
        runtime.action(25,1);update(0);ASSERT_TRUE(draw());
        EXPECT_GT(runtime.hudUploadCount(),catalogCount);EXPECT_NE(pixels(),catalogPixels);
        runtime.action(20);update(0);ASSERT_TRUE(draw());
        const auto unpaintedPixels=pixels();const auto unpaintedCount=runtime.hudUploadCount();
        runtime.action(38);update(0);ASSERT_TRUE(draw());
        EXPECT_TRUE(read().at("colourPickerOpen").get<bool>());EXPECT_GT(runtime.hudThumbnailCount(),0u);
        EXPECT_GT(runtime.hudUploadCount(),unpaintedCount);EXPECT_NE(pixels(),unpaintedPixels);
        const auto pickerCount=runtime.hudUploadCount();
        runtime.action(30,0x1060d0);update(0);ASSERT_TRUE(draw());
        EXPECT_FALSE(read().at("colourPickerOpen").get<bool>());EXPECT_EQ(read().at("paint"),0x1060d0);
        EXPECT_GT(runtime.hudUploadCount(),pickerCount);EXPECT_NE(pixels(),unpaintedPixels);
        EXPECT_EQ(runtime.hudTriangleUploadCount(),buildTriangles);
        auto controls=read().at("hud").at("controls");ASSERT_FALSE(controls.empty());
        const auto selectedUploads=runtime.hudUploadCount();
        update(0,960,600);ASSERT_TRUE(draw(true,960,600));
        EXPECT_GT(runtime.hudUploadCount(),selectedUploads);EXPECT_NE(read().at("hud").at("controls"),controls);
        EXPECT_EQ(read().at("hud").at("width"),960);EXPECT_EQ(read().at("hud").at("height"),600);
        std::vector<std::byte> snapshot;ASSERT_TRUE(runtime.snapshot(snapshot,error))<<error;
        const auto saved=runtime.state();ASSERT_TRUE(runtime.restore(snapshot,saved.world,error))<<error;
        EXPECT_EQ(runtime.state(),saved);update();ASSERT_TRUE(draw());EXPECT_EQ(read().at("mode"),"explore");

        // Optional same-binary microbenchmark isolates content preparation and
        // command encoding. Commands are discarded; no GPU/FPS claim is made.
        if(const char* output=std::getenv("VOXY_FRONTIER_HUD_BENCHMARK");output&&*output) {
            for(int frame=0;frame<300;++frame)update();
            for(int frame=0;frame<100;++frame){update(0);ASSERT_TRUE(draw(false));}
            const auto firstUpload=runtime.hudUploadCount(),firstTriangle=runtime.hudTriangleUploadCount();
            std::vector<double> timings;timings.reserve(1200);
            for(int frame=0;frame<1200;++frame) {
                const auto begin=std::chrono::steady_clock::now();update(0);ASSERT_TRUE(draw(false));
                timings.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count());
            }
            nlohmann::json result={{"scope","Frontier HUD update and command encoding, frozen simulation, discarded command encoders"},
                {"cpu",sampleDistribution(std::move(timings))},{"hudUploads",runtime.hudUploadCount()-firstUpload},
                {"triangleUploads",runtime.hudTriangleUploadCount()-firstTriangle}};
            std::ofstream report(output);report<<result.dump(2)<<'\n';ASSERT_TRUE(report);
            RecordProperty("hudCpuBenchmark",result.dump());
        }
        wgpuQueueSubmit(context.getQueue(),0,nullptr);
        auto completion=std::make_shared<std::atomic<int>>(0);
        auto* callback=new std::shared_ptr<std::atomic<int>>(completion);
        wgpuQueueOnSubmittedWorkDone(context.getQueue(),[](WGPUQueueWorkDoneStatus status,void* data){
            std::unique_ptr<std::shared_ptr<std::atomic<int>>> result(static_cast<std::shared_ptr<std::atomic<int>>*>(data));
            (*result)->store(status==WGPUQueueWorkDoneStatus_Success?1:2);
        },callback);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        while(completion->load()==0&&std::chrono::steady_clock::now()<deadline){context.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_EQ(completion->load(),1);
    }
    context.tick();EXPECT_EQ(gpuErrors->load(),0u);context.setErrorCallback({});
#endif
}

TEST(FrontierRuntimePerformance, DeterministicPublicReplayProducesExactStateAndHudOracle) {
#if !defined(VOXY_NATIVE)
    GTEST_SKIP()<<"Requires native runtime component integration.";
#else
    const char* output=std::getenv("VOXY_FRONTIER_RUNTIME_ORACLE");
    if(!output||!*output)GTEST_SKIP()<<"Opt in with VOXY_FRONTIER_RUNTIME_ORACLE set to an output prefix.";
    const char* terrainPath=std::getenv("VOXY_ADVENTURE_TEST_TERRAIN");
    if(!terrainPath||!*terrainPath)GTEST_SKIP()<<"Set VOXY_ADVENTURE_TEST_TERRAIN to installed full raw terrain.";
    const bool nearby=std::getenv("VOXY_FRONTIER_RUNTIME_ORACLE_NEARBY")!=nullptr;
    std::vector<uint16_t> samples(8192u*8192u);
    std::ifstream terrainFile(terrainPath,std::ios::binary);
    ASSERT_TRUE(terrainFile.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(uint16_t))));
    ASSERT_EQ(terrainFile.peek(),std::char_traits<char>::eof());
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    auto workspace=std::filesystem::current_path();
    if(const char* root=std::getenv("BUILD_WORKSPACE_DIRECTORY");root&&*root)workspace=root;
    if(const char* root=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");root&&*root)workspace=root;
    workspace=std::filesystem::absolute(workspace);
    IsolatedFrontierWorld folder;
    SavedEnvironment saveRoot("VOXY_ADVENTURE_ROOT",folder.path.c_str());
    SavedEnvironment preferences("VOXY_ADVENTURE_PREFERENCES",nullptr);
    SavedEnvironment newWorld("VOXY_ADVENTURE_NEW","1");
    SavedEnvironment selectedWorld("VOXY_ADVENTURE_WORLD",nullptr);
    SavedEnvironment observe("VOXY_ADVENTURE_OBSERVE",nullptr);
    SavedEnvironment resources("BUILD_WORKSPACE_DIRECTORY",workspace.c_str());
    gpu::Context context;
    if(!context.initHeadless())GTEST_SKIP()<<"No headless WebGPU adapter.";
    auto gpuErrors=std::make_shared<std::atomic<unsigned>>(0);
    context.setErrorCallback([gpuErrors](WGPUErrorType,const char*){++*gpuErrors;});
    {
        AdventureRuntime runtime(true,true);std::string error;
        ASSERT_TRUE(runtime.initialize(surface,context.getDevice(),context.getQueue(),workspace/"shaders",WGPUTextureFormat_RGBA8Unorm,error))<<error;
        auto fixture=runtime.state();fixture.world.bytes.fill(0x42);
        if(nearby) {
            ASSERT_FALSE(runtime.content().frontierEnemies.empty());
            const auto spawn=runtime.content().frontierEnemies.front().spawn;
            bool admitted=false;
            for(const double angle:std::array<double,4>{std::numbers::pi,std::numbers::pi*.5,-std::numbers::pi*.5,0}) {
                const glm::dvec2 point{spawn.x+std::cos(angle)*12,spawn.z+std::sin(angle)*12};
                const auto& queries=runtime.spatialQueries();
                const double support=queries.supportHeight(point,1.12,spawn.y+3);
                const glm::dvec3 feet{point.x,support+.005,point.y};
                if(!std::isfinite(support)||std::abs(support-spawn.y)>=2.5
                    ||!queries.clearCapsule(feet,1.12,4.76)
                    ||!frontierCombatSight(queries,feet,{spawn.x,spawn.y,spawn.z}))continue;
                fixture.player={point.x,support+.005,point.y,fixture.player.yaw};admitted=true;break;
            }
            ASSERT_TRUE(admitted)<<"The installed enemy approach has no clear supported oracle pose";
        }
        std::vector<std::byte> beginning;
        ASSERT_TRUE(AdventureSaveCodec::encode(fixture,runtime.content(),beginning,error))<<error;
        ASSERT_TRUE(runtime.restore(beginning,fixture.world,error))<<error;
        ASSERT_EQ(runtime.state(),fixture);
        HudTarget target;ASSERT_TRUE(target.initialize(context.getDevice()));
        Input input;input.onFocusChanged(true);input.onMouseMove(640,360);Camera camera;
        std::ofstream observations(std::string(output)+".jsonl",std::ios::binary);ASSERT_TRUE(observations);
        auto hashes=nlohmann::json::array();bool enemyAttackObserved=false;
        std::vector<double> simulationCpu,pausedCpu;
        simulationCpu.reserve(1200);pausedCpu.reserve(1200);
        for(int frame=0;frame<1200;++frame) {
            // This replay uses accepted public actions and library Input. It
            // never edits actor state, requests a terrain proxy or disables AI.
            // Modal pauses bound combat exposure and avoid defeat/autosave I/O
            // races in a byte-for-byte observer oracle. They are real game rules.
            if(!nearby&&frame==30)input.onKeyDown(static_cast<int>(Key::S));
            if(!nearby&&frame==60)input.onKeyUp(static_cast<int>(Key::S));
            if(frame==120)runtime.action(28);
            if(frame==180)runtime.action(31);
            if(frame==300)runtime.action(20);
            if(frame==301)runtime.action(21);
            if(frame==302)runtime.action(39,2);
            if(frame==330)runtime.action(13,1);
            if(frame==360)runtime.action(20);
            if(frame==361)runtime.action(31);
            input.beginFrame();input.computeDeltas();
            const auto updateStart=std::chrono::steady_clock::now();
            runtime.update(AdventurePlayer::fixedStep,input,camera,HudTarget::width,HudTarget::height,{HudTarget::width,HudTarget::height});
            const double updateUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-updateStart).count();
            if(frame>=60)(runtime.isPaused()?pausedCpu:simulationCpu).push_back(updateUs);
            input.endFrame();
            ASSERT_TRUE(target.draw(runtime,context));
            const auto observation=runtime.json();observations<<observation<<'\n';
            const auto parsed=nlohmann::json::parse(observation);
            const auto event=parsed.at("audioEvent").get<std::string>();
            enemyAttackObserved|=event=="warn"||event=="charge"||event=="hurt";
            ASSERT_GT(runtime.state().health,0)<<"Oracle exposure unexpectedly defeated the player";
            if(frame%60==0||frame==1199) {
                const auto pixels=target.pixels(context);ASSERT_FALSE(pixels.empty());
                hashes.push_back({{"frame",frame},{"sha256",core::sha256Hex(core::sha256(std::as_bytes(std::span(pixels))))}});
            }
        }
        ASSERT_TRUE(observations);
        if(nearby){EXPECT_TRUE(enemyAttackObserved)<<"The nearby fixture never exercised an enemy attack";}
        EXPECT_TRUE(runtime.isPaused());EXPECT_EQ(runtime.state().world,fixture.world);
        std::vector<std::byte> final;ASSERT_TRUE(runtime.snapshot(final,error))<<error;
        AdventureState decoded;ASSERT_TRUE(AdventureSaveCodec::decode(final,fixture.world,runtime.content(),decoded,error))<<error;
        EXPECT_EQ(decoded,runtime.state());
        std::ofstream archive(std::string(output)+".save",std::ios::binary);
        archive.write(reinterpret_cast<const char*>(final.data()),std::streamsize(final.size()));ASSERT_TRUE(archive);
        nlohmann::json report={{"scope","1200 fixed-step public Frontier updates including normal modal pauses; actual GPU HUD pixels; no world renderer or FPS"},
            {"nearbyEnemy",nearby},{"enemyAttackObserved",enemyAttackObserved},{"frames",1200},{"pixelHashes",hashes},
            {"archiveSha256",core::sha256Hex(core::sha256(final))},{"hudUploads",runtime.hudUploadCount()},
            {"hudTriangleUploads",runtime.hudTriangleUploadCount()},{"adapter",context.getAdapterInfo().description}};
        report["cpuScope"]="Native AdventureRuntime::update only, excludes HUD draw/JSON/readback; first 60 updates excluded; no displayed FPS";
        report["simulationCpu"]=sampleDistribution(std::move(simulationCpu));
        report["pausedCpu"]=sampleDistribution(std::move(pausedCpu));
        std::ofstream summary(std::string(output)+".pixels.json");summary<<report.dump(2)<<'\n';ASSERT_TRUE(summary);
        RecordProperty("oracle",report.dump());
    }
    context.tick();EXPECT_EQ(gpuErrors->load(),0u);context.setErrorCallback({});
#endif
}
} // namespace voxy::game::adventure

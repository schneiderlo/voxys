// Deterministic headless component benchmark. This is not a renderer/FPS test.
#include "game/adventure/adventure_runtime.hpp"
#include "game/adventure/adventure_save.hpp"
#include "game/adventure/construction_policy.hpp"
#include "game/adventure/world_definition.hpp"
#include "engine/platform/input.hpp"
#include "camera/camera.hpp"
#include "gpu/context.hpp"
#include <json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <sys/resource.h>

using namespace voxy;
using namespace voxy::game::adventure;
using Clock=std::chrono::steady_clock;
namespace {
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
double microseconds(Clock::time_point start){return std::chrono::duration<double,std::micro>(Clock::now()-start).count();}
struct TemporaryWorld {
    std::filesystem::path path;
    TemporaryWorld(){std::string pattern="/tmp/voxys-free-build-benchmark-XXXXXX";auto* p=mkdtemp(pattern.data());require(p,"mkdtemp failed");path=p;}
    ~TemporaryWorld(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
void environment(const char* key,const char* value){require((value?setenv(key,value,1):unsetenv(key))==0,std::string("Environment: ")+key);}
nlohmann::json distribution(std::vector<double> samples){
    std::sort(samples.begin(),samples.end());
    const auto q=[&](double fraction){return samples.at(size_t(std::ceil(fraction*double(samples.size())))-1);};
    return {{"count",samples.size()},{"p50_us",q(.50)},{"p95_us",q(.95)},{"p99_us",q(.99)},
        {"total_us",std::accumulate(samples.begin(),samples.end(),0.0)}};
}
size_t partCount(const AdventureState& state){size_t count=0;for(const auto& s:state.structures)count+=s.parts.size();return count;}
}
int main(int argc,char** argv)try {
    require(argc==6,"usage: free_build_benchmark WORKSPACE PARTS FRAMES OUTPUT_PREFIX idle|edit");
    const auto workspace=std::filesystem::absolute(argv[1]);
    const size_t parts=std::stoul(argv[2]),frames=std::stoul(argv[3]);
    const std::string output=argv[4],workload=argv[5];
    require(parts<=768&&frames>=120&&frames<=360000,"Invalid bounded workload size");
    require(workload=="idle"||workload=="edit","Unknown workload");
    TemporaryWorld temporary;
    environment("VOXY_ADVENTURE_ROOT",temporary.path.c_str());
    environment("VOXY_ADVENTURE_NEW","1");
    environment("VOXY_ADVENTURE_WORLD",nullptr);
    environment("VOXY_ADVENTURE_PREFERENCES",nullptr);
    environment("VOXY_ADVENTURE_OBSERVE",nullptr);
    environment("BUILD_WORKSPACE_DIRECTORY",workspace.c_str());
    const auto startupStart=Clock::now();
    std::vector<uint16_t> samples(8192*8192);
    std::ifstream raw(workspace/"data/generated/td_seed_1234_8192.r16",std::ios::binary);
    require(bool(raw.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(uint16_t)))),"Terrain read failed");
    require(raw.peek()==std::char_traits<char>::eof(),"Unexpected terrain length");
    const terrain::lego::Surface surface{samples,8192,8192,600.f,1.f};
    gpu::Context gpu;require(gpu.initHeadless(),"Headless GPU unavailable");
    AdventureRuntime runtime(true);std::string error;
    require(runtime.initialize(surface,gpu.getDevice(),gpu.getQueue(),workspace/"shaders",WGPUTextureFormat_RGBA8Unorm,error),error);
    auto fixture=runtime.state();
    fixture.world.bytes.fill(0x42);
    if(parts){
        WorldStructure structure;structure.id=3;structure.revision=1;
        for(size_t i=0;i<parts;++i){
            const glm::dvec2 at{std::round(fixture.player.x*50)/50+8+3*double(i%32),std::round(fixture.player.z*50)/50-8-3*double(i/32)};
            auto height=terrainPlacementHeight(PieceKind::Brick2x2,0,at,surface);
            require(height.has_value(),"Fixture terrain height unavailable");
            WorldPart part{4+i,PieceKind::Brick2x2,{int32_t(std::round(at.x*50)),int32_t(std::round(*height*50)),int32_t(std::round(at.y*50))},0,uint32_t(i%2?0x86a789:0xd98775)};
            structure.parts.push_back(part);
        }
        structure.origin=structure.parts.front().position;
        fixture.structures.push_back(std::move(structure));fixture.lastIssuedId=parts+3;fixture.revision=1;
    }
    std::vector<std::byte> initial;
    require(AdventureSaveCodec::encode(fixture,runtime.content(),initial,error),"Fixture encode: "+error);
    require(runtime.restore(initial,fixture.world,error),"Fixture restore: "+error);
    require(runtime.state()==fixture,"Restore changed fixture");
    runtime.action(19,1); // Publish DOM ownership metadata; there is no rendered native HUD.
    Input input;input.onFocusChanged(true);input.onMouseMove(640,560);Camera camera;
    const auto frame=[&]{input.beginFrame();input.computeDeltas();runtime.update(1./60,input,camera,1280,800,{1280,800});input.endFrame();};
    for(int i=0;i<120;++i)frame();
    const double startupMs=microseconds(startupStart)/1000;
    // Profile only replay, not resource loading. Profilers are optional preloads.
    auto cpuStart=reinterpret_cast<int(*)(const char*)>(dlsym(RTLD_DEFAULT,"ProfilerStart"));
    auto cpuStop=reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT,"ProfilerStop"));
    auto heapStart=reinterpret_cast<void(*)(const char*)>(dlsym(RTLD_DEFAULT,"HeapProfilerStart"));
    auto heapDump=reinterpret_cast<void(*)(const char*)>(dlsym(RTLD_DEFAULT,"HeapProfilerDump"));
    auto heapStop=reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT,"HeapProfilerStop"));
    if(const char* path=std::getenv("VOXY_BENCH_CPU")){require(cpuStart&&cpuStop,"CPU profiler not preloaded");require(cpuStart(path)!=0,"CPU profile start failed");}
    if(const char* path=std::getenv("VOXY_BENCH_HEAP")){require(heapStart&&heapDump&&heapStop,"Heap profiler not preloaded");heapStart(path);}
    std::vector<double> updates,serializations;updates.reserve(frames);serializations.reserve(frames/6+1);
    std::vector<double> acceptedEditUpdates;acceptedEditUpdates.reserve(frames/300+1);
    std::vector<std::string> observations;observations.reserve(frames/6+1);
    size_t accepted=0,removed=0,valid=0,invalid=0,goldenBytes=0;
    std::cerr<<"VOXY_REPLAY_BEGIN\n";
    const auto replayStart=Clock::now();
    for(size_t i=0;i<frames;++i){
        if(workload=="edit"){
            // Stable grid of real cursor positions, plus repeated equal inputs.
            input.onMouseMove(float(320+(i/30%5)*160),float(400+(i/150%3)*80));
            switch(i%360){
            case 0:input.onKeyUp(int(Key::S));runtime.action(2,10);break;
            case 30:runtime.action(3);break;
            case 60:runtime.action(30,0xd98775);break;
            case 90:runtime.action(4);break;
            case 120:runtime.action(6);break;
            case 150:runtime.action(12,1);break;
            case 180:runtime.action(4);break;
            case 210:runtime.action(12,-1);break;
            case 240:runtime.action(31);break;
            case 270:runtime.action(20);break;
            case 300:input.onScroll(-1);break;
            case 330:runtime.action(22);input.onKeyDown(int(Key::S));break;
            default:break;
            }
        }
        const auto before=partCount(runtime.state());
        auto start=Clock::now();frame();const double updateUs=microseconds(start);updates.push_back(updateUs);
        const auto after=partCount(runtime.state());if(after>before)accepted+=after-before;if(after<before)removed+=before-after;
        if(after!=before)acceptedEditUpdates.push_back(updateUs);
        if(i%6==0){
            start=Clock::now();auto json=runtime.json();serializations.push_back(microseconds(start));
            if(json.find("\"valid\":true")!=std::string::npos)++valid;else ++invalid;
            goldenBytes+=json.size()+1;observations.push_back(std::move(json));
        }
        if(i%60==0)gpu.tick();
    }
    const double replayUs=microseconds(replayStart);
    std::cerr<<"VOXY_REPLAY_END\n";
    if(std::getenv("VOXY_BENCH_CPU"))cpuStop();
    if(std::getenv("VOXY_BENCH_HEAP")){heapDump("replay-complete");heapStop();}
    std::vector<std::byte> archive;require(runtime.snapshot(archive,error),error);
    AdventureState decoded;require(AdventureSaveCodec::decode(archive,fixture.world,runtime.content(),decoded,error),error);
    require(decoded==runtime.state(),"Archive round trip changed accepted state");
    require(runtime.state().backpack==fixture.backpack,"Creative inventory mutated");
    require(runtime.state().combat.tick==0,"Creative combat advanced");
    struct rusage usage{};require(getrusage(RUSAGE_SELF,&usage)==0,"getrusage failed");
    std::ofstream golden(output+".jsonl",std::ios::binary);for(const auto& line:observations)golden<<line<<'\n';require(bool(golden),"Golden output write failed");
    std::ofstream snapshot(output+".save",std::ios::binary);snapshot.write(reinterpret_cast<const char*>(archive.data()),std::streamsize(archive.size()));require(bool(snapshot),"Snapshot write failed");
    nlohmann::json result={{"scope","native optimized creative CPU component; no rendered FPS"},{"parts",parts},{"frames",frames},{"workload",workload},
        {"update",distribution(std::move(updates))},{"serialization",distribution(std::move(serializations))},
        {"accepted_edit_update",acceptedEditUpdates.empty()?nlohmann::json(nullptr):distribution(std::move(acceptedEditUpdates))},
        {"replay_us",replayUs},{"updates_per_second",double(frames)*1e6/replayUs},{"peak_rss_kib",usage.ru_maxrss},{"retained_golden_bytes",goldenBytes},
        {"startup_ms",startupMs},{"accepted_placements",accepted},{"removed_parts",removed},{"valid_observations",valid},{"invalid_observations",invalid},
        {"archive_sha256",core::sha256Hex(core::sha256(archive))},{"terrain_sha256",installedWorld().samplesSha256}};
    std::ofstream metrics(output+".metrics.json");metrics<<result.dump(2)<<'\n';require(bool(metrics),"Metrics write failed");
    std::cout<<result.dump()<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<"Benchmark failed: "<<error.what()<<'\n';return 1;}

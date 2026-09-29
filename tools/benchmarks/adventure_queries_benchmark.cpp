// CPU query workload. It does not initialize a GPU or measure rendered FPS.
#include "game/adventure/spatial_queries.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

using voxy::game::adventure::AdventureSpatialQueries;
using Queries=AdventureSpatialQueries;
using Clock=std::chrono::steady_clock;
namespace {
void require(bool accepted,const char* message){if(!accepted)throw std::runtime_error(message);}
voxy::game::construction::DurableId identity(uint64_t counter) {
    voxy::game::construction::WorldNamespace world{};world.bytes[0]=73;return {world,counter};
}
void mix(uint64_t& checksum,uint64_t word){checksum^=word;checksum*=1099511628211ull;}
void mix(uint64_t& checksum,double word){mix(checksum,std::bit_cast<uint64_t>(word));}
enum class Workload {Support,Clear,Sweep,Ray,Column};
constexpr std::array<const char*,5> names{"support","clear","sweep","ray","column"};
struct Fixture {
    std::string name;
    std::vector<Queries::Solid> solids;
    std::vector<glm::dvec3> probes;
};
uint64_t replay(const Queries& queries,std::span<const glm::dvec3> probes,Workload workload,size_t first,size_t count) {
    uint64_t checksum=14695981039346656037ull;
    for(size_t i=first;i<first+count;++i) {
        const auto point=probes[i%probes.size()];
        switch(workload) {
        case Workload::Support:mix(checksum,queries.supportHeight({point.x,point.z},1.12,point.y+.015));break;
        case Workload::Clear:mix(checksum,uint64_t(queries.clearCapsule(point,1.12,4.76)));break;
        case Workload::Sweep: {
            const auto result=queries.sweepCapsule(point,point+glm::dvec3(.08,0,.06),1.12,4.76);
            mix(checksum,uint64_t(result.complete));mix(checksum,uint64_t(result.hit));mix(checksum,uint64_t(result.startOverlapped));
            mix(checksum,result.distance);mix(checksum,result.normal.x);mix(checksum,result.normal.y);mix(checksum,result.normal.z);break;
        }
        case Workload::Ray: {
            // Nearby use/target rays use the full terrain and admitted boxes.
            const auto direction=i%2?glm::dvec3(.6,-.5,.7):glm::dvec3(.6,0,.7);
            const auto result=queries.raycast(point+glm::dvec3(0,3.1,0),direction,i%2?8.:32.);
            mix(checksum,uint64_t(result.complete));mix(checksum,uint64_t(result.hit));mix(checksum,uint64_t(result.terrain));
            mix(checksum,result.distance);mix(checksum,result.part.counter);
            mix(checksum,result.point.x);mix(checksum,result.point.y);mix(checksum,result.point.z);break;
        }
        case Workload::Column: {
            std::array<double,64> levels;
            const auto result=queries.walkableFeet({point.x,point.z},point.y-.02,point.y+8,levels,1.12,4.76);
            mix(checksum,uint64_t(result.complete));mix(checksum,uint64_t(result.count));
            for(size_t level=0;level<result.count;++level)mix(checksum,levels[level]);
            break;
        }
        }
    }
    return checksum;
}
Fixture synthetic(std::string name,size_t total) {
    Fixture fixture;fixture.name=std::move(name);fixture.solids.reserve(total);
    for(size_t index=0;index<total;++index) {
        const auto position=index%256;
        const double x=-32.+4.*double(position%16),z=-32.+4.*double(position/16);
        const double floor=3.+3.*double((index/256)%4);
        fixture.solids.push_back({identity(1),identity(index+2),{x-.5,floor,z-.5},{x+.5,floor+1,z+.5}});
    }
    for(size_t i=0;i<64;++i)fixture.probes.push_back({-31.+4.*double(i%8),.185,-31.+4.*double(i/8)});
    return fixture;
}
Fixture installed(const std::filesystem::path& path) {
    std::ifstream source(path);require(bool(source),"Could not open installed collider fixture");
    Fixture fixture;fixture.name="installed-frontier";std::string type;
    while(source>>type) {
        if(type=="POINT") {
            glm::dvec3 p;require(bool(source>>p.x>>p.y>>p.z),"Malformed installed probe");fixture.probes.push_back(p);
        } else if(type=="BOX") {
            uint64_t structure=0,part=0;glm::dvec3 low,high;
            require(bool(source>>structure>>part>>low.x>>low.y>>low.z>>high.x>>high.y>>high.z),"Malformed installed box");
            fixture.solids.push_back({identity(structure),identity(part),low,high});
        } else throw std::runtime_error("Unknown installed fixture record: "+type);
    }
    require(!fixture.probes.empty()&&!fixture.solids.empty(),"Installed fixture is empty");return fixture;
}
}
int main(int argc,char** argv)try {
    require(argc==3||argc==5,"usage: adventure_queries_benchmark BATCHES OUTPUT [FULL_TERRAIN_R16 COLLIDERS_TXT]");
    const size_t batches=std::stoul(argv[1]);require(batches>=30&&batches<=100000,"Invalid bounded sample count");
    std::vector<uint16_t> samples;
    uint32_t extent=256;float heightScale=8;
    if(argc==5) {
        extent=8192;heightScale=600;samples.resize(size_t{extent}*extent);
        std::ifstream terrain(argv[3],std::ios::binary);
        require(bool(terrain.read(reinterpret_cast<char*>(samples.data()),std::streamsize(samples.size()*sizeof(uint16_t)))),"Could not read full terrain");
        require(terrain.peek()==std::char_traits<char>::eof(),"Unexpected full terrain length");
    } else samples.assign(size_t{extent}*extent,32768);
    const voxy::terrain::lego::Surface terrain{samples,extent,extent,heightScale,1};
    std::vector<Fixture> fixtures;
    if(argc==5)fixtures.push_back(installed(argv[4]));
    else {
        fixtures.push_back(synthetic("empty",0));fixtures.push_back(synthetic("local-256",256));
        fixtures.push_back(synthetic("dense-4096",4096));fixtures.push_back(synthetic("capacity-16384",Queries::maximumSolids));
    }
    std::ofstream output(argv[2]);require(bool(output),"Could not open output");
    output<<std::setprecision(12)<<"{\n  \"scope\":\"CPU spatial queries; no GPU, rendering or FPS\",\n  \"batches\":"<<batches<<",\n  \"queriesPerBatch\":64,\n  \"cases\":[\n";
    bool first=true;
    for(const auto& fixture:fixtures) {
        Queries queries;require(queries.bindTerrain(terrain)&&queries.publish(fixture.solids,1),"Fixture publication was refused");
        for(size_t kind=0;kind<names.size();++kind) {
            const auto workload=static_cast<Workload>(kind);
            uint64_t checksum=14695981039346656037ull;
            for(size_t warmup=0;warmup<100;++warmup)mix(checksum,replay(queries,fixture.probes,workload,warmup*64,64));
            std::vector<double> timings;timings.reserve(batches);
            for(size_t batch=0;batch<batches;++batch) {
                const auto begin=Clock::now();const auto value=replay(queries,fixture.probes,workload,batch*64,64);
                timings.push_back(std::chrono::duration<double,std::micro>(Clock::now()-begin).count()/64.);
                mix(checksum,value);
            }
            std::sort(timings.begin(),timings.end());
            const auto at=[&](double fraction){return timings.at(static_cast<size_t>(std::ceil(fraction*double(batches)))-1);};
            if(!first)output<<",\n";
            first=false;
            output<<"    {\"fixture\":\""<<fixture.name<<"\",\"solids\":"<<fixture.solids.size()<<",\"query\":\""<<names[kind]
                <<"\",\"p50_us\":"<<at(.5)<<",\"p95_us\":"<<at(.95)<<",\"p99_us\":"<<at(.99)
                <<",\"checksum\":\""<<std::hex<<checksum<<std::dec<<"\"}";
        }
    }
    output<<"\n  ]\n}\n";require(bool(output),"Could not write report");return 0;
}catch(const std::exception& error){std::cerr<<"Query benchmark failed: "<<error.what()<<'\n';return 1;}

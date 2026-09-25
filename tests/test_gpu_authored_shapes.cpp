#include "physics/gpu/gpu_authored_shapes.hpp"
#include "physics/gpu/gpu_body_shape.hpp"
#include "physics/gpu/gpu_physics_backend.hpp"
#include "physics/gpu/gpu_queries.hpp"
#include "physics/gpu/gpu_narrow_phase.hpp"
#include "physics/gpu/gpu_dynamic_solver.hpp"
#include "physics/authored_body_frame.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "terrain/lego_surface.hpp"
#include "game/adventure/imported_assembly.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <glm/gtc/quaternion.hpp>

#if defined(VOXY_WASM)
#include <emscripten.h>
extern "C" WGPUDevice emscripten_webgpu_get_device(void);
#else
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice,WGPUBool,const WGPUWrappedSubmissionIndex*);
#endif

namespace voxy::physics {
namespace {
using Rows=std::array<std::array<uint32_t,4>,16>;
struct Context {
#if defined(VOXY_WASM)
    WGPUDevice device=nullptr;
    WGPUQueue queue=nullptr;
    bool initialize() { device=emscripten_webgpu_get_device(); if(device) queue=wgpuDeviceGetQueue(device); return device && queue; }
    WGPUDevice getDevice() const { return device; }
    WGPUQueue getQueue() const { return queue; }
    ~Context() { if(queue) wgpuQueueRelease(queue); if(device) wgpuDeviceRelease(device); }
#else
    gpu::Context context;
    bool initialize() { return context.initHeadless(); }
    WGPUDevice getDevice() const { return context.getDevice(); }
    WGPUQueue getQueue() const { return context.getQueue(); }
#endif
    void pump() {
#if defined(VOXY_WASM)
        emscripten_sleep(1);
#else
        static_cast<void>(wgpuDevicePoll(getDevice(),false,nullptr));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
};
template<class Predicate> bool wait(Context& context,Predicate&& predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    do { context.pump(); if(predicate()) return true; } while(std::chrono::steady_clock::now()<deadline);
    return false;
}
AuthoredShape prepared(uint32_t first=7,bool append=true) {
    using namespace geometry;
    std::array<UnionBox,2> boxes{{{{{-50,-50,-50},{50,50,50}},first},{{{50,0,0},{80,25,25}},first+4}}};
    BoxUnionIssue issue; auto shape=BoxUnion::compile(std::span{boxes}.first(append ? 2u : 1u),issue);
    if(!shape) throw std::runtime_error("shape union fixture");
    const RigidMassInput mass{5,{.25,-.5,.75},{88.0/25,-2.0/75,8.0/15,-2.0/75,161.0/75,2.0/5,8.0/15,2.0/5,10.0/3}};
    AuthoredShapeIssue error; auto result=AuthoredShape::prepare(*shape,mass,error);
    if(!result) throw std::runtime_error("mass packing fixture");
    return std::move(*result);
}
GpuShapeStoreLimits smallLimits(uint32_t slots=8) {
    GpuShapeStoreLimits limits; limits.cpu.slots=slots; limits.cpu.cells=64; limits.cpu.faces=512; limits.cpu.nodes=128;
    limits.gpuBytes=65536; return limits;
}
std::unique_ptr<GpuAuthoredShapeStore> create(Context& context,GpuShapeStoreLimits limits=smallLimits(),uint64_t identity=71) {
    GpuShapeError error; auto result=GpuAuthoredShapeStore::create(context.getDevice(),identity,error,limits);
    if(!result || error!=GpuShapeError::None) throw std::runtime_error("GPU shape creation fixture");
    if(!wait(context,[&]{result->poll(); return result->stats().phase!=GpuShapePhase::Initializing;})
        || result->stats().phase!=GpuShapePhase::Ready) throw std::runtime_error("GPU shape initialization: "+std::string(result->failure()));
    return result;
}
bool drain(Context& context,IAuthoredShapeResources& store) {
    return wait(context,[&]{store.poll(); return store.stats().pendingOperations==0;}) && store.stats().phase!=GpuShapePhase::Failed;
}
ShapeHandle upload(Context& context,IAuthoredShapeResources& store,uint32_t source=7,bool append=true) {
    auto shape=prepared(source,append); GpuShapeError error; const auto handle=store.upload(std::move(shape),error);
    if(!handle.valid() || error!=GpuShapeError::None || !drain(context,store) || store.state(handle)!=GpuShapeState::Ready)
        throw std::runtime_error("GPU shape upload: "+std::string(store.failure()));
    return handle;
}
void close(Context& context,IAuthoredShapeResources& store) {
    store.close(); EXPECT_TRUE(wait(context,[&]{store.poll(); return store.stats().phase==GpuShapePhase::Closed;})) << store.failure();
    EXPECT_EQ(store.stats().gpuBytes,0u); EXPECT_EQ(store.stats().cpu.charged,(ShapeResourceCost{}));
    EXPECT_EQ(store.stats().pendingCallbacks,0u); EXPECT_EQ(store.buffer(),nullptr);
}

constexpr auto kProbe=R"wgsl(
@group(0) @binding(0) var<storage,read> authored_shape_heap: array<vec4<u32>>;
@group(0) @binding(1) var<uniform> request: vec4<u32>;
@group(0) @binding(2) var<storage,read_write> result: array<vec4<u32>>;
@compute @workgroup_size(1)
fn probe() {
    for(var i=0u;i<16u;i++) { result[i]=vec4<u32>(0u); }
    let shape=authored_shape(request.x,request.y);
    if(!shape.valid) { return; }
    result[0]=vec4<u32>(1u,shape.cell_count,shape.face_count,shape.node_count);
    var volume=0.0; var area=0.0; var sources=0u; var leaves=0u;
    for(var i=0u;i<shape.cell_count;i++) {
        let cell=authored_cell(shape,i); let d=cell.maximum-cell.minimum;
        if(!cell.valid) { result[15].x=1u; return; }
        volume+=d.x*d.y*d.z;
    }
    for(var i=0u;i<shape.face_count;i++) {
        let face=authored_face(shape,i); let d=face.maximum-face.minimum;
        if(!face.valid) { result[15].x=2u; return; }
        area+=d[(face.axis+1u)%3u]*d[(face.axis+2u)%3u]; sources+=face.source;
    }
    for(var i=0u;i<shape.node_count;i++) {
        let node=authored_node(shape,i);
        if(!node.valid) { result[15].x=3u; return; }
        if(node.cell!=0xffffffffu) { leaves++; }
    }
    result[1]=vec4<u32>(sources,bitcast<u32>(volume),bitcast<u32>(area),leaves);
    let impulse=authored_impulse_at_root_point(shape,shape.root_from_body,vec3<f32>(2,3,-4),vec3<f32>(1,-2,3));
    result[2]=bitcast<vec4<u32>>(vec4<f32>(impulse.linear,0));
    result[3]=bitcast<vec4<u32>>(vec4<f32>(impulse.angular,0));
    let root_orientation=vec4<f32>(0,0,sqrt(0.5),sqrt(0.5));
    let body_orientation=authored_body_orientation(shape,root_orientation);
    let center=authored_com_position(shape,vec3<f32>(10,20,30),root_orientation);
    result[4]=bitcast<vec4<u32>>(vec4<f32>(center,shape.center_inverse_mass.w));
    result[5]=bitcast<vec4<u32>>(vec4<f32>(authored_root_position(shape,center,body_orientation),0));
    result[6]=bitcast<vec4<u32>>(vec4<f32>(authored_com_velocity(shape,vec3<f32>(3,4,5),vec3<f32>(0,0,2),root_orientation),0));
    result[7]=bitcast<vec4<u32>>(vec4<f32>(authored_root_point(shape,authored_body_point(shape,vec3<f32>(1,-2,3))),shape.inverse_inertia_radius.w));
    result[8]=vec4<u32>(select(0u,1u,authored_cell(shape,shape.cell_count).valid),
        select(0u,1u,authored_face(shape,shape.face_count).valid),select(0u,1u,authored_node(shape,shape.node_count).valid),0u);
    result[9]=bitcast<vec4<u32>>(vec4<f32>(shape.minimum,0));
    result[10]=bitcast<vec4<u32>>(vec4<f32>(shape.maximum,0));
}
)wgsl";

struct Probe {
    WGPUShaderModule shader=nullptr;
    WGPUComputePipeline pipeline=nullptr;
    WGPUBindGroupLayout layout=nullptr;
    WGPUBuffer input=nullptr,output=nullptr,readback=nullptr;
    WGPUBindGroup bindGroup=nullptr;
    WGPUCommandEncoder encoder=nullptr;
    WGPUCommandBuffer command=nullptr;
    ~Probe() {
        if(command) wgpuCommandBufferRelease(command);
        if(encoder) wgpuCommandEncoderRelease(encoder);
        if(bindGroup) wgpuBindGroupRelease(bindGroup);
        if(layout) wgpuBindGroupLayoutRelease(layout);
        if(pipeline) wgpuComputePipelineRelease(pipeline);
        if(shader) wgpuShaderModuleRelease(shader);
        for(auto buffer:{input,output,readback}) if(buffer) wgpuBufferRelease(buffer);
    }
    bool initialize(Context& context,IAuthoredShapeResources& store) {
        std::ifstream stream("shaders/physics_authored_shapes.wgsl",std::ios::ate|std::ios::binary);
        if(!stream) return false;
        const auto size=stream.tellg(); if(size<=0 || size>1024*1024) return false;
        std::string source(static_cast<size_t>(size),'\0'); stream.seekg(0);
        stream.read(source.data(),static_cast<std::streamsize>(size)); if(!stream) return false;
        shader=gpu::createShaderModule(context.getDevice(),source+kProbe,"authored_probe"); if(!shader) return false;
        WGPUComputePipelineDescriptor desc{}; WGPU_SET_LABEL(desc,"authored_probe");
        desc.compute.module=shader; WGPU_SET_ENTRY_POINT(desc.compute,"probe");
        pipeline=wgpuDeviceCreateComputePipeline(context.getDevice(),&desc); if(!pipeline) return false;
        layout=wgpuComputePipelineGetBindGroupLayout(pipeline,0); if(!layout) return false;
        input=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::uniform(16,"shape_request"));
        output=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::storage(sizeof(Rows),false,"shape_result"));
        readback=gpu::createBuffer(context.getDevice(),gpu::BufferDesc{.label="shape_readback",.size=sizeof(Rows),.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
        if(!input || !output || !readback) return false;
        const std::array entries{gpu::BindGroupEntry(0).buffer(store.buffer()),gpu::BindGroupEntry(1).buffer(input),gpu::BindGroupEntry(2).buffer(output)};
        bindGroup=gpu::createBindGroup(context.getDevice(),layout,entries,"shape_probe"); return bindGroup!=nullptr;
    }
    bool encode(Context& context,ShapeHandle handle) {
        const std::array<uint32_t,4> request{handle.index,handle.generation,0,0};
        wgpuQueueWriteBuffer(context.getQueue(),input,0,request.data(),sizeof(request));
        WGPUCommandEncoderDescriptor desc{}; encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&desc); if(!encoder) return false;
        WGPUComputePassDescriptor passDesc{}; const auto pass=wgpuCommandEncoderBeginComputePass(encoder,&passDesc); if(!pass) return false;
        wgpuComputePassEncoderSetPipeline(pass,pipeline); wgpuComputePassEncoderSetBindGroup(pass,0,bindGroup,0,nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1); wgpuComputePassEncoderEnd(pass); wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,output,0,readback,0,sizeof(Rows));
        WGPUCommandBufferDescriptor commandDesc{}; command=wgpuCommandEncoderFinish(encoder,&commandDesc); return command!=nullptr;
    }
    bool read(Context& context,Rows& rows) {
        auto complete=std::make_shared<std::atomic<uint32_t>>(0);
        using Payload=std::shared_ptr<std::atomic<uint32_t>>; auto* payload=new Payload(complete);
#if defined(VOXY_WASM)
        WGPUBufferMapCallbackInfo info=WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        info.mode=WGPUCallbackMode_AllowSpontaneous; info.userdata1=payload;
        info.callback=[](WGPUMapAsyncStatus status,WGPUStringView,void* data,void*) {
            const std::unique_ptr<Payload> holder{static_cast<Payload*>(data)};
            (*holder)->store(status==WGPUMapAsyncStatus_Success ? 1u : 2u,std::memory_order_release);
        };
        static_cast<void>(wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,sizeof(Rows),info));
#else
        wgpuBufferMapAsync(readback,WGPUMapMode_Read,0,sizeof(Rows),[](WGPUBufferMapAsyncStatus status,void* data) {
            const std::unique_ptr<Payload> holder{static_cast<Payload*>(data)};
            (*holder)->store(status==WGPUBufferMapAsyncStatus_Success ? 1u : 2u,std::memory_order_release);
        },payload);
#endif
        if(!wait(context,[&]{return complete->load(std::memory_order_acquire)!=0;}) || complete->load()!=1) return false;
        const auto* bytes=wgpuBufferGetConstMappedRange(readback,0,sizeof(Rows)); if(!bytes) return false;
        std::memcpy(&rows,bytes,sizeof(rows)); wgpuBufferUnmap(readback);
        if(const char* directory=std::getenv("VOXY_SHAPE_CAPTURE_DIR"); directory && *directory) {
            static uint32_t sequence=0;
            const auto* testInfo=::testing::UnitTest::GetInstance()->current_test_info();
            const auto path=std::filesystem::path(directory)/(std::to_string(sequence++)+"-"+testInfo->name()+".u32");
            std::ofstream file(path,std::ios::binary); file.write(reinterpret_cast<const char*>(rows.data()),sizeof(rows));
            if(!file) return false;
        }
        return true;
    }
};
Rows runProbe(Context& context,IAuthoredShapeResources& store,ShapeHandle handle,bool declare=true) {
    Probe probe; if(!probe.initialize(context,store)) throw std::runtime_error("GPU probe pipeline");
    const std::array handles{handle}; GpuShapeError error;
    const auto ticket=store.prepareSubmission(declare ? std::span<const ShapeHandle>{handles} : std::span<const ShapeHandle>{},error);
    if(!ticket.valid() || !probe.encode(context,handle)) throw std::runtime_error("GPU probe encoding");
    const std::array commands{probe.command};
    if(store.submit(ticket,commands)!=GpuShapeError::None) throw std::runtime_error("GPU probe submission");
    Rows rows{}; if(!probe.read(context,rows) || !drain(context,store)) throw std::runtime_error("GPU probe readback: "+std::string(store.failure()));
    return rows;
}
void near(const Rows& rows,size_t row,std::array<float,3> values,float tolerance=0.00002f) {
    for(size_t i=0;i<3;++i) EXPECT_NEAR(std::bit_cast<float>(rows[row][i]),values[i],tolerance) << row << ":" << i;
}
class GpuAuthoredShapes : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(context.initialize()) << "Actual GPU required; no skipped acceptance cases"; }
    Context context;
};

TEST_F(GpuAuthoredShapes, RealHeapDecodesGeometryAndAnalyticalMassFrames) {
    auto store=create(context); const auto handle=upload(context,*store);
    const auto rows=runProbe(context,*store,handle);
    EXPECT_EQ(rows[0],(std::array<uint32_t,4>{1,2,14,3})); EXPECT_EQ(rows[1][0],118u); EXPECT_EQ(rows[1][3],2u);
    EXPECT_NEAR(std::bit_cast<float>(rows[1][1]),8.15f,.00002f); EXPECT_NEAR(std::bit_cast<float>(rows[1][2]),25.2f,.00002f);
    near(rows,2,{.4f,.6f,-.8f}); near(rows,3,{-1357.0f/3600.0f,5863.0f/1800.0f,56.0f/45.0f});
    near(rows,4,{10.5f,20.25f,30.75f}); near(rows,5,{10,20,30}); near(rows,6,{2.5f,5,5}); near(rows,7,{1,-2,3});
    near(rows,9,{-1,-1,-1}); near(rows,10,{1.6f,1,1});
    EXPECT_EQ(rows[8],(std::array<uint32_t,4>{})); EXPECT_EQ(rows[15],(std::array<uint32_t,4>{}));
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, RetiringAnEncodedShapePreservesActualGpuReadAndLateReader) {
    auto store=create(context,smallLimits(1)); const auto handle=upload(context,*store);
    ASSERT_EQ(store->retain(handle),GpuShapeError::None); const auto* held=store->get(handle);
    Probe probe; ASSERT_TRUE(probe.initialize(context,*store));
    const std::array handles{handle}; GpuShapeError error; const auto ticket=store->prepareSubmission(handles,error); ASSERT_TRUE(ticket.valid());
    ASSERT_TRUE(probe.encode(context,handle)); EXPECT_EQ(store->retire(handle),GpuShapeError::None);
    auto replacement=prepared(31); const auto cost=replacement.cost();
    EXPECT_FALSE(store->upload(std::move(replacement),error).valid()); EXPECT_EQ(error,GpuShapeError::Busy); EXPECT_EQ(replacement.cost(),cost);
    const std::array commands{probe.command}; ASSERT_EQ(store->submit(ticket,commands),GpuShapeError::None);
    EXPECT_FALSE(store->upload(std::move(replacement),error).valid()); EXPECT_EQ(error,GpuShapeError::Capacity);
    Rows old{}; ASSERT_TRUE(probe.read(context,old)); ASSERT_TRUE(drain(context,*store));
    EXPECT_EQ(old[1][0],118u); EXPECT_EQ(store->get(handle),held); EXPECT_EQ(store->stats().cpu.retiring,1u);
    ASSERT_EQ(store->release(handle),GpuShapeError::None); ASSERT_TRUE(drain(context,*store)); EXPECT_EQ(store->get(handle),nullptr);
    const auto next=store->upload(std::move(replacement),error); ASSERT_TRUE(next.valid()); ASSERT_TRUE(drain(context,*store));
    EXPECT_EQ(next.index,handle.index); EXPECT_EQ(next.generation,handle.generation+1);
    const auto stale=runProbe(context,*store,handle,false); EXPECT_EQ(stale[0][0],0u);
    const auto current=runProbe(context,*store,next); EXPECT_EQ(current[1][0],454u);
    EXPECT_EQ(store->retain(handle),GpuShapeError::InvalidHandle); close(context,*store);
}

TEST_F(GpuAuthoredShapes, DiscardUsesRealFenceAndInvalidatesOldGpuGeneration) {
    auto store=create(context,smallLimits(1)); const auto handle=upload(context,*store);
    const std::array handles{handle}; GpuShapeError error; const auto ticket=store->prepareSubmission(handles,error); ASSERT_TRUE(ticket.valid());
    EXPECT_EQ(store->submit(ticket,{}),GpuShapeError::InvalidCommands);
    EXPECT_EQ(store->retire(handle),GpuShapeError::None); EXPECT_EQ(store->discard(ticket),GpuShapeError::None);
    EXPECT_EQ(store->discard(ticket),GpuShapeError::InvalidTicket); ASSERT_TRUE(drain(context,*store));
    EXPECT_EQ(store->get(handle),nullptr); const auto rows=runProbe(context,*store,handle,false); EXPECT_EQ(rows[0][0],0u);
    EXPECT_EQ(store->stats().cpu.completed,store->stats().cpu.submitted); close(context,*store);
}

TEST_F(GpuAuthoredShapes, BoundedPendingOperationsRefuseWithoutConsumingPreparedStorage) {
    auto store=create(context,smallLimits(16)); GpuShapeError error;
    std::array<ShapeHandle,8> handles{};
    for(size_t i=0;i<handles.size();++i) { auto value=prepared(); handles[i]=store->upload(std::move(value),error); ASSERT_TRUE(handles[i].valid()); }
    EXPECT_EQ(store->stats().pendingOperations,8u);
    auto ninth=prepared(); const auto before=ninth.cost(); EXPECT_FALSE(store->upload(std::move(ninth),error).valid());
    EXPECT_EQ(error,GpuShapeError::Busy); EXPECT_EQ(ninth.cost(),before); ASSERT_TRUE(drain(context,*store));
    for(auto handle:handles) EXPECT_EQ(store->state(handle),GpuShapeState::Ready);
    EXPECT_TRUE(store->upload(std::move(ninth),error).valid()); ASSERT_TRUE(drain(context,*store)); close(context,*store);
}

TEST_F(GpuAuthoredShapes, ContiguousRangesRefuseFragmentationAndCoalesceAfterRetirement) {
    auto limits=smallLimits(4); limits.cpu.cells=6;
    auto store=create(context,limits); const auto a=upload(context,*store,7,true),b=upload(context,*store,21,false),c=upload(context,*store,41,true);
    EXPECT_EQ(store->retire(b),GpuShapeError::None); ASSERT_TRUE(drain(context,*store));
    auto value=prepared(61); GpuShapeError error; const auto before=value.cost();
    EXPECT_EQ(store->stats().cpu.charged.cells,4u); EXPECT_FALSE(store->upload(std::move(value),error).valid());
    EXPECT_EQ(error,GpuShapeError::Capacity); EXPECT_EQ(value.cost(),before);
    EXPECT_EQ(store->retire(a),GpuShapeError::None); ASSERT_TRUE(drain(context,*store));
    const auto d=store->upload(std::move(value),error); ASSERT_TRUE(d.valid()); ASSERT_TRUE(drain(context,*store));
    const auto preserved=runProbe(context,*store,c); EXPECT_EQ(preserved[1][0],594u);
    const auto replaced=runProbe(context,*store,d); EXPECT_EQ(replaced[1][0],874u); close(context,*store);
}

TEST_F(GpuAuthoredShapes, InvalidHandlesTicketsAndGpuBudgetsDoNotSubmitWork) {
    GpuShapeError error; EXPECT_FALSE(GpuAuthoredShapeStore::create(nullptr,71,error)); EXPECT_EQ(error,GpuShapeError::InvalidContext);
    auto limits=smallLimits(); limits.gpuBytes=1;
    EXPECT_FALSE(GpuAuthoredShapeStore::create(context.getDevice(),71,error,limits)); EXPECT_EQ(error,GpuShapeError::InvalidLimits);
    auto store=create(context); const auto handle=upload(context,*store);
    auto foreign=handle; foreign.pool=72; const std::array handles{handle,foreign}; const auto before=store->stats().cpu;
    EXPECT_FALSE(store->prepareSubmission(handles,error).valid()); EXPECT_EQ(error,GpuShapeError::InvalidHandle); EXPECT_EQ(store->stats().cpu,before);
    std::array<ShapeHandle,513> oversized{}; oversized.fill(handle);
    EXPECT_FALSE(store->prepareSubmission(oversized,error).valid()); EXPECT_EQ(error,GpuShapeError::Capacity); EXPECT_EQ(store->stats().cpu,before);
    EXPECT_EQ(store->discard({72,1}),GpuShapeError::InvalidTicket); close(context,*store);
}

TEST_F(GpuAuthoredShapes, CloseWaitsForAnUnresolvedSubmissionAndRetainedReader) {
    auto store=create(context); const auto handle=upload(context,*store); ASSERT_EQ(store->retain(handle),GpuShapeError::None);
    const std::array handles{handle}; GpuShapeError error; const auto ticket=store->prepareSubmission(handles,error); ASSERT_TRUE(ticket.valid());
    store->close(); EXPECT_EQ(store->stats().phase,GpuShapePhase::Closing); EXPECT_NE(store->buffer(),nullptr);
    EXPECT_TRUE(store->stats().unresolvedSubmission); EXPECT_EQ(store->retain(handle),GpuShapeError::Closed);
    ASSERT_EQ(store->discard(ticket),GpuShapeError::None); ASSERT_TRUE(drain(context,*store));
    EXPECT_EQ(store->stats().phase,GpuShapePhase::Closing); EXPECT_NE(store->get(handle),nullptr);
    EXPECT_EQ(store->release(handle),GpuShapeError::None); close(context,*store);
}

TEST_F(GpuAuthoredShapes, ValidationFailureCannotCertifyItsSubmissionOrReuseResources) {
    auto store=create(context); const auto handle=upload(context,*store);
    const std::array handles{handle}; GpuShapeError error; const auto ticket=store->prepareSubmission(handles,error); ASSERT_TRUE(ticket.valid());
    WGPUCommandEncoderDescriptor desc{}; const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&desc); ASSERT_NE(encoder,nullptr);
    // Deliberately invalid copy, captured by the actual device scope. Finish
    // and submit: Dawn may defer encoder validation until finish(), while an
    // abandoned unfinished encoder has no invalid submission to certify.
    wgpuCommandEncoderCopyBufferToBuffer(encoder,store->buffer(),0,store->buffer(),0,16);
    WGPUCommandBufferDescriptor commandDesc{};
    const auto command=wgpuCommandEncoderFinish(encoder,&commandDesc);
    wgpuCommandEncoderRelease(encoder); ASSERT_NE(command,nullptr);
    const std::array commands{command}; const auto submitted=store->submit(ticket,commands);
    wgpuCommandBufferRelease(command); ASSERT_EQ(submitted,GpuShapeError::None);
    ASSERT_TRUE(wait(context,[&]{store->poll(); return store->stats().pendingOperations==0;}));
    EXPECT_EQ(store->stats().phase,GpuShapePhase::Failed); EXPECT_FALSE(store->failure().empty());
    EXPECT_LT(store->stats().cpu.completed,ticket.serial); EXPECT_NE(store->get(handle),nullptr);
    auto value=prepared(); EXPECT_FALSE(store->upload(std::move(value),error).valid()); EXPECT_EQ(error,GpuShapeError::GpuFailure);
    store->close(); EXPECT_EQ(store->stats().phase,GpuShapePhase::Failed);
}

// Execute the shipping ballistic command/integration entry points. The input
// tables are controlled GPU fixtures, not an authored-body admission API.
struct BodyKernel : Probe {
    struct alignas(16) Command {
        glm::uvec4 header{11u,1u,1u,1u};
        glm::vec4 p0{},p1{},p2{},p3{},p4{};
        glm::ivec4 p5{};
        glm::vec4 p6{};
    };
    static_assert(sizeof(Command)==128);
    std::array<glm::vec4,4> poses{},motions{};
    std::array<GpuBodyShape,2> shapes{};
    std::array<glm::uvec4,2> metadata{};
    std::array<glm::vec4,13> uniforms{};
    Command bodyCommand{};
    std::array<WGPUBuffer,11> buffers{};
    std::array<WGPUPipelineLayout,2> pipelineLayouts{};
    WGPUComputePipeline integratePipeline=nullptr;
    WGPUBindGroupLayout integrateLayout=nullptr;
    WGPUBindGroup integrateGroup=nullptr;
    std::array<WGPUTexture,2> textures{};
    std::array<WGPUTextureView,2> views{};
    WGPUSampler sampler=nullptr;
    bool captureBodyMetadata=false;
    bool captureTerrainContacts=false;
    uint32_t terrainWidth=1,terrainHeight=1;
    std::vector<uint32_t> terrainSamples;
    explicit BodyKernel(float dt=1.0f/60.0f) {
        poses[1]={0,0,0,1}; poses[2]={.25f,-.5f,.75f,.2f}; poses[3]={0,0,0,1};
        shapes[1].dimensionsType={2,2,2,1}; shapes[1].inverseInertiaMaterial={.5f,1.0f/3.0f,.25f,0};
        metadata[1]={0u,0u,0u,1u|(1u<<20u)|(1u<<21u)};
        uniforms[0]={0,0,0,dt}; uniforms[1]={0,0,500,100};
        const glm::uvec4 counts{2,1,2,1}; std::memcpy(&uniforms[2],&counts,sizeof(counts));
        uniforms[4]={0,0,1,1}; uniforms[6]={0,0,1,0}; uniforms[7]={0,0,0,.005f};
        uniforms[8]={0,.2f,.05f,.5f}; uniforms[11]={0,0,1,1};
    }
    ~BodyKernel() {
        if(integrateGroup) wgpuBindGroupRelease(integrateGroup);
        if(integratePipeline) wgpuComputePipelineRelease(integratePipeline);
        if(integrateLayout) wgpuBindGroupLayoutRelease(integrateLayout);
        for(auto value:pipelineLayouts) if(value) wgpuPipelineLayoutRelease(value);
        for(auto value:buffers) if(value) wgpuBufferRelease(value);
        if(sampler) wgpuSamplerRelease(sampler);
        for(auto value:views) if(value) wgpuTextureViewRelease(value);
        for(auto value:textures) if(value) wgpuTextureRelease(value);
    }
    bool initialize(Context& context,const char* integrationEntry=nullptr,WGPUBuffer authoredAtlas=nullptr) {
        std::ifstream stream("shaders/physics_ballistic.wgsl",std::ios::ate|std::ios::binary);
        if(!stream) return false;
        const auto size=stream.tellg(); if(size<=0 || size>1024*1024) return false;
        std::string source(static_cast<size_t>(size),'\0'); stream.seekg(0);
        stream.read(source.data(),static_cast<std::streamsize>(size)); if(!stream) return false;
#if !defined(VOXY_WASM)
        // Keep full compiler diagnostics in the test log. The production
        // owner's bounded failure message deliberately retains only a prefix.
        wgpuDevicePushErrorScope(context.getDevice(),WGPUErrorFilter_Validation);
#endif
        shader=gpu::createShaderModule(context.getDevice(),source,"body_mass_contract");
#if !defined(VOXY_WASM)
        bool shaderFailed=false;
        wgpuDevicePopErrorScope(context.getDevice(),[](WGPUErrorType type,const char* message,void* data) {
            if(type!=WGPUErrorType_NoError) {
                *static_cast<bool*>(data)=true;std::fprintf(stderr,"%s\n",message ? message : "Shader validation failed");
            }
        },&shaderFailed);
        (void)wgpuDevicePoll(context.getDevice(),true,nullptr);
        if(shaderFailed) return false;
#endif
        if(!shader) return false;
        const auto storage=[&](uint32_t index,const void* data,size_t bytes) {
            buffers[index]=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::storage(bytes,false,"body_mass_table"));
            if(!buffers[index]) return false;
            if(data) wgpuQueueWriteBuffer(context.getQueue(),buffers[index],0,data,bytes);
            return true;
        };
        std::array<uint32_t,32> counters{}; counters[0]=1;
        const uint32_t active=1;
        if(!storage(0,poses.data(),sizeof(poses)) || !storage(1,motions.data(),sizeof(motions))
            || !storage(2,shapes.data(),sizeof(shapes)) || !storage(3,metadata.data(),sizeof(metadata))
            || !storage(4,nullptr,32) || !storage(5,counters.data(),sizeof(counters))
            || !storage(6,&bodyCommand,sizeof(bodyCommand)) || !storage(8,&active,sizeof(active))
            || !storage(9,nullptr,96) || !storage(10,nullptr,32)) return false;
        buffers[7]=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::uniform(sizeof(uniforms),"body_mass_uniforms"));
        if(!buffers[7]) return false;
        wgpuQueueWriteBuffer(context.getQueue(),buffers[7],0,uniforms.data(),sizeof(uniforms));
        using LE=gpu::BindGroupLayoutEntry; using BE=gpu::BindGroupEntry;
        std::vector<LE> commandEntries;
        for(auto binding:{0u,1u,2u,3u,5u,6u,7u}) commandEntries.push_back(LE(binding).computeVisible().storageBuffer(binding==7));
        commandEntries.push_back(LE(18).computeVisible().storageBuffer(true));
        commandEntries.push_back(LE(8).computeVisible().uniformBuffer(false,sizeof(uniforms)));
        layout=gpu::createBindGroupLayout(context.getDevice(),commandEntries,"body_mass_commands"); if(!layout) return false;
        const std::array layouts{layout}; pipelineLayouts[0]=gpu::createPipelineLayout(context.getDevice(),layouts,"body_mass_commands");
        WGPUComputePipelineDescriptor pd{}; pd.layout=pipelineLayouts[0]; pd.compute.module=shader; WGPU_SET_ENTRY_POINT(pd.compute,"apply_commands");
        pipeline=wgpuDeviceCreateComputePipeline(context.getDevice(),&pd); if(!pipeline) return false;
        const std::array commandBindings{BE(0).buffer(buffers[0]),BE(1).buffer(buffers[1]),BE(2).buffer(buffers[2]),
            BE(3).buffer(buffers[3]),BE(5).buffer(buffers[4]),BE(6).buffer(buffers[5]),BE(7).buffer(buffers[6]),BE(8).buffer(buffers[7]),
            BE(18).buffer(authoredAtlas ? authoredAtlas : buffers[10])};
        bindGroup=gpu::createBindGroup(context.getDevice(),layout,commandBindings,"body_mass_commands"); if(!bindGroup) return false;
        if(integrationEntry) {
            const bool staticContacts=std::string_view(integrationEntry)=="solve_static_contacts";
            std::vector<LE> entries;
            for(auto binding:{0u,1u,2u,3u,5u,6u,9u,15u}) {
                if(staticContacts && binding==5u) continue;
                entries.push_back(LE(binding).computeVisible().storageBuffer(false));
            }
            if(staticContacts) entries.push_back(LE(18).computeVisible().storageBuffer(true));
            entries.push_back(LE(8).computeVisible().uniformBuffer(false,sizeof(uniforms)));
            entries.push_back(LE(14).computeVisible().texture(WGPUTextureSampleType_Uint,WGPUTextureViewDimension_2D,false));
            if(!staticContacts) {
                entries.push_back(LE(16).computeVisible().texture(WGPUTextureSampleType_Float,WGPUTextureViewDimension_2DArray,false));
                entries.push_back(LE(17).computeVisible().sampler(WGPUSamplerBindingType_Filtering));
            }
            integrateLayout=gpu::createBindGroupLayout(context.getDevice(),entries,"body_mass_integrate"); if(!integrateLayout) return false;
            const std::array integrateLayouts{integrateLayout}; pipelineLayouts[1]=gpu::createPipelineLayout(context.getDevice(),integrateLayouts,"body_mass_integrate");
            pd.layout=pipelineLayouts[1]; WGPU_SET_ENTRY_POINT(pd.compute,integrationEntry);
            integratePipeline=wgpuDeviceCreateComputePipeline(context.getDevice(),&pd); if(!integratePipeline) return false;
            for(size_t i=0;i<2;++i) {
                WGPUTextureDescriptor td{}; td.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst; td.dimension=WGPUTextureDimension_2D;
                td.size={i==0?terrainWidth:1u,i==0?terrainHeight:1u,i==0 ? 1u : 4u}; td.format=i==0 ? WGPUTextureFormat_R32Uint : WGPUTextureFormat_RGBA8Unorm;
                td.mipLevelCount=1; td.sampleCount=1;
                textures[i]=wgpuDeviceCreateTexture(context.getDevice(),&td); if(!textures[i]) return false;
                WGPUTextureViewDescriptor vd{}; vd.format=td.format; vd.dimension=i==0 ? WGPUTextureViewDimension_2D : WGPUTextureViewDimension_2DArray;
                vd.mipLevelCount=1; vd.arrayLayerCount=td.size.depthOrArrayLayers; vd.aspect=WGPUTextureAspect_All;
                views[i]=wgpuTextureCreateView(textures[i],&vd); if(!views[i]) return false;
            }
            if(!terrainSamples.empty() && !gpu::writeTexture(context.getQueue(),textures[0],
                std::as_bytes(std::span<const uint32_t>(terrainSamples)),terrainWidth,terrainHeight,terrainWidth*4u)) return false;
            WGPUSamplerDescriptor sd{}; sd.addressModeU=sd.addressModeV=sd.addressModeW=WGPUAddressMode_ClampToEdge;
            sd.magFilter=sd.minFilter=WGPUFilterMode_Nearest; sd.mipmapFilter=WGPUMipmapFilterMode_Nearest; sd.lodMaxClamp=32; sd.maxAnisotropy=1;
            sampler=wgpuDeviceCreateSampler(context.getDevice(),&sd); if(!sampler) return false;
            std::vector<BE> bindings{BE(0).buffer(buffers[0]),BE(1).buffer(buffers[1]),BE(2).buffer(buffers[2]),BE(3).buffer(buffers[3]),
                BE(6).buffer(buffers[5]),BE(9).buffer(buffers[8]),BE(15).buffer(buffers[9]),BE(8).buffer(buffers[7]),BE(14).textureView(views[0])};
            if(staticContacts) bindings.push_back(BE(18).buffer(authoredAtlas));
            else {
                bindings.push_back(BE(5).buffer(buffers[4]));
                bindings.push_back(BE(16).textureView(views[1]));
                bindings.push_back(BE(17).sampler(sampler));
            }
            integrateGroup=gpu::createBindGroup(context.getDevice(),integrateLayout,bindings,"body_mass_integrate"); if(!integrateGroup) return false;
        }
        readback=gpu::createBuffer(context.getDevice(),gpu::BufferDesc{.label="body_mass_readback",.size=sizeof(Rows),.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
        return readback!=nullptr;
    }
    bool encode(Context& context) {
        WGPUCommandEncoderDescriptor ed{}; encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed); if(!encoder) return false;
        const auto dispatch=[&](WGPUComputePipeline selected,WGPUBindGroup bindings) {
            WGPUComputePassDescriptor pd{}; const auto pass=wgpuCommandEncoderBeginComputePass(encoder,&pd); if(!pass) return false;
            wgpuComputePassEncoderSetPipeline(pass,selected); wgpuComputePassEncoderSetBindGroup(pass,0,bindings,0,nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1); wgpuComputePassEncoderEnd(pass); wgpuComputePassEncoderRelease(pass); return true;
        };
        if(!dispatch(pipeline,bindGroup) || (integratePipeline && !dispatch(integratePipeline,integrateGroup))) return false;
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[0],0,readback,0,64);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[1],0,readback,64,64);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[2],0,readback,128,128);
        // New motion fixtures also inspect actual sector normalization. Row 8
        // would otherwise contain the unused slot-zero shape dimensions.
        if(captureBodyMetadata) wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[3],16,readback,128,16);
        if(captureTerrainContacts) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[0],32,readback,0,32);
            wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[1],32,readback,32,32);
            wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[3],16,readback,64,16);
            wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[9],48,readback,80,48);
            wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[5],0,readback,128,128);
        }
        WGPUCommandBufferDescriptor cd{}; command=wgpuCommandEncoderFinish(encoder,&cd); return command!=nullptr;
    }
};
Rows runBodyKernel(Context& context,BodyKernel& kernel,const char* integration=nullptr) {
    auto store=create(context); GpuShapeError error; const auto ticket=store->prepareSubmission({},error);
    if(!ticket.valid() || !kernel.initialize(context,integration) || !kernel.encode(context)) throw std::runtime_error("Body kernel encoding");
    const std::array commands{kernel.command}; if(store->submit(ticket,commands)!=GpuShapeError::None) throw std::runtime_error("Body kernel submit");
    Rows rows{}; if(!kernel.read(context,rows) || !drain(context,*store)) throw std::runtime_error("Body kernel GPU validation: "+std::string(store->failure()));
    close(context,*store); return rows;
}
TEST_F(GpuAuthoredShapes, BodyKernelForceUsesPreparedPrincipalInertia) {
    const auto shape=prepared(); const auto& mass=shape.packedMass(); const auto point=shape.massFrame().bodyPoint({1,-2,3});
    BodyKernel kernel;
    kernel.poses[3]={mass.rootFromBodyQuaternion[0],mass.rootFromBodyQuaternion[1],mass.rootFromBodyQuaternion[2],mass.rootFromBodyQuaternion[3]};
    kernel.shapes[1].inverseInertiaMaterial={mass.inverseInertiaRadius[0],mass.inverseInertiaRadius[1],mass.inverseInertiaRadius[2],0};
    kernel.bodyCommand.p0={2,3,-4,0}; kernel.bodyCommand.p1={static_cast<float>(point[0]),static_cast<float>(point[1]),static_cast<float>(point[2]),0};
    const auto rows=runBodyKernel(context,kernel,"prepare_dynamic_bodies");
    near(rows,6,{.4f/60,.6f/60,-.8f/60}); near(rows,7,{-1357.0f/216000.0f,5863.0f/108000.0f,56.0f/2700.0f});
    near(rows,13,{mass.inverseInertiaRadius[0],mass.inverseInertiaRadius[1],mass.inverseInertiaRadius[2]});
    near(rows,2,{.25f,-.5f,.75f}); EXPECT_EQ(rows[15],(std::array<uint32_t,4>{}));
}
TEST_F(GpuAuthoredShapes, BodyKernelBallisticPoseAdvancesAboutItsStoredCenter) {
    const auto shape=prepared(); const auto& mass=shape.packedMass(); BodyKernel kernel;
    const auto& q=mass.rootFromBodyQuaternion; kernel.poses[3]={q[0],q[1],q[2],q[3]};
    kernel.motions[2]={3,4,5,0}; kernel.motions[3]={0,0,2,0}; kernel.bodyCommand.header.x=8; // Wake, no force.
    const auto rows=runBodyKernel(context,kernel,"integrate_bodies");
    near(rows,2,{.3f,-.5f+4.0f/60,.75f+5.0f/60}); near(rows,6,{3,4,5}); near(rows,7,{0,0,2});
    const float angle=2.0f/60;
    const glm::quat expected=glm::angleAxis(angle,glm::vec3(0,0,1))*glm::quat(q[3],q[0],q[1],q[2]);
    near(rows,3,{expected.x,expected.y,expected.z},0.000005f);
    EXPECT_NEAR(std::bit_cast<float>(rows[3][3]),expected.w,0.000005f);
    // Independent world root reconstruction after the COM-centered rotation.
    const glm::quat actual(std::bit_cast<float>(rows[3][3]),std::bit_cast<float>(rows[3][0]),std::bit_cast<float>(rows[3][1]),std::bit_cast<float>(rows[3][2]));
    const glm::vec3 actualCenter(std::bit_cast<float>(rows[2][0]),std::bit_cast<float>(rows[2][1]),std::bit_cast<float>(rows[2][2]));
    const glm::vec3 root=actualCenter-(actual*glm::conjugate(glm::quat(q[3],q[0],q[1],q[2])))*glm::vec3(.25f,-.5f,.75f);
    const glm::vec3 expectedRoot(.3f-(.25f*std::cos(angle)+.5f*std::sin(angle)),
        -.5f+4.0f/60-(.25f*std::sin(angle)-.5f*std::cos(angle)),5.0f/60);
    for(size_t i=0;i<3;++i) EXPECT_NEAR(root[static_cast<int>(i)],expectedRoot[static_cast<int>(i)],0.000005f);
}
TEST_F(GpuAuthoredShapes, BodyKernelSpawnAndDestroyClearTheIndependentShapeReference) {
    BodyKernel spawn; spawn.metadata[1].w=1; spawn.shapes[1].authoredShape={37,53,1,0};
    spawn.bodyCommand.header.x=0; spawn.bodyCommand.p0={0,0,0,.2f}; spawn.bodyCommand.p1={0,0,0,1}; spawn.bodyCommand.p4={2,2,2,1};
    const auto created=runBodyKernel(context,spawn); EXPECT_EQ(created[15],(std::array<uint32_t,4>{}));
    near(created,13,{.3f,.3f,.3f}); near(created,12,{2,2,2});
    BodyKernel destroy; destroy.shapes[1].authoredShape={37,53,1,0}; destroy.bodyCommand.header.x=1;
    const auto retired=runBodyKernel(context,destroy); EXPECT_EQ(retired[15],(std::array<uint32_t,4>{}));
    EXPECT_EQ(retired[12],(std::array<uint32_t,4>{})); EXPECT_EQ(retired[13],(std::array<uint32_t,4>{}));
}
TEST_F(GpuAuthoredShapes, BodyKernelMaterialChangesPreserveMassAndShapeIdentity) {
    BodyKernel kernel; kernel.shapes[1].authoredShape={37,53,1,0}; kernel.bodyCommand.header.x=7;
    kernel.bodyCommand.p5.w=std::bit_cast<int32_t>(0xb1234567u); kernel.bodyCommand.p6={.4f,.5f,.6f,7};
    const auto rows=runBodyKernel(context,kernel);
    EXPECT_EQ(rows[15],(std::array<uint32_t,4>{37,53,1,0}));
    near(rows,13,{.5f,1.0f/3,.25f}); EXPECT_EQ(rows[13][3],0xb1234567u); near(rows,14,{.4f,.5f,.6f});
    EXPECT_EQ(std::bit_cast<float>(rows[14][3]),7.0f);
}

void seedConvertedMotion(BodyKernel& kernel,const AuthoredBodyMotion& body,const PackedShapeMass& mass) {
    kernel.poses[2]=glm::vec4(body.centerPosition.local,mass.centerInverseMass[3]);
    kernel.poses[3]={body.orientation.x,body.orientation.y,body.orientation.z,body.orientation.w};
    kernel.motions[2]=glm::vec4(body.centerVelocity,0); kernel.motions[3]=glm::vec4(body.angularVelocity,0);
    kernel.shapes[1].inverseInertiaMaterial={mass.inverseInertiaRadius[0],mass.inverseInertiaRadius[1],mass.inverseInertiaRadius[2],0};
    for(int i=0;i<3;++i) kernel.metadata[1][i]=std::bit_cast<uint32_t>(body.centerPosition.sector[i]);
    kernel.captureBodyMetadata=true;
}

ShapeHandle uploadTerrainHull(Context& context,IAuthoredShapeResources& store,bool gap=false,
                             geometry::GridBox bounds={{-50,-50,-50},{50,50,50}}) {
    using namespace geometry;
    const std::array<UnionBox,2> pontoons{{{{{-50,-50,-50},{-25,50,50}},7},
                                         {{{25,-50,-50},{50,50,50}},11}}};
    const std::array<UnionBox,1> cube{{{bounds,7}}};
    BoxUnionIssue unionIssue;
    auto volume=BoxUnion::compile(gap ? std::span<const UnionBox>(pontoons) : std::span<const UnionBox>(cube),unionIssue);
    if(!volume) throw std::runtime_error("terrain hull geometry");
    // Deliberately offset COM and nondiagonal inertia exercise the shipping
    // principal-frame conversion even for a geometrically symmetric hull.
    const RigidMassInput mass{5,{.25,-.5,.75},
        {88.0/25,-2.0/75,8.0/15,-2.0/75,161.0/75,2.0/5,8.0/15,2.0/5,10.0/3}};
    AuthoredShapeIssue issue; auto shape=AuthoredShape::prepare(*volume,mass,issue);
    if(!shape) throw std::runtime_error("terrain hull mass");
    GpuShapeError error; const auto handle=store.upload(std::move(*shape),error);
    if(!handle.valid() || !drain(context,store)) throw std::runtime_error("terrain hull upload");
    return handle;
}

struct AuthoredTerrainFixture : BodyKernel {
    Context& context;
    IAuthoredShapeResources& store;
    ShapeHandle handle;
    AuthoredTerrainFixture(Context& c,IAuthoredShapeResources& s,ShapeHandle h,
                           AuthoredRootMotion root={}) : context(c),store(s),handle(h) {
        const auto* shape=store.get(handle); if(!shape) throw std::runtime_error("terrain shape");
        AuthoredFrameError error; const auto body=AuthoredBodyFrame(*shape).bodyMotion(root,error);
        if(!body) throw std::runtime_error("terrain body frame");
        seedConvertedMotion(*this,*body,shape->packedMass());
        const auto diameter=2*shape->packedMass().inverseInertiaRadius[3];
        shapes[1].dimensionsType={diameter,diameter,diameter,2};
        shapes[1].authoredShape={handle.index,handle.generation,1,0};
        bodyCommand.header.x=8; captureTerrainContacts=true;
        uniforms[8].x=.01f;
        const glm::ivec4 world(root.position.sector,0); std::memcpy(&uniforms[12],&world,sizeof(world));
        field(9,.25f);
    }
    static uint32_t height(double y) { return static_cast<uint32_t>(std::lround((y/2+1)*32767.5)); }
    void field(uint32_t size,float cell) {
        terrainWidth=terrainHeight=size;
        terrainSamples.assign(static_cast<size_t>(size)*size,height(0));
        const float origin=float(size-1)*cell*.5f;
        uniforms[4]={origin,origin,cell,2};
        const glm::uvec4 terrain(size,size,1,1); std::memcpy(&uniforms[5],&terrain,sizeof(terrain));
    }
    void lego(uint32_t size=9,float cell=1.0f) {
        field(size,cell);
        const glm::uvec4 terrain(size,size,1,2);std::memcpy(&uniforms[5],&terrain,sizeof(terrain));
    }
    Rows run() {
        GpuShapeError error; const std::array handles{handle};
        const auto ticket=store.prepareSubmission(handles,error);
        if(!ticket.valid() || !initialize(context,"solve_static_contacts",store.buffer()) || !encode(context))
            throw std::runtime_error("terrain encoding");
        const std::array commands{command};
        if(store.submit(ticket,commands)!=GpuShapeError::None) throw std::runtime_error("terrain submit");
        Rows rows{};
        if(!read(context,rows) || !drain(context,store)) throw std::runtime_error("terrain GPU: "+std::string(store.failure()));
        return rows;
    }
};

TEST_F(GpuAuthoredShapes,TerrainHullFaceInteriorCatchesSeabedPeak) {
    auto store=create(context); const auto handle=uploadTerrainHull(context,*store);
    {
        AuthoredRootMotion root; root.position.local.y=1.05f;
        AuthoredTerrainFixture fixture(context,*store,handle,root);
        fixture.terrainSamples[40]=AuthoredTerrainFixture::height(.25);
        const auto rows=fixture.run();
        EXPECT_EQ(rows[12][0],0u); // No incomplete terrain evaluation.
        ASSERT_GT(rows[7][1],0u); ASSERT_LE(rows[7][1],4u);
        EXPECT_GT(std::bit_cast<float>(rows[0][1]),fixture.poses[2].y);
        EXPECT_GT(std::bit_cast<float>(rows[2][1]),0);
        for(uint32_t i=0;i<rows[7][1];++i) {
            ASSERT_NE(rows[6][i]&0x80000000u,0u);
            const auto face=rows[6][i]&0x7fffffffu;
            ASSERT_LT(face,store->get(handle)->faces().size());
            const auto& exterior=store->get(handle)->faces()[face];
            EXPECT_EQ(exterior.source,7u); EXPECT_EQ(exterior.axis,1u); EXPECT_EQ(exterior.sign,-1);
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,LegoHullRestsOnRoundStudCapsAndNotSmoothHeightfield) {
    auto store=create(context);const auto handle=uploadTerrainHull(context,*store);
    const float cap=terrain::lego::top(static_cast<uint16_t>(AuthoredTerrainFixture::height(0)),2,1)+.18f;
    for(bool touching:{true,false}) {
        AuthoredRootMotion root;root.position.local={0,1+cap+(touching?-.03f:.03f),0};
        root.originVelocity={0,-1,0};
        root.orientation=glm::angleAxis(.31f,glm::vec3(0,1,0));
        AuthoredTerrainFixture fixture(context,*store,handle,root);fixture.lego();
        const auto rows=fixture.run();EXPECT_EQ(rows[12][0],0u);
        if(touching) { EXPECT_GT(rows[7][1],0u);EXPECT_GT(std::bit_cast<float>(rows[2][1]),-.9f); }
        else { EXPECT_EQ(rows[7][1],0u);near(rows,2,{0,-1,0}); }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,LegoRaisedColumnInsidePontoonGapHasNoFalseSupport) {
    auto store=create(context);const auto handle=uploadTerrainHull(context,*store,true);
    AuthoredRootMotion root;root.position.local.y=1.05f;
    AuthoredTerrainFixture fixture(context,*store,handle,root);fixture.lego(9,.25f);
    std::fill(fixture.terrainSamples.begin(),fixture.terrainSamples.end(),AuthoredTerrainFixture::height(-2));
    fixture.terrainSamples[40]=AuthoredTerrainFixture::height(.25);
    const auto rows=fixture.run();EXPECT_EQ(rows[12][0],0u);EXPECT_EQ(rows[7][1],0u);near(rows,2,{0,0,0});
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,LegoExposedRiserStopsSidewaysHullMotion) {
    auto store=create(context);const auto handle=uploadTerrainHull(context,*store);
    AuthoredRootMotion root;root.position.local={-.99f,1.1f,0};root.originVelocity={1,0,0};
    AuthoredTerrainFixture fixture(context,*store,handle,root);fixture.lego();
    for(uint32_t z=0;z<9;z++)for(uint32_t x=0;x<9;x++)
        fixture.terrainSamples[z*9+x]=AuthoredTerrainFixture::height(x>=4?.6:-2);
    const auto rows=fixture.run();EXPECT_EQ(rows[12][0],0u);ASSERT_GT(rows[7][1],0u);
    EXPECT_LT(std::bit_cast<float>(rows[2][0]),.9f);
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,LegoStudSidesAreRoundAndGapsBetweenStudsStayOpen) {
    auto store=create(context);const auto handle=uploadTerrainHull(context,*store,false,{{-2,-2,-2},{2,2,2}});
    const float top=terrain::lego::top(static_cast<uint16_t>(AuthoredTerrainFixture::height(0)),2,1);
    for(bool touching:{true,false}) {
        AuthoredRootMotion root;
        // Stud at (.5,.5), radius .3. The second probe is inside its bounding
        // square but outside the actual circle: a box proxy would hit it.
        root.position.local=touching ? glm::vec3(.82f,top+.09f,.5f) : glm::vec3(.78f,top+.09f,.78f);
        root.originVelocity={-1,0,0};
        AuthoredTerrainFixture fixture(context,*store,handle,root);fixture.lego();
        const auto rows=fixture.run();EXPECT_EQ(rows[12][0],0u);
        if(touching) { EXPECT_GT(rows[7][1],0u);EXPECT_GT(std::bit_cast<float>(rows[2][0]),-.9f); }
        else { EXPECT_EQ(rows[7][1],0u);near(rows,2,{-1,0,0}); }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,TerrainPontoonGapDoesNotCreateFalseGround) {
    auto store=create(context); const auto handle=uploadTerrainHull(context,*store,true);
    {
        AuthoredRootMotion root; root.position.local.y=1.05f;
        AuthoredTerrainFixture fixture(context,*store,handle,root);
        fixture.terrainSamples[40]=AuthoredTerrainFixture::height(.25);
        const auto rows=fixture.run();
        EXPECT_EQ(rows[12][0],0u); EXPECT_EQ(rows[7][1],0u);
        near(rows,0,{fixture.poses[2].x,fixture.poses[2].y,fixture.poses[2].z});
        near(rows,2,{0,0,0}); near(rows,3,{0,0,0});
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,TerrainContactsRestoreRotatedRootInDistantSectors) {
    auto store=create(context); const auto handle=uploadTerrainHull(context,*store);
    {
        AuthoredRootMotion root; root.position={{1'000'000,-1'000'000,4},{0,.95f,0}};
        root.orientation=glm::angleAxis(.7f,glm::vec3(0,1,0));
        AuthoredTerrainFixture fixture(context,*store,handle,root);
        const auto rows=fixture.run();
        EXPECT_EQ(rows[12][0],0u); ASSERT_GT(rows[7][1],0u);
        EXPECT_NEAR(std::bit_cast<float>(rows[0][1])-fixture.poses[2].y,.0157607f,.00003f);
        for(int i=0;i<3;++i) EXPECT_EQ(std::bit_cast<int32_t>(rows[4][static_cast<size_t>(i)]),root.position.sector[i]);
        EXPECT_GT(std::bit_cast<float>(rows[2][1]),0);
        for(size_t row=0;row<4;++row) for(uint32_t value:rows[row]) EXPECT_TRUE(std::isfinite(std::bit_cast<float>(value)));
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,TerrainIncompleteGeometryStopsWithoutPrimitiveFallback) {
    auto store=create(context); const auto handle=uploadTerrainHull(context,*store);
    for(uint32_t failure:{1u,4u,2u}) {
        AuthoredRootMotion root; root.position.local.y=.95f; root.originVelocity={0,-1,0};
        AuthoredTerrainFixture fixture(context,*store,handle,root);
        if(failure==1u) fixture.shapes[1].authoredShape.y++;
        if(failure==4u) {
            const glm::uvec4 terrain(9,9,1,3); std::memcpy(&fixture.uniforms[5],&terrain,sizeof(terrain));
        }
        if(failure==2u) fixture.field(129,1.0f/64);
        const auto rows=fixture.run();
        EXPECT_EQ(rows[12][0],1u); EXPECT_EQ(rows[7][1],0u); EXPECT_EQ(rows[7][2],failure);
        if(failure==2u) { EXPECT_EQ(rows[7][3],8192u); }
        near(rows,0,{fixture.poses[2].x,fixture.poses[2].y,fixture.poses[2].z});
        near(rows,2,{0,0,0}); near(rows,3,{0,0,0});
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, BodyKernelConvertedRootForceMatchesWorldTensor) {
    const auto shape=prepared(); const AuthoredBodyFrame frame(shape); AuthoredFrameError error;
    AuthoredRootMotion root;
    const float half=std::sqrt(.5f); root.orientation={half,0,0,half};
    root.position.sector={1'000'000,-1'000'000,4};
    const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
    const auto point=frame.bodyPoint({1,-2,3},error); ASSERT_TRUE(point);
    BodyKernel kernel; seedConvertedMotion(kernel,*body,shape.packedMass());
    // Rotate the original analytical force by Rz(90 degrees). The root-local
    // application point stays authored; only its body adapter is used here.
    kernel.bodyCommand.p0={-3,2,-4,0}; kernel.bodyCommand.p1=glm::vec4(*point,0);
    const auto rows=runBodyKernel(context,kernel,"prepare_dynamic_bodies");
    near(rows,6,{-.6f/60,.4f/60,-.8f/60},.000002f);
    near(rows,7,{-5863.0f/108000.0f,-1357.0f/216000.0f,56.0f/2700.0f},.000002f);
    near(rows,2,{.5f,.25f,.75f},.000002f);
    for(int i=0;i<3;++i) EXPECT_EQ(std::bit_cast<int32_t>(rows[8][static_cast<size_t>(i)]),root.position.sector[i]);
}

TEST_F(GpuAuthoredShapes, BodyKernelConvertedRootMotionCrossesSectorsAndRestoresOrigin) {
    const auto shape=prepared(); const AuthoredBodyFrame frame(shape); AuthoredFrameError error;
    AuthoredRootMotion root;
    const float half=std::sqrt(.5f); root.orientation={half,0,0,half};
    root.position={{1'000'000,-1'000'000,2'000'000'000},{127.46875f,-127.875f,127.1875f}};
    root.originVelocity={3,4,5}; root.angularVelocity={0,0,2};
    const auto body=frame.bodyMotion(root,error); ASSERT_TRUE(body);
    BodyKernel kernel; seedConvertedMotion(kernel,*body,shape.packedMass());
    kernel.bodyCommand.header.x=8; // Wake; isolate free COM integration.
    const auto rows=runBodyKernel(context,kernel,"integrate_bodies");
    AuthoredBodyMotion completed;
    for(int i=0;i<3;++i) {
        const auto axis=static_cast<size_t>(i);
        completed.centerPosition.local[i]=std::bit_cast<float>(rows[2][axis]);
        completed.centerPosition.sector[i]=std::bit_cast<int32_t>(rows[8][axis]);
        completed.centerVelocity[i]=std::bit_cast<float>(rows[6][axis]);
        completed.angularVelocity[i]=std::bit_cast<float>(rows[7][axis]);
    }
    completed.orientation={std::bit_cast<float>(rows[3][3]),std::bit_cast<float>(rows[3][0]),
        std::bit_cast<float>(rows[3][1]),std::bit_cast<float>(rows[3][2])};
    EXPECT_EQ(completed.centerPosition.sector,root.position.sector+glm::ivec3(1,0,1));
    ASSERT_TRUE(isValidWorldPosition(completed.centerPosition));
    const auto restored=frame.rootMotion(completed,error); ASSERT_TRUE(restored);
    EXPECT_EQ(restored->position.sector,root.position.sector);
    // Closed-form rotation for constant world omega. The shader uses a
    // normalized Euler quaternion step, whose one-step error is below 2e-5.
    const double angle=2.0/60.0,c=std::cos(angle),s=std::sin(angle);
    const glm::dvec3 offset(.5*c-.25*s,.5*s+.25*c,.75);
    const glm::dvec3 expectedCenter(127.96875+2.5/60.0,-127.625+5.0/60.0,127.9375+5.0/60.0);
    const glm::dvec3 expectedRoot=expectedCenter-offset;
    const glm::dvec3 expectedVelocity(2.5+2*offset.y,5-2*offset.x,5);
    for(int i=0;i<3;++i) {
        EXPECT_NEAR(static_cast<double>(restored->position.local[i]),expectedRoot[i],.00002);
        EXPECT_NEAR(static_cast<double>(restored->originVelocity[i]),expectedVelocity[i],.00002);
    }
    near(rows,6,{2.5f,5,5},.000002f); near(rows,7,{0,0,2},.000002f);
}

template<class T> T readGpuValue(Context& context,WGPUBuffer buffer) {
    auto complete=std::make_shared<std::atomic<uint32_t>>(0);
    using Payload=std::shared_ptr<std::atomic<uint32_t>>;
    auto* payload=new Payload(complete);
#if defined(VOXY_WASM)
    WGPUBufferMapCallbackInfo info=WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    info.mode=WGPUCallbackMode_AllowSpontaneous; info.userdata1=payload;
    info.callback=[](WGPUMapAsyncStatus status,WGPUStringView,void* data,void*) {
        const std::unique_ptr<Payload> holder{static_cast<Payload*>(data)};
        (*holder)->store(status==WGPUMapAsyncStatus_Success?1u:2u,std::memory_order_release);
    };
    static_cast<void>(wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,sizeof(T),info));
#else
    wgpuBufferMapAsync(buffer,WGPUMapMode_Read,0,sizeof(T),[](WGPUBufferMapAsyncStatus status,void* data) {
        const std::unique_ptr<Payload> holder{static_cast<Payload*>(data)};
        (*holder)->store(status==WGPUBufferMapAsyncStatus_Success?1u:2u,std::memory_order_release);
    },payload);
#endif
    if(!wait(context,[&]{return complete->load(std::memory_order_acquire)!=0;}) || complete->load()!=1)
        throw std::runtime_error("contact GPU readback timeout/failure");
    const auto* bytes=wgpuBufferGetConstMappedRange(buffer,0,sizeof(T));
    if(!bytes) throw std::runtime_error("contact GPU readback range");
    T result; std::memcpy(&result,bytes,sizeof(T)); wgpuBufferUnmap(buffer); return result;
}

struct AuthoredContactFixture {
    struct Snapshot {
        GpuContactManifold manifold;
        std::array<uint32_t,32> telemetry;
        std::array<glm::vec4,6> poses,motions;
        std::array<GpuContactManifold,8> patches{};
    };
    Context& context;
    IAuthoredShapeResources& store;
    GpuNarrowPhase narrow;
    std::array<glm::vec4,6> poses{};
    std::array<glm::vec4,6> motions{};
    std::array<GpuBodyShape,3> shapes{};
    std::array<glm::ivec4,3> metadata{};
    std::array<ShapeHandle,3> handles{};
    std::array<WGPUBuffer,7> buffers{};
    uint32_t patchSlots = 1;
    AuthoredContactFixture(Context& c,IAuthoredShapeResources& s,uint32_t patches=1):context(c),store(s),patchSlots(patches) {
        GpuNarrowPhase::Config config; config.pairCapacity=1; config.workgroupSize=64;
        config.normalPatchesPerPair=patches;
        if(!narrow.initialize(context.getDevice(),context.getQueue(),config))
            throw std::runtime_error("contact pipeline");
        const auto uploadBuffer=[&](auto& values,const char* label) {
            using T=typename std::remove_reference_t<decltype(values)>::value_type;
            return gpu::createBufferWithData(context.getDevice(),context.getQueue(),
                gpu::BufferDesc{.label=label,.size=sizeof(values),
                    .usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopyDst|WGPUBufferUsage_CopySrc},
                std::span<const T>(values));
        };
        std::array<GpuKeyValue,1> pairs{{{1,2,0,0}}};
        std::array<uint32_t,32> telemetry{}; telemetry[3]=1;
        buffers[0]=uploadBuffer(poses,"contact_poses");
        buffers[1]=uploadBuffer(shapes,"contact_shapes");
        buffers[2]=uploadBuffer(metadata,"contact_metadata");
        buffers[3]=uploadBuffer(pairs,"contact_pairs");
        buffers[4]=uploadBuffer(telemetry,"contact_broad_telemetry");
        buffers[6]=uploadBuffer(motions,"contact_motions");
        buffers[5]=gpu::createBuffer(context.getDevice(),gpu::BufferDesc{
            .label="contact_readback",.size=sizeof(Snapshot),
            .usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
        for(auto buffer:buffers) if(!buffer) throw std::runtime_error("contact buffer");
    }
    ~AuthoredContactFixture() { narrow.shutdown(); for(auto buffer:buffers) if(buffer) wgpuBufferRelease(buffer); }
    void primitive(uint32_t body,glm::vec3 position,glm::vec4 dimensions,
                   glm::quat orientation={1,0,0,0},glm::ivec3 sector={0,0,0}) {
        handles[body]={}; poses[2*body]=glm::vec4(position,1);
        poses[2*body+1]={orientation.x,orientation.y,orientation.z,orientation.w};
        metadata[body]=glm::ivec4(sector,1|(1<<20)|(1<<21));
        shapes[body]={}; shapes[body].dimensionsType=dimensions;
        shapes[body].inverseInertiaMaterial={1,1,1,0};
    }
    void authored(uint32_t body,ShapeHandle handle,AuthoredRootMotion root={}) {
        const auto* shape=store.get(handle); if(!shape) throw std::runtime_error("contact shape");
        AuthoredFrameError error; const auto motion=AuthoredBodyFrame(*shape).bodyMotion(root,error);
        if(!motion) throw std::runtime_error("contact frame");
        const auto& mass=shape->packedMass();
        const float diameter=2*mass.inverseInertiaRadius[3]; // COM-centered conservative broad-phase proxy.
        primitive(body,motion->centerPosition.local,{diameter,diameter,diameter,2},
            motion->orientation,motion->centerPosition.sector);
        handles[body]=handle; poses[2*body].w=mass.centerInverseMass[3];
        shapes[body].authoredShape={handle.index,handle.generation,1,0};
        for(glm::length_t i=0;i<3;++i) shapes[body].inverseInertiaMaterial[i]=mass.inverseInertiaRadius[static_cast<size_t>(i)];
    }
    Snapshot run(bool atlas=true,bool declare=true,GpuDynamicSolver* solver=nullptr) {
        if(!gpu::writeBuffer(context.getQueue(),buffers[0],0,std::span<const glm::vec4>(poses))
            || !gpu::writeBuffer(context.getQueue(),buffers[1],0,std::span<const GpuBodyShape>(shapes))
            || !gpu::writeBuffer(context.getQueue(),buffers[2],0,std::span<const glm::ivec4>(metadata)))
            throw std::runtime_error("contact input write");
        if(!gpu::writeBuffer(context.getQueue(),buffers[6],0,std::span<const glm::vec4>(motions)))
            throw std::runtime_error("contact motion write");
        narrow.setInput({buffers[0],buffers[1],buffers[3],buffers[4],3,1,buffers[2],atlas?store.buffer():nullptr});
        std::array<ShapeHandle,2> used{}; size_t count=0;
        if(declare) for(auto h:handles) if(h.valid()) used[count++]=h;
        ShapeResourceError error;
        const auto ticket=store.prepareSubmission(std::span<const ShapeHandle>(used).first(count),error);
        if(!ticket.valid()) throw std::runtime_error("contact use declaration");
        WGPUCommandEncoderDescriptor ed{}; auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        if(!encoder || !narrow.encode(encoder)) {
            if(encoder) wgpuCommandEncoderRelease(encoder);
            (void)store.discard(ticket); throw std::runtime_error("contact encode");
        }
        if(solver) {
            solver->setInput({buffers[0],buffers[6],buffers[1],buffers[2],
                narrow.activeManifolds(),narrow.telemetryBuffer(),3,patchSlots,
                narrow.activeContactDispatchBuffer(),GpuNarrowPhase::kActiveContactDispatchOffset});
            if(!solver->encode(encoder) || !narrow.encodeCommitActiveManifolds(encoder)) {
                wgpuCommandEncoderRelease(encoder); (void)store.discard(ticket);
                throw std::runtime_error("authored contact solver encode");
            }
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder,narrow.manifolds(),0,buffers[5],0,sizeof(GpuContactManifold));
        wgpuCommandEncoderCopyBufferToBuffer(encoder,narrow.telemetryBuffer(),0,buffers[5],sizeof(GpuContactManifold),128);
        wgpuCommandEncoderCopyBufferToBuffer(encoder,narrow.manifolds(),0,buffers[5],offsetof(Snapshot,patches),patchSlots*sizeof(GpuContactManifold));
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[0],0,buffers[5],offsetof(Snapshot,poses),sizeof(poses));
        wgpuCommandEncoderCopyBufferToBuffer(encoder,buffers[6],0,buffers[5],offsetof(Snapshot,motions),sizeof(motions));
        WGPUCommandBufferDescriptor cd{}; auto command=wgpuCommandEncoderFinish(encoder,&cd);
        wgpuCommandEncoderRelease(encoder);
        const std::array commands{command}; auto status=store.submit(ticket,commands);
        if(command) wgpuCommandBufferRelease(command);
        if(status!=ShapeResourceError::None) throw std::runtime_error("contact submit");
        auto result=readGpuValue<Snapshot>(context,buffers[5]);
        if(!drain(context,store)) throw std::runtime_error("contact completion: "+std::string(store.failure()));
        return result;
    }
    glm::vec3 anchorWorld(const GpuManifoldPoint& point,bool isA) const {
        const uint32_t body=isA?2u:1u;
        const auto& local=isA?point.localAnchorASeparation:point.localAnchorBNormalImpulse;
        const auto q=poses[body*2+1];
        return glm::vec3(poses[body*2])+glm::quat(q.w,q.x,q.y,q.z)*glm::vec3(local[0],local[1],local[2]);
    }
};
void contactHit(const AuthoredContactFixture::Snapshot& result,float separation,glm::vec3 normal) {
    ASSERT_GT(result.manifold.state[0],0u);
    ASSERT_LE(result.manifold.state[0],4u);
    EXPECT_EQ(result.telemetry[16],0u);
    for(size_t i=0;i<3;++i) EXPECT_NEAR(result.manifold.normal[i],normal[static_cast<glm::length_t>(i)],.0005f);
    for(uint32_t i=0;i<result.manifold.state[0];++i)
        EXPECT_NEAR(result.manifold.points[i].localAnchorASeparation[3],separation,.0005f);
}

TEST_F(GpuAuthoredShapes,ContactSphereUsesExteriorAndPreservesConcaveGap) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredContactFixture contact(context,*store);
        for (uint32_t authoredBody : {1u,2u}) {
            SCOPED_TRACE(authoredBody);
            const uint32_t sphereBody=3u-authoredBody;
            contact.authored(authoredBody,handle);
            contact.primitive(sphereBody,{1.65f,.25f,.25f},{.2f,.2f,.2f,0});
            auto hit=contact.run(); contactHit(hit,-.05f,{authoredBody==1u?-1.0f:1.0f,0,0});
            ASSERT_GT(hit.manifold.state[0],0u);
            const auto face=hit.manifold.points[0].features[authoredBody==1u?1u:0u]&0x7fffffffu;
            ASSERT_LT(face,store->get(handle)->faces().size()); EXPECT_EQ(store->get(handle)->faces()[face].source,11u);
            const auto point=contact.anchorWorld(hit.manifold.points[0],authoredBody==2u);
            EXPECT_NEAR(point.x,1.6f,.0005f);
            contact.primitive(sphereBody,{1.3f,-.25f,.25f},{.2f,.2f,.2f,0});
            EXPECT_EQ(contact.run().manifold.state[0],0u);
            contact.primitive(sphereBody,{1.1f,.25f,.25f},{.04f,.04f,.04f,0});
            hit=contact.run(); ASSERT_EQ(hit.manifold.state[0],1u);
            EXPECT_NEAR(hit.manifold.points[0].localAnchorASeparation[3],-.27f,.0005f);
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactCapsuleFindsThinRailWithoutSegmentSampling) {
    auto store=create(context);
    const geometry::UnionBox box{{{-5,11,-5},{5,12,5}},41}; geometry::BoxUnionIssue issue;
    auto geometry=geometry::BoxUnion::compile(std::span{&box,1},issue); ASSERT_TRUE(geometry);
    AuthoredShapeIssue shapeIssue; auto shape=AuthoredShape::prepare(*geometry,{5,{.25,-.5,.75},{3,0,0,0,3,0,0,0,3}},shapeIssue);
    ASSERT_TRUE(shape); ShapeResourceError error; auto handle=store->upload(std::move(*shape),error); ASSERT_TRUE(drain(context,*store));
    {
        AuthoredContactFixture contact(context,*store);
        for (uint32_t authoredBody : {1u,2u}) {
            SCOPED_TRACE(authoredBody);
            const uint32_t capsuleBody=3u-authoredBody;
            contact.authored(authoredBody,handle);
            contact.primitive(capsuleBody,{.1005f,0,0},{.002f,40,.002f,3});
            const auto hit=contact.run(); contactHit(hit,-.0005f,{authoredBody==1u?-1.0f:1.0f,0,0});
            ASSERT_EQ(hit.manifold.state[0],1u);
            const auto point=contact.anchorWorld(hit.manifold.points[0],authoredBody==2u);
            EXPECT_NEAR(point.x,.1f,.0005f);
            EXPECT_GE(point.y,.219f); EXPECT_LE(point.y,.241f);
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactBoxClipsToExteriorPatchesAndLeavesNotchOpen) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredContactFixture contact(context,*store); contact.authored(1,handle);
        contact.primitive(2,{1.05f,-.25f,.25f},{.2f,.2f,.2f,2});
        auto hit=contact.run(); contactHit(hit,-.05f,{-1,0,0});
        ASSERT_EQ(hit.manifold.state[0],4u); uint32_t corners=0;
        for(uint32_t i=0;i<hit.manifold.state[0];++i) {
            const auto point=contact.anchorWorld(hit.manifold.points[i],false);
            EXPECT_NEAR(point.x,1,.0005f); EXPECT_LT(point.y,0);
            EXPECT_NEAR(std::abs(point.y+.25f),.1f,.0005f);
            EXPECT_NEAR(std::abs(point.z-.25f),.1f,.0005f);
            corners|=1u<<((point.y>-.25f?1u:0u)|(point.z>.25f?2u:0u));
        }
        EXPECT_EQ(corners,15u);
        contact.primitive(2,{1.65f,.25f,.25f},{.2f,.2f,.2f,2});
        hit=contact.run(); contactHit(hit,-.05f,{-1,0,0});
        contact.primitive(2,{1.3f,-.25f,.25f},{.2f,.2f,.2f,2});
        hit=contact.run(); EXPECT_EQ(hit.manifold.state[0],0u); EXPECT_EQ(hit.telemetry[16],0u);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactBoxBodyOrderPreservesExteriorAnchors) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredContactFixture contact(context,*store);
        // The broad phase orders by body ID, not shape type. Exercise both
        // branches of the authored-body reorder with the same physical pair.
        for (uint32_t authoredBody : {1u,2u}) {
            SCOPED_TRACE(authoredBody);
            const uint32_t boxBody=3u-authoredBody;
            contact.authored(authoredBody,handle);
            contact.primitive(boxBody,{1.05f,-.25f,.25f},{.2f,.2f,.2f,2});
            auto hit=contact.run();
            contactHit(hit,-.05f,{authoredBody==1u?-1.0f:1.0f,0,0});
            ASSERT_EQ(hit.manifold.state[0],4u);
            uint32_t corners=0;
            for(uint32_t i=0;i<hit.manifold.state[0];++i) {
                const auto point=contact.anchorWorld(hit.manifold.points[i],authoredBody==2u);
                EXPECT_NEAR(point.x,1,.0005f);
                EXPECT_NEAR(std::abs(point.y+.25f),.1f,.0005f);
                EXPECT_NEAR(std::abs(point.z-.25f),.1f,.0005f);
                corners|=1u<<((point.y>-.25f?1u:0u)|(point.z>.25f?2u:0u));
            }
            EXPECT_EQ(corners,15u);
            contact.primitive(boxBody,{1.3f,-.25f,.25f},{.2f,.2f,.2f,2});
            hit=contact.run();
            EXPECT_EQ(hit.manifold.state[0],0u);
            EXPECT_EQ(hit.telemetry[16],0u);
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactTiltedBottomEdgesKeepDominantExteriorFaceIdentity) {
    auto store=create(context);const auto handle=upload(context,*store,7,false);
    {
        AuthoredContactFixture contact(context,*store);
        for(const float angle:{-.001f,0.f,.001f}) {
            SCOPED_TRACE(angle);
            AuthoredRootMotion root;root.orientation=glm::angleAxis(angle,glm::vec3(0,0,1));
            contact.authored(1,handle,root);
            // Wide, level floor makes the support normal vertical. The cube's
            // bottom corner also lies on a side face with tiny positive normal
            // alignment; that side must not steal the support feature ID.
            contact.primitive(2,{0,-1.49f,0},{6,1,6,2});
            const auto hit=contact.run();ASSERT_EQ(hit.telemetry[16],0u);
            ASSERT_GE(hit.manifold.state[0],2u);
            EXPECT_GT(hit.manifold.normal[1],.999f);
            for(uint32_t i=0;i<hit.manifold.state[0];++i) {
                const auto feature=hit.manifold.points[i].features[1];
                ASSERT_NE(feature&0x80000000u,0u);
                const auto faceIndex=feature&0x7fffffffu;
                ASSERT_LT(faceIndex,store->get(handle)->faces().size());
                const auto& face=store->get(handle)->faces()[faceIndex];
                const auto world=contact.anchorWorld(hit.manifold.points[i],false);
                const auto local=glm::inverse(root.orientation)*world;
                EXPECT_NEAR(local.y,-1.f,.0001f);
                EXPECT_EQ(face.axis,1u)<<"point "<<i<<" root "<<local.x<<","<<local.y<<","<<local.z;
                EXPECT_EQ(face.sign,-1);
            }
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactReductionRetainsBothEndsOfManyCoplanarExteriorPatches) {
    std::vector<geometry::UnionBox> boxes;
    for(int32_t i=0;i<10;++i) {
        const int32_t x=-450+i*100;
        boxes.push_back({{{x-25,-50,-50},{x+25,0,50}},uint32_t(i+1)});
    }
    boxes.push_back({{{-475,0,-50},{475,50,50}},11});
    geometry::BoxUnionIssue unionIssue;const auto volume=geometry::BoxUnion::compile(boxes,unionIssue);ASSERT_TRUE(volume);
    const RigidMassInput mass{5,{0,0,0},{10,0,0,0,100,0,0,0,100}};
    AuthoredShapeIssue shapeIssue;auto shape=AuthoredShape::prepare(*volume,mass,shapeIssue);ASSERT_TRUE(shape);
    auto store=create(context);ShapeResourceError error;
    const auto handle=store->upload(std::move(*shape),error);ASSERT_TRUE(handle.valid());ASSERT_TRUE(drain(context,*store));
    const geometry::UnionBox ground{{{-750,-25,-250},{750,25,250}},1};
    const auto groundVolume=geometry::BoxUnion::compile(std::span(&ground,1),unionIssue);ASSERT_TRUE(groundVolume);
    auto groundShape=AuthoredShape::prepare(*groundVolume,mass,shapeIssue);ASSERT_TRUE(groundShape);
    const auto groundHandle=store->upload(std::move(*groundShape),error);ASSERT_TRUE(groundHandle.valid());ASSERT_TRUE(drain(context,*store));
    for(const bool authoredGround:{false,true}) {
        SCOPED_TRACE(authoredGround?"authored ground":"primitive ground");
        AuthoredContactFixture contact(context,*store);contact.authored(2,handle);
        if(authoredGround) {
            AuthoredRootMotion root;root.position=worldPositionFromAbsolute({0,-1.45,0});
            contact.authored(1,groundHandle,root);
        } else contact.primitive(1,{0,-1.45f,0},{30,1,10,2});
        const auto hit=contact.run();ASSERT_EQ(hit.manifold.state[0],4u);
        float minX=INFINITY,maxX=-INFINITY,minZ=INFINITY,maxZ=-INFINITY;
        for(const auto& point:hit.manifold.points) {
            const auto p=contact.anchorWorld(point,true);
            minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minZ=std::min(minZ,p.z);maxZ=std::max(maxZ,p.z);
        }
        // Ten separate bottom patches yield forty raw clipped corners. The
        // final four constraints must support both ends, not the first four
        // patches encountered before the sixteen-candidate work buffer fills.
        EXPECT_LT(minX,-8);EXPECT_GT(maxX,8);EXPECT_LT(minZ,-.9f);EXPECT_GT(maxZ,.9f);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,CompoundNormalKeepsCoherentSupportWithinSlopButAllowsDeeperSide) {
    game::adventure::ImportedAssemblySource source;source.assetId="support-normal";source.sourceSha256=std::string(64,'a');
    source.parts={{1,"lower","98283.dat",4,1,{0,0,0},{1,3.774895063202166e-8,0,0}},
                  {2,"upper","3005.dat",4,2,{0,0,0},{1,3.774895063202166e-8,0,0}}};
    std::string error;const auto graph=game::adventure::ImportedAssembly::prepare(source,error);ASSERT_TRUE(graph)<<error;
    auto limits=smallLimits();limits.cpu.cells=512;limits.cpu.faces=4096;limits.cpu.nodes=1024;limits.cpu.bytes=1024*1024;limits.gpuBytes=1024*1024;
    auto store=create(context,limits);std::array<ShapeHandle,2> handles{};ShapeResourceError status;
    for(size_t i=0;i<2;++i){auto shape=graph->roots()[i].shape;handles[i]=store->upload(std::move(shape),status);ASSERT_TRUE(handles[i].valid());}
    ASSERT_TRUE(drain(context,*store));
    {
        AuthoredContactFixture contact(context,*store);
        const auto pose=[&](uint32_t body,glm::dvec3 p,glm::dquat q) {
            contact.authored(body,handles[body-1]);const auto position=worldPositionFromAbsolute(p);
            contact.poses[body*2]=glm::vec4(position.local,contact.poses[body*2].w);
            contact.poses[body*2+1]={float(q.x),float(q.y),float(q.z),float(q.w)};
            contact.metadata[body]=glm::ivec4(position.sector,contact.metadata[body].w);
        };
        // Real adjacent masonry/1x1 poses isolate a tiny stud-side contact
        // competing with the already coherent vertical supporting patch.
        pose(1,{1194.9104843139648,13.649364471435547,-1034.4612321853638},
            {-.010204754769802094,.0002891026088036597,-.9999476671218872,.0006663290550932288});
        pose(2,{1195.4047775268555,14.82012939453125,-1034.468822479248},
            {.5074785351753235,-.49262332916259766,-.4917924106121063,.5078660845756531});
        auto hit=contact.run();ASSERT_GT(hit.manifold.state[0],0u);ASSERT_LT(hit.manifold.normal[1],-.9f);
        pose(1,{1194.9093704223633,13.650254249572754,-1034.461166381836},
            {-.010301144793629646,-.00032086853752844036,-.9999467134475708,.0004865774535574019});
        pose(2,{1195.405174255371,14.821036338806152,-1034.4688501358032},
            {.5074757933616638,-.492708295583725,-.4920057952404022,.5075796842575073});
        hit=contact.run();ASSERT_GT(hit.manifold.state[0],0u);EXPECT_LT(hit.manifold.normal[1],-.9f);
        // A genuinely deeper side collision must override temporal preference.
        contact.poses[4].z-=.03f;hit=contact.run();ASSERT_GT(hit.manifold.state[0],0u);
        EXPECT_LT(hit.manifold.normal[2],-.9f);EXPECT_GT(hit.manifold.normal[1],-.2f);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,CompoundKeepsSimultaneousSupportAndSidePatches) {
    game::adventure::ImportedAssemblySource source;source.assetId="support-normal";source.sourceSha256=std::string(64,'a');
    source.parts={{1,"lower","98283.dat",4,1,{0,0,0},{1,3.774895063202166e-8,0,0}},
                  {2,"upper","3005.dat",4,2,{0,0,0},{1,3.774895063202166e-8,0,0}}};
    std::string error;const auto graph=game::adventure::ImportedAssembly::prepare(source,error);ASSERT_TRUE(graph)<<error;
    auto limits=smallLimits();limits.cpu.cells=512;limits.cpu.faces=4096;limits.cpu.nodes=1024;limits.cpu.bytes=1024*1024;limits.gpuBytes=1024*1024;
    auto store=create(context,limits);std::array<ShapeHandle,2> handles{};ShapeResourceError status;
    for(size_t i=0;i<2;++i){auto shape=graph->roots()[i].shape;handles[i]=store->upload(std::move(shape),status);ASSERT_TRUE(handles[i].valid());}
    ASSERT_TRUE(drain(context,*store));
    {
        AuthoredContactFixture contact(context,*store,8);
        const auto pose=[&](uint32_t body,glm::dvec3 p,glm::dquat q) {
            contact.authored(body,handles[body-1]);const auto position=worldPositionFromAbsolute(p);
            contact.poses[body*2]=glm::vec4(position.local,contact.poses[body*2].w);
            contact.poses[body*2+1]={float(q.x),float(q.y),float(q.z),float(q.w)};
            contact.metadata[body]=glm::ivec4(position.sector,contact.metadata[body].w);
        };
        // Real adjacent masonry/1x1 poses isolate a tiny stud-side contact
        // competing with the already coherent vertical supporting patch.
        pose(1,{1194.9104843139648,13.649364471435547,-1034.4612321853638},
            {-.010204754769802094,.0002891026088036597,-.9999476671218872,.0006663290550932288});
        pose(2,{1195.4047775268555,14.82012939453125,-1034.468822479248},
            {.5074785351753235,-.49262332916259766,-.4917924106121063,.5078660845756531});
        auto hit=contact.run();
        std::ostringstream diagnostics;for(size_t i=0;i<32;++i)diagnostics<<i<<'='<<hit.telemetry[i]<<' ';
        for(const auto& m:hit.patches)diagnostics<<" patch "<<m.state[0]<<" normal="<<m.normal[0]<<','<<m.normal[1]<<','<<m.normal[2];
        ASSERT_EQ(hit.telemetry[25],0u)<<diagnostics.str();ASSERT_GT(hit.telemetry[24],0u)<<diagnostics.str();
        pose(1,{1194.9093704223633,13.650254249572754,-1034.461166381836},
            {-.010301144793629646,-.00032086853752844036,-.9999467134475708,.0004865774535574019});
        pose(2,{1195.405174255371,14.821036338806152,-1034.4688501358032},
            {.5074757933616638,-.492708295583725,-.4920057952404022,.5075796842575073});
        hit=contact.run();ASSERT_EQ(hit.telemetry[25],0u);ASSERT_EQ(hit.telemetry[27],0u);
        const auto hasNormal=[&](const auto& snapshot,size_t axis,float limit) {
            return std::any_of(snapshot.patches.begin(),snapshot.patches.end(),[&](const auto& patch){return patch.state[0]!=0u&&patch.normal[axis]<limit;});
        };
        EXPECT_TRUE(hasNormal(hit,1,-.9f));EXPECT_TRUE(hasNormal(hit,2,-.9f));
        EXPECT_GE(hit.telemetry[24],2u);
        // A genuinely deeper side collision must override temporal preference.
        contact.poses[4].z-=.03f;hit=contact.run();ASSERT_EQ(hit.telemetry[27],0u);
        EXPECT_TRUE(hasNormal(hit,1,-.9f));EXPECT_TRUE(hasNormal(hit,2,-.9f));
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,CompoundCornerPreservesThreeNormalsAndLatchesPatchOverflow) {
    auto store=create(context);
    const auto uploadBoxes=[&](std::span<const geometry::UnionBox> boxes) {
        geometry::BoxUnionIssue unionIssue;const auto volume=geometry::BoxUnion::compile(boxes,unionIssue);
        if(!volume)throw std::runtime_error("corner union");
        const RigidMassInput mass{1,{0,0,0},{1,0,0,0,1,0,0,0,1}};
        AuthoredShapeIssue issue;auto shape=AuthoredShape::prepare(*volume,mass,issue);
        if(!shape)throw std::runtime_error("corner shape");
        ShapeResourceError error;const auto handle=store->upload(std::move(*shape),error);
        if(!handle.valid()||!drain(context,*store))throw std::runtime_error("corner upload");
        return handle;
    };
    const std::array<geometry::UnionBox,3> walls{{
        {{{-100,-100,-100},{100,-50,100}},1},
        {{{-100,-50,-100},{-50,100,100}},2},
        {{{-50,-50,-100},{100,100,-50}},3}}};
    const geometry::UnionBox box{{{-25,-25,-25},{25,25,25}},1};
    const auto corner=uploadBoxes(walls),cube=uploadBoxes(std::span(&box,1));
    for(const uint32_t slots:{8u,4u,2u}) {
        SCOPED_TRACE(slots);AuthoredContactFixture contact(context,*store,slots);
        contact.authored(1,corner);AuthoredRootMotion root;
        root.position=worldPositionFromAbsolute({-.51,-.51,-.51});contact.authored(2,cube,root);
        auto hit=contact.run();
        if(slots>=4) {
            ASSERT_EQ(hit.telemetry[27],0u);EXPECT_EQ(hit.telemetry[24],3u);
            for(size_t axis=0;axis<3;++axis) {
                EXPECT_TRUE(std::any_of(hit.patches.begin(),hit.patches.end(),[&](const auto& m){
                    return m.state[0]>0u&&m.normal[axis]<-.99f;
                }))<<"missing corner normal "<<axis;
            }
            // History slot order is not identity. Distinct impulses must follow
            // their matching normal/anchors after the previous slots rotate.
            auto history=hit.patches;
            for(auto& manifold:history) {
                const float tag=1.f-manifold.normal[0]-2.f*manifold.normal[1]-3.f*manifold.normal[2];
                for(uint32_t point=0;point<manifold.state[0];++point)
                    manifold.points[point].localAnchorBNormalImpulse[3]=tag;
            }
            std::rotate(history.begin(),history.begin()+1,history.begin()+slots);
            ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),contact.narrow.manifolds(),0,
                std::span<const GpuContactManifold>(history).first(slots)));
            hit=contact.run();ASSERT_EQ(hit.telemetry[24],3u);ASSERT_EQ(hit.telemetry[27],0u);
            for(const auto& manifold:hit.patches) {
                const float expected=1.f-manifold.normal[0]-2.f*manifold.normal[1]-3.f*manifold.normal[2];
                for(uint32_t point=0;point<manifold.state[0];++point)
                    EXPECT_NEAR(manifold.points[point].localAnchorBNormalImpulse[3],expected,.0001f);
            }
            if(slots==8) {
                for(auto& manifold:hit.patches)for(auto& point:manifold.points)
                    point.localAnchorBNormalImpulse[3]=0;
                ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),contact.narrow.manifolds(),0,
                    std::span<const GpuContactManifold>(hit.patches)));
                contact.poses[2].w=0;
                contact.shapes[1].inverseInertiaMaterial={0,0,0,0};
                contact.motions[4]={-1,-1,-1,0};
                GpuDynamicSolver solver;GpuDynamicSolver::Config config;
                config.bodyCapacity=3;config.contactCapacity=8;config.workgroupSize=64;
                config.gravity={0,0,0};config.friction=0;config.rollingResistance=0;
                config.linearDamping=0;config.angularDamping=0;
                config.sphereRestitution=0;config.otherRestitution=0;
                ASSERT_TRUE(solver.initialize(context.getDevice(),context.getQueue(),config));
                hit=contact.run(true,true,&solver);ASSERT_EQ(hit.telemetry[24],3u);
                for(size_t slot=0;slot<hit.patches.size();++slot) {
                    const auto& manifold=hit.patches[slot];if(manifold.state[0]==0)continue;
                    EXPECT_EQ(manifold.pair.ordinal,slot);
                    float impulse=0;for(uint32_t point=0;point<manifold.state[0];++point)
                        impulse+=manifold.points[point].localAnchorBNormalImpulse[3];
                    EXPECT_GT(impulse,0.f)<<"solver result missing from raw patch "<<slot;
                }
                for(glm::length_t axis=0;axis<3;++axis)EXPECT_GT(hit.motions[4][axis],-.5f);
                solver.shutdown();
            }
        } else {
            EXPECT_EQ(hit.telemetry[25],1u);EXPECT_NE(hit.telemetry[27],0u);EXPECT_EQ(hit.telemetry[24],0u);
            root.position=worldPositionFromAbsolute({5,5,5});contact.authored(2,cube,root);
            hit=contact.run();EXPECT_EQ(hit.telemetry[25],0u);
            EXPECT_NE(hit.telemetry[27],0u);EXPECT_EQ(hit.telemetry[24],0u);
        }
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,RelocatedContactDoesNotInheritUnmatchedFrictionOrTorque) {
    auto store=create(context);const auto handle=upload(context,*store,7,false);
    {
        AuthoredContactFixture contact(context,*store,8);contact.authored(1,handle);
        contact.primitive(2,{0,-1.49f,0},{6,1,6,2});
        auto hit=contact.run();ASSERT_GT(hit.manifold.state[0],0u);
        auto history=hit.manifold;
        for(uint32_t point=0;point<history.state[0];++point)
            history.points[point].localAnchorBNormalImpulse[3]=2;
        history.tangent1[3]=3;history.tangent2[3]=4;
        history.frictionAnchorA[3]=5;history.rollingImpulse={6,7,8,0};
        ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),contact.narrow.manifolds(),0,
            std::span<const GpuContactManifold>(&history,1)));
        // Same horizontal face and normal, but every floor anchor moves much
        // farther than the existing recycling distance. No constraint survives.
        contact.poses[2].x+=.5f;hit=contact.run();ASSERT_GT(hit.manifold.state[0],0u);
        EXPECT_EQ(hit.telemetry[14],0u);EXPECT_EQ(hit.telemetry[15],0u);
        for(uint32_t point=0;point<hit.manifold.state[0];++point)
            EXPECT_EQ(hit.manifold.points[point].localAnchorBNormalImpulse[3],0.f);
        EXPECT_EQ(hit.manifold.tangent1[3],0.f);EXPECT_EQ(hit.manifold.tangent2[3],0.f);
        EXPECT_EQ(hit.manifold.frictionAnchorA[3],0.f);
        EXPECT_EQ(hit.manifold.rollingImpulse,(std::array<float,4>{0,0,0,0}));
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,OptionalActualWallPairContactReplay) {
    const char* tracePath=std::getenv("VOXY_IMPORTED_PAIR_REPLAY");
    const char* workspace=std::getenv("VOXY_ADVENTURE_TEST_WORKSPACE");
    if(!tracePath||!workspace)GTEST_SKIP()<<"Opt-in native recorded-pose diagnostic, not a default acceptance gate.";
    std::string error;
    const auto source=game::adventure::loadImportedSection(std::filesystem::path(workspace)/"data/adventure/ldraw-blacksmith-parts-r01/wall.json",error);
    ASSERT_TRUE(source)<<error;
    const auto graph=game::adventure::ImportedAssembly::prepare(*source,error);ASSERT_TRUE(graph)<<error;
    std::vector<uint64_t> bonds;for(const auto& bond:source->bonds)if(bond.active)bonds.push_back(bond.id);
    const auto cut=graph->prepareCut(source->revision,bonds,source->anchors,error);ASSERT_TRUE(cut)<<error;
    constexpr std::array<uint64_t,2> ids{74942832682433725ull,139456561832398802ull};
    auto limits=smallLimits();limits.cpu.cells=512;limits.cpu.faces=4096;limits.cpu.nodes=1024;limits.cpu.bytes=1024*1024;limits.gpuBytes=1024*1024;
    auto store=create(context,limits);std::array<ShapeHandle,2> handles{};ShapeResourceError status;
    for(size_t i=0;i<ids.size();++i) {
        const auto root=cut->rootForPart(ids[i]);ASSERT_TRUE(root);
        auto shape=cut->roots()[*root].shape;handles[i]=store->upload(std::move(shape),status);ASSERT_TRUE(handles[i].valid());
    }
    ASSERT_TRUE(drain(context,*store));
    {
        AuthoredContactFixture contact(context,*store);std::ifstream input(tracePath);ASSERT_TRUE(input.good());
        std::ostringstream trace;trace<<"tick,count,nx,ny,nz,anchorAx,anchorAy,anchorAz,separation,featureA,featureB\n";
        uint64_t tick=0;size_t samples=0,contacts=0,sides=0;float minNormalY=1,maxNormalY=-1;
        while(input>>tick) {
            ASSERT_LT(samples,120u);
            for(size_t i=0;i<2;++i) {
                glm::dvec3 absolute;glm::dquat orientation;
                ASSERT_TRUE(bool(input>>absolute.x>>absolute.y>>absolute.z>>orientation.w>>orientation.x>>orientation.y>>orientation.z));
                const uint32_t body=uint32_t(i+1);contact.authored(body,handles[i]);
                const auto position=worldPositionFromAbsolute(absolute);
                contact.poses[body*2]=glm::vec4(position.local,contact.poses[body*2].w);
                contact.poses[body*2+1]=glm::vec4(float(orientation.x),float(orientation.y),float(orientation.z),float(orientation.w));
                contact.metadata[body]=glm::ivec4(position.sector,contact.metadata[body].w);
            }
            const auto hit=contact.run();++samples;EXPECT_EQ(hit.telemetry[16],0u);
            const uint32_t count=hit.manifold.state[0];
            if(count) {
                ++contacts;const float ny=hit.manifold.normal[1];minNormalY=std::min(minNormalY,ny);maxNormalY=std::max(maxNormalY,ny);
                if(ny>-.7f)++sides;
            }
            for(uint32_t i=0;i<std::max(count,1u);++i) {
                const auto& point=hit.manifold.points[i];const auto p=contact.anchorWorld(point,true);
                trace<<tick<<','<<count<<','<<hit.manifold.normal[0]<<','<<hit.manifold.normal[1]<<','<<hit.manifold.normal[2]
                    <<','<<p.x<<','<<p.y<<','<<p.z<<','<<point.localAnchorASeparation[3]<<','<<point.features[0]<<','<<point.features[1]<<'\n';
            }
        }
        EXPECT_EQ(samples,120u);EXPECT_GT(contacts,0u);
        RecordProperty("contactReplay",trace.str());RecordProperty("contactSamples",int(contacts));RecordProperty("nonSupportNormals",int(sides));
        RecordProperty("minimumNormalY",minNormalY);RecordProperty("maximumNormalY",maxNormalY);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactAuthoredPatchWarmStartFollowsAnchorsRatherThanCornerOrder) {
    auto store=create(context);const auto handle=upload(context,*store,7,false);
    {
        AuthoredContactFixture contact(context,*store);
        AuthoredRootMotion left,right;right.position=worldPositionFromAbsolute({1.95,0,0});
        contact.authored(1,handle,left);contact.authored(2,handle,right);
        const auto initial=contact.run();ASSERT_EQ(initial.manifold.state[0],4u);
        auto seeded=initial.manifold;
        for(uint32_t i=0;i<4;++i) {
            EXPECT_EQ(seeded.points[i].features[0],seeded.points[0].features[0]);
            EXPECT_EQ(seeded.points[i].features[1],seeded.points[0].features[1]);
            seeded.points[i].localAnchorBNormalImpulse[3]=float(i+1);
        }
        const auto original=seeded;
        std::reverse(seeded.points.begin(),seeded.points.end());
        ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),contact.narrow.manifolds(),0,
            std::span<const GpuContactManifold>(&seeded,1)));
        const auto reordered=contact.run();ASSERT_EQ(reordered.manifold.state[0],4u);
        for(uint32_t i=0;i<4;++i) {
            const auto& p=reordered.manifold.points[i];size_t closest=0;double distance=INFINITY;
            for(size_t j=0;j<4;++j) {
                double next=0;
                for(size_t axis=0;axis<3;++axis) {
                    const double a=double(p.localAnchorASeparation[axis])-double(original.points[j].localAnchorASeparation[axis]);
                    const double b=double(p.localAnchorBNormalImpulse[axis])-double(original.points[j].localAnchorBNormalImpulse[axis]);
                    next+=a*a+b*b;
                }
                if(next<distance){distance=next;closest=j;}
            }
            EXPECT_LT(distance,1e-9);
            EXPECT_FLOAT_EQ(p.localAnchorBNormalImpulse[3],original.points[closest].localAnchorBNormalImpulse[3]);
        }
        // Face IDs denote a patch, not an immortal point. Newly appearing
        // corners must not inherit an impulse from an old distant corner.
        seeded=reordered.manifold;
        for(auto& p:seeded.points){p.localAnchorASeparation[1]+=3;p.localAnchorBNormalImpulse[1]+=3;p.localAnchorBNormalImpulse[3]=17;}
        ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),contact.narrow.manifolds(),0,
            std::span<const GpuContactManifold>(&seeded,1)));
        const auto changed=contact.run();ASSERT_EQ(changed.manifold.state[0],4u);
        for(const auto& p:changed.manifold.points)EXPECT_FLOAT_EQ(p.localAnchorBNormalImpulse[3],0);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactCompoundPairRestoresPrincipalAnchorsAcrossSectors) {
    auto store=create(context);
    const auto first=upload(context,*store),second=upload(context,*store,21,false);
    {
        AuthoredContactFixture contact(context,*store);
        AuthoredRootMotion a,b; a.position={{7,-3,12},{127,20,-90}}; a.orientation={std::sqrt(.5f),0,std::sqrt(.5f),0};
        b=a; b.position.local.z-=2.55f; b.position.local.x+=.5f;
        contact.authored(1,first,a); contact.authored(2,second,b);
        auto hit=contact.run();
        EXPECT_EQ(hit.telemetry[10],1u); EXPECT_EQ(hit.telemetry[16],0u);
        contactHit(hit,-.05f,{0,0,1});
        for(uint32_t i=0;i<hit.manifold.state[0];++i) {
            const auto point=contact.anchorWorld(hit.manifold.points[i],false);
            EXPECT_NEAR(point.z,-91.6f,.0005f);
            const auto face=hit.manifold.points[i].features[1]&0x7fffffffu;
            ASSERT_LT(face,store->get(first)->faces().size()); EXPECT_EQ(store->get(first)->faces()[face].source,11u);
        }
        // The offset COM crosses a sector boundary. Exchange body order without
        // moving either root, exercising both manifold frame directions.
        EXPECT_NE(contact.metadata[1].x,contact.metadata[2].x);
        contact.authored(2,first,a); contact.authored(1,second,b);
        hit=contact.run(); contactHit(hit,-.05f,{0,0,-1});
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactMissingAtlasOrRetiredShapeReportsInvalidManifold) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredContactFixture contact(context,*store); contact.authored(1,handle);
        contact.primitive(2,{1.05f,0,0},{.2f,.2f,.2f,2});
        auto hit=contact.run(false); EXPECT_EQ(hit.manifold.state[0],0u); EXPECT_EQ(hit.telemetry[16],1u);
        contact.shapes[1].authoredShape.z=2;
        hit=contact.run(); EXPECT_EQ(hit.manifold.state[0],0u); EXPECT_EQ(hit.telemetry[16],1u);
        contact.shapes[1].authoredShape.z=1;
        ASSERT_EQ(store->retire(handle),ShapeResourceError::None); ASSERT_TRUE(drain(context,*store));
        hit=contact.run(true,false); EXPECT_EQ(hit.manifold.state[0],0u); EXPECT_EQ(hit.telemetry[16],1u);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes,ContactImpactMovesAndTurnsWholeCompoundWithConservedMomentum) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredContactFixture contact(context,*store); contact.authored(1,handle);
        contact.primitive(2,{1.7f,.25f,.25f},{.2f,.2f,.2f,0});
        contact.motions[4]={-3,0,0,0};
        GpuDynamicSolver solver; GpuDynamicSolver::Config config;
        config.bodyCapacity=3; config.contactCapacity=1; config.workgroupSize=64;
        config.gravity={0,0,0}; config.friction=0; config.rollingResistance=0;
        config.linearDamping=0; config.angularDamping=0;
        config.sphereRestitution=0; config.otherRestitution=0;
        ASSERT_TRUE(solver.initialize(context.getDevice(),context.getQueue(),config));
        const auto result=contact.run(true,true,&solver);
        ASSERT_GT(result.manifold.state[0],0u);
        EXPECT_EQ(result.telemetry[16],0u);
        const auto hullVelocity=glm::vec3(result.motions[2]);
        const auto hullOmega=glm::vec3(result.motions[3]);
        const auto projectileVelocity=glm::vec3(result.motions[4]);
        EXPECT_LT(hullVelocity.x,-.05f); EXPECT_GT(glm::length(hullOmega),.05f);
        EXPECT_GT(projectileVelocity.x,-2.95f);
        const auto momentum=5.0f*hullVelocity+projectileVelocity;
        EXPECT_NEAR(momentum.x,-3,.001f); EXPECT_NEAR(momentum.y,0,.001f); EXPECT_NEAR(momentum.z,0,.001f);
        EXPECT_LT(result.poses[2].x,contact.poses[2].x);
        const auto bodyQ=result.poses[3];
        const auto bodyOmega=glm::inverse(glm::quat(bodyQ.w,bodyQ.x,bodyQ.y,bodyQ.z))*hullOmega;
        const auto& inverse=store->get(handle)->packedMass().inverseInertiaRadius;
        const auto angularEnergy=.5f*(bodyOmega.x*bodyOmega.x/inverse[0]
            +bodyOmega.y*bodyOmega.y/inverse[1]+bodyOmega.z*bodyOmega.z/inverse[2]);
        const auto energy=2.5f*glm::dot(hullVelocity,hullVelocity)
            +.5f*glm::dot(projectileVelocity,projectileVelocity)+angularEnergy;
        EXPECT_LE(energy,4.501f); solver.shutdown();
    }
    close(context,*store);
}

struct AuthoredQueryFixture {
    Context& context;
    IAuthoredShapeResources& store;
    ShapeHandle handle;
    GpuAsyncQuerySystem queries;
    std::array<WGPUBuffer,3> buffers{};
    std::array<glm::vec4,4> poses{};
    std::array<GpuBodyShape,2> shapes{};
    std::array<glm::ivec4,2> metadata{};
    AuthoredQueryFixture(Context& c,IAuthoredShapeResources& s,ShapeHandle h,
        AuthoredRootMotion root={}) : context(c),store(s),handle(h) {
        const auto* shape=store.get(handle);
        if(!shape) throw std::runtime_error("query fixture missing shape");
        AuthoredFrameError error;
        const auto body=AuthoredBodyFrame(*shape).bodyMotion(root,error);
        if(!body) throw std::runtime_error("query fixture frame");
        poses[2]=glm::vec4(body->centerPosition.local,shape->packedMass().centerInverseMass[3]);
        poses[3]={body->orientation.x,body->orientation.y,body->orientation.z,body->orientation.w};
        metadata[1]=glm::ivec4(body->centerPosition.sector,1|(1<<20)|(1<<21));
        shapes[1].dimensionsType={2,2,2,1}; // Deliberately cannot represent the appendage/notch.
        shapes[1].authoredShape={handle.index,handle.generation,1,0};
        GpuAsyncQuerySystem::Config config;
        config.bodyCapacity=2; config.requestCapacity=8; config.readbackSlots=2;
        if(!queries.initialize(context.getDevice(),context.getQueue(),config))
            throw std::runtime_error("authored query pipeline");
        const auto uploadBuffer=[&](auto& values,const char* label) {
            using T=typename std::remove_reference_t<decltype(values)>::value_type;
            return gpu::createBufferWithData(context.getDevice(),context.getQueue(),
                gpu::BufferDesc::storage(sizeof(values),true,label),std::span<const T>(values));
        };
        buffers[0]=uploadBuffer(poses,"authored_query_poses");
        buffers[1]=uploadBuffer(shapes,"authored_query_shapes");
        buffers[2]=uploadBuffer(metadata,"authored_query_metadata");
        if(!buffers[0] || !buffers[1] || !buffers[2]) throw std::runtime_error("query input allocation");
    }
    ~AuthoredQueryFixture() {
        queries.shutdown();
        for(auto buffer:buffers) if(buffer) wgpuBufferRelease(buffer);
    }
    std::vector<GpuQueryOutput> run(std::span<const GpuQueryRequest> requests,bool atlas=true,bool declare=true) {
        if(!gpu::writeBuffer(context.getQueue(),buffers[1],0,std::span<const GpuBodyShape>(shapes)))
            throw std::runtime_error("query shape write");
        queries.setBodyView({buffers[0],buffers[1],buffers[2],2,atlas?store.buffer():nullptr});
        if(!queries.submit(requests,1)) throw std::runtime_error("query batch admission");
        const std::array handles{handle}; ShapeResourceError error;
        const auto ticket=store.prepareSubmission(declare?std::span<const ShapeHandle>(handles):std::span<const ShapeHandle>{},error);
        if(!ticket.valid()) throw std::runtime_error("query shape use declaration");
        WGPUCommandEncoderDescriptor ed{};
        const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
        if(!encoder || !queries.encode(encoder)) {
            if(encoder) wgpuCommandEncoderRelease(encoder);
            (void)store.discard(ticket);
            throw std::runtime_error("query encode");
        }
        WGPUCommandBufferDescriptor cd{};
        const auto command=wgpuCommandEncoderFinish(encoder,&cd);
        wgpuCommandEncoderRelease(encoder);
        const std::array commands{command};
        const auto status=store.submit(ticket,commands);
        if(command) wgpuCommandBufferRelease(command);
        if(status!=ShapeResourceError::None) throw std::runtime_error("query submit");
        std::optional<GpuQueryBatchResult> result;
        if(!wait(context,[&]{store.poll();result=queries.poll();return result.has_value();}) || !drain(context,store))
            throw std::runtime_error("query completion: "+std::string(store.failure()));
        return std::move(result->outputs);
    }
};

GpuQueryRequest authoredRequest(GpuQueryType type,glm::vec3 origin,glm::vec3 direction={-1,0,0},float radius=0) {
    GpuQueryRequest request;
    request.ids={17u,static_cast<uint32_t>(type),8u,0u};
    request.originRadius={origin.x,origin.y,origin.z,radius};
    request.directionDistance={direction.x,direction.y,direction.z,10};
    request.dimensions={0,1,0,1};
    request.sector={0,0,0,4096};
    return request;
}
void queryHit(const GpuQueryOutput& output,float distance,glm::vec3 normal,float tolerance=.0002f) {
    ASSERT_EQ(output.header[2],0u); ASSERT_EQ(output.header[1],1u);
    EXPECT_NEAR(output.hits[0].metricDistance[1],distance,tolerance);
    EXPECT_NE(output.hits[0].ids[2]&0x80000000u,0u);
    for(size_t i=0;i<3;++i) EXPECT_NEAR(output.hits[0].normal[i],normal[static_cast<glm::length_t>(i)],tolerance);
}

TEST_F(GpuAuthoredShapes, QueryRaysResolveExteriorAppendageNotchAndInsideOrigin) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredQueryFixture query(context,*store,handle);
        const std::array requests{
            authoredRequest(GpuQueryType::RayCast,{3,.25f,.25f}),
            authoredRequest(GpuQueryType::RayCast,{3,-.5f,.25f}),
            authoredRequest(GpuQueryType::RayCast,{1.3f,-.5f,-3},{0,0,1}),
            authoredRequest(GpuQueryType::RayCast,{1.2f,.25f,.25f})};
        const auto result=query.run(requests); ASSERT_EQ(result.size(),4u);
        queryHit(result[0],1.4f,{1,0,0}); queryHit(result[1],2,{1,0,0});
        EXPECT_EQ(result[2].header[1],0u); EXPECT_EQ(result[2].header[2],0u);
        ASSERT_EQ(result[3].header[1],1u); EXPECT_NEAR(result[3].hits[0].metricDistance[1],0,.00001f);
        const auto faces=store->get(handle)->faces();
        EXPECT_EQ(faces[result[0].hits[0].ids[2]&0x7fffffffu].source,11u);
        EXPECT_EQ(faces[result[1].hits[0].ids[2]&0x7fffffffu].source,7u);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, QueryOverlapUsesUnionBoundaryInsteadOfInternalMatingFace) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredQueryFixture query(context,*store,handle);
        const std::array requests{
            authoredRequest(GpuQueryType::OverlapSphere,{1.1f,.25f,.25f},{},.02f),
            authoredRequest(GpuQueryType::OverlapSphere,{1.7f,.6f,.25f},{},.1f),
            authoredRequest(GpuQueryType::OverlapSphere,{1.7f,.6f,.25f},{},.15f)};
        const auto result=query.run(requests); ASSERT_EQ(result.size(),3u);
        ASSERT_EQ(result[0].header[1],1u); EXPECT_EQ(result[0].header[2],0u);
        EXPECT_NEAR(result[0].hits[0].metricDistance[1],.27f,.0002f);
        EXPECT_EQ(result[1].header[1],0u); EXPECT_EQ(result[1].header[2],0u);
        queryHit(result[2],.15f-std::sqrt(.02f),{std::sqrt(.5f),std::sqrt(.5f),0});
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, QuerySphereSweepKeepsConcaveGapOpen) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredQueryFixture query(context,*store,handle);
        const std::array requests{
            authoredRequest(GpuQueryType::SphereCast,{3,.25f,.25f},{-1,0,0},.2f),
            authoredRequest(GpuQueryType::SphereCast,{1.3f,-.5f,-3},{0,0,1},.1f)};
        const auto result=query.run(requests);
        queryHit(result[0],1.2f,{1,0,0});
        EXPECT_EQ(result[1].header[1],0u); EXPECT_EQ(result[1].header[2],0u);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, QueryCapsuleFindsThinRailBetweenSegmentSamples) {
    const std::array<geometry::UnionBox,1> boxes{{{{{-5,11,-5},{5,12,5}},41}}};
    geometry::BoxUnionIssue issue; auto volume=geometry::BoxUnion::compile(boxes,issue); ASSERT_TRUE(volume);
    AuthoredShapeIssue shapeIssue;
    auto shape=AuthoredShape::prepare(*volume,{5,{.25,-.5,.75},{3,0,0,0,3,0,0,0,3}},shapeIssue); ASSERT_TRUE(shape);
    auto store=create(context); ShapeResourceError error;
    const auto handle=store->upload(std::move(*shape),error); ASSERT_TRUE(handle.valid()); ASSERT_TRUE(drain(context,*store));
    {
        AuthoredQueryFixture query(context,*store,handle);
        auto request=authoredRequest(GpuQueryType::CapsuleCast,{2,0,0},{-1,0,0},.001f);
        request.dimensions[3]=20;
        const std::array requests{request}; const auto result=query.run(requests);
        queryHit(result[0],1.899f,{1,0,0},.001f);
        EXPECT_GE(result[0].hits[0].point[1],.219f); EXPECT_LE(result[0].hits[0].point[1],.241f);
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, QueryRotatedPrincipalBodyCrossesQuerySectorWithoutMovingRoot) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredRootMotion root;
        root.position={{7,-3,12},{120,20,-90}};
        root.orientation=glm::quat(std::sqrt(.5f),0,std::sqrt(.5f),0);
        AuthoredQueryFixture query(context,*store,handle,root);
        auto request=authoredRequest(GpuQueryType::RayCast,{-135.75f,20.25f,-93},{0,0,1});
        request.sector={8,-3,12,4096};
        const std::array requests{request}; const auto result=query.run(requests);
        queryHit(result[0],1.4f,{0,0,-1});
        EXPECT_NEAR(result[0].hits[0].point[2],-91.6f,.0002f);
        EXPECT_EQ(result[0].hits[0].sector,(std::array<int32_t,4>{8,-3,12,1}));
    }
    close(context,*store);
}

TEST_F(GpuAuthoredShapes, QueryMissingAtlasAndRetiredReferenceReportIncompleteInsteadOfPrimitiveFallback) {
    auto store=create(context); const auto handle=upload(context,*store);
    {
        AuthoredQueryFixture query(context,*store,handle);
        const std::array requests{authoredRequest(GpuQueryType::RayCast,{3,.25f,.25f})};
        auto result=query.run(requests,false);
        EXPECT_EQ(result[0].header[1],0u); EXPECT_EQ(result[0].header[2],2u);
        query.shapes[1].authoredShape.z=2;
        result=query.run(requests); EXPECT_EQ(result[0].header[1],0u); EXPECT_EQ(result[0].header[2],2u);
        query.shapes[1].authoredShape.z=1;
        ASSERT_EQ(store->retire(handle),ShapeResourceError::None); ASSERT_TRUE(drain(context,*store));
        result=query.run(requests,true,false);
        EXPECT_EQ(result[0].header[1],0u); EXPECT_EQ(result[0].header[2],2u);
    }
    close(context,*store);
}

PhysicsInitContext backendContext(Context& context) {
    PhysicsInitContext value;
    value.requestedBackend=BackendType::WebGpuSoft;
    value.device=context.getDevice(); value.queue=context.getQueue();
    value.maxBodies=32; value.maxActiveBodies=32; value.maxPairs=64;
    value.maxContacts=32; value.maxManifolds=64;
    value.gpu.gravity={0,0,0}; value.gpu.linearDamping=0; value.gpu.angularDamping=0;
    value.gpu.commandCapacity=128; value.gpu.attachmentCapacity=8;
    value.gpu.attachmentCommandCapacity=16; value.gpu.asyncQueryCapacity=4;
    value.gpu.debugReadbackBodyCapacity=4;
    return value;
}

// The mandatory proof must reject contact loss even with all diagnostic
// telemetry disabled, and must retain that failure across a queued batch.
TEST_F(GpuAuthoredShapes, ContactOverflowCannotCertifyOwnedTicksWithoutTelemetry) {
    GpuPhysicsBackend backend;auto config=backendContext(context);
    config.maxContacts=1;config.gpu.authoredContactPatches=4;
    config.gpu.enableTelemetryReadback=false;config.gpu.maximumCatchUpTicks=2;
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources,7,false);
    // Two separate touching pairs require two live contacts, exceeding the
    // deliberate one-contact budget without exceeding body/pair/shape storage.
    for(int i=0;i<3;++i) {
        AuthoredBodySpawnDesc desc;desc.shape=shape;
        desc.motion.position.local={float(i)*1.5f,10,0};
        ASSERT_TRUE(backend.spawnAuthoredBody(desc));
    }
    ASSERT_TRUE(backend.scheduleFixedTicks(2));
    ShapeResourceError error;const auto ticket=backend.prepareGpuSubmission(error);
    ASSERT_TRUE(ticket.valid());
    WGPUCommandEncoderDescriptor ed{};const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
    ASSERT_NE(encoder,nullptr);ASSERT_TRUE(backend.encodeGpuStepChecked(encoder).succeeded());
    WGPUCommandBufferDescriptor cd{};const auto command=wgpuCommandEncoderFinish(encoder,&cd);
    wgpuCommandEncoderRelease(encoder);ASSERT_NE(command,nullptr);
    ASSERT_EQ(backend.submitGpuSubmission(ticket,std::span{&command,1}),ShapeResourceError::None);
    wgpuCommandBufferRelease(command);
    ASSERT_TRUE(wait(context,[&]{backend.stepCpu(0);return backend.tickFrontier().failed;}));
    const auto frontier=backend.tickFrontier();
    EXPECT_TRUE(frontier.supported);EXPECT_EQ(frontier.submitted,2u);
    EXPECT_EQ(frontier.completed,0u);EXPECT_FALSE(backend.isInitialized());
    EXPECT_FALSE(backend.scheduleFixedTicks(1));
    EXPECT_FALSE(backend.pollDebugSnapshot().has_value());
    // A failed world is abandoned, never treated as a successful certified
    // release/rebuild. Its normal owner teardown releases remaining handles.
    backend.shutdown();
}

TEST_F(GpuAuthoredShapes, BackendOwnsHeapAndSubmitsItsReadWithLegacyPhysics) {
    GpuPhysicsBackend backend;
    EXPECT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::NotInitialized);
    ASSERT_TRUE(backend.initialize(backendContext(context)));
    const auto persistent=backend.stats().estimatedPersistentBytes;
    EXPECT_EQ(backend.authoredShapeResources(),nullptr);
    auto invalid=smallLimits(); invalid.cpu.slots=0;
    EXPECT_EQ(backend.enableAuthoredShapeResources(invalid),ShapeResourceError::InvalidLimits);
    EXPECT_EQ(backend.authoredShapeResources(),nullptr);
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto* resources=backend.authoredShapeResources(); ASSERT_NE(resources,nullptr);
    EXPECT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::AlreadyConfigured);
    ASSERT_TRUE(wait(context,[&]{backend.stepCpu(0);return resources->stats().phase==ShapeResourcePhase::Ready;}));
    EXPECT_EQ(backend.encodedTick(),0u);
    EXPECT_EQ(backend.stats().estimatedPersistentBytes,persistent+resources->stats().gpuBytes);
    const auto handle=upload(context,*resources);

    BodySpawnDesc primitive;
    primitive.shape=ThrowableShape::Cube; primitive.position={2,3,4};
    primitive.dimensions={1,1,1}; primitive.linearVelocity={1,0,0};
    const auto body=backend.spawnBody(primitive); ASSERT_TRUE(body.valid());
    ASSERT_TRUE(backend.scheduleFixedTicks(1)); backend.requestDebugSnapshot({body.index,1});
    const std::array handles{handle}; ShapeResourceError error;
    const auto ticket=resources->prepareSubmission(handles,error); ASSERT_TRUE(ticket.valid());
    Probe probe; ASSERT_TRUE(probe.initialize(context,*resources)); ASSERT_TRUE(probe.encode(context,handle));
    WGPUCommandEncoderDescriptor ed{};
    const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed); ASSERT_NE(encoder,nullptr);
    const auto encoded=backend.encodeGpuStepChecked(encoder);
    WGPUCommandBufferDescriptor cd{}; const auto command=wgpuCommandEncoderFinish(encoder,&cd);
    wgpuCommandEncoderRelease(encoder);
    ASSERT_NE(command,nullptr);
    const std::array commands{command,probe.command};
    const auto submitted=resources->submit(ticket,commands); wgpuCommandBufferRelease(command);
    ASSERT_TRUE(encoded.succeeded()); EXPECT_EQ(encoded.tickCount,1u);
    ASSERT_EQ(submitted,ShapeResourceError::None);
    // Retire immediately after submit; the real queued read still owns its use.
    ASSERT_EQ(resources->retire(handle),ShapeResourceError::None);
    Rows rows{}; ASSERT_TRUE(probe.read(context,rows)); ASSERT_TRUE(drain(context,*resources));
    EXPECT_EQ(rows[0],(std::array<uint32_t,4>{1,2,14,3})); EXPECT_EQ(rows[1][0],118u);
    std::optional<DebugSnapshot> snapshot;
    ASSERT_TRUE(wait(context,[&]{snapshot=backend.pollDebugSnapshot();return snapshot.has_value();}));
    ASSERT_EQ(snapshot->bodies.size(),1u); const auto& actual=snapshot->bodies[0];
    EXPECT_TRUE(actual.alive); EXPECT_EQ(actual.handle,body);
    EXPECT_NEAR(actual.position.x,2.0f+1.0f/60.0f,0.00002f);
    EXPECT_NEAR(actual.position.y,3.0f,0.00002f); EXPECT_NEAR(actual.position.z,4.0f,0.00002f);
    close(context,*resources); EXPECT_EQ(backend.stats().estimatedPersistentBytes,persistent);
    backend.shutdown(); EXPECT_EQ(backend.authoredShapeResources(),nullptr);
}

std::optional<DebugSnapshot> liveTick(Context& context, GpuPhysicsBackend& backend,
                                    BodyHandle body,bool schedule=true,AttachmentHandle rope={}) {
    if (schedule && !backend.scheduleFixedTicks(1)) return {};
    backend.requestDebugSnapshot({body.index,1,rope.index,rope.valid()?1u:0u});
    ShapeResourceError error;
    const auto ticket=backend.prepareGpuSubmission(error);
    if (!ticket.valid()) return {};
    WGPUCommandEncoderDescriptor ed{};
    const auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);
    if (!encoder) { (void)backend.discardGpuSubmission(ticket); return {}; }
    const auto encoded=backend.encodeGpuStepChecked(encoder);
    WGPUCommandBufferDescriptor cd{};
    const auto command=encoded.succeeded()?wgpuCommandEncoderFinish(encoder,&cd):nullptr;
    wgpuCommandEncoderRelease(encoder);
    if (!command) { (void)backend.discardGpuSubmission(ticket); return {}; }
    const std::array commands{command};
    error=backend.submitGpuSubmission(ticket,commands);
    wgpuCommandBufferRelease(command);
    if (error!=ShapeResourceError::None) return {};
    std::optional<DebugSnapshot> snapshot;
    if (!wait(context,[&]{ snapshot=backend.pollDebugSnapshot(); return snapshot.has_value(); })) return {};
    return snapshot;
}

// Cannonballs must reach the real authored contact solver, not merely stop at
// a broad bounding box. No terrain is attached in these cases.
void verifyStaticCannonSweep(Context& context, bool opening, bool rotated, bool distant, float height=0, bool accelerating=false) {
    GpuPhysicsBackend backend; auto config=backendContext(context);
    if (accelerating) config.gpu.gravity={14400,0,0};
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    std::vector<geometry::UnionBox> boxes;
    if (opening) {
        boxes={{{{-1,-100,-100},{1,-25,100}},1},{{{-1,25,-100},{1,100,100}},2}};
    } else { boxes={{{{-1,-100,-100},{1,100,100}},1}}; }
    geometry::BoxUnionIssue unionIssue;
    auto geometry=geometry::BoxUnion::compile(boxes,unionIssue); ASSERT_TRUE(geometry);
    AuthoredShapeIssue shapeIssue;
    auto prepared=AuthoredShape::prepare(*geometry,{10,{0,0,0},{10,0,0,0,10,0,0,0,10}},shapeIssue);
    ASSERT_TRUE(prepared); ShapeResourceError error;
    const auto shape=resources.upload(std::move(*prepared),error); ASSERT_TRUE(shape.valid());
    ASSERT_TRUE(drain(context,resources));
    AuthoredBodySpawnDesc wall; wall.shape=shape; wall.motionType=AuthoredBodyMotionType::Static;
    wall.motion.position.sector=distant ? glm::ivec3(2000000000,-2000000000,1800000000) : glm::ivec3(0);
    wall.motion.position.local=distant ? glm::vec3(127,0,0) : glm::vec3(0);
    wall.motion.orientation=rotated ? glm::angleAxis(.6f,glm::vec3(0,1,0)) : glm::quat(1,0,0,0);
    const auto target=backend.spawnAuthoredBody(wall); ASSERT_TRUE(target);
    for (int shot=0;shot<2;shot++) {
        BodySpawnDesc ball; ball.shape=ThrowableShape::Sphere; ball.dimensions={.2f,.2f,.2f};
        ball.bullet=true; ball.inverseMass=1; ball.sector=wall.motion.position.sector;
        ball.position=wall.motion.position.local+wall.motion.orientation*glm::vec3(-1,height,0);
        ball.linearVelocity=wall.motion.orientation*glm::vec3(accelerating ? 0.f : 240.f,rotated ? 12.f : 0.f,0);
        const auto projectile=backend.spawnBody(ball); ASSERT_TRUE(projectile.valid());
        auto snapshot=liveTick(context,backend,projectile); ASSERT_TRUE(snapshot);
        ASSERT_EQ(snapshot->bodies.size(),1u); const auto& actual=snapshot->bodies[0];
        const auto velocity=glm::inverse(wall.motion.orientation)*actual.linearVelocity;
        const auto relative=glm::inverse(wall.motion.orientation)*(actual.position-wall.motion.position.local
            +glm::vec3(actual.sector-wall.motion.position.sector)*256.f);
        if (opening || height>2.1f) {
            EXPECT_NEAR(velocity.x,240,0.001f);
            EXPECT_NEAR(relative.x,3,0.001f);
        } else {
            EXPECT_LT(velocity.x,-10.f) << "Normal solver restitution must remain active";
            EXPECT_LE(relative.x,-.115f) << "A 0.04-stud wall must stop a four-stud sweep";
            EXPECT_GT(relative.x,-4.1f) << "Time before impact must not be integrated twice";
            if (!accelerating) {
                const auto next=liveTick(context,backend,projectile); ASSERT_TRUE(next);
                const auto nextVelocity=glm::inverse(wall.motion.orientation)*next->bodies[0].linearVelocity;
                EXPECT_LT(nextVelocity.x,-10.f) << "Leaving the wall must not retrigger a zero-time hit";
            }
        }
        ASSERT_TRUE(backend.destroyBody(projectile)); ASSERT_TRUE(liveTick(context,backend,projectile));
    }
    ASSERT_TRUE(backend.destroyBody(target.body)); ASSERT_TRUE(liveTick(context,backend,target.body));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None); ASSERT_TRUE(drain(context,resources));
    close(context,resources); backend.shutdown();
}
TEST_F(GpuAuthoredShapes,CannonCcdEmitsEveryDenseHitWithEightSparsePatchSlots) {
    GpuPhysicsBackend backend;auto config=backendContext(context);config.gpu.authoredContactPatches=8;
    config.gpu.substeps=16;ASSERT_TRUE(backend.initialize(config));
    ASSERT_TRUE(backend.setEventReadbackEnabled(true));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources,7,false);
    AuthoredBodySpawnDesc wall;wall.shape=shape;wall.motionType=AuthoredBodyMotionType::Static;
    const auto target=backend.spawnAuthoredBody(wall);ASSERT_TRUE(target);
    std::array<BodyHandle,2> balls;
    for(size_t i=0;i<balls.size();++i) {
        BodySpawnDesc ball;ball.shape=ThrowableShape::Sphere;ball.dimensions={.2f,.2f,.2f};
        ball.bullet=true;ball.inverseMass=1;ball.position={-2,float(i)*1.2f-.6f,0};ball.linearVelocity={240,0,0};
        balls[i]=backend.spawnBody(ball);ASSERT_TRUE(balls[i].valid());
    }
    const auto snapshot=liveTick(context,backend,balls[0]);ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->bodies.size(),1u);EXPECT_LT(snapshot->bodies[0].linearVelocity.x,-10.f);
    std::optional<PhysicsEventBatch> events;
    ASSERT_TRUE(wait(context,[&]{events=backend.pollEvents();return events.has_value();}));
    EXPECT_FALSE(events->overflow);EXPECT_EQ(events->tick,snapshot->tick);
    std::array<bool,2> hit{};
    for(const auto& event:events->events)if(event.type==PhysicsEventType::ContactHit) {
        for(size_t i=0;i<balls.size();++i)if(event.bodyHandleA()==balls[i]||event.bodyHandleB()==balls[i]) {
            hit[i]=true;EXPECT_GT(event.impactSpeed,200.f);EXPECT_GT(event.impulse,0.f);
            EXPECT_TRUE(event.bodyHandleA()==target.body||event.bodyHandleB()==target.body);
            const auto feature=event.bodyHandleA()==target.body?event.featureId:event.otherFeatureId;
            EXPECT_NE(feature&0x80000000u,0u);
        }
    }
    EXPECT_TRUE(hit[0]);EXPECT_TRUE(hit[1])<<"Second dense contact lives beyond the first eight raw patch slots";
    for(auto ball:balls){ASSERT_TRUE(backend.destroyBody(ball));}
    ASSERT_TRUE(backend.destroyBody(target.body));
    ASSERT_TRUE(liveTick(context,backend,balls[0]));
    ASSERT_TRUE(wait(context,[&]{return backend.pollEvents().has_value();}));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
}
TEST_F(GpuAuthoredShapes,CannonCcdThinStaticWallRetainsSolverBounce) {
    verifyStaticCannonSweep(context,false,false,false);
}
TEST_F(GpuAuthoredShapes,CannonCcdObliqueWallInDistantSectorRetainsSolverBounce) {
    verifyStaticCannonSweep(context,false,true,true);
}
TEST_F(GpuAuthoredShapes,CannonCcdGrazingMissDoesNotHitWallBounds) {
    verifyStaticCannonSweep(context,false,false,false,2.12f);
}
TEST_F(GpuAuthoredShapes,CannonCcdUsesVelocityAfterForces) {
    verifyStaticCannonSweep(context,false,false,false,0,true);
}
TEST_F(GpuAuthoredShapes,CannonCcdRealOpeningRemainsClear) {
    verifyStaticCannonSweep(context,true,false,false);
}

void verifyCannonReviewCase(Context& context, bool separatingCompound, bool slow, bool zeroSpeculation) {
    GpuPhysicsBackend backend; auto config=backendContext(context);
    if (zeroSpeculation) config.gpu.speculativeDistance=0;
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    std::vector<geometry::UnionBox> boxes{{{{-1,-100,-100},{1,100,100}},1}};
    if (separatingCompound) boxes.push_back({{{99,-100,-100},{101,100,100}},2});
    geometry::BoxUnionIssue unionIssue;
    auto geometry=geometry::BoxUnion::compile(boxes,unionIssue); ASSERT_TRUE(geometry);
    AuthoredShapeIssue shapeIssue;
    auto prepared=AuthoredShape::prepare(*geometry,{10,{0,0,0},{10,0,0,0,10,0,0,0,10}},shapeIssue);
    ASSERT_TRUE(prepared); ShapeResourceError error;
    const auto shape=resources.upload(std::move(*prepared),error); ASSERT_TRUE(shape.valid());
    ASSERT_TRUE(drain(context,resources));
    const auto wall=backend.spawnAuthoredBody({.shape=shape,.motionType=AuthoredBodyMotionType::Static});
    ASSERT_TRUE(wall);
    BodySpawnDesc ball; ball.shape=ThrowableShape::Sphere;
    ball.dimensions=glm::vec3(separatingCompound ? .4f : .2f);
    ball.bullet=!slow; ball.inverseMass=1;
    ball.position={separatingCompound ? .22f : (slow ? -.16f : -1.f),0,0};
    ball.linearVelocity={slow ? 2.88f : 240.f,0,0};
    const auto projectile=backend.spawnBody(ball); ASSERT_TRUE(projectile.valid());
    const auto snapshot=liveTick(context,backend,projectile); ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->bodies.size(),1u);
    EXPECT_LT(snapshot->bodies[0].linearVelocity.x,0.f) << "A real entering face must produce solver response";
    EXPECT_LE(snapshot->bodies[0].position.x,separatingCompound ? 1.781f : -.119f);
    EXPECT_GT(snapshot->bodies[0].position.x,separatingCompound ? 1.5f : -2.f)
        << "The first separating face must not steal the later impact";
    ASSERT_TRUE(backend.destroyBody(projectile)); ASSERT_TRUE(backend.destroyBody(wall.body));
    ASSERT_TRUE(liveTick(context,backend,projectile));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None); ASSERT_TRUE(drain(context,resources));
    close(context,resources); backend.shutdown();
}
TEST_F(GpuAuthoredShapes,CannonCcdLeavingOneFaceStillHitsAnotherInSameCompound) {
    verifyCannonReviewCase(context,true,false,false);
}
TEST_F(GpuAuthoredShapes,CannonCcdSlowNonBulletStillReachesSolver) {
    verifyCannonReviewCase(context,false,true,false);
}
TEST_F(GpuAuthoredShapes,CannonCcdZeroSpeculativeDistanceStillReachesSolver) {
    verifyCannonReviewCase(context,false,false,true);
}

TEST_F(GpuAuthoredShapes, LiveBodyReceivesRootForceWithPreparedMassAndPrincipalInertia) {
    GpuPhysicsBackend backend; ASSERT_TRUE(backend.initialize(backendContext(context)));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    AuthoredBodySpawnDesc desc; desc.shape=shape;
    desc.motion.position={{7,-3,12},{120,20,-90}};
    desc.motion.orientation=glm::quat(std::sqrt(.5f),0,0,std::sqrt(.5f));
    const auto spawned=backend.spawnAuthoredBody(desc); ASSERT_TRUE(spawned);
    const AuthoredBodyFrame frame(*resources.get(shape)); AuthoredFrameError frameError;
    const auto point=frame.bodyPoint({1,-2,3},frameError); ASSERT_TRUE(point);
    PhysicsCommand force; force.type=PhysicsCommandType::ApplyForceAtLocalPoint;
    force.body=spawned.body; force.a={-3,2,-4,0}; force.b=glm::vec4(*point,0);
    backend.enqueue(std::span{&force,1});
    auto snapshot=liveTick(context,backend,spawned.body); ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->bodies.size(),1u); const auto& body=snapshot->bodies[0];
    EXPECT_TRUE(body.alive); EXPECT_EQ(body.handle,spawned.body); EXPECT_EQ(body.authoredShape,shape);
    EXPECT_EQ(body.sector,desc.motion.position.sector);
    EXPECT_NEAR(body.linearVelocity.x,-.6f/60,.00002f);
    EXPECT_NEAR(body.linearVelocity.y, .4f/60,.00002f);
    EXPECT_NEAR(body.linearVelocity.z,-.8f/60,.00002f);
    EXPECT_NEAR(body.angularVelocity.x,-5863.f/108000,.00002f);
    EXPECT_NEAR(body.angularVelocity.y,-1357.f/216000,.00002f);
    EXPECT_NEAR(body.angularVelocity.z,56.f/2700,.00002f);
    EXPECT_TRUE(backend.dynamicBodies(0).empty()); // Never render its broadphase box.
    ASSERT_TRUE(backend.destroyBody(spawned.body));
    snapshot=liveTick(context,backend,spawned.body); ASSERT_TRUE(snapshot);
    EXPECT_FALSE(snapshot->bodies[0].alive);
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);
    ASSERT_TRUE(drain(context,resources)); close(context,resources); backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveBodyRequiresOwnedSubmissionAndCancelsUnsubmittedSpawn) {
    GpuPhysicsBackend backend; ASSERT_TRUE(backend.initialize(backendContext(context)));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    const auto cancelled=backend.spawnAuthoredBody({.shape=shape}); ASSERT_TRUE(cancelled);
    ASSERT_TRUE(backend.destroyBody(cancelled.body)); EXPECT_EQ(backend.stats().residentBodies,0u);
    const auto live=backend.spawnAuthoredBody({.shape=shape}); ASSERT_TRUE(live);
    EXPECT_EQ(cancelled.body.index,live.body.index); EXPECT_NE(cancelled.body.generation,live.body.generation);
    ASSERT_TRUE(backend.scheduleFixedTicks(1));
    WGPUCommandEncoderDescriptor ed{};
    auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed); ASSERT_NE(encoder,nullptr);
    EXPECT_EQ(backend.encodeGpuStepChecked(encoder).status,PhysicsEncodeStatus::AuthoredSubmissionRequired);
    wgpuCommandEncoderRelease(encoder); EXPECT_EQ(backend.encodedTick(),0u);
    ShapeResourceError error; auto ticket=backend.prepareGpuSubmission(error); ASSERT_TRUE(ticket.valid());
    EXPECT_EQ(backend.spawnAuthoredBody({.shape=shape}).error,AuthoredBodyError::Busy);
    EXPECT_FALSE(backend.destroyBody(live.body)); EXPECT_FALSE(backend.scheduleFixedTicks(1));
    ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);
    EXPECT_TRUE(backend.isInitialized()); ASSERT_TRUE(drain(context,resources));
    ticket=backend.prepareGpuSubmission(error); ASSERT_TRUE(ticket.valid());
    encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed); ASSERT_NE(encoder,nullptr);
    ASSERT_TRUE(backend.encodeGpuStepChecked(encoder).succeeded());
    wgpuCommandEncoderRelease(encoder);
    EXPECT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);
    EXPECT_FALSE(backend.isInitialized()); // Encoded host mutations cannot be replayed after discard.
    ASSERT_TRUE(drain(context,resources)); backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveTickFrontierRequiresSubmissionAndGpuProof) {
    GpuPhysicsBackend backend;auto config=backendContext(context);config.gpu.maximumCatchUpTicks=2;
    ASSERT_TRUE(backend.initialize(config));EXPECT_FALSE(backend.tickFrontier().supported);
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    ShapeResourceError error;auto ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
    const auto incarnation=backend.tickFrontier().incarnation;EXPECT_NE(incarnation,0u);
    ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);
    ASSERT_TRUE(backend.scheduleFixedTicks(2));
    EXPECT_EQ(backend.tickFrontier().scheduled,2u);EXPECT_EQ(backend.tickFrontier().encoded,0u);
    ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
    WGPUCommandEncoderDescriptor ed{};auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);ASSERT_NE(encoder,nullptr);
    ASSERT_TRUE(backend.encodeGpuStepChecked(encoder).succeeded());
    EXPECT_EQ(backend.tickFrontier().encoded,2u);EXPECT_EQ(backend.tickFrontier().submitted,0u);
    EXPECT_EQ(backend.tickFrontier().completed,0u);
    EXPECT_FALSE(backend.encodeGpuStepChecked(encoder).succeeded());
    WGPUCommandBufferDescriptor cd{};auto command=wgpuCommandEncoderFinish(encoder,&cd);wgpuCommandEncoderRelease(encoder);
    ASSERT_NE(command,nullptr);ASSERT_EQ(backend.submitGpuSubmission(ticket,std::span{&command,1}),ShapeResourceError::None);
    wgpuCommandBufferRelease(command);
    EXPECT_EQ(backend.tickFrontier().submitted,2u);EXPECT_EQ(backend.tickFrontier().completed,0u);
    ASSERT_TRUE(wait(context,[&]{backend.stepCpu(0);return backend.tickFrontier().completed==2;}));
    EXPECT_EQ(backend.tickFrontier().pendingBatches,0u);EXPECT_FALSE(backend.tickFrontier().failed);
    // Paused frames only poll completion; they cannot manufacture new ticks.
    for(int i=0;i<30;++i)backend.stepCpu(0);
    EXPECT_EQ(backend.tickFrontier().scheduled,2u);
    EXPECT_FALSE(backend.scheduleFixedTicks(3));EXPECT_EQ(backend.tickFrontier().scheduled,2u);
    ASSERT_TRUE(backend.scheduleFixedTicks(1));ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
    encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);ASSERT_NE(encoder,nullptr);
    ASSERT_TRUE(backend.encodeGpuStepChecked(encoder).succeeded());wgpuCommandEncoderRelease(encoder);
    ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);
    ASSERT_TRUE(drain(context,resources));backend.stepCpu(0);
    EXPECT_TRUE(backend.tickFrontier().failed);EXPECT_EQ(backend.tickFrontier().completed,2u);
    EXPECT_EQ(backend.tickFrontier().submitted,2u);EXPECT_EQ(backend.tickFrontier().encoded,3u);
    close(context,resources);backend.shutdown();ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& replacement=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,replacement));
    ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
    EXPECT_NE(backend.tickFrontier().incarnation,incarnation);EXPECT_EQ(backend.tickFrontier().completed,0u);
    ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);close(context,replacement);backend.shutdown();
}

TEST_F(GpuAuthoredShapes, RestoredInitialTickPreservesFullClockAcrossGpuCounterWrap) {
    GpuPhysicsBackend backend;auto config=backendContext(context);
    constexpr uint64_t base=(uint64_t{1}<<53)-2; // Cross low-u32 rollover and JS exact-integer limit.
    config.gpu.initialTick=base;
    ASSERT_TRUE(backend.initialize(config));EXPECT_EQ(backend.encodedTick(),base);
    EXPECT_FALSE(backend.initialize(config)); // Initialization cannot rebase a live world.
    ASSERT_TRUE(backend.setEventReadbackEnabled(true));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    AuthoredBodySpawnDesc desc;desc.shape=shape;desc.motion.originVelocity={1,0,0};
    const auto body=backend.spawnAuthoredBody(desc);ASSERT_TRUE(body);
    for(uint64_t step=1;step<=4;++step) {
        auto snapshot=liveTick(context,backend,body.body);ASSERT_TRUE(snapshot);
        const auto frontier=backend.tickFrontier();
        EXPECT_EQ(frontier.baseTick,base);EXPECT_EQ(frontier.completed,base+step);
        EXPECT_EQ(frontier.encoded,base+step);EXPECT_EQ(frontier.submitted,base+step);
        EXPECT_EQ(snapshot->tick,base+step);EXPECT_FALSE(frontier.failed);
        ASSERT_TRUE(snapshot->bodies.front().alive);
        ASSERT_TRUE(wait(context,[&]{auto events=backend.pollEvents();if(!events)return false;
            EXPECT_EQ(events->tick,base+step);return true;}));
    }
    ASSERT_TRUE(backend.destroyBody(body.body));
    auto removed=liveTick(context,backend,body.body);ASSERT_TRUE(removed);EXPECT_FALSE(removed->bodies.front().alive);
    ASSERT_TRUE(wait(context,[&]{return backend.pollEvents().has_value();}));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
    config.gpu.initialTick=UINT64_MAX;EXPECT_FALSE(backend.initialize(config));
    config.gpu.initialTick=0;ASSERT_TRUE(backend.initialize(config));EXPECT_EQ(backend.encodedTick(),0u);
    backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LivePauseDrainsReservedTicksAndResumesWithoutClockDebt) {
    GpuPhysicsBackend backend;auto config=backendContext(context);
    config.gpu.maximumCatchUpTicks=2;config.gpu.enableRenderInterpolation=true;
    ASSERT_TRUE(backend.initialize(config));EXPECT_FALSE(backend.setSchedulingPaused(true));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    AuthoredBodySpawnDesc desc;desc.shape=shape;desc.motion.originVelocity={1,0,0};
    const auto body=backend.spawnAuthoredBody(desc);ASSERT_TRUE(body);
    ShapeResourceError error;const auto ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
    EXPECT_FALSE(backend.setSchedulingPaused(true)); // No scheduling edits inside an owned submission.
    ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);
    backend.stepCpu(2.5f/60);EXPECT_EQ(backend.tickFrontier().scheduled,2u);
    EXPECT_FALSE(backend.setSchedulingPaused(true,1)); // Atomic capacity refusal; retain both scheduled ticks.
    EXPECT_EQ(backend.tickFrontier().scheduled,2u);
    ASSERT_TRUE(backend.setSchedulingPaused(true));
    for(int frame=0;frame<30;++frame)backend.stepCpu(1);
    EXPECT_EQ(backend.tickFrontier().scheduled,2u);EXPECT_FALSE(backend.scheduleFixedTicks(1));
    auto stopped=liveTick(context,backend,body.body,false);ASSERT_TRUE(stopped);
    EXPECT_EQ(stopped->tick,2u);EXPECT_EQ(backend.tickFrontier().completed,2u);
    EXPECT_FLOAT_EQ(backend.renderView().interpolationAlpha,1); // Show the captured pose while paused.
    const auto observed=liveTick(context,backend,body.body,false);ASSERT_TRUE(observed);
    EXPECT_EQ(observed->tick,2u);EXPECT_EQ(observed->bodies.front().position,stopped->bodies.front().position);
    ASSERT_TRUE(backend.setSchedulingPaused(false));
    backend.stepCpu(.25f/60);EXPECT_EQ(backend.tickFrontier().scheduled,2u);
    backend.stepCpu(.75f/60);EXPECT_EQ(backend.tickFrontier().scheduled,3u);
    auto moving=liveTick(context,backend,body.body,false);ASSERT_TRUE(moving);
    EXPECT_GT(moving->bodies.front().position.x,stopped->bodies.front().position.x);
    ASSERT_TRUE(backend.setSchedulingPaused(true,1));EXPECT_EQ(backend.tickFrontier().scheduled,4u);
    EXPECT_FALSE(backend.setSchedulingPaused(true,1));EXPECT_FALSE(backend.setSchedulingPaused(false,1));
    for(int frame=0;frame<30;++frame)backend.stepCpu(1);
    stopped=liveTick(context,backend,body.body,false);ASSERT_TRUE(stopped);EXPECT_EQ(stopped->tick,4u);
    EXPECT_EQ(backend.tickFrontier().scheduled,4u);EXPECT_FALSE(backend.tickFrontier().failed);
    ASSERT_TRUE(backend.setSchedulingPaused(false));
    ASSERT_TRUE(backend.destroyBody(body.body));backend.stepCpu(1.f/60);
    ASSERT_TRUE(liveTick(context,backend,body.body,false));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
    ASSERT_TRUE(backend.initialize(config));EXPECT_FALSE(backend.setSchedulingPaused(true));
    backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveTickOrderMatchesThirtySixtyAndOneFortyFourHz) {
    for(int frameRate:{30,60,144}) {
        SCOPED_TRACE(frameRate);
        GpuPhysicsBackend backend;ASSERT_TRUE(backend.initialize(backendContext(context)));
        ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
        auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
        ShapeResourceError error;auto ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
        ASSERT_EQ(backend.discardGpuSubmission(ticket),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
        for(int frame=0;frame<frameRate*2;++frame) {
            backend.stepCpu(1.0f/static_cast<float>(frameRate));
            const auto before=backend.tickFrontier();
            ASSERT_LE(before.scheduled-before.completed,before.maximumInFlightTicks);
            ticket=backend.prepareGpuSubmission(error);ASSERT_TRUE(ticket.valid());
            WGPUCommandEncoderDescriptor ed{};auto encoder=wgpuDeviceCreateCommandEncoder(context.getDevice(),&ed);ASSERT_NE(encoder,nullptr);
            ASSERT_TRUE(backend.encodeGpuStepChecked(encoder).succeeded());
            WGPUCommandBufferDescriptor cd{};auto command=wgpuCommandEncoderFinish(encoder,&cd);wgpuCommandEncoderRelease(encoder);
            ASSERT_NE(command,nullptr);ASSERT_EQ(backend.submitGpuSubmission(ticket,std::span{&command,1}),ShapeResourceError::None);
            wgpuCommandBufferRelease(command);
            ASSERT_TRUE(wait(context,[&]{backend.stepCpu(0);return backend.tickFrontier().completed==before.scheduled
                && resources.stats().pendingOperations==0;}));
        }
        EXPECT_EQ(backend.tickFrontier().completed,120u);
        EXPECT_EQ(backend.tickFrontier().completed,backend.tickFrontier().submitted);
        close(context,resources);backend.shutdown();
    }
}

TEST_F(GpuAuthoredShapes, LiveStaticSceneryRefusesMotionAndRetainsExactShape) {
    GpuPhysicsBackend backend; ASSERT_TRUE(backend.initialize(backendContext(context)));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    AuthoredBodySpawnDesc desc;desc.shape=shape;desc.motionType=AuthoredBodyMotionType::Static;
    desc.motion.position={{3,-2,4},{3,4,5}};
    desc.motion.originVelocity={1,0,0};
    EXPECT_EQ(backend.spawnAuthoredBody(desc).error,AuthoredBodyError::InvalidMotion);
    desc.motion.originVelocity={0,0,0};
    const auto spawned=backend.spawnAuthoredBody(desc);ASSERT_TRUE(spawned);
    const auto first=liveTick(context,backend,spawned.body);ASSERT_TRUE(first);
    const auto& initial=first->bodies[0];
    EXPECT_EQ(initial.authoredShape,shape);EXPECT_EQ(initial.inverseInertia,glm::vec3(0));
    const std::array cells{AuthoredWaterCell{{-1,-1,-1},{1,1,1}}};
    EXPECT_EQ(backend.configureAuthoredWaterBody({.body=spawned.body,.cells=cells}),AuthoredBodyError::Unsupported);
    for(const auto type:{PhysicsCommandType::ApplyImpulse,PhysicsCommandType::ApplyForceAtLocalPoint,
        PhysicsCommandType::SetVelocity,PhysicsCommandType::SetAngularVelocity,
        PhysicsCommandType::SetKinematicTarget,PhysicsCommandType::Teleport}) {
        PhysicsCommand command;command.type=type;command.body=spawned.body;
        command.a={1000,2000,3000,0};command.b={0,0,0,1};backend.enqueue(std::span{&command,1});
    }
    const auto after=liveTick(context,backend,spawned.body);ASSERT_TRUE(after);
    const auto& fixed=after->bodies[0];
    EXPECT_EQ(fixed.position,initial.position);EXPECT_EQ(fixed.sector,initial.sector);
    EXPECT_EQ(fixed.orientation,initial.orientation);
    EXPECT_EQ(fixed.linearVelocity,glm::vec3(0));EXPECT_EQ(fixed.angularVelocity,glm::vec3(0));
    ASSERT_TRUE(backend.destroyBody(spawned.body));ASSERT_TRUE(liveTick(context,backend,spawned.body));
    const auto dynamic=backend.spawnAuthoredBody({.shape=shape});ASSERT_TRUE(dynamic);
    EXPECT_EQ(dynamic.body.index,spawned.body.index);EXPECT_NE(dynamic.body.generation,spawned.body.generation);
    const auto reused=liveTick(context,backend,dynamic.body);ASSERT_TRUE(reused);
    EXPECT_GT(reused->bodies[0].inverseInertia.x,0);
    ASSERT_TRUE(backend.destroyBody(dynamic.body));ASSERT_TRUE(liveTick(context,backend,dynamic.body));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveWaterBalancesDisplacementAndSteeredPropellerAppliesTorque) {
    GpuPhysicsBackend backend; auto config=backendContext(context); config.gpu.gravity={0,-9.81f,0};
    ASSERT_TRUE(backend.initialize(config)); backend.setWaterPlane(0,true);
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources(); ASSERT_TRUE(drain(context,resources));
    geometry::UnionBox box{{{-50,-25,-25},{50,25,25}},1};
    geometry::BoxUnionIssue unionIssue;
    auto geometry=geometry::BoxUnion::compile(std::span{&box,1},unionIssue); ASSERT_TRUE(geometry);
    AuthoredShapeIssue shapeIssue;
    auto prepared=AuthoredShape::prepare(*geometry,{1000,{0,0,0},
        {1000./6,0,0,0,5000./12,0,0,0,5000./12}},shapeIssue); ASSERT_TRUE(prepared);
    ShapeResourceError error;
    const auto shape=resources.upload(std::move(*prepared),error); ASSERT_TRUE(shape.valid());
    ASSERT_TRUE(drain(context,resources));
    const auto spawned=backend.spawnAuthoredBody({.shape=shape}); ASSERT_TRUE(spawned);
    const std::array cells{AuthoredWaterCell{{-1,-.5f,-.5f},{1,.5f,.5f}}};
    AuthoredWaterBodyDesc water;
    water.body=spawned.body; water.cells=cells; water.propellerPoint={0,-.25f,.8f};
    water.maximumThrustNewtons=1200; water.maximumSteeringRadians=.6f;
    water.linearDragPerSecond=0; water.angularDragPerSecond=0;
    ASSERT_EQ(backend.configureAuthoredWaterBody(water),AuthoredBodyError::None);
    auto snapshot=liveTick(context,backend,spawned.body); ASSERT_TRUE(snapshot);
    const auto& rest=snapshot->bodies[0];
    EXPECT_NEAR(rest.linearVelocity.y,0,.00001f);
    EXPECT_NEAR(glm::length(rest.angularVelocity),0,.00001f);
    EXPECT_NEAR(rest.position.y,0,.00001f);
    EXPECT_FALSE(backend.setAuthoredHelm(spawned.body,2,0));
    ASSERT_TRUE(backend.setAuthoredHelm(spawned.body,1,.5f));
    snapshot=liveTick(context,backend,spawned.body); ASSERT_TRUE(snapshot);
    const auto& driven=snapshot->bodies[0];
    const float fx=-1200*std::sin(.3f), fz=-1200*std::cos(.3f);
    EXPECT_NEAR(driven.linearVelocity.x,fx/1000/60,.00001f);
    EXPECT_NEAR(driven.linearVelocity.y,0,.00001f);
    EXPECT_NEAR(driven.linearVelocity.z,fz/1000/60,.00001f);
    EXPECT_NEAR(driven.angularVelocity.x,-.25f*fz/(1000.f/6)/60,.00001f);
    EXPECT_NEAR(driven.angularVelocity.y,.8f*fx/(5000.f/12)/60,.00001f);
    EXPECT_NEAR(driven.angularVelocity.z,.25f*fx/(5000.f/12)/60,.00001f);
    ASSERT_TRUE(backend.destroyBody(spawned.body)); ASSERT_TRUE(liveTick(context,backend,spawned.body));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None); ASSERT_TRUE(drain(context,resources));
    close(context,resources); backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveWinchBreakIsConfirmedAndUnreadEventsBackpressureBeforeEncoding) {
    GpuPhysicsBackend backend;auto config=backendContext(context);
    config.gpu.maximumCatchUpTicks=1;config.gpu.eventReadbackSlots=1;
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_TRUE(backend.setEventReadbackEnabled(true));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    AuthoredBodySpawnDesc anchor;anchor.shape=shape;anchor.motionType=AuthoredBodyMotionType::Static;
    const auto fixed=backend.spawnAuthoredBody(anchor);ASSERT_TRUE(fixed);
    AuthoredBodySpawnDesc load;load.shape=shape;load.motion.position=worldPositionFromAbsolute({8,0,0});
    const auto moving=backend.spawnAuthoredBody(load);ASSERT_TRUE(moving);
    AuthoredFrameError frameError;const auto point=AuthoredBodyFrame(prepared()).bodyPoint({0,0,0},frameError);ASSERT_TRUE(point);
    DistanceAttachmentDesc desc;desc.bodyA=fixed.body;desc.bodyB=moving.body;
    desc.localAnchorA=*point;desc.localAnchorB=*point;desc.targetLength=1;desc.breakForce=10;desc.maximumForce=2000;
    const auto rope=backend.createDistanceAttachment(desc);ASSERT_TRUE(rope.valid());
    const auto snapshot=liveTick(context,backend,moving.body,true,rope);ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->tick,1u);EXPECT_EQ(snapshot->confirmedIncarnation,backend.tickFrontier().incarnation);
    ASSERT_EQ(snapshot->attachments.size(),1u);
    const auto& constraint=snapshot->attachments.front();
    EXPECT_EQ(constraint.handle,rope);EXPECT_TRUE(constraint.broken);EXPECT_FALSE(constraint.alive);
    EXPECT_EQ(constraint.breakTick,snapshot->tick);
    EXPECT_EQ(constraint.distance.bodyA,fixed.body);EXPECT_EQ(constraint.distance.bodyB,moving.body);
    ASSERT_TRUE(backend.scheduleFixedTicks(1));
    ShapeResourceError error;EXPECT_FALSE(backend.prepareGpuSubmission(error).valid());
    EXPECT_EQ(error,ShapeResourceError::Busy);EXPECT_EQ(backend.tickFrontier().encoded,1u);
    EXPECT_FALSE(backend.tickFrontier().failed);
    EXPECT_FALSE(backend.setEventReadbackEnabled(false));
    PhysicsEventBatch events;
    const auto receive=[&]{auto next=backend.pollEvents();if(!next)return false;events=std::move(*next);return true;};
    ASSERT_TRUE(wait(context,receive));
    EXPECT_EQ(events.tick,1u);EXPECT_EQ(events.confirmedIncarnation,snapshot->confirmedIncarnation);
    const auto broken=std::find_if(events.events.begin(),events.events.end(),
        [](const auto& event){return event.type==PhysicsEventType::AttachmentBreak;});
    ASSERT_NE(broken,events.events.end());EXPECT_EQ(broken->attachmentHandle(),rope);
    EXPECT_EQ(broken->bodyHandleA(),fixed.body);EXPECT_EQ(broken->bodyHandleB(),moving.body);
    EXPECT_GT(broken->force,desc.breakForce);
    ASSERT_TRUE(liveTick(context,backend,moving.body,false));
    events={};ASSERT_TRUE(wait(context,receive));
    EXPECT_EQ(events.tick,2u);EXPECT_TRUE(events.events.empty());
    ASSERT_TRUE(backend.destroyAttachment(rope));
    ASSERT_TRUE(backend.destroyBody(fixed.body));ASSERT_TRUE(backend.destroyBody(moving.body));
    ASSERT_TRUE(liveTick(context,backend,moving.body));
    events={};ASSERT_TRUE(wait(context,receive));
    ASSERT_TRUE(backend.setEventReadbackEnabled(false));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveForceLimitedWinchStallsInsteadOfInventingCableOverload) {
    GpuPhysicsBackend backend;auto config=backendContext(context);config.gpu.maximumCatchUpTicks=1;
    ASSERT_TRUE(backend.initialize(config));ASSERT_TRUE(backend.setEventReadbackEnabled(true));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    const auto shape=upload(context,resources);
    const auto anchor=backend.spawnAuthoredBody({.shape=shape,.motionType=AuthoredBodyMotionType::Static});ASSERT_TRUE(anchor);
    AuthoredBodySpawnDesc load;load.shape=shape;load.motion.position=worldPositionFromAbsolute({8,0,0});
    const auto cargo=backend.spawnAuthoredBody(load);ASSERT_TRUE(cargo);
    AuthoredFrameError error;const auto point=AuthoredBodyFrame(prepared()).bodyPoint({0,0,0},error);ASSERT_TRUE(point);
    DistanceAttachmentDesc desc;desc.bodyA=anchor.body;desc.bodyB=cargo.body;desc.localAnchorA=*point;desc.localAnchorB=*point;
    desc.targetLength=8;desc.motorSpeed=100;desc.maximumForce=1;desc.breakForce=10;
    const auto rope=backend.createDistanceAttachment(desc);ASSERT_TRUE(rope.valid());
    for(int tick=0;tick<5;++tick) {
        const auto snapshot=liveTick(context,backend,cargo.body);ASSERT_TRUE(snapshot);
        EXPECT_LT(glm::length(snapshot->bodies[0].linearVelocity),.03f);
        bool broken=false;
        ASSERT_TRUE(wait(context,[&]{auto events=backend.pollEvents();if(!events)return false;
            for(const auto& event:events->events) broken|=event.type==PhysicsEventType::AttachmentBreak;
            return true;}));
        EXPECT_FALSE(broken);
    }
    ASSERT_TRUE(backend.destroyAttachment(rope));ASSERT_TRUE(backend.destroyBody(anchor.body));ASSERT_TRUE(backend.destroyBody(cargo.body));
    ASSERT_TRUE(liveTick(context,backend,cargo.body));
    ASSERT_TRUE(wait(context,[&]{return backend.pollEvents().has_value();}));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));close(context,resources);backend.shutdown();
}

TEST_F(GpuAuthoredShapes, LiveTwoWaterBodiesKeepIndependentBuoyancyAndHelmControls) {
    GpuPhysicsBackend backend;auto config=backendContext(context);config.gpu.gravity={0,-9.81f,0};
    ASSERT_TRUE(backend.initialize(config));backend.setWaterPlane(0,true);
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto& resources=*backend.authoredShapeResources();ASSERT_TRUE(drain(context,resources));
    geometry::UnionBox box{{{-50,-25,-25},{50,25,25}},1};geometry::BoxUnionIssue unionIssue;
    const auto geometry=geometry::BoxUnion::compile(std::span{&box,1},unionIssue);ASSERT_TRUE(geometry);
    AuthoredShapeIssue shapeIssue;
    auto prepared=AuthoredShape::prepare(*geometry,{1000,{0,0,0},{1000./6,0,0,0,5000./12,0,0,0,5000./12}},shapeIssue);
    ASSERT_TRUE(prepared);ShapeResourceError error;const auto shape=resources.upload(std::move(*prepared),error);
    ASSERT_TRUE(shape.valid());ASSERT_TRUE(drain(context,resources));
    const auto a=backend.spawnAuthoredBody({.shape=shape});ASSERT_TRUE(a);
    AuthoredBodySpawnDesc desc;desc.shape=shape;desc.motion.position=worldPositionFromAbsolute({10,0,0});
    const auto b=backend.spawnAuthoredBody(desc);ASSERT_TRUE(b);
    const std::array cells{AuthoredWaterCell{{-1,-.5f,-.5f},{1,.5f,.5f}}};
    AuthoredWaterBodyDesc water;water.body=a.body;water.cells=cells;water.maximumThrustNewtons=1200;
    water.propellerPoint={0,-.25f,0};water.linearDragPerSecond=0;water.angularDragPerSecond=0;
    ASSERT_EQ(backend.configureAuthoredWaterBody(water),AuthoredBodyError::None);
    water.body=b.body;water.maximumThrustNewtons=0;
    ASSERT_EQ(backend.configureAuthoredWaterBody(water),AuthoredBodyError::None);
    ASSERT_TRUE(backend.setAuthoredHelm(a.body,1,0));
    auto snapshot=liveTick(context,backend,a.body);ASSERT_TRUE(snapshot);
    EXPECT_LT(snapshot->bodies[0].linearVelocity.z,-.019f);
    EXPECT_NEAR(snapshot->bodies[0].linearVelocity.y,0,.00001f);
    snapshot=liveTick(context,backend,b.body);ASSERT_TRUE(snapshot);
    EXPECT_NEAR(glm::length(snapshot->bodies[0].linearVelocity),0,.00001f);
    ASSERT_TRUE(backend.destroyBody(a.body));ASSERT_TRUE(liveTick(context,backend,b.body));
    snapshot=liveTick(context,backend,b.body);ASSERT_TRUE(snapshot);
    EXPECT_NEAR(snapshot->bodies[0].position.y,0,.00001f);
    EXPECT_FALSE(backend.setAuthoredHelm(a.body,1,0));
    ASSERT_TRUE(backend.destroyBody(b.body));ASSERT_TRUE(liveTick(context,backend,b.body));
    ASSERT_EQ(resources.retire(shape),ShapeResourceError::None);ASSERT_TRUE(drain(context,resources));
    close(context,resources);backend.shutdown();
}

TEST_F(GpuAuthoredShapes, BackendRestartRejectsOldWorldShapeIdentity) {
    GpuPhysicsBackend backend; const auto config=backendContext(context);
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto* first=backend.authoredShapeResources(); ASSERT_NE(first,nullptr);
    ASSERT_TRUE(drain(context,*first)); const auto old=upload(context,*first);
    close(context,*first); backend.shutdown();
    ASSERT_TRUE(backend.initialize(config));
    ASSERT_EQ(backend.enableAuthoredShapeResources(smallLimits()),ShapeResourceError::None);
    auto* second=backend.authoredShapeResources(); ASSERT_NE(second,nullptr);
    ASSERT_TRUE(drain(context,*second)); const auto replacement=upload(context,*second,31);
    EXPECT_EQ(old.index,replacement.index); EXPECT_EQ(old.generation,replacement.generation);
    EXPECT_NE(old.pool,replacement.pool);
    const auto before=second->stats().cpu;
    EXPECT_EQ(second->get(old),nullptr); EXPECT_EQ(second->retain(old),ShapeResourceError::InvalidHandle);
    EXPECT_EQ(second->retire(old),ShapeResourceError::InvalidHandle);
    const std::array oldHandles{old}; ShapeResourceError error;
    EXPECT_FALSE(second->prepareSubmission(oldHandles,error).valid()); EXPECT_EQ(error,ShapeResourceError::InvalidHandle);
    EXPECT_EQ(second->stats().cpu,before);
    const auto rows=runProbe(context,*second,replacement);
    EXPECT_EQ(rows[0],(std::array<uint32_t,4>{1,2,14,3})); EXPECT_EQ(rows[1][0],454u);
    close(context,*second); backend.shutdown();
}
} // namespace
} // namespace voxy::physics

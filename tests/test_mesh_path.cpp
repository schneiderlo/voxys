#include <gtest/gtest.h>

#include "gpu/context.hpp"
#include "gpu/resources.hpp"
#include "moto/vmesh.hpp"
#include "game/assets/rigid_prefab.hpp"
#include "render/mesh_path.hpp"
#include "render/opaque_scene.hpp"
#include <glm/gtc/packing.hpp>
#include "render/primitive_path.hpp"

#include <cstring>
#include <bit>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <thread>

#include <glm/gtc/matrix_transform.hpp>

#if !defined(VOXY_WASM)
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, const WGPUWrappedSubmissionIndex*);
#else
#include <emscripten.h>
extern "C" WGPUDevice emscripten_webgpu_get_device(void);
#endif

namespace voxy::render {

TEST(MeshPathTest, RejectsNullGpuHandlesAndInvalidData) {
    MeshPath path;
    EXPECT_FALSE(path.init(nullptr, nullptr));
    EXPECT_FALSE(path.loadMeshData({}));
}

TEST(MeshPathTest, OpaquePaintDecodesSrgbOnceAndNeverUsesSwatchAlpha) {
    const auto paint = opaqueSrgbPaintOverride({0, 128, 255, 0});
    EXPECT_FLOAT_EQ(paint.r, 0.0f);
    EXPECT_NEAR(paint.g, 0.2158605001f, 1e-7f);
    EXPECT_FLOAT_EQ(paint.b, 1.0f);
    EXPECT_FLOAT_EQ(paint.w, 1.0f);
    EXPECT_EQ(paint, opaqueSrgbPaintOverride({0, 128, 255, 255}));
    // Both sides of the IEC sRGB transfer boundary, not only its endpoints.
    const auto boundary = opaqueSrgbPaintOverride({10, 11, 64, 127});
    EXPECT_NEAR(boundary.r, 0.0030352698f, 1e-9f);
    EXPECT_NEAR(boundary.g, 0.0033465358f, 1e-9f);
    EXPECT_NEAR(boundary.b, 0.0512694584f, 1e-7f);
    EXPECT_EQ(MeshDrawInstance{}.baseColorOverride, glm::vec4(0.0f));
    // White is only a sentinel at the application boundary. This conversion
    // helper must remain usable as a real opaque white material replacement.
    EXPECT_EQ(opaqueSrgbPaintOverride({255, 255, 255, 255}), glm::vec4(1.0f));
}

namespace {

// The browser supplies a real requested device. It must not call the native
// synchronous adapter path or count an unavailable browser adapter as a skip.
struct DiagnosticContext {
#if defined(VOXY_WASM)
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    bool initHeadless() {
        device = emscripten_webgpu_get_device();
        if (device) queue = wgpuDeviceGetQueue(device);
        return device && queue;
    }
    WGPUDevice getDevice() const { return device; }
    WGPUQueue getQueue() const { return queue; }
    ~DiagnosticContext() {
        if (queue) wgpuQueueRelease(queue);
        if (device) wgpuDeviceRelease(device);
    }
#else
    gpu::Context context;
    bool initHeadless() { return context.initHeadless(); }
    WGPUDevice getDevice() const { return context.getDevice(); }
    WGPUQueue getQueue() const { return context.getQueue(); }
#endif
};

moto::VmeshData diagnosticQuad(bool diagonalNormal = false) {
    moto::VmeshData data;
    data.header.flags = moto::kVmeshHasTangent;
    data.header.vertexCount = 4u;
    data.header.indexCount = 6u;
    data.header.submeshCount = 1u;
    data.header.materialCount = 1u;
    data.header.meshCount = 1u;
    const float a = diagonalNormal ? std::sqrt(0.5f) : 1.0f;
    const float b = diagonalNormal ? a : 0.0f;
    // Front is canonical -Z; from -Z the actual LH camera has +X screen-right.
    // Top-left, bottom-left, bottom-right, top-right in that declared view.
    std::array<moto::VmeshVertex, 4> vertices{};
    constexpr std::array<glm::vec2,4> corners{{{-1,1},{-1,-1},{1,-1},{1,1}}};
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto& v = vertices[i];
        v.position[0] = a * corners[i].x; v.position[1] = corners[i].y; v.position[2] = b * corners[i].x;
        v.normal[0] = b; v.normal[2] = -a;
        v.tangent[0] = a; v.tangent[2] = b; v.tangent[3] = 1;
        v.texCoord[0] = (corners[i].x + 1.0f) * 0.5f;
        v.texCoord[1] = (1.0f - corners[i].y) * 0.5f;
    }
    constexpr std::array<uint16_t,6> indices{{0,2,1,0,3,2}};
    data.vertices.resize(sizeof(vertices));
    std::memcpy(data.vertices.data(), vertices.data(), sizeof(vertices));
    data.indices.resize(sizeof(indices));
    std::memcpy(data.indices.data(), indices.data(), sizeof(indices));
    data.submeshes.push_back({0,6,0,0});
    data.materials.emplace_back();
    return data;
}

void setDiagnosticTexture(moto::VmeshData& data, moto::VmeshTextureSlot slot,
                          const std::array<uint8_t,16>& pixels) {
    auto& m = data.materials[0];
    m.hasTexture[slot] = 1;
    m.textureIsSrgb[slot] = (slot == moto::VmeshTextureBaseColor || slot == moto::VmeshTextureEmissive) ? 1 : 0;
    m.textureWidth[slot] = 2; m.textureHeight[slot] = 2;
    m.textureOffset[slot] = static_cast<uint32_t>(data.images.size());
    m.textureSize[slot] = 16;
    data.images.insert(data.images.end(), pixels.begin(), pixels.end());
}

struct PixelResources {
    WGPUTexture color = nullptr;
    WGPUTexture depth = nullptr;
    WGPUTextureView colorView = nullptr;
    WGPUTextureView depthView = nullptr;
    WGPUBuffer readback = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer command = nullptr;
    ~PixelResources() {
        if (command) wgpuCommandBufferRelease(command);
        if (encoder) wgpuCommandEncoderRelease(encoder);
        if (readback) { wgpuBufferDestroy(readback); wgpuBufferRelease(readback); }
        if (depthView) wgpuTextureViewRelease(depthView);
        if (colorView) wgpuTextureViewRelease(colorView);
        if (depth) { wgpuTextureDestroy(depth); wgpuTextureRelease(depth); }
        if (color) { wgpuTextureDestroy(color); wgpuTextureRelease(color); }
    }
};

bool drawDiagnosticPixels(MeshPath& path, DiagnosticContext& context, glm::vec3 camera,
                          const PrimitiveLighting& lighting, std::vector<uint8_t>& pixels,
                          bool perspective = false, bool releaseBeforeSubmit = false) {
    constexpr uint32_t extent = 64u;
    constexpr uint32_t rowBytes = 256u;
    constexpr size_t byteCount = size_t{rowBytes} * extent;
    PixelResources resources;
    auto colorDesc = gpu::TextureDesc::renderTarget(extent, extent, WGPUTextureFormat_RGBA8Unorm, "mesh_diagnostic_color");
    colorDesc.usage |= WGPUTextureUsage_CopySrc;
    resources.color = gpu::createTexture(context.getDevice(), colorDesc);
    resources.depth = gpu::createTexture(context.getDevice(), gpu::TextureDesc::depth(
        extent, extent, WGPUTextureFormat_Depth32Float, "mesh_diagnostic_depth"));
    if (!resources.color || !resources.depth) return false;
    resources.colorView = gpu::createTextureView(resources.color);
    resources.depthView = gpu::createTextureView(resources.depth);
    resources.readback = gpu::createBuffer(context.getDevice(), gpu::BufferDesc{
        .label = "mesh_diagnostic_pixels", .size = byteCount,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead});
    WGPUCommandEncoderDescriptor encoderDescriptor{};
    resources.encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), &encoderDescriptor);
    if (!resources.colorView || !resources.depthView || !resources.readback || !resources.encoder) return false;
    if (!path.encodeEnvironmentLighting(resources.encoder)) return false;
    WGPURenderPassColorAttachment color{};
    color.view = resources.colorView; color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {0,0,0,1}; color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view = resources.depthView; depth.depthLoadOp = WGPULoadOp_Clear; depth.depthStoreOp = WGPUStoreOp_Store;
    depth.depthClearValue = 1; depth.stencilLoadOp = WGPULoadOp_Undefined; depth.stencilStoreOp = WGPUStoreOp_Undefined;
    depth.stencilReadOnly = true;
    WGPURenderPassDescriptor passDescriptor{};
    passDescriptor.colorAttachmentCount = 1; passDescriptor.colorAttachments = &color;
    passDescriptor.depthStencilAttachment = &depth;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(resources.encoder, &passDescriptor);
    if (!pass) return false;
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    if (!path.render(resources.encoder, resources.colorView, resources.depthView,
        glm::lookAt(camera, glm::vec3(0), glm::vec3(0,1,0)),
        perspective ? glm::perspective(glm::radians(55.0f),1.0f,0.1f,10.0f)
                    : glm::ortho(-1.6f,1.6f,-1.6f,1.6f,0.1f,10.0f),
        camera, lighting, extent, extent, false)) {
        path.discardEnvironmentEncoding();
        return false;
    }
    gpu::CompatImageCopyTexture source{}; source.texture = resources.color; source.aspect = WGPUTextureAspect_All;
#if defined(VOXY_WASM)
    WGPUTexelCopyBufferInfo destination{};
#else
    WGPUImageCopyBuffer destination{};
#endif
    destination.buffer = resources.readback;
    destination.layout.bytesPerRow = rowBytes; destination.layout.rowsPerImage = extent;
    const WGPUExtent3D copyExtent{extent,extent,1};
    wgpuCommandEncoderCopyTextureToBuffer(resources.encoder, &source, &destination, &copyExtent);
    WGPUCommandBufferDescriptor commandDescriptor{};
    resources.command = wgpuCommandEncoderFinish(resources.encoder, &commandDescriptor);
    if (!resources.command) return false;
    if (releaseBeforeSubmit) path.releaseHandles();
    wgpuQueueSubmit(context.getQueue(), 1, &resources.command);
    path.acknowledgeEnvironmentSubmission();
    // A timeout must not leave a callback pointing at a dead stack frame.
    auto state = std::make_shared<std::atomic<int>>(0);
    using Payload = std::shared_ptr<std::atomic<int>>;
    auto* payload = new Payload(state);
#if defined(VOXY_WASM)
    WGPUBufferMapCallbackInfo mapInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mapInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    mapInfo.userdata1 = payload;
    mapInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
        const std::unique_ptr<Payload> completion(static_cast<Payload*>(userdata));
        (*completion)->store(status == WGPUMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
    };
    static_cast<void>(wgpuBufferMapAsync(resources.readback, WGPUMapMode_Read, 0, byteCount, mapInfo));
#else
    wgpuBufferMapAsync(resources.readback, WGPUMapMode_Read, 0, byteCount,
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            const std::unique_ptr<Payload> completion(static_cast<Payload*>(userdata));
            (*completion)->store(status == WGPUBufferMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
        }, payload);
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (state->load(std::memory_order_acquire) == 0 && std::chrono::steady_clock::now() < deadline) {
#if defined(VOXY_WASM)
        emscripten_sleep(1);
#else
        static_cast<void>(wgpuDevicePoll(context.getDevice(), false, nullptr));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    if (state->load(std::memory_order_acquire) != 1) return false;
    const auto* bytes = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(resources.readback, 0, byteCount));
    if (!bytes) { wgpuBufferUnmap(resources.readback); return false; }
    pixels.assign(bytes, bytes + byteCount);
    wgpuBufferUnmap(resources.readback);
    if (const char* directory = std::getenv("VOXY_MESH_CAPTURE_DIR"); directory && *directory) {
        static uint32_t sequence = 0;
        const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string name = std::to_string(sequence++) + "-" + test->name();
        const auto output = std::filesystem::path(directory) / (name + ".rgba");
        std::ofstream file(output, std::ios::binary);
        if (!file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))) return false;
        std::ofstream metadata(std::filesystem::path(directory) / (name + ".json"));
        metadata << "{\"width\":64,\"height\":64,\"format\":\"rgba8unorm\",\"camera\":["
                 << camera.x << ',' << camera.y << ',' << camera.z << "],\"light\":["
                 << lighting.direction.x << ',' << lighting.direction.y << ',' << lighting.direction.z
                 << "],\"perspective\":" << (perspective ? "true" : "false") << '}';
        if (!metadata) return false;
    }
    return true;
}

glm::ivec3 pixelAt(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y) {
    const size_t offset = size_t{y} * 256u + x * 4u;
    return {pixels[offset],pixels[offset+1u],pixels[offset+2u]};
}

} // namespace

TEST(MeshPathGPUTest, NamedMechanismsKeepStationaryNodesAndMatchLiveBodyAtLargeSectors) {
    namespace a=game::assets;
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config; config.colorFormat=WGPUTextureFormat_RGBA8Unorm; config.frontFace=WGPUFrontFace_CW;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    const glm::vec3 rootPosition{.15f,.1f,0},center{.4f,-.2f,.25f};
    const auto rootRotation=glm::angleAxis(.2f,glm::vec3(0,0,1));
    const auto principal=glm::angleAxis(.4f,glm::vec3(1,0,0));
    const auto orientation=rootRotation*principal;
    std::array<glm::vec4,4> poses{};
    poses[2]=glm::vec4(rootPosition+rootRotation*center,1);
    poses[3]={orientation.x,orientation.y,orientation.z,orientation.w};
    std::array<glm::uvec4,2> metadata{};
    metadata[1]=glm::uvec4(glm::ivec4(1000000,-2000000,3,0x00100003));
    std::array<glm::uvec4,8> shapes{}; shapes[7]={1,7,1,0};
    std::array<glm::uvec4,39> atlas{};
    atlas[0]={1,1,16,19}; atlas[1]={37,39,1,6};
    atlas[9]={7,1,0,1}; atlas[10]={0,6,0,1};
    const auto bits=[](glm::vec4 v){return glm::uvec4(std::bit_cast<uint32_t>(v.x),std::bit_cast<uint32_t>(v.y),
        std::bit_cast<uint32_t>(v.z),std::bit_cast<uint32_t>(v.w));};
    atlas[11]=bits(glm::vec4(center,1));
    atlas[12]=bits({principal.x,principal.y,principal.z,principal.w});
    atlas[13]=bits({1,1,1,2}); atlas[14]=glm::uvec4(glm::ivec4(-50,-50,-50,0)); atlas[15]={50,50,50,0};
    struct Buffers {std::vector<WGPUBuffer> handles;~Buffers(){for(auto b:handles)wgpuBufferRelease(b);}} buffers;
    const auto upload=[&](const auto& values){
        auto buffer=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::storage(sizeof(values),true,"mechanism_live_pose"));
        if(buffer){buffers.handles.push_back(buffer);(void)gpu::writeBuffer(context.getQueue(),buffer,0,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(values.data()),sizeof(values)));}
        return buffer;
    };
    physics::PhysicsRenderView live;
    live.poseBuffer=upload(poses); live.metadataBuffer=upload(metadata);
    live.shapeBuffer=upload(shapes); live.authoredShapeBuffer=upload(atlas);
    ASSERT_NE(live.authoredShapeBuffer,nullptr);
    physics::WorldPosition worldCamera; worldCamera.sector={1000000,-2000000,3};
    ASSERT_TRUE(path.setAuthoredBodyView(live,worldCamera));
    const auto root=glm::translate(glm::dmat4(1),glm::dvec3(rootPosition))*glm::mat4_cast(glm::dquat(rootRotation));
    PrimitiveLighting light; light.fogDensity=0;
    uint32_t assetIndex=0;
    for(const auto kind:{a::RigidMechanismKind::PropellerRotor,a::RigidMechanismKind::WinchDrum}) {
        const bool propeller=kind==a::RigidMechanismKind::PropellerRotor;
        auto mesh=diagnosticQuad(); mesh.header.flags=0;mesh.materials[0].unlit=1;
        mesh.header.nodeCount=2;mesh.nodes.resize(2);mesh.stringBlob.assign(1,'\0');
        const auto name=[&](std::string_view value){auto offset=static_cast<uint32_t>(mesh.stringBlob.size());
            mesh.stringBlob.append(value);mesh.stringBlob.push_back('\0');return offset;};
        mesh.nodes[0].meshIndex=mesh.nodes[1].meshIndex=0;
        mesh.nodes[0].nameOffset=name("voxys_mechanism_static");
        mesh.nodes[0].translation[propeller?1:2]=.9f;
        mesh.nodes[1].nameOffset=name(propeller?"voxys_propeller_rotor":"voxys_winch_drum");
        mesh.nodes[1].translation[1]=propeller?0:.12f;
        for(uint32_t i=0;i<mesh.header.vertexCount;++i) {
            moto::VmeshVertex v{};std::memcpy(&v,mesh.vertices.data()+i*sizeof(v),sizeof(v));
            const float x=v.position[0],y=v.position[1];
            v.position[0]=propeller?x*.55f:0;v.position[1]=propeller?y*.1f:x*.55f;v.position[2]=propeller?0:y*.1f;
            v.normal[0]=propeller?0:-1;v.normal[1]=0;v.normal[2]=propeller?-1:0;
            std::memcpy(mesh.vertices.data()+i*sizeof(v),&v,sizeof(v));
        }
        a::RigidPrefab prefab;std::string error;
        ASSERT_TRUE(a::prepareRigidPrefab(mesh,{},{},prefab,error))<<error;
        ASSERT_TRUE(path.loadMeshData(mesh));
        const glm::vec3 camera=propeller?glm::vec3(0,0,-3):glm::vec3(-3,0,0);
        const auto draw=[&](double phase,bool body,std::vector<uint8_t>& pixels,uint32_t generation=3){
            std::vector<a::RigidPrefabDraw> records;
            if(!a::placeRigidPrefab(prefab,body?glm::dmat4(1):root,{},2,records,error,a::RigidMechanismPose{kind,phase}))return false;
            path.clearInstances();
            for(const auto& record:records)path.addInstance({.assetIndex=assetIndex,.meshIndex=record.meshIndex,
                .modelMatrix=record.modelMatrix,.tintColor=record.nodeIndex==0?glm::vec4(0,1,0,1):glm::vec4(1,.15f,0,1),
                .physicsBody=body?physics::BodyHandle{1,generation}:physics::BodyHandle{}});
            return drawDiagnosticPixels(path,context,camera,light,pixels);
        };
        std::vector<uint8_t> neutral,rotated,actual,stale;
        ASSERT_TRUE(draw(0,false,neutral));ASSERT_TRUE(draw(std::acos(-1.0)*.5,false,rotated));
        ASSERT_TRUE(draw(std::acos(-1.0)*.5,true,actual));
        size_t changed=0,green=0,greenDifferences=0,liveDifferences=0;
        // ACES mixes color channels: linear (0,1,0) becomes about
        // RGBA8 (148,228,89), so exact zero red/blue cannot identify it.
        // Use the same separated green hue as the texture-quadrant test.
        const auto stationaryPixel=[](const std::vector<uint8_t>& pixels,size_t offset){
            return pixels[offset+1]>pixels[offset]+70 && pixels[offset+1]>pixels[offset+2]+70;
        };
        for(size_t offset=0;offset<rotated.size();offset+=4) {
            const bool staticBefore=stationaryPixel(neutral,offset);
            const bool staticAfter=stationaryPixel(rotated,offset);
            green+=staticBefore;
            if(staticBefore||staticAfter)for(size_t c=0;c<3;++c)greenDifferences+=neutral[offset+c]!=rotated[offset+c];
            for(size_t c=0;c<3;++c){changed+=neutral[offset+c]!=rotated[offset+c];liveDifferences+=actual[offset+c]!=rotated[offset+c];}
        }
        EXPECT_GT(green,20u);EXPECT_EQ(greenDifferences,0u);EXPECT_GT(changed,100u);
        EXPECT_LE(liveDifferences,32u);
        ASSERT_TRUE(draw(std::acos(-1.0)*.5,true,stale,4));
        size_t staleColor=0;for(size_t offset=0;offset<stale.size();offset+=4)
            staleColor+=stale[offset]!=0||stale[offset+1]!=0||stale[offset+2]!=0;
        EXPECT_EQ(staleColor,0u);
        std::printf("Mechanism %s stationaryPixels=%zu stationaryChanged=%zu movingChanged=%zu liveDifference=%zu stalePixels=%zu\n",
            propeller?"rotor":"drum",green,greenDifferences,changed,liveDifferences,staleColor);
        ++assetIndex;
    }
    EXPECT_EQ(MeshPath::gpuInstanceBytes,128u);
    path.shutdown();
}

TEST(MeshPathGPUTest, AuthoredPoseRendersRootFrameAndRejectsStaleBodyGeneration) {
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config; config.colorFormat=WGPUTextureFormat_RGBA8Unorm; config.frontFace=WGPUFrontFace_CW;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    auto data=diagnosticQuad(); data.materials[0].unlit=1;
    ASSERT_TRUE(path.loadMeshData(data));
    const glm::vec3 rootPosition{.3f,.1f,0}, center{.4f,-.2f,.25f};
    const auto rootRotation=glm::angleAxis(.3f,glm::vec3(0,0,1));
    const auto principal=glm::angleAxis(.4f,glm::vec3(1,0,0));
    const auto orientation=rootRotation*principal;
    std::array<glm::vec4,4> poses{};
    poses[2]=glm::vec4(rootPosition+rootRotation*center,1);
    poses[3]={orientation.x,orientation.y,orientation.z,orientation.w};
    std::array<glm::uvec4,2> metadata{}; metadata[1]={0,0,0,0x00100003u};
    std::array<glm::uvec4,8> shapes{}; shapes[7]={1,7,1,0};
    std::array<glm::uvec4,39> atlas{};
    atlas[0]={1,1,16,19}; atlas[1]={37,39,1,6};
    atlas[9]={7,1,0,1}; atlas[10]={0,6,0,1};
    const auto bits=[](glm::vec4 v){return glm::uvec4(std::bit_cast<uint32_t>(v.x),std::bit_cast<uint32_t>(v.y),
        std::bit_cast<uint32_t>(v.z),std::bit_cast<uint32_t>(v.w));};
    atlas[11]=bits(glm::vec4(center,1));
    atlas[12]=bits({principal.x,principal.y,principal.z,principal.w});
    atlas[13]=bits({1,1,1,2}); atlas[14]=glm::uvec4(glm::ivec4(-50,-50,-50,0)); atlas[15]={50,50,50,0};
    struct Buffers { std::vector<WGPUBuffer> handles; ~Buffers(){for(auto b:handles)wgpuBufferRelease(b);} } buffers;
    const auto upload=[&](const auto& values){
        auto buffer=gpu::createBuffer(context.getDevice(),gpu::BufferDesc::storage(sizeof(values),true,"live_mesh_test"));
        if(buffer) { buffers.handles.push_back(buffer); (void)gpu::writeBuffer(context.getQueue(),buffer,0,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(values.data()),sizeof(values))); }
        return buffer;
    };
    physics::PhysicsRenderView bodyView;
    bodyView.poseBuffer=upload(poses); bodyView.metadataBuffer=upload(metadata);
    bodyView.shapeBuffer=upload(shapes); bodyView.authoredShapeBuffer=upload(atlas);
    ASSERT_NE(bodyView.authoredShapeBuffer,nullptr);
    PrimitiveLighting lighting; lighting.fogDensity=0;
    std::vector<uint8_t> reference,actual;
    path.addInstance({.modelMatrix=glm::translate(glm::mat4(1),rootPosition)*glm::mat4_cast(rootRotation)});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,reference));
    ASSERT_TRUE(path.setAuthoredBodyView(bodyView,{}));
    path.clearInstances(); path.addInstance({.physicsBody={1,3}});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,actual));
    size_t differences=0; for(size_t i=0;i<actual.size();++i) differences+=actual[i]!=reference[i];
    EXPECT_LE(differences,32u); EXPECT_GT(pixelAt(actual,32,32).r,10);
    // The appended paint vector must not overwrite the live body identity.
    // Compare a rotated, COM-offset GPU root with its static matrix, and
    // require an actual color change so two unpainted draws cannot pass.
    const auto paint = opaqueSrgbPaintOverride({48,112,224,0});
    const glm::vec4 selection{0.5f,0.8f,1.0f,1.0f};
    path.clearInstances();
    path.addInstance({.modelMatrix=glm::translate(glm::mat4(1),rootPosition)*glm::mat4_cast(rootRotation),
        .tintColor=selection,.baseColorOverride=paint});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,reference));
    const auto painted = pixelAt(reference,32,32);
    EXPECT_GT(painted.b, painted.r + 40);
    path.clearInstances();
    path.addInstance({.tintColor=selection,.physicsBody={1,3},.baseColorOverride=paint});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,actual));
    differences=0; for(size_t i=0;i<actual.size();++i) differences+=actual[i]!=reference[i];
    EXPECT_LE(differences,32u);
    for (int channel=0; channel<3; ++channel)
        EXPECT_NEAR(pixelAt(actual,32,32)[channel],painted[channel],1);
    std::printf("Paint live root static=%d,%d,%d live=%d,%d,%d differingBytes=%zu\n",
        painted.r,painted.g,painted.b,pixelAt(actual,32,32).r,pixelAt(actual,32,32).g,
        pixelAt(actual,32,32).b,differences);
    auto coatedData=data;coatedData.materials[0].unlit=0;coatedData.materials[0].roughnessFactor=.42f;
    ASSERT_TRUE(path.loadMeshData(coatedData));
    lighting.direction={.1f,.2f,-1};lighting.sunIntensity=.6f;
    const glm::vec4 acceptedSurface{.75f,.3f,1,.5f};
    path.clearInstances();path.addInstance({.assetIndex=1,
        .modelMatrix=glm::translate(glm::mat4(1),rootPosition)*glm::mat4_cast(rootRotation),
        .tintColor=selection,.baseColorOverride=paint,.surface=acceptedSurface});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,reference));
    path.clearInstances();path.addInstance({.assetIndex=1,.tintColor=selection,.physicsBody={1,3},
        .baseColorOverride=paint,.surface=acceptedSurface});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,actual));
    differences=0;for(size_t i=0;i<actual.size();++i)differences+=actual[i]!=reference[i];
    EXPECT_LE(differences,32u);EXPECT_GT(pixelAt(actual,32,32).b,10);
    RecordProperty("surfaceLiveStaticDifferingBytes",std::to_string(differences));
    poses[2].x+=4;
    ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),bodyView.poseBuffer,0,std::span<const glm::vec4>(poses)));
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,actual));
    EXPECT_EQ(pixelAt(actual,32,32).r,0);
    poses[2].x-=4;
    ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),bodyView.poseBuffer,0,std::span<const glm::vec4>(poses)));
    path.clearInstances(); path.addInstance({.physicsBody={1,4},.baseColorOverride=paint});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,actual));
    EXPECT_EQ(pixelAt(actual,32,32).r,0);
    path.shutdown();
}

TEST(MeshPathGPUTest, LiveSunShadowUpdatesOffscreenCastersAndRejectsStalePoses) {
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    config.frontFace = WGPUFrontFace_CW; config.sunShadows = true;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    auto surface = diagnosticQuad(); surface.materials[0].roughnessFactor = 1;
    ASSERT_TRUE(path.loadMeshData(surface));
    auto cutout = surface; cutout.materials[0].alphaMode = moto::VmeshAlphaMask;
    cutout.materials[0].baseColorFactor[3] = 0;
    ASSERT_TRUE(path.loadMeshData(cutout));

    std::array<glm::vec4,4> poses{};
    // Caster is entirely outside the camera frustum, along the sun ray from
    // the receiver's centre. Its shadow must still fall on that visible point.
    poses[2] = {2,0,-2,1}; poses[3] = {0,0,0,1};
    std::array<glm::uvec4,2> metadata{}; metadata[1] = {0,0,0,0x00100003u};
    std::array<glm::uvec4,8> shapes{}; shapes[7] = {1,7,1,0};
    std::array<glm::uvec4,39> atlas{};
    atlas[0] = {1,1,16,19}; atlas[1] = {37,39,1,6};
    atlas[9] = {7,1,0,1}; atlas[10] = {0,6,0,1};
    const auto bits=[](glm::vec4 v) { return glm::uvec4(std::bit_cast<uint32_t>(v.x),
        std::bit_cast<uint32_t>(v.y),std::bit_cast<uint32_t>(v.z),std::bit_cast<uint32_t>(v.w)); };
    atlas[11] = bits({0,0,0,1}); atlas[12] = bits({0,0,0,1}); atlas[13] = bits({1,1,1,2});
    atlas[14] = glm::uvec4(glm::ivec4(-50,-50,-50,0)); atlas[15] = {50,50,50,0};
    struct Buffers { std::vector<WGPUBuffer> values; ~Buffers(){for(auto b:values) wgpuBufferRelease(b);} } buffers;
    const auto upload=[&](const auto& values) {
        auto buffer = gpu::createBuffer(context.getDevice(),gpu::BufferDesc::storage(sizeof(values),true));
        if (buffer) {
            buffers.values.push_back(buffer);
            const auto bytes = std::as_bytes(std::span(values));
            if (!gpu::writeBuffer(context.getQueue(),buffer,0,
                std::span<const std::byte>(bytes.data(),bytes.size()))) return WGPUBuffer{};
        }
        return buffer;
    };
    physics::PhysicsRenderView live;
    live.poseBuffer = upload(poses); live.metadataBuffer = upload(metadata);
    live.shapeBuffer = upload(shapes); live.authoredShapeBuffer = upload(atlas);
    ASSERT_TRUE(path.setAuthoredBodyView(live,{}));
    PrimitiveLighting light; light.direction = {1,0,-1}; light.sunIntensity = 2;
    light.ambientIntensity = 0.08f; light.fogDensity = 0;
    std::vector<uint8_t> pixels;
    const auto sample=[&](uint32_t generation, uint32_t asset, glm::vec4 paint=glm::vec4(0)) {
        path.clearInstances(); path.addInstance({});
        if (generation >= 42) path.addInstance({.assetIndex=asset,
            .modelMatrix=glm::translate(glm::mat4(1),glm::vec3(2,0,-2))*glm::scale(glm::mat4(1),glm::vec3(.3f)),
            .castsSunShadow=generation==42,.baseColorOverride=paint});
        else if (generation) path.addInstance({.assetIndex=asset,
            .modelMatrix=glm::scale(glm::mat4(1),glm::vec3(.3f)),.physicsBody={1,generation},
            .baseColorOverride=paint});
        if (!drawDiagnosticPixels(path,context,{0,0,-3},light,pixels)) return -1;
        return pixelAt(pixels,32,32).r;
    };
    const int clear = sample(0,0);
    const int staticShadow = sample(42,0);
    const int shadowed = sample(3,0);
    std::printf("Sun receiver clear=%d static=%d live=%d\n",clear,staticShadow,shadowed);
    EXPECT_NEAR(staticShadow,shadowed,2);
    EXPECT_NEAR(sample(43,0),clear,2); // Palette thumbnails cannot shadow the game.
    ASSERT_GT(clear,100); ASSERT_GT(shadowed,0); EXPECT_LT(shadowed,clear-40);
    // GPU buffer update only: same camera, matrices, instances and static cache.
    poses[2].x += 2;
    ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),live.poseBuffer,0,std::span<const glm::vec4>(poses)));
    EXPECT_NEAR(sample(3,0),clear,2);
    poses[2].x -= 2;
    ASSERT_TRUE(gpu::writeBuffer(context.getQueue(),live.poseBuffer,0,std::span<const glm::vec4>(poses)));
    EXPECT_NEAR(sample(3,0),shadowed,2);
    EXPECT_NEAR(sample(4,0),clear,2); // Recycled/stale body cannot leave a shadow.
    EXPECT_NEAR(sample(3,1),clear,2); // Masked holes cannot cast solid silhouettes.
    const auto paint = opaqueSrgbPaintOverride({48,112,224,0});
    EXPECT_NEAR(sample(42,0,paint),staticShadow,2); // Paint alpha never removes an opaque caster.
    EXPECT_NEAR(sample(3,1,paint),clear,2); // Paint never fills authored alpha-mask holes.
    // Sun occlusion never darkens skylight or emits a stale shadow next frame.
    light.sunIntensity = 0;
    const int ambient = sample(0,0);
    EXPECT_NEAR(sample(3,0),ambient,2);
    std::printf("Live sun: clear %d, shadow %d, ambient %d\n",clear,shadowed,ambient);
}

TEST(MeshPathGPUTest, ExceptionalReleasePreservesEncodedTextureDependencies) {
    DiagnosticContext context;
    if (!context.initHeadless()) GTEST_SKIP() << "GPU context not available";
    MeshPathConfig config;
    config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    config.frontFace = WGPUFrontFace_CW;
    MeshPath path;
    ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    auto data = diagnosticQuad(); data.materials[0].unlit = 1;
    setDiagnosticTexture(data, moto::VmeshTextureBaseColor,
        {{240,8,8,255, 8,240,8,255, 8,8,240,255, 240,240,8,255}});
    ASSERT_TRUE(path.loadMeshData(data));
    path.addInstance({.assetIndex=0,.meshIndex=0});
    PrimitiveLighting lighting; lighting.fogDensity = 0;
    std::vector<uint8_t> pixels;
    // This releases every external MeshPath ref after encoding but BEFORE
    // submission. Explicit texture Destroy here would invalidate the command.
    ASSERT_TRUE(drawDiagnosticPixels(path, context, {0,0,-3}, lighting, pixels, true, true));
    EXPECT_FALSE(path.isInitialized());
    EXPECT_EQ(path.assetCount(), 0u);
    const auto red = pixelAt(pixels,22,22);
    const auto blue = pixelAt(pixels,22,42);
    EXPECT_GT(red.r, red.g + 70); EXPECT_GT(red.r, red.b + 70);
    EXPECT_GT(blue.b, blue.r + 70); EXPECT_GT(blue.b, blue.g + 70);
    path.releaseHandles(); // Idempotent exceptional and ordinary teardown.
    path.shutdown();
}

TEST(MeshPathGPUTest, SamplesActualTextureQuadrantsAndRejectsSingleSidedBackFaces) {
    DiagnosticContext context;
    if (!context.initHeadless()) GTEST_SKIP() << "GPU context not available";
    MeshPathConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm; config.frontFace = WGPUFrontFace_CW;
    MeshPath invalidPath;
    auto invalidConfig = config; invalidConfig.frontFace = WGPUFrontFace_Force32;
    EXPECT_FALSE(invalidPath.init(context.getDevice(), context.getQueue(), invalidConfig));
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    auto data = diagnosticQuad(); data.materials[0].unlit = 1;
    setDiagnosticTexture(data, moto::VmeshTextureBaseColor,
        {{240,8,8,255, 8,240,8,255, 8,8,240,255, 240,240,8,255}});
    auto malformed = data; malformed.images.pop_back();
    EXPECT_FALSE(path.loadMeshData(malformed)); EXPECT_EQ(path.assetCount(), 0u);
    malformed = data; malformed.header.vertexCount = 0x20000004u; // 72-byte multiplication wraps in 32-bit size_t.
    EXPECT_FALSE(path.loadMeshData(malformed)); EXPECT_EQ(path.assetCount(), 0u);
    malformed = data; malformed.header.meshCount = UINT32_MAX;
    EXPECT_FALSE(path.loadMeshData(malformed)); EXPECT_EQ(path.assetCount(), 0u);
    malformed = data; malformed.header.materialCount = 2;
    EXPECT_FALSE(path.loadMeshData(malformed)); EXPECT_EQ(path.assetCount(), 0u);
    ASSERT_TRUE(path.loadMeshData(data));
    path.addInstance({.assetIndex=0,.meshIndex=0});
    PrimitiveLighting lighting; lighting.fogDensity = 0; lighting.exposure = 1;
    std::vector<uint8_t> pixels;
    const auto declaredView = glm::lookAt(glm::vec3(0,0,-3),glm::vec3(0),glm::vec3(0,1,0));
    EXPECT_FLOAT_EQ(declaredView[0][0], 1.0f); // Independent screen-right oracle for the actual LH camera.
    EXPECT_FLOAT_EQ(declaredView[1][1], 1.0f);
    for (bool perspective : {false,true}) {
        ASSERT_TRUE(drawDiagnosticPixels(path, context, {0,0,-3}, lighting, pixels, perspective));
        EXPECT_EQ(path.lastSubmittedDrawCount(), 1u);
        const auto red = pixelAt(pixels,22,22); const auto green = pixelAt(pixels,42,22);
        const auto blue = pixelAt(pixels,22,42); const auto yellow = pixelAt(pixels,42,42);
        EXPECT_GT(red.r, red.g + 70); EXPECT_GT(red.r, red.b + 70);
        EXPECT_GT(green.g, green.r + 70); EXPECT_GT(green.g, green.b + 70);
        EXPECT_GT(blue.b, blue.r + 70); EXPECT_GT(blue.b, blue.g + 70);
        EXPECT_GT(yellow.r, yellow.b + 70); EXPECT_GT(yellow.g, yellow.b + 70);
    }
    ASSERT_TRUE(drawDiagnosticPixels(path, context, {0,0,3}, lighting, pixels, true));
    EXPECT_EQ(pixelAt(pixels,32,32), glm::ivec3(0));
    path.clearInstances();
    ASSERT_TRUE(drawDiagnosticPixels(path, context, {0,0,-3}, lighting, pixels, true));
    EXPECT_EQ(path.lastSubmittedDrawCount(), 0u);
    EXPECT_EQ(pixelAt(pixels,32,32), glm::ivec3(0));
}

TEST(MeshPathGPUTest, PaintReplacesAuthoredRgbBeforeTintAndPreservesTexturesAndOpacity) {
    DiagnosticContext context;
    ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    config.frontFace = WGPUFrontFace_CW;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    PrimitiveLighting lighting; lighting.fogDensity = 0; lighting.exposure = 1;
    lighting.direction = {0.2f,0.1f,-1}; lighting.sunIntensity = 1.5f;
    lighting.ambientIntensity = 0.15f;
    const auto paint = opaqueSrgbPaintOverride({52,128,204,0});
    const glm::vec4 selection{0.6f,1.0f,0.4f,0.75f};
    const auto draw = [&](uint32_t asset, glm::vec4 overrideColor, std::vector<uint8_t>& pixels) {
        path.clearInstances();
        path.addInstance({.assetIndex=asset,.tintColor=selection,.baseColorOverride=overrideColor});
        return drawDiagnosticPixels(path,context,{0,0,-3},lighting,pixels);
    };
    for (bool unlit : {true,false}) {
        for (auto alphaMode : {moto::VmeshAlphaOpaque,moto::VmeshAlphaMask,moto::VmeshAlphaBlend}) {
            SCOPED_TRACE(::testing::Message() << "unlit=" << unlit << " alphaMode=" << int(alphaMode));
            auto data = diagnosticQuad();
            auto& material = data.materials[0];
            material.unlit = unlit;
            material.alphaMode = alphaMode;
            material.alphaCutoff = 0.5f;
            material.baseColorFactor[0] = 0.8f; material.baseColorFactor[1] = 0.12f;
            material.baseColorFactor[2] = 0.02f; material.baseColorFactor[3] = 0.8f;
            material.metallicFactor = 0.72f; material.roughnessFactor = 0.37f;
            material.emissiveFactor[0] = 0.013f; material.emissiveFactor[1] = 0.007f;
            material.emissiveFactor[2] = 0.002f;
            setDiagnosticTexture(data,moto::VmeshTextureBaseColor,
                {{255,128,64,255, 128,255,96,255, 64,240,255,20, 224,64,208,255}});
            setDiagnosticTexture(data,moto::VmeshTextureMetallicRoughness,
                {{0,100,224,255, 0,200,64,255, 0,128,192,255, 0,96,240,255}});
            const auto authoredIndex = static_cast<uint32_t>(path.assetCount());
            ASSERT_TRUE(path.loadMeshData(data));
            // Independent render oracle: bake only the desired linear RGB into
            // a material. All other authored fields/textures remain identical.
            auto expectedData = data;
            for (int channel=0; channel<3; ++channel)
                expectedData.materials[0].baseColorFactor[channel] = paint[channel];
            const auto expectedIndex = static_cast<uint32_t>(path.assetCount());
            ASSERT_TRUE(path.loadMeshData(expectedData));
            std::vector<uint8_t> original, disabled, painted, expected;
            ASSERT_TRUE(draw(authoredIndex,glm::vec4(0),original));
            ASSERT_TRUE(draw(authoredIndex,{0.4f,0.6f,0.8f,0},disabled));
            ASSERT_TRUE(draw(authoredIndex,paint,painted));
            ASSERT_TRUE(draw(expectedIndex,glm::vec4(0),expected));
            size_t disabledDifferences=0, paintedDifferences=0, changedBytes=0;
            for (size_t i=0; i<painted.size(); ++i) {
                disabledDifferences += original[i] != disabled[i];
                paintedDifferences += painted[i] != expected[i];
                changedBytes += original[i] != painted[i];
            }
            EXPECT_EQ(disabledDifferences,0u); // Disabled payload is byte-exact legacy.
            EXPECT_EQ(paintedDifferences,0u); // Tint, PBR fields and alpha are unchanged.
            EXPECT_GT(changedBytes,500u); // Cannot pass by ignoring paint or drawing nothing.
            if (alphaMode == moto::VmeshAlphaMask) {
                EXPECT_EQ(pixelAt(painted,22,42),glm::ivec3(0));
                EXPECT_GT(pixelAt(painted,22,22).g,10);
            }
            std::printf("Paint material unlit=%d alpha=%u center=%d,%d,%d changed=%zu mismatches=%zu\n",
                int(unlit),unsigned(alphaMode),pixelAt(painted,32,32).r,pixelAt(painted,32,32).g,
                pixelAt(painted,32,32).b,changedBytes,paintedDifferences);
        }
    }
}

TEST(MeshPathGPUTest, CoveSurfaceLayersWetReflectionWithoutChangingLegacyAlphaOrMetalHue) {
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config;config.colorFormat=WGPUTextureFormat_RGBA8Unorm;config.frontFace=WGPUFrontFace_CW;
    MeshPath path;ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    PrimitiveLighting light;light.fogDensity=0;light.ambientIntensity=0;light.sunIntensity=.3f;
    light.sunColor={1,1,1};light.exposure=1;
    const auto draw=[&](uint32_t asset,glm::vec4 surface,glm::vec3 direction,std::vector<uint8_t>& pixels) {
        light.direction=direction;path.clearInstances();
        path.addInstance({.assetIndex=asset,.surface=surface});
        return drawDiagnosticPixels(path,context,{0,0,-3},light,pixels);
    };
    for(float metallic:{0.f,1.f}) {
        auto data=diagnosticQuad();auto& material=data.materials[0];
        material.baseColorFactor[0]=.45f;material.baseColorFactor[1]=.18f;material.baseColorFactor[2]=.035f;
        material.metallicFactor=metallic;material.roughnessFactor=.45f;
        const auto asset=static_cast<uint32_t>(path.assetCount());ASSERT_TRUE(path.loadMeshData(data));
        std::vector<uint8_t> legacy,disabled,dry,wet,partial,damaged,offDry,offWet,immersed,halfImmersed;
        ASSERT_TRUE(draw(asset,{0,0,0,0},{0,0,-1},legacy));
        EXPECT_FALSE(draw(asset,{1,1,0,0},{0,0,-1},disabled));
        EXPECT_FALSE(draw(asset,{0,0,0,1},{0,0,-1},disabled));
        ASSERT_TRUE(draw(asset,{0,0,1,0},{0,0,-1},dry));
        ASSERT_TRUE(draw(asset,{1,0,1,0},{0,0,-1},wet));
        ASSERT_TRUE(draw(asset,{.5f,0,1,0},{0,0,-1},partial));
        ASSERT_TRUE(draw(asset,{0,1,1,0},{0,0,-1},damaged));
        ASSERT_TRUE(draw(asset,{0,0,1,0},{.8f,.3f,-1},offDry));
        ASSERT_TRUE(draw(asset,{1,0,1,0},{.8f,.3f,-1},offWet));
        ASSERT_TRUE(draw(asset,{1,0,1,1},{0,0,-1},immersed));
        ASSERT_TRUE(draw(asset,{1,0,1,.5f},{0,0,-1},halfImmersed));
        const auto centerDry=pixelAt(dry,32,32),centerWet=pixelAt(wet,32,32);
        EXPECT_GT(centerWet.r,centerDry.r+10); // Film adds a narrower actual light reflection.
        EXPECT_LT(pixelAt(offWet,32,32).r,pixelAt(offDry,32,32).r); // Not uniform brightening.
        EXPECT_GT(pixelAt(offWet,32,32).r,pixelAt(offWet,32,32).b+5); // Substrate hue outside the white film highlight.
        EXPECT_LT(pixelAt(damaged,32,32).r,centerDry.r); // Abrasion spreads the highlight.
        // A submerged material retains the water/substrate interface, without
        // duplicating the ocean's bright air/water film at the same highlight.
        EXPECT_LT(pixelAt(immersed,32,32).r,centerWet.r-10);
        EXPECT_GT(pixelAt(immersed,32,32).r,pixelAt(immersed,32,32).b+5);
        if(metallic==1) { EXPECT_EQ(immersed,dry); } // No arbitrary conductor darkening.
        size_t changed=0,opaque=0;
        for(size_t offset=0;offset<wet.size();offset+=4) {
            EXPECT_EQ(wet[offset+3],dry[offset+3]);
            EXPECT_EQ(immersed[offset+3],dry[offset+3]);
            for(size_t c=0;c<3;++c) {
                changed+=wet[offset+c]!=dry[offset+c];
                EXPECT_GE(int(partial[offset+c]),std::min(int(wet[offset+c]),int(dry[offset+c]))-1);
                EXPECT_LE(int(partial[offset+c]),std::max(int(wet[offset+c]),int(dry[offset+c]))+1);
                EXPECT_GE(int(halfImmersed[offset+c]),std::min(int(wet[offset+c]),int(immersed[offset+c]))-1);
                EXPECT_LE(int(halfImmersed[offset+c]),std::max(int(wet[offset+c]),int(immersed[offset+c]))+1);
            }
            opaque+=wet[offset]!=0||wet[offset+1]!=0||wet[offset+2]!=0;
        }
        EXPECT_GT(changed,500u);EXPECT_GT(opaque,500u);
        RecordProperty(metallic==0 ? "plasticWetChangedBytes" : "metalWetChangedBytes",std::to_string(changed));
        RecordProperty(metallic==0 ? "plasticWetPeak" : "metalWetPeak",centerWet.r);
        RecordProperty(metallic==0 ? "plasticImmersedPeak" : "metalImmersedPeak",pixelAt(immersed,32,32).r);
    }
    // Explicit opt-in never overrides unlit semantics, cutouts, or authored alpha.
    auto mask=diagnosticQuad();mask.materials[0].unlit=1;mask.materials[0].alphaMode=moto::VmeshAlphaMask;
    setDiagnosticTexture(mask,moto::VmeshTextureBaseColor,
        {{255,128,64,255,128,255,96,255,64,240,255,0,224,64,208,255}});
    const auto asset=static_cast<uint32_t>(path.assetCount());ASSERT_TRUE(path.loadMeshData(mask));
    std::vector<uint8_t> original,coated;ASSERT_TRUE(draw(asset,{0,0,0,0},{0,0,-1},original));
    ASSERT_TRUE(draw(asset,{1,1,1,1},{0,0,-1},coated));EXPECT_EQ(original,coated);
    EXPECT_EQ(pixelAt(coated,22,42),glm::ivec3(0));EXPECT_GT(pixelAt(coated,22,22).r,10);
}

TEST(MeshPathGPUTest, CoveNormalVarianceBroadensSubpixelSpecularHighlights) {
    DiagnosticContext context;ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config;config.colorFormat=WGPUTextureFormat_RGBA8Unorm;config.frontFace=WGPUFrontFace_CW;
    MeshPath path;ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    auto data=diagnosticQuad();data.materials[0].roughnessFactor=.045f;data.materials[0].metallicFactor=1;
    for(size_t i=0;i<4;++i) {
        moto::VmeshVertex v{};std::memcpy(&v,data.vertices.data()+i*sizeof(v),sizeof(v));
        const auto n=glm::normalize(glm::vec3(v.position[0]*.6f,v.position[1]*.6f,-1));
        const auto t=glm::normalize(glm::vec3(1,0,n.x/-n.z));
        for(int c=0;c<3;++c){v.normal[c]=n[c];v.tangent[c]=t[c];}
        std::memcpy(data.vertices.data()+i*sizeof(v),&v,sizeof(v));
    }
    ASSERT_TRUE(path.loadMeshData(data));
    PrimitiveLighting light;light.direction={0,0,-1};light.fogDensity=0;light.ambientIntensity=0;
    light.sunIntensity=.025f;light.sunColor={1,1,1};light.exposure=1;
    std::vector<uint8_t> legacy,filtered;path.addInstance({});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},light,legacy));
    path.clearInstances();path.addInstance({.surface={0,0,1,0}});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},light,filtered));
    size_t legacyFootprint=0,filteredFootprint=0,changed=0;
    for(size_t i=0;i<legacy.size();i+=4) {
        legacyFootprint+=legacy[i]>20;filteredFootprint+=filtered[i]>20;changed+=legacy[i]!=filtered[i];
        EXPECT_EQ(legacy[i+3],filtered[i+3]);
    }
    EXPECT_GT(filteredFootprint,legacyFootprint);EXPECT_GT(changed,20u);
    EXPECT_GT(pixelAt(filtered,32,32).r,20);
    EXPECT_LT(filteredFootprint,200u);EXPECT_LT(pixelAt(filtered,20,20).r,10); // A localized broadened lobe, not uniform brightness.
    RecordProperty("unfilteredHighlightPixels",std::to_string(legacyFootprint));RecordProperty("filteredHighlightPixels",std::to_string(filteredFootprint));
}

TEST(MeshPathGPUTest, DirectionalNormalMapUsesFullInverseTransposeAndTangentFrame) {
    DiagnosticContext context;
    if (!context.initHeadless()) GTEST_SKIP() << "GPU context not available";
    MeshPathConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm; config.frontFace = WGPUFrontFace_CW;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    auto data = diagnosticQuad(true);
    data.materials[0].roughnessFactor = 1;
    for (size_t i = 0; i < 3; ++i) data.materials[0].baseColorFactor[i] = 0.5f;
    // +tangent tilt; the blue/green channels are linear normal data.
    setDiagnosticTexture(data, moto::VmeshTextureNormal,
        {{218,128,218,255, 218,128,218,255, 218,128,218,255, 218,128,218,255}});
    ASSERT_TRUE(path.loadMeshData(data));
    const auto model = glm::scale(glm::mat4(1.0f), glm::vec3(2,1,0.5f));
    path.addInstance({.assetIndex=0,.meshIndex=0,.modelMatrix=model});
    // Independent normal: inverse transpose of diag(2,1,.5) applied to (1,0,-1).
    const auto normal = glm::normalize(glm::vec3(0.5f,0,-2));
    const auto tangent = glm::normalize(glm::vec3(2,0,0.5f));
    PrimitiveLighting lighting; lighting.fogDensity = 0; lighting.exposure = 1;
    lighting.ambientIntensity = 0; lighting.sunIntensity = 1;
    lighting.direction = glm::normalize(normal + tangent);
    std::vector<uint8_t> bright;
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, bright, true));
    EXPECT_EQ(path.lastSubmittedDrawCount(), 1u);
    lighting.direction = glm::normalize(normal - tangent);
    std::vector<uint8_t> dark;
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, dark, true));
    const auto brightPixel = pixelAt(bright,32,32); const auto darkPixel = pixelAt(dark,32,32);
    EXPECT_GT(brightPixel.r, darkPixel.r + 60);
    EXPECT_GT(brightPixel.g, darkPixel.g + 60);
    EXPECT_GT(brightPixel.b, darkPixel.b + 60);

    // A wrong model*normal transform can still pass a coarse light contrast.
    // The exact tangent is perpendicular to inverse-transpose(normal), while
    // it has a large positive dot against that incorrect transformed normal.
    auto flat = diagnosticQuad(true);
    flat.materials[0] = data.materials[0];
    flat.materials[0].hasTexture[moto::VmeshTextureNormal] = 0;
    ASSERT_TRUE(path.loadMeshData(flat));
    path.clearInstances(); path.addInstance({.assetIndex=1,.meshIndex=0,.modelMatrix=model});
    lighting.direction = tangent;
    std::vector<uint8_t> perpendicular;
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, perpendicular, true));
    const auto perpendicularPixel = pixelAt(perpendicular,32,32);
    EXPECT_LT(perpendicularPixel.r, 8); EXPECT_LT(perpendicularPixel.g, 8); EXPECT_LT(perpendicularPixel.b, 8);
    lighting.direction = normal;
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, bright, true));
    EXPECT_GT(pixelAt(bright,32,32).r, 100);

    // A +bitangent normal-map tilt must reverse when authored tangent.w flips.
    // This tests the sign through real texture sampling and fragment lighting.
    auto signedFrame = data;
    for (size_t i = 0; i < signedFrame.images.size(); i += 4u) {
        signedFrame.images[i] = 128; signedFrame.images[i+1u] = 218;
    }
    ASSERT_TRUE(path.loadMeshData(signedFrame));
    for (uint32_t i = 0; i < signedFrame.header.vertexCount; ++i) {
        moto::VmeshVertex vertex{};
        const size_t offset = static_cast<size_t>(i) * sizeof(vertex);
        std::memcpy(&vertex, signedFrame.vertices.data() + offset, sizeof(vertex));
        vertex.tangent[3] = -1;
        std::memcpy(signedFrame.vertices.data() + offset, &vertex, sizeof(vertex));
    }
    ASSERT_TRUE(path.loadMeshData(signedFrame));
    lighting.direction = glm::normalize(normal + glm::vec3(0,-1,0));
    path.clearInstances(); path.addInstance({.assetIndex=2,.meshIndex=0,.modelMatrix=model});
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, bright, true));
    path.clearInstances(); path.addInstance({.assetIndex=3,.meshIndex=0,.modelMatrix=model});
    ASSERT_TRUE(drawDiagnosticPixels(path, context, normal * 3.0f, lighting, dark, true));
    EXPECT_GT(pixelAt(bright,32,32).r, pixelAt(dark,32,32).r + 60);
    EXPECT_GT(pixelAt(bright,32,32).g, pixelAt(dark,32,32).g + 60);
    EXPECT_GT(pixelAt(bright,32,32).b, pixelAt(dark,32,32).b + 60);
}

TEST(MeshPathGPUTest, PreservesColorSpaceAndPackedMaterialChannels) {
    DiagnosticContext context;
    if (!context.initHeadless()) GTEST_SKIP() << "GPU context not available";
    MeshPathConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm; config.frontFace = WGPUFrontFace_CW;
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    PrimitiveLighting lighting; lighting.fogDensity = 0; lighting.exposure = 1;
    lighting.ambientIntensity = 0; lighting.sunIntensity = 0.05f; lighting.direction = {0,0,-1};
    const auto uniformTexture = [](uint8_t red, uint8_t green, uint8_t blue) {
        return std::array<uint8_t,16>{{red,green,blue,255,red,green,blue,255,
                                      red,green,blue,255,red,green,blue,255}};
    };
    const auto draw = [&](const moto::VmeshData& data, glm::ivec3& sample) {
        if (path.assetCount() > UINT32_MAX) return false;
        const uint32_t index = static_cast<uint32_t>(path.assetCount());
        if (!path.loadMeshData(data)) return false;
        path.clearInstances(); path.addInstance({.assetIndex=index,.meshIndex=0});
        std::vector<uint8_t> pixels;
        if (!drawDiagnosticPixels(path, context, {0,0,-3}, lighting, pixels, true)) return false;
        sample = pixelAt(pixels,32,32);
        return true;
    };
    const auto nearEqual = [](glm::ivec3 first, glm::ivec3 second) {
        for (int channel = 0; channel < 3; ++channel) EXPECT_NEAR(first[channel], second[channel], 2);
    };
    auto encodedColor = diagnosticQuad(); encodedColor.materials[0].unlit = 1;
    setDiagnosticTexture(encodedColor, moto::VmeshTextureBaseColor, uniformTexture(128,128,128));
    auto linearFactor = diagnosticQuad(); linearFactor.materials[0].unlit = 1;
    // IEC sRGB code 128 corresponds to this linear value. Equal appearances
    // catch missing/double decoding without duplicating the tone mapper.
    for (size_t channel = 0; channel < 3; ++channel) linearFactor.materials[0].baseColorFactor[channel] = 0.2158605f;
    glm::ivec3 colorTexturePixel, colorFactorPixel;
    ASSERT_TRUE(draw(encodedColor,colorTexturePixel)); ASSERT_TRUE(draw(linearFactor,colorFactorPixel));
    nearEqual(colorTexturePixel,colorFactorPixel);
    EXPECT_GT(colorTexturePixel.r,100); EXPECT_LT(colorTexturePixel.r,230);

    auto packed = diagnosticQuad();
    for (size_t channel = 0; channel < 3; ++channel) packed.materials[0].baseColorFactor[channel] = 0.3f;
    packed.materials[0].metallicFactor = 1; packed.materials[0].roughnessFactor = 1;
    auto factors = packed;
    factors.materials[0].roughnessFactor = 64.0f / 255.0f;
    factors.materials[0].metallicFactor = 192.0f / 255.0f;
    auto unusedRed = packed, rough = packed, dielectric = packed;
    setDiagnosticTexture(packed,moto::VmeshTextureMetallicRoughness,uniformTexture(0,64,192));
    setDiagnosticTexture(unusedRed,moto::VmeshTextureMetallicRoughness,uniformTexture(255,64,192));
    setDiagnosticTexture(rough,moto::VmeshTextureMetallicRoughness,uniformTexture(0,224,192));
    setDiagnosticTexture(dielectric,moto::VmeshTextureMetallicRoughness,uniformTexture(0,64,0));
    glm::ivec3 packedPixel, factorPixel, redPixel, roughPixel, dielectricPixel;
    ASSERT_TRUE(draw(packed,packedPixel)); ASSERT_TRUE(draw(factors,factorPixel));
    ASSERT_TRUE(draw(unusedRed,redPixel)); ASSERT_TRUE(draw(rough,roughPixel));
    ASSERT_TRUE(draw(dielectric,dielectricPixel));
    // glTF G is roughness, B is metallic, both linear; R is not either.
    nearEqual(packedPixel,factorPixel); nearEqual(packedPixel,redPixel);
    EXPECT_GT(packedPixel.r,roughPixel.r+40);
    EXPECT_GT(packedPixel.r,dielectricPixel.r+10);
}

#if !defined(VOXY_WASM)
TEST(MeshPathGPUTest, LoadsAndRendersAuthoredBikeAndRider) {
    gpu::Context context;
    if (!context.initHeadless()) {
        GTEST_SKIP() << "GPU context not available";
    }

    MeshPathConfig config;
    config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    for (const auto& candidate : {
             std::filesystem::path("shaders/mesh_path.wgsl"),
             std::filesystem::path("../shaders/mesh_path.wgsl"),
             std::filesystem::path("../../shaders/mesh_path.wgsl")}) {
        if (std::filesystem::exists(candidate)) {
            config.shaderPath = candidate;
            break;
        }
    }
    ASSERT_TRUE(std::filesystem::exists(config.shaderPath));

    MeshPath path;
    ASSERT_TRUE(path.init(context.getDevice(), context.getQueue(), config));
    ASSERT_TRUE(path.loadMesh("data/moto/bike.vmesh"));
    ASSERT_TRUE(path.loadMesh("data/moto/rider.vmesh"));
    ASSERT_TRUE(path.loadMesh("data/moto/track.vmesh"));

    // Exercise all material texture uploads, mip generation, tangent-space
    // normals, and the alpha-blend pipeline. The authored bike currently uses
    // factor-only materials, so a synthetic textured primitive is required to
    // keep these production paths covered.
    moto::VmeshData textured;
    textured.header.flags = moto::kVmeshHasTangent;
    textured.header.vertexCount = 3u;
    textured.header.indexCount = 3u;
    textured.header.indexStride = 2u;
    textured.header.submeshCount = 1u;
    textured.header.materialCount = 1u;
    textured.header.meshCount = 1u;
    const std::array<moto::VmeshVertex, 3> texturedVertices = {{
        {{-0.4f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {}, {}},
        {{0.4f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {}, {}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
         {1.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}, {}, {}},
    }};
    textured.vertices.resize(sizeof(texturedVertices));
    std::memcpy(textured.vertices.data(), texturedVertices.data(),
                sizeof(texturedVertices));
    const std::array<uint16_t, 3> texturedIndices = {0u, 1u, 2u};
    textured.indices.resize(sizeof(texturedIndices));
    std::memcpy(textured.indices.data(), texturedIndices.data(),
                sizeof(texturedIndices));
    textured.submeshes.push_back({0u, 3u, 0u, 0u});
    textured.materials.resize(1u);
    moto::VmeshMaterial& texturedMaterial = textured.materials[0];
    texturedMaterial.alphaMode = moto::VmeshAlphaBlend;
    texturedMaterial.baseColorFactor[3] = 0.75f;
    const std::array<std::array<uint8_t, 16>, moto::VmeshTextureCount>
        texturePixels = {{
            {{220u, 80u, 24u, 224u, 180u, 48u, 16u, 224u,
              240u, 120u, 32u, 224u, 200u, 64u, 20u, 224u}},
            {{128u, 128u, 255u, 255u, 144u, 128u, 254u, 255u,
              128u, 144u, 254u, 255u, 112u, 128u, 254u, 255u}},
            {{255u, 160u, 48u, 255u, 255u, 128u, 64u, 255u,
              255u, 192u, 32u, 255u, 255u, 144u, 80u, 255u}},
            {{16u, 4u, 1u, 255u, 24u, 6u, 2u, 255u,
              8u, 2u, 1u, 255u, 20u, 5u, 1u, 255u}},
        }};
    for (uint32_t slot = 0u; slot < moto::VmeshTextureCount; ++slot) {
        texturedMaterial.hasTexture[slot] = 1u;
        texturedMaterial.textureIsSrgb[slot] =
            slot == moto::VmeshTextureBaseColor
                    || slot == moto::VmeshTextureEmissive
                ? 1u : 0u;
        texturedMaterial.textureWidth[slot] = 2u;
        texturedMaterial.textureHeight[slot] = 2u;
        texturedMaterial.textureOffset[slot] =
            static_cast<uint32_t>(textured.images.size());
        texturedMaterial.textureSize[slot] = 16u;
        textured.images.insert(textured.images.end(),
                               texturePixels[slot].begin(),
                               texturePixels[slot].end());
    }
    std::vector<uint8_t> texturedBytes;
    std::string texturedError;
    ASSERT_TRUE(moto::writeVmesh(
        textured, &texturedBytes, &texturedError)) << texturedError;
    ASSERT_TRUE(path.loadMeshFromBytes(texturedBytes));
    ASSERT_EQ(path.assetCount(), 4u);

    for (uint32_t mesh = 0u; mesh < 52u; ++mesh) {
        path.addInstance({.assetIndex = 0u, .meshIndex = mesh});
    }
    for (uint32_t mesh = 0u; mesh < 24u; ++mesh) {
        path.addInstance({.assetIndex = 1u, .meshIndex = mesh});
    }
    path.addInstance({.assetIndex = 2u, .meshIndex = 0u});
    MeshDrawInstance culledInstance;
    culledInstance.assetIndex = 2u;
    culledInstance.meshIndex = 0u;
    culledInstance.modelMatrix = glm::translate(
        glm::mat4(1.0f), glm::vec3(10000.0f, 0.0f, 0.0f));
    path.addInstance(culledInstance);

    constexpr uint32_t extent = 128u;
    WGPUTexture colorTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::renderTarget(
            extent, extent, WGPUTextureFormat_RGBA8Unorm,
            "mesh_path_test_color"));
    WGPUTexture depthTexture = gpu::createTexture(
        context.getDevice(), gpu::TextureDesc::depth(
            extent, extent, WGPUTextureFormat_Depth32Float,
            "mesh_path_test_depth"));
    ASSERT_NE(colorTexture, nullptr);
    ASSERT_NE(depthTexture, nullptr);
    WGPUTextureView colorView = gpu::createTextureView(colorTexture);
    WGPUTextureView depthView = gpu::createTextureView(depthTexture);
    ASSERT_NE(colorView, nullptr);
    ASSERT_NE(depthView, nullptr);

    WGPUCommandEncoderDescriptor encoderDescriptor{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        context.getDevice(), &encoderDescriptor);
    ASSERT_NE(encoder, nullptr);

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDepthStencilAttachment depthAttachment{};
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor clearDescriptor{};
    clearDescriptor.colorAttachmentCount = 1u;
    clearDescriptor.colorAttachments = &colorAttachment;
    clearDescriptor.depthStencilAttachment = &depthAttachment;
    WGPURenderPassEncoder clearPass =
        wgpuCommandEncoderBeginRenderPass(encoder, &clearDescriptor);
    ASSERT_NE(clearPass, nullptr);
    wgpuRenderPassEncoderEnd(clearPass);
    wgpuRenderPassEncoderRelease(clearPass);

    const glm::vec3 cameraPosition(3.0f, 2.2f, -4.5f);
    path.render(
        encoder, colorView, depthView,
        glm::lookAt(cameraPosition, glm::vec3(0.0f, 0.8f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 100.0f),
        cameraPosition, glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)),
        extent, extent, true);
    EXPECT_EQ(path.lastCulledInstanceCount(), 1u);
    EXPECT_EQ(path.lastSubmittedDrawCount(), 77u);

    WGPUCommandBufferDescriptor commandDescriptor{};
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(
        encoder, &commandDescriptor);
    ASSERT_NE(command, nullptr);
    wgpuQueueSubmit(context.getQueue(), 1u, &command);
    context.tick();

    wgpuCommandBufferRelease(command);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(depthView);
    wgpuTextureViewRelease(colorView);
    wgpuTextureDestroy(depthTexture);
    wgpuTextureRelease(depthTexture);
    wgpuTextureDestroy(colorTexture);
    wgpuTextureRelease(colorTexture);
    path.shutdown();
    context.shutdown();
}
#endif

namespace {
int expectedGrey(float radiance) {
    const float x = radiance / .6f;
    const float linear = std::clamp((x*(x+.0245786f)-.000090537f)
        / (x*(.983729f*x+.432951f)+.238081f), 0.0f, 1.0f);
    const float srgb = linear <= .0031308f ? linear*12.92f : 1.055f*std::pow(linear,1.0f/2.4f)-.055f;
    return static_cast<int>(std::lround(srgb*255));
}
PrimitiveLighting environmentOnly() {
    PrimitiveLighting lighting;
    lighting.fogDensity=0; lighting.sunIntensity=0;
    lighting.ambientColor=glm::vec3(1); lighting.ambientIntensity=1; lighting.exposure=1;
    return lighting;
}
MeshPathConfig filteredConfig() {
    MeshPathConfig config; config.filteredEnvironment=true;
    config.colorFormat=WGPUTextureFormat_RGBA8Unorm; config.frontFace=WGPUFrontFace_CW;
    return config;
}
}

TEST(MeshPathGPUTest, CoveSmoothGgxPeakUsesFinitePhysicalDenominatorAndLeavesLegacyUntouched) {
    DiagnosticContext context;ASSERT_TRUE(context.initHeadless());
    MeshPathConfig config;config.colorFormat=WGPUTextureFormat_RGBA8Unorm;config.frontFace=WGPUFrontFace_CW;
    MeshPath path;ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),config));
    auto data=diagnosticQuad();data.materials[0].metallicFactor=1;data.materials[0].roughnessFactor=.1f;
    ASSERT_TRUE(path.loadMeshData(data));
    // In the declared 3.2 m orthographic frame, pixel32's center is (.025,-.025).
    // Reflect its actual view about -Z so NoH=1. Keep radiance low enough to
    // measure the normalized narrow peak without saturating the display target.
    PrimitiveLighting light;light.direction=glm::normalize(glm::vec3(.025f,-.025f,-3));
    light.sunColor={1,1,1};light.sunIntensity=.0001f;light.ambientIntensity=0;light.fogDensity=0;light.exposure=1;
    std::vector<uint8_t> legacy,disabled,cove;
    path.addInstance({});ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},light,legacy));
    path.clearInstances();path.addInstance({.surface={1,1,0,0}});
    EXPECT_FALSE(drawDiagnosticPixels(path,context,{0,0,-3},light,disabled));
    path.clearInstances();path.addInstance({.surface={0,0,1,0}});
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},light,cove));
    const double cosine=3/std::sqrt(9+2*.025*.025),k=1.1*1.1/8;
    const double masking=cosine/(cosine*(1-k)+k);
    const double distribution=1/(std::acos(-1.)*.0001);
    const int expected=expectedGrey(float(distribution*masking*masking*.0001/(4*cosine)));
    for(int c=0;c<3;++c)EXPECT_NEAR(pixelAt(cove,32,32)[c],expected,3);
    EXPECT_GT(pixelAt(cove,32,32).r,pixelAt(legacy,32,32).r+20);
    RecordProperty("smoothPeakExpectedGrey",expected);RecordProperty("smoothPeakActualGrey",pixelAt(cove,32,32).r);
}

TEST(MeshEnvironmentGPUTest, WhiteFurnaceUsesIntegratedReflectionAndDiffuseEnergy) {
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),filteredConfig()));
    EXPECT_EQ(path.environmentLightingBytes(),MeshPath::filteredEnvironmentReservationBytes);
    const auto lighting=environmentOnly();
    for (uint32_t material=0;material<2;++material) {
        auto data=diagnosticQuad();
        data.materials[0].baseColorFactor[0]=1; data.materials[0].baseColorFactor[1]=1;
        data.materials[0].baseColorFactor[2]=1; data.materials[0].baseColorFactor[3]=1;
        data.materials[0].metallicFactor=material==0 ? 1.0f : 0.0f;
        data.materials[0].roughnessFactor=1;
        ASSERT_TRUE(path.loadMeshData(data)); path.clearInstances();
        path.addInstance({.assetIndex=material,.meshIndex=0});
        std::vector<uint8_t> pixels;
        ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,pixels));
        // Perfectly reflective GGX at r=1, NoV=1 integrates to 1-ln(2).
        // A white dielectric retains one unit with this diffuse-remainder model.
        const int expected=expectedGrey(material==0 ? 1.0f-std::log(2.0f) : 1.0f);
        const auto pixel=pixelAt(pixels,32,32);
        for (int c=0;c<3;++c) EXPECT_NEAR(pixel[c],expected,3);
        EXPECT_TRUE(path.environmentLightingReady());
        EXPECT_EQ(path.environmentBakeCount(),1u);
    }
}

TEST(MeshEnvironmentGPUTest, CoveWetFilmKeepsWhiteFurnaceBoundedAndUsesSharedFilteredOwner) {
    DiagnosticContext context;ASSERT_TRUE(context.initHeadless());
    MeshPath path;ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),filteredConfig()));
    const auto lighting=environmentOnly();
    for(uint32_t metal=0;metal<2;++metal) {
        auto data=diagnosticQuad();data.materials[0].metallicFactor=float(metal);data.materials[0].roughnessFactor=.45f;
        ASSERT_TRUE(path.loadMeshData(data));
        std::vector<uint8_t> dry,wet,partial,immersed;
        const auto draw=[&](float coverage,std::vector<uint8_t>& pixels,float immersion=0) {
            path.clearInstances();path.addInstance({.assetIndex=metal,.surface={coverage,0,1,immersion}});
            return drawDiagnosticPixels(path,context,{0,0,-3},lighting,pixels);
        };
        ASSERT_TRUE(draw(0,dry));ASSERT_TRUE(draw(1,wet));ASSERT_TRUE(draw(.5f,partial));
        ASSERT_TRUE(draw(1,immersed,1));
        const auto center=pixelAt(wet,32,32);
        for(int c=0;c<3;++c) {
            EXPECT_LE(center[c],expectedGrey(1)+2);EXPECT_GT(center[c],expectedGrey(.15f));
            EXPECT_GE(pixelAt(partial,32,32)[c],std::min(center[c],pixelAt(dry,32,32)[c])-1);
            EXPECT_LE(pixelAt(partial,32,32)[c],std::max(center[c],pixelAt(dry,32,32)[c])+1);
            EXPECT_LE(pixelAt(immersed,32,32)[c],expectedGrey(1)+2);
            EXPECT_GT(pixelAt(immersed,32,32)[c],expectedGrey(.15f));
        }
        if(metal==1) { EXPECT_EQ(immersed,dry); }
        EXPECT_GT(std::abs(center.r-pixelAt(dry,32,32).r),1);
        EXPECT_EQ(path.environmentBakeCount(),1u);EXPECT_TRUE(path.environmentLightingReady());
        EXPECT_EQ(path.environmentLightingBytes(),MeshPath::filteredEnvironmentReservationBytes);
        RecordProperty(metal==0 ? "plasticWetFurnaceGrey" : "metalWetFurnaceGrey",center.r);
        RecordProperty(metal==0 ? "plasticImmersedFurnaceGrey" : "metalImmersedFurnaceGrey",pixelAt(immersed,32,32).r);
    }
}

TEST(MeshEnvironmentGPUTest, DiscardRetriesAndSceneReplacementInvalidatesOnlyItsBake) {
    DiagnosticContext context; ASSERT_TRUE(context.initHeadless());
    MeshPath path; ASSERT_TRUE(path.init(context.getDevice(),context.getQueue(),filteredConfig()));
    auto data=diagnosticQuad();
    for (auto& c:data.materials[0].baseColorFactor) c=1;
    data.materials[0].metallicFactor=1; data.materials[0].roughnessFactor=1;
    ASSERT_TRUE(path.loadMeshData(data)); path.addInstance({.assetIndex=0,.meshIndex=0});
    WGPUCommandEncoderDescriptor desc{};
    auto abandoned=wgpuDeviceCreateCommandEncoder(context.getDevice(),&desc); ASSERT_NE(abandoned,nullptr);
    ASSERT_TRUE(path.encodeEnvironmentLighting(abandoned));
    EXPECT_FALSE(path.environmentLightingReady());
    EXPECT_FALSE(path.setSceneTextures(nullptr,nullptr));
    EXPECT_FALSE(path.invalidateEnvironmentLighting());
    wgpuCommandEncoderRelease(abandoned); path.discardEnvironmentEncoding();
    const auto lighting=environmentOnly();
    for (uint32_t draw=0;draw<2;++draw) {
        std::vector<uint8_t> pixels;
        ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,pixels));
        for (int c=0;c<3;++c) EXPECT_NEAR(pixelAt(pixels,32,32)[c],expectedGrey(1.0f-std::log(2.0f)),3);
        EXPECT_EQ(path.environmentBakeCount(),2u); // First discarded, second submitted, third draw cached.
    }
    struct Source {
        WGPUTexture texture=nullptr; WGPUTextureView view=nullptr;
        ~Source() { if(view) wgpuTextureViewRelease(view); if(texture) wgpuTextureRelease(texture); }
    } source;
    const std::array<uint8_t,4> grey{64,64,64,255};
    source.texture=gpu::createTextureWithData(context.getDevice(),context.getQueue(),
        gpu::TextureDesc::tex2D(1,1,WGPUTextureFormat_RGBA8Unorm),std::as_bytes(std::span(grey)),4);
    ASSERT_NE(source.texture,nullptr); source.view=gpu::createTextureView(source.texture); ASSERT_NE(source.view,nullptr);
    ASSERT_TRUE(path.setSceneTextures(source.view,nullptr)); EXPECT_FALSE(path.environmentLightingReady());
    std::vector<uint8_t> pixels;
    ASSERT_TRUE(drawDiagnosticPixels(path,context,{0,0,-3},lighting,pixels));
    for (int c=0;c<3;++c) EXPECT_NEAR(pixelAt(pixels,32,32)[c],expectedGrey((64.0f/255)*(1.0f-std::log(2.0f))),3);
    EXPECT_TRUE(path.environmentLightingReady()); EXPECT_EQ(path.environmentBakeCount(),3u);
}


namespace {
// One actual texel is enough for the analytic HDR/depth checks below. No image
// gallery is generated; all values come back through the real GPU queue.
bool readOpaqueTexel(DiagnosticContext& context, WGPUTexture texture,
                     std::array<uint8_t, 256>& bytes) {
    PixelResources resources;
    resources.readback = gpu::createBuffer(context.getDevice(), gpu::BufferDesc{
        .label = "opaque_texel_readback", .size = bytes.size(),
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead});
    resources.encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), nullptr);
    if (!resources.readback || !resources.encoder) return false;
    gpu::CompatImageCopyTexture source{};
    source.texture = texture; source.aspect = WGPUTextureAspect_All;
    source.origin = {32,32,0};
#if defined(VOXY_WASM)
    WGPUTexelCopyBufferInfo destination{};
#else
    WGPUImageCopyBuffer destination{};
#endif
    destination.buffer = resources.readback;
    destination.layout.bytesPerRow = 256; destination.layout.rowsPerImage = 1;
    const WGPUExtent3D extent{1,1,1};
    wgpuCommandEncoderCopyTextureToBuffer(resources.encoder, &source, &destination, &extent);
    resources.command = wgpuCommandEncoderFinish(resources.encoder, nullptr);
    if (!resources.command) return false;
    wgpuQueueSubmit(context.getQueue(), 1, &resources.command);
    auto state = std::make_shared<std::atomic<int>>(0);
    using Payload = std::shared_ptr<std::atomic<int>>;
    auto* payload = new Payload(state);
#if defined(VOXY_WASM)
    WGPUBufferMapCallbackInfo info = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = payload;
    info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
        const std::unique_ptr<Payload> completion(static_cast<Payload*>(userdata));
        (*completion)->store(status == WGPUMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
    };
    static_cast<void>(wgpuBufferMapAsync(resources.readback, WGPUMapMode_Read, 0, bytes.size(), info));
#else
    wgpuBufferMapAsync(resources.readback, WGPUMapMode_Read, 0, bytes.size(),
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            const std::unique_ptr<Payload> completion(static_cast<Payload*>(userdata));
            (*completion)->store(status == WGPUBufferMapAsyncStatus_Success ? 1 : 2, std::memory_order_release);
        }, payload);
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (state->load(std::memory_order_acquire) == 0 && std::chrono::steady_clock::now() < deadline) {
#if defined(VOXY_WASM)
        emscripten_sleep(1);
#else
        static_cast<void>(wgpuDevicePoll(context.getDevice(), false, nullptr));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    }
    if (state->load(std::memory_order_acquire) != 1) return false;
    const auto* mapped = wgpuBufferGetConstMappedRange(resources.readback, 0, bytes.size());
    if (!mapped) { wgpuBufferUnmap(resources.readback); return false; }
    std::memcpy(bytes.data(), mapped, bytes.size());
    wgpuBufferUnmap(resources.readback);
    return true;
}
} // namespace

TEST(MeshOpaqueGPUTest, PreservesLinearHdrNearestDepthTerrainCacheAndRemovesPreviousObjects) {
    DiagnosticContext context;
    ASSERT_TRUE(context.initHeadless()) << "Hardware/browser GPU required for opaque integration";
    auto device = context.getDevice(); auto queue = context.getQueue();
    OpaqueScene scene;
    ASSERT_TRUE(scene.init(device, "shaders/opaque_scene.wgsl"));
    ASSERT_TRUE(scene.resize(64,64));
    EXPECT_EQ(scene.requestedBytes(), 64u*64u*12u);
    auto original = scene.colorView();
    EXPECT_FALSE(scene.resize(0,64)); EXPECT_FALSE(scene.resize(8192,8192));
    EXPECT_EQ(scene.colorView(), original);
    ASSERT_TRUE(scene.resize(128,64));
    EXPECT_EQ(scene.requestedBytes(), 128u*64u*12u);
    ASSERT_TRUE(scene.resize(64,64));

    PixelResources terrain;
    auto desc = gpu::TextureDesc::renderTarget(64,64,WGPUTextureFormat_RGBA16Float,"immutable_test_terrain");
    desc.usage |= WGPUTextureUsage_CopySrc;
    terrain.color = gpu::createTexture(device,desc);
    desc.format = WGPUTextureFormat_R32Float;
    terrain.depth = gpu::createTexture(device,desc);
    ASSERT_NE(terrain.color,nullptr); ASSERT_NE(terrain.depth,nullptr);
    terrain.colorView = gpu::createTextureView(terrain.color);
    terrain.depthView = gpu::createTextureView(terrain.depth);
    PixelResources hardwareDepth;
    hardwareDepth.depth = gpu::createTexture(device,gpu::TextureDesc::depth(64,64,WGPUTextureFormat_Depth32Float));
    ASSERT_NE(hardwareDepth.depth,nullptr);
    hardwareDepth.depthView = gpu::createTextureView(hardwareDepth.depth);
    MeshPathConfig config;
    config.linearHdrOutput = true; config.colorFormat = WGPUTextureFormat_RGBA16Float; config.frontFace = WGPUFrontFace_CW;
    MeshPath mesh;
    auto invalid = config; invalid.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    EXPECT_FALSE(mesh.init(device,queue,invalid));
    ASSERT_TRUE(mesh.init(device,queue,config));
    auto red = diagnosticQuad(); red.materials[0].unlit = 1;
    red.materials[0].baseColorFactor[1] = 0; red.materials[0].baseColorFactor[2] = 0;
    red.materials[0].emissiveFactor[0] = 3; // Exactly 4 linear red; output conversion would destroy this.
    auto blue = red; blue.materials[0].baseColorFactor[0] = 0; blue.materials[0].baseColorFactor[2] = 1;
    blue.materials[0].emissiveFactor[0] = 0;
    ASSERT_TRUE(mesh.loadMeshData(red)); ASSERT_TRUE(mesh.loadMeshData(blue));
    auto blended = red; blended.materials[0].alphaMode = moto::VmeshAlphaBlend;
    EXPECT_FALSE(mesh.loadMeshData(blended)); EXPECT_EQ(mesh.assetCount(),2u);
    ASSERT_TRUE(mesh.setSceneTextures(nullptr,terrain.depthView));
    const glm::vec3 camera(0,0,-3);
    const auto view = glm::lookAt(camera,glm::vec3(0),glm::vec3(0,1,0));
    const auto projection = glm::perspective(glm::radians(60.0f),1.0f,0.1f,10.0f);
    PrimitiveLighting lighting; lighting.fogDensity = 0; lighting.exposure = 0.1f;
    for (int frame = 0; frame < 3; ++frame) {
        PixelResources commands;
        commands.encoder = wgpuDeviceCreateCommandEncoder(device,nullptr);
        ASSERT_NE(commands.encoder,nullptr);
        // Seed the immutable terrain only once, not once per frame.
        std::array<WGPURenderPassColorAttachment,2> cache{};
        cache[0].view = terrain.colorView; cache[0].clearValue = {0.25,0.5,1,1};
        cache[1].view = terrain.depthView; cache[1].clearValue = {5,0,0,0};
        for (auto& color : cache) {
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
        }
        WGPURenderPassDepthStencilAttachment depth{};
        depth.view = hardwareDepth.depthView; depth.depthClearValue = 1;
        depth.depthLoadOp = WGPULoadOp_Clear; depth.depthStoreOp = WGPUStoreOp_Store;
        depth.stencilReadOnly = true;
        WGPURenderPassDescriptor passDesc{};
        passDesc.depthStencilAttachment = &depth;
        if (frame == 0) { passDesc.colorAttachments = cache.data(); passDesc.colorAttachmentCount = cache.size(); }
        auto pass = wgpuCommandEncoderBeginRenderPass(commands.encoder,&passDesc);
        ASSERT_NE(pass,nullptr); wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        EXPECT_FALSE(scene.seed(commands.encoder,scene.colorView(),terrain.depthView));
        ASSERT_TRUE(scene.seed(commands.encoder,terrain.colorView,terrain.depthView));
        mesh.clearInstances();
        if (frame == 0) {
            mesh.addInstance({.assetIndex=0,.meshIndex=0});
            // Draw the farther object second: object depth must still keep red.
            mesh.addInstance({.assetIndex=1,.meshIndex=0,.modelMatrix=glm::translate(glm::mat4(1),glm::vec3(0,0,1))});
        } else if (frame == 2) {
            // Behind the cached terrain: must neither replace its color nor its depth.
            mesh.addInstance({.assetIndex=0,.meshIndex=0,.modelMatrix=glm::translate(glm::mat4(1),glm::vec3(0,0,3))});
        }
        EXPECT_FALSE(mesh.render(commands.encoder,scene.colorView(),hardwareDepth.depthView,
            view,projection,camera,lighting,64,64,true)); // HDR requires its radial output.
        ASSERT_TRUE(mesh.render(commands.encoder,scene.colorView(),hardwareDepth.depthView,
            view,projection,camera,lighting,64,64,true,scene.depthView()));
        commands.command = wgpuCommandEncoderFinish(commands.encoder,nullptr);
        ASSERT_NE(commands.command,nullptr); wgpuQueueSubmit(queue,1,&commands.command);
        std::array<uint8_t,256> bytes{};
        ASSERT_TRUE(readOpaqueTexel(context,scene.colorTexture(),bytes));
        glm::u16vec4 encoded{}; std::memcpy(&encoded,bytes.data(),8);
        const auto color = glm::unpackHalf(encoded);
        EXPECT_FLOAT_EQ(color.r,frame==0 ? 4.0f : 0.25f);
        EXPECT_FLOAT_EQ(color.g,frame==0 ? 0.0f : 0.5f);
        EXPECT_FLOAT_EQ(color.b,frame==0 ? 0.0f : 1.0f);
        ASSERT_TRUE(readOpaqueTexel(context,scene.depthTexture(),bytes));
        float radial = 0; std::memcpy(&radial,bytes.data(),4);
        // Independent perspective oracle: centre sample is half a pixel off axis.
        const float slope = std::tan(glm::radians(30.0f)) / 64.0f;
        EXPECT_NEAR(radial,frame==0 ? 3.0f*std::sqrt(1+2*slope*slope) : 5.0f,0.00001f);
        std::printf("Opaque frame %d: linear RGB %.3f %.3f %.3f; radial metres %.6f\n",frame,
            static_cast<double>(color.r),static_cast<double>(color.g),static_cast<double>(color.b),static_cast<double>(radial));
    }
    std::array<uint8_t,256> bytes{};
    ASSERT_TRUE(readOpaqueTexel(context,terrain.color,bytes));
    glm::u16vec4 encoded{}; std::memcpy(&encoded,bytes.data(),8);
    EXPECT_EQ(glm::unpackHalf(encoded),glm::vec4(0.25f,0.5f,1,1));
    ASSERT_TRUE(readOpaqueTexel(context,terrain.depth,bytes));
    float radial = 0; std::memcpy(&radial,bytes.data(),4); EXPECT_FLOAT_EQ(radial,5);
}

}  // namespace voxy::render

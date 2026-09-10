#include "render/salvage_asset_fixture.hpp"
#include "core/sha256.hpp"
#include "gpu/context.hpp"
#include "gpu/resources.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>

#if !defined(VOXY_WASM)
#include <unistd.h>
struct WGPUWrappedSubmissionIndex;
extern "C" WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, const WGPUWrappedSubmissionIndex*);
#endif

namespace voxy::render {
namespace {
using Bundle = game::assets::CookedPartBundle;
using Status = SalvageFixtureStatus;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr uint64_t probeLod = 9007199254740997ull;
constexpr std::string_view probeManifest = "f595f6b490fbbcc6f947eb1ac31f7328cc4dedf9d84896268a8aed45dea80898";

Bytes bytes(std::string_view text) { return {text.begin(), text.end()}; }
std::string hash(const Bytes& value) { return core::sha256Hex(core::sha256(std::as_bytes(std::span(value)))); }

struct Package {
    std::map<std::string, Bytes, std::less<>> files;
    game::assets::CookedPartSelection selection;
    Package() {
        for (const auto& name : {"cook-manifest.json", "gameplay.json", "lod-9007199254740997.vmesh"}) {
            // These are declared test runfiles (which Bazel may symlink), not
            // the production directory provider. Production no-follow is
            // covered by CookedPartDirectory's materialized-package tests.
            std::ifstream input(std::filesystem::path("data/salvage/runtime_probe_v1") / name, std::ios::binary);
            if (!input) throw std::runtime_error("missing declared probe runfile");
            files[name] = Bytes(std::istreambuf_iterator<char>(input), {});
        }
        std::string error;
        const auto parsed = game::assets::parseGameplaySidecar(
            std::string(files["gameplay.json"].begin(), files["gameplay.json"].end()), [](const auto&) { return true; }, error);
        if (!parsed) throw std::runtime_error(error);
        selection.part = parsed->part.key;
        selection.manifestSha256 = probeManifest;
        selection.lodRules.push_back({probeLod, {}});
    }
    std::shared_ptr<const Bundle> admit() {
        std::string error;
        auto result = game::assets::admitCookedPartBundle(selection,
            [&](std::string_view name, size_t cap, std::string& reason) -> std::optional<Bytes> {
                const auto found = files.find(name);
                if (found == files.end() || found->second.size() > cap) { reason = "missing or over cap"; return {}; }
                return found->second;
            }, error);
        if (!result) throw std::runtime_error(error);
        return result;
    }
    void changeMesh(const std::function<void(moto::VmeshData&)>& change) {
        std::string error;
        moto::VmeshData mesh;
        auto& encoded = files["lod-9007199254740997.vmesh"];
        if (!moto::readVmesh(encoded.data(), encoded.size(), &mesh, &error)) throw std::runtime_error(error);
        change(mesh);
        if (!moto::writeVmesh(mesh, &encoded, &error)) throw std::runtime_error(error);
        auto manifest = Json::parse(files["cook-manifest.json"]);
        manifest["lods"][0]["sha256"] = hash(encoded);
        manifest["lods"][0]["bytes"] = encoded.size();
        files["cook-manifest.json"] = bytes(manifest.dump(2) + "\n");
        selection.manifestSha256 = hash(files["cook-manifest.json"]);
    }
    void multipleLods(uint64_t firstAsset, uint32_t count) {
        auto metadata = Json::parse(files["gameplay.json"]);
        auto manifest = Json::parse(files["cook-manifest.json"]);
        const auto sourceLod = metadata["lods"][0];
        const auto sourceRecord = manifest["lods"][0];
        const auto sourceBytes = files["lod-9007199254740997.vmesh"];
        metadata["lods"] = Json::array(); manifest["lods"] = Json::array();
        selection.lodRules.clear();
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t id = 100u + i;
            const auto filename = game::assets::cookedLodFilename(id);
            auto lod = sourceLod; auto record = sourceRecord;
            lod["id"] = std::to_string(id); lod["asset"]["counter"] = std::to_string(firstAsset + i);
            lod["minimum_screen_height_pixels"] = (count - 1u - i) * 60u;
            record["id"] = std::to_string(id); record["file"] = filename;
            metadata["lods"].push_back(lod); manifest["lods"].push_back(record);
            files[filename] = sourceBytes;
            selection.lodRules.push_back({id, {}});
        }
        std::string error;
        const auto normalized = game::assets::parseGameplaySidecar(metadata.dump(), [](const auto&) { return true; }, error);
        if (!normalized) throw std::runtime_error(error);
        files["gameplay.json"] = bytes(normalized->normalizedJson);
        manifest["normalized_sidecar"]["bytes"] = files["gameplay.json"].size();
        manifest["normalized_sidecar"]["sha256"] = hash(files["gameplay.json"]);
        files["cook-manifest.json"] = bytes(manifest.dump(2) + "\n");
        selection.manifestSha256 = hash(files["cook-manifest.json"]);
    }
};

TEST(SalvageAssetFixture, RejectsUninitializedUseWithoutMutatingTicket) {
    SalvageAssetFixture fixture;
    std::string error;
    EXPECT_FALSE(fixture.init(nullptr, nullptr, {}, error));
    EXPECT_EQ(fixture.poll(), Status::Uninitialized);
    EXPECT_FALSE(fixture.beginCandidate({}, error));
    EXPECT_FALSE(fixture.publishCandidate(error));
    EXPECT_FALSE(fixture.setSceneViews(nullptr, nullptr, error));
    SalvageFixtureTicket ticket{7,9};
    EXPECT_FALSE(fixture.encode(nullptr, nullptr, nullptr, {}, {}, ticket, error));
    EXPECT_EQ(ticket, (SalvageFixtureTicket{7,9}));
    EXPECT_FALSE(fixture.submitted(ticket, error));
    EXPECT_FALSE(fixture.discarded(ticket, error));
    EXPECT_FALSE(fixture.resetInstances(error));
    EXPECT_FALSE(fixture.requestLeave(error));
    fixture.shutdown();
}

game::construction::PartDefinition guidePart() {
    game::construction::PartDefinition part;
    part.footprint = {{-25,-24,-100},{25,24,100}};
    game::construction::SocketDefinition socket;
    socket.frame = {{0,24,-50},{5}};
    socket.clearance = {{-6,0,-6},{6,9,6}};
    part.sockets.push_back(socket);
    return part;
}

TEST(InspectionGuides, SharedMeshHasOutwardFacesExactBoundsAndAccountedStorage) {
    const auto mesh = inspectionGuideMesh();
    game::assets::RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(game::assets::prepareRigidPrefab(mesh, {}, {}, prefab, error)) << error;
    EXPECT_EQ(prefab.counts.gpuBytes, inspectionGuideGpuBytes);
    EXPECT_EQ(prefab.counts.expandedDraws, 1u);
    EXPECT_EQ(prefab.canonicalBounds.minimum, glm::dvec3(-.5));
    EXPECT_EQ(prefab.canonicalBounds.maximum, glm::dvec3(.5));
    EXPECT_EQ(mesh.materials[0].unlit, 1);
    for (size_t t=0; t<12; ++t) {
        std::array<uint32_t,3> index{};
        std::memcpy(index.data(), mesh.indices.data()+t*12,12);
        std::array<glm::vec3,3> p{};
        moto::VmeshVertex vertex{};
        for (size_t c=0; c<3; ++c) {
            std::memcpy(&vertex, mesh.vertices.data()+index[c]*sizeof(vertex),sizeof(vertex));
            p[c]={vertex.position[0],vertex.position[1],vertex.position[2]};
        }
        const auto normal=glm::cross(p[1]-p[0],p[2]-p[0]);
        EXPECT_GT(glm::dot(normal,p[0]),0) << t;
        EXPECT_GT(glm::dot(normal,glm::vec3(vertex.normal[0],vertex.normal[1],vertex.normal[2])),0);
    }
}

TEST(InspectionGuides, ExactMetrePlateRulersAndAsymmetricCanonicalAxes) {
    const auto part=guidePart();
    const game::assets::PrefabBounds bounds{{-.5,-.48,-2},{.5,.66,2},true};
    std::vector<InspectionGuideBox> guides;
    std::string error;
    ASSERT_TRUE(makeInspectionGuides(part,bounds,glm::dmat4(1),{},InspectionGuides::Dimensions,512,guides,error)) << error;
    ASSERT_EQ(guides.size(),26u);
    // Twelve amber bound edges, 4 m ruler plus five whole-metre ticks,
    // .96 m body ruler plus four plate ticks, then +X,+Y,-Z stand.
    EXPECT_NEAR(glm::length(glm::vec3(guides[12].model[2])),4.0,1e-6);
    for (size_t i=14;i<18;++i) EXPECT_NEAR(guides[i].model[3].z-guides[i-1].model[3].z,1,1e-6);
    EXPECT_NEAR(glm::length(glm::vec3(guides[18].model[1])),.96,1e-6);
    for (size_t i=20;i<23;++i) EXPECT_NEAR(guides[i].model[3].y-guides[i-1].model[3].y,.32,1e-6);
    EXPECT_NEAR(glm::length(glm::vec3(guides[23].model[0])),.6,1e-6);
    EXPECT_NEAR(glm::length(glm::vec3(guides[24].model[1])),.8,1e-6);
    EXPECT_NEAR(glm::length(glm::vec3(guides[25].model[2])),1,1e-6);
    EXPECT_LT(guides[25].model[3].z, guides[24].model[3].z);
}

TEST(InspectionGuides, SocketFramesStayCanonicalAcrossAllRotationsAndSectorRebases) {
    namespace c=game::construction;
    const auto part=guidePart();
    for (uint8_t rotation=0; rotation<24; ++rotation) for (double sector : {1e9,1e9+256.0}) {
        const c::GridTransform placement{{-75,-48,-125},{rotation}};
        const auto socket=c::compose(placement,part.sockets[0].frame);
        ASSERT_TRUE(socket);
        const auto start=c::transformPosition(*socket,{}), end=c::transformPosition(*socket,{0,20,0});
        ASSERT_TRUE(start); ASSERT_TRUE(end);
        const glm::dvec3 rootOffset{1e9-sector,3,-5};
        std::vector<InspectionGuideBox> guides;
        std::string error;
        ASSERT_TRUE(makeInspectionGuides(part,{},glm::translate(glm::dmat4(1),rootOffset),placement,
            InspectionGuides::Sockets,512,guides,error)) << error;
        ASSERT_EQ(guides.size(),15u);
        const auto origin=glm::dvec3(guides[1].model[3])-glm::dvec3(guides[1].model[1])*.5;
        const auto tip=glm::dvec3(guides[1].model[3])+glm::dvec3(guides[1].model[1])*.5;
        const auto a=c::toMetres(*start),b=c::toMetres(*end);
        ASSERT_TRUE(a); ASSERT_TRUE(b);
        const auto expectedA=rootOffset+glm::dvec3(a->x,a->y,a->z),expectedB=rootOffset+glm::dvec3(b->x,b->y,b->z);
        for (glm::length_t i=0;i<3;++i) {
            EXPECT_NEAR(origin[i],expectedA[i],2e-5) << int(rotation);
            EXPECT_NEAR(tip[i],expectedB[i],2e-5) << int(rotation);
        }
    }
}

TEST(InspectionGuides, RejectsIncompleteOverlaysAndInvalidTransformsTransactionally) {
    auto part=guidePart();
    std::vector<InspectionGuideBox> guides(1);
    guides[0].color={.1f,.2f,.3f,1};
    std::string error;
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,14,guides,error));
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::scale(glm::dmat4(1),glm::dvec3(2)),{},InspectionGuides::Sockets,512,guides,error));
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{{},{24}},InspectionGuides::Sockets,512,guides,error));
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{},static_cast<InspectionGuides>(3),512,guides,error));
    part.sockets[0].clearance.maximum.y=0;
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,512,guides,error));
    ASSERT_EQ(guides.size(),1u);
    EXPECT_EQ(guides[0].color,glm::vec4(.1f,.2f,.3f,1));
    EXPECT_TRUE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Off,0,guides,error));
    EXPECT_TRUE(guides.empty());
}

TEST(InspectionGuides, PrototypeBeamPreservesExactCanonicalFootprintAndLitMaterial) {
    const auto part=game::construction::makeStarterCatalogDraft().definitions[0];
    moto::VmeshData mesh;
    game::assets::RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(makePrototypeFixtureMesh(part,mesh,prefab,error)) << error;
    EXPECT_EQ(prefab.counts.gpuBytes,1936u);
    EXPECT_EQ(prefab.renderToCanonical.value,0);
    EXPECT_EQ(prefab.canonicalBounds.minimum,glm::dvec3(-2,static_cast<double>(-.48f),-.5));
    EXPECT_EQ(prefab.canonicalBounds.maximum,glm::dvec3(2,static_cast<double>(.48f),.5));
    EXPECT_EQ(mesh.materials[0].unlit,0);
    EXPECT_FLOAT_EQ(mesh.materials[0].roughnessFactor,static_cast<float>(part.material.roughness));
    EXPECT_FLOAT_EQ(mesh.materials[0].baseColorFactor[0],static_cast<float>(part.material.linearBaseColor[0]));
    auto invalid=part;invalid.visuals.clear();
    EXPECT_FALSE(makePrototypeFixtureMesh(invalid,mesh,prefab,error));
    EXPECT_EQ(prefab.counts.gpuBytes,1936u);
    EXPECT_EQ(mesh.header.vertexCount,24u);
}

TEST(InspectionGuides, ExplicitConnectedSocketSelectionIsCompleteOrRejected) {
    namespace c=game::construction;
    const auto part=c::makeStarterCatalogDraft().definitions[0];
    const std::array selected{c::SocketId{101},c::SocketId{107}};
    std::vector<InspectionGuideBox> guides;
    std::string error;
    ASSERT_TRUE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,512,guides,error,
        std::span<const c::SocketId>(selected))) << error;
    EXPECT_EQ(guides.size(),30u);
    const std::array bad{c::SocketId{101},c::SocketId{999}};
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,512,guides,error,
        std::span<const c::SocketId>(bad)));
    EXPECT_EQ(guides.size(),30u);
    const std::array duplicate{c::SocketId{101},c::SocketId{101}};
    EXPECT_FALSE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,512,guides,error,
        std::span<const c::SocketId>(duplicate)));
    EXPECT_EQ(guides.size(),30u);
    ASSERT_TRUE(makeInspectionGuides(part,{},glm::dmat4(1),{},InspectionGuides::Sockets,0,guides,error,
        std::span<const c::SocketId>{}));
    EXPECT_TRUE(guides.empty());
}

#if !defined(VOXY_WASM)
struct FixtureGPU : testing::Test {
    gpu::Context context;
    SalvageAssetFixture fixture;
    std::shared_ptr<const Bundle> bundle;
    std::string error;
    WGPUTexture color = nullptr, depth = nullptr;
    WGPUTextureView colorView = nullptr, depthView = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer command = nullptr;
    std::filesystem::path temporary;
    void SetUp() override {
        if (!context.initHeadless()) GTEST_SKIP() << "GPU context not available";
        bundle = Package{}.admit();
        SalvageFixtureConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
        ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error)) << error;
    }
    void pump() { static_cast<void>(wgpuDevicePoll(context.getDevice(), false, nullptr)); }
    Status await(const std::function<bool(Status)>& predicate) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        Status status = fixture.poll();
        while (!predicate(status) && status != Status::Fatal && std::chrono::steady_clock::now() < until) {
            pump(); status = fixture.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return status;
    }
    void begin(std::span<const std::shared_ptr<const Bundle>> bundles) {
        ASSERT_TRUE(fixture.beginCandidate(bundles, error)) << error;
        ASSERT_EQ(await([](Status value) { return value == Status::CandidateReady; }), Status::CandidateReady) << fixture.lastError();
        ASSERT_TRUE(fixture.publishCandidate(error)) << error;
    }
    void begin() { const std::array bundles{bundle}; begin(bundles); }
    SalvageFixtureFrame frame() const {
        SalvageFixtureFrame value;
        value.cameraPosition = {5,4,-7};
        value.view = glm::lookAt(value.cameraPosition, glm::vec3(0,.3f,0), glm::vec3(0,1,0));
        value.projection = glm::perspective(glm::radians(55.0f), 1.0f, .1f, 100.0f);
        value.width = value.height = 64;
        return value;
    }
    void startFrame() {
        if (!color) {
            color = gpu::createTexture(context.getDevice(), gpu::TextureDesc::renderTarget(64,64,WGPUTextureFormat_RGBA8Unorm));
            depth = gpu::createTexture(context.getDevice(), gpu::TextureDesc::depth(64,64,WGPUTextureFormat_Depth32Float));
            ASSERT_NE(color, nullptr); ASSERT_NE(depth, nullptr);
            colorView = gpu::createTextureView(color); depthView = gpu::createTextureView(depth);
        }
        WGPUCommandEncoderDescriptor descriptor{};
        encoder = wgpuDeviceCreateCommandEncoder(context.getDevice(), &descriptor);
        ASSERT_NE(encoder, nullptr);
        WGPURenderPassColorAttachment attachment{};
        attachment.view = colorView; attachment.loadOp = WGPULoadOp_Clear; attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = {0,0,0,1}; attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = depthView; depthAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store; depthAttachment.depthClearValue = 1;
        depthAttachment.stencilLoadOp = WGPULoadOp_Undefined; depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
        depthAttachment.stencilReadOnly = true;
        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.colorAttachmentCount = 1; passDescriptor.colorAttachments = &attachment;
        passDescriptor.depthStencilAttachment = &depthAttachment;
        auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        ASSERT_NE(pass, nullptr); wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    }
    void submit(SalvageFixtureTicket ticket) {
        WGPUCommandBufferDescriptor descriptor{};
        command = wgpuCommandEncoderFinish(encoder, &descriptor);
        ASSERT_NE(command, nullptr);
        wgpuQueueSubmit(context.getQueue(), 1, &command);
        ASSERT_TRUE(fixture.submitted(ticket, error)) << error;
        releaseCommands();
    }
    void releaseCommands() {
        if (command) { wgpuCommandBufferRelease(command); command = nullptr; }
        if (encoder) { wgpuCommandEncoderRelease(encoder); encoder = nullptr; }
    }
    void TearDown() override {
        releaseCommands();
        const auto ticket = fixture.stats().unresolved;
        if (ticket.serial) { EXPECT_TRUE(fixture.discarded(ticket, error)) << error; }
        if (fixture.poll() != Status::Uninitialized && fixture.poll() != Status::Fatal) {
            EXPECT_TRUE(fixture.requestLeave(error)) << error;
            EXPECT_EQ(await([](Status value) { return value == Status::Drained; }), Status::Drained) << fixture.lastError();
        }
        fixture.shutdown();
        if (context.getDevice()) static_cast<void>(wgpuDevicePoll(context.getDevice(), true, nullptr));
        if (colorView) wgpuTextureViewRelease(colorView);
        if (depthView) wgpuTextureViewRelease(depthView);
        if (color) { wgpuTextureDestroy(color); wgpuTextureRelease(color); }
        if (depth) { wgpuTextureDestroy(depth); wgpuTextureRelease(depth); }
        if (!temporary.empty()) std::filesystem::remove_all(temporary);
    }
};

TEST_F(FixtureGPU, FilteredEnvironmentIsChargedRetriedAndRetiredWithItsGeneration) {
    fixture.shutdown();
    SalvageFixtureConfig config; config.colorFormat=WGPUTextureFormat_RGBA8Unorm;
    config.filteredEnvironment=true;
    const uint64_t charge=SalvageAssetFixture::fixedGpuReservationBytes
        +MeshPath::filteredEnvironmentReservationBytes+bundle->requestedGpuBytes();
    config.maximumOwnerGpuBytes=charge-1;
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error));
    const std::array bundles{bundle};
    EXPECT_FALSE(fixture.beginCandidate(bundles,error));
    EXPECT_EQ(fixture.stats().candidate.environmentGpuBytes,0u);
    fixture.shutdown(); config.maximumOwnerGpuBytes=16ull*1024ull*1024ull;
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error));
    begin();
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,charge);
    EXPECT_EQ(fixture.stats().active.environmentGpuBytes,MeshPath::filteredEnvironmentReservationBytes);
    const std::array placements{SalvageFixturePlacement{0,probeLod,glm::dmat4(1),{}}};
    auto value=frame(); SalvageFixtureTicket ticket;
    startFrame(); ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error));
    EXPECT_EQ(fixture.stats().active.environmentBakeCount,1u);
    EXPECT_FALSE(fixture.stats().active.environmentReady);
    releaseCommands(); ASSERT_TRUE(fixture.discarded(ticket,error));
    EXPECT_FALSE(fixture.stats().active.environmentReady);
    startFrame(); ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error));
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.environmentBakeCount,2u);
    EXPECT_TRUE(fixture.stats().active.environmentReady);
    ASSERT_TRUE(fixture.resetInstances(error));
    value.guides=InspectionGuides::Sockets;
    startFrame(); ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error));
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.environmentBakeCount,2u);
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,charge);
    ASSERT_TRUE(fixture.beginCandidate(bundles,error));
    EXPECT_EQ(fixture.stats().candidate.environmentGpuBytes,MeshPath::filteredEnvironmentReservationBytes);
    EXPECT_FALSE(fixture.stats().candidate.environmentReady);
    ASSERT_EQ(await([](Status s){return s==Status::CandidateReady;}),Status::CandidateReady);
    ASSERT_TRUE(fixture.publishCandidate(error));
    EXPECT_FALSE(fixture.stats().active.environmentReady);
    EXPECT_EQ(fixture.stats().active.environmentBakeCount,0u);
    ASSERT_TRUE(fixture.requestLeave(error));
    ASSERT_EQ(await([](Status s){return s==Status::Drained;}),Status::Drained);
    EXPECT_EQ(fixture.stats().active.environmentGpuBytes,0u);
    EXPECT_EQ(fixture.stats().candidate.environmentGpuBytes,0u);
    EXPECT_EQ(fixture.stats().retiring.environmentGpuBytes,0u);
}

TEST_F(FixtureGPU, DeduplicatesAdmittedAssetsAndPreservesActiveOnCpuRejection) {
    const std::array duplicate{bundle,bundle};
    ASSERT_TRUE(fixture.beginCandidate(duplicate, error)) << error;
    EXPECT_EQ(fixture.stats().active.generation, 0u);
    EXPECT_EQ(fixture.stats().candidate.uniqueUploads, 1u);
    EXPECT_EQ(fixture.stats().candidate.assetGpuBytes, bundle->requestedGpuBytes());
    ASSERT_EQ(await([](Status value) { return value == Status::CandidateReady; }), Status::CandidateReady);
    ASSERT_TRUE(fixture.publishCandidate(error)) << error;
    const auto generation = fixture.stats().active.generation;
    EXPECT_FALSE(fixture.beginCandidate({}, error));
    std::array<std::shared_ptr<const Bundle>, 1> nulls{};
    EXPECT_FALSE(fixture.beginCandidate(nulls, error));
    std::vector<std::shared_ptr<const Bundle>> tooMany(SalvageAssetFixture::maximumBundles+1, bundle);
    EXPECT_FALSE(fixture.beginCandidate(tooMany, error));
    Package altered;
    altered.changeMesh([](auto& mesh) { mesh.materials[0].baseColorFactor[0] = .25f; });
    const std::array conflicting{bundle, altered.admit()};
    EXPECT_FALSE(fixture.beginCandidate(conflicting, error));
    EXPECT_NE(error.find("conflicting"), std::string::npos) << error;
    EXPECT_EQ(fixture.stats().active.generation, generation);
    EXPECT_EQ(fixture.stats().candidate.generation, 0u);
}

TEST_F(FixtureGPU, GuideToggleSharesOwnerAndRespectsSeparateGuideBudget) {
    begin();
    const auto resident=fixture.stats().active;
    const std::array placements{SalvageFixturePlacement{0,probeLod,glm::dmat4(1),{}}};
    auto value=frame();
    value.guides=InspectionGuides::Sockets;
    startFrame();
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedGuideBoxes,bundle->sidecar().part.sockets.size()*15u);
    EXPECT_GT(fixture.stats().lastEncodedDraws,0u);
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.generation,resident.generation);
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,resident.reservedGpuBytes);
    releaseCommands();
    startFrame();
    const std::vector<SalvageFixturePlacement> maximumPlacements(32,placements[0]);
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,maximumPlacements,value,ticket,error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedGuideBoxes,480u);
    submit(ticket);
    startFrame();
    value.guides=InspectionGuides::Off;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedGuideBoxes,0u);
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.uniqueUploads,resident.uniqueUploads);
}

TEST_F(FixtureGPU, FullSceneAndPaletteFitWithoutIncreasingMeshInstanceBudget) {
    Package package;
    package.changeMesh([](auto& mesh) {
        const auto node=*std::find_if(mesh.nodes.begin(),mesh.nodes.end(),[](const auto& value){return value.meshIndex!=UINT32_MAX;});
        mesh.nodes.assign(1,node);mesh.nodes.front().parent=-1;mesh.header.nodeCount=1;
    });
    const std::array bundles{package.admit()};begin(bundles);startFrame();
    std::vector<SalvageFixturePlacement> placements(SalvageAssetFixture::maximumPlacements);
    for(auto& placement:placements)placement.lodId=probeLod;
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,frame(),ticket,error))<<error;
    EXPECT_EQ(fixture.stats().lastEncodedDraws,SalvageAssetFixture::maximumPlacements);
    submit(ticket);
}

TEST_F(FixtureGPU, PrototypePartsShareAccountedUploadAndUseTheSameFrameLifetime) {
    const auto beam=game::construction::makeStarterCatalogDraft().definitions[0];
    const std::array bundles{bundle}; const std::array prototypes{beam};
    ASSERT_TRUE(fixture.beginCandidate(bundles,error,prototypes)) << error;
    ASSERT_EQ(await([](Status value){return value==Status::CandidateReady;}),Status::CandidateReady);
    ASSERT_TRUE(fixture.publishCandidate(error));
    const auto resident=fixture.stats().active;
    EXPECT_EQ(resident.uniqueUploads,1u); EXPECT_EQ(resident.prototypeUploads,1u);
    EXPECT_EQ(resident.assetGpuBytes,bundle->requestedGpuBytes()+1936u);
    const std::array selected{game::construction::SocketId{101},game::construction::SocketId{107}};
    const std::array placements{
        SalvageFixturePlacement{.bundleIndex=0,.lodId=probeLod},
        SalvageFixturePlacement{.bundleIndex=0,.placement={{0,48,-50},{}},.prototype=true,.selectedSockets=selected},
        SalvageFixturePlacement{.bundleIndex=0,.placement={{0,48,50},{}},.prototype=true,.selectedSockets=selected}};
    auto value=frame();value.guides=InspectionGuides::Sockets;
    startFrame();SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedGuideBoxes,60u+bundle->sidecar().part.sockets.size()*15u);
    submit(ticket);releaseCommands();
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,resident.reservedGpuBytes);
    const std::array duplicate{beam,beam};
    EXPECT_FALSE(fixture.beginCandidate(bundles,error,duplicate));
    EXPECT_EQ(fixture.stats().active.generation,resident.generation);
    startFrame();auto invalid=placements;invalid[1].lodId=1;ticket={};
    EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,invalid,value,ticket,error));
    EXPECT_EQ(ticket.serial,0u);EXPECT_EQ(fixture.stats().unresolved.serial,0u);
}

TEST_F(FixtureGPU, SocketPathUsesItsOwnReservedCapacityAndRejectsOverflowAtomically) {
    // A connected kit has 34 socket ends (510 helper boxes). They must fit
    // alongside the models in the two separately allocated instance buffers.
    const auto beam=game::construction::makeStarterCatalogDraft().definitions[0];
    const std::array bundles{bundle}; const std::array prototypes{beam};
    ASSERT_TRUE(fixture.beginCandidate(bundles,error,prototypes)) << error;
    ASSERT_EQ(await([](Status value){return value==Status::CandidateReady;}),Status::CandidateReady);
    ASSERT_TRUE(fixture.publishCandidate(error));
    const auto resident=fixture.stats().active;
    const std::array selected{game::construction::SocketId{101},game::construction::SocketId{107}};
    const SalvageFixturePlacement placement{.bundleIndex=0,.prototype=true,.selectedSockets=selected};
    std::vector<SalvageFixturePlacement> placements(17,placement);
    auto value=frame();value.guides=InspectionGuides::Sockets;
    startFrame();SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedGuideBoxes,510u);
    EXPECT_EQ(fixture.stats().lastEncodedDraws,527u);
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,resident.reservedGpuBytes);
    EXPECT_EQ(fixture.stats().active.generation,resident.generation);
    startFrame();placements.push_back(placement);ticket={91,92};
    EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error));
    EXPECT_NE(error.find("capacity"),std::string::npos) << error;
    EXPECT_EQ(ticket,(SalvageFixtureTicket{91,92}));
    EXPECT_EQ(fixture.stats().unresolved.serial,0u);
    placements.pop_back();
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error)) << error;
    submit(ticket);
}

TEST_F(FixtureGPU, AppliesOwnerCombinedAndUniqueAssetCeilingsBeforeUpload) {
    fixture.shutdown();
    SalvageFixtureConfig config;
    config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    config.maximumOwnerGpuBytes = bundle->requestedGpuBytes() + SalvageAssetFixture::fixedGpuReservationBytes - 1u;
    ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
    const std::array bundles{bundle};
    EXPECT_FALSE(fixture.beginCandidate(bundles, error));
    EXPECT_NE(error.find("per-owner"), std::string::npos);
    EXPECT_EQ(fixture.stats().candidate.uniqueUploads, 0u);
    fixture.shutdown();
    ++config.maximumOwnerGpuBytes;
    config.maximumResidentGpuBytes = config.maximumOwnerGpuBytes;
    ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
    begin();
    EXPECT_FALSE(fixture.beginCandidate(bundles, error));
    EXPECT_NE(error.find("combined"), std::string::npos);
    fixture.shutdown();
    config = {}; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
    std::vector<std::shared_ptr<const Bundle>> many;
    for (uint64_t i = 0; i <= SalvageAssetFixture::maximumUniqueAssets/4; ++i) {
        Package package; package.multipleLods(1000u + i * 4u, 4);
        many.push_back(package.admit());
    }
    EXPECT_FALSE(fixture.beginCandidate(many, error));
    EXPECT_NE(error.find("unique asset"), std::string::npos) << error;
    many.pop_back();
    begin(many);
    EXPECT_EQ(fixture.stats().active.uniqueUploads, SalvageAssetFixture::maximumUniqueAssets);
}

TEST_F(FixtureGPU, TracksRealSubmitDiscardResetAndReplacementTickets) {
    begin();
    const auto initial = fixture.stats().active;
    const std::array bundles{bundle};
    ASSERT_TRUE(fixture.beginCandidate(bundles, error)) << error;
    startFrame();
    const std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error)) << error;
    EXPECT_GT(fixture.stats().lastEncodedDraws, 0u);
    EXPECT_EQ(fixture.stats().active.lastSubmittedSerial, 0u);
    EXPECT_FALSE(fixture.publishCandidate(error));
    EXPECT_FALSE(fixture.requestLeave(error));
    EXPECT_FALSE(fixture.resetInstances(error));
    EXPECT_FALSE(fixture.setSceneViews(nullptr, nullptr, error));
    SalvageFixtureTicket unchanged{70,90};
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, {}, frame(), unchanged, error));
    EXPECT_EQ(unchanged, (SalvageFixtureTicket{70,90}));
    EXPECT_FALSE(fixture.submitted({ticket.generation + 1u,ticket.serial}, error));
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.lastSubmittedSerial, ticket.serial);
    EXPECT_FALSE(fixture.submitted(ticket, error));
    EXPECT_FALSE(fixture.discarded(ticket, error));
    ASSERT_EQ(await([](Status value) { return value == Status::CandidateReady; }), Status::CandidateReady);
    ASSERT_TRUE(fixture.publishCandidate(error)) << error;
    EXPECT_GT(fixture.stats().active.generation, initial.generation);
    ASSERT_TRUE(fixture.resetInstances(error)) << error;
    const auto reset = fixture.stats().active;
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, {}, frame(), ticket, error)) << error;
    EXPECT_EQ(fixture.stats().lastEncodedDraws, 0u);
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.uniqueUploads, reset.uniqueUploads);
    EXPECT_EQ(fixture.stats().active.generation, reset.generation);
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error)) << error;
    const auto submittedSerial = fixture.stats().active.lastSubmittedSerial;
    releaseCommands();
    ASSERT_TRUE(fixture.discarded(ticket, error)) << error;
    EXPECT_EQ(fixture.stats().active.lastSubmittedSerial, submittedSerial);
    EXPECT_EQ(fixture.stats().lastEncodedDraws, 0u);
    EXPECT_FALSE(fixture.discarded(ticket, error));
    ASSERT_TRUE(fixture.requestLeave(error)) << error;
    EXPECT_EQ(await([](Status value) { return value == Status::Drained; }), Status::Drained);
    EXPECT_EQ(fixture.stats().active.generation, 0u);
    EXPECT_EQ(fixture.stats().retiring.generation, 0u);
}

TEST_F(FixtureGPU, RejectsInvalidStableLodsFramesAndInstanceExpansionAtomically) {
    Package package;
    package.changeMesh([](auto& mesh) {
        const auto node = *std::find_if(mesh.nodes.begin(), mesh.nodes.end(), [](const auto& value) { return value.meshIndex != UINT32_MAX; });
        mesh.nodes.assign(130, node);
        for (auto& value : mesh.nodes) value.parent = -1;
        mesh.header.nodeCount = 130;
    });
    const std::array bundles{package.admit()};
    begin(bundles);
    startFrame();
    SalvageFixtureTicket ticket{7,9};
    std::array placements{SalvageFixturePlacement{.lodId=probeLod},SalvageFixturePlacement{.lodId=probeLod}};
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error));
    EXPECT_NE(error.find("instance ceiling"), std::string::npos) << error;
    EXPECT_EQ(ticket, (SalvageFixtureTicket{7,9}));
    placements[0].lodId = 0;
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, std::span(placements).first(1), frame(), ticket, error));
    EXPECT_NE(error.find("stable LOD"), std::string::npos) << error;
    auto invalid = frame(); invalid.lighting.direction = glm::vec3(0);
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, {}, invalid, ticket, error));
    invalid = frame(); invalid.width = 8193;
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, {}, invalid, ticket, error));
    invalid = frame(); invalid.useRayDepth = true;
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, {}, invalid, ticket, error));
    std::vector<SalvageFixturePlacement> tooMany(SalvageAssetFixture::maximumPlacements+1);
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, tooMany, frame(), ticket, error));
    EXPECT_EQ(fixture.stats().unresolved.serial, 0u);
    releaseCommands();
}

TEST_F(FixtureGPU, CapturesRealPipelineValidationFailureAndKeepsPreviousGeneration) {
    char name[] = "/tmp/voxys-fixture-shader-XXXXXX";
    ASSERT_NE(::mkdtemp(name), nullptr);
    temporary = name;
    const auto shader = temporary / "fixture.wgsl";
    std::filesystem::copy_file("shaders/mesh_path.wgsl", shader);
    fixture.shutdown();
    SalvageFixtureConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm; config.shaderPath = shader;
    ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
    begin();
    const auto generation = fixture.stats().active.generation;
    { std::ofstream output(shader); output << "this is deliberately invalid WGSL"; }
    const std::array bundles{bundle};
    // Error handles may be nonnull; only the real scope callback decides.
    static_cast<void>(fixture.beginCandidate(bundles, error));
    EXPECT_EQ(await([](Status value) { return value == Status::CandidateRejected; }), Status::CandidateRejected) << fixture.lastError();
    EXPECT_FALSE(fixture.publishCandidate(error));
    EXPECT_EQ(fixture.stats().active.generation, generation);
    EXPECT_FALSE(fixture.lastError().empty());
    startFrame();
    const std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error)) << error;
    EXPECT_GT(fixture.stats().lastEncodedDraws, 0u);
    submit(ticket);
}

TEST_F(FixtureGPU, DrainsNeverDrawnCandidatesAndReentersWithoutInventedFrames) {
    ASSERT_TRUE(fixture.requestLeave(error));
    EXPECT_EQ(fixture.poll(), Status::Drained);
    for (uint32_t cycle = 0; cycle < 4; ++cycle) {
        fixture.shutdown();
        SalvageFixtureConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
        ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
        const std::array bundles{bundle};
        ASSERT_TRUE(fixture.beginCandidate(bundles, error)) << error;
        EXPECT_EQ(fixture.stats().candidate.lastSubmittedSerial, 0u);
        if (cycle == 0) {
            // Callback payloads outlive the module, but contain no GPU owners.
            fixture.shutdown();
            pump();
            EXPECT_EQ(fixture.poll(), Status::Uninitialized);
        } else {
            ASSERT_TRUE(fixture.requestLeave(error)) << error;
            EXPECT_FALSE(fixture.publishCandidate(error));
            EXPECT_EQ(await([](Status value) { return value == Status::Drained; }), Status::Drained);
        }
    }
}

TEST_F(FixtureGPU, ReentryRejectsOldTicketsAndExceptionalShutdownPreservesCommands) {
    begin();
    startFrame();
    const std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    SalvageFixtureTicket oldTicket;
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), oldTicket, error));
    releaseCommands();
    ASSERT_TRUE(fixture.discarded(oldTicket, error));
    ASSERT_TRUE(fixture.requestLeave(error));
    ASSERT_EQ(await([](Status value) { return value == Status::Drained; }), Status::Drained);
    fixture.shutdown();
    SalvageFixtureConfig config; config.colorFormat = WGPUTextureFormat_RGBA8Unorm;
    ASSERT_TRUE(fixture.init(context.getDevice(), context.getQueue(), config, error));
    begin();
    startFrame();
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error));
    EXPECT_GT(ticket.generation, oldTicket.generation);
    EXPECT_GT(ticket.serial, oldTicket.serial);
    EXPECT_FALSE(fixture.submitted(oldTicket, error));
    EXPECT_FALSE(fixture.discarded(oldTicket, error));
    WGPUCommandBufferDescriptor descriptor{};
    command = wgpuCommandEncoderFinish(encoder, &descriptor);
    ASSERT_NE(command, nullptr);
    auto failed = std::make_shared<std::atomic<bool>>(false);
    context.setErrorCallback([failed](WGPUErrorType, const char*) { failed->store(true); });
    // Final shutdown is allowed with an unresolved ticket, but must release
    // refs without Destroy and must never report a successful drained Leave.
    fixture.shutdown();
    EXPECT_EQ(fixture.poll(), Status::Uninitialized);
    wgpuQueueSubmit(context.getQueue(), 1, &command);
    static_cast<void>(wgpuDevicePoll(context.getDevice(), true, nullptr));
    EXPECT_FALSE(failed->load());
    releaseCommands();
}

TEST_F(FixtureGPU, ValidatesReboundViewsAndSurfacesInvalidViewAsTerminal) {
    begin();
    startFrame(); releaseCommands();
    ASSERT_TRUE(fixture.setSceneViews(colorView, nullptr, error)) << error;
    EXPECT_EQ(await([](Status value) { return value != Status::ViewsValidating; }), Status::Active) << fixture.lastError();
    ASSERT_TRUE(fixture.setSceneViews(nullptr, nullptr, error)) << error;
    EXPECT_EQ(await([](Status value) { return value != Status::ViewsValidating; }), Status::Active) << fixture.lastError();
    startFrame();
    const std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder, colorView, depthView, placements, frame(), ticket, error));
    submit(ticket);
    // A depth view cannot satisfy the filtering float environment binding.
    static_cast<void>(fixture.setSceneViews(depthView, nullptr, error));
    EXPECT_EQ(await([](Status value) { return value == Status::Fatal; }), Status::Fatal);
    EXPECT_FALSE(fixture.requestLeave(error));
    EXPECT_FALSE(fixture.beginCandidate(std::array{bundle}, error));
    EXPECT_FALSE(fixture.lastError().empty());
}

TEST_F(FixtureGPU, ForwardedDeviceLossDoesNotBecomeAnEndlessLeave) {
    const std::array bundles{bundle};
    ASSERT_TRUE(fixture.beginCandidate(bundles, error));
    // wgpu-native v22.1.0.5 implements DeviceDestroy as a no-op. This tests
    // the explicit platform-event boundary and actual pending-resource release,
    // not a real native device loss. Browser device.destroy proof remains open.
    fixture.notifyDeviceLost();
    EXPECT_EQ(fixture.poll(), Status::Fatal);
    EXPECT_FALSE(fixture.lastError().empty());
    EXPECT_FALSE(fixture.requestLeave(error));
    fixture.shutdown();
    pump();
    EXPECT_EQ(fixture.poll(), Status::Uninitialized);
}
#endif
} // namespace
} // namespace voxy::render

#include "render/salvage_asset_fixture.hpp"
#include "game/assets/fixture_registry.hpp"
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
#include <limits>
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
    void setPartName(std::string_view name) {
        auto metadata=Json::parse(files["gameplay.json"]);
        metadata["part"]["name_key"]=name;
        std::string error;
        const auto normalized=game::assets::parseGameplaySidecar(metadata.dump(),[](const auto&){return true;},error);
        if(!normalized)throw std::runtime_error(error);
        files["gameplay.json"]=bytes(normalized->normalizedJson);
        auto manifest=Json::parse(files["cook-manifest.json"]);
        manifest["normalized_sidecar"]["bytes"]=files["gameplay.json"].size();
        manifest["normalized_sidecar"]["sha256"]=hash(files["gameplay.json"]);
        files["cook-manifest.json"]=bytes(manifest.dump(2)+"\n");
        selection.manifestSha256=hash(files["cook-manifest.json"]);
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

TEST(SalvageFixtureAccounting, PaintFitsBothFullDrawPathsWithinUnchangedReservation) {
    EXPECT_EQ(MeshPath::gpuInstanceBytes,112u);
    EXPECT_EQ(SalvageAssetFixture::maximumExpandedDraws,512u);
    EXPECT_EQ(SalvageAssetFixture::maximumMeshInstances,256u);
    EXPECT_EQ(SalvageAssetFixture::fixedGpuReservationBytes,128u*1024u);
    // The ABI assertion in MeshPath ties this value to the allocation stride.
    // Account for both model and X-ray buffers, including all 512 entries in
    // each, rather than charging only visible or currently painted instances.
    EXPECT_EQ(2u * SalvageAssetFixture::maximumExpandedDraws * (MeshPath::gpuInstanceBytes-96u),16384u);
    // Each path loads its own helper mesh (vertices, indices and material),
    // even though both uploads originate from the same CPU data.
    EXPECT_EQ(SalvageAssetFixture::fixedGpuRequestedBytes,119336u);
    EXPECT_EQ(SalvageAssetFixture::fixedGpuRequestedBytes,102952u+16384u);
    EXPECT_LE(SalvageAssetFixture::fixedGpuRequestedBytes,SalvageAssetFixture::fixedGpuReservationBytes);
    EXPECT_EQ(SalvageFixtureConfig{}.maximumOwnerGpuBytes,16u*1024u*1024u);
    EXPECT_EQ(SalvageFixtureConfig{}.maximumResidentGpuBytes,48u*1024u*1024u);
    EXPECT_EQ(SalvageFixturePlacement{}.baseColorOverride,glm::vec4(0));
}

TEST(CoveDockMarkings, InstalledStaticPanelsKeepSocketsChannelsAndGeneratorClear) {
    std::string error;
    const auto source=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(source)<<error;
    const auto digest=source->installedRegistryDigest;
    CoveDockMarkings marks;
    ASSERT_TRUE(makeCoveDockMarkings(*source,marks,error))<<error;
    EXPECT_EQ(source->installedRegistryDigest,digest);
    EXPECT_EQ(marks.dockPlacements,(std::vector<uint32_t>{11,12,13,14,15,16,17,18}));
    ASSERT_EQ(marks.mesh.materials.size(),2u);
    EXPECT_EQ(marks.prefab.counts.meshInstances,1u);
    EXPECT_EQ(marks.prefab.counts.expandedDraws,2u);
    EXPECT_EQ(marks.prefab.counts.textureCount,0u);
    EXPECT_EQ(marks.prefab.counts.gpuBytes,marks.mesh.vertices.size()+marks.mesh.indices.size()+2u*64u);
    EXPECT_LE(marks.prefab.counts.gpuBytes,16u*1024u);
    for (const auto& m:marks.mesh.materials) {
        EXPECT_EQ(m.unlit,0); EXPECT_EQ(m.alphaMode,moto::VmeshAlphaOpaque);
        EXPECT_FLOAT_EQ(m.baseColorFactor[3],1); EXPECT_FLOAT_EQ(m.metallicFactor,0);
    }
    ASSERT_EQ(marks.mesh.header.indexCount%3,0u);
    for (size_t t=0;t<marks.mesh.header.indexCount;t+=3) {
        std::array<uint32_t,3> indices{};
        std::memcpy(indices.data(),marks.mesh.indices.data()+t*4,12);
        std::array<glm::dvec3,3> points{};
        glm::dvec3 low(1e9),high(-1e9);
        for (size_t c=0;c<3;++c) {
            moto::VmeshVertex v{};
            std::memcpy(&v,marks.mesh.vertices.data()+indices[c]*sizeof(v),sizeof(v));
            points[c]={v.position[0],v.position[1],v.position[2]};
            low=glm::min(low,points[c]); high=glm::max(high,points[c]);
            EXPECT_NEAR(v.position[1],1.2815,1e-6);
            EXPECT_FLOAT_EQ(v.normal[1],1);
        }
        EXPECT_GT(glm::cross(points[1]-points[0],points[2]-points[0]).y,1e-9)<<t;
        // Independent all-LOD cooked-surface contract: complete triangles fit
        // one flat 0.974 x 0.970 m panel, not just their centroids.
        bool onFlat=false;
        for (double centerX:{4.5,5.5,6.5,7.5}) for (int row=0;row<16;++row) {
            const double centerZ=-57.5+row;
            onFlat |= low.x>=centerX-.487-1e-5 && high.x<=centerX+.487+1e-5
                && low.z>=centerZ-.485-1e-5 && high.z<=centerZ+.485+1e-5;
        }
        EXPECT_TRUE(onFlat)<<t;
        for (double socketX:{4.5,5.5,6.5,7.5}) for (int row=0;row<8;++row) {
            const double socketZ=-57+row*2;
            const bool overlap=high.x>socketX-.32+1e-5 && low.x<socketX+.32-1e-5
                && high.z>socketZ-.32+1e-5 && low.z<socketZ+.32-1e-5;
            EXPECT_FALSE(overlap)<<t;
        }
        EXPECT_FALSE(high.x>5.08 && low.x<6.92 && high.z>-54.32 && low.z<-52.68)<<t;
    }
}

TEST(CoveDockMarkings, MovingBoatAndCargoCannotMoveStaticMarkingsAndUnsafeRouteRefusesAtomically) {
    std::string error;
    const auto source=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(source)<<error;
    CoveDockMarkings original;
    ASSERT_TRUE(makeCoveDockMarkings(*source,original,error))<<error;
    game::assets::LoadedAssetFixture moved;
    moved.registry=source->registry; moved.bundles=source->bundles;
    for (const auto index:moved.registry.navigation->boatPlacements) moved.registry.placements[index].placement.translation.x+=10000;
    for (const auto index:moved.registry.navigation->cargoPlacements) moved.registry.placements[index].placement.translation={300,96,-2500};
    CoveDockMarkings marks;
    ASSERT_TRUE(makeCoveDockMarkings(moved,marks,error))<<error;
    EXPECT_EQ(marks.mesh.vertices,original.mesh.vertices);
    EXPECT_EQ(marks.mesh.indices,original.mesh.indices);
    // The installed obstacle now occupies the lane; it may never be clipped
    // away while retaining a misleading navigation cue through its collider.
    moved.registry.placements[23].placement.translation.x=225;
    EXPECT_FALSE(makeCoveDockMarkings(moved,marks,error));
    EXPECT_EQ(marks.mesh.vertices,original.mesh.vertices);
    moved.registry=source->registry;
    moved.registry.placements[14].placement.translation.z+=2000;
    EXPECT_FALSE(makeCoveDockMarkings(moved,marks,error));
    EXPECT_EQ(marks.mesh.indices,original.mesh.indices);
    moved.registry=source->registry;
    moved.registry.placements[14].placement.rotation={6};
    EXPECT_FALSE(makeCoveDockMarkings(moved,marks,error));
    moved.registry=source->registry;
    moved.registry.navigation->spawn.y=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(makeCoveDockMarkings(moved,marks,error));
}

TEST(CoveDockMarkings, AdmissionRecountsExactStorageAndRejectsUnlitOrNonIdentityMesh) {
    std::string error;
    const auto source=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(source)<<error;
    CoveDockMarkings marks;
    ASSERT_TRUE(makeCoveDockMarkings(*source,marks,error))<<error;
    const auto bytes=marks.prefab.counts.gpuBytes;
    marks.prefab.counts.gpuBytes=0;
    ASSERT_TRUE(prepareCoveDockMarkingMesh(marks.mesh,marks.prefab,error))<<error;
    EXPECT_EQ(marks.prefab.counts.gpuBytes,bytes);
    marks.mesh.materials[0].unlit=1;
    EXPECT_FALSE(prepareCoveDockMarkingMesh(marks.mesh,marks.prefab,error));
    EXPECT_EQ(marks.prefab.counts.gpuBytes,bytes);
    marks.mesh.materials[0].unlit=0;
    marks.mesh.nodes[0].translation[0]=1;
    EXPECT_FALSE(prepareCoveDockMarkingMesh(marks.mesh,marks.prefab,error));
    marks.mesh.nodes[0].translation[0]=0;
    // Actual double absolute-to-camera-sector bridge at a nonzero far origin.
    const glm::dvec3 sector(1000000,-2000000,3);
    const glm::dvec3 origin=sector*256.0+glm::dvec3(3,0,2);
    const auto root=glm::translate(glm::dmat4(1),origin-sector*256.0);
    std::vector<game::assets::RigidPrefabDraw> placed;
    ASSERT_TRUE(game::assets::placeRigidPrefab(marks.prefab,root,{},1,placed,error))<<error;
    ASSERT_EQ(placed.size(),1u);
    EXPECT_EQ(glm::vec3(placed[0].modelMatrix[3]),glm::vec3(3,0,2));
    EXPECT_EQ(MeshPath::gpuInstanceBytes,112u);
    EXPECT_EQ(SalvageAssetFixture::fixedGpuRequestedBytes,119336u);
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
    void startFrame(bool allowReadback=false) {
        if (!color) {
            auto colorDesc=gpu::TextureDesc::renderTarget(64,64,WGPUTextureFormat_RGBA8Unorm);
            auto depthDesc=gpu::TextureDesc::depth(64,64,WGPUTextureFormat_Depth32Float);
            if (allowReadback) {colorDesc.usage|=WGPUTextureUsage_CopySrc;depthDesc.usage|=WGPUTextureUsage_CopySrc;}
            color = gpu::createTexture(context.getDevice(), colorDesc);
            depth = gpu::createTexture(context.getDevice(), depthDesc);
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
    struct NumericFrame {
        std::array<uint8_t,64u*64u*4u> rgba{};
        std::array<float,64u*64u> depth{};
    };
    // Test-only numeric readback of the already encoded frame. No capture path
    // or image writer exists; copies and scene draw share one submitted ticket.
    bool submitNumeric(SalvageFixtureTicket ticket, NumericFrame& output) {
        constexpr uint64_t planeBytes=64u*64u*4u,totalBytes=2u*planeBytes;
        struct Readback {
            WGPUBuffer buffer=nullptr;
            ~Readback(){if(buffer){wgpuBufferDestroy(buffer);wgpuBufferRelease(buffer);}}
        } readback;
        readback.buffer=gpu::createBuffer(context.getDevice(),gpu::BufferDesc{
            .label="dock numeric color/depth",.size=totalBytes,
            .usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead});
        if(!readback.buffer)return false;
        for(int plane=0;plane<2;++plane) {
            gpu::CompatImageCopyTexture source{};
            source.texture=plane==0?color:depth;
            source.aspect=plane==0?WGPUTextureAspect_All:WGPUTextureAspect_DepthOnly;
            WGPUImageCopyBuffer destination{};
            destination.buffer=readback.buffer;
            destination.layout.offset=plane==0?0:planeBytes;
            destination.layout.bytesPerRow=256;destination.layout.rowsPerImage=64;
            const WGPUExtent3D extent{64,64,1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&destination,&extent);
        }
        submit(ticket);
        if(HasFatalFailure())return false;
        auto state=std::make_shared<std::atomic<int>>(0);
        using Payload=std::shared_ptr<std::atomic<int>>;
        wgpuBufferMapAsync(readback.buffer,WGPUMapMode_Read,0,totalBytes,
            [](WGPUBufferMapAsyncStatus status,void* userdata){
                const std::unique_ptr<Payload> payload(static_cast<Payload*>(userdata));
                (*payload)->store(status==WGPUBufferMapAsyncStatus_Success?1:2,std::memory_order_release);
            },new Payload(state));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(state->load(std::memory_order_acquire)==0 && std::chrono::steady_clock::now()<deadline) {
            pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(state->load(std::memory_order_acquire)!=1)return false;
        const auto* bytes=static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(readback.buffer,0,totalBytes));
        if(!bytes){wgpuBufferUnmap(readback.buffer);return false;}
        std::memcpy(output.rgba.data(),bytes,planeBytes);
        std::memcpy(output.depth.data(),bytes+planeBytes,planeBytes);
        wgpuBufferUnmap(readback.buffer);
        return true;
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

TEST_F(FixtureGPU, DockMarkingsUseExactOwnerChargeSharedDrawTicketAndRetirement) {
    const auto source=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(source)<<error;
    CoveDockMarkings marks;
    ASSERT_TRUE(makeCoveDockMarkings(*source,marks,error))<<error;
    // Use the real admitted plate underneath the inlay, not an empty backdrop.
    bundle=source->bundles[2];
    std::vector<SalvageFixturePlacement> dock;
    for(const auto index:marks.dockPlacements)
        dock.push_back({0,bundle->lods().front().id,glm::dmat4(1),source->registry.placements[index].placement});
    fixture.shutdown();
    SalvageFixtureConfig config; config.colorFormat=WGPUTextureFormat_RGBA8Unorm;
    config.maximumOwnerGpuBytes=SalvageAssetFixture::fixedGpuReservationBytes
        +bundle->requestedGpuBytes()+marks.prefab.counts.gpuBytes-1u;
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error))<<error;
    EXPECT_FALSE(fixture.beginCandidate(std::array{bundle},error,{},&marks));
    EXPECT_EQ(fixture.stats().candidate.generation,0u);
    EXPECT_EQ(fixture.stats().candidate.assetGpuBytes,0u);
    fixture.shutdown();
    config.maximumOwnerGpuBytes=16u*1024u*1024u;
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error))<<error;
    begin();
    const auto original=fixture.stats().active;
    auto value=frame(); value.dockMarkingsRoot=glm::dmat4(1);
    value.cameraPosition={5,8,-51.5f};
    value.view=glm::lookAtLH(value.cameraPosition,glm::vec3(5,1.28f,-51.5f),glm::vec3(0,0,-1));
    value.projection=glm::orthoLH_ZO(-2.5f,2.5f,-3.0f,3.0f,.1f,20.0f);
    value.lighting.direction={0,1,0};value.lighting.sunColor={1,1,1};value.lighting.sunIntensity=.8f;
    value.lighting.ambientColor={.25f,.25f,.25f};value.lighting.ambientIntensity=1;
    value.lighting.fogDensity=0;
    startFrame(true); SalvageFixtureTicket ticket;
    EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error));
    EXPECT_EQ(ticket.serial,0u); EXPECT_EQ(fixture.stats().unresolved.serial,0u);
    releaseCommands();
    value.dockMarkingsRoot.reset();
    NumericFrame baseline,visible,hidden;
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error))<<error;
    // This fixed camera sees four of the eight dock plates. Compare against
    // its actual culled baseline, not every admitted offscreen placement.
    const auto baseDraws=fixture.stats().lastEncodedDraws;
    EXPECT_GT(baseDraws,0u);
    EXPECT_LE(baseDraws,bundle->lods().front().prefab.counts.expandedDraws*dock.size());
    ASSERT_TRUE(submitNumeric(ticket,baseline));
    const std::array bundles{bundle};
    ASSERT_TRUE(fixture.beginCandidate(bundles,error,{},&marks))<<error;
    EXPECT_EQ(fixture.stats().candidate.reservedGpuBytes,original.reservedGpuBytes+marks.prefab.counts.gpuBytes);
    EXPECT_EQ(fixture.stats().candidate.assetGpuBytes,original.assetGpuBytes+marks.prefab.counts.gpuBytes);
    EXPECT_EQ(fixture.stats().candidate.dockMarkingGpuBytes,marks.prefab.counts.gpuBytes);
    EXPECT_EQ(fixture.stats().candidate.uniqueUploads,original.uniqueUploads);
    ASSERT_EQ(await([](Status s){return s==Status::CandidateReady;}),Status::CandidateReady)<<fixture.lastError();
    ASSERT_TRUE(fixture.publishCandidate(error))<<error;
    value.dockMarkingsRoot=glm::dmat4(1);
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error))<<error;
    EXPECT_EQ(fixture.stats().lastEncodedDraws,baseDraws+marks.prefab.counts.expandedDraws);
    EXPECT_EQ(fixture.stats().lastEncodedDockMarkingDraws,2u);
    EXPECT_EQ(fixture.stats().lastSubmittedDockMarkingDraws,0u);
    EXPECT_FALSE(fixture.requestLeave(error));
    ASSERT_TRUE(submitNumeric(ticket,visible));
    EXPECT_EQ(fixture.stats().lastSubmittedDraws,baseDraws+2u);
    EXPECT_EQ(fixture.stats().lastSubmittedDockMarkingDraws,2u);
    uint32_t changed=0,teal=0,orange=0,stable=0;
    for(size_t pixel=0;pixel<64u*64u;++pixel) {
        const auto byte=pixel*4u;
        const bool colorChanged=std::memcmp(visible.rgba.data()+byte,baseline.rgba.data()+byte,4)!=0;
        if(colorChanged) {
            ++changed;
            const int r=visible.rgba[byte],g=visible.rgba[byte+1],b=visible.rgba[byte+2];
            // Hue ordering survives the renderer's ACES tone mapping; do not
            // assume saturated primaries remain exact encoded RGB channels.
            teal+=g>r+8 && b>r+8;
            orange+=r>g+8 && g>b+4;
            EXPECT_EQ(visible.rgba[byte+3],255u);
            EXPECT_LT(visible.depth[pixel],baseline.depth[pixel]-1e-6f)<<pixel;
            EXPECT_LT(baseline.depth[pixel],1.0f)<<pixel; // actual dock below it
        } else {
            ++stable;
            EXPECT_FLOAT_EQ(visible.depth[pixel],baseline.depth[pixel])<<pixel;
        }
    }
    EXPECT_GT(changed,20u);EXPECT_GT(teal,8u);EXPECT_GT(orange,8u);EXPECT_GT(stable,3500u);
    RecordProperty("dockChangedOpaquePixels",changed);
    RecordProperty("dockTealPixels",teal);RecordProperty("dockOrangePixels",orange);
    RecordProperty("dockMarkingGpuBytes",std::to_string(marks.prefab.counts.gpuBytes));
    // Encoding and discarding hidden workshop state does not advance the
    // submitted observation. An actual hidden submission restores baseline.
    value.dockMarkingsRoot.reset();
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error))<<error;
    EXPECT_EQ(fixture.stats().lastEncodedDockMarkingDraws,0u);
    EXPECT_EQ(fixture.stats().lastSubmittedDockMarkingDraws,2u);
    releaseCommands(); ASSERT_TRUE(fixture.discarded(ticket,error));
    EXPECT_EQ(fixture.stats().lastSubmittedDockMarkingDraws,2u);
    startFrame();
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error))<<error;
    ASSERT_TRUE(submitNumeric(ticket,hidden));
    EXPECT_EQ(fixture.stats().lastSubmittedDockMarkingDraws,0u);
    EXPECT_EQ(hidden.rgba,baseline.rgba);
    EXPECT_EQ(hidden.depth,baseline.depth);
    startFrame(); ticket={};
    value.dockMarkingsRoot=glm::scale(glm::dmat4(1),glm::dvec3(2));
    EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,dock,value,ticket,error));
    EXPECT_EQ(ticket.serial,0u); EXPECT_EQ(fixture.stats().unresolved.serial,0u);
    releaseCommands();
    ASSERT_TRUE(fixture.requestLeave(error));
    ASSERT_EQ(await([](Status s){return s==Status::Drained;}),Status::Drained);
    EXPECT_EQ(fixture.stats().active.dockMarkingGpuBytes,0u);
    EXPECT_EQ(fixture.stats().retiring.dockMarkingGpuBytes,0u);
}

TEST_F(FixtureGPU, FilteredEnvironmentAndSunShadowsAreChargedRetriedAndRetiredWithTheirGeneration) {
    fixture.shutdown();
    SalvageFixtureConfig config; config.colorFormat=WGPUTextureFormat_RGBA8Unorm;
    config.filteredEnvironment=true;
    config.sunShadows=true;
    EXPECT_EQ(MeshPath::sunShadowReservationBytes,
        uint64_t(MeshPath::sunShadowResolution)*MeshPath::sunShadowResolution*4u+96u);
    const uint64_t charge=SalvageAssetFixture::fixedGpuReservationBytes
        +MeshPath::filteredEnvironmentReservationBytes+MeshPath::sunShadowReservationBytes+bundle->requestedGpuBytes();
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

TEST_F(FixtureGPU, CoveToyArtFitsExistingOwnerAndDrawLimits) {
    fixture.shutdown();
    const auto base=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    const auto scene=game::assets::appendAssetFixtureCatalog(*base,std::filesystem::canonical("data/salvage/cove-workshop-r03.json"),error);
    ASSERT_TRUE(scene)<<error;
    SalvageFixtureConfig config;config.colorFormat=WGPUTextureFormat_RGBA8Unorm;
    config.filteredEnvironment=true;config.sunShadows=true;
    uint64_t requested=SalvageAssetFixture::fixedGpuReservationBytes
        +MeshPath::filteredEnvironmentReservationBytes+MeshPath::sunShadowReservationBytes;
    for(size_t i=0;i<scene->renderBundles().size();++i) {
        uint64_t bytes=0;
        for(const auto& lod:scene->renderBundles()[i]->lods())bytes+=lod.prefab.counts.gpuBytes;
        requested+=bytes;
        RecordProperty("bundleGpuBytes"+std::to_string(i),std::to_string(bytes));
    }
    RecordProperty("requestedOwnerGpuBytes",std::to_string(requested));
    ASSERT_LE(requested,config.maximumOwnerGpuBytes);
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error))<<error;
    ASSERT_TRUE(fixture.beginCandidate(scene->renderBundles(),error))<<error;
    ASSERT_EQ(await([](Status s){return s==Status::CandidateReady;}),Status::CandidateReady)<<fixture.lastError();
    ASSERT_TRUE(fixture.publishCandidate(error))<<error;
    const auto reserved=fixture.stats().active.reservedGpuBytes;
    EXPECT_LE(reserved,16ull*1024ull*1024ull);
    EXPECT_EQ(fixture.stats().active.uniqueUploads,36u);
    RecordProperty("ownerGpuBytes",std::to_string(reserved));
    std::vector<SalvageFixturePlacement> placements;
    for(const auto& p:scene->registry.placements)placements.push_back({p.bundleIndex,1,glm::dmat4(1),p.placement});
    auto value=frame();value.cameraPosition={4,8,-46};
    value.view=glm::lookAtLH(glm::vec3(4,8,-46),glm::vec3(1,0,-54),glm::vec3(0,1,0));
    startFrame();SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error))<<error;
    submit(ticket);
    EXPECT_LE(fixture.stats().lastSubmittedDraws,SalvageAssetFixture::maximumExpandedDraws);
    RecordProperty("colorDraws",std::to_string(fixture.stats().lastSubmittedDraws));
}

TEST_F(FixtureGPU, CoveMoldedMachineryFitsCurrentOwnerBudget) {
    fixture.shutdown();
    const auto base=game::assets::loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    const auto scene=game::assets::appendAssetFixtureCatalog(*base,std::filesystem::canonical("data/salvage/cove-workshop-r06.json"),error);
    ASSERT_TRUE(scene)<<error;
    SalvageFixtureConfig config;config.colorFormat=WGPUTextureFormat_RGBA8Unorm;
    config.filteredEnvironment=true;config.sunShadows=true;
    uint64_t requested=SalvageAssetFixture::fixedGpuReservationBytes
        +MeshPath::filteredEnvironmentReservationBytes+MeshPath::sunShadowReservationBytes;
    for(size_t i=0;i<scene->renderBundles().size();++i) {
        uint64_t bytes=0;
        for(const auto& lod:scene->renderBundles()[i]->lods())bytes+=lod.prefab.counts.gpuBytes;
        requested+=bytes;
        RecordProperty("bundleGpuBytes"+std::to_string(i),std::to_string(bytes));
    }
    RecordProperty("requestedOwnerGpuBytes",std::to_string(requested));
    ASSERT_LE(requested,config.maximumOwnerGpuBytes);
    ASSERT_TRUE(fixture.init(context.getDevice(),context.getQueue(),config,error))<<error;
    ASSERT_TRUE(fixture.beginCandidate(scene->renderBundles(),error))<<error;
    ASSERT_EQ(await([](Status s){return s==Status::CandidateReady;}),Status::CandidateReady)<<fixture.lastError();
    ASSERT_TRUE(fixture.publishCandidate(error))<<error;
    const auto reserved=fixture.stats().active.reservedGpuBytes;
    EXPECT_LE(reserved,16ull*1024ull*1024ull);
    EXPECT_EQ(reserved,requested);
    EXPECT_EQ(reserved,9724200u); // Independent cooked-byte audit, all nine presentations.
    EXPECT_EQ(fixture.stats().active.uniqueUploads,36u);
    RecordProperty("ownerGpuBytes",std::to_string(reserved));
    std::vector<SalvageFixturePlacement> placements;
    for(const auto& p:scene->registry.placements)placements.push_back({p.bundleIndex,1,glm::dmat4(1),p.placement});
    auto value=frame();value.cameraPosition={4,8,-46};
    value.view=glm::lookAtLH(glm::vec3(4,8,-46),glm::vec3(1,0,-54),glm::vec3(0,1,0));
    startFrame();SalvageFixtureTicket ticket;
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error))<<error;
    submit(ticket);
    EXPECT_LE(fixture.stats().lastSubmittedDraws,SalvageAssetFixture::maximumExpandedDraws);
    RecordProperty("colorDraws",std::to_string(fixture.stats().lastSubmittedDraws));

    releaseCommands();
    for(auto& placement:placements) {
        const auto& binding=scene->renderBundles()[placement.bundleIndex]->lods().front().prefab.mechanism;
        if(binding) placement.mechanism=game::assets::RigidMechanismPose{binding->kind,.75};
    }
    // The real articulated scene must retain the supported 64-large-brick
    // builder capacity and its three palette previews under existing caps.
    for(int i=0;i<64;++i)
        placements.push_back({11,1,glm::dmat4(1),{{(i%8)*250,100+(i/8)*48,-3000},{}}});
    for(uint32_t i=9;i<12;++i)placements.push_back({i,1,glm::dmat4(1),{{0,500,-3000},{}}});
    ASSERT_LE(placements.size(),SalvageAssetFixture::maximumPlacements);
    startFrame();ticket={};
    ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,value,ticket,error))<<error;
    submit(ticket);
    EXPECT_EQ(fixture.stats().active.reservedGpuBytes,reserved);
    EXPECT_LE(fixture.stats().lastSubmittedDraws,SalvageAssetFixture::maximumExpandedDraws);
    RecordProperty("with64BricksColorDraws",std::to_string(fixture.stats().lastSubmittedDraws));
    RecordProperty("with64BricksPlacements",std::to_string(placements.size()));
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
    invalid = frame(); invalid.shadowFrameWorldOrigin.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, {}, invalid, ticket, error));
    std::vector<SalvageFixturePlacement> tooMany(SalvageAssetFixture::maximumPlacements+1);
    EXPECT_FALSE(fixture.encode(encoder, colorView, depthView, tooMany, frame(), ticket, error));
    EXPECT_EQ(fixture.stats().unresolved.serial, 0u);
    releaseCommands();
}

TEST_F(FixtureGPU, RejectsInvalidPaintBeforeOpeningTicketAndAcceptsNeutralOrOpaqueColor) {
    begin();
    startFrame();
    SalvageFixtureTicket ticket{7,9};
    std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    const auto before = fixture.stats().active;
    for (const auto& invalid : std::array{
        glm::vec4(-0.01f,0,0,1),glm::vec4(0,1.01f,0,1),glm::vec4(0,0,0,0.5f),
        glm::vec4(0,0,std::numeric_limits<float>::infinity(),1),
        glm::vec4(0,0,0,std::numeric_limits<float>::quiet_NaN())}) {
        placements[0].baseColorOverride = invalid;
        EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,placements,frame(),ticket,error));
        EXPECT_NE(error.find("base color override"),std::string::npos) << error;
        EXPECT_EQ(ticket,(SalvageFixtureTicket{7,9}));
        EXPECT_EQ(fixture.stats().unresolved.serial,0u);
        EXPECT_EQ(fixture.stats().active.generation,before.generation);
        EXPECT_EQ(fixture.stats().active.reservedGpuBytes,before.reservedGpuBytes);
    }
    releaseCommands();
    for (const auto& valid : std::array{glm::vec4(0),glm::vec4(0.3f,0.6f,0.9f,0),
        opaqueSrgbPaintOverride({48,112,224,0})}) {
        placements[0].baseColorOverride = valid;
        startFrame();
        ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,frame(),ticket,error)) << error;
        EXPECT_GT(fixture.stats().lastEncodedDraws,0u);
        submit(ticket);
        EXPECT_EQ(fixture.stats().active.reservedGpuBytes,before.reservedGpuBytes);
    }
}

void makeMechanismProbe(Package& package) {
    package.setPartName("salvage.part.propeller");
    package.changeMesh([](auto& mesh) {
        mesh.nodes.assign(2,moto::VmeshNode{}); mesh.header.nodeCount=2;
        mesh.stringBlob.assign(1,'\0');
        const auto name=[&](std::string_view value){auto offset=static_cast<uint32_t>(mesh.stringBlob.size());
            mesh.stringBlob.append(value);mesh.stringBlob.push_back('\0');return offset;};
        mesh.nodes[0].nameOffset=name("voxys_mechanism_static");
        mesh.nodes[1].nameOffset=name("voxys_propeller_rotor");
        mesh.nodes[0].meshIndex=mesh.nodes[1].meshIndex=0;
        mesh.nodes[0].translation[0]=1.5f;
    });
}

TEST_F(FixtureGPU, NamedMechanismPhaseUsesOwnedTicketWithoutAdditionalGpuReservation) {
    Package package;makeMechanismProbe(package);bundle=package.admit();begin();
    const auto reserved=fixture.stats().active.reservedGpuBytes;
    std::array placements{SalvageFixturePlacement{.lodId=probeLod}};
    uint32_t drawCount=0;
    for(double angle:{0.,.7,-.7}) {
        placements[0].mechanism=game::assets::RigidMechanismPose{game::assets::RigidMechanismKind::PropellerRotor,angle};
        SalvageFixtureTicket ticket;startFrame();
        ASSERT_TRUE(fixture.encode(encoder,colorView,depthView,placements,frame(),ticket,error))<<error;
        if(drawCount==0)drawCount=fixture.stats().lastEncodedDraws;
        EXPECT_EQ(fixture.stats().lastEncodedDraws,drawCount);
        EXPECT_GT(drawCount,1u);submit(ticket);
        EXPECT_EQ(fixture.stats().active.reservedGpuBytes,reserved);
    }
    SalvageFixtureTicket unchanged{71,93};startFrame();
    for(const auto pose:{game::assets::RigidMechanismPose{game::assets::RigidMechanismKind::WinchDrum,0},
            game::assets::RigidMechanismPose{game::assets::RigidMechanismKind::PropellerRotor,std::numeric_limits<double>::quiet_NaN()}}) {
        placements[0].mechanism=pose;
        EXPECT_FALSE(fixture.encode(encoder,colorView,depthView,placements,frame(),unchanged,error));
        EXPECT_NE(error.find("mechanism"),std::string::npos)<<error;
        EXPECT_EQ(unchanged,(SalvageFixtureTicket{71,93}));
        EXPECT_EQ(fixture.stats().unresolved.serial,0u);
        EXPECT_EQ(fixture.stats().active.reservedGpuBytes,reserved);
    }
    releaseCommands();
    EXPECT_EQ(MeshPath::gpuInstanceBytes,112u);
    EXPECT_EQ(SalvageAssetFixture::fixedGpuRequestedBytes,119336u);
    EXPECT_EQ(SalvageAssetFixture::maximumExpandedDraws,512u);
    EXPECT_EQ(SalvageAssetFixture::maximumMeshInstances,256u);
}

TEST_F(FixtureGPU, RejectsWrongCanonicalMechanismRoleAndMissingRoleInAnotherLodBeforeUpload) {
    Package wrong;makeMechanismProbe(wrong);wrong.setPartName("salvage.part.beam");
    const std::array wrongBundles{wrong.admit()};
    EXPECT_FALSE(fixture.beginCandidate(wrongBundles,error));
    EXPECT_NE(error.find("canonical part name"),std::string::npos)<<error;
    EXPECT_EQ(fixture.stats().candidate.generation,0u);
    Package partial;makeMechanismProbe(partial);partial.multipleLods(7401,2);
    const auto legacy=Package{}.files.at("lod-9007199254740997.vmesh");
    partial.files["lod-101.vmesh"]=legacy;
    auto manifest=Json::parse(partial.files["cook-manifest.json"]);
    manifest["lods"][1]["bytes"]=legacy.size();manifest["lods"][1]["sha256"]=hash(legacy);
    partial.files["cook-manifest.json"]=bytes(manifest.dump(2)+"\n");
    partial.selection.manifestSha256=hash(partial.files["cook-manifest.json"]);
    const std::array partialBundles{partial.admit()};
    EXPECT_FALSE(fixture.beginCandidate(partialBundles,error));
    EXPECT_NE(error.find("across bundle LODs"),std::string::npos)<<error;
    EXPECT_EQ(fixture.stats().candidate.generation,0u);
    EXPECT_EQ(fixture.stats().unresolved.serial,0u);
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

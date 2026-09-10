#include "game/assets/fixture_registry.hpp"
#include "game/construction/compiled_assembly.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace voxy::game::assets;
using namespace voxy::game::construction;
using Json = nlohmann::json;

// Copy declared installed runfiles: Bazel may expose them as symlinks, whereas
// the actual runtime directory loader deliberately refuses symlinked content.
struct KitFiles {
    std::filesystem::path root;
    KitFiles() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i=0; i<100; ++i) {
            auto path=std::filesystem::temp_directory_path()/
                ("voxys-kit-"+std::to_string(stamp)+"-"+std::to_string(i));
            if (std::filesystem::create_directory(path)) {root=std::move(path);break;}
        }
        if (root.empty()) throw std::runtime_error("cannot create isolated kit fixture");
        try {
            for (const auto* label : {"narrow","broad","cargo"}) {
                const auto filename=std::string("fixture-kit-")+label+".json";
                std::ifstream input(std::filesystem::path("data/salvage")/filename);
                Json data; input>>data;
                std::ofstream(root/filename)<<data.dump();
                for (const auto& bundle : data["bundles"]) {
                    const auto directory=bundle["directory"].get<std::string>();
                    std::filesystem::create_directories(root/directory);
                    for (const auto* file : {"gameplay.json","cook-manifest.json","lod-1.vmesh","lod-2.vmesh","lod-3.vmesh"}) {
                        const auto to=root/directory/file;
                        if (!std::filesystem::exists(to))
                            std::filesystem::copy_file(std::filesystem::path("data/salvage")/directory/file,to);
                    }
                }
            }
        } catch (...) {std::error_code ignored;std::filesystem::remove_all(root,ignored);throw;}
    }
    ~KitFiles() {std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    std::unique_ptr<const LoadedAssetFixture> load(const char* label) const {
        std::string error;
        auto result=loadAssetFixture(root/(std::string("fixture-kit-")+label+".json"),error);
        if (!result) throw std::runtime_error(error);
        return result;
    }
};

PartCatalog catalog(const LoadedAssetFixture& fixture) {
    PartCatalogDraft draft;
    for (const auto& bundle : fixture.bundles) draft.definitions.push_back(bundle->sidecar().part);
    CatalogIssue issue;
    auto result=PartCatalog::create(draft,issue,[&](const CookedMeshVisual& visual) {
        for (const auto& bundle : fixture.bundles) for (const auto& lod : bundle->lods())
            if (lod.asset==visual.asset) return true;
        return false;
    });
    if (!result) throw std::runtime_error(std::string(issue.field));
    return std::move(*result);
}

TEST(FunctionalKit, BothInstalledStartersAreConnectedAndFitEveryProperOrientation) {
    KitFiles files;
    for (const auto* label : {"narrow","broad"}) {
        SCOPED_TRACE(label);
        const auto fixture=files.load(label);
        ASSERT_TRUE(fixture->assembly);
        ASSERT_EQ(fixture->bundles.size(),8u);
        EXPECT_TRUE(fixture->prototypes.empty());
        const auto definitions=catalog(*fixture);
        const auto& source=fixture->assembly->snapshot();
        ASSERT_EQ(source.parts.size(),11u);
        ASSERT_EQ(source.connections.size(),17u);
        for (uint8_t rotation=0; rotation<24; ++rotation) {
            auto candidate=source;
            const GridTransform root{{-101,29,-87},{rotation}};
            for (auto& part : candidate.parts) part.placement=*compose(root,part.placement);
            AssemblyFunctionIssue issue;
            const auto compiled=CompiledAssembly::compile(candidate,definitions,issue);
            ASSERT_TRUE(compiled) << label << ' ' << static_cast<unsigned>(rotation);
            ASSERT_EQ(compiled->mass().roots().size(),1u);
            EXPECT_DOUBLE_EQ(compiled->mass().roots().front().mass.dryMassKg,1035.0);
            candidate.parts.back().placement.translation.x+=1;
            BuildIssue bad;
            EXPECT_FALSE(BuildModel::create(candidate,definitions,bad));
            EXPECT_EQ(bad.error,BuildError::MisalignedWeld);
        }
    }
}

TEST(FunctionalKit, WiderFlotationChangesInertiaWithoutAddingMassOrMovingWorldCenter) {
    KitFiles files;
    const auto narrow=files.load("narrow"), broad=files.load("broad");
    AssemblyIssue issue;
    const auto a=AssemblyMassPlan::compile(narrow->assembly->snapshot(),catalog(*narrow),issue);
    const auto b=AssemblyMassPlan::compile(broad->assembly->snapshot(),catalog(*broad),issue);
    ASSERT_TRUE(a); ASSERT_TRUE(b);
    const auto& ar=a->roots().front(); const auto& br=b->roots().front();
    const auto absoluteCenter=[](const AssemblyMassRoot& root) {
        const auto origin=*toMetres(root.buildFromRoot.translation);
        const auto center=root.mass.localCenterOfMass;
        return glm::dvec3(origin.x+center.x,origin.y+center.y,origin.z+center.z);
    };
    const auto ac=absoluteCenter(ar),bc=absoluteCenter(br);
    EXPECT_NEAR(glm::length(ac-bc),0,1e-12);
    EXPECT_DOUBLE_EQ(ar.mass.dryMassKg,br.mass.dryMassKg);
    // Two 120 kg pontoons move from x=±.5 to ±1.5: ΔIyy=ΔIzz=480.
    EXPECT_NEAR(br.mass.inertia.elements[0]-ar.mass.inertia.elements[0],0,1e-9);
    EXPECT_NEAR(br.mass.inertia.elements[4]-ar.mass.inertia.elements[4],480,1e-9);
    EXPECT_NEAR(br.mass.inertia.elements[8]-ar.mass.inertia.elements[8],480,1e-9);
}

TEST(FunctionalKit, OpenMachineryKeepsVoidSpaceAndDisjointSolidMaterialProxies) {
    KitFiles files;
    const auto fixture=files.load("broad");
    const auto contains=[](const PartBox& b,GridPosition p) {
        const auto local=transformPosition(*inverse(b.frame),p);
        if (!local) return false;
        return local->x>=-b.halfExtents.x && local->x<b.halfExtents.x
            && local->y>=-b.halfExtents.y && local->y<b.halfExtents.y
            && local->z>=-b.halfExtents.z && local->z<b.halfExtents.z;
    };
    struct Probe {size_t bundle; GridPosition open,solid;};
    // Independent points inside the old bounding boxes: beneath the drum,
    // between cradle rail/pedestal, and beside the helm's narrow column.
    for (const auto& probe : {Probe{5,{20,-10,0},{0,-10,-6}},
             Probe{6,{0,-30,0},{18,-25,0}}, Probe{7,{25,0,0},{44,0,0}}}) {
        const auto& part=fixture->bundles[probe.bundle]->sidecar().part;
        SCOPED_TRACE(part.nameKey);
        EXPECT_EQ(part.collision,part.solidOccupancy);
        EXPECT_FALSE(std::any_of(part.collision.begin(),part.collision.end(),
            [&](const auto& b){return contains(b,probe.open);}));
        EXPECT_TRUE(std::any_of(part.collision.begin(),part.collision.end(),
            [&](const auto& b){return contains(b,probe.solid);}));
        for (size_t i=0;i<part.collision.size();++i) {
            const auto& a=part.collision[i];
            ASSERT_EQ(a.frame.rotation.value,0u);
            for (size_t j=i+1;j<part.collision.size();++j) {
                const auto& b=part.collision[j];
                ASSERT_EQ(b.frame.rotation.value,0u);
                const bool overlap=std::abs(a.frame.translation.x-b.frame.translation.x)<a.halfExtents.x+b.halfExtents.x
                    && std::abs(a.frame.translation.y-b.frame.translation.y)<a.halfExtents.y+b.halfExtents.y
                    && std::abs(a.frame.translation.z-b.frame.translation.z)<a.halfExtents.z+b.halfExtents.z;
                EXPECT_FALSE(overlap) << i << '/' << j;
            }
        }
        for (const auto& region : part.buoyancy) {
            EXPECT_EQ(region.kind,BuoyancyKind::SolidMaterial);
            const auto found=std::find_if(part.collision.begin(),part.collision.end(),
                [&](const auto& b){return b.id==region.box.id;});
            ASSERT_NE(found,part.collision.end());
            EXPECT_EQ(found->frame,region.box.frame);
            EXPECT_LE(region.box.halfExtents.x,found->halfExtents.x);
            EXPECT_LE(region.box.halfExtents.y,found->halfExtents.y);
            EXPECT_LE(region.box.halfExtents.z,found->halfExtents.z);
            EXPECT_FALSE(contains(region.box,probe.open));
        }
    }
}

TEST(FunctionalKit, CargoLatchAndTowEyesHaveCompatibleTypesAndNoninterferingAxialSpace) {
    KitFiles files;
    const auto fixture=files.load("cargo");
    const auto& cradle=fixture->bundles[0]->sidecar().part;
    const auto* lower=findSocket(cradle,SocketId{2});
    const auto* latch=findSocket(cradle,SocketId{10});
    ASSERT_NE(lower,nullptr); ASSERT_NE(latch,nullptr);
    const auto* winch=findSocket(fixture->bundles[3]->sidecar().part,SocketId{10});
    ASSERT_NE(winch,nullptr);
    EXPECT_GE(latch->frame.translation.y-lower->frame.translation.y,20); // Two .20 m wells.
    for (size_t index : {1u,2u}) {
        const auto& cargo=fixture->bundles[index]->sidecar().part;
        const auto* pin=findSocket(cargo,SocketId{11});
        const auto* eye=findSocket(cargo,SocketId{10});
        ASSERT_NE(pin,nullptr); ASSERT_NE(eye,nullptr);
        EXPECT_EQ(matchSockets(*latch,*pin,ConnectionKind::Latch),SocketMatchError::None);
        EXPECT_EQ(matchSockets(*winch,*eye,ConnectionKind::Rope),SocketMatchError::None);
        EXPECT_EQ(compose(latch->frame.rotation,CubeRotation{2}),pin->frame.rotation);
        const GridTransform cargoFrame{{0,latch->frame.translation.y-pin->frame.translation.y,0},{0}};
        EXPECT_EQ(compose(cargoFrame,pin->frame)->translation,latch->frame.translation);
        EXPECT_NEAR(cargo.mass.dryMassKg,index==1 ? 420.0 : 700.0,1e-12);
        for (const auto& volume : cargo.buoyancy) EXPECT_EQ(volume.kind,BuoyancyKind::SolidMaterial);
    }
    EXPECT_EQ(findSocket(fixture->bundles[2]->sidecar().part,SocketId{10})->frame.translation.x,25);
}

TEST(FunctionalKit, FullSkiffRetainsAllLodsWithinExistingOwnerBudget) {
    KitFiles files;
    const auto fixture=files.load("broad");
    uint64_t gpu=128u*1024u;
    for (const auto& bundle : fixture->bundles) {
        gpu+=bundle->requestedGpuBytes();
        ASSERT_EQ(bundle->lods().size(),3u);
        for (const auto& lod : bundle->lods()) {
            EXPECT_EQ(lod.prefab.meshNodes.size(),1u);
            EXPECT_EQ(lod.prefab.renderToCanonical.value,12);
            EXPECT_EQ(lod.mesh.materials.size(),1u);
        }
    }
    EXPECT_LE(gpu,16u*1024u*1024u);
    std::cout<<"FUNCTIONAL_KIT_REQUESTED_GPU_BYTES "<<gpu<<'\n';
    // The lower leg and the propeller clear the deck's back edge at z=2 m.
    const auto& parts=fixture->assembly->snapshot().parts;
    const auto& engine=fixture->bundles[3]->sidecar().part;
    const auto* shaft=findSocket(engine,SocketId{10});
    ASSERT_NE(shaft,nullptr);
    const auto placed=compose(parts[6].placement,shaft->frame);
    ASSERT_TRUE(placed);
    EXPECT_EQ(placed->translation,(GridPosition{25,-24,120}));
    EXPECT_EQ(parts[7].placement.translation,(GridPosition{25,-24,136}));
    // Conservative level-water estimate from the two sealed pontoons only.
    // Other submerged solid cores add buoyancy, but do not establish live trim.
    const auto& pontoon=fixture->bundles[0]->sidecar().part;
    const auto displacedMass=[&](double height) {
        double volume=0;
        for (const auto& region : pontoon.buoyancy) {
            const auto h=*toMetres(region.box.halfExtents);
            const auto c=*toMetres(region.box.frame.translation);
            volume+=4*h.x*h.z*std::clamp(height-c.y+h.y,0.0,2*h.y);
        }
        return 2*1000*volume;
    };
    for (double mass : {1035.0,1455.0,1735.0}) {
        double low=-.48, high=.48;
        for (unsigned i=0; i<50; ++i) {
            const double middle=(low+high)/2;
            if (displacedMass(middle)<mass) low=middle; else high=middle;
        }
        const double shaftHeight=static_cast<double>(placed->translation.y)/50;
        EXPECT_LT(shaftHeight,low-.1); // At least .10 m centre immersion in this estimate.
        std::cout<<"KIT_LEVEL_WATER_ESTIMATE "<<mass<<' '<<low<<'\n';
    }
}
} // namespace

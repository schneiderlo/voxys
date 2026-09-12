#include "game/assets/fixture_registry.hpp"
#include "core/sha256.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <array>

namespace voxy::game::assets {
namespace {
using Json = nlohmann::json;
Json registry() {
    std::ifstream file("data/salvage/fixture-pontoon-v2.json");
    Json value; file >> value; return value;
}
Json assemblyRegistry() {
    std::ifstream file("data/salvage/fixture-pontoon-assembly.json");
    Json value; file >> value; return value;
}

TEST(FixtureRegistry, ParsesBoundedAssemblyPrototypeAndExactSocketOrdinals) {
    std::string error;
    const auto value=parseAssetFixtureRegistry(assemblyRegistry().dump(),error);
    ASSERT_TRUE(value) << error;
    EXPECT_EQ(value->schema,2u);
    ASSERT_EQ(value->prototypes.size(),1u);
    EXPECT_EQ(value->prototypes[0],construction::starterPartKey(construction::StarterPart::Beam));
    ASSERT_EQ(value->placements.size(),6u);
    EXPECT_FALSE(value->placements[3].prototype); EXPECT_TRUE(value->placements[4].prototype);
    EXPECT_EQ(value->placements[4].placement.translation,(construction::GridPosition{0,48,-50}));
    EXPECT_EQ(value->placements[5].placement.translation,(construction::GridPosition{0,48,50}));
    ASSERT_EQ(value->connections.size(),5u);
    EXPECT_EQ(value->connections[1].aPlacement,1u);
    EXPECT_EQ(value->connections[1].bSocket.value(),107u);
    EXPECT_EQ(value->connections[4].aSocket.value(),1u);
    EXPECT_EQ(value->connections[4].bSocket.value(),2u);
}

TEST(FixtureRegistry, RejectsAmbiguousAssemblyReferencesAndExcessBeforeLoading) {
    std::string error;
    const auto rejects=[&](const Json& value){EXPECT_FALSE(parseAssetFixtureRegistry(value.dump(),error)) << value.dump();};
    auto value=assemblyRegistry(); value["schema"]=1; rejects(value);
    value=assemblyRegistry(); value["prototypes"].push_back(value["prototypes"][0]); rejects(value);
    value=assemblyRegistry(); value["prototypes"]=Json::array(); rejects(value);
    value=assemblyRegistry(); value["prototypes"][0]["version"]=0; rejects(value);
    value=assemblyRegistry(); value["placements"][4]["bundle"]=0; rejects(value);
    value=assemblyRegistry(); value["placements"][4]["prototype"]=1; rejects(value);
    value=assemblyRegistry(); value["connections"]=Json::array(); rejects(value);
    value=assemblyRegistry(); value["connections"][0]["kind"]="rope"; rejects(value);
    value=assemblyRegistry(); value["connections"][0]["a"]["socket"]=101; rejects(value);
    value=assemblyRegistry(); value["connections"][0]["a"]["placement"]=6; rejects(value);
    value=assemblyRegistry(); value["connections"][0]["b"]["placement"]=0; rejects(value);
    value=assemblyRegistry(); value["connections"][0]["a"]["socket"]="0101"; rejects(value);
    value=assemblyRegistry(); value["connections"]=Json::array();
    for (size_t i=0;i<=kMaximumFixtureConnections;++i)value["connections"].push_back(assemblyRegistry()["connections"][0]);
    rejects(value);
}

TEST(FixtureRegistry, LoadsCompleteCoveAndRejectsExcessBundleAdmission) {
    std::string error;
    // Resolve trusted Bazel runfiles before invoking production no-follow
    // package admission; its path protections remain active for every leaf.
    const auto loaded = loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"), error);
    ASSERT_TRUE(loaded) << error;
    EXPECT_EQ(loaded->bundles.size(), 9u);
    EXPECT_EQ(loaded->registry.placements.size(), 25u);
    EXPECT_EQ(loaded->registry.connections.size(), 17u);
    EXPECT_FALSE(loaded->assembly.has_value()); // CoveBoatAssembly selects the boat separately from scenery.
    std::ifstream file("data/salvage/fixture-cove-r01.json");
    Json value; file >> value;
    while (value["bundles"].size() <= kMaximumFixtureBundles) value["bundles"].push_back(value["bundles"][0]);
    EXPECT_FALSE(parseAssetFixtureRegistry(value.dump(), error));
    EXPECT_NE(error.find("ceiling"), std::string::npos);
}

TEST(FixtureRegistry, AdditiveBricksPreserveInstalledCoveIdentityAndEveryExistingDefinition) {
    std::string error;
    const auto base=loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    const auto catalog=std::filesystem::canonical("data/salvage/cove-bricks-r02.json");
    const auto expanded=appendAssetFixtureCatalog(*base,catalog,error);ASSERT_TRUE(expanded)<<error;
    ASSERT_EQ(expanded->bundles.size(),12u);
    EXPECT_EQ(expanded->installedRegistryDigest,base->installedRegistryDigest);
    EXPECT_EQ(expanded->registry.placements.size(),base->registry.placements.size());
    EXPECT_EQ(expanded->registry.connections.size(),base->registry.connections.size());
    ASSERT_TRUE(expanded->registry.navigation);
    EXPECT_EQ(expanded->registry.navigation->boatPlacements,base->registry.navigation->boatPlacements);
    EXPECT_EQ(expanded->registry.navigation->cargoPlacements,base->registry.navigation->cargoPlacements);
    EXPECT_EQ(expanded->registry.navigation->spawn,base->registry.navigation->spawn);
    for(size_t i=0;i<base->bundles.size();++i)EXPECT_EQ(expanded->bundles[i],base->bundles[i]);
    for(size_t i=0;i<base->registry.placements.size();++i) {
        EXPECT_EQ(expanded->registry.placements[i].placement,base->registry.placements[i].placement);
        EXPECT_EQ(expanded->registry.placements[i].bundleIndex,base->registry.placements[i].bundleIndex);
    }
    EXPECT_EQ(expanded->bundles[9]->sidecar().part.nameKey,"salvage.part.brick_1x2");
    EXPECT_EQ(expanded->bundles[10]->sidecar().part.nameKey,"salvage.part.brick_2x2");
    EXPECT_EQ(expanded->bundles[11]->sidecar().part.nameKey,"salvage.part.brick_2x4");
    EXPECT_FALSE(appendAssetFixtureCatalog(*expanded,catalog,error));
    EXPECT_NE(error.find("replace"),std::string::npos)<<error;
    EXPECT_EQ(base->bundles.size(),9u);EXPECT_EQ(expanded->bundles.size(),12u);
}

TEST(FixtureRegistry, MoldedMachineryPreservesEveryCanonicalPartAndPlacedTransform) {
    std::string error;
    const auto base=loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    const auto selected=appendAssetFixtureCatalog(*base,std::filesystem::canonical("data/salvage/cove-workshop-r04.json"),error);
    ASSERT_TRUE(selected)<<error;
    ASSERT_EQ(selected->bundles.size(),12u);ASSERT_EQ(selected->renderBundles().size(),12u);
    EXPECT_EQ(selected->installedRegistryDigest,base->installedRegistryDigest);
    EXPECT_EQ(selected->registry.navigation->boatPlacements,base->registry.navigation->boatPlacements);
    EXPECT_EQ(selected->registry.navigation->cargoPlacements,base->registry.navigation->cargoPlacements);
    EXPECT_EQ(selected->registry.navigation->spawn,base->registry.navigation->spawn);
    size_t revised=0;
    for(size_t i=0;i<base->bundles.size();++i) {
        EXPECT_EQ(selected->bundles[i],base->bundles[i]);
        EXPECT_EQ(selected->registry.bundles[i].selection.manifestSha256,base->registry.bundles[i].selection.manifestSha256);
        const auto& key=base->bundles[i]->sidecar().part.nameKey;
        const bool changed=key=="salvage.part.pontoon"||key=="salvage.part.beam"||key=="salvage.part.plate"
            ||key=="salvage.part.engine"||key=="salvage.part.propeller"||key=="salvage.part.helm"||key=="salvage.part.winch";
        if(changed) {
            ++revised;
            EXPECT_NE(selected->renderBundles()[i],selected->bundles[i]);
            EXPECT_EQ(selected->renderBundles()[i]->sidecar().part.key,selected->bundles[i]->sidecar().part.key);
            ASSERT_EQ(selected->renderBundles()[i]->lods().size(),3u);
            for(size_t lod=0;lod<3;++lod) {
                EXPECT_NE(selected->renderBundles()[i]->lods()[lod].asset,selected->bundles[i]->lods()[lod].asset);
            }
        } else EXPECT_EQ(selected->renderBundles()[i],selected->bundles[i]);
    }
    EXPECT_EQ(revised,7u);
    ASSERT_EQ(selected->registry.placements.size(),base->registry.placements.size());
    for(size_t i=0;i<base->registry.placements.size();++i) {
        EXPECT_EQ(selected->registry.placements[i].placement,base->registry.placements[i].placement);
        EXPECT_EQ(selected->registry.placements[i].bundleIndex,base->registry.placements[i].bundleIndex);
    }
    for(size_t i=9;i<12;++i)EXPECT_EQ(selected->renderBundles()[i],selected->bundles[i]);
    EXPECT_TRUE(base->presentationBundles.empty());
}

TEST(FixtureRegistry, ToyArtSelectsRenderBundlesWithoutChangingOwnedContent) {
    std::string error;
    const auto base=loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    const auto selected=appendAssetFixtureCatalog(*base,std::filesystem::canonical("data/salvage/cove-workshop-r03.json"),error);
    ASSERT_TRUE(selected)<<error;
    ASSERT_EQ(selected->bundles.size(),12u);ASSERT_EQ(selected->renderBundles().size(),12u);
    EXPECT_EQ(selected->installedRegistryDigest,base->installedRegistryDigest);
    EXPECT_EQ(selected->registry.navigation->boatPlacements,base->registry.navigation->boatPlacements);
    EXPECT_EQ(selected->registry.navigation->spawn,base->registry.navigation->spawn);
    for(size_t i=0;i<base->bundles.size();++i) {
        EXPECT_EQ(selected->bundles[i],base->bundles[i]);
        EXPECT_EQ(selected->registry.bundles[i].selection.manifestSha256,base->registry.bundles[i].selection.manifestSha256);
        if(i<3) {
            EXPECT_NE(selected->renderBundles()[i],selected->bundles[i]);
            EXPECT_EQ(selected->renderBundles()[i]->sidecar().part.key,selected->bundles[i]->sidecar().part.key);
            EXPECT_NE(selected->renderBundles()[i]->lods()[0].asset,selected->bundles[i]->lods()[0].asset);
        } else EXPECT_EQ(selected->renderBundles()[i],selected->bundles[i]);
    }
    for(size_t i=0;i<base->registry.placements.size();++i)
        EXPECT_EQ(selected->registry.placements[i].placement,base->registry.placements[i].placement);
    for(size_t i=9;i<12;++i)EXPECT_EQ(selected->renderBundles()[i],selected->bundles[i]);
    EXPECT_TRUE(base->presentationBundles.empty());
}

#if defined(__unix__) || defined(__EMSCRIPTEN__)
TEST(FixtureRegistry, PresentationRejectsChangedGameplaySourceAndGeometryAtomically) {
    std::string error;
    const auto base=loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    char name[]="/tmp/voxys-presentation-XXXXXX";ASSERT_NE(::mkdtemp(name),nullptr);
    const std::filesystem::path root(name);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{root};
    Json source;std::ifstream("data/salvage/cove-workshop-r03.json")>>source;
    // Isolate the beam presentation. This also checks schema 2 can select art
    // after a canonical catalogue has already been loaded, with no new parts.
    source["bundles"]=Json::array();source["presentations"]=Json::array({source["presentations"][1]});
    const auto directory=source["presentations"][0]["bundle"]["directory"].get<std::string>();
    const auto package=root/directory;std::filesystem::create_directories(package);
    for(const auto* file:{"cook-manifest.json","gameplay.json","lod-1.vmesh","lod-2.vmesh","lod-3.vmesh"})
        std::filesystem::copy_file(std::filesystem::path("data/salvage")/directory/file,package/file);
    const auto write=[](const std::filesystem::path& path,std::string_view bytes){std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));};
    const auto hash=[](std::string_view bytes){return core::sha256Hex(core::sha256(std::as_bytes(std::span(bytes))));};
    const auto selects=[&](const Json& value){write(root/"catalog.json",value.dump());return appendAssetFixtureCatalog(*base,root/"catalog.json",error);};
    ASSERT_TRUE(selects(source))<<error;
    const auto refuses=[&](const Json& value,std::string_view message){
        EXPECT_FALSE(selects(value));EXPECT_NE(error.find(message),std::string::npos)<<error;
        EXPECT_TRUE(base->presentationBundles.empty());EXPECT_EQ(base->bundles.size(),9u);
    };
    auto value=source;value["presentations"][0]["source_manifest_sha256"]=std::string(64,'0');refuses(value,"source");
    value=source;value["presentations"].push_back(value["presentations"][0]);refuses(value,"duplicate presentation");
    value=source;value["presentations"][0]["bundle"]["directory"]="../escape";refuses(value,"directory");
    value=source;value["presentations"][0]["bundle"]["manifest_sha256"]=std::string(64,'0');refuses(value,"SHA256 mismatch");
    Json metadata,manifest;std::ifstream(package/"gameplay.json")>>metadata;std::ifstream(package/"cook-manifest.json")>>manifest;
    const auto installMetadata=[&](Json changed){
        const auto parsed=parseGameplaySidecar(changed.dump(),[](const auto&){return true;},error);
        if(!parsed)throw std::runtime_error(error);
        write(package/"gameplay.json",parsed->normalizedJson);
        auto updated=manifest;updated["normalized_sidecar"]["bytes"]=parsed->normalizedJson.size();
        updated["normalized_sidecar"]["sha256"]=hash(parsed->normalizedJson);
        const auto encoded=updated.dump(2)+"\n";write(package/"cook-manifest.json",encoded);
        auto selection=source;selection["presentations"][0]["bundle"]["manifest_sha256"]=hash(encoded);return selection;
    };
    auto changed=metadata;changed["part"]["cost"]["salvage_material"]="13";
    refuses(installMetadata(changed),"gameplay metadata");
    changed=metadata;changed["lods"][0]["minimum_screen_height_pixels"]=201;
    refuses(installMetadata(changed),"LOD identity");
    changed=metadata;changed["lods"][0]["asset"]=Json::parse(base->bundles[1]->sidecar().normalizedJson)["lods"][0]["asset"];
    refuses(installMetadata(changed),"unique new visual asset");
    (void)installMetadata(metadata);
    // Rehash a valid mesh whose root has moved: byte integrity alone must not
    // permit art to move away from the owned physical part.
    std::ifstream input(package/"lod-1.vmesh",std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input),{});moto::VmeshData mesh;
    ASSERT_TRUE(moto::readVmesh(bytes.data(),bytes.size(),&mesh,&error))<<error;
    ASSERT_FALSE(mesh.nodes.empty());mesh.nodes[0].translation[0]+=.5f;
    ASSERT_TRUE(moto::writeVmesh(mesh,&bytes,&error))<<error;
    const std::string encodedMesh(bytes.begin(),bytes.end());write(package/"lod-1.vmesh",encodedMesh);
    manifest["lods"][0]["bytes"]=bytes.size();manifest["lods"][0]["sha256"]=hash(encodedMesh);
    const auto encoded=manifest.dump(2)+"\n";write(package/"cook-manifest.json",encoded);
    value=source;value["presentations"][0]["bundle"]["manifest_sha256"]=hash(encoded);refuses(value,"visual envelope");
}

TEST(FixtureRegistry, AdditiveCatalogRejectsLayoutChangesReplacementTraversalAndCorruptionAtomically) {
    std::string error;
    const auto base=loadAssetFixture(std::filesystem::canonical("data/salvage/fixture-cove-r01.json"),error);
    ASSERT_TRUE(base)<<error;
    char name[]="/tmp/voxys-catalog-XXXXXX";ASSERT_NE(::mkdtemp(name),nullptr);
    const std::filesystem::path root(name);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{root};
    Json source;std::ifstream("data/salvage/cove-bricks-r02.json")>>source;
    for(const auto& spec:source["bundles"]) {
        const auto directory=spec["directory"].get<std::string>();std::filesystem::create_directories(root/directory);
        for(const auto* file:{"cook-manifest.json","gameplay.json","lod-1.vmesh","lod-2.vmesh","lod-3.vmesh"})
            std::filesystem::copy_file(std::filesystem::path("data/salvage")/directory/file,root/directory/file);
    }
    const auto refuses=[&](const Json& value,const char* expected){
        {std::ofstream file(root/"catalog.json");file<<value.dump();}
        EXPECT_FALSE(appendAssetFixtureCatalog(*base,root/"catalog.json",error));
        EXPECT_NE(error.find(expected),std::string::npos)<<error;
        EXPECT_EQ(base->bundles.size(),9u);EXPECT_EQ(base->registry.placements.size(),25u);
    };
    auto value=source;value["placements"]=Json::array();refuses(value,"field");
    value=source;value["bundles"][0]["directory"]="../bricks";refuses(value,"directory");
    value=source;value["bundles"].push_back(value["bundles"][0]);refuses(value,"replace");
    Json original;std::ifstream("data/salvage/fixture-cove-r01.json")>>original;
    value=source;value["bundles"][0]["part"]=original["bundles"][0]["part"];
    value["bundles"][0]["part"]["version"]=999;refuses(value,"replace");
    // The third admission fails after two successful cooks; no partial
    // catalogue reaches the caller and the base remains usable.
    value=source;value["bundles"][2]["manifest_sha256"]=std::string(64,'0');refuses(value,"SHA256 mismatch");
}
#endif

TEST(FixtureRegistry, ParsesDataDrivenKeysLimitsPlacementsAndCamera) {
    std::string error;
    const auto value = parseAssetFixtureRegistry(registry().dump(), error);
    ASSERT_TRUE(value) << error;
    ASSERT_EQ(value->bundles.size(), 1u);
    EXPECT_EQ(value->bundles[0].selection.part.id.counter, 3u);
    EXPECT_EQ(value->bundles[0].selection.part.version, 2u);
    ASSERT_EQ(value->bundles[0].selection.lodRules.size(), 3u);
    EXPECT_EQ(value->bundles[0].selection.lodRules[0].limits.maximumIndices, 18000u);
    EXPECT_EQ(value->bundles[0].selection.lodRules[2].limits.maximumTextureDimension, 64u);
    ASSERT_EQ(value->placements.size(), 4u);
    EXPECT_EQ(value->placements[0].placement.translation.x, -75);
    EXPECT_EQ(value->placements[3].placement.translation.y, 48);
    EXPECT_DOUBLE_EQ(value->cameraEye.x, 7);
}

TEST(FixtureRegistry, RejectsAmbiguousUnboundedAndMalformedSelections) {
    std::string error;
    const auto rejects = [&](const Json& value) { EXPECT_FALSE(parseAssetFixtureRegistry(value.dump(), error)) << value.dump(); };
    EXPECT_FALSE(parseAssetFixtureRegistry("", error));
    EXPECT_FALSE(parseAssetFixtureRegistry(std::string(65537,' '), error));
    EXPECT_FALSE(parseAssetFixtureRegistry("{\"schema\":1,\"schema\":1}", error));
    EXPECT_NE(error.find("duplicate"), std::string::npos) << error;
    auto value = registry(); value["extra"] = 1; rejects(value);
    value = registry(); value["schema"] = 2; rejects(value);
    value = registry(); value["bundles"] = Json::array(); rejects(value);
    value = registry(); value["placements"] = Json::array(); rejects(value);
    value = registry(); value["bundles"][0]["part"]["counter"] = "03"; rejects(value);
    value = registry(); value["bundles"][0]["part"]["counter"] = 3; rejects(value);
    value = registry(); value["bundles"][0]["part"]["namespace"] = std::string(32,'0'); rejects(value);
    value = registry(); value["bundles"][0]["manifest_sha256"] = std::string(64,'A'); rejects(value);
    value = registry(); value["bundles"][0]["lod_limits"][0]["vertices"] = 65537; rejects(value);
    value = registry(); value["bundles"][0]["lod_limits"][0]["triangles"] = 65537; rejects(value);
    value = registry(); value["bundles"][0]["lod_limits"][0]["texture_dimension"] = 2049; rejects(value);
    value = registry(); value["bundles"][0]["lod_limits"][1]["id"] = "1"; rejects(value);
    value = registry(); value["placements"][0]["bundle"] = 1; rejects(value);
    value = registry(); value["placements"][0]["rotation"] = 24; rejects(value);
    value = registry(); value["placements"][0]["translation_ticks"][0] = 1.5; rejects(value);
    value = registry(); value["placements"][0]["translation_ticks"][0] = UINT64_MAX; rejects(value);
    value = registry(); value["camera"]["eye"] = value["camera"]["target"]; rejects(value);
    value = registry(); value["camera"]["eye"] = {0,1001,0}; rejects(value);
    value = registry(); value["camera"]["eye"] = {0,1,0}; value["camera"]["target"] = {0,0,0}; rejects(value);
}

TEST(FixtureRegistry, RejectsTraversalAndNonpackageDirectoryForms) {
    for (const auto& path : {"", "..", "../cooked", "/tmp/cooked", "a//b", "a/./b", "a/../b", "a/", "https://host/a", "a\\b"}) {
        auto value = registry(); value["bundles"][0]["directory"] = path;
        std::string error;
        EXPECT_FALSE(parseAssetFixtureRegistry(value.dump(), error)) << path;
    }
    auto value = registry(); value["bundles"][0]["directory"] = std::string("a\0b",3);
    std::string error;
    EXPECT_FALSE(parseAssetFixtureRegistry(value.dump(), error));
}

#if defined(__unix__) || defined(__EMSCRIPTEN__)
TEST(FixtureRegistry, AdmitsEveryInstalledRotationPairAndRejectsOffsetOrWrongKey) {
    char name[]="/tmp/voxys-rotation-registry-XXXXXX";
    ASSERT_NE(::mkdtemp(name),nullptr);
    const std::filesystem::path root(name);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{root};
    const auto source=registry();
    const auto directory=source["bundles"][0]["directory"].get<std::string>();
    std::filesystem::create_directories(root/directory);
    for (const auto* file : {"cook-manifest.json","gameplay.json","lod-1.vmesh","lod-2.vmesh","lod-3.vmesh"})
        std::filesystem::copy_file(std::filesystem::path("data/salvage")/directory/file,root/directory/file);
    const auto write=[&](const Json& value){std::ofstream file(root/"rotations.json");file<<value.dump();};
    std::array<bool,24> seen{};
    for (const auto* group : {"a","b"}) {
        std::ifstream input(std::string("data/salvage/fixture-pontoon-rotations-")+group+".json");
        Json data; input>>data;
        write(data);
        std::string error;
        const auto admitted=loadAssetFixture(root/"rotations.json",error);
        ASSERT_TRUE(admitted) << error;
        ASSERT_TRUE(admitted->assembly);
        EXPECT_TRUE(admitted->prototypes.empty());
        const auto& parts=admitted->assembly->snapshot().parts;
        ASSERT_EQ(parts.size(),24u);
        ASSERT_EQ(admitted->assembly->snapshot().connections.size(),12u);
        ASSERT_EQ(admitted->connectedSockets.size(),24u);
        for (size_t pair=0;pair<12;++pair) {
            const auto& a=parts[pair*2].placement;
            const auto& b=parts[pair*2+1].placement;
            ASSERT_LT(a.rotation.value,24);
            EXPECT_FALSE(seen[a.rotation.value]);seen[a.rotation.value]=true;
            EXPECT_EQ(b.rotation,a.rotation);
            const auto expected=construction::compose(a,construction::GridTransform{{0,48,0},{0}});
            ASSERT_TRUE(expected);
            EXPECT_EQ(b,*expected);
            EXPECT_EQ(admitted->connectedSockets[pair*2],(std::vector{construction::SocketId{1}}));
            EXPECT_EQ(admitted->connectedSockets[pair*2+1],(std::vector{construction::SocketId{2}}));
            auto bad=data;
            bad["placements"][pair*2+1]["translation_ticks"][0]=b.translation.x+1;
            write(bad); EXPECT_FALSE(loadAssetFixture(root/"rotations.json",error)) << "offset rotation " << int(a.rotation.value);
            bad=data;
            const auto wrongKey=construction::compose(b.rotation,construction::CubeRotation{21});
            ASSERT_TRUE(wrongKey);
            bad["placements"][pair*2+1]["rotation"]=wrongKey->value;
            write(bad); EXPECT_FALSE(loadAssetFixture(root/"rotations.json",error)) << "key rotation " << int(a.rotation.value);
            EXPECT_EQ(admitted->assembly->snapshot().parts.size(),24u);
            EXPECT_EQ(admitted->assembly->snapshot().connections.size(),12u);
        }
        EXPECT_LT(parts.front().placement.translation.x,0);
        EXPECT_LT(parts.front().placement.translation.z,0);
    }
    EXPECT_TRUE(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}));
}

TEST(FixtureRegistry, AdmitsActualCrossbeamGraphAndRejectsMisfitWithoutChangingPreviousAssembly) {
    char name[]="/tmp/voxys-assembly-registry-XXXXXX";
    ASSERT_NE(::mkdtemp(name),nullptr);
    const std::filesystem::path root(name);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}} cleanup{root};
    auto source=assemblyRegistry();
    const auto directory=source["bundles"][0]["directory"].get<std::string>();
    std::filesystem::create_directories(root/directory);
    for (const auto* file : {"cook-manifest.json","gameplay.json","lod-1.vmesh","lod-2.vmesh","lod-3.vmesh"})
        std::filesystem::copy_file(std::filesystem::path("data/salvage")/directory/file,root/directory/file);
    const auto write=[&](const Json& value){std::ofstream file(root/"assembly.json");file<<value.dump();};
    write(source);
    std::string error;
    const auto result=loadAssetFixture(root/"assembly.json",error);
    ASSERT_TRUE(result) << error; ASSERT_TRUE(result->assembly);
    EXPECT_EQ(result->prototypes.size(),1u);
    EXPECT_EQ(result->assembly->snapshot().parts.size(),6u);
    EXPECT_EQ(result->assembly->snapshot().connections.size(),5u);
    ASSERT_EQ(result->connectedSockets.size(),6u);
    EXPECT_EQ(result->connectedSockets[0],(std::vector{construction::SocketId{101},construction::SocketId{103}}));
    EXPECT_EQ(result->connectedSockets[4],(std::vector{construction::SocketId{101},construction::SocketId{107}}));
    const auto rejects=[&](Json value,const char* reason){
        write(value); EXPECT_FALSE(loadAssetFixture(root/"assembly.json",error));
        EXPECT_NE(error.find(reason),std::string::npos) << error;
        EXPECT_EQ(result->assembly->snapshot().connections.size(),5u);
    };
    auto bad=source; bad["placements"][4]["translation_ticks"][0]=1; rejects(bad,"BuildModel");
    bad=source; bad["placements"][4]["rotation"]=21; rejects(bad,"BuildModel");
    bad=source; bad["placements"][3]["translation_ticks"][1]=57; rejects(bad,"BuildModel");
    bad=source; bad["connections"].push_back(bad["connections"][0]); rejects(bad,"BuildModel");
    bad=source; bad["connections"][0]["b"]["socket"]="999"; rejects(bad,"missing socket");
    bad=source; bad["prototypes"][0]["version"]=2; rejects(bad,"prototype definition/version");
    // Whole-fixture proper rotations and signed translations are applied to
    // the same data that the renderer will consume, then admitted normally.
    for (uint8_t rotation=0;rotation<24;++rotation) {
        auto rotated=source;
        const construction::GridTransform transform{{-12801,-29,12799},{rotation}};
        for (size_t i=0;i<result->registry.placements.size();++i) {
            const auto placed=construction::compose(transform,result->registry.placements[i].placement);
            ASSERT_TRUE(placed);
            rotated["placements"][i]["translation_ticks"]={placed->translation.x,placed->translation.y,placed->translation.z};
            rotated["placements"][i]["rotation"]=placed->rotation.value;
        }
        write(rotated);
        const auto admitted=loadAssetFixture(root/"assembly.json",error);
        ASSERT_TRUE(admitted) << int(rotation) << ' ' << error;
        ASSERT_TRUE(admitted->assembly);
        EXPECT_EQ(admitted->assembly->snapshot().connections.size(),5u);
    }
}

TEST(FixtureRegistry, AdmitsActualPontoonCandidateAndSelectsLodsFromProjectedUnion) {
    char name[] = "/tmp/voxys-registry-XXXXXX";
    ASSERT_NE(::mkdtemp(name), nullptr);
    const std::filesystem::path root(name);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};
    const auto json = registry();
    // Intentional release-candidate pin, independent of the manifest under test.
    EXPECT_EQ(json["bundles"][0]["manifest_sha256"],
        "5cb23035a14272257af22ae4af47e7fcd863b8b310186346acbf7915fba8818e");
    EXPECT_EQ(json["bundles"][0]["directory"], "pontoon/release-candidates/v2-rc01/cooked");
    const std::string directory = json["bundles"][0]["directory"].get<std::string>();
    const auto source = std::filesystem::path("data/salvage") / directory;
    std::filesystem::create_directories(root / directory);
    for (const auto& file : {"cook-manifest.json", "gameplay.json", "lod-1.vmesh", "lod-2.vmesh", "lod-3.vmesh"})
        std::filesystem::copy_file(source / file, root / directory / file);
    std::filesystem::copy_file("data/salvage/fixture-pontoon-v2.json", root / "fixture.json");
    std::string error;
    const auto result = loadAssetFixture(root / "fixture.json", error);
    ASSERT_TRUE(result) << error;
    ASSERT_EQ(result->bundles.size(), 1u);
    const auto& bundle = *result->bundles[0];
    ASSERT_EQ(bundle.lods().size(), 3u);
    EXPECT_EQ(bundle.lods()[0].mesh.header.indexCount / 3u, 3508u);
    EXPECT_EQ(bundle.lods()[1].mesh.header.indexCount / 3u, 500u);
    EXPECT_EQ(bundle.lods()[2].mesh.header.indexCount / 3u, 256u);
    EXPECT_LT(bundle.requestedGpuBytes(), 8u * 1024u * 1024u);
    EXPECT_NEAR(bundle.lods()[0].prefab.canonicalBounds.maximum.y, .66, .001);
    const glm::dmat4 rootTransform(1);
    const auto view = glm::lookAt(glm::dvec3(0,.1,-10), glm::dvec3(0,.1,0), glm::dvec3(0,1,0));
    const auto projection = glm::perspective(glm::radians(60.0), 1.0, .1, 1000.0);
    EXPECT_EQ(selectFixtureLod(bundle, rootTransform, {}, projection * view, 1080), 2u);
    EXPECT_EQ(selectFixtureLod(bundle, rootTransform, {}, projection * view, 360), 3u);
    const auto close = glm::lookAt(glm::dvec3(0,.1,-3), glm::dvec3(0,.1,0), glm::dvec3(0,1,0));
    EXPECT_EQ(selectFixtureLod(bundle, rootTransform, {}, projection * close, 1080), 1u);
    EXPECT_FALSE(selectFixtureLod(bundle, rootTransform, {}, projection * view, 0));
    EXPECT_FALSE(selectFixtureLod(bundle, rootTransform, {{},{24}}, projection * view, 1080));
    std::filesystem::remove(root / directory / "lod-2.vmesh");
    EXPECT_FALSE(loadAssetFixture(root / "fixture.json", error));
    // The earlier owner stays usable after a failed replacement admission.
    EXPECT_EQ(bundle.lods()[1].mesh.header.indexCount / 3u, 500u);
}
#endif
} // namespace
} // namespace voxy::game::assets

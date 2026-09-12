#include "game/assets/rigid_prefab.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace voxy::game::assets {
namespace {

moto::VmeshData triangle() {
    moto::VmeshData data;
    data.header.vertexCount = 3;
    data.header.indexCount = 3;
    data.header.submeshCount = 1;
    data.header.materialCount = 1;
    data.header.meshCount = 1;
    data.header.nodeCount = 1;
    const std::array<moto::VmeshVertex, 3> vertices{{
        {{0,0,0}, {0,0,1}, {1,0,0,1}, {0,0}, {}, {}},
        {{2,0,0}, {0,0,1}, {1,0,0,1}, {1,0}, {}, {}},
        {{0,1,0}, {0,0,1}, {1,0,0,1}, {0,1}, {}, {}},
    }};
    const std::array<uint16_t, 3> indices{0,1,2};
    data.vertices.resize(sizeof(vertices));
    std::memcpy(data.vertices.data(), vertices.data(), sizeof(vertices));
    data.indices.resize(sizeof(indices));
    std::memcpy(data.indices.data(), indices.data(), sizeof(indices));
    data.submeshes.push_back({0,3,0,0});
    data.materials.emplace_back();
    data.nodes.emplace_back();
    data.nodes[0].meshIndex = 0;
    return data;
}

void expectPoint(glm::dvec3 actual, glm::dvec3 expected, double tolerance = 1.0e-5) {
    for (glm::length_t i = 0; i < 3; ++i) EXPECT_NEAR(actual[i], expected[i], tolerance);
}

void expectRejected(const moto::VmeshData& data, const std::string& reason,
                    RigidPrefabLimits limits = {}) {
    RigidPrefab output;
    output.counts.gpuBytes = 123456;
    output.meshNodes.push_back({17,19,glm::dmat4(1.0)});
    std::string error;
    EXPECT_FALSE(prepareRigidPrefab(data, {}, limits, output, error));
    EXPECT_NE(error.find(reason), std::string::npos) << error;
    EXPECT_EQ(output.counts.gpuBytes, 123456u);
    ASSERT_EQ(output.meshNodes.size(), 1u);
    EXPECT_EQ(output.meshNodes[0].meshIndex, 19u);
}

void texture(moto::VmeshData& data, uint32_t materialIndex, moto::VmeshTextureSlot slot,
             uint16_t width, uint16_t height, uint32_t offset = 0) {
    auto& material = data.materials[materialIndex];
    material.hasTexture[slot] = 1;
    material.textureIsSrgb[slot] = (slot == moto::VmeshTextureBaseColor || slot == moto::VmeshTextureEmissive) ? 1 : 0;
    material.textureWidth[slot] = width;
    material.textureHeight[slot] = height;
    material.textureSize[slot] = uint32_t{width} * height * 4u;
    material.textureOffset[slot] = offset;
    data.images.resize(static_cast<size_t>(offset) + material.textureSize[slot], 128);
}

TEST(RigidPrefab, GoldenBoundsAndRequestedGeometryUseGpuIndexStride) {
    const auto data = triangle();
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(data, {}, {}, prefab, error)) << error;
    ASSERT_EQ(prefab.meshNodes.size(), 1u);
    EXPECT_EQ(prefab.counts.vertexBytes, 216u);
    EXPECT_EQ(prefab.counts.indexBytes, 12u);
    EXPECT_EQ(prefab.counts.materialBytes, 64u);
    EXPECT_EQ(prefab.counts.gpuBytes, 292u);
    EXPECT_EQ(prefab.counts.decodedBytes, 216u + 6u + 16u + 128u + 64u);
    EXPECT_EQ(prefab.counts.meshInstances, 1u);
    EXPECT_EQ(prefab.counts.expandedDraws, 1u);
    expectPoint(prefab.canonicalBounds.minimum, {0,0,0});
    expectPoint(prefab.canonicalBounds.maximum, {2,1,0});
    EXPECT_EQ(data.nodes[0].translation[0], 0.0f);
}

TEST(RigidPrefab, ReversedNodeOrderSharedMeshAndBasisComposeExactlyOnce) {
    auto data = triangle();
    data.nodes.resize(3);
    data.header.nodeCount = 3;
    data.nodes[0].parent = 2;
    data.nodes[0].translation[0] = 1;
    data.nodes[0].translation[1] = 2;
    data.nodes[0].translation[2] = 3;
    data.nodes[1].parent = 2;
    data.nodes[1].meshIndex = 0;
    data.nodes[1].translation[0] = -1;
    data.nodes[2].translation[0] = 3;
    data.nodes[2].translation[1] = 4;
    data.nodes[2].translation[2] = 5;
    data.nodes[2].rotation[2] = std::sqrt(0.5f);
    data.nodes[2].rotation[3] = std::sqrt(0.5f);
    data.nodes[2].scale[0] = 2;
    data.nodes[2].scale[1] = 3;
    data.nodes[2].scale[2] = 4;
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(data, {12}, {}, prefab, error)) << error;
    ASSERT_EQ(prefab.meshNodes.size(), 2u);
    EXPECT_EQ(prefab.counts.expandedDraws, 2u);
    EXPECT_EQ(prefab.counts.gpuBytes, 292u); // Shared nodes do not duplicate uploads.
    expectPoint(glm::dvec3(prefab.meshNodes[0].nodeToAsset * glm::dvec4(0,0,0,1)), {-3,6,17});
    std::vector<RigidPrefabDraw> draws;
    const glm::dmat4 root = glm::translate(glm::dmat4(1.0), glm::dvec3(10,-20,30));
    ASSERT_TRUE(placeRigidPrefab(prefab, root, {{-50,100,150},{21}}, 2, draws, error)) << error;
    ASSERT_EQ(draws.size(), 2u);
    expectPoint(glm::dvec3(draws[0].modelMatrix * glm::vec4(0,0,0,1)), {-8,-12,30});
    expectPoint(glm::dvec3(draws[0].modelMatrix * glm::vec4(2,0,0,1)), {-8,-8,30});
    // The root's local data is retained, not baked into geometry or rewritten.
    EXPECT_EQ(data.nodes[0].translation[0], 1.0f);
}

TEST(RigidPrefab, ParentNonuniformScaleKeepsComposedShear) {
    auto data = triangle();
    data.nodes.resize(2);
    data.header.nodeCount = 2;
    data.nodes[0].parent = 1;
    data.nodes[0].rotation[2] = std::sin(0.3926990817f);
    data.nodes[0].rotation[3] = std::cos(0.3926990817f);
    data.nodes[1].scale[0] = 2;
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(data, {}, {}, prefab, error)) << error;
    const auto& matrix = prefab.meshNodes[0].nodeToAsset;
    EXPECT_GT(std::abs(glm::dot(glm::dvec3(matrix[0]), glm::dvec3(matrix[1]))), 1.0);
    expectPoint(glm::dvec3(matrix * glm::dvec4(1,0,0,1)), {std::sqrt(2.0),std::sqrt(0.5),0});
}

TEST(RigidPrefab, AllProperPartRotationsMatchIntegerOracleIncludingNegativeTranslation) {
    auto data = triangle();
    data.nodes[0].translation[0] = 1;
    data.nodes[0].translation[1] = 2;
    data.nodes[0].translation[2] = 3;
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(data, {12}, {}, prefab, error)) << error;
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        const construction::GridTransform part{{-200,50,-100},{rotation}};
        const auto expected = construction::transformPosition(part, {-50,100,-150});
        ASSERT_TRUE(expected);
        const auto expectedMetres = construction::toMetres(*expected);
        ASSERT_TRUE(expectedMetres);
        std::vector<RigidPrefabDraw> draws;
        ASSERT_TRUE(placeRigidPrefab(prefab, glm::dmat4(1.0), part, 1, draws, error)) << error;
        expectPoint(glm::dvec3(draws[0].modelMatrix * glm::vec4(0,0,0,1)),
                    {expectedMetres->x, expectedMetres->y, expectedMetres->z});
    }
}

TEST(RigidPrefab, SectorRelativeRootPreservesPoseOnEitherSideOfRebase) {
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(triangle(), {}, {}, prefab, error));
    // Caller has subtracted sectors in double. A rebase shifts both root and
    // camera by 256 m, leaving the view-relative point unchanged.
    for (double worldX : {-256.02,-255.98,255.98,256.02}) {
        std::array<double,2> visible{};
        for (size_t i = 0; i < 2; ++i) {
            const double sector = i == 0 ? 0.0 : (worldX < 0 ? -256.0 : 256.0);
            const auto root = glm::translate(glm::dmat4(1.0), glm::dvec3(worldX - sector,0,0));
            std::vector<RigidPrefabDraw> draws;
            ASSERT_TRUE(placeRigidPrefab(prefab, root, {{-1,0,0},{}}, 1, draws, error)) << error;
            visible[i] = double{draws[0].modelMatrix[3].x} - (worldX - sector + 5.0);
        }
        EXPECT_NEAR(visible[0], -5.02, 4.0e-5);
        EXPECT_NEAR(visible[0], visible[1], 4.0e-5);
    }
}

TEST(RigidPrefab, CyclesInvalidReferencesAndEmptyDrawableForestRejectTransactionally) {
    auto data = triangle(); data.nodes[0].parent = -2; expectRejected(data, "references");
    data = triangle(); data.nodes[0].parent = 0; expectRejected(data, "references");
    data = triangle(); data.nodes[0].meshIndex = 2; expectRejected(data, "references");
    data = triangle(); data.nodes[0].skinIndex = 0; expectRejected(data, "references");
    data = triangle(); data.nodes[0].meshIndex = UINT32_MAX; expectRejected(data, "no mesh-bearing");
    data = triangle(); data.nodes.resize(3); data.header.nodeCount = 3;
    data.nodes[1].parent = 2; data.nodes[2].parent = 1; expectRejected(data, "cycle");
}

TEST(RigidPrefab, NonFiniteSingularIllConditionedAndOverflowingTransformsReject) {
    auto data = triangle(); data.nodes[0].rotation[3] = 2; expectRejected(data, "TRS");
    data = triangle(); data.nodes[0].rotation[0] = std::numeric_limits<float>::quiet_NaN(); expectRejected(data, "TRS");
    for (float scale : {0.0f,-1.0f,std::numeric_limits<float>::infinity()}) {
        data = triangle(); data.nodes[0].scale[1] = scale; expectRejected(data, "TRS");
    }
    data = triangle(); data.nodes[0].scale[0] = 1.0e20f; expectRejected(data, "numeric transform");
    data = triangle(); data.nodes[0].scale[0] = 1.0e-8f; expectRejected(data, "numeric transform");
    data = triangle(); data.nodes.resize(3); data.header.nodeCount = 3;
    data.nodes[0].parent = 1; data.nodes[1].parent = 2;
    for (auto& node : data.nodes) for (float& scale : node.scale) scale = 1000;
    expectRejected(data, "composed transform");
    data = triangle(); data.nodes[0].translation[2] = 100'001; expectRejected(data, "numeric transform");
}

TEST(RigidPrefab, GeometryAndNormalMapFailuresAreCheckedBeforeGpuAllocation) {
    auto data = triangle(); data.indices[0] = 3; expectRejected(data, "indices");
    data = triangle(); data.submeshes[0].indexCount = 2; expectRejected(data, "submesh");
    data = triangle(); data.submeshes[0].indexOffset = UINT32_MAX; expectRejected(data, "submesh");
    data = triangle(); data.header.vertexCount = UINT32_MAX; expectRejected(data, "layout");
    data = triangle(); data.header.flags = moto::kVmeshHasSkin; expectRejected(data, "layout");
    data = triangle();
    moto::VmeshVertex vertex{};
    std::memcpy(&vertex, data.vertices.data(), sizeof(vertex));
    vertex.normal[2] = 0;
    std::memcpy(data.vertices.data(), &vertex, sizeof(vertex));
    expectRejected(data, "normal");
    data = triangle(); texture(data, 0, moto::VmeshTextureNormal, 2, 2); expectRejected(data, "tangent flag");
    data.header.flags = moto::kVmeshHasTangent;
    std::memcpy(&vertex, data.vertices.data(), sizeof(vertex));
    vertex.tangent[2] = 1;
    std::memcpy(data.vertices.data(), &vertex, sizeof(vertex));
    expectRejected(data, "tangent frame");
}

TEST(RigidPrefab, SharedImageSlotsCountEveryUploadedCopyAndCompleteMipChains) {
    auto data = triangle();
    data.materials.resize(2); data.header.materialCount = 2;
    texture(data, 0, moto::VmeshTextureBaseColor, 4, 2);
    texture(data, 1, moto::VmeshTextureBaseColor, 4, 2); // Same bytes, two GPU uploads.
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(data, {}, {}, prefab, error)) << error;
    EXPECT_EQ(prefab.counts.textureCount, 2u);
    EXPECT_EQ(prefab.counts.textureBaseBytes, 64u);
    EXPECT_EQ(prefab.counts.textureMipBytes, 88u);
    EXPECT_EQ(prefab.counts.gpuBytes, 216u + 12u + 128u + 88u);
    auto limits = RigidPrefabLimits{}; limits.maximumGpuBytes = prefab.counts.gpuBytes - 1u;
    expectRejected(data, "budget", limits);
    limits.maximumGpuBytes = prefab.counts.gpuBytes;
    ASSERT_TRUE(prepareRigidPrefab(data, {}, limits, prefab, error)) << error;
}

TEST(RigidPrefab, MalformedTextureMetadataAndUnsupportedAppearanceReject) {
    auto data = triangle(); texture(data, 0, moto::VmeshTextureBaseColor, 2, 2);
    data.materials[0].textureOffset[0] = UINT32_MAX; expectRejected(data, "range");
    data.materials[0].textureOffset[0] = 0; data.materials[0].textureSize[0] = 15; expectRejected(data, "range");
    data.materials[0].textureSize[0] = 16; data.materials[0].textureIsSrgb[0] = 0; expectRejected(data, "color space");
    data = triangle(); data.materials[0].alphaMode = moto::VmeshAlphaBlend; expectRejected(data, "rigid material");
    data = triangle(); data.materials[0].roughnessFactor = std::numeric_limits<float>::quiet_NaN(); expectRejected(data, "rigid material");
    data = triangle(); data.materials[0].normalScale = std::numeric_limits<float>::max(); expectRejected(data, "rigid material");
    data.materials[0].normalScale = 16.01f; expectRejected(data, "rigid material");
}

TEST(RigidPrefab, CallersCannotRaiseHardAdmissionCeilings) {
    auto data = triangle();
    for (auto field : {&RigidPrefabLimits::maximumNodes, &RigidPrefabLimits::maximumMeshes,
         &RigidPrefabLimits::maximumSubmeshes, &RigidPrefabLimits::maximumMaterials,
         &RigidPrefabLimits::maximumVertices, &RigidPrefabLimits::maximumIndices,
         &RigidPrefabLimits::maximumTextureDimension, &RigidPrefabLimits::maximumMeshInstances,
         &RigidPrefabLimits::maximumExpandedDraws}) {
        auto limits = RigidPrefabLimits{};
        limits.*field = UINT32_MAX;
        expectRejected(data, "invalid limits", limits);
    }
    for (auto field : {&RigidPrefabLimits::maximumDecodedBytes, &RigidPrefabLimits::maximumGpuBytes}) {
        auto limits = RigidPrefabLimits{}; limits.*field = UINT64_MAX;
        expectRejected(data, "invalid limits", limits);
    }
    auto limits = RigidPrefabLimits{}; limits.maximumMeshes = UINT32_MAX;
    data.header.meshCount = UINT32_MAX; // Tiny owned input cannot trigger a huge bounds allocation.
    expectRejected(data, "invalid limits", limits);
    limits = {}; expectRejected(data, "layout", limits);
}

TEST(RigidPrefab, AllIndependentBudgetsRejectWithoutReplacingOutput) {
    auto data = triangle();
    auto limits = RigidPrefabLimits{}; limits.maximumDecodedBytes = 1; expectRejected(data, "decoded", limits);
    limits = {}; limits.maximumVertices = 2; expectRejected(data, "layout", limits);
    limits = {}; limits.maximumIndices = 2; expectRejected(data, "layout", limits);
    data.nodes.push_back(data.nodes[0]); data.header.nodeCount = 2;
    limits = {}; limits.maximumMeshInstances = 1; expectRejected(data, "instance/draw", limits);
    limits = {}; limits.maximumExpandedDraws = 1; expectRejected(data, "instance/draw", limits);
    limits = {}; limits.maximumNodes = 1; expectRejected(data, "layout", limits);
    data = triangle(); texture(data, 0, moto::VmeshTextureBaseColor, 4, 4);
    limits = {}; limits.maximumTextureDimension = 2; expectRejected(data, "dimensions", limits);
}

TEST(RigidPrefab, PlacementCapacityAndInvalidFinalTransformLeavePriorDraws) {
    RigidPrefab prefab;
    std::string error;
    ASSERT_TRUE(prepareRigidPrefab(triangle(), {}, {}, prefab, error));
    std::vector<RigidPrefabDraw> draws{{7,9,glm::mat4(1.0f)}};
    EXPECT_FALSE(placeRigidPrefab(prefab, glm::dmat4(1.0), {}, 0, draws, error));
    EXPECT_EQ(draws[0].meshIndex, 9u);
    EXPECT_FALSE(placeRigidPrefab(prefab, glm::dmat4(1.0), {{}, {24}}, 1, draws, error));
    EXPECT_EQ(draws[0].meshIndex, 9u);
    const auto farRoot = glm::translate(glm::dmat4(1.0), glm::dvec3(100'000,0,0));
    EXPECT_FALSE(placeRigidPrefab(prefab, farRoot, {}, 1, draws, error));
    EXPECT_EQ(draws[0].meshIndex, 9u);
    auto reflected = glm::dmat4(1.0); reflected[0][0] = -1;
    EXPECT_FALSE(placeRigidPrefab(prefab, reflected, {}, 1, draws, error));
    EXPECT_EQ(draws[0].meshIndex, 9u);
}

// Both nodes deliberately share the same mesh. Rotation must be selected by
// the authored node identity, never by mesh/material index or traversal order.
moto::VmeshData mechanismTriangle(RigidMechanismKind kind) {
    auto data = triangle();
    data.nodes.resize(2);
    data.header.nodeCount = 2;
    data.stringBlob.assign(1, '\0');
    const auto name = [&](std::string_view value) {
        const auto offset = static_cast<uint32_t>(data.stringBlob.size());
        data.stringBlob.append(value); data.stringBlob.push_back('\0'); return offset;
    };
    data.nodes[0].meshIndex = data.nodes[1].meshIndex = 0;
    data.nodes[0].nameOffset = name("voxys_mechanism_static");
    data.nodes[0].translation[0] = 4;
    data.nodes[1].nameOffset = name(kind == RigidMechanismKind::PropellerRotor
        ? "voxys_propeller_rotor" : "voxys_winch_drum");
    data.nodes[1].translation[1] = kind == RigidMechanismKind::WinchDrum ? .12f : 0;
    return data;
}

TEST(RigidPrefab, NamedMechanismUsesCanonicalAxisPivotAndExactNodeAcrossAllPartRotations) {
    for (const auto kind : {RigidMechanismKind::PropellerRotor, RigidMechanismKind::WinchDrum}) {
        const auto data = mechanismTriangle(kind);
        RigidPrefab prefab; std::string error;
        ASSERT_TRUE(prepareRigidPrefab(data, {12}, {}, prefab, error)) << error;
        ASSERT_TRUE(prefab.mechanism);
        EXPECT_EQ(prefab.mechanism->nodeIndex, 1u);
        EXPECT_EQ(prefab.mechanism->sourceAxis, kind == RigidMechanismKind::PropellerRotor
            ? glm::dvec3(0,0,-1) : glm::dvec3(-1,0,0));
        for (uint8_t rotation = 0; rotation < 24; ++rotation) for (int sign : {-1,1}) {
            const construction::GridTransform part{{-75,25,50},{rotation}};
            const auto root = glm::translate(glm::dmat4(1), glm::dvec3(-257,3,511))
                * glm::rotate(glm::dmat4(1), .37, glm::dvec3(0,1,0));
            std::vector<RigidPrefabDraw> neutral, zero, moving;
            ASSERT_TRUE(placeRigidPrefab(prefab,root,part,2,neutral,error)) << error;
            ASSERT_TRUE(placeRigidPrefab(prefab,root,part,2,zero,error,RigidMechanismPose{kind,0})) << error;
            ASSERT_TRUE(placeRigidPrefab(prefab,root,part,2,moving,error,
                RigidMechanismPose{kind,sign*std::acos(-1.0)*.5})) << error;
            for (size_t i=0;i<2;++i)
                EXPECT_EQ(std::memcmp(&neutral[i].modelMatrix,&zero[i].modelMatrix,sizeof(glm::mat4)),0);
            EXPECT_EQ(std::memcmp(&neutral[0].modelMatrix,&moving[0].modelMatrix,sizeof(glm::mat4)),0);
            const bool propeller = kind == RigidMechanismKind::PropellerRotor;
            // Independent canonical quarter-turn oracle, then exact integer
            // part placement; includes the .12 m winch pivot (six ticks).
            const auto expectedTicks = construction::transformPosition(part,
                propeller ? construction::GridPosition{0,-sign*50,0} : construction::GridPosition{0,6+sign*50,0});
            ASSERT_TRUE(expectedTicks);
            const auto metres = construction::toMetres(*expectedTicks); ASSERT_TRUE(metres);
            const glm::vec4 source = propeller ? glm::vec4(1,0,0,1) : glm::vec4(0,0,1,1);
            expectPoint(glm::dvec3(moving[1].modelMatrix*source),
                glm::dvec3(root*glm::dvec4(metres->x,metres->y,metres->z,1)),4e-5);
        }
        EXPECT_EQ(prefab.counts.gpuBytes,292u); // Shared geometry is uploaded once.
        EXPECT_EQ(prefab.counts.meshInstances,2u);
        EXPECT_EQ(prefab.counts.expandedDraws,2u);
    }
}

TEST(RigidPrefab, MechanismContractRejectsMissingDuplicateHierarchyAndWrongPivots) {
    const auto reject = [&](moto::VmeshData data) {
        RigidPrefab output; output.counts.gpuBytes=12345; std::string error;
        EXPECT_FALSE(prepareRigidPrefab(data,{12},{},output,error));
        EXPECT_EQ(output.counts.gpuBytes,12345u);
        EXPECT_NE(error.find("mechanism"),std::string::npos) << error;
    };
    auto data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.nodes[0].nameOffset=0; reject(data);
    data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.nodes[0].nameOffset=data.nodes[1].nameOffset; reject(data);
    data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.nodes[1].parent=0; reject(data);
    data=mechanismTriangle(RigidMechanismKind::WinchDrum);
    data.nodes[1].translation[1]=0; reject(data);
    data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.nodes[1].scale[0]=2; reject(data);
    data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.nodes[1].rotation[2]=std::sqrt(.5f); data.nodes[1].rotation[3]=std::sqrt(.5f); reject(data);
    data=mechanismTriangle(RigidMechanismKind::PropellerRotor);
    data.stringBlob[data.nodes[1].nameOffset+20]='x'; reject(data);
}

TEST(RigidPrefab, MechanismPhaseRefusalIsAtomicAndDoesNotRelaxExistingCapacity) {
    RigidPrefab prefab; std::string error;
    ASSERT_TRUE(prepareRigidPrefab(mechanismTriangle(RigidMechanismKind::PropellerRotor),{12},{},prefab,error)) << error;
    std::vector<RigidPrefabDraw> output(1); output[0].meshIndex=91;
    for (const auto pose : {RigidMechanismPose{RigidMechanismKind::WinchDrum,0},
            RigidMechanismPose{RigidMechanismKind::PropellerRotor,std::numeric_limits<double>::infinity()},
            RigidMechanismPose{RigidMechanismKind::PropellerRotor,std::numeric_limits<double>::quiet_NaN()}}) {
        EXPECT_FALSE(placeRigidPrefab(prefab,glm::dmat4(1),{},2,output,error,pose));
        EXPECT_EQ(output.size(),1u); EXPECT_EQ(output[0].meshIndex,91u);
    }
    EXPECT_FALSE(placeRigidPrefab(prefab,glm::dmat4(1),{},1,output,error,
        RigidMechanismPose{RigidMechanismKind::PropellerRotor,.4}));
    EXPECT_EQ(output[0].meshIndex,91u);
    RigidPrefab legacy;
    ASSERT_TRUE(prepareRigidPrefab(triangle(),{},{},legacy,error)) << error;
    EXPECT_FALSE(placeRigidPrefab(legacy,glm::dmat4(1),{},2,output,error,
        RigidMechanismPose{RigidMechanismKind::PropellerRotor,0}));
    EXPECT_EQ(output[0].meshIndex,91u);
    std::vector<RigidPrefabDraw> near, wrapped;
    ASSERT_TRUE(placeRigidPrefab(prefab,glm::dmat4(1),{},2,near,error,
        RigidMechanismPose{RigidMechanismKind::PropellerRotor,.37})) << error;
    ASSERT_TRUE(placeRigidPrefab(prefab,glm::dmat4(1),{},2,wrapped,error,
        RigidMechanismPose{RigidMechanismKind::PropellerRotor,.37+2000*std::acos(-1.0)})) << error;
    for(size_t n=0;n<2;++n)for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        EXPECT_NEAR(near[n].modelMatrix[c][r],wrapped[n].modelMatrix[c][r],1e-6);
}

} // namespace
} // namespace voxy::game::assets

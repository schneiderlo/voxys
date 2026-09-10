#include "game/assets/cooked_part_bundle.hpp"
#include "core/sha256.hpp"

#include <gtest/gtest.h>
#include <json.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace {
using namespace voxy::game::assets;
using namespace voxy::game::construction;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr char folder[] = "data/salvage/hierarchy_probe/candidates/v1-r02/";
// Independent installed fixture pins. Regeneration is an explicit content
// update; never compute these from the bundle under test to accept changed data.
constexpr char manifestSha[] = "9980954b79dac7d4727aa493e02a71f7b244ad12a474616c14e59a9bd9b24377";
constexpr char goldenSha[] = "39fba894132780dc944be9f29fcd09bfb7bcbdd38cf0b6fc95796f802cc2985f";

Bytes read(std::string_view name) {
    std::ifstream input(std::string(folder) + std::string(name), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 1024 * 1024)
        throw std::runtime_error("Missing or over-bound declared hierarchy runfile");
    Bytes result(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("Short hierarchy runfile read");
    return result;
}
std::string sha(const Bytes& bytes) {
    return voxy::core::sha256Hex(voxy::core::sha256(std::as_bytes(std::span(bytes))));
}
struct Fixture {
    std::unique_ptr<const CookedPartBundle> bundle;
    Json golden;
    Fixture() {
        CookedPartSelection selection;
        constexpr WorldNamespace world{{'v','o','x','y','-','h','i','e','r','-','p','r','o','b','e','1'}};
        selection.part = {{world, 1}, 1};
        selection.manifestSha256 = manifestSha;
        PartLodAdmissionRule rule;
        rule.id = 1;
        rule.limits.maximumVertices = 4000;
        rule.limits.maximumIndices = 6000;
        rule.limits.maximumTextureDimension = 1;
        selection.lodRules.push_back(rule);
        std::string error;
        bundle = admitCookedPartBundle(selection, [](std::string_view name, size_t maximum, std::string& reason) -> std::optional<Bytes> {
            if (name != "cook-manifest.json" && name != "gameplay.json" && name != "lod-1.vmesh") {
                reason = "undeclared fixture file"; return {};
            }
            auto result = read("cooked/" + std::string(name));
            if (result.size() > maximum) { reason = "file exceeds admission cap"; return {}; }
            return result;
        }, error);
        if (!bundle) throw std::runtime_error(error);
        const auto bytes = read("source/golden.json");
        if (sha(bytes) != goldenSha) throw std::runtime_error("Pre-export Blender oracle changed");
        golden = Json::parse(std::string(bytes.begin(), bytes.end()));
    }
    const Json& oracle(const RigidPrefabDraw& draw) const {
        const auto& mesh = bundle->lods().front().mesh;
        const std::string name = mesh.name(mesh.nodes.at(draw.nodeIndex).nameOffset);
        for (const auto& node : golden.at("nodes")) if (node.at("name") == name) return node;
        throw std::runtime_error("No Blender oracle for imported node " + name);
    }
};
glm::dvec3 vector(const Json& values) {
    return {values.at(0).get<double>(), values.at(1).get<double>(), values.at(2).get<double>()};
}
glm::dmat4 goldenMatrix(const Json& node) {
    glm::dmat4 result(1);
    const auto& rows = node.at("canonical_matrix_rows");
    for (size_t row = 0; row < 4; ++row) for (size_t col = 0; col < 4; ++col)
        result[static_cast<glm::length_t>(col)][static_cast<glm::length_t>(row)] = rows.at(row).at(col).get<double>();
    return result;
}
voxy::moto::VmeshVertex vertex(const voxy::moto::VmeshData& mesh, uint32_t index) {
    if (index >= mesh.header.vertexCount) throw std::runtime_error("Index outside vertices");
    voxy::moto::VmeshVertex result;
    std::memcpy(&result, mesh.vertices.data() + static_cast<size_t>(index) * sizeof(result), sizeof(result));
    return result;
}
uint32_t indexAt(const voxy::moto::VmeshData& mesh, size_t offset) {
    if (mesh.header.indexStride == 2) {
        uint16_t value; std::memcpy(&value, mesh.indices.data() + offset, sizeof(value)); return value;
    }
    uint32_t value; std::memcpy(&value, mesh.indices.data() + offset, sizeof(value)); return value;
}
glm::dvec3 position(const voxy::moto::VmeshVertex& value) {
    return {value.position[0], value.position[1], value.position[2]};
}
glm::dvec3 normal(const voxy::moto::VmeshVertex& value) {
    return {value.normal[0], value.normal[1], value.normal[2]};
}

TEST(HierarchyPipeline, PublishedBundleRetainsNestedTransformsAndOneSharedMesh) {
    const Fixture fixture;
    const auto& lod = fixture.bundle->lods().front();
    ASSERT_EQ(lod.mesh.nodes.size(), 8u);
    EXPECT_EQ(lod.prefab.meshBounds.size(), 5u);
    EXPECT_EQ(lod.prefab.meshNodes.size(), 6u);
    EXPECT_EQ(lod.prefab.counts.expandedDraws, 6u);
    EXPECT_EQ(lod.prefab.renderToCanonical.value, 12);
    std::map<std::string, size_t> byName;
    for (size_t i = 0; i < lod.mesh.nodes.size(); ++i)
        byName.emplace(lod.mesh.name(lod.mesh.nodes[i].nameOffset), i);
    const auto& a = lod.mesh.nodes.at(byName.at("nested_leaf_a"));
    const auto& b = lod.mesh.nodes.at(byName.at("nested_leaf_b"));
    const auto& child = lod.mesh.nodes.at(byName.at("nested_child"));
    EXPECT_EQ(a.meshIndex, b.meshIndex);
    EXPECT_EQ(a.parent, static_cast<int32_t>(byName.at("nested_child")));
    EXPECT_EQ(b.parent, a.parent);
    EXPECT_EQ(child.parent, static_cast<int32_t>(byName.at("nested_root")));
    double shear = 0;
    double wrongNormalDifference = 0;
    for (const auto& node : fixture.golden.at("nodes")) {
        const glm::dmat3 matrix(goldenMatrix(node));
        for (int axis = 0; axis < 3; ++axis)
            shear = std::max(shear, std::abs(glm::dot(glm::normalize(matrix[axis]), glm::normalize(matrix[(axis + 1) % 3]))));
        const glm::dvec3 testNormal = glm::normalize(glm::dvec3(1, 2, 3));
        wrongNormalDifference = std::max(wrongNormalDifference, glm::length(
            glm::normalize(matrix * testNormal) - glm::normalize(glm::transpose(glm::inverse(matrix)) * testNormal)));
    }
    EXPECT_GT(shear, .1); // Noncommuting scales produce actual composite shear.
    EXPECT_GT(wrongNormalDifference, .1); // A naive normal transform cannot pass.
    for (const auto& material : lod.mesh.materials) EXPECT_EQ(material.doubleSided, 0);
}

TEST(HierarchyPipeline, EveryTriangleAndNormalMatchesPreExportBlenderOracle) {
    const Fixture fixture;
    const auto& lod = fixture.bundle->lods().front();
    std::vector<RigidPrefabDraw> draws;
    std::string error;
    ASSERT_TRUE(placeRigidPrefab(lod.prefab, glm::dmat4(1), {}, 6, draws, error)) << error;
    size_t total = 0;
    for (const auto& draw : draws) {
        const auto& expected = fixture.oracle(draw).at("triangles");
        std::vector<bool> seen(expected.size(), false);
        const glm::dmat4 matrix(draw.modelMatrix);
        const auto normalMatrix = glm::transpose(glm::inverse(glm::dmat3(matrix)));
        for (const auto& submesh : lod.mesh.submeshes) if (submesh.meshIndex == draw.meshIndex) {
            for (uint32_t first = 0; first < submesh.indexCount; first += 3) {
                std::array<glm::dvec3, 3> points{}, normals{};
                for (size_t corner = 0; corner < 3; ++corner) {
                    const auto value = vertex(lod.mesh, indexAt(lod.mesh,
                        submesh.indexOffset + (static_cast<size_t>(first) + corner) * lod.mesh.header.indexStride));
                    points[corner] = glm::dvec3(matrix * glm::dvec4(position(value), 1));
                    normals[corner] = glm::normalize(normalMatrix * normal(value));
                    // The displayed bound must contain every composed leaf,
                    // including both instances of the nested shared mesh.
                    for (int axis = 0; axis < 3; ++axis) {
                        EXPECT_GE(points[corner][axis], lod.prefab.canonicalBounds.minimum[axis] - .0001);
                        EXPECT_LE(points[corner][axis], lod.prefab.canonicalBounds.maximum[axis] + .0001);
                    }
                }
                const auto outward = glm::normalize(glm::cross(points[1] - points[0], points[2] - points[0]));
                for (const auto& n : normals) EXPECT_GT(glm::dot(outward, n), .9999);
                size_t found = expected.size();
                // Cyclic permutations preserve winding; reversed triangles
                // cannot match. One-to-one matching also rejects duplicates.
                for (size_t i = 0; i < expected.size() && found == expected.size(); ++i) if (!seen[i]) {
                    for (size_t shift = 0; shift < 3; ++shift) {
                        bool match = true;
                        for (size_t corner = 0; corner < 3; ++corner)
                            match = match && glm::length(points[corner] - vector(expected[i].at("positions").at((corner + shift) % 3))) < .0001;
                        if (match) { found = i; break; }
                    }
                }
                ASSERT_LT(found, expected.size()) << "No outward Blender triangle for " << fixture.oracle(draw).at("name");
                seen[found] = true;
                for (const auto& n : normals) EXPECT_LT(glm::length(n - vector(expected[found].at("normal"))), fixture.golden.at("normal_tolerance").get<double>());
                ++total;
            }
        }
        EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }));
    }
    EXPECT_EQ(total, 1128u);
}

TEST(HierarchyPipeline, BlenderGeometrySurvivesAllRotationsAndAdjacentSectorRebasing) {
    const Fixture fixture;
    const auto& lod = fixture.bundle->lods().front();
    for (uint8_t rotation = 0; rotation < 24; ++rotation) {
        const GridTransform placement{{-75,48,-50},{rotation}};
        const auto axes = *rotationMatrix(placement.rotation);
        glm::dmat3 rotate(1);
        for (size_t row = 0; row < 3; ++row) for (size_t col = 0; col < 3; ++col)
            rotate[static_cast<glm::length_t>(col)][static_cast<glm::length_t>(row)] = axes.elements[row * 3 + col];
        const glm::dvec3 sector(1e9, -1e9, -1);
        const auto world = sector * 256.0 + glm::dvec3(255.875, 10, -2);
        std::vector<RigidPrefabDraw> before, after;
        std::string error;
        ASSERT_TRUE(placeRigidPrefab(lod.prefab, glm::translate(glm::dmat4(1), world - sector * 256.0), placement, 6, before, error)) << error;
        ASSERT_TRUE(placeRigidPrefab(lod.prefab, glm::translate(glm::dmat4(1), world - (sector + glm::dvec3(1,0,0)) * 256.0), placement, 6, after, error)) << error;
        for (size_t drawIndex = 0; drawIndex < before.size(); ++drawIndex) {
            const auto& draw = before[drawIndex];
            const auto authoredMatrix = goldenMatrix(fixture.oracle(draw));
            const auto authoredNormal = glm::transpose(glm::inverse(glm::dmat3(authoredMatrix)));
            const auto actualNormal = glm::transpose(glm::inverse(glm::dmat3(draw.modelMatrix)));
            std::set<uint32_t> vertices;
            for (const auto& submesh : lod.mesh.submeshes) if (submesh.meshIndex == draw.meshIndex)
                for (uint32_t i = 0; i < submesh.indexCount; ++i)
                    vertices.insert(indexAt(lod.mesh, submesh.indexOffset + static_cast<size_t>(i) * lod.mesh.header.indexStride));
            for (const auto index : vertices) {
                const auto value = vertex(lod.mesh, index);
                const auto p = position(value), n = normal(value);
                // Undo only the documented Blender glTF exporter mapping.
                // The independent saved Blender matrix already includes all
                // authored parents plus the complete canonical conversion.
                const auto canonical = glm::dvec3(authoredMatrix * glm::dvec4(p.x, -p.z, p.y, 1));
                const auto expected = glm::dvec3(255.875,10,-2) + glm::dvec3(-1.5,.96,-1) + rotate * canonical;
                const auto actual = glm::dvec3(draw.modelMatrix * glm::vec4(p, 1));
                const auto rebased = glm::dvec3(after[drawIndex].modelMatrix * glm::vec4(p, 1));
                EXPECT_LT(glm::length(actual - expected), .0001);
                EXPECT_LT(glm::length(actual - rebased - glm::dvec3(256,0,0)), .0001);
                EXPECT_LT(glm::length(glm::normalize(actualNormal * n)
                    - rotate * glm::normalize(authoredNormal * glm::dvec3(n.x,-n.z,n.y))), .0001);
            }
        }
    }
}
} // namespace

#include "game/assets/rigid_prefab.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxy::game::assets {
namespace {

bool fail(std::string& error, const std::string& reason) {
    error = reason;
    return false;
}

bool addBytes(uint64_t& total, uint64_t count, uint64_t stride, uint64_t limit) {
    if (total > limit || (stride != 0u && count > (limit - total) / stride)) return false;
    total += count * stride;
    return true;
}

bool finite(glm::dvec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::dmat4& value) {
    for (glm::length_t c = 0; c < 4; ++c) for (glm::length_t r = 0; r < 4; ++r) {
        if (!std::isfinite(value[c][r])) return false;
    }
    return true;
}

double frobenius(const glm::dmat3& value) {
    double sum = 0.0;
    for (glm::length_t c = 0; c < 3; ++c) sum += glm::dot(value[c], value[c]);
    return std::sqrt(sum);
}

bool validLimits(const RigidPrefabLimits& limits) {
    constexpr RigidPrefabLimits hard;
    return limits.maximumNodes > 0u && limits.maximumNodes <= hard.maximumNodes
        && limits.maximumMeshes > 0u && limits.maximumMeshes <= hard.maximumMeshes
        && limits.maximumSubmeshes > 0u && limits.maximumSubmeshes <= hard.maximumSubmeshes
        && limits.maximumMaterials > 0u && limits.maximumMaterials <= hard.maximumMaterials
        && limits.maximumVertices > 0u && limits.maximumVertices <= hard.maximumVertices
        && limits.maximumIndices > 0u && limits.maximumIndices <= hard.maximumIndices
        && limits.maximumTextureDimension > 0u && limits.maximumTextureDimension <= hard.maximumTextureDimension
        && limits.maximumMeshInstances > 0u && limits.maximumMeshInstances <= hard.maximumMeshInstances
        && limits.maximumExpandedDraws > 0u && limits.maximumExpandedDraws <= hard.maximumExpandedDraws
        && limits.maximumDecodedBytes > 0u && limits.maximumDecodedBytes <= hard.maximumDecodedBytes
        && limits.maximumGpuBytes > 0u && limits.maximumGpuBytes <= hard.maximumGpuBytes
        && std::isfinite(limits.maximumAbsoluteCoordinate)
        && limits.maximumAbsoluteCoordinate > 0.0 && limits.maximumAbsoluteCoordinate <= hard.maximumAbsoluteCoordinate
        && std::isfinite(limits.maximumTransformCondition) && limits.maximumTransformCondition >= 3.0
        && limits.maximumTransformCondition <= hard.maximumTransformCondition;
}

bool validTransform(const glm::dmat4& value, const RigidPrefabLimits& limits) {
    if (!finite(value) || std::abs(value[0][3]) > 1.0e-12 || std::abs(value[1][3]) > 1.0e-12
        || std::abs(value[2][3]) > 1.0e-12 || std::abs(value[3][3] - 1.0) > 1.0e-12
        || glm::any(glm::greaterThan(glm::abs(glm::dvec3(value[3])),
                                     glm::dvec3(limits.maximumAbsoluteCoordinate)))) return false;
    const glm::dmat3 linear(value);
    const double determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || determinant <= 1.0e-12) return false;
    const double norm = frobenius(linear);
    const double inverseNorm = frobenius(glm::inverse(linear));
    if (!std::isfinite(norm) || !std::isfinite(inverseNorm)
        || norm * inverseNorm > limits.maximumTransformCondition) return false;
    // The shader normalizes unscaled cofactors with a 1e-12 squared-length
    // floor. Conservatively bound every unit input, not just a chosen axis.
    const glm::dmat3 cofactors(glm::cross(linear[1], linear[2]),
                              glm::cross(linear[2], linear[0]),
                              glm::cross(linear[0], linear[1]));
    const double cofactorNorm = frobenius(cofactors);
    const double inverseCofactorNorm = frobenius(glm::inverse(cofactors));
    const double maximumNorm = std::sqrt(double{std::numeric_limits<float>::max()}) / 16.0;
    if (!std::isfinite(cofactorNorm) || !std::isfinite(inverseCofactorNorm)
        || norm >= maximumNorm || cofactorNorm >= maximumNorm
        || inverseNorm >= 990'000.0 || inverseCofactorNorm >= 990'000.0) return false;
    // Match the float determinant actually consumed by MeshPath.
    const float floatDeterminant = glm::determinant(glm::mat3(value));
    return std::isfinite(floatDeterminant) && floatDeterminant > 1.0e-12f;
}

glm::dmat4 basisMatrix(construction::CubeRotation rotation) {
    const auto basis = construction::rotationMatrix(rotation);
    glm::dmat4 result(1.0);
    if (basis) for (glm::length_t c = 0; c < 3; ++c) for (glm::length_t r = 0; r < 3; ++r) {
        result[c][r] = static_cast<double>(basis->elements[static_cast<size_t>(r * 3 + c)]);
    }
    return result;
}

bool includePoint(PrefabBounds& bounds, glm::dvec3 point, double maximum) {
    if (!finite(point) || glm::any(glm::greaterThan(glm::abs(point), glm::dvec3(maximum)))) return false;
    if (!bounds.valid) {
        bounds.minimum = point;
        bounds.maximum = point;
        bounds.valid = true;
    } else {
        bounds.minimum = glm::min(bounds.minimum, point);
        bounds.maximum = glm::max(bounds.maximum, point);
    }
    return true;
}

bool transformBounds(const PrefabBounds& source, const glm::dmat4& matrix,
                     double maximum, PrefabBounds& destination) {
    if (!source.valid || !finite(source.minimum) || !finite(source.maximum)
        || glm::any(glm::greaterThan(source.minimum, source.maximum))) return false;
    for (uint32_t i = 0; i < 8u; ++i) {
        const glm::dvec3 p((i & 1u) ? source.maximum.x : source.minimum.x,
                          (i & 2u) ? source.maximum.y : source.minimum.y,
                          (i & 4u) ? source.maximum.z : source.minimum.z);
        if (!includePoint(destination, glm::dvec3(matrix * glm::dvec4(p, 1.0)), maximum)) return false;
    }
    return true;
}

moto::VmeshVertex vertexAt(const moto::VmeshData& data, uint32_t index) {
    moto::VmeshVertex vertex{};
    std::memcpy(&vertex, data.vertices.data() + static_cast<size_t>(index) * sizeof(vertex), sizeof(vertex));
    return vertex;
}

uint32_t indexAt(const moto::VmeshData& data, uint32_t index) {
    const uint8_t* address = data.indices.data() + static_cast<size_t>(index) * data.header.indexStride;
    uint32_t result = 0;
    if (data.header.indexStride == 2u) {
        uint16_t value = 0;
        std::memcpy(&value, address, sizeof(value));
        result = value;
    } else {
        std::memcpy(&result, address, sizeof(result));
    }
    return result;
}

bool unit(double squaredLength) {
    return std::isfinite(squaredLength) && std::abs(squaredLength - 1.0) <= 1.0e-3;
}

bool validMaterial(const moto::VmeshMaterial& material) {
    for (float value : material.baseColorFactor) if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    for (float value : material.emissiveFactor) if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    return std::isfinite(material.metallicFactor) && material.metallicFactor >= 0.0f && material.metallicFactor <= 1.0f
        && std::isfinite(material.roughnessFactor) && material.roughnessFactor >= 0.0f && material.roughnessFactor <= 1.0f
        && std::isfinite(material.normalScale) && material.normalScale >= 0.0f && material.normalScale <= 16.0f
        && std::isfinite(material.occlusionStrength) && material.occlusionStrength >= 0.0f && material.occlusionStrength <= 1.0f
        && std::isfinite(material.alphaCutoff) && material.alphaMode == moto::VmeshAlphaOpaque
        && material.doubleSided <= 1u && material.unlit <= 1u;
}

} // namespace

bool prepareRigidPrefab(const moto::VmeshData& data, construction::CubeRotation renderToCanonical,
                        const RigidPrefabLimits& limits, RigidPrefab& output, std::string& error) {
    error.clear();
    if (!validLimits(limits) || !construction::isValid(renderToCanonical)) return fail(error, "prefab: invalid limits/basis");
    const auto& h = data.header;
    if (h.version != moto::kVmeshVersion || (h.flags & ~moto::kVmeshHasTangent) != 0u
        || h.vertexStride != sizeof(moto::VmeshVertex) || (h.indexStride != 2u && h.indexStride != 4u)
        || h.vertexCount == 0u || h.vertexCount > limits.maximumVertices
        || h.indexCount == 0u || h.indexCount > limits.maximumIndices
        || h.meshCount == 0u || h.meshCount > limits.maximumMeshes
        || h.nodeCount == 0u || h.nodeCount > limits.maximumNodes || h.nodeCount != data.nodes.size()
        || h.submeshCount == 0u || h.submeshCount > limits.maximumSubmeshes || h.submeshCount != data.submeshes.size()
        || h.materialCount == 0u || h.materialCount > limits.maximumMaterials || h.materialCount != data.materials.size()
        || h.skinCount != 0u || h.jointCount != 0u || h.animCount != 0u || h.animChannelCount != 0u
        || !data.skins.empty() || !data.joints.empty() || !data.anims.empty() || !data.animChannels.empty()
        || !data.channelData.empty()
        || uint64_t{h.vertexCount} * sizeof(moto::VmeshVertex) != data.vertices.size()
        || uint64_t{h.indexCount} * h.indexStride != data.indices.size()) return fail(error, "prefab: rigid counts/layout");

    RigidPrefab pending;
    pending.renderToCanonical = renderToCanonical;
    pending.limits = limits;
    auto& counts = pending.counts;
    for (const size_t bytes : {data.vertices.size(), data.indices.size(), data.images.size(), data.stringBlob.size()}) {
        if (!addBytes(counts.decodedBytes, bytes, 1u, limits.maximumDecodedBytes)) return fail(error, "prefab: decoded byte budget");
    }
    if (!addBytes(counts.decodedBytes, data.nodes.size(), sizeof(moto::VmeshNode), limits.maximumDecodedBytes)
        || !addBytes(counts.decodedBytes, data.submeshes.size(), sizeof(moto::VmeshSubmesh), limits.maximumDecodedBytes)
        || !addBytes(counts.decodedBytes, data.materials.size(), sizeof(moto::VmeshMaterial), limits.maximumDecodedBytes))
        return fail(error, "prefab: decoded byte budget");
    counts.vertexBytes = uint64_t{h.vertexCount} * sizeof(moto::VmeshVertex);
    counts.indexBytes = uint64_t{h.indexCount} * sizeof(uint32_t);
    counts.materialBytes = uint64_t{h.materialCount} * 64u;
    for (const uint64_t bytes : {counts.vertexBytes, counts.indexBytes, counts.materialBytes}) {
        if (!addBytes(counts.gpuBytes, bytes, 1u, limits.maximumGpuBytes)) return fail(error, "prefab: GPU byte budget");
    }
    for (size_t m = 0; m < data.materials.size(); ++m) {
        const auto& material = data.materials[m];
        if (!validMaterial(material)) return fail(error, "materials[" + std::to_string(m) + "]: rigid material");
        for (uint32_t slot = 0; slot < moto::VmeshTextureCount; ++slot) {
            if (material.hasTexture[slot] > 1u || material.textureIsSrgb[slot] > 1u) return fail(error, "material: texture flag");
            if (material.hasTexture[slot] == 0u) continue;
            if ((slot == moto::VmeshTextureBaseColor || slot == moto::VmeshTextureEmissive)
                != (material.textureIsSrgb[slot] != 0u)) return fail(error, "material: texture color space");
            uint32_t width = material.textureWidth[slot];
            uint32_t height = material.textureHeight[slot];
            const uint64_t bytes = uint64_t{width} * height * 4u;
            const size_t offset = material.textureOffset[slot];
            if (width == 0u || height == 0u || width > limits.maximumTextureDimension || height > limits.maximumTextureDimension
                || bytes != material.textureSize[slot] || offset > data.images.size() || bytes > data.images.size() - offset)
                return fail(error, "material: texture dimensions/range");
            if (!addBytes(counts.textureBaseBytes, bytes, 1u, limits.maximumGpuBytes)) return fail(error, "prefab: texture byte budget");
            ++counts.textureCount;
            for (;;) {
                const uint64_t level = uint64_t{width} * height * 4u;
                if (!addBytes(counts.textureMipBytes, level, 1u, limits.maximumGpuBytes)
                    || !addBytes(counts.gpuBytes, level, 1u, limits.maximumGpuBytes)) return fail(error, "prefab: mip/GPU byte budget");
                if (width == 1u && height == 1u) break;
                width = std::max(width / 2u, 1u);
                height = std::max(height / 2u, 1u);
            }
        }
    }

    for (uint32_t i = 0; i < h.vertexCount; ++i) {
        const auto v = vertexAt(data, i);
        const glm::dvec3 p(v.position[0], v.position[1], v.position[2]);
        const glm::dvec3 n(v.normal[0], v.normal[1], v.normal[2]);
        if (!finite(p) || glm::any(glm::greaterThan(glm::abs(p), glm::dvec3(limits.maximumAbsoluteCoordinate)))
            || !unit(glm::dot(n, n))) return fail(error, "vertices[" + std::to_string(i) + "]: position/normal");
        for (float value : v.tangent) if (!std::isfinite(value)) return fail(error, "vertex: tangent");
        for (float value : v.texCoord) if (!std::isfinite(value)) return fail(error, "vertex: UV");
        for (uint16_t value : v.joint) if (value != 0u) return fail(error, "vertex: rigid joint");
        for (float value : v.weight) if (value != 0.0f) return fail(error, "vertex: rigid weight");
    }
    for (uint32_t i = 0; i < h.indexCount; ++i) if (indexAt(data, i) >= h.vertexCount) return fail(error, "indices: vertex out of range");
    pending.meshBounds.resize(h.meshCount);
    std::vector<uint32_t> meshDraws(h.meshCount);
    for (const auto& submesh : data.submeshes) {
        if (submesh.meshIndex >= h.meshCount || submesh.materialIndex >= data.materials.size()
            || submesh.indexCount == 0u || submesh.indexCount % 3u != 0u || submesh.indexOffset % h.indexStride != 0u)
            return fail(error, "submesh: triangle/mesh/material");
        const uint32_t first = submesh.indexOffset / h.indexStride;
        if (first > h.indexCount || submesh.indexCount > h.indexCount - first) return fail(error, "submesh: index span");
        ++meshDraws[submesh.meshIndex];
        const bool normalMapped = data.materials[submesh.materialIndex].hasTexture[moto::VmeshTextureNormal] != 0u;
        if (normalMapped && (h.flags & moto::kVmeshHasTangent) == 0u) return fail(error, "submesh: missing normal-map tangent flag");
        for (uint32_t i = 0; i < submesh.indexCount; ++i) {
            const auto v = vertexAt(data, indexAt(data, first + i));
            if (!includePoint(pending.meshBounds[submesh.meshIndex], glm::dvec3(v.position[0], v.position[1], v.position[2]),
                              limits.maximumAbsoluteCoordinate)) return fail(error, "submesh: position bounds");
            if (normalMapped) {
                const glm::dvec3 tangent(v.tangent[0], v.tangent[1], v.tangent[2]);
                const glm::dvec3 normal(v.normal[0], v.normal[1], v.normal[2]);
                if (!unit(glm::dot(tangent, tangent)) || std::abs(glm::dot(tangent, normal)) > 1.0e-3
                    || std::abs(v.tangent[3]) != 1.0f) return fail(error, "submesh: invalid normal-map tangent frame");
            }
        }
    }
    for (const auto& bounds : pending.meshBounds) if (!bounds.valid) return fail(error, "mesh: no drawable triangles");

    std::vector<glm::dmat4> local(data.nodes.size(), glm::dmat4(1.0));
    std::vector<glm::dmat4> evaluated(data.nodes.size(), glm::dmat4(1.0));
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const auto& node = data.nodes[i];
        if (node.parent < -1 || (node.parent >= 0 && static_cast<size_t>(node.parent) >= data.nodes.size())
            || node.parent == static_cast<int64_t>(i) || node.skinIndex != -1
            || (node.meshIndex != UINT32_MAX && node.meshIndex >= h.meshCount)) return fail(error, "nodes[" + std::to_string(i) + "]: references");
        const glm::dvec3 translation(node.translation[0], node.translation[1], node.translation[2]);
        const glm::dvec3 scale(node.scale[0], node.scale[1], node.scale[2]);
        const glm::dquat rotation(double{node.rotation[3]}, double{node.rotation[0]},
                                  double{node.rotation[1]}, double{node.rotation[2]});
        if (!finite(translation) || !finite(scale) || glm::any(glm::lessThanEqual(scale, glm::dvec3(0.0)))
            || !unit(glm::dot(rotation, rotation))) return fail(error, "nodes[" + std::to_string(i) + "]: finite positive TRS");
        local[i] = glm::translate(glm::dmat4(1.0), translation) * glm::mat4_cast(rotation) * glm::scale(glm::dmat4(1.0), scale);
        if (!validTransform(local[i], limits)) return fail(error, "nodes[" + std::to_string(i) + "]: unsupported numeric transform");
    }
    std::vector<uint8_t> state(data.nodes.size());
    std::vector<size_t> stack;
    stack.reserve(data.nodes.size());
    for (size_t start = 0; start < data.nodes.size(); ++start) {
        if (state[start] == 2u) continue;
        size_t current = start;
        while (state[current] != 2u) {
            if (state[current] == 1u) return fail(error, "nodes: parent cycle");
            state[current] = 1u;
            stack.push_back(current);
            const int32_t parent = data.nodes[current].parent;
            if (parent < 0) break;
            current = static_cast<size_t>(parent);
        }
        while (!stack.empty()) {
            const size_t index = stack.back();
            stack.pop_back();
            const int32_t parent = data.nodes[index].parent;
            evaluated[index] = parent < 0 ? local[index] : evaluated[static_cast<size_t>(parent)] * local[index];
            if (!validTransform(evaluated[index], limits)) return fail(error, "nodes: composed transform range/condition");
            state[index] = 2u;
        }
    }
    const glm::dmat4 basis = basisMatrix(renderToCanonical);
    for (size_t i = 0; i < data.nodes.size(); ++i) {
        const auto& node = data.nodes[i];
        if (node.meshIndex == UINT32_MAX) continue;
        if (counts.meshInstances >= limits.maximumMeshInstances
            || meshDraws[node.meshIndex] > limits.maximumExpandedDraws - counts.expandedDraws)
            return fail(error, "prefab: instance/draw budget");
        ++counts.meshInstances;
        counts.expandedDraws += meshDraws[node.meshIndex];
        pending.meshNodes.push_back({static_cast<uint32_t>(i), node.meshIndex, evaluated[i]});
        if (!transformBounds(pending.meshBounds[node.meshIndex], basis * evaluated[i],
                             limits.maximumAbsoluteCoordinate, pending.canonicalBounds)) return fail(error, "prefab: canonical render bounds");
    }
    if (pending.meshNodes.empty()) return fail(error, "prefab: no mesh-bearing node");
    output = std::move(pending);
    return true;
}

bool placeRigidPrefab(const RigidPrefab& prefab, const glm::dmat4& cameraRelativeRoot,
                      construction::GridTransform gridPart, size_t maximumDrawRecords,
                      std::vector<RigidPrefabDraw>& output, std::string& error) {
    error.clear();
    if (!validLimits(prefab.limits) || !construction::isValid(prefab.renderToCanonical)
        || !construction::isValid(gridPart.rotation) || !construction::isValid(gridPart.translation)
        || prefab.meshNodes.empty() || prefab.meshNodes.size() > maximumDrawRecords
        || prefab.meshNodes.size() > prefab.limits.maximumMeshInstances
        || !validTransform(cameraRelativeRoot, prefab.limits)) return fail(error, "placement: invalid root/part/capacity");
    const auto metres = construction::toMetres(gridPart.translation);
    if (!metres) return fail(error, "placement: invalid ticks");
    const glm::dmat4 rootPartBasis = cameraRelativeRoot
        * glm::translate(glm::dmat4(1.0), glm::dvec3(metres->x, metres->y, metres->z))
        * basisMatrix(gridPart.rotation) * basisMatrix(prefab.renderToCanonical);
    std::vector<RigidPrefabDraw> pending;
    pending.reserve(prefab.meshNodes.size());
    for (const auto& node : prefab.meshNodes) {
        const glm::dmat4 matrix = rootPartBasis * node.nodeToAsset;
        PrefabBounds transformed;
        if (node.meshIndex >= prefab.meshBounds.size() || !validTransform(matrix, prefab.limits)
            || !transformBounds(prefab.meshBounds[node.meshIndex], matrix,
                                prefab.limits.maximumAbsoluteCoordinate, transformed)) return fail(error, "placement: matrix/bounds range");
        pending.push_back({node.nodeIndex, node.meshIndex, glm::mat4(matrix)});
    }
    output = std::move(pending);
    return true;
}

} // namespace voxy::game::assets

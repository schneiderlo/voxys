#include "salvage_assets/gameplay_sidecar.hpp"
#include "moto/vmesh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <limits>

namespace {
bool drawable(const voxy::moto::VmeshData& mesh) {
    const auto& header = mesh.header;
    if (header.vertexCount < 3u || header.indexCount < 3u || header.meshCount == 0u
        || mesh.submeshes.empty() || mesh.materials.empty()
        || header.vertexStride != sizeof(voxy::moto::VmeshVertex)
        || (header.indexStride != 2u && header.indexStride != 4u)) return false;
    // readVmesh has checked byte bounds. Its format permits empty assets and
    // does not validate triangle contents; a catalog visual must be drawable.
    for (uint32_t i = 0; i < header.vertexCount; ++i) {
        voxy::moto::VmeshVertex vertex{};
        std::memcpy(&vertex, mesh.vertices.data() + size_t{i} * sizeof(vertex), sizeof(vertex));
        double normalLengthSquared = 0;
        for (float value : vertex.position) if (!std::isfinite(value)) return false;
        for (float value : vertex.normal) {
            if (!std::isfinite(value)) return false;
            normalLengthSquared += static_cast<double>(value) * static_cast<double>(value);
        }
        if (normalLengthSquared <= static_cast<double>(std::numeric_limits<float>::min())) return false;
        for (float value : vertex.tangent) if (!std::isfinite(value)) return false;
        for (float value : vertex.texCoord) if (!std::isfinite(value)) return false;
        for (float value : vertex.weight) if (!std::isfinite(value)) return false;
    }
    for (uint32_t i = 0; i < header.indexCount; ++i) {
        uint32_t index = 0;
        const auto* source = mesh.indices.data() + size_t{i} * header.indexStride;
        if (header.indexStride == 2u) {
            uint16_t small = 0; std::memcpy(&small, source, sizeof(small)); index = small;
        } else std::memcpy(&index, source, sizeof(index));
        if (index >= header.vertexCount) return false;
    }
    for (const auto& submesh : mesh.submeshes) {
        if (submesh.indexCount == 0u || submesh.indexCount % 3u != 0u
            || submesh.indexOffset % header.indexStride != 0u
            || submesh.materialIndex >= mesh.materials.size()
            || submesh.meshIndex >= header.meshCount) return false;
        const auto first = submesh.indexOffset / header.indexStride;
        if (first > header.indexCount || submesh.indexCount > header.indexCount - first) return false;
    }
    return true;
}

std::optional<std::vector<uint8_t>> readBounded(const std::filesystem::path& path, size_t maximum) {
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || error || !std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum) return std::nullopt;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size) + 1);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.bad() || stream.gcount() != static_cast<std::streamsize>(size)) return std::nullopt;
    bytes.resize(static_cast<size_t>(size));
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    using namespace voxy::tools::salvage;
    if (argc != 3) {
        std::cerr << "usage: gameplay_sidecar_tool <sidecar.json> <cooked-lod-directory>\n";
        return 2;
    }
    const auto input = readBounded(argv[1], kMaximumSidecarBytes);
    if (!input) { std::cerr << "sidecar file missing, symlinked, changed or oversized\n"; return 1; }
    const std::filesystem::path root(argv[2]);
    std::string error;
    const auto resolver = [&root](const voxy::game::construction::CookedMeshVisual& visual) {
        const auto bytes = readBounded(root / visual.path, kMaximumCookedVmeshBytes);
        if (!bytes) return false;
        voxy::moto::VmeshData mesh;
        std::string reason;
        return voxy::moto::readVmesh(bytes->data(), bytes->size(), &mesh, &reason) && drawable(mesh);
    };
    const auto sidecar = parseGameplaySidecar(
        std::string_view(reinterpret_cast<const char*>(input->data()), input->size()), resolver, error);
    if (!sidecar) { std::cerr << error << '\n'; return 1; }
    std::cout << sidecar->normalizedJson;
    return std::cout ? 0 : 1;
}

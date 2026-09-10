// ═══════════════════════════════════════════════════════════════════════════════
// gltf_vmesh_tool.cpp - glTF 2.0 to .vmesh offline converter
// ═══════════════════════════════════════════════════════════════════════════════
// Reads a glTF 2.0 document (ASCII .gltf or binary .glb) from memory and emits
// the runtime .vmesh form described in moto/vmesh.hpp. The converter is a pure
// byte-in / VmeshData-out function so unit tests run without a filesystem.
// ═══════════════════════════════════════════════════════════════════════════════

#include "gltf_vmesh_tool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stb_image.h>
#include <tiny_gltf.h>
#include <json.hpp>

namespace voxy::tools {

namespace {

constexpr size_t kMaxInputBytes = size_t(1) << 30u;     // 1 GiB
constexpr size_t kMaxAccessorCount = size_t(1) << 24u;  // 16M elements
constexpr size_t kMaxTotalVertices = size_t(1) << 24u;  // 16M vertices
constexpr size_t kMaxTotalIndices = size_t(1) << 26u;   // 64M indices
constexpr size_t kRigidInputBytes = 64u * 1024u * 1024u;
constexpr size_t kRigidJsonBytes = 1024u * 1024u;
constexpr size_t kRigidImageBytes = 128u * 1024u * 1024u;
constexpr size_t kRigidOutputBytes = 256u * 1024u * 1024u;
constexpr size_t kRigidVertices = size_t{1} << 20u;
constexpr size_t kRigidIndices = 3u * kRigidVertices;

struct ImportContext {
    bool rigid = false;
    size_t decodedImageBytes = 0;
};

bool fail(std::string* error, const std::string& reason) {
    if (error != nullptr) *error = reason;
    return false;
}

using Json = nlohmann::json;

[[noreturn]] void reject(const std::string& path, const std::string& reason) {
    throw std::runtime_error(path + ": " + reason);
}

size_t jsonSize(const Json& value, size_t maximum, const std::string& path) {
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned()
        && value.get<int64_t>() < 0)) reject(path, "expected nonnegative integer");
    const uint64_t number = value.get<uint64_t>();
    if (number > maximum) reject(path, "integer exceeds limit");
    return static_cast<size_t>(number);
}

const Json& jsonArray(const Json& parent, const char* name, size_t maximum,
                      const std::string& path) {
    static const Json empty = Json::array();
    if (!parent.contains(name)) return empty;
    const auto& values = parent.at(name);
    if (!values.is_array() || values.size() > maximum) reject(path + "." + name, "array limit/type");
    return values;
}

bool embeddedUri(const std::string& uri) {
    constexpr std::string_view prefixes[] = {"data:application/octet-stream;base64,",
        "data:application/gltf-buffer;base64,", "data:image/png;base64,", "data:image/jpeg;base64,"};
    for (auto prefix : prefixes) {
        if (!uri.starts_with(prefix)) continue;
        const auto payload = std::string_view(uri).substr(prefix.size());
        if (payload.empty() || payload.size() % 4u != 0u) return false;
        size_t padding = 0;
        for (size_t i = 0; i < payload.size(); ++i) {
            const char c = payload[i];
            if (c == '=') {
                if (i < payload.size() - 2u || ++padding > 2u) return false;
            } else if (padding != 0u || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || (c >= '0' && c <= '9') || c == '+' || c == '/')) return false;
        }
        return true;
    }
    return false;
}

void inspectJson(const Json& value, const std::string& path, size_t* remaining) {
    if (*remaining == 0u) reject(path, "JSON value limit");
    --*remaining;
    if (value.is_number_float() && !std::isfinite(value.get<double>())) reject(path, "nonfinite number");
    if (value.is_string() && value.get_ref<const std::string&>().size() > kRigidJsonBytes) reject(path, "string limit");
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "extensions") {
                if (!it.value().is_object()) reject(path, "extensions must be object");
                for (auto ext = it.value().begin(); ext != it.value().end(); ++ext) {
                    const bool material = path.starts_with("materials[") && path.find('.') == std::string::npos;
                    if (!material || ext.key() != "KHR_materials_unlit" || !ext.value().is_object()
                        || !ext.value().empty()) reject(path + ".extensions." + ext.key(), "unsupported extension");
                }
            }
            inspectJson(it.value(), path.empty() ? it.key() : path + "." + it.key(), remaining);
        }
    } else if (value.is_array()) {
        for (size_t i = 0; i < value.size(); ++i) inspectJson(value[i], path + "[" + std::to_string(i) + "]", remaining);
    }
}

uint32_t u32le(const uint8_t* bytes) {
    return uint32_t{bytes[0]} | (uint32_t{bytes[1]} << 8u)
        | (uint32_t{bytes[2]} << 16u) | (uint32_t{bytes[3]} << 24u);
}

bool preflightRigid(const std::vector<uint8_t>& input, bool isGlb, std::string* error) {
    try {
        if (!isGlb || input.size() < 20u || input.size() > kRigidInputBytes
            || u32le(input.data()) != 0x46546c67u || u32le(input.data() + 4u) != 2u
            || u32le(input.data() + 8u) != input.size()) reject("GLB", "invalid header/size or non-GLB input");
        size_t offset = 12u;
        std::string_view text;
        size_t chunks = 0;
        while (offset < input.size()) {
            if (input.size() - offset < 8u || chunks >= 2u) reject("GLB", "chunk header/count");
            const size_t size = u32le(input.data() + offset);
            const uint32_t type = u32le(input.data() + offset + 4u);
            offset += 8u;
            if (size == 0u || size % 4u != 0u || size > input.size() - offset
                || type != (chunks == 0u ? 0x4e4f534au : 0x004e4942u)) reject("GLB", "chunk type/size");
            if (chunks == 0u) {
                if (size > kRigidJsonBytes) reject("GLB", "JSON byte limit");
                text = {reinterpret_cast<const char*>(input.data() + offset), size};
            }
            offset += size;
            ++chunks;
        }
        std::vector<std::set<std::string>> keys;
        const auto document = Json::parse(text, [&keys](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 32) reject("JSON", "nesting limit");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            else if (event == Json::parse_event_t::key) {
                const auto& key = parsed.get_ref<const std::string&>();
                if (key.size() > 128u || keys.empty() || !keys.back().insert(key).second) reject("JSON", "duplicate/oversized key");
            } else if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        if (!document.is_object() || !document.contains("asset") || !document["asset"].is_object()
            || document["asset"].value("version", "") != "2.0"
            || (document["asset"].contains("minVersion") && document["asset"]["minVersion"] != "2.0")) reject("asset", "unsupported version");
        size_t remaining = 100000u;
        inspectJson(document, "", &remaining);
        for (const char* key : {"extensionsRequired", "extensionsUsed"}) {
            for (const auto& extension : jsonArray(document, key, 1u, "root"))
                if (extension != "KHR_materials_unlit") reject(key, "unsupported extension");
        }
        for (const char* key : {"skins", "animations", "cameras"})
            if (!jsonArray(document, key, 0u, "root").empty()) reject(key, "unsupported rigid feature");
        for (const char* key : {"materials", "images", "textures", "samplers"}) (void)jsonArray(document, key, 64u, "root");
        for (const char* key : {"accessors", "bufferViews", "buffers"}) (void)jsonArray(document, key, 4096u, "root");
        const auto& scenes = jsonArray(document, "scenes", 1u, "root");
        const auto& nodes = jsonArray(document, "nodes", 256u, "root");
        const auto& meshes = jsonArray(document, "meshes", 256u, "root");
        if (scenes.size() != 1u || nodes.empty() || meshes.empty()) reject("scene", "one nonempty rigid scene required");
        if (document.contains("scene")) (void)jsonSize(document["scene"], 0u, "scene");
        size_t bufferBytes = 0u;
        for (const char* category : {"buffers", "images"}) {
            size_t recordIndex = 0u;
            for (const auto& record : jsonArray(document, category, 4096u, "root")) {
                if (!record.is_object()) reject(category, "record type");
                if (record.contains("uri") && (!record["uri"].is_string()
                    || !embeddedUri(record["uri"].get<std::string>()))) reject(category, "external/invalid embedded URI");
                if (std::string_view(category) == "buffers") {
                    const size_t size = jsonSize(record.at("byteLength"), kRigidInputBytes, "buffer.byteLength");
                    if (size == 0u || size > kRigidInputBytes - bufferBytes) reject("buffers", "aggregate byte limit");
                    bufferBytes += size;
                    if (!record.contains("uri") && recordIndex != 0u) reject("buffers", "only buffer zero may use BIN chunk");
                } else if (record.contains("mimeType")) {
                    if (record["mimeType"] != "image/png" && record["mimeType"] != "image/jpeg") reject("image.mimeType", "unsupported format");
                    if (record.contains("uri")) {
                        const auto uri = record["uri"].get<std::string>();
                        const auto mime = record["mimeType"].get<std::string>();
                        if (!uri.starts_with("data:" + mime + ";base64,")) reject("image.mimeType", "URI MIME mismatch");
                    }
                }
                if (std::string_view(category) == "images" && record.contains("bufferView")) {
                    (void)jsonSize(record["bufferView"], 4095u, "image.bufferView");
                    if (!record.contains("mimeType")) reject("image.mimeType", "required for buffer view");
                }
                ++recordIndex;
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            const std::string path = "nodes[" + std::to_string(i) + "]";
            if (!node.is_object() || node.contains("skin") || node.contains("weights") || node.contains("camera")) reject(path, "unsupported node");
            if (node.contains("matrix") && (node.contains("translation") || node.contains("rotation") || node.contains("scale"))) reject(path, "matrix and TRS are exclusive");
            for (const auto& [key, count] : {std::pair{"matrix", 16u}, {"translation", 3u}, {"rotation", 4u}, {"scale", 3u}}) {
                if (!node.contains(key)) continue;
                const auto& values = node[key];
                if (!values.is_array() || values.size() != count) reject(path + "." + key, "wrong component count");
                for (const auto& value : values) if (!value.is_number() || !std::isfinite(value.get<double>())
                    || std::abs(value.get<double>()) > static_cast<double>(std::numeric_limits<float>::max())) reject(path + "." + key, "nonfinite/out-of-range component");
            }
            if (node.contains("mesh")) (void)jsonSize(node["mesh"], meshes.size() - 1u, path + ".mesh");
        }
        std::vector<int> parents(nodes.size(), -1);
        for (size_t i = 0; i < nodes.size(); ++i) {
            for (const auto& child : jsonArray(nodes[i], "children", 256u, "nodes")) {
                const size_t index = jsonSize(child, nodes.size() - 1u, "node child");
                if (parents[index] != -1 || index == i) reject("nodes", "duplicate parent/self cycle");
                parents[index] = static_cast<int>(i);
            }
        }
        const auto& roots = jsonArray(scenes[0], "nodes", 256u, "scene");
        if (roots.empty()) reject("scene", "no roots");
        std::vector<bool> visited(nodes.size(), false);
        std::vector<size_t> stack;
        for (const auto& root : roots) {
            const size_t index = jsonSize(root, nodes.size() - 1u, "scene root");
            if (parents[index] != -1) reject("scene root", "root has parent");
            stack.push_back(index);
        }
        while (!stack.empty()) {
            const size_t index = stack.back(); stack.pop_back();
            if (visited[index]) reject("scene", "repeated node/cycle");
            visited[index] = true;
            for (const auto& child : jsonArray(nodes[index], "children", 256u, "nodes")) stack.push_back(jsonSize(child, nodes.size() - 1u, "child"));
        }
        if (std::find(visited.begin(), visited.end(), false) != visited.end()) reject("nodes", "unreachable node/cycle");
        for (const auto& accessor : jsonArray(document, "accessors", 4096u, "root")) {
            if (!accessor.is_object() || accessor.contains("sparse")) reject("accessor", "sparse/type unsupported");
            (void)jsonSize(accessor.at("count"), kRigidIndices, "accessor.count");
            (void)jsonSize(accessor.at("componentType"), 5126u, "accessor.componentType");
            if (accessor.contains("normalized") && !accessor["normalized"].is_boolean()) reject("accessor.normalized", "expected boolean");
            for (const char* key : {"byteOffset", "bufferView"}) if (accessor.contains(key)) (void)jsonSize(accessor[key], kRigidInputBytes, std::string("accessor.") + key);
        }
        for (const auto& view : jsonArray(document, "bufferViews", 4096u, "root")) {
            if (!view.is_object()) reject("bufferView", "record type");
            (void)jsonSize(view.at("buffer"), 4095u, "bufferView.buffer");
            for (const char* key : {"byteOffset", "byteLength", "byteStride"}) if (view.contains(key)) (void)jsonSize(view[key], kRigidInputBytes, std::string("bufferView.") + key);
            if (view.contains("target")) {
                const size_t target = jsonSize(view["target"], 34963u, "bufferView.target");
                if (target != 34962u && target != 34963u) reject("bufferView.target", "unsupported target");
            }
        }
        for (const auto& texture : jsonArray(document, "textures", 64u, "root")) {
            if (!texture.is_object()) reject("texture", "record type");
            (void)jsonSize(texture.at("source"), 63u, "texture.source");
            if (texture.contains("sampler")) (void)jsonSize(texture["sampler"], 63u, "texture.sampler");
        }
        for (const auto& sampler : jsonArray(document, "samplers", 64u, "root")) {
            if (!sampler.is_object()) reject("sampler", "record type");
            for (const char* key : {"wrapS", "wrapT", "magFilter", "minFilter"}) {
                if (!sampler.contains(key)) continue;
                const size_t expected = std::string_view(key).starts_with("wrap") ? 10497u
                    : (std::string_view(key) == "magFilter" ? 9729u : 9987u);
                if (jsonSize(sampler[key], expected, std::string("sampler.") + key) != expected) reject("sampler", "only repeat/linear/trilinear supported");
            }
        }
        for (const auto& material : jsonArray(document, "materials", 64u, "root")) {
            if (!material.is_object()) reject("material", "record type");
            if (material.contains("doubleSided") && !material["doubleSided"].is_boolean()) reject("material.doubleSided", "expected boolean");
            if (material.contains("occlusionTexture")) reject("material", "AO texture unsupported");
            if (material.contains("alphaMode") && material["alphaMode"] != "OPAQUE") reject("material.alphaMode", "only opaque supported");
            const auto scalar = [](const Json& owner, const char* key, double maximum) {
                if (!owner.contains(key)) return;
                const auto& value = owner[key];
                if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0.0
                    || value.get<double>() > maximum) reject(key, "invalid factor");
            };
            const auto vector = [&scalar](const Json& owner, const char* key, size_t count) {
                if (!owner.contains(key)) return;
                const auto& values = owner[key];
                if (!values.is_array() || values.size() != count) reject(key, "invalid factor components");
                for (const auto& value : values) scalar(Json{{"component", value}}, "component", 1.0);
            };
            scalar(material, "alphaCutoff", 1.0); vector(material, "emissiveFactor", 3u);
            const auto textureInfo = [](const Json& owner, const char* key) {
                if (!owner.contains(key)) return;
                const auto& info = owner[key];
                if (!info.is_object()) reject(key, "texture info type");
                (void)jsonSize(info.at("index"), 63u, std::string(key) + ".index");
                if (info.contains("texCoord")) (void)jsonSize(info["texCoord"], 0u, std::string(key) + ".texCoord");
            };
            textureInfo(material, "normalTexture"); textureInfo(material, "emissiveTexture");
            if (material.contains("normalTexture")) scalar(material["normalTexture"], "scale", static_cast<double>(std::numeric_limits<float>::max()));
            if (material.contains("pbrMetallicRoughness")) {
                const auto& pbr = material["pbrMetallicRoughness"];
                if (!pbr.is_object()) reject("pbrMetallicRoughness", "expected object");
                textureInfo(pbr, "baseColorTexture"); textureInfo(pbr, "metallicRoughnessTexture");
                scalar(pbr, "metallicFactor", 1.0); scalar(pbr, "roughnessFactor", 1.0);
                vector(pbr, "baseColorFactor", 4u);
            }
        }
        size_t primitives = 0;
        for (size_t m = 0; m < meshes.size(); ++m) {
            if (!meshes[m].is_object() || meshes[m].contains("weights")) reject("mesh", "morph weights unsupported");
            const auto& entries = jsonArray(meshes[m], "primitives", 512u, "mesh");
            if (entries.empty() || entries.size() > 512u - primitives) reject("meshes", "primitive count limit");
            primitives += entries.size();
            for (const auto& primitive : entries) {
                if (!primitive.is_object() || primitive.contains("targets")) reject("primitive", "morph/type unsupported");
                if (primitive.contains("mode") && jsonSize(primitive["mode"], 4u, "primitive.mode") != 4u) reject("primitive", "only triangles supported");
                const auto& attributes = primitive.at("attributes");
                if (!attributes.is_object() || !attributes.contains("POSITION") || !attributes.contains("NORMAL")) reject("attributes", "POSITION and NORMAL required");
                for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                    if (it.key() != "POSITION" && it.key() != "NORMAL" && it.key() != "TANGENT" && it.key() != "TEXCOORD_0") reject("attributes." + it.key(), "unsupported rigid attribute");
                    (void)jsonSize(it.value(), 4095u, "attributes." + it.key());
                }
                if (primitive.contains("indices")) (void)jsonSize(primitive["indices"], 4095u, "primitive.indices");
                if (primitive.contains("material")) (void)jsonSize(primitive["material"], 63u, "primitive.material");
            }
        }
        return true;
    } catch (const std::exception& exception) { return fail(error, std::string("salvage-rigid-v1: ") + exception.what()); }
}

int gltfComponentCount(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 0;
    }
}

int gltfComponentSize(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return 2;
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return 4;
        default:
            return 0;
    }
}

// Bounds-checked view into a resolved accessor's raw component stream.
struct AccessorView {
    const uint8_t* bytes = nullptr;
    size_t count = 0;
    int componentType = -1;
    int componentCount = 0;
    size_t stride = 0;
    bool normalized = false;
};

bool resolveAccessor(const tinygltf::Model& model, int accessorIndex,
                     AccessorView* view, std::string* error, bool rigid = false) {
    if (accessorIndex < 0) return fail(error, "missing accessor");
    const size_t index = static_cast<size_t>(accessorIndex);
    if (index >= model.accessors.size()) return fail(error, "accessor index out of range");
    const tinygltf::Accessor& accessor = model.accessors[index];
    if (accessor.sparse.isSparse) return fail(error, "sparse accessors are not supported");
    if (accessor.count == 0u) return fail(error, "accessor has zero count");
    if (accessor.count > kMaxAccessorCount) return fail(error, "accessor too large");
    if (accessor.bufferView < 0) return fail(error, "accessor has no buffer view");

    const size_t viewIndex = static_cast<size_t>(accessor.bufferView);
    if (viewIndex >= model.bufferViews.size()) return fail(error, "accessor buffer view out of range");
    const tinygltf::BufferView& bufferView = model.bufferViews[viewIndex];
    if (bufferView.buffer < 0) return fail(error, "buffer view has no buffer");
    const size_t bufferIndex = static_cast<size_t>(bufferView.buffer);
    if (bufferIndex >= model.buffers.size()) return fail(error, "buffer view buffer out of range");
    const tinygltf::Buffer& buffer = model.buffers[bufferIndex];

    const int componentCount = gltfComponentCount(accessor.type);
    const int componentSize = gltfComponentSize(accessor.componentType);
    if (componentCount == 0) return fail(error, "unsupported accessor type");
    if (componentSize == 0) return fail(error, "unsupported component type");

    const size_t elementSize = static_cast<size_t>(componentSize) * static_cast<size_t>(componentCount);
    size_t stride = elementSize;
    if (bufferView.byteStride > 0u) {
        if (bufferView.byteStride < elementSize || bufferView.byteStride > 252u) {
            return fail(error, "invalid buffer view stride");
        }
        stride = bufferView.byteStride;
    }
    if (bufferView.byteOffset > buffer.data.size()
        || bufferView.byteLength > buffer.data.size() - bufferView.byteOffset) return fail(error, "buffer view out of range");
    if (accessor.byteOffset > bufferView.byteLength
        || elementSize > bufferView.byteLength - accessor.byteOffset
        || accessor.count - 1u > (bufferView.byteLength - accessor.byteOffset - elementSize) / stride)
        return fail(error, "accessor data out of range");
    if (rigid && (accessor.byteOffset % static_cast<size_t>(componentSize) != 0u
        || bufferView.byteOffset % static_cast<size_t>(componentSize) != 0u
        || stride % static_cast<size_t>(componentSize) != 0u)) return fail(error, "accessor component alignment");
    if (buffer.data.empty()) return fail(error, "buffer has no data");

    view->bytes = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    view->count = accessor.count;
    view->componentType = accessor.componentType;
    view->componentCount = componentCount;
    view->stride = stride;
    view->normalized = accessor.normalized;
    return true;
}

float readFloatComponent(const AccessorView& view, size_t element, int component) {
    const uint8_t* p = view.bytes + element * view.stride +
                       static_cast<size_t>(component) * static_cast<size_t>(gltfComponentSize(view.componentType));
    switch (view.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float value = 0.0f;
            std::memcpy(&value, p, sizeof(value));
            return value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t value = p[0];
            if (view.normalized) return static_cast<float>(value) * (1.0f / 255.0f);
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            const int8_t value = static_cast<int8_t>(p[0]);
            if (view.normalized) return std::max(static_cast<float>(value) * (1.0f / 127.0f), -1.0f);
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            if (view.normalized) return static_cast<float>(value) * (1.0f / 65535.0f);
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            int16_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            if (view.normalized) return std::max(static_cast<float>(value) * (1.0f / 32767.0f), -1.0f);
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_INT: {
            int32_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            return static_cast<float>(value);
        }
        default:
            return 0.0f;
    }
}

uint32_t readRawUInt(const AccessorView& view, size_t element, int component) {
    const uint8_t* p = view.bytes + element * view.stride +
                       static_cast<size_t>(component) * static_cast<size_t>(gltfComponentSize(view.componentType));
    switch (view.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return p[0];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            return value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t value = 0;
            std::memcpy(&value, p, sizeof(value));
            return value;
        }
        default:
            return 0u;
    }
}

void appendU16LE(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
}

void appendU32LE(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 16u) & 0xFFu));
    out->push_back(static_cast<uint8_t>((value >> 24u) & 0xFFu));
}

void appendFloatLE(std::vector<uint8_t>* out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32LE(out, bits);
}

void appendVertex(std::vector<uint8_t>* out, const moto::VmeshVertex& vertex) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&vertex);
    out->insert(out->end(), bytes, bytes + sizeof(moto::VmeshVertex));
}

uint32_t storeName(std::string* blob, const std::string& name) {
    if (name.empty()) return 0u;
    const uint32_t offset = static_cast<uint32_t>(blob->size());
    blob->append(name);
    blob->push_back('\0');
    return offset;
}

struct Vec2 {
    float u = 0.0f;
    float v = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool finiteVec3(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct Mat4 {
    float m[16];
};

// Decompose a column-major TRS matrix with Shepperd's rotation extraction.
bool decomposeMatrix(const Mat4& matrix, float translation[3], float rotation[4],
                     float scale[3], std::string* error) {
    for (int c = 0; c < 16; ++c) {
        if (!std::isfinite(matrix.m[c])) return fail(error, "non-finite node matrix");
    }
    if (matrix.m[3] != 0.0f || matrix.m[7] != 0.0f || matrix.m[11] != 0.0f || matrix.m[15] != 1.0f)
        return fail(error, "node matrix is not affine");
    translation[0] = matrix.m[12];
    translation[1] = matrix.m[13];
    translation[2] = matrix.m[14];

    auto column = [&matrix](int col, Vec3* out) {
        out->x = matrix.m[col * 4 + 0];
        out->y = matrix.m[col * 4 + 1];
        out->z = matrix.m[col * 4 + 2];
    };
    Vec3 x;
    Vec3 y;
    Vec3 z;
    column(0, &x);
    column(1, &y);
    column(2, &z);

    const auto length = [](Vec3 v) { return std::hypot(static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)); };
    const double dx = length(x), dy = length(y), dz = length(z);
    if (dx > static_cast<double>(std::numeric_limits<float>::max()) || dy > static_cast<double>(std::numeric_limits<float>::max())
        || dz > static_cast<double>(std::numeric_limits<float>::max())) return fail(error, "node matrix scale overflow");
    const float scaleX = static_cast<float>(dx);
    const float scaleY = static_cast<float>(dy);
    const float scaleZ = static_cast<float>(dz);
    if (scaleX < 1e-30f || scaleY < 1e-30f || scaleZ < 1e-30f) {
        return fail(error, "degenerate node matrix");
    }
    Vec3 rx = x * (1.0f / scaleX);
    Vec3 ry = y * (1.0f / scaleY);
    Vec3 rz = z * (1.0f / scaleZ);
    const float determinant = dot(cross(rx, ry), rz);
    if (std::abs(dot(rx, ry)) > 1.0e-5f || std::abs(dot(rx, rz)) > 1.0e-5f
        || std::abs(dot(ry, rz)) > 1.0e-5f || std::abs(std::abs(determinant) - 1.0f) > 1.0e-5f)
        return fail(error, "node matrix contains shear");
    scale[0] = scaleX;
    scale[1] = scaleY;
    scale[2] = determinant < 0.0f ? -scaleZ : scaleZ;
    if (determinant < 0.0f) rz = rz * -1.0f;

    const float m00 = rx.x;
    const float m01 = ry.x;
    const float m02 = rz.x;
    const float m10 = rx.y;
    const float m11 = ry.y;
    const float m12 = rz.y;
    const float m20 = rx.z;
    const float m21 = ry.z;
    const float m22 = rz.z;
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        rotation[3] = 0.25f * s;
        rotation[0] = (m21 - m12) / s;
        rotation[1] = (m02 - m20) / s;
        rotation[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        rotation[3] = (m21 - m12) / s;
        rotation[0] = 0.25f * s;
        rotation[1] = (m01 + m10) / s;
        rotation[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        rotation[3] = (m02 - m20) / s;
        rotation[0] = (m01 + m10) / s;
        rotation[1] = 0.25f * s;
        rotation[2] = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        rotation[3] = (m10 - m01) / s;
        rotation[0] = (m02 + m20) / s;
        rotation[1] = (m12 + m21) / s;
        rotation[2] = 0.25f * s;
    }
    return true;
}

struct DecodedImage {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Strict glTF uses STB's top-row-first order. Preserve historical legacy rows.
bool decodeRgbaBytes(const uint8_t* bytes, size_t byteCount, DecodedImage* out,
                     std::string* error, ImportContext* context = nullptr) {
    if (byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return fail(error, "image too large");
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    const bool rigid = context != nullptr && context->rigid;
    if (rigid) {
        constexpr uint8_t png[] = {0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
        const bool isPng = byteCount >= sizeof(png) && std::memcmp(bytes, png, sizeof(png)) == 0;
        const bool isJpeg = byteCount >= 3u && bytes[0] == 0xffu && bytes[1] == 0xd8u && bytes[2] == 0xffu;
        if (!isPng && !isJpeg) return fail(error, "only PNG/JPEG image payloads supported");
    }
    if (stbi_info_from_memory(bytes, static_cast<int>(byteCount), &width, &height, &channels) == 0
        || width <= 0 || height <= 0) return fail(error, "invalid image header/dimensions");
    const size_t dimensionLimit = rigid ? 2048u : 65535u;
    if (static_cast<size_t>(width) > dimensionLimit || static_cast<size_t>(height) > dimensionLimit)
        return fail(error, "decoded image dimension limit");
    const uint64_t decodedBytes = uint64_t{static_cast<uint32_t>(width)} * static_cast<uint32_t>(height) * 4u;
    const size_t byteLimit = rigid ? kRigidImageBytes : kMaxInputBytes;
    if (decodedBytes > byteLimit || decodedBytes > std::numeric_limits<size_t>::max()
        || (context != nullptr && context->decodedImageBytes > byteLimit - static_cast<size_t>(decodedBytes)))
        return fail(error, "decoded image byte limit");
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes, static_cast<int>(byteCount), &width, &height, &channels, 4),
        stbi_image_free);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        if (reason == nullptr) return fail(error, "failed to decode image");
        return fail(error, std::string("failed to decode image: ") + reason);
    }
    if (width <= 0 || height <= 0) {
        return fail(error, "decoded image has invalid dimensions");
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (int row = 0; row < height; ++row) {
        const size_t srcRow = static_cast<size_t>(rigid ? row : height - 1 - row) * rowBytes;
        const size_t dstRow = static_cast<size_t>(row) * rowBytes;
        std::memcpy(rgba.data() + dstRow, pixels.get() + srcRow, rowBytes);
    }
    out->pixels = std::move(rgba);
    out->width = static_cast<uint32_t>(width);
    out->height = static_cast<uint32_t>(height);
    if (context != nullptr) context->decodedImageBytes += static_cast<size_t>(decodedBytes);
    return true;
}

// TinyGLTF image loader. Strict assets use top-row-first RGBA8; legacy retains
// its historical inverted rows, identified by the chosen import profile.
bool decodeGltfImage(tinygltf::Image* image, int /*imageIndex*/, std::string* err,
                     std::string* /*warn*/, int reqWidth, int reqHeight,
                     const unsigned char* bytes, int size, void* userData) {
    if (bytes == nullptr || size <= 0) {
        if (err != nullptr) *err = "image has no data";
        return false;
    }
    DecodedImage decoded;
    std::string decodeError;
    auto* context = static_cast<ImportContext*>(userData);
    if (context != nullptr && context->rigid && !image->mimeType.empty()) {
        const bool png = size >= 8 && bytes[0] == 0x89u;
        if (image->mimeType != (png ? "image/png" : "image/jpeg")) return fail(err, "image MIME/payload mismatch");
    }
    if (!decodeRgbaBytes(bytes, static_cast<size_t>(size), &decoded, &decodeError, context)) {
        if (err != nullptr) *err = decodeError;
        return false;
    }
    if (reqWidth > 0 && reqHeight > 0 &&
        (decoded.width != static_cast<uint32_t>(reqWidth) ||
         decoded.height != static_cast<uint32_t>(reqHeight))) {
        if (err != nullptr) *err = "image dimensions do not match metadata";
        return false;
    }
    image->image = std::move(decoded.pixels);
    image->width = static_cast<int>(decoded.width);
    image->height = static_cast<int>(decoded.height);
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    return true;
}

bool loadImagePixels(const tinygltf::Model& model, int imageIndex, DecodedImage* out,
                     std::string* error) {
    if (imageIndex < 0 || static_cast<size_t>(imageIndex) >= model.images.size()) {
        return fail(error, "image index out of range");
    }
    const tinygltf::Image& image = model.images[static_cast<size_t>(imageIndex)];
    if (!image.image.empty() && image.width > 0 && image.height > 0 && image.component == 4 &&
        image.bits == 8 && image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const size_t expected = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u;
        if (expected != image.image.size()) return fail(error, "decoded image size mismatch");
        out->pixels = image.image;
        out->width = static_cast<uint32_t>(image.width);
        out->height = static_cast<uint32_t>(image.height);
        return true;
    }
    const uint8_t* raw = nullptr;
    size_t rawSize = 0;
    if (!image.image.empty()) {
        raw = image.image.data();
        rawSize = image.image.size();
    } else if (image.bufferView >= 0) {
        const size_t viewIndex = static_cast<size_t>(image.bufferView);
        if (viewIndex >= model.bufferViews.size()) return fail(error, "image buffer view out of range");
        const tinygltf::BufferView& view = model.bufferViews[viewIndex];
        if (view.buffer < 0 || static_cast<size_t>(view.buffer) >= model.buffers.size()) {
            return fail(error, "image buffer out of range");
        }
        const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(view.buffer)];
        if (view.byteOffset >= buffer.data.size()) return fail(error, "image data out of range");
        if (view.byteLength > buffer.data.size() - view.byteOffset) return fail(error, "image data out of range");
        raw = buffer.data.data() + view.byteOffset;
        rawSize = view.byteLength;
    } else {
        return fail(error, "image has no data");
    }
    return decodeRgbaBytes(raw, rawSize, out, error);
}

bool convertTexture(const tinygltf::Model& model, int textureIndex, int texCoord,
                    moto::VmeshTextureSlot slot, bool isSrgb,
                    moto::VmeshMaterial* material, moto::VmeshData* data,
                    std::string* error, bool rigid) {
    if (textureIndex < 0) return true;
    if (texCoord != 0) return fail(error, "only texture coordinate set 0 is supported");
    if (static_cast<size_t>(textureIndex) >= model.textures.size()) {
        return fail(error, "texture index out of range");
    }
    const tinygltf::Texture& texture = model.textures[static_cast<size_t>(textureIndex)];
    if (texture.source < 0) return fail(error, "texture has no source image");
    DecodedImage decoded;
    if (!loadImagePixels(model, texture.source, &decoded, error)) return false;
    if (decoded.width == 0u || decoded.height == 0u) return fail(error, "texture has zero extent");
    if (decoded.width > 65535u || decoded.height > 65535u) return fail(error, "texture too large");
    if (decoded.pixels.size() > static_cast<size_t>(0xFFFFFFFFu)) return fail(error, "texture too large");
    if (data->images.size() > static_cast<size_t>(0xFFFFFFFFu)) return fail(error, "image section too large");
    const size_t limit = rigid ? kRigidImageBytes : kMaxInputBytes;
    if (decoded.pixels.size() > limit || data->images.size() > limit - decoded.pixels.size())
        return fail(error, "copied image byte limit");
    material->textureOffset[slot] = static_cast<uint32_t>(data->images.size());
    material->textureSize[slot] = static_cast<uint32_t>(decoded.pixels.size());
    material->textureWidth[slot] = static_cast<uint16_t>(decoded.width);
    material->textureHeight[slot] = static_cast<uint16_t>(decoded.height);
    material->hasTexture[slot] = 1u;
    material->textureIsSrgb[slot] = isSrgb ? 1u : 0u;
    data->images.insert(data->images.end(), decoded.pixels.begin(), decoded.pixels.end());
    return true;
}

bool validateRigidMaterials(const tinygltf::Model& model, std::string* error) {
    const auto factor = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    for (const auto& sampler : model.samplers) {
        if (sampler.wrapS != TINYGLTF_TEXTURE_WRAP_REPEAT || sampler.wrapT != TINYGLTF_TEXTURE_WRAP_REPEAT
            || (sampler.magFilter != -1 && sampler.magFilter != TINYGLTF_TEXTURE_FILTER_LINEAR)
            || (sampler.minFilter != -1 && sampler.minFilter != TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR))
            return fail(error, "sampler: only repeat/linear/trilinear supported");
    }
    for (const auto& texture : model.textures) {
        if (texture.sampler < -1 || (texture.sampler >= 0 && static_cast<size_t>(texture.sampler) >= model.samplers.size()))
            return fail(error, "texture sampler out of range");
    }
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& material = model.materials[i];
        const std::string path = "materials[" + std::to_string(i) + "]";
        if (material.alphaMode != "OPAQUE" || material.occlusionTexture.index != -1)
            return fail(error, path + ": only opaque materials without AO texture supported");
        for (double value : material.pbrMetallicRoughness.baseColorFactor) if (!factor(value)) return fail(error, path + ": invalid base color");
        for (double value : material.emissiveFactor) if (!factor(value)) return fail(error, path + ": invalid emissive factor");
        if (!factor(material.pbrMetallicRoughness.metallicFactor) || !factor(material.pbrMetallicRoughness.roughnessFactor)
            || !factor(material.alphaCutoff) || !factor(material.occlusionTexture.strength)
            || !std::isfinite(material.normalTexture.scale) || material.normalTexture.scale < 0.0
            || material.normalTexture.scale > static_cast<double>(std::numeric_limits<float>::max())) return fail(error, path + ": invalid material factor");
    }
    return true;
}

bool buildMaterials(const tinygltf::Model& model, moto::VmeshData* data, std::string* error, bool rigid) {
    const size_t sourceCount = model.materials.size();
    const bool needsDefault = sourceCount == 0u;
    bool appendDefault = false;
    if (rigid && !needsDefault) for (const auto& mesh : model.meshes)
        for (const auto& primitive : mesh.primitives) if (primitive.material == -1) appendDefault = true;
    data->materials.resize(needsDefault ? 1u : sourceCount + (appendDefault ? 1u : 0u));
    if (appendDefault) data->materials.back().metallicFactor = 1.0f;
    data->header.materialCount = static_cast<uint32_t>(data->materials.size());

    if (needsDefault) {
        data->materials[0].metallicFactor = 1.0f;
        return true;
    }

    for (size_t i = 0; i < sourceCount; ++i) {
        const tinygltf::Material& source = model.materials[i];
        moto::VmeshMaterial& material = data->materials[i];
        material.nameOffset = storeName(&data->stringBlob, source.name);

        if (!source.pbrMetallicRoughness.baseColorFactor.empty()) {
            if (source.pbrMetallicRoughness.baseColorFactor.size() != 4u) {
                return fail(error, "baseColorFactor must have 4 components");
            }
            for (int c = 0; c < 4; ++c) {
                material.baseColorFactor[c] =
                    static_cast<float>(source.pbrMetallicRoughness.baseColorFactor[static_cast<size_t>(c)]);
            }
        }
        if (!source.emissiveFactor.empty()) {
            if (source.emissiveFactor.size() != 3u) return fail(error, "emissiveFactor must have 3 components");
            for (int c = 0; c < 3; ++c) {
                material.emissiveFactor[c] = static_cast<float>(source.emissiveFactor[static_cast<size_t>(c)]);
            }
        }
        material.metallicFactor = static_cast<float>(source.pbrMetallicRoughness.metallicFactor);
        material.roughnessFactor = static_cast<float>(source.pbrMetallicRoughness.roughnessFactor);
        material.normalScale = static_cast<float>(source.normalTexture.scale);
        material.occlusionStrength = static_cast<float>(source.occlusionTexture.strength);
        material.alphaCutoff = static_cast<float>(source.alphaCutoff);

        if (source.alphaMode == "OPAQUE") {
            material.alphaMode = moto::VmeshAlphaOpaque;
        } else if (source.alphaMode == "MASK") {
            material.alphaMode = moto::VmeshAlphaMask;
        } else if (source.alphaMode == "BLEND") {
            material.alphaMode = moto::VmeshAlphaBlend;
        } else {
            return fail(error, "invalid alpha mode");
        }
        material.doubleSided = source.doubleSided ? 1u : 0u;
        material.unlit =
            source.extensions.find("KHR_materials_unlit") != source.extensions.end() ? 1u : 0u;

        if (!convertTexture(model, source.pbrMetallicRoughness.baseColorTexture.index,
                            source.pbrMetallicRoughness.baseColorTexture.texCoord,
                            moto::VmeshTextureBaseColor, true, &material, data, error, rigid)) {
            return false;
        }
        if (!convertTexture(model, source.normalTexture.index, source.normalTexture.texCoord,
                            moto::VmeshTextureNormal, false, &material, data, error, rigid)) {
            return false;
        }
        if (!convertTexture(model, source.pbrMetallicRoughness.metallicRoughnessTexture.index,
                            source.pbrMetallicRoughness.metallicRoughnessTexture.texCoord,
                            moto::VmeshTextureMetallicRoughness, false, &material, data, error, rigid)) {
            return false;
        }
        if (!convertTexture(model, source.emissiveTexture.index, source.emissiveTexture.texCoord,
                            moto::VmeshTextureEmissive, true, &material, data, error, rigid)) {
            return false;
        }
    }
    return true;
}

struct PrimitiveResult {
    std::vector<moto::VmeshVertex> vertices;
    std::vector<uint32_t> indices;
    bool hasTangent = false;
    bool hasSkin = false;
};

bool convertPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive,
                      PrimitiveResult* out, std::string* error, bool rigid) {
    out->vertices.clear();
    out->indices.clear();
    out->hasTangent = false;
    out->hasSkin = false;

    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        return fail(error, "only triangle primitives are supported");
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) return fail(error, "primitive has no POSITION attribute");
    AccessorView position;
    if (!resolveAccessor(model, positionIt->second, &position, error)) return false;
    if (position.componentCount != 3 || position.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        return fail(error, "POSITION must be a VEC3 of floats");
    }
    // Accessor byte bounds alone do not permit indexing another attribute with
    // POSITION.count. Prove every relation before any component read/allocation.
    for (const auto& [name, accessor] : primitive.attributes) {
        AccessorView attribute;
        if (!resolveAccessor(model, accessor, &attribute, error, rigid)) return false;
        if (attribute.count != position.count) return fail(error, "attributes." + name + ": count "
            + std::to_string(attribute.count) + ", expected " + std::to_string(position.count));
        if (rigid && (attribute.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || attribute.normalized
            || attribute.stride % 4u != 0u)) return fail(error, "attributes." + name + ": expected aligned unnormalized FLOAT");
    }
    if (rigid && position.count > kRigidVertices) return fail(error, "POSITION: rigid vertex limit");

    AccessorView normal;
    AccessorView tangent;
    AccessorView texCoord;
    AccessorView joints;
    AccessorView weights;
    bool hasNormal = false;
    bool hasTangent = false;
    bool hasTexCoord = false;
    bool hasJoints = false;
    bool hasWeights = false;

    const auto attributeIt = primitive.attributes.find("NORMAL");
    if (attributeIt != primitive.attributes.end()) {
        if (!resolveAccessor(model, attributeIt->second, &normal, error)) return false;
        if (normal.componentCount != 3) return fail(error, "NORMAL must be VEC3");
        hasNormal = true;
    }
    const auto tangentIt = primitive.attributes.find("TANGENT");
    if (tangentIt != primitive.attributes.end()) {
        if (!resolveAccessor(model, tangentIt->second, &tangent, error)) return false;
        if (tangent.componentCount != 4) return fail(error, "TANGENT must be VEC4");
        hasTangent = true;
    }
    const auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
    if (texCoordIt != primitive.attributes.end()) {
        if (!resolveAccessor(model, texCoordIt->second, &texCoord, error)) return false;
        if (texCoord.componentCount != 2) return fail(error, "TEXCOORD_0 must be VEC2");
        hasTexCoord = true;
    }
    const auto jointsIt = primitive.attributes.find("JOINTS_0");
    if (jointsIt != primitive.attributes.end()) {
        if (!resolveAccessor(model, jointsIt->second, &joints, error)) return false;
        if (joints.componentCount != 4) return fail(error, "JOINTS_0 must be VEC4");
        if (joints.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            joints.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            joints.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            return fail(error, "JOINTS_0 must be an unsigned integer type");
        }
        hasJoints = true;
    }
    const auto weightsIt = primitive.attributes.find("WEIGHTS_0");
    if (weightsIt != primitive.attributes.end()) {
        if (!resolveAccessor(model, weightsIt->second, &weights, error)) return false;
        if (weights.componentCount != 4) return fail(error, "WEIGHTS_0 must be VEC4");
        hasWeights = true;
    }

    const size_t vertexCount = position.count;
    if (rigid) {
        if (!hasNormal) return fail(error, "NORMAL required by rigid profile");
        if (primitive.material >= 0) {
            if (static_cast<size_t>(primitive.material) >= model.materials.size()) return fail(error, "primitive material out of range");
            const auto& material = model.materials[static_cast<size_t>(primitive.material)];
            if ((material.pbrMetallicRoughness.baseColorTexture.index >= 0 || material.normalTexture.index >= 0
                || material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0 || material.emissiveTexture.index >= 0)
                && !hasTexCoord) return fail(error, "textured primitive requires TEXCOORD_0");
            if (material.normalTexture.index >= 0 && !hasTangent) return fail(error, "normal map requires authored TANGENT");
        }
    }

    // Local index stream (indexed or synthesized 0..count-1).
    AccessorView indexView;
    bool hasIndices = primitive.indices >= 0;
    if (hasIndices) {
        if (!resolveAccessor(model, primitive.indices, &indexView, error, rigid)) return false;
        if (indexView.componentCount != 1) return fail(error, "index accessor must be SCALAR");
        if (indexView.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            indexView.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            indexView.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            return fail(error, "index accessor must be an unsigned integer type");
        }
        if (rigid && indexView.normalized) return fail(error, "index accessor cannot be normalized");
    }
    const size_t indexCount = hasIndices ? indexView.count : vertexCount;
    if (rigid && indexCount > kRigidIndices) return fail(error, "rigid index limit");
    if (indexCount % 3u != 0u) return fail(error, "triangle primitive has a non-triangle index count");

    std::vector<uint32_t> localIndices(indexCount);
    for (size_t i = 0; i < indexCount; ++i) {
        uint32_t index = 0u;
        if (hasIndices) {
            const uint8_t* p = indexView.bytes + i * indexView.stride;
            if (indexView.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                index = p[0];
            } else if (indexView.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                uint16_t value = 0;
                std::memcpy(&value, p, sizeof(value));
                index = value;
            } else {
                uint32_t value = 0;
                std::memcpy(&value, p, sizeof(value));
                index = value;
            }
        } else {
            index = static_cast<uint32_t>(i);
        }
        if (index >= vertexCount) return fail(error, "index out of range");
        if (rigid && hasIndices && (index == 0xffffffffu
            || (indexView.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && index == 65535u)
            || (indexView.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && index == 255u))) return fail(error, "primitive restart index unsupported");
        localIndices[i] = index;
    }

    std::vector<Vec3> positions(vertexCount);
    std::vector<Vec3> normals(vertexCount);
    std::vector<Vec2> uvs(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
        const Vec3 p{readFloatComponent(position, v, 0), readFloatComponent(position, v, 1),
                     readFloatComponent(position, v, 2)};
        if (!finiteVec3(p)) return fail(error, "non-finite POSITION");
        positions[v] = p;
        if (hasNormal) {
            const Vec3 n{readFloatComponent(normal, v, 0), readFloatComponent(normal, v, 1),
                         readFloatComponent(normal, v, 2)};
            if (!finiteVec3(n)) return fail(error, "non-finite NORMAL");
            if (rigid && std::abs(dot(n, n) - 1.0f) > 1.0e-3f) return fail(error, "NORMAL must be unit length");
            normals[v] = n;
        } else {
            normals[v] = {0.0f, 1.0f, 0.0f};
        }
        if (hasTexCoord) {
            const Vec2 t{readFloatComponent(texCoord, v, 0), readFloatComponent(texCoord, v, 1)};
            if (!std::isfinite(t.u) || !std::isfinite(t.v)) return fail(error, "non-finite TEXCOORD_0");
            uvs[v] = t;
        }
    }

    // Per-vertex skin data.
    std::vector<uint16_t> jointData;
    std::vector<float> weightData;
    if (hasJoints || hasWeights) {
        jointData.assign(vertexCount * 4u, 0u);
        weightData.assign(vertexCount * 4u, 0.0f);
        for (size_t v = 0; v < vertexCount; ++v) {
            for (int c = 0; c < 4; ++c) {
                if (hasJoints) {
                    const uint32_t joint = readRawUInt(joints, v, c);
                    if (joint > 65535u) return fail(error, "joint index out of range");
                    jointData[v * 4u + static_cast<size_t>(c)] = static_cast<uint16_t>(joint);
                }
                if (hasWeights) {
                    const float weight = readFloatComponent(weights, v, c);
                    if (!std::isfinite(weight)) return fail(error, "non-finite WEIGHTS_0");
                    weightData[v * 4u + static_cast<size_t>(c)] = weight;
                }
            }
        }
    }

    // Tangent synthesis when authored tangents are absent but the primitive has
    // both normals and UVs (Gram-Schmidt with the classic bitangent sign).
    std::vector<float> tangentData;
    bool computedTangents = false;
    if (hasTangent) {
        tangentData.resize(vertexCount * 4u);
        for (size_t v = 0; v < vertexCount; ++v) {
            for (int c = 0; c < 4; ++c) {
                const float value = readFloatComponent(tangent, v, c);
                if (!std::isfinite(value)) return fail(error, "non-finite TANGENT");
                tangentData[v * 4u + static_cast<size_t>(c)] = value;
            }
            if (rigid) {
                const Vec3 t{tangentData[v * 4u], tangentData[v * 4u + 1u], tangentData[v * 4u + 2u]};
                if (std::abs(dot(t, t) - 1.0f) > 1.0e-3f || std::abs(dot(t, normals[v])) > 1.0e-3f
                    || std::abs(tangentData[v * 4u + 3u]) != 1.0f) return fail(error, "invalid authored tangent frame");
            }
        }
    } else if (hasNormal && hasTexCoord) {
        std::vector<Vec3> tan1(vertexCount);
        std::vector<Vec3> tan2(vertexCount);
        for (size_t t = 0; t + 2u < indexCount; t += 3u) {
            const uint32_t i0 = localIndices[t];
            const uint32_t i1 = localIndices[t + 1u];
            const uint32_t i2 = localIndices[t + 2u];
            const Vec3 e1 = positions[i1] - positions[i0];
            const Vec3 e2 = positions[i2] - positions[i0];
            const Vec2 uv1{uvs[i1].u - uvs[i0].u, uvs[i1].v - uvs[i0].v};
            const Vec2 uv2{uvs[i2].u - uvs[i0].u, uvs[i2].v - uvs[i0].v};
            const float denominator = uv1.u * uv2.v - uv1.v * uv2.u;
            if (denominator == 0.0f) continue;
            const float r = 1.0f / denominator;
            const Vec3 sdir = (e1 * uv2.v - e2 * uv1.v) * r;
            const Vec3 tdir = (e2 * uv1.u - e1 * uv2.u) * r;
            if (!finiteVec3(sdir) || !finiteVec3(tdir)) return fail(error, "non-finite generated TANGENT direction");
            tan1[i0] = tan1[i0] + sdir;
            tan1[i1] = tan1[i1] + sdir;
            tan1[i2] = tan1[i2] + sdir;
            tan2[i0] = tan2[i0] + tdir;
            tan2[i1] = tan2[i1] + tdir;
            tan2[i2] = tan2[i2] + tdir;
        }
        tangentData.resize(vertexCount * 4u);
        for (size_t v = 0; v < vertexCount; ++v) {
            const Vec3 n = normals[v];
            Vec3 t = tan1[v];
            const float nd = dot(n, t);
            t = {t.x - n.x * nd, t.y - n.y * nd, t.z - n.z * nd};
            const float length = std::sqrt(dot(t, t));
            if (!std::isfinite(length)) return fail(error, "non-finite generated TANGENT length");
            if (length > 1e-20f) {
                t = t * (1.0f / length);
                const float w = dot(cross(n, t), tan2[v]) < 0.0f ? -1.0f : 1.0f;
                tangentData[v * 4u + 0u] = t.x;
                tangentData[v * 4u + 1u] = t.y;
                tangentData[v * 4u + 2u] = t.z;
                tangentData[v * 4u + 3u] = w;
            } else {
                tangentData[v * 4u + 0u] = 1.0f;
                tangentData[v * 4u + 1u] = 0.0f;
                tangentData[v * 4u + 2u] = 0.0f;
                tangentData[v * 4u + 3u] = 1.0f;
            }
        }
        computedTangents = true;
    }

    // Finite inputs can still overflow tangent synthesis (e.g. tiny UV spans).
    // Never publish NaN/Inf produced by reciprocal/Gram-Schmidt arithmetic.
    for (float value : tangentData) if (!std::isfinite(value)) return fail(error, "non-finite generated TANGENT");
    if (computedTangents) for (size_t v = 0; v < vertexCount; ++v) {
        const Vec3 t{tangentData[v * 4u], tangentData[v * 4u + 1u], tangentData[v * 4u + 2u]};
        if (dot(t, t) <= std::numeric_limits<float>::min()) return fail(error, "zero generated TANGENT");
    }
    out->vertices.resize(vertexCount);
    for (size_t v = 0; v < vertexCount; ++v) {
        moto::VmeshVertex& vertex = out->vertices[v];
        vertex.position[0] = positions[v].x;
        vertex.position[1] = positions[v].y;
        vertex.position[2] = positions[v].z;
        vertex.normal[0] = normals[v].x;
        vertex.normal[1] = normals[v].y;
        vertex.normal[2] = normals[v].z;
        if (hasTangent || computedTangents) {
            for (int c = 0; c < 4; ++c) vertex.tangent[c] = tangentData[v * 4u + static_cast<size_t>(c)];
        } else {
            vertex.tangent[0] = 1.0f;
            vertex.tangent[1] = 0.0f;
            vertex.tangent[2] = 0.0f;
            vertex.tangent[3] = 1.0f;
        }
        vertex.texCoord[0] = uvs[v].u;
        vertex.texCoord[1] = uvs[v].v;
        if (hasJoints || hasWeights) {
            for (int c = 0; c < 4; ++c) {
                vertex.joint[c] = jointData[v * 4u + static_cast<size_t>(c)];
                vertex.weight[c] = weightData[v * 4u + static_cast<size_t>(c)];
            }
        }
    }
    out->indices = std::move(localIndices);
    out->hasTangent = hasTangent || computedTangents;
    out->hasSkin = hasJoints || hasWeights;
    return true;
}

bool buildMeshes(const tinygltf::Model& model, moto::VmeshData* data, std::string* error, bool rigid) {
    const size_t meshCount = model.meshes.size();
    data->header.meshCount = static_cast<uint32_t>(meshCount);
    const uint32_t materialCount = data->header.materialCount;

    size_t totalVertices = 0;
    size_t totalIndices = 0;
    std::vector<moto::VmeshVertex> vertexPool;
    std::vector<uint32_t> logicalIndices;
    struct SubmeshRange {
        uint32_t indexStart;
        uint32_t indexCount;
        uint32_t materialIndex;
        uint32_t meshIndex;
    };
    std::vector<SubmeshRange> ranges;

    for (size_t m = 0; m < meshCount; ++m) {
        const tinygltf::Mesh& mesh = model.meshes[m];
        for (const tinygltf::Primitive& primitive : mesh.primitives) {
            PrimitiveResult result;
            if (!convertPrimitive(model, primitive, &result, error, rigid)) return false;
            const uint32_t materialIndex = primitive.material >= 0 ? static_cast<uint32_t>(primitive.material)
                : (rigid ? materialCount - 1u : 0u);
            if (materialIndex >= materialCount) return fail(error, "primitive material out of range");
            const size_t vertexCount = result.vertices.size();
            const size_t indexCount = result.indices.size();
            if (totalVertices > kMaxTotalVertices - vertexCount) return fail(error, "too many vertices");
            if (totalIndices > kMaxTotalIndices - indexCount) return fail(error, "too many indices");
            if (rigid && (vertexCount > kRigidVertices - totalVertices || indexCount > kRigidIndices - totalIndices))
                return fail(error, "rigid aggregate geometry limit");
            const uint32_t base = static_cast<uint32_t>(totalVertices);
            vertexPool.insert(vertexPool.end(), result.vertices.begin(), result.vertices.end());
            for (const uint32_t localIndex : result.indices) {
                logicalIndices.push_back(base + localIndex);
            }
            ranges.push_back({static_cast<uint32_t>(totalIndices), static_cast<uint32_t>(indexCount),
                              materialIndex, static_cast<uint32_t>(m)});
            totalVertices += vertexCount;
            totalIndices += indexCount;
            if (result.hasTangent) data->header.flags |= moto::kVmeshHasTangent;
            if (result.hasSkin) data->header.flags |= moto::kVmeshHasSkin;
        }
    }

    const size_t indexStride = totalVertices <= 65535u ? 2u : 4u;
    data->header.indexStride = static_cast<uint32_t>(indexStride);

    data->vertices.reserve(totalVertices * sizeof(moto::VmeshVertex));
    for (const moto::VmeshVertex& vertex : vertexPool) {
        appendVertex(&data->vertices, vertex);
    }
    data->header.vertexCount = static_cast<uint32_t>(totalVertices);

    data->indices.reserve(totalIndices * indexStride);
    data->submeshes.resize(ranges.size());
    for (size_t i = 0; i < ranges.size(); ++i) {
        const SubmeshRange& range = ranges[i];
        moto::VmeshSubmesh& submesh = data->submeshes[i];
        submesh.indexOffset = static_cast<uint32_t>(data->indices.size());
        submesh.indexCount = range.indexCount;
        submesh.materialIndex = range.materialIndex;
        submesh.meshIndex = range.meshIndex;
        for (uint32_t k = 0; k < range.indexCount; ++k) {
            const uint32_t index = logicalIndices[static_cast<size_t>(range.indexStart) + k];
            if (indexStride == 2u) {
                appendU16LE(&data->indices, static_cast<uint16_t>(index));
            } else {
                appendU32LE(&data->indices, index);
            }
        }
    }
    data->header.indexCount = static_cast<uint32_t>(totalIndices);
    data->header.submeshCount = static_cast<uint32_t>(data->submeshes.size());
    return true;
}

bool buildSkins(const tinygltf::Model& model, moto::VmeshData* data, std::string* error) {
    const size_t skinCount = model.skins.size();
    data->skins.resize(skinCount);
    data->header.skinCount = static_cast<uint32_t>(skinCount);
    if (skinCount != 0u) data->header.flags |= moto::kVmeshHasSkin;

    for (size_t i = 0; i < skinCount; ++i) {
        const tinygltf::Skin& source = model.skins[i];
        moto::VmeshSkin& skin = data->skins[i];
        skin.skeletonRoot = source.skeleton >= 0 ? static_cast<int32_t>(source.skeleton) : -1;

        const size_t jointStart = data->joints.size();
        skin.jointCount = static_cast<uint32_t>(source.joints.size());
        const size_t jointByteOffset = jointStart * sizeof(moto::VmeshJoint);
        skin.jointsOffset = jointByteOffset;

        for (const int joint : source.joints) {
            if (joint < 0 || static_cast<size_t>(joint) >= model.nodes.size()) {
                return fail(error, "skin joint node out of range");
            }
            moto::VmeshJoint vmeshJoint{};
            vmeshJoint.nodeIndex = static_cast<uint32_t>(joint);
            data->joints.push_back(vmeshJoint);
        }

        if (source.inverseBindMatrices >= 0) {
            AccessorView ibm;
            if (!resolveAccessor(model, source.inverseBindMatrices, &ibm, error)) return false;
            if (ibm.componentCount != 16 || ibm.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
                return fail(error, "inverse bind matrices must be MAT4 FLOAT");
            }
            if (ibm.count < source.joints.size()) return fail(error, "inverse bind matrix count mismatch");
            for (size_t j = 0; j < source.joints.size(); ++j) {
                float matrix[16];
                for (int c = 0; c < 16; ++c) {
                    const float value = readFloatComponent(ibm, j, c);
                    if (!std::isfinite(value)) return fail(error, "non-finite inverse bind matrix");
                    matrix[c] = value;
                }
                std::memcpy(data->joints[jointStart + j].inverseBindMatrix, matrix, sizeof(matrix));
            }
        }
    }
    data->header.jointCount = static_cast<uint32_t>(data->joints.size());
    return true;
}

bool buildNodes(const tinygltf::Model& model, moto::VmeshData* data, std::string* error, bool rigid) {
    const size_t nodeCount = model.nodes.size();
    data->nodes.resize(nodeCount);
    data->header.nodeCount = static_cast<uint32_t>(nodeCount);

    std::vector<int32_t> parent(nodeCount, -1);
    for (const tinygltf::Scene& scene : model.scenes) {
        std::vector<int> stack;
        for (const int root : scene.nodes) {
            if (root < 0 || static_cast<size_t>(root) >= nodeCount) return fail(error, "scene node out of range");
            stack.push_back(root);
        }
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            if (current < 0 || static_cast<size_t>(current) >= nodeCount) return fail(error, "node index out of range");
            for (const int child : model.nodes[static_cast<size_t>(current)].children) {
                if (child < 0 || static_cast<size_t>(child) >= nodeCount) return fail(error, "node child out of range");
                if (parent[static_cast<size_t>(child)] != -1) return fail(error, "node has multiple parents");
                parent[static_cast<size_t>(child)] = static_cast<int32_t>(current);
                stack.push_back(child);
            }
        }
    }

    for (size_t i = 0; i < nodeCount; ++i) {
        const tinygltf::Node& source = model.nodes[i];
        moto::VmeshNode& node = data->nodes[i];
        node.parent = parent[i];
        node.nameOffset = storeName(&data->stringBlob, source.name);
        node.skinIndex = source.skin >= 0 ? static_cast<int32_t>(source.skin) : -1;
        if (source.skin >= 0 && static_cast<size_t>(source.skin) >= data->skins.size()) {
            return fail(error, "node skin out of range");
        }
        if (source.mesh >= 0) {
            if (static_cast<size_t>(source.mesh) >= model.meshes.size()) return fail(error, "node mesh out of range");
            node.meshIndex = static_cast<uint32_t>(source.mesh);
        }

        if (!source.matrix.empty()) {
            if (source.matrix.size() != 16u) return fail(error, "node matrix must have 16 elements");
            Mat4 matrix;
            for (int c = 0; c < 16; ++c) {
                matrix.m[c] = static_cast<float>(source.matrix[static_cast<size_t>(c)]);
            }
            if (!decomposeMatrix(matrix, node.translation, node.rotation, node.scale, error)) return false;
        } else {
            if ((!source.translation.empty() && source.translation.size() != 3u)
                || (!source.rotation.empty() && source.rotation.size() != 4u)
                || (!source.scale.empty() && source.scale.size() != 3u)) {
                return fail(error, "node TRS component has wrong size");
            }
            if (source.translation.size() == 3u) {
                for (int c = 0; c < 3; ++c) {
                    node.translation[c] = static_cast<float>(source.translation[static_cast<size_t>(c)]);
                }
            }
            if (source.rotation.size() == 4u) {
                double lengthSquared = 0.0;
                for (int c = 0; c < 4; ++c) {
                    const float value = static_cast<float>(source.rotation[static_cast<size_t>(c)]);
                    node.rotation[c] = value;
                    lengthSquared += static_cast<double>(value) * static_cast<double>(value);
                }
                if (rigid && (!std::isfinite(lengthSquared) || std::abs(lengthSquared - 1.0) > 1.0e-3))
                    return fail(error, "rigid node rotation must be unit quaternion");
                if (lengthSquared > 0.0) {
                    const float invLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
                    for (int c = 0; c < 4; ++c) node.rotation[c] *= invLength;
                }
            }
            if (source.scale.size() == 3u) {
                for (int c = 0; c < 3; ++c) {
                    node.scale[c] = static_cast<float>(source.scale[static_cast<size_t>(c)]);
                }
            }
        }
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(node.translation[c]) || !std::isfinite(node.scale[c])) {
                return fail(error, "non-finite node transform");
            }
        }
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(node.rotation[c])) return fail(error, "non-finite node rotation");
        }
        if (rigid) {
            for (float value : node.scale) if (value < 1.0e-30f) return fail(error, "rigid node scale must be positive/invertible");
            double squared = 0;
            for (float value : node.rotation) squared += static_cast<double>(value) * static_cast<double>(value);
            if (!std::isfinite(squared) || squared < 1.0e-12) return fail(error, "invalid rigid node quaternion");
            const float inverse = static_cast<float>(1.0 / std::sqrt(squared));
            for (float& value : node.rotation) value *= inverse;
            bool negate = false;
            for (int index : {3, 0, 1, 2}) if (node.rotation[index] != 0.0f) { negate = node.rotation[index] < 0.0f; break; }
            for (float& value : node.rotation) { if (negate) value = -value; if (value == 0.0f) value = 0.0f; }
            if (!source.matrix.empty()) {
                const double x = node.rotation[0], y = node.rotation[1], z = node.rotation[2], w = node.rotation[3];
                const double rotation[9] = {1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w),
                    2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w),
                    2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y)};
                for (size_t col = 0; col < 3u; ++col) for (size_t row = 0; row < 3u; ++row) {
                    const double reconstructed = rotation[col * 3u + row] * static_cast<double>(node.scale[col]);
                    if (std::abs(reconstructed - source.matrix[col * 4u + row]) > 1.0e-5 * std::max(1.0, static_cast<double>(node.scale[col])))
                        return fail(error, "node matrix TRS reconstruction mismatch");
                }
            }
        }
    }
    return true;
}

bool validateSkins(const moto::VmeshData& data, std::string* error) {
    const size_t nodeCount = data.nodes.size();
    for (const moto::VmeshSkin& skin : data.skins) {
        if (skin.skeletonRoot >= static_cast<int32_t>(nodeCount)) {
            return fail(error, "skin skeleton node out of range");
        }
        for (uint32_t j = 0; j < skin.jointCount; ++j) {
            const uint64_t index = skin.jointsOffset / sizeof(moto::VmeshJoint) + j;
            if (index >= data.joints.size()) return fail(error, "skin joint out of range");
            if (data.joints[static_cast<size_t>(index)].nodeIndex >= nodeCount) {
                return fail(error, "skin joint node out of range");
            }
        }
    }
    return true;
}

bool buildAnimations(const tinygltf::Model& model, moto::VmeshData* data, std::string* error) {
    const size_t animationCount = model.animations.size();
    for (size_t a = 0; a < animationCount; ++a) {
        const tinygltf::Animation& source = model.animations[a];
        if (source.channels.size() > 0xFFFFFFFFu) return fail(error, "too many animation channels");

        const size_t animIndex = data->anims.size();
        moto::VmeshAnim anim;
        anim.channelCount = static_cast<uint32_t>(source.channels.size());
        anim.channelsOffset = data->animChannels.size() * sizeof(moto::VmeshAnimChannel);
        anim.nameOffset = storeName(&data->stringBlob, source.name);
        data->anims.push_back(anim);

        for (const tinygltf::AnimationChannel& channel : source.channels) {
            if (channel.sampler < 0 || static_cast<size_t>(channel.sampler) >= source.samplers.size()) {
                return fail(error, "animation sampler out of range");
            }
            const tinygltf::AnimationSampler& sampler = source.samplers[static_cast<size_t>(channel.sampler)];
            if (sampler.interpolation != "LINEAR" && sampler.interpolation != "STEP") {
                return fail(error, "unsupported animation interpolation");
            }

            AccessorView input;
            if (!resolveAccessor(model, sampler.input, &input, error)) return false;
            if (input.componentCount != 1 || input.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
                return fail(error, "animation time accessor must be SCALAR FLOAT");
            }
            AccessorView output;
            if (!resolveAccessor(model, sampler.output, &output, error)) return false;
            if (output.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) return fail(error, "animation output must be FLOAT");

            uint8_t path = 0u;
            if (channel.target_path == "translation") {
                path = moto::VmeshAnimPathTranslation;
            } else if (channel.target_path == "rotation") {
                path = moto::VmeshAnimPathRotation;
            } else if (channel.target_path == "scale") {
                path = moto::VmeshAnimPathScale;
            } else {
                return fail(error, "unsupported animation target path");
            }
            const int valueComponents = path == moto::VmeshAnimPathRotation ? 4 : 3;
            if (output.componentCount != valueComponents) return fail(error, "animation output accessor size mismatch");
            if (input.count != output.count) return fail(error, "animation input/output count mismatch");
            if (input.count < 2u) return fail(error, "animation needs at least two keys");
            if (input.count > 0xFFFFFFFFu) return fail(error, "too many animation keys");
            if (channel.target_node < 0 || static_cast<size_t>(channel.target_node) >= data->nodes.size()) {
                return fail(error, "animation node out of range");
            }

            float duration = -std::numeric_limits<float>::infinity();
            float previous = 0.0f;
            for (size_t k = 0; k < input.count; ++k) {
                const float time = readFloatComponent(input, k, 0);
                if (!std::isfinite(time)) return fail(error, "non-finite animation time");
                if (k > 0u && time < previous) return fail(error, "animation times not monotonic");
                previous = time;
                duration = time;
            }
            if (duration > anim.duration) anim.duration = duration;

            moto::VmeshAnimChannel channelRecord;
            channelRecord.keysOffset = data->channelData.size();
            channelRecord.nodeIndex = static_cast<uint32_t>(channel.target_node);
            channelRecord.path = path;
            channelRecord.interpolation = sampler.interpolation == "STEP"
                                              ? moto::VmeshAnimInterpolationStep
                                              : moto::VmeshAnimInterpolationLinear;
            channelRecord.keyCount = static_cast<uint32_t>(input.count);
            data->animChannels.push_back(channelRecord);

            for (size_t k = 0; k < input.count; ++k) {
                appendFloatLE(&data->channelData, readFloatComponent(input, k, 0));
            }
            for (size_t k = 0; k < input.count; ++k) {
                for (int c = 0; c < valueComponents; ++c) {
                    const float value = readFloatComponent(output, k, c);
                    if (!std::isfinite(value)) return fail(error, "non-finite animation value");
                    appendFloatLE(&data->channelData, value);
                }
            }
        }
        data->anims[animIndex] = anim;
    }
    data->header.animCount = static_cast<uint32_t>(data->anims.size());
    data->header.animChannelCount = static_cast<uint32_t>(data->animChannels.size());
    if (!data->anims.empty()) data->header.flags |= moto::kVmeshHasAnimations;
    return true;
}

}  // namespace

bool convertGltfBytesToVmesh(const std::vector<uint8_t>& inputBytes, bool isGlb,
                             voxy::moto::VmeshData* out, std::string* error) {
    return convertGltfBytesToVmesh(inputBytes, isGlb, GltfImportProfile::Legacy, out, error);
}

bool convertGltfBytesToVmesh(const std::vector<uint8_t>& inputBytes, bool isGlb,
                             GltfImportProfile profile, voxy::moto::VmeshData* out, std::string* error) {
    if (out == nullptr) return fail(error, "null output");
    if (error != nullptr) error->clear();
    if (profile != GltfImportProfile::Legacy && profile != GltfImportProfile::SalvageRigidV1) return fail(error, "unknown import profile");
    if (inputBytes.empty()) return fail(error, "input is empty");
    if (inputBytes.size() > kMaxInputBytes) return fail(error, "input too large");
    const bool rigid = profile == GltfImportProfile::SalvageRigidV1;
    if (rigid && !preflightRigid(inputBytes, isGlb, error)) return false;

    tinygltf::TinyGLTF loader;
    ImportContext context{rigid, 0u};
    loader.SetImageLoader(decodeGltfImage, &context);
    if (rigid) {
        tinygltf::FsCallbacks callbacks;
        callbacks.FileExists = [](const std::string&, void*) { return false; };
        callbacks.ExpandFilePath = [](const std::string&, void*) { return std::string{}; };
        callbacks.ReadWholeFile = [](std::vector<unsigned char>*, std::string* reason, const std::string&, void*) { return fail(reason, "external file reads disabled"); };
        callbacks.WriteWholeFile = [](std::string* reason, const std::string&, const std::vector<unsigned char>&, void*) { return fail(reason, "external file writes disabled"); };
        callbacks.GetFileSizeInBytes = [](size_t*, std::string* reason, const std::string&, void*) { return fail(reason, "external file reads disabled"); };
        callbacks.user_data = nullptr;
        if (!loader.SetFsCallbacks(std::move(callbacks), error)) return false;
    }

    tinygltf::Model model;
    std::string parseError;
    std::string parseWarning;
    const bool parsed = isGlb ? loader.LoadBinaryFromMemory(&model, &parseError, &parseWarning,
                                                            inputBytes.data(),
                                                            static_cast<uint32_t>(inputBytes.size()),
                                                            "", tinygltf::REQUIRE_VERSION)
                              : loader.LoadASCIIFromString(&model, &parseError, &parseWarning,
                                                           reinterpret_cast<const char*>(inputBytes.data()),
                                                           static_cast<uint32_t>(inputBytes.size()),
                                                           "", tinygltf::REQUIRE_VERSION);
    if (!parsed) {
        if (!parseError.empty()) return fail(error, "glTF parse failed: " + parseError);
        return fail(error, "glTF parse failed");
    }
    if (rigid && !validateRigidMaterials(model, error)) return false;

    moto::VmeshData result;
    result.stringBlob.push_back('\0');
    result.header.flags = 0u;
    result.header.vertexStride = moto::kVmeshVertexStride;

    if (!buildMaterials(model, &result, error, rigid)) return false;
    if (!buildMeshes(model, &result, error, rigid)) return false;
    if (!buildSkins(model, &result, error)) return false;
    if (!buildNodes(model, &result, error, rigid)) return false;
    if (!validateSkins(result, error)) return false;
    if (!buildAnimations(model, &result, error)) return false;

    result.header.vertexCount = static_cast<uint32_t>(result.vertices.size() / moto::kVmeshVertexStride);
    result.header.indexCount = static_cast<uint32_t>(result.indices.size() / result.header.indexStride);
    result.header.submeshCount = static_cast<uint32_t>(result.submeshes.size());
    if (rigid) {
        const uint64_t bytes = result.vertices.size() + result.indices.size() + result.images.size()
            + result.materials.size() * sizeof(moto::VmeshMaterial) + result.nodes.size() * sizeof(moto::VmeshNode)
            + result.submeshes.size() * sizeof(moto::VmeshSubmesh) + result.stringBlob.size() + 4096u;
        if (bytes > kRigidOutputBytes) return fail(error, "rigid output byte limit");
        if (result.header.vertexCount == 0u || result.header.indexCount == 0u) return fail(error, "rigid scene has no geometry");
    }

    *out = std::move(result);
    return true;
}

}  // namespace voxy::tools

#ifndef GLTF_VMESH_TOOL_NO_MAIN

int main(int argc, char** argv) {
    voxy::tools::GltfImportProfile profile = voxy::tools::GltfImportProfile::Legacy;
    int firstPath = 1;
    if (argc == 5 && std::strcmp(argv[1], "--profile") == 0 && std::strcmp(argv[2], "salvage-rigid-v1") == 0) {
        profile = voxy::tools::GltfImportProfile::SalvageRigidV1;
        firstPath = 3;
    } else if (argc != 3) {
        std::fprintf(stderr, "usage: %s [--profile salvage-rigid-v1] <input.gltf|input.glb> <output.vmesh>\n",
                     argc > 0 ? argv[0] : "gltf_vmesh_tool");
        return 2;
    }
    const char* inputPath = argv[firstPath];
    const char* outputPath = argv[firstPath + 1];

    std::FILE* in = std::fopen(inputPath, "rb");
    if (in == nullptr) {
        std::fprintf(stderr, "gltf_vmesh_tool: cannot open '%s'\n", inputPath);
        return 1;
    }
    std::vector<uint8_t> bytes;
    uint8_t chunk[1u << 16u];
    const size_t maximum = profile == voxy::tools::GltfImportProfile::SalvageRigidV1
        ? 64u * 1024u * 1024u : size_t{1} << 30u;
    for (;;) {
        const size_t got = std::fread(chunk, 1u, sizeof(chunk), in);
        if (got > maximum - bytes.size()) {
            std::fclose(in);
            std::fprintf(stderr, "gltf_vmesh_tool: input byte limit exceeded\n");
            return 1;
        }
        bytes.insert(bytes.end(), chunk, chunk + got);
        if (got < sizeof(chunk)) break;
    }
    const bool readFailed = std::ferror(in) != 0;
    std::fclose(in);
    if (readFailed) {
        std::fprintf(stderr, "gltf_vmesh_tool: failed to read '%s'\n", inputPath);
        return 1;
    }

    const bool isGlb = bytes.size() >= 4u && bytes[0] == 'g' && bytes[1] == 'l' &&
                       bytes[2] == 'T' && bytes[3] == 'F';

    voxy::moto::VmeshData mesh;
    std::string error;
    if (!voxy::tools::convertGltfBytesToVmesh(bytes, isGlb, profile, &mesh, &error)) {
        std::fprintf(stderr, "gltf_vmesh_tool: conversion failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<uint8_t> output;
    if (!voxy::moto::writeVmesh(mesh, &output, &error)) {
        std::fprintf(stderr, "gltf_vmesh_tool: serialization failed: %s\n", error.c_str());
        return 1;
    }

    std::FILE* outFile = std::fopen(outputPath, "wb");
    if (outFile == nullptr) {
        std::fprintf(stderr, "gltf_vmesh_tool: cannot open '%s'\n", outputPath);
        return 1;
    }
    const size_t written = std::fwrite(output.data(), 1u, output.size(), outFile);
    std::fclose(outFile);
    if (written != output.size()) {
        std::fprintf(stderr, "gltf_vmesh_tool: failed to write '%s'\n", outputPath);
        return 1;
    }
    std::fprintf(stderr,
                 "gltf_vmesh_tool: wrote %zu bytes (v=%zu i=%zu s=%zu m=%zu n=%zu sk=%zu a=%zu)\n",
                 output.size(), static_cast<size_t>(mesh.header.vertexCount),
                 static_cast<size_t>(mesh.header.indexCount), mesh.submeshes.size(),
                 mesh.materials.size(), mesh.nodes.size(), mesh.skins.size(), mesh.anims.size());
    return 0;
}

#endif

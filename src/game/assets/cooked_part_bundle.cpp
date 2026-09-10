#include "game/assets/cooked_part_bundle.hpp"
#include "core/sha256.hpp"

#include <json.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <set>
#include <stdexcept>

namespace voxy::game::assets {
namespace {
using Json = nlohmann::json;
constexpr size_t kMaximumManifestBytes = 64 * 1024;
constexpr uint64_t kMaximumBundleBytes = 64ull * 1024ull * 1024ull;

[[noreturn]] void reject(std::string reason) { throw std::runtime_error(std::move(reason)); }

void fields(const Json& value, std::initializer_list<std::string_view> keys) {
    if (!value.is_object() || value.size() != keys.size()) reject("manifest object fields");
    for (const auto key : keys) if (!value.contains(std::string(key))) reject("manifest missing field: " + std::string(key));
}

bool validDigest(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::string digestField(const Json& value) {
    if (!value.is_string() || !validDigest(value.get_ref<const std::string&>())) reject("manifest lowercase SHA256");
    return value.get<std::string>();
}

uint64_t count(const Json& value, uint64_t maximum) {
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned()
        && value.get<int64_t>() < 0)) reject("manifest unsigned byte count");
    const auto result = value.get<uint64_t>();
    if (result == 0 || result > maximum) reject("manifest byte count capacity");
    return result;
}

uint64_t lodId(const Json& value) {
    if (!value.is_string()) reject("manifest canonical LOD ID");
    const auto result = construction::u64FromDecimal(value.get_ref<const std::string&>());
    if (!result || *result == 0) reject("manifest canonical LOD ID");
    return *result;
}

void charge(uint64_t amount, uint64_t maximum, uint64_t& total, std::string_view reason) {
    if (total > maximum || amount > maximum - total) reject(std::string(reason));
    total += amount;
}

std::span<const std::byte> byteSpan(const std::vector<uint8_t>& bytes) {
    return std::as_bytes(std::span(bytes));
}

std::string_view textView(const std::vector<uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<uint8_t> snapshot(const CookedPartByteProvider& provider, std::string_view file,
                              size_t maximum, std::optional<uint64_t> exact,
                              std::string_view expectedDigest) {
    std::string reason;
    auto bytes = provider(file, maximum, reason);
    if (!bytes) reject("bundle snapshot " + std::string(file) + ": " + reason);
    if (bytes->empty() || bytes->size() > maximum || (exact && bytes->size() != *exact))
        reject("bundle snapshot size: " + std::string(file));
    if (core::sha256Hex(core::sha256(byteSpan(*bytes))) != expectedDigest)
        reject("bundle SHA256 mismatch: " + std::string(file));
    return std::move(*bytes);
}

Json manifestDocument(std::string_view text) {
    std::vector<std::set<std::string>> keys;
    size_t values = 0;
    return Json::parse(text, [&keys, &values](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 8 || ++values > 2048) reject("manifest JSON complexity capacity");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key) {
            const auto& key = value.get_ref<const std::string&>();
            if (key.size() > 64 || keys.empty() || !keys.back().insert(key).second) reject("manifest duplicate/oversized key");
        } else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}

struct ManifestLod {
    uint64_t id = 0;
    uint64_t bytes = 0;
    uint64_t sourceBytes = 0;
    std::string file;
    std::string digest;
    std::string sourceDigest;
    const PartLodAdmissionRule* rule = nullptr;
};

void preflight(const std::vector<uint8_t>& bytes, const RigidPrefabLimits& limits) {
    if (bytes.size() < sizeof(moto::VmeshHeader)) reject("bundle VMESH header size");
    moto::VmeshHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.vertexCount > limits.maximumVertices || header.indexCount > limits.maximumIndices
        || header.nodeCount > limits.maximumNodes || header.meshCount > limits.maximumMeshes
        || header.submeshCount > limits.maximumSubmeshes || header.materialCount > limits.maximumMaterials
        || header.skinCount || header.jointCount || header.animCount || header.animChannelCount)
        reject("bundle rigid header capacity/profile");
    // The reader proves every decoded vector against ordered file sections.
    // Bound the entire snapshot first, before any vector is allocated.
    if (bytes.size() > limits.maximumDecodedBytes) reject("bundle per-LOD decode input capacity");
}
} // namespace

std::unique_ptr<const CookedPartBundle> admitCookedPartBundle(
    const CookedPartSelection& selection, const CookedPartByteProvider& provider, std::string& error) {
    error.clear();
    try {
        if (!provider || !construction::isValid(selection.part.id) || selection.part.version == 0
            || !validDigest(selection.manifestSha256) || selection.lodRules.empty()
            || selection.lodRules.size() > construction::kMaximumVisualLods)
            reject("bundle installed selection identity/LOD rules");
        for (auto bound : {selection.maximumInputBytes, selection.maximumDecodedBytes, selection.maximumGpuBytes})
            if (bound == 0 || bound > kMaximumBundleBytes) reject("bundle configured capacity");
        std::set<uint64_t> ruleIds;
        for (const auto& rule : selection.lodRules)
            if (rule.id == 0 || !ruleIds.insert(rule.id).second) reject("bundle duplicate/zero LOD rule");

        auto candidate = std::unique_ptr<CookedPartBundle>(new CookedPartBundle);
        const auto manifestBytes = snapshot(provider, "cook-manifest.json",
            static_cast<size_t>(std::min<uint64_t>(kMaximumManifestBytes, selection.maximumInputBytes)),
            std::nullopt, selection.manifestSha256);
        candidate->manifestJson_ = textView(manifestBytes);
        const auto document = manifestDocument(candidate->manifestJson_);
        fields(document, {"schema", "scope", "input_sidecar_sha256", "normalized_sidecar", "cooker", "converter", "validator", "lods"});
        if (count(document["schema"], 1) != 1
            || document["scope"] != "offline gameplay sidecar and unchanged exported-glTF-frame VMESH")
            reject("bundle manifest schema/scope");
        (void)digestField(document["input_sidecar_sha256"]);
        const auto& cooker = document["cooker"];
        fields(cooker, {"sha256", "source"});
        (void)digestField(cooker["sha256"]);
        if (cooker["source"] != "cook_gameplay_asset.py") reject("bundle cooker identity");
        const auto& converter = document["converter"];
        fields(converter, {"sha256", "profile", "interface"});
        (void)digestField(converter["sha256"]);
        if (converter["profile"] != "salvage-rigid-v1"
            || converter["interface"] != "--profile salvage-rigid-v1 input.glb output.vmesh")
            reject("bundle converter profile/interface");
        const auto& validator = document["validator"];
        fields(validator, {"sha256", "interface"});
        (void)digestField(validator["sha256"]);
        if (validator["interface"] != "sidecar.json cooked-directory") reject("bundle validator interface");
        const auto& metadata = document["normalized_sidecar"];
        fields(metadata, {"file", "sha256", "bytes"});
        if (metadata["file"] != "gameplay.json") reject("bundle normalized sidecar filename");
        const auto metadataSize = count(metadata["bytes"], kMaximumSidecarBytes);
        const auto metadataDigest = digestField(metadata["sha256"]);
        const auto& lods = document["lods"];
        if (!lods.is_array() || lods.size() != ruleIds.size()) reject("bundle manifest LOD count");

        uint64_t declaredInput = manifestBytes.size();
        charge(metadataSize, selection.maximumInputBytes, declaredInput, "bundle cumulative input capacity");
        std::vector<ManifestLod> bindings;
        bindings.reserve(lods.size());
        for (const auto& entry : lods) {
            fields(entry, {"id", "file", "sha256", "bytes", "source_sha256", "source_bytes"});
            ManifestLod lod;
            lod.id = lodId(entry["id"]);
            if (ruleIds.erase(lod.id) != 1) reject("bundle missing/duplicate/surplus LOD");
            lod.file = cookedLodFilename(lod.id);
            if (entry["file"] != lod.file) reject("bundle derived LOD filename");
            lod.bytes = count(entry["bytes"], selection.maximumInputBytes);
            lod.sourceBytes = count(entry["source_bytes"], kMaximumSourceGlbBytes);
            if (lod.sourceBytes < 20) reject("bundle source GLB size");
            lod.digest = digestField(entry["sha256"]);
            lod.sourceDigest = digestField(entry["source_sha256"]);
            lod.rule = &*std::find_if(selection.lodRules.begin(), selection.lodRules.end(),
                [&lod](const auto& rule) { return rule.id == lod.id; });
            charge(lod.bytes, selection.maximumInputBytes, declaredInput, "bundle cumulative input capacity");
            bindings.push_back(std::move(lod));
        }
        const auto metadataBytes = snapshot(provider, "gameplay.json", static_cast<size_t>(metadataSize), metadataSize, metadataDigest);
        candidate->lods_.reserve(bindings.size());
        for (const auto& binding : bindings) {
            auto bytes = snapshot(provider, binding.file, static_cast<size_t>(binding.bytes), binding.bytes, binding.digest);
            preflight(bytes, binding.rule->limits);
            if (bytes.size() > selection.maximumDecodedBytes - candidate->decodedBytes_)
                reject("bundle cumulative decode input capacity");
            AdmittedPartLod lod;
            lod.id = binding.id;
            lod.vmeshSha256 = binding.digest;
            std::string reason;
            if (!moto::readVmesh(bytes.data(), bytes.size(), &lod.mesh, &reason)) reject("bundle VMESH: " + reason);
            if (!prepareRigidPrefab(lod.mesh, {}, binding.rule->limits, lod.prefab, reason)) reject("bundle rigid prefab: " + reason);
            charge(lod.prefab.counts.decodedBytes, selection.maximumDecodedBytes, candidate->decodedBytes_, "bundle cumulative decoded capacity");
            charge(lod.prefab.counts.gpuBytes, selection.maximumGpuBytes, candidate->gpuBytes_, "bundle cumulative GPU capacity");
            candidate->lods_.push_back(std::move(lod));
        }
        const auto resolver = [&bindings](const construction::CookedMeshVisual& visual) {
            return std::any_of(bindings.begin(), bindings.end(), [&visual](const auto& binding) { return binding.file == visual.path; });
        };
        std::string reason;
        auto sidecar = parseGameplaySidecar(textView(metadataBytes), resolver, reason);
        if (!sidecar) reject("bundle gameplay sidecar: " + reason);
        if (sidecar->normalizedJson != textView(metadataBytes)) reject("bundle sidecar is not canonical normalized bytes");
        if (sidecar->part.key != selection.part || sidecar->lods.size() != bindings.size()) reject("bundle selected part/version or LOD count mismatch");
        for (const auto& binding : bindings) {
            const auto sidecarLod = std::find_if(sidecar->lods.begin(), sidecar->lods.end(),
                [&binding](const auto& lod) { return lod.id == binding.id; });
            if (sidecarLod == sidecar->lods.end() || sidecarLod->sourceBytes != binding.sourceBytes
                || sidecarLod->sourceSha256 != binding.sourceDigest) reject("bundle sidecar/source LOD binding mismatch");
            auto& lod = *std::find_if(candidate->lods_.begin(), candidate->lods_.end(),
                [&binding](const auto& item) { return item.id == binding.id; });
            lod.asset = sidecarLod->asset;
            if (!prepareRigidPrefab(lod.mesh, sidecarLod->renderToCanonical, binding.rule->limits, lod.prefab, reason))
                reject("bundle canonical prefab: " + reason);
        }
        std::sort(candidate->lods_.begin(), candidate->lods_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        candidate->sidecar_ = std::move(*sidecar);
        candidate->inputBytes_ = declaredInput;
        return candidate;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& exception) {
        error = std::string("cooked part rejected: ") + exception.what();
        return nullptr;
    }
}
} // namespace voxy::game::assets

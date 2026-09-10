#include "game/assets/gameplay_sidecar.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>

namespace voxy::game::assets {
namespace {
using namespace game::construction;
using Json = nlohmann::json;

[[noreturn]] void reject(std::string_view field) { throw std::runtime_error(std::string(field)); }

void fields(const Json& value, std::initializer_list<std::string_view> expected) {
    if (!value.is_object() || value.size() != expected.size()) reject("missing or unknown object fields");
    for (auto key : expected) if (!value.contains(std::string(key))) reject(key);
}

Json& member(Json& value, const char* key) {
    if (!value.is_object() || !value.contains(key)) reject(key);
    return value[key];
}

void array(const Json& value, size_t minimum, size_t maximum) {
    if (!value.is_array() || value.size() < minimum || value.size() > maximum) reject("array capacity or type");
}

std::string string(const Json& value, size_t maximum) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() > maximum) reject("string capacity or type");
    return value.get<std::string>();
}

uint64_t counter(const Json& value, bool zeroAllowed = false) {
    const auto number = u64FromDecimal(string(value, 20));
    if (!number || (!zeroAllowed && *number == 0)) reject("canonical decimal counter");
    return *number;
}

uint32_t unsignedNumber(Json& value, uint32_t maximum) {
    if (!value.is_number_integer()) reject("unsigned integer type");
    if (value.is_number_unsigned()) {
        if (value.get<uint64_t>() > maximum) reject("unsigned integer range");
    } else if (value.get<int64_t>() < 0 || value.get<int64_t>() > maximum) reject("unsigned integer range");
    const auto result = value.get<uint32_t>(); value = result; return result;
}

int32_t coordinate(Json& value) {
    if (!value.is_number_integer()) reject("grid coordinate type");
    if (value.is_number_unsigned() && value.get<uint64_t>() > static_cast<uint64_t>(kMaximumGridCoordinate)) reject("grid coordinate range");
    const auto result = value.get<int64_t>();
    if (result < -static_cast<int64_t>(kMaximumGridCoordinate) || result > kMaximumGridCoordinate) reject("grid coordinate range");
    value = result; return static_cast<int32_t>(result);
}

double number(Json& value) {
    if (!value.is_number()) reject("finite number type");
    const double result = value.get<double>();
    if (!std::isfinite(result)) reject("finite number range");
    value = result == 0.0 ? 0.0 : result;
    return result == 0.0 ? 0.0 : result;
}

WorldNamespace nameSpace(const Json& value) {
    const auto text = string(value, 32);
    if (text.size() != 32) reject("namespace width");
    const auto digit = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        reject("lowercase hexadecimal namespace");
    };
    WorldNamespace result;
    for (size_t i = 0; i < result.bytes.size(); ++i) result.bytes[i] = static_cast<uint8_t>(digit(text[i * 2]) * 16u + digit(text[i * 2 + 1]));
    if (!isValid(result)) reject("zero namespace");
    return result;
}

ContentKey contentKey(Json& value) {
    fields(value, {"namespace", "counter", "version"});
    ContentKey result{{nameSpace(value["namespace"]), counter(value["counter"])}, unsignedNumber(value["version"], UINT32_MAX)};
    if (result.version == 0) reject("zero content version");
    return result;
}

GridPosition grid(Json& value) {
    array(value, 3, 3); return {coordinate(value[0]), coordinate(value[1]), coordinate(value[2])};
}

MetresPosition metres(Json& value) {
    array(value, 3, 3); return {number(value[0]), number(value[1]), number(value[2])};
}

GridTransform frame(Json& value) {
    fields(value, {"translation_ticks", "rotation"});
    return {grid(value["translation_ticks"]), CubeRotation{static_cast<uint8_t>(unsignedNumber(value["rotation"], 23))}};
}

GridBox bounds(Json& value) {
    fields(value, {"minimum_ticks", "maximum_ticks"});
    return {grid(value["minimum_ticks"]), grid(value["maximum_ticks"])};
}

PartBox box(Json& value) {
    fields(value, {"id", "frame", "half_extents_ticks"});
    return {ProxyId{counter(value["id"])}, frame(value["frame"]), grid(value["half_extents_ticks"])};
}

StrengthLimits strength(Json& value) {
    fields(value, {"tension_newtons", "shear_newtons", "bending_newton_metres", "torsion_newton_metres"});
    return {number(value["tension_newtons"]), number(value["shear_newtons"]),
        number(value["bending_newton_metres"]), number(value["torsion_newton_metres"])};
}

std::array<double, 3> triple(Json& value) {
    array(value, 3, 3); return {number(value[0]), number(value[1]), number(value[2])};
}

ResourceAmounts resources(Json& value) {
    fields(value, {"salvage_material", "special_machinery"});
    return {counter(value["salvage_material"], true), counter(value["special_machinery"], true)};
}

bool nameKeyValid(const std::string& text) {
    return !text.empty() && text.front() >= 'a' && text.front() <= 'z'
        && std::all_of(text.begin(), text.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
        });
}

PartModule module(Json& value) {
    const auto type = string(member(value, "type"), 32);
    if (type == "structure" || type == "ballast") {
        fields(value, {"type"});
        return type == "structure" ? PartModule{StructureModule{}} : PartModule{BallastModule{}};
    }
    if (type == "flotation") {
        fields(value, {"type", "drag_coefficients"}); return FlotationModule{triple(value["drag_coefficients"])};
    }
    if (type == "engine") {
        fields(value, {"type", "shaft", "maximum_power_watts", "maximum_torque_newton_metres"});
        return EngineModule{SocketId{counter(value["shaft"])}, number(value["maximum_power_watts"]), number(value["maximum_torque_newton_metres"])};
    }
    if (type == "propeller") {
        fields(value, {"type", "shaft", "force_frame", "maximum_thrust_newtons", "required_power_watts"});
        return PropellerModule{SocketId{counter(value["shaft"])}, frame(value["force_frame"]),
            number(value["maximum_thrust_newtons"]), number(value["required_power_watts"])};
    }
    if (type == "helm") {
        fields(value, {"type", "operator_frame", "maximum_steering_radians"});
        return HelmModule{frame(value["operator_frame"]), number(value["maximum_steering_radians"])};
    }
    if (type == "winch") {
        fields(value, {"type", "line", "minimum_length_metres", "maximum_length_metres", "reel_speed_metres_per_second", "maximum_force_newtons"});
        return WinchModule{SocketId{counter(value["line"])}, number(value["minimum_length_metres"]),
            number(value["maximum_length_metres"]), number(value["reel_speed_metres_per_second"]), number(value["maximum_force_newtons"])};
    }
    if (type == "tow_eye") {
        fields(value, {"type", "eye"}); return TowEyeModule{SocketId{counter(value["eye"])}};
    }
    if (type == "cargo_cradle") {
        fields(value, {"type", "latch", "maximum_cargo_mass_kg", "capture_distance_metres", "capture_angle_radians", "capture_speed_metres_per_second", "capture_angular_speed_radians_per_second"});
        return CargoCradleModule{SocketId{counter(value["latch"])}, number(value["maximum_cargo_mass_kg"]),
            number(value["capture_distance_metres"]), number(value["capture_angle_radians"]),
            number(value["capture_speed_metres_per_second"]), number(value["capture_angular_speed_radians_per_second"])};
    }
    if (type == "brace") {
        fields(value, {"type", "load_transfer_factor"}); return BraceModule{number(value["load_transfer_factor"])};
    }
    if (type == "repair") {
        fields(value, {"type", "reach_metres", "health_fraction_per_second", "material_units_per_full_health"});
        return RepairModule{number(value["reach_metres"]), number(value["health_fraction_per_second"]),
            unsignedNumber(value["material_units_per_full_health"], UINT32_MAX)};
    }
    reject("unsupported module type");
}

PartDefinition definition(Json& value) {
    fields(value, {"key", "name_key", "permitted_rotation_mask", "footprint", "solid_occupancy", "collision",
        "mass", "buoyancy", "sockets", "strength", "module", "cost", "salvage_yield", "material"});
    PartDefinition part;
    part.key = contentKey(value["key"]); part.nameKey = string(value["name_key"], 96);
    part.permittedRotationMask = unsignedNumber(value["permitted_rotation_mask"], 0x00ffffff);
    part.footprint = bounds(value["footprint"]);
    for (const char* collection : {"solid_occupancy", "collision"}) {
        auto& input = value[collection]; array(input, 1, kMaximumPartBoxes);
        auto& output = std::string_view(collection) == "collision" ? part.collision : part.solidOccupancy;
        for (auto& item : input) output.push_back(box(item));
    }
    auto& mass = value["mass"];
    fields(mass, {"dry_mass_kg", "center_of_mass_metres", "inertia_kg_metres_squared"});
    part.mass.dryMassKg = number(mass["dry_mass_kg"]); part.mass.localCenterOfMass = metres(mass["center_of_mass_metres"]);
    array(mass["inertia_kg_metres_squared"], 9, 9);
    for (size_t i = 0; i < 9; ++i) part.mass.inertia.elements[i] = number(mass["inertia_kg_metres_squared"][i]);
    array(value["buoyancy"], 0, kMaximumPartBoxes);
    for (auto& item : value["buoyancy"]) {
        fields(item, {"box", "kind"}); const auto kind = string(item["kind"], 32);
        if (kind != "solid_material" && kind != "sealed_compartment") reject("buoyancy kind");
        part.buoyancy.push_back({box(item["box"]), kind == "solid_material" ? BuoyancyKind::SolidMaterial : BuoyancyKind::SealedCompartment});
    }
    array(value["sockets"], 1, kMaximumPartSockets);
    for (auto& item : value["sockets"]) {
        fields(item, {"id", "family", "role", "profile", "frame", "connection_capacity", "clearance", "strength"});
        SocketDefinition socket;
        socket.id = SocketId{counter(item["id"])};
        const auto family = string(item["family"], 32); const auto role = string(item["role"], 32);
        if (family == "structural") socket.family = SocketFamily::Structural;
        else if (family == "drive_shaft") socket.family = SocketFamily::DriveShaft;
        else if (family == "tow_line") socket.family = SocketFamily::TowLine;
        else if (family == "cargo_latch") socket.family = SocketFamily::CargoLatch;
        else reject("socket family");
        if (role == "neutral") socket.role = SocketRole::Neutral;
        else if (role == "plug") socket.role = SocketRole::Plug;
        else if (role == "receptacle") socket.role = SocketRole::Receptacle;
        else reject("socket role");
        socket.profile = unsignedNumber(item["profile"], UINT32_MAX); socket.frame = frame(item["frame"]);
        socket.connectionCapacity = static_cast<uint8_t>(unsignedNumber(item["connection_capacity"], 4));
        socket.clearance = bounds(item["clearance"]); socket.strength = strength(item["strength"]);
        part.sockets.push_back(socket);
    }
    part.strength = strength(value["strength"]); part.module = module(value["module"]);
    part.cost = resources(value["cost"]); part.salvageYield = resources(value["salvage_yield"]);
    fields(value["material"], {"linear_base_color", "roughness", "metallic"});
    part.material.linearBaseColor = triple(value["material"]["linear_base_color"]);
    part.material.roughness = number(value["material"]["roughness"]); part.material.metallic = number(value["material"]["metallic"]);
    return part;
}

void sortIds(Json& values, bool nestedBox = false) {
    std::sort(values.begin(), values.end(), [nestedBox](const Json& a, const Json& b) {
        return counter(nestedBox ? a["box"]["id"] : a["id"])
            < counter(nestedBox ? b["box"]["id"] : b["id"]);
    });
}

} // namespace

std::optional<GameplaySidecar> parseGameplaySidecar(
    std::string_view input, const game::construction::CookedAssetResolver& resolver, std::string& error) {
    using namespace game::construction;
    error.clear();
    if (input.empty() || input.size() > kMaximumSidecarBytes) { error = "sidecar byte capacity"; return std::nullopt; }
    try {
        std::vector<std::set<std::string>> objectKeys;
        auto document = Json::parse(input, [&objectKeys](int depth, Json::parse_event_t event, Json& parsed) {
            if (depth > 32) reject("JSON nesting capacity");
            if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
            else if (event == Json::parse_event_t::key) {
                const auto key = string(parsed, 128);
                if (objectKeys.empty() || !objectKeys.back().insert(key).second) reject("duplicate JSON object key");
            } else if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
            return true;
        });
        fields(document, {"schema", "units", "metadata_frame", "placement_lattice_metres", "part", "lods", "tool_anchors"});
        if (unsignedNumber(document["schema"], UINT32_MAX) != kGameplaySidecarSchema) reject("unsupported sidecar schema");
        fields(document["units"], {"length", "mass", "time", "angle"});
        if (document["units"]["length"] != "metres" || document["units"]["mass"] != "kilograms"
            || document["units"]["time"] != "seconds" || document["units"]["angle"] != "radians") reject("canonical physical units");
        if (document["metadata_frame"] != "canonical_y_up_minus_z_forward"
            || number(document["placement_lattice_metres"]) != 1.0 / kTicksPerMetre) reject("canonical metadata frame/lattice");
        auto part = definition(document["part"]);
        std::vector<LodBinding> lods;
        std::set<uint64_t> lodIds;
        std::set<ContentKey> assetKeys;
        array(document["lods"], 1, kMaximumVisualLods);
        for (auto& item : document["lods"]) {
            fields(item, {"id", "asset", "source", "minimum_screen_height_pixels"});
            fields(item["source"], {"file", "sha256", "bytes", "frame", "to_canonical_rotation"});
            auto& source = item["source"];
            LodBinding lod;
            lod.id = counter(item["id"]); lod.asset = contentKey(item["asset"]);
            if (!lodIds.insert(lod.id).second || !assetKeys.insert(lod.asset).second) reject("duplicate LOD or visual asset ID/version");
            lod.sourceFile = string(source["file"], 96); lod.sourceSha256 = string(source["sha256"], 64);
            if (lod.sourceFile.empty() || !lod.sourceFile.ends_with(".glb") || lod.sourceFile.starts_with('.')
                || !std::all_of(lod.sourceFile.begin(), lod.sourceFile.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
                })) reject("source GLB filename");
            if (lod.sourceSha256.size() != 64 || !std::all_of(lod.sourceSha256.begin(), lod.sourceSha256.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) reject("source SHA256");
            lod.sourceBytes = unsignedNumber(source["bytes"], static_cast<uint32_t>(kMaximumSourceGlbBytes));
            if (lod.sourceBytes < 20 || source["frame"] != "exported_gltf") reject("source GLB size/frame");
            lod.renderToCanonical = CubeRotation{static_cast<uint8_t>(unsignedNumber(source["to_canonical_rotation"], 23))};
            if (!basisToCanonical(SourceFrame::ExportedGltf, lod.renderToCanonical)) reject("source basis");
            lod.minimumScreenHeightPixels = number(item["minimum_screen_height_pixels"]);
            lods.push_back(lod);
            part.visuals.push_back({CookedMeshVisual{lod.asset, cookedLodFilename(lod.id)}, lod.minimumScreenHeightPixels});
        }
        std::sort(part.visuals.begin(), part.visuals.end(), [](const auto& a, const auto& b) {
            return a.minimumScreenHeightPixels > b.minimumScreenHeightPixels;
        });
        std::vector<ToolAnchor> anchors;
        std::set<uint64_t> anchorIds;
        std::set<std::string> anchorNames;
        array(document["tool_anchors"], 0, kMaximumSidecarAnchors);
        for (auto& item : document["tool_anchors"]) {
            fields(item, {"id", "name_key", "frame"});
            ToolAnchor anchor{counter(item["id"]), string(item["name_key"], 96), frame(item["frame"])};
            const auto p = anchor.frame.translation;
            if (!nameKeyValid(anchor.nameKey) || !anchorIds.insert(anchor.id).second || !anchorNames.insert(anchor.nameKey).second
                || p.x < part.footprint.minimum.x || p.x > part.footprint.maximum.x
                || p.y < part.footprint.minimum.y || p.y > part.footprint.maximum.y
                || p.z < part.footprint.minimum.z || p.z > part.footprint.maximum.z) reject("tool anchor ID/name/frame");
            anchors.push_back(anchor);
        }
        PartCatalogDraft draft;
        draft.definitions.push_back(std::move(part));
        CatalogIssue issue;
        const auto catalog = PartCatalog::create(draft, issue, resolver, CatalogPolicy{false});
        if (!catalog) {
            error = "PartCatalog " + std::string(issue.field) + ": error " + std::to_string(static_cast<unsigned>(issue.error));
            return std::nullopt;
        }
        auto& partJson = document["part"];
        sortIds(partJson["solid_occupancy"]); sortIds(partJson["collision"]);
        sortIds(partJson["buoyancy"], true); sortIds(partJson["sockets"]);
        sortIds(document["lods"]); sortIds(document["tool_anchors"]);
        std::sort(lods.begin(), lods.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        std::sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        return GameplaySidecar{catalog->definitions().front(), std::move(lods), std::move(anchors), document.dump(2) + "\n"};
    } catch (const std::exception& exception) {
        error = std::string("sidecar rejected: ") + exception.what();
        return std::nullopt;
    }
}

std::string cookedLodFilename(uint64_t id) { return "lod-" + game::construction::u64ToDecimal(id) + ".vmesh"; }

std::optional<game::construction::MetresPosition> renderPointToCanonical(
    const LodBinding& binding, game::construction::MetresPosition point) noexcept {
    return game::construction::toCanonicalFrame(point, game::construction::SourceFrame::ExportedGltf, binding.renderToCanonical);
}

} // namespace voxy::game::assets

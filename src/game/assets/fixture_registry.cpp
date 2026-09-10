#include "game/assets/fixture_registry.hpp"
#include "core/sha256.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace voxy::game::assets {
namespace {
using Json = nlohmann::json;
using namespace construction;

[[noreturn]] void reject(const char* message) { throw std::invalid_argument(message); }
void fields(const Json& value, std::initializer_list<std::string_view> names) {
    if (!value.is_object() || value.size() != names.size()) reject("fixture registry object fields");
    for (auto name : names) if (!value.contains(std::string(name))) reject("fixture registry unknown/missing field");
}
uint32_t natural(const Json& value, uint32_t maximum, bool zero = false) {
    if (!value.is_number_unsigned()) reject("fixture registry needs unsigned integer");
    const auto number = value.get<uint64_t>();
    if ((!zero && number == 0) || number > maximum) reject("fixture registry integer exceeds bound");
    return static_cast<uint32_t>(number);
}
uint64_t id(const Json& value) {
    if (!value.is_string()) reject("fixture registry ID must be canonical decimal string");
    const auto result = u64FromDecimal(value.get_ref<const std::string&>());
    if (!result || *result == 0) reject("fixture registry ID must be canonical nonzero decimal");
    return *result;
}
std::string hex(const Json& value, size_t length) {
    if (!value.is_string()) reject("fixture registry digest/namespace must be a string");
    auto text = value.get<std::string>();
    if (text.size() != length || !std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) reject("fixture registry lowercase hex width");
    return text;
}
ContentKey key(const Json& value) {
    fields(value, {"namespace", "counter", "version"});
    ContentKey result;
    const auto text = hex(value["namespace"], 32);
    const auto digit = [](char c) { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; i < 16; ++i) result.id.world.bytes[i] = static_cast<uint8_t>((digit(text[i*2]) << 4u) | digit(text[i*2+1]));
    result.id.counter = id(value["counter"]);
    result.version = natural(value["version"], UINT32_MAX);
    if (!isValid(result.id.world)) reject("fixture registry zero namespace");
    return result;
}
std::string directory(const Json& value) {
    if (!value.is_string()) reject("fixture registry directory must be a string");
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > 512 || text.front() == '/' || text.back() == '/') reject("fixture registry relative directory");
    size_t start = 0, components = 0;
    while (start < text.size()) {
        const auto end = text.find('/', start);
        const auto piece = std::string_view(text).substr(start, end == std::string::npos ? text.size()-start : end-start);
        if (++components > 8 || piece.empty() || piece.size() > 96 || piece.front() == '.'
            || !std::all_of(piece.begin(), piece.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            })) reject("fixture registry unsafe directory component");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return text;
}
glm::dvec3 point(const Json& value) {
    if (!value.is_array() || value.size() != 3) reject("fixture registry camera requires three coordinates");
    glm::dvec3 result;
    for (glm::length_t i = 0; i < 3; ++i) {
        const auto& component = value[static_cast<size_t>(i)];
        if (!component.is_number()) reject("fixture registry camera coordinate must be numeric");
        result[i] = component.get<double>();
        if (!std::isfinite(result[i]) || std::abs(result[i]) > 1000) reject("fixture registry camera bound");
    }
    return result;
}
Json registryDocument(std::string_view json) {
    if (json.empty() || json.size() > 64u * 1024u) reject("fixture registry byte ceiling");
    std::vector<std::set<std::string>> objectKeys;
    return Json::parse(json, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 16) reject("fixture registry nesting ceiling");
        if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
        if (event == Json::parse_event_t::key) {
            if (objectKeys.empty() || !objectKeys.back().insert(value.get<std::string>()).second)
                reject("fixture registry duplicate key");
        }
        if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
        return true;
    });
}
FixtureBundleSpec bundleSpec(const Json& value) {
    fields(value, {"directory", "part", "manifest_sha256", "lod_limits"});
    FixtureBundleSpec bundle;
    bundle.directory = directory(value["directory"]);
    bundle.selection.part = key(value["part"]);
    bundle.selection.manifestSha256 = hex(value["manifest_sha256"], 64);
    const auto& lods = value["lod_limits"];
    if (!lods.is_array() || lods.empty() || lods.size() > 8) reject("fixture registry LOD ceiling");
    std::set<uint64_t> ids;
    for (const auto& lod : lods) {
        fields(lod, {"id", "vertices", "triangles", "texture_dimension"});
        PartLodAdmissionRule rule;
        rule.id = id(lod["id"]);
        if (!ids.insert(rule.id).second) reject("fixture registry duplicate LOD ID");
        rule.limits.maximumVertices = natural(lod["vertices"], rule.limits.maximumVertices);
        rule.limits.maximumIndices = natural(lod["triangles"], rule.limits.maximumIndices / 3u) * 3u;
        rule.limits.maximumTextureDimension = natural(lod["texture_dimension"], rule.limits.maximumTextureDimension);
        bundle.selection.lodRules.push_back(rule);
    }
    return bundle;
}
} // namespace

std::optional<AssetFixtureRegistry> parseAssetFixtureRegistry(std::string_view json, std::string& error) {
    try {
        const auto document = registryDocument(json);
        if (!document.is_object() || !document.contains("schema")) reject("fixture registry schema");
        const auto schema = natural(document["schema"], 6);
        if (schema == 1) fields(document, {"schema", "bundles", "placements", "camera"});
        else if (schema == 3) fields(document, {"schema", "bundles", "placements", "camera", "navigation"});
        else if (schema >= 4) fields(document, {"schema", "bundles", "placements", "camera", "navigation", "connections"});
        else fields(document, {"schema", "bundles", "placements", "camera", "prototypes", "connections"});
        const auto& bundles = document["bundles"];
        const auto& placements = document["placements"];
        if (!bundles.is_array() || bundles.empty() || bundles.size() > kMaximumFixtureBundles
            || !placements.is_array() || placements.empty() || placements.size() > kMaximumFixturePlacements)
            reject("fixture registry bundle/placement ceiling");
        AssetFixtureRegistry result;
        result.schema = schema;
        if (schema == 3 || schema >= 4) {
            const auto& nav = document["navigation"];
            if(schema==6) fields(nav, {"spawn", "dock_boarding", "boat_boarding", "helm_standing", "look_target", "boat_placements", "cargo_placements", "delivery"});
            else if(schema==5) fields(nav, {"spawn", "dock_boarding", "boat_boarding", "helm_standing", "look_target", "boat_placements", "cargo_placements"});
            else fields(nav, {"spawn", "dock_boarding", "boat_boarding", "helm_standing", "look_target", "boat_placements"});
            result.navigation = CoveNavigation{point(nav["spawn"]), point(nav["dock_boarding"]),
                point(nav["boat_boarding"]), point(nav["helm_standing"]), point(nav["look_target"]), {}, {}, {}};
            const auto& members = nav["boat_placements"];
            if (!members.is_array() || members.empty() || members.size() >= placements.size())
                reject("cove boat placement membership");
            for (const auto& member : members) {
                const auto ordinal = natural(member, static_cast<uint32_t>(placements.size() - 1), true);
                auto& indices = result.navigation->boatPlacements;
                if (std::find(indices.begin(), indices.end(), ordinal) != indices.end())
                    reject("cove duplicate boat placement");
                indices.push_back(ordinal);
            }
            if(schema>=5) {
                const auto& cargo=nav["cargo_placements"];
                if(!cargo.is_array() || cargo.size()!=1) reject("starter cove requires one salvage load");
                for(const auto& member:cargo) {
                    const auto ordinal=natural(member,static_cast<uint32_t>(placements.size()-1),true);
                    if(std::find(result.navigation->boatPlacements.begin(),result.navigation->boatPlacements.end(),ordinal)
                        !=result.navigation->boatPlacements.end()) reject("cargo cannot also belong to the boat");
                    result.navigation->cargoPlacements.push_back(ordinal);
                }
            }
            if(schema==6) {
                const auto& delivery=nav["delivery"];
                fields(delivery,{"center","radius","minimum_height","maximum_speed","maximum_angular_speed"});
                const auto number=[&](const char* key,double low,double high) {
                    if(!delivery[key].is_number()) reject("delivery zone numeric field");
                    const double value=delivery[key].get<double>();
                    if(!std::isfinite(value) || value<low || value>high) reject("delivery zone bound");
                    return value;
                };
                result.navigation->delivery=CoveDeliveryZone{point(delivery["center"]),number("radius",2,10),
                    number("minimum_height",-2,2),number("maximum_speed",.05,2),number("maximum_angular_speed",.05,2)};
            }
            const double gap = glm::length(result.navigation->dockBoarding - result.navigation->boatBoarding);
            if (gap < .5 || gap > 3) reject("cove boarding gap must be between .5 and 3 metres");
        }
        if (schema == 2) {
            const auto& prototypes = document["prototypes"];
            if (!prototypes.is_array() || prototypes.size() > 8) reject("fixture registry prototype ceiling");
            for (const auto& value : prototypes) {
                const auto selected = key(value);
                if (std::find(result.prototypes.begin(),result.prototypes.end(),selected) != result.prototypes.end())
                    reject("fixture registry duplicate prototype definition");
                result.prototypes.push_back(selected);
            }
        }
        for (const auto& value : bundles) result.bundles.push_back(bundleSpec(value));
        for (const auto& value : placements) {
            FixturePartPlacement placement;
            placement.prototype = schema == 2 && value.contains("prototype");
            if (placement.prototype) {
                fields(value, {"prototype", "translation_ticks", "rotation"});
                if (result.prototypes.empty()) reject("fixture registry missing prototype definition");
                placement.bundleIndex = natural(value["prototype"],static_cast<uint32_t>(result.prototypes.size()-1u),true);
            } else {
                fields(value, {"bundle", "translation_ticks", "rotation"});
                placement.bundleIndex = natural(value["bundle"], static_cast<uint32_t>(bundles.size()-1u), true);
            }
            placement.placement.rotation.value = static_cast<uint8_t>(natural(value["rotation"], 23, true));
            const auto& ticks = value["translation_ticks"];
            if (!ticks.is_array() || ticks.size() != 3) reject("fixture registry placement requires three ticks");
            std::array<int32_t,3> coordinates{};
            for (size_t i = 0; i < 3; ++i) {
                if (!ticks[i].is_number_integer()) reject("fixture registry ticks must be integers");
                if (ticks[i].is_number_unsigned() && ticks[i].get<uint64_t>() > 1000000u)
                    reject("fixture registry tick bound");
                const auto tick = ticks[i].get<int64_t>();
                if (tick < -1000000 || tick > 1000000) reject("fixture registry tick bound");
                coordinates[i] = static_cast<int32_t>(tick);
            }
            placement.placement.translation = {coordinates[0],coordinates[1],coordinates[2]};
            result.placements.push_back(placement);
        }
        if (schema == 2 || schema >= 4) {
            const auto& connections = document["connections"];
            if (!connections.is_array() || connections.empty() || connections.size() > kMaximumFixtureConnections)
                reject("fixture registry connection ceiling");
            for (const auto& value : connections) {
                fields(value,{"a","b"}); fields(value["a"],{"placement","socket"}); fields(value["b"],{"placement","socket"});
                const auto maximum = static_cast<uint32_t>(placements.size()-1u);
                FixtureSocketConnection connection{
                    natural(value["a"]["placement"],maximum,true),natural(value["b"]["placement"],maximum,true),
                    SocketId{id(value["a"]["socket"])},SocketId{id(value["b"]["socket"])} };
                if (connection.aPlacement == connection.bPlacement) reject("fixture registry self connection");
                result.connections.push_back(connection);
            }
        }
        fields(document["camera"], {"eye", "target"});
        result.cameraEye = point(document["camera"]["eye"]);
        result.cameraTarget = point(document["camera"]["target"]);
        const auto direction = result.cameraTarget - result.cameraEye;
        if (glm::length(direction) < .01 || std::hypot(direction.x, direction.z) < .01)
            reject("fixture registry camera needs a nonvertical view direction");
        error.clear();
        return result;
    } catch (const std::bad_alloc&) { throw; }
      catch (const std::exception& failure) { error = failure.what(); return std::nullopt; }
}

std::unique_ptr<const LoadedAssetFixture> loadAssetFixture(const std::filesystem::path& path, std::string& error) {
    const auto root = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    const auto provider = openCookedPartDirectory(root, error);
    if (!provider) return {};
    const auto bytes = (*provider)(path.filename().string(), 64u * 1024u, error);
    if (!bytes) return {};
    auto registry = parseAssetFixtureRegistry(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error);
    if (!registry) return {};
    auto result = std::make_unique<LoadedAssetFixture>();
    result->registry = std::move(*registry);
    result->installedRegistryDigest = core::sha256(std::as_bytes(std::span(*bytes))).bytes;
    for (const auto& bundle : result->registry.bundles) {
        const auto source = openCookedPartDirectory(root / bundle.directory, error);
        if (!source) return {};
        auto admitted = admitCookedPartBundle(bundle.selection, *source, error);
        if (!admitted) return {};
        result->bundles.emplace_back(std::move(admitted));
    }
    if (result->registry.schema == 2) {
        const auto fail = [&](const std::string& message) -> std::unique_ptr<const LoadedAssetFixture> {
            error = "fixture assembly: " + message; return {};
        };
        CatalogIssue catalogIssue;
        const auto starter = PartCatalog::create(makeStarterCatalogDraft(),catalogIssue);
        if (!starter) return fail("invalid built-in prototype catalog");
        PartCatalogDraft draft;
        for (const auto selected : result->registry.prototypes) {
            const auto found = starter->lookup(selected);
            if (!found || found.definition->visuals.size() != 1
                || !std::holds_alternative<PrototypeBoxVisual>(found.definition->visuals[0].asset))
                return fail("unknown exact prototype definition/version or non-box visual");
            result->prototypes.push_back(*found.definition);
            draft.definitions.push_back(*found.definition);
        }
        for (const auto& bundle : result->bundles) draft.definitions.push_back(bundle->sidecar().part);
        const auto catalog = PartCatalog::create(draft,catalogIssue,[&](const CookedMeshVisual& visual) {
            for (const auto& bundle : result->bundles) for (const auto& lod : bundle->lods())
                if (lod.asset == visual.asset) return true;
            return false;
        });
        if (!catalog) return fail("catalog " + std::string(catalogIssue.field));
        // A deliberately private namespace for this transient CPU proof. It
        // is never an allocation from the player's durable world ID sequence.
        constexpr WorldNamespace inspectionWorld{{'v','o','x','y','-','i','n','s','p','e','c','t','-','v','0','1'}};
        const auto localId = [&](uint64_t counter) { return DurableId{inspectionWorld,counter}; };
        BuildSnapshot build; build.id=localId(1); build.owner=localId(2);
        result->connectedSockets.resize(result->registry.placements.size());
        for (size_t i=0; i<result->registry.placements.size(); ++i) {
            const auto& placed = result->registry.placements[i];
            const auto& definition = placed.prototype ? result->prototypes[placed.bundleIndex]
                : result->bundles[placed.bundleIndex]->sidecar().part;
            PartInstance instance;
            instance.id=localId(3u+i); instance.owningBuild=build.id;
            instance.definition=definition.key; instance.placement=placed.placement;
            instance.settings=defaultModuleSettings(definition);
            build.parts.push_back(instance);
        }
        for (size_t i=0; i<result->registry.connections.size(); ++i) {
            const auto& source=result->registry.connections[i];
            const auto& a=build.parts[source.aPlacement]; const auto& b=build.parts[source.bPlacement];
            const auto* sa=findSocket(*catalog->lookup(a.definition).definition,source.aSocket);
            const auto* sb=findSocket(*catalog->lookup(b.definition).definition,source.bSocket);
            if (!sa || !sb) return fail("connection references a missing socket");
            Connection connection; connection.id=localId(3u+build.parts.size()+i);
            connection.a={a.id,source.aSocket}; connection.b={b.id,source.bSocket};
            connection.strength={std::min(sa->strength.tensionNewtons,sb->strength.tensionNewtons),
                std::min(sa->strength.shearNewtons,sb->strength.shearNewtons),
                std::min(sa->strength.bendingNewtonMetres,sb->strength.bendingNewtonMetres),
                std::min(sa->strength.torsionNewtonMetres,sb->strength.torsionNewtonMetres)};
            build.connections.push_back(connection);
            result->connectedSockets[source.aPlacement].push_back(source.aSocket);
            result->connectedSockets[source.bPlacement].push_back(source.bSocket);
        }
        BuildIssue issue;
        result->assembly=BuildModel::create(build,*catalog,issue);
        if (!result->assembly) return fail("BuildModel error " + std::to_string(static_cast<unsigned>(issue.error))
            + " at " + std::string(issue.field));
        for (auto& sockets : result->connectedSockets) {
            std::sort(sockets.begin(),sockets.end());
            sockets.erase(std::unique(sockets.begin(),sockets.end()),sockets.end());
        }
    }
    error.clear();
    return result;
}

std::unique_ptr<const LoadedAssetFixture> appendAssetFixtureCatalog(
    const LoadedAssetFixture& base,const std::filesystem::path& path,std::string& error) {
    try {
        const auto root=path.has_parent_path()?path.parent_path():std::filesystem::path(".");
        const auto provider=openCookedPartDirectory(root,error);if(!provider)return {};
        const auto bytes=(*provider)(path.filename().string(),64u*1024u,error);if(!bytes)return {};
        const auto document=registryDocument(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
        fields(document,{"schema","bundles"});
        if(natural(document["schema"],1)!=1)reject("catalogue schema");
        const auto& entries=document["bundles"];
        if(!entries.is_array() || entries.empty() || base.bundles.size()>kMaximumFixtureBundles
            || entries.size()>kMaximumFixtureBundles-base.bundles.size()
            || base.registry.bundles.size()!=base.bundles.size())reject("catalogue bundle ceiling");
        auto result=std::make_unique<LoadedAssetFixture>(base);
        PartCatalogDraft draft;draft.definitions=base.prototypes;
        for(const auto& bundle:base.bundles) {
            if(!bundle)reject("catalogue base bundle missing");
            draft.definitions.push_back(bundle->sidecar().part);
        }
        for(const auto& entry:entries) {
            auto spec=bundleSpec(entry);
            if(std::any_of(draft.definitions.begin(),draft.definitions.end(),[&](const auto& part){
                return part.key.id==spec.selection.part.id;
            }))reject("catalogue cannot replace an installed part ID");
            const auto source=openCookedPartDirectory(root/spec.directory,error);if(!source)return {};
            auto bundle=admitCookedPartBundle(spec.selection,*source,error);if(!bundle)return {};
            draft.definitions.push_back(bundle->sidecar().part);
            result->registry.bundles.push_back(std::move(spec));
            result->bundles.emplace_back(std::move(bundle));
        }
        CatalogIssue issue;
        const auto catalog=PartCatalog::create(draft,issue,[&](const CookedMeshVisual& visual){
            for(const auto& bundle:result->bundles)for(const auto& lod:bundle->lods())
                if(lod.asset==visual.asset)return true;
            return false;
        });
        if(!catalog){error="catalogue: "+std::string(issue.field);return {};}
        // Base layout, navigation, current assembly and digest remain exact.
        // Only unused catalogue definitions have been appended. New owned
        // parts still undergo the complete canonical build/save validation.
        error.clear();return result;
    } catch(const std::bad_alloc&){throw;}
      catch(const std::exception& failure){error=failure.what();return {};}
}

std::optional<uint64_t> selectFixtureLod(const CookedPartBundle& bundle,
    const glm::dmat4& root, GridTransform placement, const glm::dmat4& viewProjection,
    uint32_t height) {
    if (height == 0 || height > 8192 || bundle.lods().empty() || bundle.sidecar().lods.empty()) return {};
    // Sidecar records are canonically sorted by durable ID, not by screen
    // threshold. IDs can remain stable while the quality ordering changes.
    const LodBinding* finest = &bundle.sidecar().lods.front();
    const LodBinding* coarsest = finest;
    for (const auto& lod : bundle.sidecar().lods) {
        if (lod.minimumScreenHeightPixels > finest->minimumScreenHeightPixels) finest = &lod;
        if (lod.minimumScreenHeightPixels < coarsest->minimumScreenHeightPixels) coarsest = &lod;
    }
    const auto rotation = rotationMatrix(placement.rotation);
    const auto translation = toMetres(placement.translation);
    if (!rotation || !translation) return {};
    glm::dmat4 grid(1.0);
    for (glm::length_t c = 0; c < 3; ++c) for (glm::length_t r = 0; r < 3; ++r)
        grid[c][r] = static_cast<double>(rotation->elements[static_cast<size_t>(r*3+c)]);
    grid[3] = {translation->x, translation->y, translation->z, 1};
    auto minimum = bundle.lods()[0].prefab.canonicalBounds.minimum;
    auto maximum = bundle.lods()[0].prefab.canonicalBounds.maximum;
    for (const auto& lod : bundle.lods()) {
        minimum = glm::min(minimum, lod.prefab.canonicalBounds.minimum);
        maximum = glm::max(maximum, lod.prefab.canonicalBounds.maximum);
    }
    const auto transform = viewProjection * root * grid;
    for (glm::length_t c = 0; c < 4; ++c) for (glm::length_t r = 0; r < 4; ++r)
        if (!std::isfinite(transform[c][r])) return {};
    double low = std::numeric_limits<double>::infinity();
    double high = -low;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const auto clip = transform * glm::dvec4(corner & 1u ? maximum.x : minimum.x,
            corner & 2u ? maximum.y : minimum.y, corner & 4u ? maximum.z : minimum.z, 1);
        if (!std::isfinite(clip.y) || !std::isfinite(clip.w)) return {};
        if (clip.w <= 1.0e-8) return finest->id;
        const auto y = clip.y / clip.w;
        low = std::min(low, y); high = std::max(high, y);
    }
    const double pixels = (high - low) * .5 * static_cast<double>(height);
    const LodBinding* selected = nullptr;
    for (const auto& lod : bundle.sidecar().lods) {
        if (pixels >= lod.minimumScreenHeightPixels
            && (!selected || lod.minimumScreenHeightPixels > selected->minimumScreenHeightPixels)) selected = &lod;
    }
    return (selected ? selected : coarsest)->id;
}

} // namespace voxy::game::assets

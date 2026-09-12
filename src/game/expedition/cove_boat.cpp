#include "game/expedition/cove_boat.hpp"

#include <algorithm>
#include <array>

namespace voxy::game::expedition {
using namespace construction;

std::optional<PartCatalog> makeCovePartCatalog(const assets::LoadedAssetFixture& scene,std::string& error) {
    PartCatalogDraft draft;
    for (const auto& bundle : scene.bundles) {
        if (!bundle) {error="Missing admitted cove part";return std::nullopt;}
        draft.definitions.push_back(bundle->sidecar().part);
    }
    for (const auto& prototype : scene.prototypes) draft.definitions.push_back(prototype);
    CatalogIssue catalogIssue;
    const auto catalog = PartCatalog::create(draft, catalogIssue, [&](const CookedMeshVisual& visual) {
        for (const auto& bundle : scene.bundles) for (const auto& lod : bundle->lods())
            if (lod.asset == visual.asset) return true;
        return false;
    });
    if (!catalog) {error="Cove catalog: "+std::string(catalogIssue.field);return std::nullopt;}
    error.clear();return catalog;
}

std::unique_ptr<CoveSceneryCollision> CoveSceneryCollision::compile(
    const assets::LoadedAssetFixture& scene, std::string& error) {
    const auto fail=[&](const char* detail)->std::unique_ptr<CoveSceneryCollision>{
        error=std::string("Cove scenery collision: ")+detail;return {};
    };
    if(!scene.registry.navigation || scene.registry.placements.size()>assets::kMaximumFixturePlacements) return fail("missing bounded navigation");
    std::array<bool,assets::kMaximumFixturePlacements> boat{};
    for(uint32_t index:scene.registry.navigation->boatPlacements) {
        if(index>=scene.registry.placements.size() || boat[index]) return fail("invalid boat membership");
        boat[index]=true;
    }
    std::optional<GridPosition> origin;
    std::vector<geometry::UnionBox> boxes;
    std::vector<Source> sources;
    for(size_t index=0;index<scene.registry.placements.size();++index) {
        const auto& cargo=scene.registry.navigation->cargoPlacements;
        if(boat[index] || std::find(cargo.begin(),cargo.end(),index)!=cargo.end()) continue;
        const auto& placement=scene.registry.placements[index];
        if(placement.prototype || placement.bundleIndex>=scene.bundles.size() || !scene.bundles[placement.bundleIndex])
            return fail("requires admitted authored scenery");
        if(!origin) origin=placement.placement.translation;
        auto frame=placement.placement;
        const auto local=checkedSubtract(frame.translation,*origin);
        if(!local) return fail("placement overflow");
        frame.translation=*local;
        for(const auto& proxy:scene.bundles[placement.bundleIndex]->sidecar().part.collision) {
            const auto bounds=boxBounds(proxy);
            const auto transformed=bounds?transformBounds(frame,*bounds):std::nullopt;
            if(!transformed || boxes.size()>=geometry::kMaximumUnionInputBoxes) return fail("bounds or capacity");
            sources.push_back({static_cast<uint32_t>(index),proxy.id});
            boxes.push_back({*transformed,static_cast<uint32_t>(sources.size())});
        }
    }
    if(!origin) return fail("missing scenery");
    geometry::BoxUnionIssue unionIssue;
    const auto exterior=geometry::BoxUnion::compile(boxes,unionIssue);
    if(!exterior) return fail("exterior preparation");
    // Fixed scenery has infinite effective mass/inertia. These positive unit
    // reference values only satisfy the shared immutable frame format; Static
    // admission zeros their inverse values in the live body record.
    physics::AuthoredShapeIssue shapeIssue;
    auto shape=physics::AuthoredShape::prepare(*exterior,{1,{0,0,0},{1,0,0,0,1,0,0,0,1}},shapeIssue);
    if(!shape) return fail("shape preparation");
    error.clear();
    return std::unique_ptr<CoveSceneryCollision>(new CoveSceneryCollision(std::move(*shape),
        glm::dvec3(origin->x,origin->y,origin->z)*.02,std::move(sources)));
}

std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compile(
    const assets::LoadedAssetFixture& scene, std::string& error, Diagnostic* diagnostic) {
    if(diagnostic)*diagnostic={};
    if(!scene.registry.navigation) {error="Missing cove navigation";if(diagnostic)diagnostic->failure=Diagnostic::Failure::InvalidScene;return {};}
    return compileMembers(scene,scene.registry.navigation->boatPlacements,error,false,diagnostic);
}
std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileCargo(
    const assets::LoadedAssetFixture& scene,uint32_t placement,std::string& error) {
    return compileMembers(scene,std::span{&placement,1},error);
}
std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileSeparatedScene(
    const assets::LoadedAssetFixture& scene,std::string& error) {
    if(!scene.registry.navigation) {error="Missing cove navigation";return {};}
    return compileMembers(scene,scene.registry.navigation->boatPlacements,error,true);
}
std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileMembers(
    const assets::LoadedAssetFixture& scene,std::span<const uint32_t> members,std::string& error,bool separated,Diagnostic* diagnostic) {
    const auto fail = [&](const std::string& detail) -> std::unique_ptr<CoveBoatAssembly> {
        if(diagnostic)diagnostic->failure=Diagnostic::Failure::InvalidScene;
        error = "Boat assembly: " + detail; return {};
    };
    if (members.empty() || scene.registry.placements.size()>assets::kMaximumFixturePlacements) return fail("missing physical membership");
    std::array<bool,assets::kMaximumFixturePlacements> selected{};
    for (uint32_t index : members) {
        if (index >= scene.registry.placements.size() || selected[index]) return fail("invalid boat membership");
        selected[index] = true;
    }
    const auto catalog=makeCovePartCatalog(scene,error);
    if(!catalog){if(diagnostic)diagnostic->failure=Diagnostic::Failure::InvalidScene;return {};}
    constexpr WorldNamespace sceneWorld{{'v','o','x','y','-','c','o','v','e','-','b','o','a','t','0','1'}};
    const auto id = [&](uint64_t value) { return DurableId{sceneWorld, value}; };
    BuildSnapshot build; build.id = id(1); build.owner = id(2);
    std::vector<Part> parts;
    std::array<size_t,assets::kMaximumFixturePlacements> partAt{};
    for (size_t index = 0; index < scene.registry.placements.size(); ++index) {
        if (!selected[index]) continue;
        const auto& placement = scene.registry.placements[index];
        if (placement.prototype ? placement.bundleIndex >= scene.prototypes.size()
                                : placement.bundleIndex >= scene.bundles.size()) return fail("unknown placed part");
        const auto& definition = placement.prototype ? scene.prototypes[placement.bundleIndex]
            : scene.bundles[placement.bundleIndex]->sidecar().part;
        PartInstance part;
        part.id = id(100 + index); part.owningBuild = build.id;
        part.definition = definition.key; part.placement = placement.placement;
        part.settings = placement.settings.value_or(defaultModuleSettings(definition));
        part.paint = placement.paint.value_or(part.paint);
        partAt[index] = build.parts.size();
        parts.push_back({static_cast<uint32_t>(index), part.id});
        build.parts.push_back(std::move(part));
    }
    for (size_t index = 0; index < scene.registry.connections.size(); ++index) {
        const auto& connection = scene.registry.connections[index];
        if (connection.aPlacement >= scene.registry.placements.size()
            || connection.bPlacement >= scene.registry.placements.size()) return fail("unknown connection endpoint");
        if (selected[connection.aPlacement] != selected[connection.bPlacement]) return fail("boat is welded to scenery");
        if (!selected[connection.aPlacement]) continue;
        const auto& a = build.parts[partAt[connection.aPlacement]];
        const auto& b = build.parts[partAt[connection.bPlacement]];
        const auto* sa = findSocket(*catalog->lookup(a.definition).definition, connection.aSocket);
        const auto* sb = findSocket(*catalog->lookup(b.definition).definition, connection.bSocket);
        if (!sa || !sb) return fail("unknown welded socket");
        Connection weld;
        weld.id = id(1000 + index); weld.a = {a.id, connection.aSocket}; weld.b = {b.id, connection.bSocket};
        weld.strength = {std::min(sa->strength.tensionNewtons, sb->strength.tensionNewtons),
            std::min(sa->strength.shearNewtons, sb->strength.shearNewtons),
            std::min(sa->strength.bendingNewtonMetres, sb->strength.bendingNewtonMetres),
            std::min(sa->strength.torsionNewtonMetres, sb->strength.torsionNewtonMetres)};
        build.connections.push_back(weld);
    }
    if(separated) {
        std::optional<DurableId> helm;
        for(const auto& part:build.parts)if(std::holds_alternative<HelmModule>(catalog->lookup(part.definition).definition->module)) {
            if(helm)return fail("multiple control helms");
            helm=part.id;
        }
        if(!helm)return fail("missing control helm");
        return compileFragments(build,*catalog,parts,*helm,error);
    }
    return compileRoots(build,*catalog,parts,std::nullopt,error,diagnostic);
}

std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileBuild(
    const BuildSnapshot& build,const PartCatalog& catalog,std::span<const Part> placements,std::string& error) {
    return compileRoots(build,catalog,placements,std::nullopt,error);
}
std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileFragments(
    const BuildSnapshot& build,const PartCatalog& catalog,std::span<const Part> placements,DurableId controlPart,std::string& error) {
    return compileRoots(build,catalog,placements,controlPart,error);
}
std::unique_ptr<CoveBoatAssembly> CoveBoatAssembly::compileRoots(
    const BuildSnapshot& build,const PartCatalog& catalog,std::span<const Part> placements,
    std::optional<DurableId> controlPart,std::string& error,Diagnostic* diagnostic) {
    const auto fail=[&](const std::string& detail,Diagnostic::Failure failure=Diagnostic::Failure::PhysicalPreparation)->std::unique_ptr<CoveBoatAssembly>{
        if(diagnostic)diagnostic->failure=failure;
        error="Boat assembly: "+detail;return {};
    };
    if(placements.size()!=build.parts.size() || placements.empty() || placements.size()>assets::kMaximumFixturePlacements)return fail("part mapping count");
    std::array<bool,assets::kMaximumFixturePlacements> used{};
    for(size_t i=0;i<placements.size();++i) {
        const auto& binding=placements[i];
        if(binding.placement>=used.size() || used[binding.placement])return fail("duplicate or invalid scene mapping");
        used[binding.placement]=true;
        if(std::count_if(build.parts.begin(),build.parts.end(),[&](const auto& part){return part.id==binding.id;})!=1)
            return fail("missing canonical mapped part");
        for(size_t j=0;j<i;++j)if(placements[j].id==binding.id)return fail("duplicate canonical mapped part");
    }
    AssemblyFunctionIssue issue;
    auto assembly = CompiledAssembly::compile(build, catalog, issue);
    if(diagnostic) {
        diagnostic->assembly=issue;
        const auto object=issue.buoyancy.collision.assembly.build.object;
        const auto part=std::find_if(placements.begin(),placements.end(),[&](const auto& p){return p.id==object;});
        if(part!=placements.end())diagnostic->placement=part->placement;
    }
    if (!assembly) return fail("invalid physical build at " + std::string(issue.buoyancy.collision.assembly.build.field)
        + " (build " + std::to_string(static_cast<unsigned>(issue.buoyancy.collision.assembly.build.error))
        + ", assembly " + std::to_string(static_cast<unsigned>(issue.buoyancy.collision.assembly.error))
        + ", exterior " + std::to_string(static_cast<unsigned>(issue.buoyancy.collision.geometry.error))
        + ", coverage " + std::to_string(static_cast<unsigned>(issue.buoyancy.coverage.error))
        + ", functions " + std::to_string(static_cast<unsigned>(issue.error)) + ")",Diagnostic::Failure::Compilation);
    const auto count=assembly->mass().roots().size();
    if(!controlPart && count!=1)return fail("boat parts must form one welded body",Diagnostic::Failure::Disconnected);
    if(count==0 || count!=assembly->collision().roots().size() || count!=assembly->buoyancy().roots().size())
        return fail("incomplete rigid roots");
    uint32_t primary=0;
    const auto& functions=assembly->functions();
    if(controlPart) {
        const auto* module=functions.module(*controlPart);
        if(!module || !std::holds_alternative<HelmModule>(module->parameters))return fail("missing control helm");
        primary=module->root;
    }
    std::vector<Root> roots;roots.reserve(count);
    for(size_t index=0;index<count;++index) {
        const auto& mass=assembly->mass().roots()[index].mass;
        const auto& collision=assembly->collision().roots()[index];
        const auto& buoyancy=assembly->buoyancy().roots()[index];
        if(collision.massRoot!=index || buoyancy.massRoot!=index)return fail("mismatched rigid roots");
        physics::AuthoredShapeIssue shapeIssue;
        auto shape=physics::AuthoredShape::prepare(collision.shape,
            {mass.dryMassKg,{mass.localCenterOfMass.x,mass.localCenterOfMass.y,mass.localCenterOfMass.z},mass.inertia.elements},shapeIssue);
        if(!shape)return fail("GPU hull preparation "+std::to_string(static_cast<unsigned>(shapeIssue.error)));
        Root root{std::move(*shape)};root.cells.reserve(buoyancy.coverage.cells().size());
        for(const auto& cell:buoyancy.coverage.cells()) {
            const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
            root.cells.push_back({glm::vec3(a.x,a.y,a.z)*.02f,glm::vec3(b.x,b.y,b.z)*.02f});
        }
        roots.push_back(std::move(root));
    }
    for(size_t index=0;index<functions.modules().size();++index) {
        const auto& module=functions.modules()[index];
        if(module.root>=roots.size())return fail("unknown module root");
        auto& root=roots[module.root];const auto output=coveModuleOutput(module);
        if(const auto* propeller=std::get_if<PropellerModule>(&module.parameters)) {
            if(root.propeller)return fail("one propeller per rigid cove body is supported",Diagnostic::Failure::DuplicatePropeller);
            unsigned frames=0;
            for(const auto& frame:functions.moduleFrames(index))if(frame.kind==AssemblyFrameKind::Thrust) {
                const auto direction=rotate(frame.rootFromFrame.rotation,{0,0,-1});
                if(!direction)return fail("invalid propeller direction");
                const auto p=frame.rootFromFrame.translation;
                root.propellerPoint=glm::vec3(p.x,p.y,p.z)*.02f;
                root.propellerDirection=glm::vec3(direction->x,direction->y,direction->z)*(module.settings.reversed?-1.0f:1.0f);
                ++frames;
            }
            if(frames!=1)return fail("missing propeller frame");
            root.propeller=module.part;root.maximumThrustNewtons=static_cast<float>(propeller->maximumThrustNewtons)*output;
        }
        if(const auto* helm=std::get_if<HelmModule>(&module.parameters)) {
            if(root.helm)return fail("one helm per rigid cove body is supported",Diagnostic::Failure::DuplicateHelm);
            root.helm=module.part;root.maximumSteeringRadians=static_cast<float>(helm->maximumSteeringRadians)*output;
        }
    }
    auto result = std::unique_ptr<CoveBoatAssembly>(new CoveBoatAssembly(
        std::move(*assembly),std::move(roots),primary,std::vector<Part>(placements.begin(),placements.end()),build));
    error.clear(); return result;
}

physics::AuthoredWaterBodyDesc CoveBoatAssembly::Root::water(physics::BodyHandle body) const noexcept {
    return {.body=body,.cells=cells,.propellerPoint=propellerPoint,.propellerDirection=propellerDirection,
        .maximumThrustNewtons=maximumThrustNewtons,.maximumSteeringRadians=maximumSteeringRadians};
}
std::optional<uint32_t> CoveBoatAssembly::rootForPart(DurableId part) const noexcept {
    const auto* module=assembly_.functions().module(part);
    return module?std::optional{module->root}:std::nullopt;
}
double CoveBoatAssembly::displacementCubicMetres() const noexcept {
    double volume=0;
    for(const auto& root:assembly_.buoyancy().roots())volume+=static_cast<double>(root.coverage.stats().volumeTicks3)*.02*.02*.02;
    return volume;
}

double CoveBoatAssembly::equilibriumRootHeight(double waterDensity) const noexcept {
    const double target=primaryMassRoot().mass.dryMassKg/waterDensity;
    double lower=-256,upper=256;
    for (int iteration=0;iteration<48;++iteration) {
        const double y=(lower+upper)*.5;
        double volume=0;
        for(const auto& cell:assembly_.buoyancy().roots()[primaryRoot_].coverage.cells()) {
            const auto a=cell.bounds.minimum,b=cell.bounds.maximum;
            const double submerged=std::clamp(-y-double(a.y)*.02,0.0,double(b.y-a.y)*.02);
            volume+=double(b.x-a.x)*.02*double(b.z-a.z)*.02*submerged;
        }
        if(volume>target) lower=y; else upper=y;
    }
    return (lower+upper)*.5;
}
} // namespace voxy::game::expedition

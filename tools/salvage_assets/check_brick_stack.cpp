// Offline content validation through the production admission/build compiler.
// This does not substitute for the playable editor acceptance journey.
#include "game/assets/fixture_registry.hpp"
#include "game/construction/compiled_assembly.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace voxy::game::assets;
using namespace voxy::game::construction;

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: check_brick_stack <fixture.json>");
        std::string error;
        const auto fixture=loadAssetFixture(argv[1],error);
        if(!fixture || !fixture->assembly)throw std::runtime_error("admission: "+error);
        PartCatalogDraft draft;
        for(const auto& b:fixture->bundles)draft.definitions.push_back(b->sidecar().part);
        CatalogIssue catalogIssue;
        const auto catalog=PartCatalog::create(draft,catalogIssue,[&](const CookedMeshVisual& visual){
            for(const auto& b:fixture->bundles)for(const auto& lod:b->lods())
                if(lod.asset==visual.asset)return true;
            return false;
        });
        if(!catalog)throw std::runtime_error(std::string(catalogIssue.field));
        const auto source=fixture->assembly->snapshot();
        if(source.parts.size()!=5 || source.connections.size()!=12)
            throw std::runtime_error("expected five bricks joined by twelve stud connections");
        for(uint8_t rotation=0;rotation<24;++rotation) {
            auto build=source;
            const GridTransform root{{-101,53,217},{rotation}};
            for(auto& part:build.parts)part.placement=*compose(root,part.placement);
            AssemblyFunctionIssue issue;
            const auto compiled=CompiledAssembly::compile(build,*catalog,issue);
            if(!compiled || compiled->mass().roots().size()!=1)
                throw std::runtime_error("stack failed at rotation "+std::to_string(rotation));
            if(std::abs(compiled->mass().roots()[0].mass.dryMassKg-120.0)>1e-9)
                throw std::runtime_error("stack mass changed");
            // A gap of one lattice tick must not count as a connected stack.
            build.parts.back().placement.translation.x+=1;
            BuildIssue bad;
            if(BuildModel::create(build,*catalog,bad) || bad.error!=BuildError::MisalignedWeld)
                throw std::runtime_error("misaligned stack was accepted");
        }
        auto overlap=source;
        const auto moved=overlap.parts[2].id;
        std::erase_if(overlap.connections,[&](const auto& weld){return weld.a.part==moved || weld.b.part==moved;});
        overlap.parts[2].placement=overlap.parts[1].placement;
        BuildIssue bad;
        if(BuildModel::create(overlap,*catalog,bad) || bad.error!=BuildError::SolidOverlap)
            throw std::runtime_error("overlapping unconnected bricks were accepted");
        std::cout<<"Brick stack: admitted 3 cooked definitions / 9 LODs; 24 orientations and 24 misalignment refusals; overlap refused; 120 kg conserved.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}

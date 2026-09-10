#include "game/expedition/cove_save.hpp"
#include "game/construction/assembly_compiler.hpp"
#include "core/sha256.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <new>
#include <type_traits>
#include <tuple>

namespace voxy::game::expedition {
namespace {
using namespace construction;
template<bool Read>struct Bytes {
    std::span<const std::byte> input{};
    std::vector<std::byte> output;
    size_t at=0;
    bool bad=false;
    template<class T>void scalar(T& value) {
        static_assert(std::is_unsigned_v<T>);
        if(bad)return;
        if constexpr(Read) {
            if(at>input.size() || sizeof(T)>input.size()-at){bad=true;return;}
            uint64_t bits=0;
            for(size_t i=0;i<sizeof(T);++i)bits|=uint64_t(std::to_integer<uint8_t>(input[at++]))<<(8*i);
            value=static_cast<T>(bits);
        } else {
            if(at>kMaximumCoveSaveBytes || sizeof(T)>kMaximumCoveSaveBytes-at){bad=true;return;}
            for(size_t i=0;i<sizeof(T);++i)output.push_back(static_cast<std::byte>((uint64_t(value)>>(8*i))&255));
            at+=sizeof(T);
        }
    }
    template<class T>void item(T& value) {
        if(bad)return;
        if constexpr(std::is_same_v<T,bool>) {
            uint8_t bits=value?1:0;scalar(bits);if(bits>1)bad=true;
            if constexpr(Read)value=bits==1;
        } else if constexpr(std::is_floating_point_v<T>) {
            using U=std::conditional_t<sizeof(T)==4,uint32_t,uint64_t>;
            U bits=std::bit_cast<U>(value==0?T{0}:value);scalar(bits);
            if constexpr(Read)value=std::bit_cast<T>(bits);
            if(!std::isfinite(value))bad=true;
        } else if constexpr(std::is_enum_v<T>) {
            uint8_t bits=static_cast<uint8_t>(value);scalar(bits);
            if constexpr(Read)value=static_cast<T>(bits);
        } else if constexpr(std::is_unsigned_v<T>)scalar(value);
        else if constexpr(requires{value.value();}) {
            uint64_t bits=value.value();scalar(bits);if constexpr(Read)value=T{bits};
        } else fields(*this,value);
    }
    template<class... T>void operator()(T&... values){(item(values),...);}
    template<class T,size_t N>void array(std::array<T,N>& value){for(auto& element:value)item(element);}
    void append(std::span<const std::byte> data) {
        if(bad || at>kMaximumCoveSaveBytes || data.size()>kMaximumCoveSaveBytes-at){bad=true;return;}
        output.insert(output.end(),data.begin(),data.end());at+=data.size();
    }
    std::span<const std::byte> take(size_t size) {
        if(bad || at>input.size() || size>input.size()-at){bad=true;return {};}
        const auto result=input.subspan(at,size);at+=size;return result;
    }
};
template<class A>void fields(A& a,WorldNamespace& v){a.array(v.bytes);}
template<class A>void fields(A& a,DurableId& v){a(v.world,v.counter);}
template<class A>void fields(A& a,ContentKey& v){a(v.id,v.version);}
template<class A>void fields(A& a,MetresPosition& v){a(v.x,v.y,v.z);}
template<class A>void fields(A& a,CanonicalQuaternion& v){a(v.x,v.y,v.z,v.w);}
template<class A>void fields(A& a,CoveSavedMotion& v){a(v.position,v.orientation);a.array(v.originVelocity);a.array(v.angularVelocity);}
template<class A>void fields(A& a,CoveSavedRoot& v){a(v.key,v.motion);}
template<class A>void fields(A& a,CoveSavedPlayer& v){a(v.feet,v.verticalSpeed,v.tick,v.interactions,v.mode,v.aboard,v.viewYaw,v.viewPitch);}
template<class A>void fields(A& a,CoveSavedWater& v){
    a(v.model,v.seconds,v.height,v.strength,v.significantWaveHeight,v.directionRadians,v.choppiness,
        v.peakEnhancement,v.windAlignment,v.animationSpeed);
    a.array(v.patchLengths);a.array(v.cascadeAmplitudes);a(v.directionalSineScale);
}
template<class A>void fields(A& a,CovePhysicalSave& v){
    a(v.tick,v.origin,v.boat,v.cargo,v.job,v.cargoDefinition,v.boatMotion,v.cargoMotion,
        v.player,v.water,v.cargoState,v.winchPart,v.ropeLength);
}
template<class T,class L,class H>bool bounded(T value,L lo,H hi){
    const double number=static_cast<double>(value);
    return std::isfinite(number)&&number>=static_cast<double>(lo)&&number<=static_cast<double>(hi);
}
bool position(MetresPosition value){return bounded(value.x,-1e6,1e6)&&bounded(value.y,-1e6,1e6)&&bounded(value.z,-1e6,1e6);}
bool motion(const CoveSavedMotion& v){
    return position(v.position)&&isCanonical(v.orientation)
        &&std::all_of(v.originVelocity.begin(),v.originVelocity.end(),[](float x){return bounded(x,-1e4,1e4);})
        &&std::all_of(v.angularVelocity.begin(),v.angularVelocity.end(),[](float x){return bounded(x,-1e4,1e4);});
}
bool water(const CoveSavedWater& v){
    constexpr float pi=std::numbers::pi_v<float>;
    return v.model==1&&bounded(v.seconds,0,1e12)&&bounded(v.height,-1e6,1e6)&&bounded(v.strength,0,2)
        &&bounded(v.significantWaveHeight,.1f,100)&&bounded(v.directionRadians,-pi,pi)&&bounded(v.choppiness,0,5)
        &&bounded(v.peakEnhancement,.05f,10)&&bounded(v.windAlignment,0,1)&&bounded(v.animationSpeed,0,5)
        &&bounded(v.patchLengths[0],64,8192)&&bounded(v.patchLengths[1],16,2048)
        &&bounded(v.cascadeAmplitudes[0],0,2)&&bounded(v.cascadeAmplitudes[1],0,2)&&bounded(v.directionalSineScale,0,1.5);
}
bool player(const CoveSavedPlayer& v){
    return position(v.feet)&&bounded(v.verticalSpeed,-30,6)&&v.mode<=CoveSavedPlayerMode::Helm
        &&bounded(v.viewYaw,-1e6,1e6)&&bounded(v.viewPitch,-std::numbers::pi/2,std::numbers::pi/2)
        &&(v.mode==CoveSavedPlayerMode::Airborne||v.verticalSpeed==0)
        &&(v.mode!=CoveSavedPlayerMode::Helm||v.aboard)
        &&(v.mode!=CoveSavedPlayerMode::Swimming||(!v.aboard&&std::abs(v.feet.y+.8)<=1e-6));
}
std::vector<std::byte> normalizedDesign(BuildBlueprint blueprint,const PartCatalog& catalog) {
    if(blueprint.parts.empty()||blueprint.parts.size()>32||blueprint.connections.size()>64)return {};
    for(const auto& c:blueprint.connections)if(c.kind!=ConnectionKind::Weld||!c.enabled)return {};
    const auto key=[](const DesignPart& p) {
        const auto& t=p.placement.translation;const auto& s=p.settings;
        return std::tuple{t.x,t.y,t.z,p.placement.rotation.value,p.definition,p.paint,
            s.kind,s.enabled,s.controlChannel,s.limitPermille,s.reversed,s.defaultLineLengthMillimetres};
    };
    std::sort(blueprint.parts.begin(),blueprint.parts.end(),[&](const auto& a,const auto& b){return key(a)<key(b);});
    std::array<uint32_t,33> ordinals{};
    for(size_t i=0;i<blueprint.parts.size();++i) {
        auto& p=blueprint.parts[i];if(!p.ordinal||p.ordinal>32||ordinals[p.ordinal])return {};
        ordinals[p.ordinal]=static_cast<uint32_t>(i+1);p.ordinal=static_cast<uint32_t>(i+1);
    }
    for(auto& c:blueprint.connections) {
        if(c.a.partOrdinal>32||c.b.partOrdinal>32||!ordinals[c.a.partOrdinal]||!ordinals[c.b.partOrdinal])return {};
        c.a.partOrdinal=ordinals[c.a.partOrdinal];c.b.partOrdinal=ordinals[c.b.partOrdinal];
        if(c.b<c.a)std::swap(c.a,c.b);
    }
    std::sort(blueprint.connections.begin(),blueprint.connections.end(),[](const auto& a,const auto& b){return std::pair{a.a,a.b}<std::pair{b.a,b.b};});
    std::vector<std::byte> bytes;
    if(encodeBlueprint(blueprint,catalog,bytes)||bytes.size()>kMaximumCoveRecoveryDesignBytes)return {};
    return bytes;
}
bool validRecoveryDesign(std::span<const std::byte> bytes,const PartCatalog& catalog) {
    if(bytes.empty()||bytes.size()>kMaximumCoveRecoveryDesignBytes)return false;
    std::optional<BuildBlueprint> blueprint;if(decodeBlueprint(bytes,catalog,blueprint))return false;
    const auto normalized=normalizedDesign(std::move(*blueprint),catalog);
    return normalized.size()==bytes.size()&&std::equal(normalized.begin(),normalized.end(),bytes.begin());
}
bool validRecoveryDesigns(const CoveRecoveryDesigns& designs,const PartCatalog& catalog) {
    if(designs.size()>kMaximumCoveRecoveryDesigns)return false;
    for(size_t i=0;i<designs.size();++i)
        if(!validRecoveryDesign(designs[i],catalog)||std::find(designs.begin(),designs.begin()+static_cast<std::ptrdiff_t>(i),designs[i])!=designs.begin()+static_cast<std::ptrdiff_t>(i))return false;
    return true;
}
CoveSaveError validRoots(const BuildSnapshot& build,const CovePhysicalSave& saved,const PartCatalog& catalog) {
    if(saved.boatRoots.size()>kMaximumCoveSavedRoots)return CoveSaveError::Capacity;
    // Derive membership from accepted parts and enabled welds. Trusting the
    // archive's count would allow a cut boat to silently lose a physical root.
    AssemblyIssue issue;
    const auto plan=AssemblyMassPlan::compile(build,catalog,issue,
        {.parts=32,.roots=kMaximumCoveSavedRoots});
    if(!plan)return issue.error==AssemblyError::Capacity?CoveSaveError::Capacity:CoveSaveError::PhysicalState;
    if(saved.boatRoots.empty()) {
        return plan->roots().size()==1 && saved.controlPart==DurableId{} && saved.playerRoot==DurableId{}
            ?CoveSaveError::None:CoveSaveError::PhysicalState;
    }
    if(saved.boatRoots.size()!=plan->roots().size())return CoveSaveError::PhysicalState;
    for(size_t i=0;i<saved.boatRoots.size();++i) {
        if(saved.boatRoots[i].key!=plan->roots()[i].key || !motion(saved.boatRoots[i].motion))
            return CoveSaveError::PhysicalState;
    }
    const auto part=std::find_if(build.parts.begin(),build.parts.end(),[&](const auto& p){return p.id==saved.controlPart;});
    if(part==build.parts.end())return CoveSaveError::PhysicalState;
    const auto definition=catalog.lookup(part->definition);
    if(!definition||!std::holds_alternative<HelmModule>(definition.definition->module))return CoveSaveError::PhysicalState;
    const auto binding=std::find_if(plan->parts().begin(),plan->parts().end(),[&](const auto& p){return p.part==saved.controlPart;});
    if(binding==plan->parts().end() || binding->root>=saved.boatRoots.size())return CoveSaveError::PhysicalState;
    const auto& control=saved.boatRoots[binding->root];
    if(saved.boatMotion!=control.motion)return CoveSaveError::PhysicalState;
    if(saved.player.aboard) {
        if(std::none_of(saved.boatRoots.begin(),saved.boatRoots.end(),[&](const auto& r){return r.key==saved.playerRoot;}))
            return CoveSaveError::PhysicalState;
        if(saved.player.mode==CoveSavedPlayerMode::Helm
            &&(saved.playerRoot!=control.key||!part->settings.enabled))return CoveSaveError::PhysicalState;
    } else if(saved.playerRoot!=DurableId{})return CoveSaveError::PhysicalState;
    // The current cove's four harbor slings suspend one rigid assembly. A
    // multi-root lift needs explicit per-line ownership before it is savable.
    if(saved.boatRoots.size()>1&&saved.harborLift.mode!=CoveHarborLiftMode::Detached)return CoveSaveError::PhysicalState;
    return CoveSaveError::None;
}
CoveSaveError validate(const LogicalRecoveryCheckpoint& current,const LogicalRecoveryCheckpoint* parent,
    const CovePhysicalSave& v,const CoveSaveContext& context,const PartCatalog& catalog){
    const auto& state=current.accepted;
    if(!validRecoveryDesigns(v.recoveryDesigns,catalog))return CoveSaveError::PhysicalState;
    const auto role=[&](DurableId id){return isValid(id)&&id.world==context.identity.world;};
    if(!isValid(context.identity.world)||!role(context.boat)||!role(context.cargo)||!role(context.job)
        ||context.boat==context.cargo||context.boat==context.job||context.cargo==context.job
        ||!position(context.origin)||!isValid(context.cargoDefinition.key.id)||!context.cargoDefinition.key.version
        ||state.world!=context.identity.world||current.content!=context.identity.content
        ||v.boat!=context.boat||v.cargo!=context.cargo||v.job!=context.job
        ||v.cargoDefinition!=context.cargoDefinition.key||v.origin!=context.origin)return CoveSaveError::Identity;
    if(current.pendingCount||state.builds.size()!=1||state.builds.front().id!=v.boat
        ||state.builds.front().parts.empty()||state.builds.front().parts.size()>32
        ||state.jobs.size()!=1||state.jobs.front().id!=v.job||state.cargoDefinitions.size()!=1
        ||state.cargoDefinitions.front()!=context.cargoDefinition||v.tick!=state.tick)return CoveSaveError::LogicalState;
    if(std::any_of(state.builds.front().connections.begin(),state.builds.front().connections.end(),
        [](const auto& c){return c.kind!=ConnectionKind::Weld;}))return CoveSaveError::PhysicalState;
    if(v.cargoState>CoveSavedCargoState::Banked||!motion(v.boatMotion)||!motion(v.cargoMotion)
        ||!player(v.player)||!water(v.water)||double(v.water.height)!=v.origin.y)return CoveSaveError::PhysicalState;
    if(const auto roots=validRoots(state.builds.front(),v,catalog);roots!=CoveSaveError::None)return roots;
    const bool banked=v.cargoState==CoveSavedCargoState::Banked;
    if(!validCoveHarborLiftState(v.harborLift))return CoveSaveError::PhysicalState;
    if(v.harborLift.mode!=CoveHarborLiftMode::Detached&&!banked)return CoveSaveError::LogicalState;
    if(banked!=(state.jobs.front().phase==JobPhase::Completed))return CoveSaveError::LogicalState;
    if(banked){
        if(!state.cargo.empty())return CoveSaveError::LogicalState;
        for(float velocity:v.cargoMotion.originVelocity)if(velocity!=0)return CoveSaveError::PhysicalState;
        for(float velocity:v.cargoMotion.angularVelocity)if(velocity!=0)return CoveSaveError::PhysicalState;
    } else if(state.cargo.size()!=1||state.cargo.front().id!=v.cargo
        ||state.cargo.front().definition!=v.cargoDefinition||state.cargo.front().job!=v.job
        ||state.cargo.front().owner!=state.caller.participant)return CoveSaveError::LogicalState;
    const bool rope=v.cargoState==CoveSavedCargoState::Towed||v.cargoState==CoveSavedCargoState::BrokenTow;
    if(rope){
        const auto& parts=state.builds.front().parts;
        const auto part=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return p.id==v.winchPart;});
        if(part==parts.end()||!part->settings.enabled)return CoveSaveError::PhysicalState;
        const auto found=catalog.lookup(part->definition);
        const auto* winch=found?std::get_if<WinchModule>(&found.definition->module):nullptr;
        if(!winch||!bounded(v.ropeLength,winch->minimumLengthMetres,winch->maximumLengthMetres))return CoveSaveError::PhysicalState;
    } else if(v.winchPart!=DurableId{}||v.ropeLength!=0)return CoveSaveError::PhysicalState;
    if(bool(current.origin)!=bool(parent))return CoveSaveError::Lineage;
    if(parent){
        const RecoveryOrigin origin{parent->journalIdentity,parent->coveredThrough,parent->accepted.caller,
            parent->admission.generation,parent->admission.processedThrough,parent->allocator,parent->history.generation};
        if(parent->admission.open||parent->pendingCount||parent->accepted.world!=state.world
            ||parent->content!=current.content||parent->accepted.tick>state.tick||*current.origin!=origin)
            return CoveSaveError::Lineage;
    }
    return CoveSaveError::None;
}
constexpr std::array<std::byte,4> magic{std::byte{'S'},std::byte{'V'},std::byte{'C'},std::byte{'E'}};
}

std::vector<std::byte> makeCoveRecoveryDesign(const construction::BuildSnapshot& build,const construction::PartCatalog& catalog) {
    construction::BuildIssue issue;const auto model=construction::BuildModel::create(build,catalog,issue);
    if(!model)return {};
    return normalizedDesign(construction::duplicateDesign(*model),catalog);
}
CoveRememberDesign rememberCoveRecoveryDesign(CoveRecoveryDesigns& designs,std::span<const std::byte> bytes,const construction::PartCatalog& catalog) {
    if(!validRecoveryDesigns(designs,catalog)||!validRecoveryDesign(bytes,catalog))return CoveRememberDesign::Invalid;
    for(const auto& existing:designs)if(existing.size()==bytes.size()&&std::equal(existing.begin(),existing.end(),bytes.begin()))return CoveRememberDesign::Known;
    if(designs.size()==kMaximumCoveRecoveryDesigns)return CoveRememberDesign::Full;
    std::vector<std::byte> owned(bytes.begin(),bytes.end());designs.push_back(std::move(owned));return CoveRememberDesign::Stored;
}

bool protectCoveRecoveryDesign(const construction::BuildSnapshot& build,const construction::PartCatalog& catalog,
    std::span<const std::byte> builtIn,CoveRecoveryDesigns& designs,size_t& selected,std::string& error) {
    auto intact=build;bool broken=false;
    for(auto& link:intact.connections)if(!link.enabled) {
        if(link.kind!=construction::ConnectionKind::Weld){error="This connection cannot be protected for recovery.";return false;}
        broken=true;link.enabled=true;
    }
    const auto bytes=makeCoveRecoveryDesign(intact,catalog);
    if(bytes.empty()||!validRecoveryDesigns(designs,catalog)) {error="Your intact design could not be protected.";return false;}
    const auto equals=[&](std::span<const std::byte> other){return bytes.size()==other.size()&&std::equal(bytes.begin(),bytes.end(),other.begin());};
    if(equals(builtIn)){error.clear();return true;}
    for(size_t i=0;i<designs.size();++i)if(equals(designs[i])){selected=i;error.clear();return true;}
    if(broken){error="The intact design is missing. Recover the sections before editing this build.";return false;}
    const auto kept=rememberCoveRecoveryDesign(designs,bytes,catalog);
    if(kept!=CoveRememberDesign::Stored){error="Recovery designs are full. Remove a saved recovery design first.";return false;}
    selected=designs.size()-1;error.clear();return true;
}

bool CoveSaveCodec::encode(const ValidatedRecoveryCheckpoint& current,const ValidatedRecoveryCheckpoint* parent,
    const CovePhysicalSave& physical,const CoveSaveContext& context,const PartCatalog& catalog,
    std::vector<std::byte>& output,CoveSaveIssue& issue){
    try {
        issue={validate(current.snapshot(),parent?&parent->snapshot():nullptr,physical,context,catalog),{}};
        if(issue)return false;
        std::vector<std::byte> currentBytes,parentBytes;
        if(!SessionSaveCodec::encodeCheckpoint(current,currentBytes,issue.session)
            ||(parent&&!SessionSaveCodec::encodeCheckpoint(*parent,parentBytes,issue.session))){issue.error=CoveSaveError::LogicalState;return false;}
        Bytes<false> bytes;bytes.output.reserve(currentBytes.size()+parentBytes.size()+1024);
        bytes.append(magic);auto version=!physical.boatRoots.empty()?kCoveSaveRootsSchema:
            !physical.recoveryDesigns.empty()?kCoveSaveRecoverySchema:physical.harborLift.profile==0?kCoveSaveSchema:kCoveSaveHarborSchema;
        bytes(version);auto state=physical;bytes(state);
        if(version>=kCoveSaveHarborSchema){
            bytes(state.harborLift.profile,state.harborLift.mode,state.harborLift.brokenMask);
            bytes.array(state.harborLift.lengths);
        }
        if(version>=kCoveSaveRecoverySchema) {
            auto count=static_cast<uint32_t>(physical.recoveryDesigns.size());bytes(count);
            for(const auto& design:physical.recoveryDesigns){auto size=static_cast<uint32_t>(design.size());bytes(size);bytes.append(design);}
        }
        if(version>=kCoveSaveRootsSchema) {
            bytes(state.controlPart,state.playerRoot);
            auto count=static_cast<uint32_t>(state.boatRoots.size());bytes(count);
            for(auto& root:state.boatRoots)bytes(root);
        }
        auto currentSize=static_cast<uint32_t>(currentBytes.size()),parentSize=static_cast<uint32_t>(parentBytes.size());
        bytes(currentSize);bytes.append(currentBytes);bytes(parentSize);bytes.append(parentBytes);
        const auto digest=core::sha256(bytes.output);bytes.append(digest.bytes);
        if(bytes.bad){issue.error=CoveSaveError::Capacity;return false;}
        output=std::move(bytes.output);issue={};return true;
    } catch(const std::bad_alloc&){issue={CoveSaveError::Capacity,{}};return false;}
}

std::unique_ptr<CoveSaveArchive> CoveSaveCodec::decode(std::span<const std::byte> input,
    const CoveSaveContext& context,const PartCatalog& catalog,CoveSaveIssue& issue){
    issue={};
    try {
        if(input.size()>kMaximumCoveSaveBytes){issue.error=CoveSaveError::Capacity;return {};}
        if(input.size()<40||!std::equal(magic.begin(),magic.end(),input.begin())){issue.error=CoveSaveError::Encoding;return {};}
        Bytes<true> bytes;bytes.input=input.first(input.size()-32);bytes.at=4;
        uint32_t version=0;bytes(version);
        if(version<kCoveSaveSchema||version>kCoveSaveRootsSchema){issue.error=CoveSaveError::UnsupportedSchema;return {};}
        const auto digest=core::sha256(bytes.input);
        if(!std::equal(digest.bytes.begin(),digest.bytes.end(),input.end()-32)){issue.error=CoveSaveError::Checksum;return {};}
        auto result=std::make_unique<CoveSaveArchive>();bytes(result->physical);
        if(version>=kCoveSaveHarborSchema){
            bytes(result->physical.harborLift.profile,result->physical.harborLift.mode,result->physical.harborLift.brokenMask);
            bytes.array(result->physical.harborLift.lengths);
            if(bytes.bad){issue.error=CoveSaveError::Encoding;return {};}
            if(result->physical.harborLift.profile>kCoveHarborLiftProfile||(version==kCoveSaveHarborSchema&&!result->physical.harborLift.profile)){issue.error=CoveSaveError::UnsupportedSchema;return {};}
        }
        if(version>=kCoveSaveRecoverySchema) {
            uint32_t count=0;bytes(count);
            if((version==kCoveSaveRecoverySchema&&!count)||count>kMaximumCoveRecoveryDesigns){issue.error=CoveSaveError::Capacity;return {};}
            for(uint32_t i=0;i<count;++i) {
                uint32_t size=0;bytes(size);
                if(!size||size>kMaximumCoveRecoveryDesignBytes){issue.error=CoveSaveError::Capacity;return {};}
                const auto data=bytes.take(size);if(bytes.bad){issue.error=CoveSaveError::Encoding;return {};}
                result->physical.recoveryDesigns.emplace_back(data.begin(),data.end());
            }
        }
        if(version>=kCoveSaveRootsSchema) {
            bytes(result->physical.controlPart,result->physical.playerRoot);
            uint32_t count=0;bytes(count);
            if(bytes.bad){issue.error=CoveSaveError::Encoding;return {};}
            if(!count||count>kMaximumCoveSavedRoots){issue.error=CoveSaveError::Capacity;return {};}
            // An entry is 24 bytes of stable ID plus 64 bytes of motion. Bound
            // the allocation by both the profile and remaining input bytes.
            if(size_t(count)>(bytes.input.size()-bytes.at)/88){issue.error=CoveSaveError::Encoding;return {};}
            result->physical.boatRoots.resize(count);
            for(auto& root:result->physical.boatRoots)bytes(root);
        }
        uint32_t currentSize=0;bytes(currentSize);
        if(currentSize>kMaximumSessionSaveBytes){issue.error=CoveSaveError::Capacity;return {};}
        const auto currentBytes=bytes.take(currentSize);
        uint32_t parentSize=0;bytes(parentSize);
        if(parentSize>kMaximumSessionSaveBytes){issue.error=CoveSaveError::Capacity;return {};}
        const auto parentBytes=bytes.take(parentSize);
        if(bytes.bad||!currentSize||bytes.at!=bytes.input.size()){issue.error=CoveSaveError::Encoding;return {};}
        result->current=SessionSaveCodec::decodeCheckpoint(currentBytes,context.identity,catalog,issue.session);
        if(!result->current){issue.error=CoveSaveError::LogicalState;return {};}
        if(parentSize){
            result->retiredParent=SessionSaveCodec::decodeCheckpoint(parentBytes,context.identity,catalog,issue.session);
            if(!result->retiredParent){issue.error=CoveSaveError::Lineage;return {};}
        }
        issue.error=validate(result->current->snapshot(),result->retiredParent?&result->retiredParent->snapshot():nullptr,
            result->physical,context,catalog);
        if(issue)return {};
        std::vector<std::byte> canonical;
        if(!encode(*result->current,result->retiredParent.get(),result->physical,context,catalog,canonical,issue))return {};
        if(canonical.size()!=input.size()||!std::equal(canonical.begin(),canonical.end(),input.begin())){
            issue.error=CoveSaveError::NonCanonical;return {};
        }
        issue={};return result;
    } catch(const std::bad_alloc&){issue={CoveSaveError::Capacity,{}};return {};}
}
} // namespace voxy::game::expedition

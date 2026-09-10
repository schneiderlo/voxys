#include "game/expedition/session_save.hpp"
#include "core/sha256.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>

namespace voxy::game::expedition {
namespace {
using namespace construction;
template<class T>struct IsCounter:std::false_type{};
template<class Tag,bool Zero>struct IsCounter<Counter<Tag,Zero>>:std::true_type{};
template<class T>struct IsOptional:std::false_type{};
template<class T>struct IsOptional<std::optional<T>>:std::true_type{};
template<class T>struct IsArray:std::false_type{};
template<class T,size_t N>struct IsArray<std::array<T,N>>:std::true_type{};
template<class T>constexpr uint8_t lastEnum();
#define SAVE_ENUM(T,Last) template<>constexpr uint8_t lastEnum<T>(){return static_cast<uint8_t>(T::Last);}
SAVE_ENUM(PartOrigin,StarterLoan)
SAVE_ENUM(ConnectionKind,Latch)
SAVE_ENUM(BuildError,NonCanonicalOrder)
SAVE_ENUM(CargoRecoveryRule,PreserveUnique)
SAVE_ENUM(JobPhase,Completed)
SAVE_ENUM(SessionError,DeliveryNotReady)
SAVE_ENUM(ReceiptState,Committed)
SAVE_ENUM(ReceiptDurability,Volatile)
SAVE_ENUM(HistoryAction,Service)
SAVE_ENUM(RecoveryLeasePolicy,ReleaseLocalParticipantLeases)
SAVE_ENUM(RecoveryPendingState,JournalAdmitted)
#undef SAVE_ENUM

// Field widths below are protocol choices. size_t only goes through size() or
// bounded counts, never sizeof(size_t). Padding, pointers and unused array tails
// are absent. A reader charges retained object storage before growing vectors.
template<bool Read>class Archive {
public:
    static constexpr bool reading=Read;
    std::span<const std::byte> input{};
    std::vector<std::byte> output{};
    size_t position=0,maximum=0,owned=0;
    uint32_t version=1;
    SaveCodecIssue issue{};
    Archive(std::span<const std::byte> bytes,size_t limit,size_t initialOwned=0):input(bytes),maximum(limit),owned(initialOwned){}
    void fail(SaveCodecError error){if(!issue)issue={error,position,{}};}
    bool charge(size_t count,size_t size) {
        if(issue)return false;
        if(owned>kMaximumLogicalRecoveryBytes || (size&&count>(kMaximumLogicalRecoveryBytes-owned)/size)) {fail(SaveCodecError::Capacity);return false;}
        owned+=count*size;return true;
    }
    void integer(uint64_t& bits,size_t width) {
        if(issue)return;
        if constexpr(Read) {
            if(position>input.size() || width>input.size()-position){fail(SaveCodecError::InvalidEncoding);return;}
            bits=0;for(size_t i=0;i<width;++i)bits|=uint64_t(std::to_integer<uint8_t>(input[position++]))<<(i*8);
        } else {
            if(position>maximum || width>maximum-position){fail(SaveCodecError::Capacity);return;}
            for(size_t i=0;i<width;++i)output.push_back(static_cast<std::byte>((bits>>(i*8))&255));
            position+=width;
        }
    }
    template<class T>void item(T& value) {
        if(issue)return;
        using V=std::remove_cv_t<T>;
        if constexpr(std::is_same_v<V,bool>) {
            uint64_t bits=value?1:0;integer(bits,1);if(bits>1)fail(SaveCodecError::InvalidEncoding);
            if constexpr(Read)value=bits==1;
        } else if constexpr(std::is_same_v<V,std::byte>) {
            uint64_t bits=std::to_integer<uint8_t>(value);integer(bits,1);if constexpr(Read)value=static_cast<std::byte>(bits);
        } else if constexpr(std::is_integral_v<V>) {
            using U=std::make_unsigned_t<V>;uint64_t bits=std::bit_cast<U>(value);integer(bits,sizeof(V));
            if constexpr(Read)value=std::bit_cast<V>(static_cast<U>(bits));
        } else if constexpr(std::is_floating_point_v<V>) {
            using U=std::conditional_t<sizeof(V)==4,uint32_t,uint64_t>;
            uint64_t bits=std::bit_cast<U>(value==0?V{0}:value);integer(bits,sizeof(V));
            if constexpr(Read)value=std::bit_cast<V>(static_cast<U>(bits));
            if(!std::isfinite(value))fail(SaveCodecError::InvalidEncoding);
        } else if constexpr(std::is_enum_v<V>) {
            uint64_t bits=static_cast<uint8_t>(value);integer(bits,1);if(bits>lastEnum<V>())fail(SaveCodecError::InvalidEncoding);
            if constexpr(Read)value=static_cast<V>(bits);
        } else if constexpr(IsCounter<V>::value) {
            uint64_t bits=value.value();integer(bits,8);if constexpr(Read)value=V{bits};
        } else if constexpr(IsOptional<V>::value) {
            bool present=value.has_value();item(present);
            if constexpr(Read){if(present&&!issue)value.emplace();else value.reset();}
            if(present&&!issue)item(*value);
        } else if constexpr(IsArray<V>::value) {
            for(auto& element:value)item(element);
        } else schema(*this,value);
    }
    template<class... T>void operator()(T&... values){(item(values),...);}
    template<class T>void size(T& value) {
        uint64_t bits=static_cast<uint64_t>(value);integer(bits,8);
        if constexpr(Read){if(bits>std::numeric_limits<std::remove_cv_t<T>>::max())fail(SaveCodecError::Capacity);else value=static_cast<T>(bits);}
    }
    template<class T>uint32_t count(T& value,size_t limit) {
        if constexpr(!Read){if(value>limit){fail(SaveCodecError::Capacity);return 0;}}
        uint32_t number=static_cast<uint32_t>(value);item(number);
        if(number>limit){fail(SaveCodecError::Capacity);return 0;}
        if constexpr(Read)value=static_cast<T>(number);
        return number;
    }
    template<class V>void vector(V& values,size_t limit) {
        size_t sizeValue=values.size();const auto number=count(sizeValue,limit);if(issue)return;
        if constexpr(Read){if(!charge(number,sizeof(typename V::value_type)))return;values.resize(number);}
        for(auto& element:values)item(element);
    }
    template<class V,class C>void prefix(V& values,C& used) {
        const auto number=count(used,values.size());if(issue)return;
        for(uint32_t i=0;i<number;++i)item(values[i]);
    }
    template<size_t I=0,class V>void readAlternative(V& value,uint8_t tag) {
        if constexpr(I<std::variant_size_v<V>) {
            if(tag==I){value.template emplace<I>();item(std::get<I>(value));}
            else readAlternative<I+1>(value,tag);
        }else fail(SaveCodecError::InvalidEncoding);
    }
    template<class... Alternatives,class V>void variant(V& value) {
        static_assert(std::is_same_v<std::remove_cv_t<V>,std::variant<Alternatives...>>,"Update the frozen SAVE variant table explicitly");
        uint8_t tag=static_cast<uint8_t>(value.index());item(tag);if(issue)return;
        if constexpr(Read)readAlternative(value,tag);
        else std::visit([&](const auto& alternative){item(alternative);},value);
    }
};

// This is the frozen field-order table for schema 1. Const writing and mutable
// reading use the same explicit fields; no member is discovered by reflection.
#define RECORD(Type,...) template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,Type> void schema(A& a,V& v){a(__VA_ARGS__);}
RECORD(WorldNamespace,v.bytes)
RECORD(DurableId,v.world,v.counter)
RECORD(ContentKey,v.id,v.version)
RECORD(GridPosition,v.x,v.y,v.z)
RECORD(MetresPosition,v.x,v.y,v.z)
RECORD(CubeRotation,v.value)
RECORD(GridTransform,v.translation,v.rotation)
RECORD(CanonicalQuaternion,v.x,v.y,v.z,v.w)
RECORD(ResourceAmounts,v.salvageMaterial,v.specialMachinery)
RECORD(StrengthLimits,v.tensionNewtons,v.shearNewtons,v.bendingNewtonMetres,v.torsionNewtonMetres)
// Rejected refit requests retain their exact malformed settings for retry
// identity. Preserve the raw kind byte; recovery validates it in context.
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,ModuleSettings> void schema(A& a,V& v){
    uint8_t kind=static_cast<uint8_t>(v.kind);a(kind);if constexpr(A::reading)v.kind=static_cast<SettingsKind>(kind);
    a(v.enabled,v.controlChannel,v.limitPermille,v.reversed,v.defaultLineLengthMillimetres);
}
RECORD(PartProvenance,v.origin,v.starterEntitlement)
RECORD(PartInstance,v.id,v.definition,v.placement,v.owningBuild,v.health,v.paint,v.settings,v.provenance)
RECORD(SocketEndpoint,v.part,v.socket)
RECORD(Connection,v.id,v.a,v.b,v.kind,v.enabled,v.damage,v.strength,v.minimumLengthMillimetres,v.maximumLengthMillimetres,v.restLengthMillimetres)
RECORD(EditLease,v.holder,v.epoch,v.expiresAfter)
RECORD(DesignPart,v.ordinal,v.definition,v.placement,v.paint,v.settings)
RECORD(DesignEndpoint,v.partOrdinal,v.socket)
RECORD(RefitPart,v.source,v.design)
RECORD(RefitWeld,v.a,v.b)
RECORD(RefitPartChange,v.before,v.after)
RECORD(RefitWeldChange,v.before,v.after)
RECORD(CallerContext,v.participant,v.sessionToken)
RECORD(CargoDefinition,v.key,v.massKg,v.displacedVolumeCubicMetres,v.value,v.recovery)
RECORD(CargoRecord,v.id,v.definition,v.owner,v.position,v.orientation,v.job)
RECORD(JobRecord,v.id,v.generation,v.phase,v.acceptedBy)
RECORD(BuildTarget,v.build,v.expectedRevision)
RECORD(AddPart,v.target,v.definition,v.placement)
RECORD(MovePart,v.target,v.part,v.placement)
RECORD(RemovePart,v.target,v.part)
RECORD(AcceptJob,v.job)
RECORD(Undo,v.target,v.entry,v.expectedHistoryGeneration)
RECORD(Redo,v.target,v.entry,v.expectedHistoryGeneration)
RECORD(DeliverCargo,v.cargo)
RECORD(RefitBuild,v.target,v.design)
RECORD(RebuildStarter,v.target)
RECORD(CutWeld,v.target,v.connection)
RECORD(StarterKitRegisteredRecord,v.caller,v.admission,v.beforeRevision,v.afterRevision,v.kit,v.balance,v.admittedThrough,v.processedThrough,v.allocator)
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,StarterKit> void schema(A& a,V& v){
    a(v.build,v.entitlement,v.design);a.prefix(v.partIds,v.partCount);
}
RECORD(Command,v.epoch,v.sequence,v.expectedRevision,v.intent)
RECORD(RequestKey,v.caller,v.epoch,v.admissionGeneration,v.sequence)
RECORD(AdmissionState,v.generation,v.retiredThrough,v.admittedThrough,v.processedThrough,v.open)
RECORD(AllocatorMarkers,v.issuedThrough,v.reservedThrough)
RECORD(BuildHeader,v.id,v.revision,v.owner,v.editLease,v.materialized)
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,BuildTransition> void schema(A& a,V& v){
    a(v.before,v.after,v.beforePart,v.afterPart,v.refit,v.refitForward);if(a.version>=2)a(v.storage);if(a.version>=3)a(v.cut);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,std::shared_ptr<const WeldCutTransition>> void schema(A& a,V& v){
    bool present=bool(v);a(present);if(a.issue)return;
    if(!present){if constexpr(A::reading)v.reset();return;}
    if constexpr(A::reading) {
        if(!a.charge(1,sizeof(WeldCutTransition)))return;
        auto value=std::make_shared<WeldCutTransition>();a(value->before);if(!a.issue)v=std::move(value);
    }else a(v->before);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,std::shared_ptr<const PartStorageTransition>> void schema(A& a,V& v){
    bool present=bool(v);a(present);if(a.issue)return;
    if(!present){if constexpr(A::reading)v.reset();return;}
    if constexpr(A::reading) {
        if(!a.charge(1,sizeof(PartStorageTransition)))return;
        auto value=std::make_shared<PartStorageTransition>();
        a.vector(value->deposited,32);a.vector(value->withdrawn,32);a(value->starterBefore);
        if(a.issue)return;
        if(value->retainedBytes()>kMaximumStorageTransitionBytes){a.fail(SaveCodecError::Capacity);return;}
        v=std::move(value);
    }else {a.vector(v->deposited,32);a.vector(v->withdrawn,32);a(v->starterBefore);}
}
RECORD(JobTransition,v.before,v.after)
RECORD(CargoDeliveryTransition,v.cargo,v.before,v.after,v.reward)
RECORD(DecisionOutcome,v.state,v.error,v.buildError,v.issueObject,v.revision,v.buildRevision,v.object)
RECORD(AssignedIdRange,v.first,v.count)
RECORD(RequestAdmissionRecord,v.key,v.command,v.admittedBefore,v.processedThrough,v.assignedObject,v.allocator,v.assignedRange)
RECORD(RequestDecisionRecord,v.key,v.command,v.beforeRevision,v.afterRevision,v.processedBefore,v.balanceBefore,v.balanceAfter,v.objects,v.history,v.allocator,v.outcome)
RECORD(AllocatorLeaseRecord,v.issuedThrough,v.reservedBefore,v.reservedAfter)
RECORD(JournalIdentity,v.world,v.epoch,v.writerGeneration)
RECORD(AdmissionOpenedRecord,v.caller,v.generation,v.retiredThrough,v.allocator)
RECORD(AdmissionClosedRecord,v.caller,v.generation,v.processedThrough)
RECORD(RecoveryOrigin,v.parent,v.closedThrough,v.caller,v.generation,v.processedThrough,v.allocator,v.historyGeneration)
RECORD(RecoveryOpenedRecord,v.origin,v.admission,v.revision,v.balance,v.leases)
RECORD(EntitlementRetiredRecord,v.caller,v.admission,v.entitlement,v.beforeRevision,v.afterRevision,v.balance,v.admittedThrough,v.processedThrough,v.allocator,v.history)
RECORD(JournalRecord,v.schema,v.world,v.epoch,v.sequence,v.tick,v.payload)
RECORD(RecoveryContentIdentity,v.manifest,v.manifestDigest,v.profile,v.profileVersion)
RECORD(RecoveryHistoryEntry,v.id,v.caller,v.admission,v.edit,v.debit,v.credit)
RECORD(RecoveryPending,v.command,v.state,v.assignedObject,v.buildError,v.error,v.issueObject,v.admissionJournal,v.observedExpectedSequence,v.assignedRange)
RECORD(RecoveryReceipt,v.command,v.outcome,v.observedExpectedSequence,v.decisionJournal,v.durability)
#undef RECORD
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,CreateBuild> void schema(A&,V&){}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,std::monostate> void schema(A&,V&){}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,Intent> void schema(A& a,V& v){
    a.template variant<CreateBuild,AddPart,MovePart,RemovePart,AcceptJob,Undo,Redo,DeliverCargo,RefitBuild,RebuildStarter,CutWeld>(v);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,ObjectTransition> void schema(A& a,V& v){
    a.template variant<std::monostate,BuildTransition,JobTransition,CargoDeliveryTransition>(v);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,JournalPayload> void schema(A& a,V& v){
    a.template variant<RequestAdmissionRecord,RequestDecisionRecord,AllocatorLeaseRecord,AdmissionOpenedRecord,AdmissionClosedRecord,RecoveryOpenedRecord,EntitlementRetiredRecord,StarterKitRegisteredRecord>(v);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,BuildSnapshot> void schema(A& a,V& v){
    a(v.id,v.revision,v.owner,v.editLease);a.vector(v.parts,kMaximumBuildParts);a.vector(v.connections,kMaximumBuildConnections);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,SessionLimits> void schema(A& a,V& v){
    a(v.builds,v.totalParts,v.cargo,v.jobs,v.receipts,v.pendingCommands,v.journalRecords);a.size(v.journalBytes);a.size(v.candidateBytes);
    a(v.historyEntries);a.size(v.historyBytes);a(v.dormantParts,v.dormantBuilds);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,SessionBootstrap> void schema(A& a,V& v){
    a(v.world,v.caller,v.epoch,v.lastIssuedId,v.revision,v.tick,v.inventory,v.limits,v.workshopEnabled);
    a.vector(v.builds,32);a.vector(v.starterEntitlements,256);a.vector(v.cargoDefinitions,256);a.vector(v.cargo,256);a.vector(v.jobs,128);
    if(a.version>=2){a.vector(v.starterKits,kMaximumStarterKits);a.vector(v.storedParts,kMaximumStoredParts);}
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,HistoryTransition> void schema(A& a,V& v){
    a(v.before,v.after,v.action,v.entry);a.prefix(v.evicted,v.evictedCount);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,EntitlementHistoryDelta> void schema(A& a,V& v){
    a(v.before,v.after);a.prefix(v.invalidatedEntries,v.entryCount);a.prefix(v.retiredParts,v.partCount);a.prefix(v.retiredBuilds,v.buildCount);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,RecoveryHistory> void schema(A& a,V& v){
    a(v.generation);a.count(v.applied,32);a.prefix(v.entries,v.count);a.prefix(v.parts,v.partCount);a.prefix(v.builds,v.buildCount);
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,LogicalRecoveryCheckpoint> void schema(A& a,V& v){
    a(v.schema,v.content,v.accepted,v.admission,v.allocator,v.journalIdentity,v.origin,v.coveredThrough,v.modelReleasedThrough,
        v.historyClearedOnRestore,v.durability,v.history);
    a.prefix(v.pending,v.pendingCount);a.prefix(v.receipts,v.receiptCount);a.prefix(v.retainedJournal,v.retainedJournalCount);
    // ownedBytes is host ABI-dependent accounting, recomputed after decoding.
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,std::shared_ptr<const BuildRefitRequest>> void schema(A& a,V& v){
    bool present=bool(v);a(present);if(a.issue)return;
    if(!present){if constexpr(A::reading)v.reset();return;}
    if constexpr(A::reading) {
        std::vector<RefitPart> parts;std::vector<RefitWeld> welds;a.vector(parts,kMaximumBuildParts);a.vector(welds,kMaximumBuildConnections);
        if(a.issue || !a.charge(1,sizeof(BuildRefitRequest)))return;
        BuildIssue issue;v=BuildRefitRequest::create(parts,welds,issue);
        if(!v){a.fail(SaveCodecError::InvalidEncoding);return;}
        const auto counted=sizeof(BuildRefitRequest)+parts.size()*sizeof(RefitPart)+welds.size()*sizeof(RefitWeld);
        if(v->retainedBytes()>counted)(void)a.charge(1,v->retainedBytes()-counted);
    }else {auto parts=v->parts();auto welds=v->welds();a.vector(parts,kMaximumBuildParts);a.vector(welds,kMaximumBuildConnections);}
}
template<class A,class V>requires std::is_same_v<std::remove_cv_t<V>,std::shared_ptr<const BuildRefitDelta>> void schema(A& a,V& v){
    bool present=bool(v);a(present);if(a.issue)return;
    if(!present){if constexpr(A::reading)v.reset();return;}
    if constexpr(A::reading) {
        DurableId build;std::vector<RefitPartChange> parts;std::vector<RefitWeldChange> welds;
        a(build);a.vector(parts,kMaximumBuildParts);a.vector(welds,kMaximumBuildConnections);
        if(a.issue || !a.charge(1,sizeof(BuildRefitDelta)))return;
        BuildSnapshot before,after;before.id=after.id=build;
        for(const auto& change:parts){if(change.before)before.parts.push_back(*change.before);if(change.after)after.parts.push_back(*change.after);}
        for(const auto& change:welds){if(change.before)before.connections.push_back(*change.before);if(change.after)after.connections.push_back(*change.after);}
        BuildIssue issue;v=BuildRefitDelta::between(before,after,issue);
        if(!v || !std::equal(parts.begin(),parts.end(),v->parts().begin(),v->parts().end())
            ||!std::equal(welds.begin(),welds.end(),v->welds().begin(),v->welds().end())){a.fail(SaveCodecError::InvalidEncoding);return;}
        const auto counted=sizeof(BuildRefitDelta)+parts.size()*sizeof(RefitPartChange)+welds.size()*sizeof(RefitWeldChange);
        if(v->retainedBytes()>counted)(void)a.charge(1,v->retainedBytes()-counted);
    }else {const auto build=v->build();a(build);auto parts=v->parts();auto welds=v->welds();a.vector(parts,kMaximumBuildParts);a.vector(welds,kMaximumBuildConnections);}
}
using Reader=Archive<true>;using Writer=Archive<false>;

bool envelope(std::span<const std::byte> bytes,std::array<uint8_t,4> magic,size_t maximum,SaveCodecIssue& issue){
    if(bytes.size()>maximum){issue={SaveCodecError::Capacity};return false;}
    if(bytes.size()<40){issue={SaveCodecError::InvalidEncoding};return false;}
    if(!std::equal(magic.begin(),magic.end(),bytes.begin(),[](auto a,auto b){return a==std::to_integer<uint8_t>(b);})){
        issue={SaveCodecError::InvalidEncoding};return false;
    }
    uint32_t schemaVersion=0;for(size_t i=0;i<4;++i)schemaVersion|=uint32_t(std::to_integer<uint8_t>(bytes[4+i]))<<(i*8);
    if(schemaVersion<1||schemaVersion>kSessionSaveSchema){issue={SaveCodecError::UnsupportedSchema,4};return false;}
    const auto hash=core::sha256(bytes.first(bytes.size()-32));
    if(!std::equal(hash.bytes.begin(),hash.bytes.end(),bytes.end()-32)){issue={SaveCodecError::Checksum,bytes.size()-32};return false;}
    return true;
}
bool requiresV2(const JournalRecord& record){
    return std::visit([](const auto& value){
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,StarterKitRegisteredRecord>)return true;
        else if constexpr(std::is_same_v<T,RequestAdmissionRecord>)return std::holds_alternative<RebuildStarter>(value.command.intent);
        else if constexpr(std::is_same_v<T,RequestDecisionRecord>) {
            const auto* edit=std::get_if<BuildTransition>(&value.objects);
            return std::holds_alternative<RebuildStarter>(value.command.intent)||(edit&&edit->storage);
        }else return false;
    },record.payload);
}
bool requiresV3(const JournalRecord& record){
    return std::visit([](const auto& value){
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,RequestAdmissionRecord>)return std::holds_alternative<CutWeld>(value.command.intent);
        else if constexpr(std::is_same_v<T,RequestDecisionRecord>) {
            const auto* edit=std::get_if<BuildTransition>(&value.objects);
            return std::holds_alternative<CutWeld>(value.command.intent)||(edit&&edit->cut);
        }else return false;
    },record.payload);
}
void header(Writer& writer,std::array<uint8_t,4> magic){writer(magic);writer(writer.version);}
bool finish(Writer& writer,std::vector<std::byte>& output,SaveCodecIssue& issue){
    if(writer.issue){issue=writer.issue;return false;}
    const auto hash=core::sha256(writer.output);writer.output.insert(writer.output.end(),hash.bytes.begin(),hash.bytes.end());
    output=std::move(writer.output);issue={};return true;
}
void measure(LogicalRecoveryCheckpoint& image){
    const auto& boot=image.accepted;
    image.ownedBytes=sizeof(LogicalRecoveryCheckpoint)+ownedExtraBytes(image)+boot.builds.size()*sizeof(BuildSnapshot)
        +boot.starterEntitlements.size()*sizeof(DurableId)+boot.cargoDefinitions.size()*sizeof(CargoDefinition)
        +boot.cargo.size()*sizeof(CargoRecord)+boot.jobs.size()*sizeof(JobRecord)
        +boot.starterKits.size()*sizeof(StarterKit)+boot.storedParts.size()*sizeof(PartInstance);
    for(const auto& build:boot.builds)image.ownedBytes+=build.parts.size()*sizeof(PartInstance)+build.connections.size()*sizeof(Connection);
}
bool validBatch(JournalIdentity identity,std::span<const JournalRecord> records){
    if(!isValid(identity.world)||!identity.epoch.valid()||!identity.writerGeneration.valid()||records.size()>kMaximumJournalRecords)return false;
    for(size_t i=0;i<records.size();++i){
        const auto& record=records[i];
        if(record.world!=identity.world||record.epoch!=identity.epoch||!validAssignedJournalRecord(record))return false;
        if(i&&(records[i-1].sequence.value()==std::numeric_limits<uint64_t>::max()||record.sequence.value()!=records[i-1].sequence.value()+1
            ||record.tick<records[i-1].tick))return false;
    }
    return true;
}
} // namespace

bool SessionSaveCodec::encodeCheckpoint(const ValidatedRecoveryCheckpoint& value,std::vector<std::byte>& output,SaveCodecIssue& issue){
    try{Writer writer({},kMaximumSessionSaveBytes-32);
        const auto& state=value.snapshot();
        writer.version=state.accepted.starterKits.empty()&&state.accepted.storedParts.empty()
            &&std::none_of(state.receipts.begin(),state.receipts.begin()+static_cast<std::ptrdiff_t>(state.receiptCount),
                [](const auto& r){return std::holds_alternative<RebuildStarter>(r.command.intent);})
            &&std::none_of(state.pending.begin(),state.pending.begin()+static_cast<std::ptrdiff_t>(state.pendingCount),
                [](const auto& r){return std::holds_alternative<RebuildStarter>(r.command.intent);})
            &&std::none_of(state.retainedJournal.begin(),state.retainedJournal.begin()+static_cast<std::ptrdiff_t>(state.retainedJournalCount),requiresV2)?1:2;
        if(std::any_of(state.receipts.begin(),state.receipts.begin()+static_cast<std::ptrdiff_t>(state.receiptCount),
                [](const auto& r){return std::holds_alternative<CutWeld>(r.command.intent);})
            ||std::any_of(state.pending.begin(),state.pending.begin()+static_cast<std::ptrdiff_t>(state.pendingCount),
                [](const auto& r){return std::holds_alternative<CutWeld>(r.command.intent);})
            ||std::any_of(state.retainedJournal.begin(),state.retainedJournal.begin()+static_cast<std::ptrdiff_t>(state.retainedJournalCount),requiresV3))writer.version=3;
        header(writer,{'S','V','S','C'});writer(value.snapshot());return finish(writer,output,issue);}
    catch(const std::bad_alloc&){issue={SaveCodecError::Capacity};return false;}
}
std::unique_ptr<ValidatedRecoveryCheckpoint> SessionSaveCodec::decodeCheckpoint(
    std::span<const std::byte> bytes,ExpectedRecoveryIdentity expected,const PartCatalog& catalog,SaveCodecIssue& issue){
    issue={};if(!envelope(bytes,{'S','V','S','C'},kMaximumSessionSaveBytes,issue))return {};
    try{
        auto image=std::make_unique<LogicalRecoveryCheckpoint>();Reader reader(bytes.subspan(8,bytes.size()-40),kMaximumSessionSaveBytes,sizeof(LogicalRecoveryCheckpoint));
        reader.version=std::to_integer<uint8_t>(bytes[4]);
        reader(*image);if(reader.issue){issue=reader.issue;issue.offset+=8;return {};}
        if(reader.position!=reader.input.size()){issue={SaveCodecError::InvalidEncoding,reader.position+8};return {};}
        measure(*image);RecoveryIssue recovery;auto validated=SessionRecovery::admit(*image,expected,catalog,recovery);
        if(!validated){issue={SaveCodecError::InvalidState,0,recovery};return {};}
        std::vector<std::byte> canonical;if(!encodeCheckpoint(*validated,canonical,issue))return {};
        if(!std::equal(bytes.begin(),bytes.end(),canonical.begin(),canonical.end())){issue={SaveCodecError::NonCanonical};return {};}
        return validated;
    }catch(const std::bad_alloc&){issue={SaveCodecError::Capacity};return {};}
}
bool SessionSaveCodec::encodeJournal(JournalIdentity identity,std::span<const JournalRecord> records,std::vector<std::byte>& output,SaveCodecIssue& issue){
    if(!validBatch(identity,records)){issue={SaveCodecError::InvalidJournal};return false;}
    try{Writer writer({},kMaximumJournalSaveBytes-32);writer.version=std::any_of(records.begin(),records.end(),requiresV3)?3:std::any_of(records.begin(),records.end(),requiresV2)?2:1;header(writer,{'S','V','J','B'});writer(identity);writer.vector(records,kMaximumJournalRecords);return finish(writer,output,issue);}
    catch(const std::bad_alloc&){issue={SaveCodecError::Capacity};return false;}
}
bool SessionSaveCodec::decodeJournal(std::span<const std::byte> bytes,JournalIdentity expected,std::vector<JournalRecord>& output,SaveCodecIssue& issue){
    issue={};if(!envelope(bytes,{'S','V','J','B'},kMaximumJournalSaveBytes,issue))return false;
    try{
        Reader reader(bytes.subspan(8,bytes.size()-40),kMaximumJournalSaveBytes);JournalIdentity identity;std::vector<JournalRecord> records;
        reader.version=std::to_integer<uint8_t>(bytes[4]);reader(identity);reader.vector(records,kMaximumJournalRecords);
        if(reader.issue){issue=reader.issue;issue.offset+=8;return false;}
        if(reader.position!=reader.input.size()){issue={SaveCodecError::InvalidEncoding,reader.position+8};return false;}
        if(identity!=expected||!validBatch(identity,records)){issue={SaveCodecError::InvalidJournal};return false;}
        std::vector<std::byte> canonical;if(!encodeJournal(identity,records,canonical,issue))return false;
        if(!std::equal(bytes.begin(),bytes.end(),canonical.begin(),canonical.end())){issue={SaveCodecError::NonCanonical};return false;}
        output=std::move(records);return true;
    }catch(const std::bad_alloc&){issue={SaveCodecError::Capacity};return false;}
}
} // namespace voxy::game::expedition

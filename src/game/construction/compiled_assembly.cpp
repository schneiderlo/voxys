#include "game/construction/compiled_assembly.hpp"

#include <algorithm>
#include <bit>

namespace voxy::game::construction {
namespace {
// Grammar: docs/validation/salvage/SIM-01/cache-wire-v1.md. Every write has an
// explicit width/order; no uninitialized padding, native enum layout or pointer.
class PhysicalHash {
public:
    core::Sha256 hash;
    void u8(uint8_t value) noexcept { const auto byte = std::byte{value}; hash.update(std::span{&byte, size_t{1}}); }
    void u32(uint32_t value) noexcept { hash.u32Little(value); }
    void u64(uint64_t value) noexcept { hash.u64Little(value); }
    void count(size_t value) noexcept { u32(static_cast<uint32_t>(value)); } // Validated maxima fit u32 on both platforms.
    void boolean(bool value) noexcept { u8(value ? 1 : 0); }
    void real(double value) noexcept { u64(std::bit_cast<uint64_t>(value == 0.0 ? 0.0 : value)); }
    void id(DurableId value) noexcept { for (auto byte : value.world.bytes) u8(byte); u64(value.counter); }
    void key(ContentKey value) noexcept { id(value.id); u32(value.version); }
    void point(GridPosition value) noexcept {
        u32(std::bit_cast<uint32_t>(value.x)); u32(std::bit_cast<uint32_t>(value.y)); u32(std::bit_cast<uint32_t>(value.z));
    }
    void frame(GridTransform value) noexcept { point(value.translation); u8(value.rotation.value); }
    void bounds(GridBox value) noexcept { point(value.minimum); point(value.maximum); }
    void box(const PartBox& value) noexcept { u64(value.id.value()); frame(value.frame); point(value.halfExtents); }
    void strength(StrengthLimits value) noexcept {
        real(value.tensionNewtons); real(value.shearNewtons); real(value.bendingNewtonMetres); real(value.torsionNewtonMetres);
    }
    void settings(ModuleSettings value) noexcept {
        u8(static_cast<uint8_t>(value.kind)); boolean(value.enabled); u8(value.controlChannel);
        u32(value.limitPermille); boolean(value.reversed); u32(value.defaultLineLengthMillimetres);
    }
    void endpoint(SocketEndpoint value) noexcept { id(value.part); u64(value.socket.value()); }
    void module(const PartModule& parameters) noexcept {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, StructureModule>) u8(1);
            else if constexpr (std::is_same_v<T, FlotationModule>) { u8(2); for (auto coefficient : value.dragCoefficients) real(coefficient); }
            else if constexpr (std::is_same_v<T, EngineModule>) { u8(3); u64(value.shaft.value()); real(value.maximumPowerWatts); real(value.maximumTorqueNewtonMetres); }
            else if constexpr (std::is_same_v<T, PropellerModule>) {
                u8(4); u64(value.shaft.value()); frame(value.forceFrame); real(value.maximumThrustNewtons); real(value.requiredPowerWatts);
            } else if constexpr (std::is_same_v<T, HelmModule>) { u8(5); frame(value.operatorFrame); real(value.maximumSteeringRadians); }
            else if constexpr (std::is_same_v<T, WinchModule>) {
                u8(6); u64(value.line.value()); real(value.minimumLengthMetres); real(value.maximumLengthMetres);
                real(value.reelSpeedMetresPerSecond); real(value.maximumForceNewtons);
            } else if constexpr (std::is_same_v<T, TowEyeModule>) { u8(7); u64(value.eye.value()); }
            else if constexpr (std::is_same_v<T, CargoCradleModule>) {
                u8(8); u64(value.latch.value()); real(value.maximumCargoMassKg); real(value.captureDistanceMetres);
                real(value.captureAngleRadians); real(value.captureSpeedMetresPerSecond); real(value.captureAngularSpeedRadiansPerSecond);
            } else if constexpr (std::is_same_v<T, BraceModule>) { u8(9); real(value.loadTransferFactor); }
            else if constexpr (std::is_same_v<T, BallastModule>) u8(10);
            else {
                static_assert(std::is_same_v<T, RepairModule>);
                u8(11); real(value.reachMetres); real(value.healthFractionPerSecond); u32(value.materialUnitsPerFullHealth);
            }
        }, parameters);
    }
    void definition(const PartDefinition& value) noexcept {
        key(value.key); u32(value.permittedRotationMask); bounds(value.footprint);
        count(value.solidOccupancy.size()); for (const auto& item : value.solidOccupancy) box(item);
        count(value.collision.size()); for (const auto& item : value.collision) box(item);
        real(value.mass.dryMassKg); real(value.mass.localCenterOfMass.x); real(value.mass.localCenterOfMass.y); real(value.mass.localCenterOfMass.z);
        for (auto item : value.mass.inertia.elements) real(item);
        count(value.buoyancy.size()); for (const auto& item : value.buoyancy) { box(item.box); u8(static_cast<uint8_t>(item.kind)); }
        count(value.sockets.size());
        for (const auto& socket : value.sockets) {
            u64(socket.id.value()); u8(static_cast<uint8_t>(socket.family)); u8(static_cast<uint8_t>(socket.role));
            u32(socket.profile); frame(socket.frame); u8(socket.connectionCapacity); bounds(socket.clearance); strength(socket.strength);
        }
        strength(value.strength); module(value.module);
    }
    void profile(const AssemblyCompileProfile& value) noexcept {
        count(value.mass.parts); count(value.mass.roots); u32(static_cast<uint32_t>(value.mass.radiusTicks));
        count(value.collision.inputBoxes); count(value.collision.cells); count(value.collision.faces); count(value.collision.scratchPieces);
        u64(value.collision.clipTests); u32(static_cast<uint32_t>(value.collision.radiusTicks));
        count(value.buoyancy.inputBoxes); count(value.buoyancy.cells); count(value.buoyancy.references); count(value.buoyancy.scratchPieces);
        u64(value.buoyancy.clipTests); u64(value.buoyancy.referenceWrites); u32(static_cast<uint32_t>(value.buoyancy.radiusTicks));
        count(value.functions.modules); count(value.functions.sockets); count(value.functions.connections); count(value.functions.frames);
    }
};

core::Sha256Digest identity(const BuildSnapshot& input, const PartCatalog& catalog,
    const AssemblyFunctionPlan& compiled, const AssemblyCompileProfile& profile) noexcept {
    PhysicalHash stream;
    stream.hash.string("Voxys.CompiledAssembly.inputs.v1"); // ASCII, no NUL terminator.
    stream.u32(kAssemblyCompilerVersion); stream.u32(kTicksPerMetre); stream.profile(profile);
    stream.id(input.id); stream.u64(input.revision.value());
    std::array<const PartInstance*, kMaximumBuildParts> ordered{};
    std::array<const PartDefinition*, kMaximumBuildParts> used{};
    for (size_t i = 0; i < input.parts.size(); ++i) { ordered[i] = &input.parts[i]; used[i] = catalog.lookup(input.parts[i].definition).definition; }
    const auto parts = std::span{ordered}.first(input.parts.size());
    std::sort(parts.begin(), parts.end(), [](const auto* a, const auto* b) { return a->id < b->id; });
    stream.count(parts.size());
    for (const auto* part : parts) {
        stream.id(part->id); stream.key(part->definition); stream.frame(part->placement); stream.u32(part->health); stream.settings(part->settings);
    }
    stream.count(compiled.connections().size());
    for (const auto& binding : compiled.connections()) {
        const auto& connection = binding.definition;
        stream.id(connection.id); stream.endpoint(connection.a); stream.endpoint(connection.b); stream.u8(static_cast<uint8_t>(connection.kind));
        stream.boolean(connection.enabled); stream.u32(connection.damage); stream.strength(connection.strength);
        stream.u32(connection.minimumLengthMillimetres); stream.u32(connection.maximumLengthMillimetres); stream.u32(connection.restLengthMillimetres);
    }
    auto definitions = std::span{used}.first(input.parts.size());
    std::sort(definitions.begin(), definitions.end(), [](const auto* a, const auto* b) { return a->key < b->key; });
    const auto end = std::unique(definitions.begin(), definitions.end(), [](const auto* a, const auto* b) { return a->key == b->key; });
    definitions = definitions.first(static_cast<size_t>(end - definitions.begin()));
    stream.count(definitions.size()); for (const auto* definition : definitions) stream.definition(*definition);
    return stream.hash.finish();
}
}

std::optional<CompiledAssembly> CompiledAssembly::compile(
    const BuildSnapshot& input, const PartCatalog& catalog, AssemblyFunctionIssue& issue, AssemblyCompileProfile profile) {
    auto functions = AssemblyFunctionPlan::compile(input, catalog, issue, profile.mass, profile.collision, profile.buoyancy, profile.functions);
    if (!functions) return std::nullopt;
    const auto digest = identity(input, catalog, *functions, profile);
    return CompiledAssembly{std::move(*functions), profile, digest};
}
} // namespace voxy::game::construction

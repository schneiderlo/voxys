#include "physics/physics_world.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using voxy::physics::BackendType;
using voxy::physics::PhysicsInitContext;
using voxy::physics::PhysicsWorld;

constexpr std::string_view kReviewedBaselineCommit = "2dbbb6d";
constexpr std::string_view kBox3DCommit =
    "d421e45c828f6f853a145f726f0b9425d31146eb";

struct Options {
    BackendType backend = BackendType::JoltLegacy;
    std::string backendName = "jolt_legacy";
    std::string scene = "junkyard";
    std::string format = "json";
    std::string outputPath;
    uint32_t bodies = 10'000;
    uint32_t frames = 120;
    uint32_t warmupFrames = 30;
    uint32_t workers = 1;
    bool water = false;
};

struct Summary {
    uint64_t semanticHash = 0;
    uint32_t residentBodies = 0;
    uint32_t activeBodies = 0;
    uint32_t workerConcurrency = 0;
    size_t persistentBytes = 0;
    size_t scratchBytes = 0;
    double stepP50Ms = 0.0;
    double stepP95Ms = 0.0;
    double stepP99Ms = 0.0;
    double simulationAverageMs = 0.0;
    double waterAverageMs = 0.0;
    double snapshotAverageMs = 0.0;
};

[[nodiscard]] bool parseUint(std::string_view text, uint32_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

void printHelp(std::ostream& output) {
    output
        << "Usage: physics_benchmark [options]\n"
        << "  --backend jolt|box3d\n"
        << "  --scene snapshot|large_pyramid|rain_contact_churn|junkyard|high_degree_overflow\n"
        << "  --bodies N        Resident body count (default 10000)\n"
        << "  --frames N        Measured fixed ticks (default 120)\n"
        << "  --warmup N        Warmup fixed ticks (default 30)\n"
        << "  --workers N       Total CPU concurrency (default 1)\n"
        << "  --water           Enable the Voxys water extension\n"
        << "  --format json|csv Machine-readable output format\n"
        << "  --output PATH     Write output to a file instead of stdout\n";
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto next = [&]() -> std::string_view {
            if (index + 1 >= argc) {
                return {};
            }
            return argv[++index];
        };

        if (argument == "--help" || argument == "-h") {
            printHelp(std::cout);
            return false;
        }
        if (argument == "--water") {
            options.water = true;
            continue;
        }
        if (argument == "--backend") {
            const std::string_view value = next();
            if (value == "jolt" || value == "jolt_legacy") {
                options.backend = BackendType::JoltLegacy;
                options.backendName = "jolt_legacy";
            } else if (value == "box3d" || value == "box3d_reference") {
                options.backend = BackendType::Box3DReference;
                options.backendName = "box3d_reference";
            } else {
                std::cerr << "Unknown backend: " << value << '\n';
                return false;
            }
            continue;
        }
        if (argument == "--scene") {
            options.scene = std::string(next());
            continue;
        }
        if (argument == "--format") {
            options.format = std::string(next());
            if (options.format != "json" && options.format != "csv") {
                std::cerr << "Format must be json or csv\n";
                return false;
            }
            continue;
        }
        if (argument == "--output") {
            options.outputPath = std::string(next());
            continue;
        }

        uint32_t* destination = nullptr;
        if (argument == "--bodies") destination = &options.bodies;
        else if (argument == "--frames") destination = &options.frames;
        else if (argument == "--warmup") destination = &options.warmupFrames;
        else if (argument == "--workers") destination = &options.workers;
        if (destination != nullptr) {
            const std::string_view value = next();
            if (!parseUint(value, *destination)) {
                std::cerr << "Invalid integer for " << argument << ": "
                          << value << '\n';
                return false;
            }
            continue;
        }

        std::cerr << "Unknown option: " << argument << '\n';
        return false;
    }

    if (options.bodies == 0 || options.frames == 0 || options.workers == 0) {
        std::cerr << "Bodies, frames, and workers must be non-zero\n";
        return false;
    }
    if (options.bodies > 16'384) {
        std::cerr << "CPU baseline capacity is 16384 bodies\n";
        return false;
    }
    if (options.scene == "high_degree_overflow" && options.bodies > 512) {
        std::cerr << "high_degree_overflow is intentionally capped at 512 bodies\n";
        return false;
    }
    const std::array<std::string_view, 5> scenes{
        "snapshot", "large_pyramid", "rain_contact_churn", "junkyard",
        "high_degree_overflow"};
    if (std::find(scenes.begin(), scenes.end(), options.scene)
        == scenes.end()) {
        std::cerr << "Unknown scene: " << options.scene << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] uint32_t xorshift32(uint32_t& state) noexcept {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

[[nodiscard]] float randomSigned(uint32_t& state) noexcept {
    return static_cast<float>(xorshift32(state) & 0x00ff'ffffu)
        / static_cast<float>(0x007f'ffffu) - 1.0f;
}

[[nodiscard]] bool addBody(PhysicsWorld& world,
                           PhysicsWorld::ThrowableShape shape,
                           const glm::vec3& position,
                           const glm::vec3& velocity = {}) {
    return world.throwBody(shape, position, velocity);
}

[[nodiscard]] bool populateScene(PhysicsWorld& world,
                                 const Options& options,
                                 std::vector<uint16_t>& terrainStorage) {
    using Shape = PhysicsWorld::ThrowableShape;
    constexpr uint32_t shapeCount = static_cast<uint32_t>(Shape::Count);

    if (options.scene != "snapshot") {
        constexpr uint32_t terrainSize = 512;
        terrainStorage.assign(
            static_cast<size_t>(terrainSize) * terrainSize, 32'768u);
        if (!world.setTerrain(terrainStorage, terrainSize, terrainSize, 32.0f,
                              1.0f)) {
            return false;
        }
    }

    if (options.scene == "snapshot") {
        constexpr uint32_t columns = 128;
        for (uint32_t index = 0; index < options.bodies; ++index) {
            if (!addBody(world, static_cast<Shape>(index % shapeCount),
                         {static_cast<float>(index % columns) * 3.0f,
                          300.0f,
                          static_cast<float>(index / columns) * 3.0f})) {
                return false;
            }
        }
        return true;
    }

    if (options.scene == "large_pyramid") {
        uint32_t row = 0;
        uint32_t rowStart = 0;
        while (rowStart + row + 1u < options.bodies) {
            rowStart += ++row;
        }
        const uint32_t maximumRow = row;
        uint32_t created = 0;
        for (uint32_t y = 0; created < options.bodies; ++y) {
            const uint32_t width = std::max(maximumRow + 1u - y, 1u);
            for (uint32_t x = 0; x < width && created < options.bodies;
                 ++x, ++created) {
                const float center = 0.5f * static_cast<float>(width - 1u);
                if (!addBody(world, Shape::Cube,
                             {(static_cast<float>(x) - center) * 1.12f,
                              0.56f + static_cast<float>(y) * 1.12f,
                              0.0f})) {
                    return false;
                }
            }
        }
        return true;
    }

    if (options.scene == "rain_contact_churn") {
        constexpr uint32_t columns = 64;
        for (uint32_t index = 0; index < options.bodies; ++index) {
            const uint32_t x = index % columns;
            const uint32_t z = (index / columns) % columns;
            const uint32_t layer = index / (columns * columns);
            if (!addBody(
                    world, static_cast<Shape>(index % shapeCount),
                    {(static_cast<float>(x) - 31.5f) * 1.7f,
                     8.0f + static_cast<float>(layer) * 2.0f
                         + static_cast<float>(index % 11u) * 0.19f,
                     (static_cast<float>(z) - 31.5f) * 1.7f},
                    {0.2f * static_cast<float>(index % 3u), -1.0f, 0.0f})) {
                return false;
            }
        }
        return true;
    }

    if (options.scene == "high_degree_overflow") {
        for (uint32_t index = 0; index < options.bodies; ++index) {
            // Deliberate common overlap: this stresses pair/contact capacity
            // and serves as the CPU oracle for later graph-color overflow.
            const float jitter = 0.002f * static_cast<float>(index % 17u);
            if (!addBody(world, Shape::Sphere,
                         {jitter, 1.0f + jitter, -jitter})) {
                return false;
            }
        }
        return true;
    }

    uint32_t randomState = 0x6d2b79f5u;
    for (uint32_t index = 0; index < options.bodies; ++index) {
        const glm::vec3 position{
            90.0f * randomSigned(randomState),
            2.0f + 30.0f * (0.5f + 0.5f * randomSigned(randomState)),
            90.0f * randomSigned(randomState)};
        const glm::vec3 velocity{
            2.0f * randomSigned(randomState),
            -1.0f,
            2.0f * randomSigned(randomState)};
        if (!addBody(world, static_cast<Shape>(index % shapeCount),
                     position, velocity)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t hashSnapshots(
    std::span<const PhysicsWorld::DynamicBodySnapshot> snapshots) {
    uint64_t hash = 14695981039346656037ull;
    const auto append = [&hash](uint32_t word) {
        for (uint32_t shift = 0; shift < 32u; shift += 8u) {
            hash ^= static_cast<uint8_t>(word >> shift);
            hash *= 1099511628211ull;
        }
    };
    for (const auto& body : snapshots) {
        append(static_cast<uint32_t>(body.shape));
        for (float value : {
                 body.position.x, body.position.y, body.position.z,
                 body.rotation.x, body.rotation.y, body.rotation.z,
                 body.rotation.w, body.dimensions.x, body.dimensions.y,
                 body.dimensions.z}) {
            append(std::bit_cast<uint32_t>(value));
        }
        append(body.active ? 1u : 0u);
    }
    return hash;
}

[[nodiscard]] double percentile(std::vector<double> values,
                                double fraction) {
    std::sort(values.begin(), values.end());
    const size_t rank = static_cast<size_t>(
        std::ceil(fraction * static_cast<double>(values.size())));
    return values[std::min(std::max<size_t>(rank, 1u) - 1u,
                           values.size() - 1u)];
}

[[nodiscard]] Summary runBenchmark(PhysicsWorld& world,
                                   const Options& options) {
    for (uint32_t frame = 0; frame < options.warmupFrames; ++frame) {
        world.update(1.0f / 60.0f);
        (void)world.dynamicBodies();
    }

    std::vector<double> stepSamples;
    stepSamples.reserve(options.frames);
    double simulationSum = 0.0;
    double waterSum = 0.0;
    double snapshotSum = 0.0;
    std::vector<PhysicsWorld::DynamicBodySnapshot> snapshots;
    for (uint32_t frame = 0; frame < options.frames; ++frame) {
        const auto stepStart = std::chrono::steady_clock::now();
        world.update(1.0f / 60.0f);
        stepSamples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - stepStart).count());
        const auto stepStats = world.lastStepStats();
        simulationSum += stepStats.simulationMs;
        waterSum += stepStats.waterMs;

        const auto snapshotStart = std::chrono::steady_clock::now();
        snapshots = world.dynamicBodies();
        snapshotSum += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - snapshotStart).count();
    }

    const auto stats = world.stats();
    Summary summary;
    summary.semanticHash = hashSnapshots(snapshots);
    summary.residentBodies = stats.residentBodies;
    summary.activeBodies = stats.activeBodies;
    summary.workerConcurrency = stats.workerConcurrency;
    summary.persistentBytes = stats.estimatedPersistentBytes;
    summary.scratchBytes = stats.scratchBytes;
    summary.stepP50Ms = percentile(stepSamples, 0.50);
    summary.stepP95Ms = percentile(stepSamples, 0.95);
    summary.stepP99Ms = percentile(stepSamples, 0.99);
    summary.simulationAverageMs = simulationSum
        / static_cast<double>(options.frames);
    summary.waterAverageMs = waterSum
        / static_cast<double>(options.frames);
    summary.snapshotAverageMs = snapshotSum
        / static_cast<double>(options.frames);
    return summary;
}

[[nodiscard]] std::string asJson(const Options& options,
                                 const Summary& summary) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema\": \"voxys.physics_benchmark.v1\",\n"
           << "  \"reviewed_baseline_commit\": \""
           << kReviewedBaselineCommit << "\",\n"
           << "  \"box3d_commit\": \"" << kBox3DCommit << "\",\n"
           << "  \"backend\": \"" << options.backendName << "\",\n"
           << "  \"scene\": \"" << options.scene << "\",\n"
           << "  \"requested_bodies\": " << options.bodies << ",\n"
           << "  \"resident_bodies\": " << summary.residentBodies << ",\n"
           << "  \"active_bodies\": " << summary.activeBodies << ",\n"
           << "  \"frames\": " << options.frames << ",\n"
           << "  \"warmup_frames\": " << options.warmupFrames << ",\n"
           << "  \"worker_concurrency\": "
           << summary.workerConcurrency << ",\n"
           << "  \"water_enabled\": "
           << (options.water ? "true" : "false") << ",\n"
           << "  \"step_p50_ms\": " << summary.stepP50Ms << ",\n"
           << "  \"step_p95_ms\": " << summary.stepP95Ms << ",\n"
           << "  \"step_p99_ms\": " << summary.stepP99Ms << ",\n"
           << "  \"simulation_avg_ms\": "
           << summary.simulationAverageMs << ",\n"
           << "  \"water_avg_ms\": " << summary.waterAverageMs << ",\n"
           << "  \"snapshot_avg_ms\": "
           << summary.snapshotAverageMs << ",\n"
           << "  \"persistent_bytes\": " << summary.persistentBytes << ",\n"
           << "  \"scratch_bytes\": " << summary.scratchBytes << ",\n"
           << "  \"semantic_hash_fnv64\": \"0x" << std::hex
           << summary.semanticHash << std::dec << "\"\n"
           << "}\n";
    return output.str();
}

[[nodiscard]] std::string asCsv(const Options& options,
                                const Summary& summary) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6)
           << "schema,reviewed_baseline_commit,box3d_commit,backend,scene,"
              "requested_bodies,resident_bodies,active_bodies,frames,"
              "warmup_frames,worker_concurrency,water_enabled,step_p50_ms,"
              "step_p95_ms,step_p99_ms,simulation_avg_ms,water_avg_ms,"
              "snapshot_avg_ms,persistent_bytes,scratch_bytes,semantic_hash_fnv64\n"
           << "voxys.physics_benchmark.v1," << kReviewedBaselineCommit << ','
           << kBox3DCommit << ',' << options.backendName << ','
           << options.scene << ',' << options.bodies << ','
           << summary.residentBodies << ',' << summary.activeBodies << ','
           << options.frames << ',' << options.warmupFrames << ','
           << summary.workerConcurrency << ','
           << (options.water ? "true" : "false") << ','
           << summary.stepP50Ms << ',' << summary.stepP95Ms << ','
           << summary.stepP99Ms << ',' << summary.simulationAverageMs << ','
           << summary.waterAverageMs << ',' << summary.snapshotAverageMs
           << ',' << summary.persistentBytes << ',' << summary.scratchBytes
           << ",0x" << std::hex << summary.semanticHash << std::dec << '\n';
    return output.str();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        return argc > 1 && (std::string_view(argv[1]) == "--help"
                            || std::string_view(argv[1]) == "-h")
            ? 0 : 2;
    }

    PhysicsInitContext context;
    context.requestedBackend = options.backend;
    if (options.backend == BackendType::JoltLegacy && options.workers > 1) {
        context.joltJobSystem = voxy::physics::JoltJobSystemMode::ThreadPool;
        context.joltWorkerThreads = options.workers - 1u;
    }
    context.box3dWorkerThreads = options.workers;

    PhysicsWorld world;
    if (!world.initialize(context)) {
        std::cerr << "Failed to initialize " << options.backendName << '\n';
        return 1;
    }
    std::vector<uint16_t> terrainStorage;
    if (!populateScene(world, options, terrainStorage)) {
        std::cerr << "Failed to populate scene " << options.scene << '\n';
        return 1;
    }
    world.setWaterPlane(5.0f, options.water);

    const Summary summary = runBenchmark(world, options);
    const std::string report = options.format == "csv"
        ? asCsv(options, summary) : asJson(options, summary);
    if (options.outputPath.empty()) {
        std::cout << report;
    } else {
        std::ofstream output(options.outputPath,
                             std::ios::out | std::ios::trunc);
        if (!output) {
            std::cerr << "Cannot open output: " << options.outputPath << '\n';
            return 1;
        }
        output << report;
    }
    return 0;
}

#include "server/world_coordinator.hpp"
#include <algorithm>
#include <chrono>
#include <charconv>
#include <stdexcept>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <new>
#include <limits>
#include <cmath>
#include <string_view>
#include <sys/resource.h>
// Single-threaded benchmark: count only allocations inside the measured query.
namespace {
bool countAllocations = false;
size_t allocations = 0;
uint32_t unsignedArgument(const char* text) {
    const std::string_view value(text);
    uint32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("expected a nonnegative 32-bit integer");
    return result;
}
double latencyArgument(const char* text) {
    const std::string_view value(text);
    double result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
        || !std::isfinite(result) || result <= 0)
        throw std::invalid_argument("expected a finite positive p95 limit");
    return result;
}
}
void* operator new(size_t bytes) {
    if (void* memory = std::malloc(std::max(bytes, size_t{1}))) {
        if (countAllocations) ++allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
using namespace voxy;
int main(int argc, char** argv) try {
    if (argc > 8) throw std::invalid_argument("too many arguments");
    const unsigned islands = argc > 1 ? unsignedArgument(argv[1]) : 256;
    const unsigned bodies = argc > 2 ? unsignedArgument(argv[2]) : 128;
    const unsigned iterations = argc > 3 ? unsignedArgument(argv[3]) : 200;
    const size_t maximumAllocations = argc > 4 ? unsignedArgument(argv[4])
        : std::numeric_limits<size_t>::max();
    const double maximumP95 = argc > 5 ? latencyArgument(argv[5])
        : std::numeric_limits<double>::infinity();
    if (islands < 3 || islands > 4096 || bodies == 0 || bodies > 4096
        || iterations == 0 || iterations > 100000 || maximumP95 <= 0
        || std::isnan(maximumP95)) return 2;
    if (argc > 6 && std::string_view(argv[6]) != "sparse"
        && std::string_view(argv[6]) != "dense")
        throw std::invalid_argument("layout must be sparse or dense");
    const bool dense = argc > 6 && std::string_view(argv[6]) == "dense";
    const unsigned pairCapacity = argc > 7 ? unsignedArgument(argv[7]) : 65536;
    if (pairCapacity == 0 || pairCapacity > 65536) return 2;
    server::WorldCoordinator coordinator({.maximumCrossWorkerPairs = pairCapacity});
    for (unsigned w = 0; w < 4; ++w)
        if (!coordinator.registerWorker({w, 1000000, 0, true})) return 2;
    for (unsigned i = 1; i <= islands; ++i) {
        server::IslandDescriptor island;
        island.islandId = i; island.workerId = i % 4; island.loadUnits = 1;
        for (unsigned b = 1; b <= bodies; ++b) {
            physics::deterministic::LockstepBody body;
            body.identity = {b, 1, 3, 0};
            body.sectorRadius = {0, 0, 0, 2048};
            body.positionInvMass = {int(b * 8192), 8192, 0, 65536};
            island.checkpoint.bodies.push_back(body);
        }
        if (!coordinator.registerIsland(std::move(island))) return 3;
        // Three adjacent overlapping proxies; sparse cross-worker workload.
        const int64_t x = dense ? 0 : int64_t(i) * 8192;
        if (!coordinator.publishBoundaryProxy({i, i % 4, 1, 10, 5,
                {x, 0, 0}, {x + 16384, 100, 100}})) return 4;
    }
    // Independent golden: sparse boxes intersect their next two neighbors;
    // dense boxes all intersect. Modulo-four workers and the first-K bound
    // determine the exact canonical output, including touching faces.
    std::vector<server::IslandPair> expected;
    for (uint64_t first = 1; first <= islands && expected.size() < pairCapacity; ++first)
        for (uint64_t second = first + 1;
             second <= (dense ? islands : std::min<uint64_t>(first + 2, islands))
                 && expected.size() < pairCapacity; ++second)
            if (first % 4 != second % 4) expected.push_back({first, second});
    if (coordinator.crossWorkerPairs(12) != expected) return 5;
    for (unsigned i = 0; i < 10; ++i)
        if (coordinator.crossWorkerPairs(12) != expected) return 5;
    std::vector<double> samples;
    samples.reserve(iterations);
    uint64_t hash = 14695981039346656037ull;
    size_t peakQueryAllocations = 0;
    for (unsigned i = 0; i < iterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        allocations = 0;
        countAllocations = true;
        const auto pairs = coordinator.crossWorkerPairs(12);
        countAllocations = false;
        peakQueryAllocations = std::max(peakQueryAllocations, allocations);
        samples.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
        if (pairs != expected) return 6;
    }
    for (const auto& pair : expected) for (uint64_t word : {pair.first, pair.second})
        for (unsigned shift = 0; shift < 64; shift += 8)
            hash = (hash ^ ((word >> shift) & 255)) * 1099511628211ull;
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    std::sort(samples.begin(), samples.end());
    const auto p = [&](size_t percent) { return samples[(iterations * percent + 99) / 100 - 1]; };
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    std::cout << "islands=" << islands << " bodies_per_island=" << bodies
        << " layout=" << (dense ? "dense" : "sparse")
        << " pair_capacity=" << pairCapacity << " iterations=" << iterations << " pairs=" << expected.size()
        << " hash=" << std::hex << hash << std::dec
        << " p50_ms=" << p(50) << " p95_ms=" << p(95) << " p99_ms=" << p(99)
        << " queries_per_second=" << iterations * 1000 / total
        << " max_rss_kib=" << usage.ru_maxrss
        << " max_query_allocations=" << peakQueryAllocations << '\n';
    return peakQueryAllocations > maximumAllocations || p(95) > maximumP95 ? 7 : 0;
}
catch (const std::exception& error) {
    std::cerr << "coordinator_benchmark: " << error.what() << '\n';
    return 2;
}

// Runs the production loader on the shipped shore and original landscape.
// Build for native or WASM/Node; no graphics adapter is required.
#include "terrain/compression.hpp"
#include "core/log.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace voxy::terrain;
    voxy::log::setColorEnabled(false);
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: check_lego_assets shore.ldh source8192.ldh [source2048.ldh]\n";
        return 2;
    }
    auto shore = decompressFromFile(argv[1], 256, 256);
    if (!shore) { std::cerr << "Shore: " << errorToString(shore.error()) << '\n'; return 1; }
    auto source = decompressFromFile(argv[2], 8192, 8192);
    if (!source) { std::cerr << "Source: " << errorToString(source.error()) << '\n'; return 1; }
    for (size_t z = 0; z < 256; ++z) for (size_t x = 0; x < 256; ++x) {
        if (shore.value().data[z * 256 + x] != source.value().data[(7424 + z) * 8192 + 2816 + x]) {
            std::cerr << "Shore sample differs from source at " << x << ',' << z << '\n';
            return 1;
        }
    }
    if (argc == 4) {
        auto smaller = decompressFromFile(argv[3], 2048, 2048);
        if (!smaller) { std::cerr << "2048 source: " << errorToString(smaller.error()) << '\n'; return 1; }
    }
    std::cout << "PASS: production loader accepts shore and existing terrain; all 65,536 shore samples match the source crop\n";
    return 0;
}

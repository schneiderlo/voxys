// ═══════════════════════════════════════════════════════════════════════════════
// ldh_tool - Command-Line Heightmap Compression Utility
// ═══════════════════════════════════════════════════════════════════════════════
// A tool for compressing and decompressing heightmaps using the LDH format.
//
// Usage:
//   ldh_tool compress <input> <output.ldh> [options]
//   ldh_tool decompress <input.ldh> <output> [options]
//   ldh_tool validate <input.ldh>
//   ldh_tool info <input.ldh>
//
// See --help for full usage information.
// ═══════════════════════════════════════════════════════════════════════════════

#include "terrain/compression.hpp"
#include "terrain/heightmap.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// Image loading libraries
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

namespace fs = std::filesystem;
using namespace voxy::terrain;

// ═══════════════════════════════════════════════════════════════════════════════
// Command-Line Argument Parsing
// ═══════════════════════════════════════════════════════════════════════════════

struct Options {
    std::string command;
    std::string inputPath;
    std::string outputPath;
    
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t upscaleWidth = 0;
    uint32_t upscaleHeight = 0;
    int zstdLevel = 19;
    bool addChecksum = false;
    bool verbose = false;
    bool benchmark = false;
    bool help = false;
};

void printUsage(const char* progName) {
    std::cout << R"(
ldh_tool - LDH Heightmap Compression Utility

USAGE:
    )" << progName << R"( <command> [options] <args...>

COMMANDS:
    compress <input> <output.ldh>   Compress a heightmap to LDH format
    decompress <input.ldh> <output> Decompress an LDH file to raw/PNG
    validate <input.ldh>            Validate an LDH file
    info <input.ldh>                Display LDH file information

OPTIONS:
    --width <N>     Input width in samples (required for RAW input)
    --height <N>    Input height in samples (required for RAW input)
    --upscale <W> <H> Upscale/resize input to WxH before compression
    --level <N>     zstd compression level (1-22, default: 19)
    --checksum      Add CRC32 checksum to output
    --verbose       Enable verbose output
    --benchmark     Show detailed timing information
    --help, -h      Show this help message

SUPPORTED FORMATS:
    Input:  .raw (16-bit), .r16 (16-bit), .png (16-bit grayscale)
    Output: .ldh (compressed), .raw (16-bit), .r16 (16-bit)

EXAMPLES:
    # Compress a 16-bit PNG heightmap
    ldh_tool compress terrain.png terrain.ldh --checksum

    # Compress a raw heightmap (must specify dimensions)
    ldh_tool compress terrain.raw terrain.ldh --width 8192 --height 8192

    # Decompress to raw format
    ldh_tool decompress terrain.ldh terrain_out.raw

    # Show file info
    ldh_tool info terrain.ldh

    # Validate file integrity
    ldh_tool validate terrain.ldh
)";
}

std::optional<Options> parseArgs(int argc, char* argv[]) {
    Options opts;
    
    if (argc < 2) {
        printUsage(argv[0]);
        return std::nullopt;
    }
    
    int i = 1;
    
    // Check for help first
    for (int j = 1; j < argc; ++j) {
        if (std::strcmp(argv[j], "--help") == 0 || std::strcmp(argv[j], "-h") == 0) {
            opts.help = true;
            return opts;
        }
    }
    
    // Parse command
    opts.command = argv[i++];
    
    // Parse options and positional args
    while (i < argc) {
        std::string arg = argv[i];
        
        if (arg == "--width" && i + 1 < argc) {
            opts.width = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--height" && i + 1 < argc) {
            opts.height = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--upscale" && i + 2 < argc) {
            opts.upscaleWidth = static_cast<uint32_t>(std::stoul(argv[++i]));
            opts.upscaleHeight = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--level" && i + 1 < argc) {
            opts.zstdLevel = std::stoi(argv[++i]);
        }
        else if (arg == "--checksum") {
            opts.addChecksum = true;
        }
        else if (arg == "--verbose") {
            opts.verbose = true;
        }
        else if (arg == "--benchmark") {
            opts.benchmark = true;
        }
        else if (arg[0] != '-') {
            // Positional argument
            if (opts.inputPath.empty()) {
                opts.inputPath = arg;
            } else if (opts.outputPath.empty()) {
                opts.outputPath = arg;
            }
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return std::nullopt;
        }
        
        ++i;
    }
    
    return opts;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ═══════════════════════════════════════════════════════════════════════════════

std::string formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        return std::to_string(bytes / 1024) + " KB";
    }
    return std::to_string(bytes / (1024 * 1024)) + " MB";
}

std::string formatTime(double ms) {
    if (ms < 1.0) {
        return std::to_string(static_cast<int>(ms * 1000)) + " us";
    }
    if (ms < 1000.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f ms", ms);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f s", ms / 1000.0);
    return buf;
}

bool writeRawFile(const fs::path& path, const std::vector<uint16_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to create output file: " << path << "\n";
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), 
               static_cast<std::streamsize>(data.size() * sizeof(uint16_t)));
    
    return file.good();
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    
    const auto size = file.tellg();
    if (size <= 0) return {};
    
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

struct LoadedImage {
    std::vector<uint16_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::optional<LoadedImage> loadRaw(const fs::path& path, uint32_t width, uint32_t height) {
    auto bytes = readFile(path);
    if (bytes.empty()) {
        std::cerr << "Error: Failed to read file: " << path << "\n";
        return std::nullopt;
    }
    
    if (width == 0 || height == 0) {
        std::cerr << "Error: RAW loading requires explicit dimensions\n";
        return std::nullopt;
    }
    
    const size_t expectedSize = static_cast<size_t>(width) * height * sizeof(uint16_t);
    if (bytes.size() != expectedSize) {
        std::cerr << "Error: RAW size mismatch. Expected " << expectedSize << ", got " << bytes.size() << "\n";
        return std::nullopt;
    }
    
    LoadedImage img;
    img.width = width;
    img.height = height;
    img.data.resize(width * height);
    std::memcpy(img.data.data(), bytes.data(), bytes.size());
    return img;
}

std::optional<LoadedImage> loadPng(const fs::path& path) {
    auto bytes = readFile(path);
    if (bytes.empty()) {
        std::cerr << "Error: Failed to read file: " << path << "\n";
        return std::nullopt;
    }
    
    int w, h, channels;
    if (!stbi_is_16_bit_from_memory(bytes.data(), static_cast<int>(bytes.size()))) {
        std::cerr << "Error: PNG is not 16-bit\n";
        return std::nullopt;
    }
    
    stbi_us* data = stbi_load_16_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 1);
    if (!data) {
        std::cerr << "Error: Failed to decode PNG: " << stbi_failure_reason() << "\n";
        return std::nullopt;
    }
    
    LoadedImage img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.data.resize(img.width * img.height);
    std::memcpy(img.data.data(), data, img.data.size() * sizeof(uint16_t));
    
    stbi_image_free(data);
    return img;
}

std::optional<LoadedImage> loadExr(const fs::path& path) {
    auto bytes = readFile(path);
    if (bytes.empty()) {
        std::cerr << "Error: Failed to read file: " << path << "\n";
        return std::nullopt;
    }
    
    float* out_rgba = nullptr;
    int w, h;
    const char* err = nullptr;
    
    int ret = LoadEXRFromMemory(&out_rgba, &w, &h, bytes.data(), bytes.size(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::cerr << "Error: Failed to load EXR: " << (err ? err : "unknown") << "\n";
        FreeEXRErrorMessage(err);
        return std::nullopt;
    }
    
    LoadedImage img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.data.resize(img.width * img.height);
    
    // Convert RGBA float to grayscale uint16
    // Assuming height is in R channel (or just use first channel)
    // LoadEXRFromMemory returns RGBA float (4 floats per pixel)
    
    float minVal = 1e9f;
    float maxVal = -1e9f;
    
    for (size_t i = 0; i < img.data.size(); ++i) {
        float val = out_rgba[i * 4]; // R channel
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }
    
    float range = maxVal - minVal;
    if (range <= 0.0f) range = 1.0f;
    
    for (size_t i = 0; i < img.data.size(); ++i) {
        float val = out_rgba[i * 4];
        float norm = (val - minVal) / range;
        img.data[i] = static_cast<uint16_t>(std::clamp(norm * 65535.0f, 0.0f, 65535.0f));
    }
    
    free(out_rgba);
    return img;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Compress Command
// ═══════════════════════════════════════════════════════════════════════════════

int cmdCompress(const Options& opts) {
    if (opts.inputPath.empty() || opts.outputPath.empty()) {
        std::cerr << "Error: compress requires <input> and <output.ldh> arguments\n";
        return 1;
    }
    
    const fs::path inputPath = opts.inputPath;
    const fs::path outputPath = opts.outputPath;
    
    if (!fs::exists(inputPath)) {
        std::cerr << "Error: Input file not found: " << inputPath << "\n";
        return 1;
    }
    
    // Load the heightmap
    if (opts.verbose) {
        std::cout << "Reading: " << inputPath << "\n";
    }
    
    Heightmap heightmap;
    std::optional<LoadedImage> img;
    
    const auto ext = inputPath.extension().string();
    if (ext == ".raw" || ext == ".r16") {
        img = loadRaw(inputPath, opts.width, opts.height);
    } else if (ext == ".png") {
        img = loadPng(inputPath);
    } else if (ext == ".exr") {
        img = loadExr(inputPath);
    } else {
        std::cerr << "Error: Unsupported input format: " << ext << "\n";
        return 1;
    }
    
    if (!img) {
        return 1;
    }
    
    // Populate heightmap
    // We use loadRawFromMemory to set the data in the heightmap object
    // Note: We cast vector<uint16_t> to span<const byte>
    std::span<const std::byte> byteSpan(
        reinterpret_cast<const std::byte*>(img->data.data()),
        img->data.size() * sizeof(uint16_t)
    );
    
    auto loadResult = heightmap.loadRawFromMemory(byteSpan, img->width, img->height);
    
    if (!loadResult) {
        std::cerr << "Error: Failed to load heightmap data: " 
                  << errorToString(loadResult.error()) << "\n";
        return 1;
    }
    
    if (opts.verbose) {
        std::cout << "  Dimensions: " << heightmap.getWidth() << " x " 
                  << heightmap.getHeight() << "\n";
        std::cout << "  Samples: " << heightmap.getSampleCount() << "\n";
        std::cout << "  Size: " << formatBytes(heightmap.getSizeBytes()) << "\n";
        
        auto [minH, maxH] = heightmap.getMinMax();
        std::cout << "  Height range: " << minH << " - " << maxH << "\n";
    }
    
    // Upscale if requested
    if (opts.upscaleWidth > 0 && opts.upscaleHeight > 0) {
        if (opts.verbose) {
            std::cout << "Upscaling to " << opts.upscaleWidth << " x " << opts.upscaleHeight << "...\n";
        }
        
        auto resizeResult = heightmap.resize(opts.upscaleWidth, opts.upscaleHeight);
        if (!resizeResult) {
            std::cerr << "Error: Failed to upscale heightmap: " 
                      << errorToString(resizeResult.error()) << "\n";
            return 1;
        }
        
        if (opts.verbose) {
            std::cout << "  New dimensions: " << heightmap.getWidth() << " x " 
                      << heightmap.getHeight() << "\n";
        }
    }
    
    // Set compression options
    CompressionOptions compOpts;
    compOpts.zstdLevel = opts.zstdLevel;
    compOpts.addChecksum = opts.addChecksum;
    
    if (opts.verbose) {
        std::cout << "Compressing with zstd level " << compOpts.zstdLevel;
        if (compOpts.addChecksum) {
            std::cout << " (with checksum)";
        }
        std::cout << "...\n";
    }
    
    // Compress
    auto compressResult = compressToFile(
        heightmap.getData(),
        heightmap.getWidth(),
        heightmap.getHeight(),
        outputPath,
        compOpts
    );
    
    if (!compressResult) {
        std::cerr << "Error: Compression failed: " 
                  << errorToString(compressResult.error()) << "\n";
        return 1;
    }
    
    const auto& stats = compressResult.value();
    
    // Print results
    std::cout << "Compressed: " << inputPath.filename().string() << " -> " 
              << outputPath.filename().string() << "\n";
    std::cout << "  Input:  " << formatBytes(stats.inputSize) << "\n";
    std::cout << "  Output: " << formatBytes(stats.outputSize) << "\n";
    
    char ratioStr[32];
    std::snprintf(ratioStr, sizeof(ratioStr), "%.2fx", stats.compressionRatio);
    std::cout << "  Ratio:  " << ratioStr << "\n";
    
    if (opts.benchmark) {
        std::cout << "  Timing:\n";
        std::cout << "    Encode: " << formatTime(stats.encodeTimeMs) << "\n";
        std::cout << "    zstd:   " << formatTime(stats.zstdTimeMs) << "\n";
        std::cout << "    Total:  " << formatTime(stats.totalTimeMs) << "\n";
    }

    // Verification step
    if (opts.verbose) {
        std::cout << "Verifying compressed data...\n";
    }

    auto decompressResult = decompressFromFile(outputPath);
    if (!decompressResult) {
        std::cerr << "Error: Verification failed (decompression error): " 
                  << errorToString(decompressResult.error()) << "\n";
        return 1;
    }

    const auto& result = decompressResult.value();
    
    // Check dimensions
    if (result.width != heightmap.getWidth() || result.height != heightmap.getHeight()) {
        std::cerr << "Error: Verification failed (dimension mismatch)\n";
        std::cerr << "  Original: " << heightmap.getWidth() << "x" << heightmap.getHeight() << "\n";
        std::cerr << "  Decoded:  " << result.width << "x" << result.height << "\n";
        return 1;
    }

    // Check data equality
    const auto& originalData = heightmap.getData();
    if (result.data.size() != originalData.size()) {
        std::cerr << "Error: Verification failed (size mismatch)\n";
        return 1;
    }

    if (std::memcmp(result.data.data(), originalData.data(), originalData.size() * sizeof(uint16_t)) != 0) {
        std::cerr << "Error: Verification failed (data mismatch)\n";
        // Optional: Find first mismatch
        for (size_t i = 0; i < originalData.size(); ++i) {
            if (result.data[i] != originalData[i]) {
                std::cerr << "  First mismatch at index " << i << ": " 
                          << originalData[i] << " != " << result.data[i] << "\n";
                break;
            }
        }
        return 1;
    }

    if (opts.verbose) {
        std::cout << "Verification successful: Decompressed data matches original.\n";
    }
    
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Decompress Command
// ═══════════════════════════════════════════════════════════════════════════════

int cmdDecompress(const Options& opts) {
    if (opts.inputPath.empty() || opts.outputPath.empty()) {
        std::cerr << "Error: decompress requires <input.ldh> and <output> arguments\n";
        return 1;
    }
    
    const fs::path inputPath = opts.inputPath;
    const fs::path outputPath = opts.outputPath;
    
    if (!fs::exists(inputPath)) {
        std::cerr << "Error: Input file not found: " << inputPath << "\n";
        return 1;
    }
    
    if (opts.verbose) {
        std::cout << "Decompressing: " << inputPath << "\n";
    }
    
    // Decompress
    auto decompressResult = decompressFromFile(inputPath);
    
    if (!decompressResult) {
        std::cerr << "Error: Decompression failed: " 
                  << errorToString(decompressResult.error()) << "\n";
        return 1;
    }
    
    const auto& result = decompressResult.value();
    
    if (opts.verbose) {
        std::cout << "  Dimensions: " << result.width << " x " << result.height << "\n";
        std::cout << "  Samples: " << result.data.size() << "\n";
    }
    
    // Write output file
    const auto ext = outputPath.extension().string();
    
    if (ext == ".raw" || ext == ".r16") {
        if (!writeRawFile(outputPath, result.data)) {
            return 1;
        }
    } else {
        std::cerr << "Error: Unsupported output format: " << ext << "\n";
        std::cerr << "       Supported formats: .raw, .r16\n";
        return 1;
    }
    
    // Print results
    std::cout << "Decompressed: " << inputPath.filename().string() << " -> " 
              << outputPath.filename().string() << "\n";
    std::cout << "  Dimensions: " << result.width << " x " << result.height << "\n";
    std::cout << "  Output: " << formatBytes(result.data.size() * sizeof(uint16_t)) << "\n";
    
    if (opts.benchmark) {
        const auto& stats = result.stats;
        std::cout << "  Timing:\n";
        std::cout << "    zstd:   " << formatTime(stats.zstdTimeMs) << "\n";
        std::cout << "    Decode: " << formatTime(stats.encodeTimeMs) << "\n";
        std::cout << "    Total:  " << formatTime(stats.totalTimeMs) << "\n";
    }
    
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Validate Command
// ═══════════════════════════════════════════════════════════════════════════════

int cmdValidate(const Options& opts) {
    if (opts.inputPath.empty()) {
        std::cerr << "Error: validate requires <input.ldh> argument\n";
        return 1;
    }
    
    const fs::path inputPath = opts.inputPath;
    
    if (!fs::exists(inputPath)) {
        std::cerr << "Error: Input file not found: " << inputPath << "\n";
        return 1;
    }
    
    std::cout << "Validating: " << inputPath << "\n";
    
    // Read header
    auto headerResult = readHeaderFromFile(inputPath);
    if (!headerResult) {
        std::cerr << "INVALID: " << errorToString(headerResult.error()) << "\n";
        return 1;
    }
    
    const auto& header = headerResult.value();
    std::cout << "  Header: OK\n";
    std::cout << "  Dimensions: " << header.width << " x " << header.height << "\n";
    
    // Check file size
    const auto fileSize = fs::file_size(inputPath);
    const auto expectedMinSize = header.expectedFileSize();
    
    if (fileSize < expectedMinSize) {
        std::cerr << "INVALID: File truncated (expected " << expectedMinSize 
                  << " bytes, got " << fileSize << ")\n";
        return 1;
    }
    std::cout << "  File size: OK (" << formatBytes(fileSize) << ")\n";
    
    // Full decompress to verify data integrity
    if (opts.verbose) {
        std::cout << "  Performing full decompression test...\n";
    }
    
    auto decompressResult = decompressFromFile(inputPath);
    if (!decompressResult) {
        std::cerr << "INVALID: Decompression failed: " 
                  << errorToString(decompressResult.error()) << "\n";
        return 1;
    }
    
    const auto& result = decompressResult.value();
    
    // Verify sample count
    const size_t expectedSamples = static_cast<size_t>(header.width) * header.height;
    if (result.data.size() != expectedSamples) {
        std::cerr << "INVALID: Sample count mismatch (expected " << expectedSamples 
                  << ", got " << result.data.size() << ")\n";
        return 1;
    }
    std::cout << "  Data integrity: OK\n";
    
    if (hasFlag(header.getFlags(), LDHFlags::HasChecksum)) {
        std::cout << "  Checksum: VERIFIED\n";
    }
    
    std::cout << "VALID\n";
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Info Command
// ═══════════════════════════════════════════════════════════════════════════════

int cmdInfo(const Options& opts) {
    if (opts.inputPath.empty()) {
        std::cerr << "Error: info requires <input.ldh> argument\n";
        return 1;
    }
    
    const fs::path inputPath = opts.inputPath;
    
    if (!fs::exists(inputPath)) {
        std::cerr << "Error: Input file not found: " << inputPath << "\n";
        return 1;
    }
    
    // Read header
    auto headerResult = readHeaderFromFile(inputPath);
    if (!headerResult) {
        std::cerr << "Error: " << errorToString(headerResult.error()) << "\n";
        return 1;
    }
    
    const auto& header = headerResult.value();
    const auto fileSize = fs::file_size(inputPath);
    
    // Print info
    std::cout << "File: " << inputPath.filename().string() << "\n";
    std::cout << "Magic: LDH1\n";
    std::cout << "Version: " << header.version << "\n";
    std::cout << "Dimensions: " << header.width << " x " << header.height << "\n";
    
    // Flags
    std::cout << "Flags: ";
    auto flags = header.getFlags();
    if (flags == LDHFlags::None) {
        std::cout << "None";
    } else {
        bool first = true;
        if (hasFlag(flags, LDHFlags::SplitBytes)) {
            std::cout << "SPLIT_BYTES";
            first = false;
        }
        if (hasFlag(flags, LDHFlags::HasChecksum)) {
            if (!first) std::cout << " | ";
            std::cout << "HAS_CHECKSUM";
            first = false;
        }
        if (hasFlag(flags, LDHFlags::HasMetadata)) {
            if (!first) std::cout << " | ";
            std::cout << "HAS_METADATA";
        }
    }
    std::cout << "\n";
    
    std::cout << "Low stream: " << formatBytes(header.lowStreamSize) << "\n";
    std::cout << "High stream: " << formatBytes(header.highStreamSize) << "\n";
    std::cout << "Total compressed: " << formatBytes(fileSize) << "\n";
    std::cout << "Uncompressed size: " << formatBytes(header.uncompressedSize()) << "\n";
    
    const double ratio = static_cast<double>(header.uncompressedSize()) / 
                         static_cast<double>(fileSize);
    char ratioStr[32];
    std::snprintf(ratioStr, sizeof(ratioStr), "%.2fx", ratio);
    std::cout << "Compression ratio: " << ratioStr << "\n";
    
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    // Disable logging for CLI tool unless verbose
    voxy::log::setLevel(voxy::log::Level::Error);
    
    auto opts = parseArgs(argc, argv);
    if (!opts) {
        return 1;
    }
    
    if (opts->help) {
        printUsage(argv[0]);
        return 0;
    }
    
    if (opts->verbose) {
        voxy::log::setLevel(voxy::log::Level::Info);
    }
    
    // Dispatch to command handler
    if (opts->command == "compress") {
        return cmdCompress(*opts);
    }
    else if (opts->command == "decompress") {
        return cmdDecompress(*opts);
    }
    else if (opts->command == "validate") {
        return cmdValidate(*opts);
    }
    else if (opts->command == "info") {
        return cmdInfo(*opts);
    }
    else {
        std::cerr << "Unknown command: " << opts->command << "\n";
        std::cerr << "Use --help to see available commands.\n";
        return 1;
    }
}




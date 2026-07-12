# Heightmap Compression Specification

> **Project:** voxy  
> **Version:** 1.0  
> **Status:** Draft

---

## Overview

The voxy project uses a custom heightmap compression pipeline optimized for 16-bit terrain data. This pipeline achieves significant compression ratios (up to 10.7×) while maintaining lossless reconstruction of the original heightmap.

---

## 1. Input Format

### 1.1 Supported Input Types

| Format | Description | Notes |
|--------|-------------|-------|
| RAW | Raw 16-bit unsigned integers | Little-endian, row-major |
| PNG | 16-bit grayscale PNG | Standard PNG decoding |
| EXR | OpenEXR High Dynamic Range | Single channel (Y/R), float32 normalized |

### 1.2 Target Dimensions

| Parameter | Value |
|-----------|-------|
| Primary Resolution | 8192 × 8192 |
| Bit Depth | 16-bit unsigned |
| Total Samples | 67,108,864 |
| Uncompressed Size | 134,217,728 bytes (128 MB) |

### 1.3 Debug/Test Resolutions

For development and testing, smaller heightmaps are supported:

| Resolution | Samples | Uncompressed Size |
|------------|---------|-------------------|
| 512 × 512 | 262,144 | 512 KB |
| 1024 × 1024 | 1,048,576 | 2 MB |
| 2048 × 2048 | 4,194,304 | 8 MB |
| 4096 × 4096 | 16,777,216 | 32 MB |

---

## 2. Compression Pipeline

### 2.1 Stage 1: Planar Predictor

The planar predictor exploits spatial coherence in terrain data by predicting each sample from its already-decoded neighbors.

**Prediction Formula:**

For pixel at position `(x, y)`:

```
If x == 0 and y == 0:
    prediction = 0
    
Else if y == 0:
    prediction = sample[x-1, y]
    
Else if x == 0:
    prediction = sample[x, y-1]
    
Else:
    // Three-point planar prediction
    A = sample[x-1, y]      // Left neighbor
    B = sample[x-1, y-1]    // Diagonal neighbor  
    C = sample[x, y-1]      // Top neighbor
    
    // Plane through A, B, C extrapolated to (x, y)
    prediction = A + C - B
```

**Visual Representation:**
```
    C ─── ?
    │     │
    B ─── A

Where ? = A + C - B (planar extrapolation)
```

**Rationale:**
- Points A, B, C define a plane in (x, y, height) space
- The prediction assumes the terrain continues along this plane
- For smooth terrain, this produces very small residuals
- Edges and discontinuities produce larger residuals but remain bounded

### 2.2 Stage 2: Delta Encoding

The difference between predicted and actual values is stored as a 16-bit delta.

**Delta Calculation:**
```cpp
uint16_t encode_delta(uint16_t actual, uint16_t predicted) {
    // Unsigned subtraction with natural wraparound
    return actual - predicted;
}

uint16_t decode_delta(uint16_t delta, uint16_t predicted) {
    // Unsigned addition with natural wraparound
    return predicted + delta;
}
```

**Properties:**
- Uses C/C++ defined wraparound semantics for `uint16_t`
- No overflow/underflow issues - arithmetic is modulo 2¹⁶
- Signed deltas are implicitly encoded:
  - Small positive deltas: 0x0000 - 0x7FFF
  - Small negative deltas: 0x8000 - 0xFFFF (wraps around)
- Distribution concentrates around 0, making subsequent compression efficient

**Delta Distribution (Typical Terrain):**
```
|delta| = 0:     ~30-40% of samples
|delta| < 256:   ~85-95% of samples
|delta| < 4096:  ~99% of samples
```

### 2.3 Stage 3: Bit-Stream Splitting

To maximize compressibility, 16-bit deltas are split into two streams:

**Stream Layout:**
```
Original delta: [H7 H6 H5 H4 H3 H2 H1 H0 | L7 L6 L5 L4 L3 L2 L1 L0]
                        High byte                   Low byte

Low-byte stream:  [L7 L6 L5 L4 L3 L2 L1 L0] × N samples
High-byte stream: [H7 H6 H5 H4 H3 H2 H1 H0] × N samples
```

**Rationale:**
- Low bytes contain the actual residual magnitude (usually non-zero)
- High bytes are mostly zero for small deltas
- Separating streams allows the compressor to exploit the high-byte regularity
- High-byte stream achieves very high compression ratios

**Alternative Split (Enhanced):**

For even better compression, split at a different bit boundary:

```
Split at bit 10:
  Lower 10 bits: Variable (captures most residuals)
  Upper 6 bits:  Almost always zero

Storage:
  Stream 1: Lower 10 bits packed (10 bits × N samples)
  Stream 2: Upper 6 bits packed (6 bits × N samples)
```

### 2.4 Stage 4: zstd Compression

Both streams are compressed using the zstd library.

**Parameters:**
| Parameter | Value | Notes |
|-----------|-------|-------|
| Compression Level | 19 | High compression, acceptable speed |
| Window Size | Default | Let zstd choose optimal |
| Checksum | Enabled | For data integrity |

**Compression Performance (8192×8192):**

| Configuration | Compressed Size | Ratio |
|---------------|-----------------|-------|
| zstd only | ~45 MB | 3× |
| Planar + zstd | ~30 MB | 4.5× |
| Planar + Split + zstd | ~12.5 MB | 10.7× |

---

## 3. File Format (.ldh)

### 3.1 Header Structure

```cpp
struct LDHHeader {
    uint32_t magic;           // 'LDH1' (0x4C444831)
    uint32_t version;         // Format version (1)
    uint32_t width;           // Heightmap width
    uint32_t height;          // Heightmap height
    uint32_t flags;           // Feature flags
    uint32_t lowStreamSize;   // Compressed low-byte stream size
    uint32_t highStreamSize;  // Compressed high-byte stream size
    uint32_t reserved[9];     // Future use, zero-filled
};
// Total header size: 64 bytes
```

### 3.2 Flags

```cpp
enum LDHFlags : uint32_t {
    LDH_FLAG_NONE          = 0x00,
    LDH_FLAG_SPLIT_BYTES   = 0x01,  // Byte-split encoding used
    LDH_FLAG_SPLIT_10_6    = 0x02,  // 10/6 bit split encoding
    LDH_FLAG_HAS_CHECKSUM  = 0x04,  // CRC32 checksum present
    LDH_FLAG_HAS_METADATA  = 0x08,  // Optional metadata block
};
```

### 3.3 File Layout

```
┌──────────────────────────────┐  Offset 0
│         LDH Header           │  64 bytes
├──────────────────────────────┤  Offset 64
│   Compressed Low Stream      │  lowStreamSize bytes
│      (zstd frame)            │
├──────────────────────────────┤  Offset 64 + lowStreamSize
│   Compressed High Stream     │  highStreamSize bytes
│      (zstd frame)            │
├──────────────────────────────┤  (Optional)
│   Optional Metadata          │  Variable
├──────────────────────────────┤  (Optional)
│   CRC32 Checksum             │  4 bytes
└──────────────────────────────┘
```

---

## 4. Encoding Algorithm

### 4.1 Pseudocode

```cpp
void encode_heightmap(const uint16_t* input, int width, int height,
                      std::vector<uint8_t>& output) {
    // Allocate streams
    std::vector<uint8_t> lowStream(width * height);
    std::vector<uint8_t> highStream(width * height);
    
    // Encode with planar predictor
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t actual = input[y * width + x];
            uint16_t predicted = predict(input, x, y, width);
            uint16_t delta = actual - predicted;  // Wraparound OK
            
            lowStream[y * width + x] = delta & 0xFF;
            highStream[y * width + x] = delta >> 8;
        }
    }
    
    // Compress streams
    auto compressedLow = zstd_compress(lowStream, 19);
    auto compressedHigh = zstd_compress(highStream, 19);
    
    // Write output
    LDHHeader header = {};
    header.magic = 0x4C444831;
    header.version = 1;
    header.width = width;
    header.height = height;
    header.flags = LDH_FLAG_SPLIT_BYTES;
    header.lowStreamSize = compressedLow.size();
    header.highStreamSize = compressedHigh.size();
    
    output.resize(sizeof(header) + compressedLow.size() + compressedHigh.size());
    memcpy(output.data(), &header, sizeof(header));
    memcpy(output.data() + sizeof(header), compressedLow.data(), compressedLow.size());
    memcpy(output.data() + sizeof(header) + compressedLow.size(), 
           compressedHigh.data(), compressedHigh.size());
}

uint16_t predict(const uint16_t* data, int x, int y, int width) {
    if (x == 0 && y == 0) return 0;
    if (y == 0) return data[x - 1];
    if (x == 0) return data[y * width - width];  // data[x, y-1]
    
    uint16_t A = data[y * width + x - 1];        // Left
    uint16_t B = data[(y - 1) * width + x - 1];  // Diagonal
    uint16_t C = data[(y - 1) * width + x];      // Top
    
    return A + C - B;
}
```

### 4.2 Decoding Algorithm

```cpp
void decode_heightmap(const uint8_t* input, size_t inputSize,
                      uint16_t* output, int width, int height) {
    // Parse header
    const LDHHeader* header = reinterpret_cast<const LDHHeader*>(input);
    assert(header->magic == 0x4C444831);
    assert(header->width == width && header->height == height);
    
    // Decompress streams
    const uint8_t* lowCompressed = input + sizeof(LDHHeader);
    const uint8_t* highCompressed = lowCompressed + header->lowStreamSize;
    
    std::vector<uint8_t> lowStream = zstd_decompress(lowCompressed, header->lowStreamSize);
    std::vector<uint8_t> highStream = zstd_decompress(highCompressed, header->highStreamSize);
    
    // Decode with planar predictor
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = y * width + x;
            uint16_t delta = lowStream[idx] | (uint16_t(highStream[idx]) << 8);
            uint16_t predicted = predict(output, x, y, width);  // Uses already-decoded values
            output[idx] = predicted + delta;  // Wraparound OK
        }
    }
}
```

---

## 5. Command-Line Tool

### 5.1 Tool Name

`ldh_tool` (Limited Detail Heightmap Tool)

### 5.2 Usage

```bash
# Compression
ldh_tool compress <input.raw|input.png> <output.ldh> [options]

# Decompression  
ldh_tool decompress <input.ldh> <output.raw|output.png>

# Validation
ldh_tool validate <input.ldh>

# Info
ldh_tool info <input.ldh>
```

### 5.3 Options

| Option | Description |
|--------|-------------|
| `--width <N>` | Input width (required for RAW) |
| `--height <N>` | Input height (required for RAW) |
| `--level <N>` | zstd compression level (1-22, default: 19) |
| `--split-mode <mode>` | `bytes` (default) or `10-6` |
| `--checksum` | Add CRC32 checksum |
| `--verbose` | Verbose output |
| `--benchmark` | Show compression/decompression timing |

### 5.4 Example Sessions

```bash
# Compress a 16-bit PNG heightmap
$ ldh_tool compress terrain_8192.png terrain.ldh --checksum
Reading: terrain_8192.png (8192x8192, 16-bit)
Encoding with planar predictor...
Splitting into byte streams...
Compressing with zstd (level 19)...
  Low stream:  67108864 -> 8234567 bytes (8.15x)
  High stream: 67108864 -> 4123456 bytes (16.28x)
Total: 134217728 -> 12358087 bytes (10.86x)
Writing: terrain.ldh
Done.

# Get info about compressed file
$ ldh_tool info terrain.ldh
File: terrain.ldh
Magic: LDH1
Version: 1
Dimensions: 8192 x 8192
Flags: SPLIT_BYTES | HAS_CHECKSUM
Low stream: 8234567 bytes
High stream: 4123456 bytes
Total compressed: 12358087 bytes
Compression ratio: 10.86x

# Decompress for verification
$ ldh_tool decompress terrain.ldh terrain_verify.raw --benchmark
Decompressing terrain.ldh...
  zstd decompress: 45.2 ms
  Planar decode: 312.4 ms
  Total: 357.6 ms
Writing: terrain_verify.raw (134217728 bytes)
Done.
```

---

## 6. Runtime Decompression

### 6.1 Integration

The runtime decompression is integrated into the application startup:

```cpp
class HeightmapLoader {
public:
    struct LoadResult {
        std::vector<uint16_t> data;
        uint32_t width;
        uint32_t height;
        double loadTimeMs;
    };
    
    static LoadResult loadCompressed(const std::string& path);
    static LoadResult loadRaw(const std::string& path, uint32_t width, uint32_t height);
};
```

### 6.2 GPU Upload

After decompression, the heightmap is uploaded to GPU:

```cpp
void uploadHeightmap(const HeightmapLoader::LoadResult& heightmap) {
    // Create texture descriptor
    wgpu::TextureDescriptor desc = {};
    desc.size = {heightmap.width, heightmap.height, 1};
    desc.format = wgpu::TextureFormat::R16Uint;
    desc.usage = wgpu::TextureUsage::TextureBinding | 
                 wgpu::TextureUsage::CopyDst;
    desc.mipLevelCount = calculateMipLevels(heightmap.width, heightmap.height);
    
    heightmapTexture = device.CreateTexture(&desc);
    
    // Upload base level
    wgpu::ImageCopyTexture dst = {};
    dst.texture = heightmapTexture;
    dst.mipLevel = 0;
    
    wgpu::TextureDataLayout layout = {};
    layout.bytesPerRow = heightmap.width * sizeof(uint16_t);
    layout.rowsPerImage = heightmap.height;
    
    queue.WriteTexture(&dst, heightmap.data.data(), 
                       heightmap.data.size() * sizeof(uint16_t),
                       &layout, &desc.size);
    
    // Generate mip chain (max-height pyramid)
    generateMaxHeightMips();
}
```

### 6.3 Max-Height Mip Generation

The mip chain stores maximum height values for hierarchical ray-casting:

```cpp
void generateMaxHeightMips() {
    // Option 1: CPU generation
    std::vector<uint16_t> prevLevel = baseHeightmap;
    uint32_t w = width, h = height;
    
    for (uint32_t mip = 1; mip < mipLevels; mip++) {
        w /= 2;
        h /= 2;
        std::vector<uint16_t> nextLevel(w * h);
        
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint16_t maxH = 0;
                for (uint32_t dy = 0; dy < 2; dy++) {
                    for (uint32_t dx = 0; dx < 2; dx++) {
                        uint16_t sample = prevLevel[(y*2+dy) * (w*2) + (x*2+dx)];
                        maxH = std::max(maxH, sample);
                    }
                }
                nextLevel[y * w + x] = maxH;
            }
        }
        
        // Upload mip level
        uploadMipLevel(mip, nextLevel, w, h);
        prevLevel = std::move(nextLevel);
    }
    
    // Option 2: Compute shader generation (faster for large heightmaps)
    // See shaders/mip_generate.wgsl
}
```

---

## 7. Memory Budget

### 7.1 Compressed (Disk/Network)

| Component | Size |
|-----------|------|
| LDH Header | 64 bytes |
| Low Stream | ~8 MB |
| High Stream | ~4 MB |
| **Total** | **~12 MB** |

### 7.2 Runtime (RAM)

| Component | Size |
|-----------|------|
| Compressed buffer (temporary) | ~12 MB |
| Decompressed heightmap | 128 MB |
| Peak during decompression | ~140 MB |

### 7.3 GPU (VRAM)

| Component | Size | Notes |
|-----------|------|-------|
| Base mip (8192×8192) | 128 MB | R16Uint |
| Mip 1 (4096×4096) | 32 MB | |
| Mip 2 (2048×2048) | 8 MB | |
| Mip 3 (1024×1024) | 2 MB | |
| Mip 4 (512×512) | 512 KB | |
| Mip 5 (256×256) | 128 KB | |
| Mip 6 (128×128) | 32 KB | |
| Mip 7 (64×64) | 8 KB | |
| Mips 8-13 | <8 KB | |
| **Total** | **~171 MB** | Full mip chain |

---

## 8. Error Handling

### 8.1 Compression Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `LDH_ERR_READ_FAILED` | Cannot read input file | Report file path, permissions |
| `LDH_ERR_INVALID_PNG` | PNG decode failure | Use RAW format instead |
| `LDH_ERR_WRONG_DEPTH` | Not 16-bit | Reject or convert |
| `LDH_ERR_ZSTD_FAILED` | zstd compression error | Report zstd error code |

### 8.2 Decompression Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `LDH_ERR_INVALID_MAGIC` | Not an LDH file | Report actual magic bytes |
| `LDH_ERR_VERSION_UNSUPPORTED` | Future version | Suggest tool update |
| `LDH_ERR_ZSTD_DECOMPRESS` | Corrupt data | Report offset if possible |
| `LDH_ERR_SIZE_MISMATCH` | Truncated file | Report expected vs actual |
| `LDH_ERR_CHECKSUM_FAILED` | Data corruption | Suggest re-download |

---

## 9. Future Enhancements

### 9.1 Streaming Decompression

For very large heightmaps or memory-constrained platforms:
- Decompress in tiles (e.g., 1024×1024 regions)
- Stream from disk/network as needed
- LRU cache for tiles

### 9.2 GPU Decompression

Move decompression to compute shaders:
- Upload compressed data directly
- Decompress in parallel on GPU
- Eliminates CPU-GPU transfer of full heightmap

### 9.3 Lossy Mode

For extreme compression (satellite imagery, etc.):
- Quantize deltas before entropy coding
- Configurable quality parameter
- Typical ratios: 20-50×

